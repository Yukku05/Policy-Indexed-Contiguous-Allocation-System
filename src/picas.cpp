#include "picas/picas.hpp"
#include "picas/fallback.hpp"

#include <cstring>
#include <algorithm>
#include <string>
#include <cstdint>
#include <limits>

namespace picas {

// From safety.cpp
bool safety_validate_and_sanitize(Config& cfg, std::string* why);

// From scavenger.cpp
void scavenger_run_light(LayerState* layers, std::uint32_t num_layers, const ScavengerConfig& cfg);

// --- Global singleton ---
static PICAS* g_alloc = nullptr;

// --- Utilities ---
static inline std::size_t block_total(std::size_t user_size) {
  // total = header + payload aligned to 16
  const std::size_t a = 16;
  const std::size_t x = sizeof(BlockHeader) + user_size;
  return (x + (a - 1)) & ~(a - 1);
}

// Magic used in your file
static constexpr std::uint32_t kMagic = 0x50494341; // 'PICA'

// ---------- Section A: practical helpers (memalign tag) ----------
static inline bool is_pow2_local(std::size_t x) { return x && ((x & (x - 1)) == 0); }

static inline std::byte* align_ptr(std::byte* p, std::size_t a) {
  auto v = reinterpret_cast<std::uintptr_t>(p);
  v = (v + (a - 1)) & ~(a - 1);
  return reinterpret_cast<std::byte*>(v);
}

// Tag placed immediately before the aligned pointer returned to the user.
struct AlignTag {
  std::uint64_t magic;
  void* base;              // base pointer returned by PICAS::malloc (or fallback)
  std::size_t requested;   // user requested size
};

static constexpr std::uint64_t kAlignMagic = 0x50494341414C4947ULL; // "PICAALIG"

// --- PICAS lifecycle ---
PICAS::PICAS(Config cfg)
: cfg_(cfg), policy_(cfg_) {
  std::string why;
  safety_validate_and_sanitize(cfg_, &why);

  num_layers_ = std::max<std::uint32_t>(
      1,
      std::min<std::uint32_t>(cfg_.num_layers, Config::kMaxLayers)
  );

  // Init fallback allocator early (safety)
  fallback_init(cfg_.safety.fallback);

  // Setup halting controller (debug-only)
  halter_.enable(cfg_.enable_debug_pause);
  halter_.set_pause_ms(cfg_.debug_pause_ms);

  tracer_.enable(cfg_.enable_tracing);

  // Reserve a single large OS arena and split among layers
  std::size_t total = 0;
  for (std::uint32_t i = 0; i < num_layers_; ++i) total += cfg_.mem_layers[i].bytes;

  const std::size_t ps = os_page_size();
  total = align_up(total, ps);

  pages_ = os_reserve_and_commit(total);
  layers_.reset(new LayerState[num_layers_]);

  auto* base = reinterpret_cast<std::byte*>(pages_.base);
  std::size_t offset = 0;

  for (std::uint32_t i = 0; i < num_layers_; ++i) {
    std::size_t cap = cfg_.mem_layers[i].bytes;
    cap = align_up(cap, ps);

    LayerState& L = layers_[i];
    L.begin = base + offset;
    L.end   = base + offset + cap;
    L.bump  = L.begin;

    L.capacity_bytes   = cap;
    L.bump_used_bytes  = 0;
    L.live_bytes_est   = 0;

    L.points.mem_tp    = std::min(cfg_.mem_layers[i].mem_tp_bytes, cap);
    L.mem_tp_reached   = (L.points.mem_tp == 0);

    for (auto& bin : L.bins) bin = nullptr;

    offset += cap;
  }
}

PICAS::~PICAS() {
  fallback_shutdown();
  os_release(pages_);
}

void PICAS::set_event_hook(EventHook hook) {
  hook_ = std::move(hook);
}

// Emit + optional halting
void PICAS::emit(Event e) {
  if (!cfg_.enable_event_hooks) return;
  if (hook_) hook_(e);
  halter_.on_event(e);
}

// ---- arena ptr check ----
bool PICAS::ptr_in_arena(const void* p) const {
  const auto* b = reinterpret_cast<const std::byte*>(pages_.base);
  const auto* e = b + pages_.size;
  const auto* x = reinterpret_cast<const std::byte*>(p);
  return (b && x >= b && x < e);
}

// ---- header resolution (normal vs aligned tag) ----
BlockHeader* PICAS::header_from_user_ptr(void* p) const {
  if (!p) return nullptr;

  // 1) Normal layout: header is right before user pointer
  auto* h1 = reinterpret_cast<BlockHeader*>(reinterpret_cast<std::byte*>(p) - sizeof(BlockHeader));
  if (ptr_in_arena(h1) && h1->magic == kMagic) return h1;

  // 2) Aligned layout: AlignTag immediately before user pointer
  if (reinterpret_cast<std::uintptr_t>(p) >= sizeof(AlignTag)) {
    auto* tag = reinterpret_cast<AlignTag*>(reinterpret_cast<std::byte*>(p) - sizeof(AlignTag));
    if (tag->magic == kAlignMagic && tag->base) {
      void* base = tag->base;

      // base can be fallback-owned (then we don't have a BlockHeader)
      if (fallback_owns(base)) return nullptr;

      auto* h2 = reinterpret_cast<BlockHeader*>(reinterpret_cast<std::byte*>(base) - sizeof(BlockHeader));
      if (ptr_in_arena(h2) && h2->magic == kMagic) return h2;
    }
  }

  return nullptr;
}

// --- Layer helpers ---
bool PICAS::any_prev_layer_incomplete(std::uint32_t upto_layer) const {
  if (upto_layer == 0) return false;
  for (std::uint32_t i = 0; i < upto_layer; ++i) {
    const LayerState& L = layers_[i];
    if (L.bump < L.end) return true;
  }
  return false;
}

std::uint32_t PICAS::find_earliest_incomplete(std::uint32_t dl) {
  if (dl == 0) return dl;
  for (std::uint32_t i = 0; i < dl; ++i) {
    if (layers_[i].bump < layers_[i].end) return i;
  }
  return dl;
}

bool PICAS::would_strand_too_much(std::uint32_t layer) const {
  if (!cfg_.safety.anti_stranding.enabled) return false;
  if (layer >= num_layers_) return false;
  const LayerState& L = layers_[layer];
  std::size_t stranded = (L.end > L.bump) ? std::size_t(L.end - L.bump) : 0;
  return stranded > cfg_.safety.anti_stranding.max_stranded_per_layer;
}

// --- Bounded layer selection ---
std::uint32_t PICAS::choose_layer_bounded(std::uint32_t preferred) {
  const std::size_t need = block_total(1); // minimal; real need checked later

  auto layer_has_space = [&](std::uint32_t li) -> bool {
    if (li >= num_layers_) return false;
    return layers_[li].bump + need <= layers_[li].end;
  };

  if (preferred < num_layers_ && layer_has_space(preferred)) return preferred;

  std::uint32_t probes = 0;
  const std::uint32_t max_probes = std::max<std::uint32_t>(1, cfg_.safety.max_layer_probes);

  std::uint32_t cur = ring_cursor_.load(std::memory_order_relaxed) % num_layers_;

  while (probes < max_probes && probes < num_layers_) {
    if (layer_has_space(cur)) {
      ring_cursor_.store((cur + 1) % num_layers_, std::memory_order_relaxed);
      return cur;
    }
    cur = (cur + 1) % num_layers_;
    ++probes;
  }
  return num_layers_; // none found
}

// --- Scavenger (maintenance) ---
void PICAS::maybe_scavenge() {
  if (!cfg_.scavenger.enabled) return;
  if (cfg_.scavenger.period_allocs == 0) return;

  const auto n = allocs_since_scavenge_.fetch_add(1, std::memory_order_relaxed) + 1;
  if (n < cfg_.scavenger.period_allocs) return;

  allocs_since_scavenge_.store(0, std::memory_order_relaxed);

  scavenger_run_light(layers_.get(), num_layers_, cfg_.scavenger);
  emit({EventType::Scavenge, current_data_layer_.load(), current_mem_layer_.load(), 0, "scavenger run"});
}

// --- Allocation core ---
void* PICAS::alloc_from_layer(std::uint32_t data_layer, std::uint32_t mem_layer, std::size_t size, bool& out_oom) {
  out_oom = false;
  if (mem_layer >= num_layers_) { out_oom = true; return nullptr; }

  LayerState& L = layers_[mem_layer];
  std::lock_guard<std::mutex> lock(L.mtx);

  const std::size_t total = block_total(size);

  // 1) free-list bins
  std::size_t bi = LayerState::bin_index(total);
  for (std::size_t b = bi; b < LayerState::kBins; ++b) {
    FreeNode* prev = nullptr;
    FreeNode* cur = L.bins[b];

    while (cur) {
      if (cur->size >= total) {
        if (prev) prev->next = cur->next;
        else L.bins[b] = cur->next;

        const std::size_t remainder = cur->size - total;
        if (remainder >= block_total(32)) {
          auto* base = reinterpret_cast<std::byte*>(cur);
          auto* split = reinterpret_cast<FreeNode*>(base + total);
          split->size = remainder;
          split->next = nullptr;

          std::size_t sbi = LayerState::bin_index(split->size);
          split->next = L.bins[sbi];
          L.bins[sbi] = split;

          cur->size = total;
        }

        auto* h = reinterpret_cast<BlockHeader*>(cur);
        h->magic = kMagic;
        h->mem_layer = mem_layer;
        h->data_layer = data_layer;
        h->flags = 0;
        h->user_size = size;
        h->total_size = cur->size;

        L.live_bytes_est += h->total_size;

        void* user = reinterpret_cast<void*>(reinterpret_cast<std::byte*>(h) + sizeof(BlockHeader));
        emit({EventType::Alloc, data_layer, mem_layer, size, "free-list"});
        return user;
      }
      prev = cur;
      cur = cur->next;
    }
  }

  // 2) bump allocate
  if (!L.has_space(total)) {
    out_oom = true;
    return nullptr;
  }

  auto* h = reinterpret_cast<BlockHeader*>(L.bump);
  L.bump += total;

  h->magic = kMagic;
  h->mem_layer = mem_layer;
  h->data_layer = data_layer;
  h->flags = 0;
  h->user_size = size;
  h->total_size = total;

  L.bump_used_bytes += total;
  L.live_bytes_est += total;

  if (!L.mem_tp_reached && L.points.mem_tp > 0 && L.bump_used_bytes >= L.points.mem_tp) {
    L.mem_tp_reached = true;
    emit({EventType::LayerMemTPReached, data_layer, mem_layer, size, "MEM-TP reached"});
  }

  void* user = reinterpret_cast<void*>(reinterpret_cast<std::byte*>(h) + sizeof(BlockHeader));
  emit({EventType::Alloc, data_layer, mem_layer, size, "bump"});
  return user;
}

// Free: insert into bins
void PICAS::free_into_layer(BlockHeader* h) {
  if (!h) return;
  if (h->magic != kMagic) return;

  const std::uint32_t ml = h->mem_layer;
  if (ml >= num_layers_) return;

  LayerState& L = layers_[ml];
  std::lock_guard<std::mutex> lock(L.mtx);

  auto* node = reinterpret_cast<FreeNode*>(h);
  node->size = h->total_size;

  std::size_t bi = LayerState::bin_index(node->size);
  node->next = L.bins[bi];
  L.bins[bi] = node;

  if (L.live_bytes_est >= node->size) L.live_bytes_est -= node->size;

  emit({EventType::Free, h->data_layer, h->mem_layer, h->user_size, "free"});
}

// --- Public malloc/free/realloc ---
void* PICAS::malloc(std::size_t size) {
  maybe_scavenge();
  if (size == 0) size = 1;

  std::uint32_t dl = current_data_layer_.load(std::memory_order_relaxed);
  std::uint32_t ml = current_mem_layer_.load(std::memory_order_relaxed);
  if (dl >= num_layers_) dl = num_layers_ - 1;
  if (ml >= num_layers_) ml = dl;

  std::size_t dc = data_alloc_count_in_layer_.load(std::memory_order_relaxed);
  std::size_t db = data_alloc_bytes_in_layer_.load(std::memory_order_relaxed);

  const DataLayerPoints* dp = &cfg_.data_layers[dl];

  const LayerState& curL = layers_[ml];
  bool memtp   = curL.mem_tp_reached;
  bool memfull = curL.mem_lp_full();

  PolicyInput pin{};
  pin.num_layers = num_layers_;
  pin.data_layer = dl;
  pin.mem_layer  = ml;
  pin.request_size = size;

  pin.data_alloc_count = dc;
  pin.data_alloc_bytes = db;
  pin.data_points = dp;

  pin.mem_tp_reached = memtp;
  pin.mem_lp_full = memfull;
  pin.mem_used_bytes = curL.used_bytes();
  pin.mem_capacity_bytes = curL.capacity_bytes;
  pin.mem_tp_bytes = curL.points.mem_tp;

  pin.prev_layers_incomplete = any_prev_layer_incomplete(dl);

  PolicyOutput pout = policy_.decide(pin);

  if (pout.reached_tlp) {
    emit({EventType::LayerTLPReached, dl, ml, size, "TLP reached"});
  }
  if (pout.reached_data_lp) {
    emit({EventType::LayerDataLPReached, dl, ml, size, "DATA-LP reached"});
  }

  if (pout.hard_error) {
    emit({EventType::OutOfMemory, dl, ml, size, pout.note});
    if (cfg_.safety.always_fallback_on_fail) {
      void* fp = fallback_alloc(size);
      if (fp) {
        emit({EventType::FallbackAlloc, dl, ml, size, "fallback (hard_error)"});
        return fp;
      }
    }
    return nullptr;
  }

  // Anti-stranding
  if (pout.jump_data_layer && cfg_.safety.anti_stranding.enabled) {
    const bool strand_bad = would_strand_too_much(ml);

    const bool pressured =
        layers_[ml].mem_lp_full() ||
        (layers_[ml].capacity_bytes != 0 &&
         layers_[ml].used_bytes() > (layers_[ml].capacity_bytes * 9) / 10);

    if (strand_bad && !(cfg_.safety.anti_stranding.allow_jump_if_pressure && pressured)) {
      pout.jump_data_layer = false;
      pout.jump_mem_layer  = false;
      if (cfg_.safety.anti_stranding.aggressive_backfill) {
        pout.backfill_memory = true;
      }
      pout.note = "Anti-stranding: delayed jump; prefer backfill/same-layer";
    }
  }

  // Apply jumps
  if (pout.jump_data_layer && dl + 1 < num_layers_) {
    emit({EventType::JumpToNextLayer, dl, ml, size, pout.note});
    dl = dl + 1;
    current_data_layer_.store(dl, std::memory_order_relaxed);
    data_alloc_count_in_layer_.store(0, std::memory_order_relaxed);
    data_alloc_bytes_in_layer_.store(0, std::memory_order_relaxed);
    if (pout.jump_mem_layer) {
      ml = std::min<std::uint32_t>(dl, num_layers_ - 1);
      current_mem_layer_.store(ml, std::memory_order_relaxed);
    }
  }

  // Backfill selection
  std::uint32_t chosen_ml = pout.chosen_mem_layer;
  if (pout.backfill_memory) {
    chosen_ml = find_earliest_incomplete(dl);
    emit({EventType::DataAdvancedMemoryBackfill, dl, chosen_ml, size, pout.note});
  } else {
    chosen_ml = std::min<std::uint32_t>(pout.chosen_mem_layer, num_layers_ - 1);
  }

  // If chosen layer is full, do bounded probe
  if (chosen_ml >= num_layers_ || layers_[chosen_ml].mem_lp_full()) {
    std::uint32_t probed = choose_layer_bounded(dl);
    if (probed < num_layers_) {
      chosen_ml = probed;
      emit({EventType::MemorySpillToOtherLayer, dl, chosen_ml, size, "bounded-probe spill"});
    }
  }

  bool oom = false;
  void* p = alloc_from_layer(dl, chosen_ml, size, oom);

  // Retry bounded probe
  if (!p) {
    std::uint32_t probed = choose_layer_bounded(chosen_ml);
    if (probed < num_layers_) {
      chosen_ml = probed;
      emit({EventType::MemorySpillToOtherLayer, dl, chosen_ml, size, "bounded-probe retry"});
      p = alloc_from_layer(dl, chosen_ml, size, oom);
    }
  }

  // Fallback
  if (!p) {
    emit({EventType::OutOfMemory, dl, chosen_ml, size, "PICAS arena exhausted"});
    if (cfg_.safety.always_fallback_on_fail) {
      void* fp = fallback_alloc(size);
      if (fp) {
        emit({EventType::FallbackAlloc, dl, chosen_ml, size, "fallback"});
        return fp;
      }
    }
    return nullptr;
  }

  data_alloc_count_in_layer_.fetch_add(1, std::memory_order_relaxed);
  data_alloc_bytes_in_layer_.fetch_add(size, std::memory_order_relaxed);

  if (cfg_.enable_tracing && tracer_.enabled()) {
    TraceEntry te{};
    te.seq = alloc_seq_.fetch_add(1, std::memory_order_relaxed);
    te.data_layer = dl;
    te.mem_layer = chosen_ml;
    te.size = size;
    te.addr = reinterpret_cast<std::uintptr_t>(p);

    auto* pb = reinterpret_cast<std::byte*>(p);
    te.layer_offset = (pb >= layers_[chosen_ml].begin)
        ? std::size_t(pb - layers_[chosen_ml].begin)
        : 0;

    te.penalty_cost = (chosen_ml == dl) ? 0.0 : cfg_.penalty_k;
    te.note = pout.note;
    tracer_.record(te);
  }

  return p;
}

void PICAS::free(void* p) {
  if (!p) return;

  // Aligned pointer? (AlignTag right before p)
  if (reinterpret_cast<std::uintptr_t>(p) >= sizeof(AlignTag)) {
    auto* tag = reinterpret_cast<AlignTag*>(reinterpret_cast<std::byte*>(p) - sizeof(AlignTag));
    if (tag->magic == kAlignMagic && tag->base) {
      // base may be fallback or PICAS-owned; free() handles both paths
      free(tag->base);
      return;
    }
  }

  // fallback-owned?
  if (fallback_owns(p)) {
    fallback_free(p);
    emit({EventType::Free, current_data_layer_.load(), 0, 0, "free fallback"});
    return;
  }

  BlockHeader* h = header_from_user_ptr(p);
  if (!h) return;

  free_into_layer(h);
}

void* PICAS::realloc(void* p, std::size_t new_size) {
  if (!p) return malloc(new_size);
  if (new_size == 0) { free(p); return nullptr; }

  // Aligned pointer? (degrade realloc(memalign_ptr) into allocate+copy+free)
  if (reinterpret_cast<std::uintptr_t>(p) >= sizeof(AlignTag)) {
    auto* tag = reinterpret_cast<AlignTag*>(reinterpret_cast<std::byte*>(p) - sizeof(AlignTag));
    if (tag->magic == kAlignMagic && tag->base) {
      const std::size_t old_sz = tag->requested;
      void* np = malloc(new_size);
      if (!np) return nullptr;
      const std::size_t to_copy = (old_sz < new_size) ? old_sz : new_size;
      if (to_copy) std::memcpy(np, p, to_copy);
      free(p);
      emit({EventType::Realloc, current_data_layer_.load(), current_mem_layer_.load(), new_size, "realloc aligned -> copy"});
      return np;
    }
  }

  // If fallback-owned
  if (fallback_owns(p)) {
    const std::size_t old_sz = fallback_usable_size(p);
    void* np = malloc(new_size);
    if (!np) return nullptr;

    const std::size_t to_copy = (old_sz < new_size) ? old_sz : new_size;
    if (to_copy > 0) std::memcpy(np, p, to_copy);

    fallback_free(p);
    emit({EventType::Realloc, current_data_layer_.load(), 0, new_size, "realloc fallback -> picas"});
    return np;
  }

  BlockHeader* h = header_from_user_ptr(p);
  if (!h) return nullptr;

  if (new_size <= h->user_size) {
    h->user_size = new_size;
    emit({EventType::Realloc, h->data_layer, h->mem_layer, new_size, "shrink in-place"});
    return p;
  }

  void* np = malloc(new_size);
  if (!np) return nullptr;

  std::memcpy(np, p, h->user_size);
  free(p);

  emit({EventType::Realloc, h->data_layer, h->mem_layer, new_size, "grow via copy"});
  return np;
}

// --- Section A: memalign implementation (robust with AlignTag) ---
void* PICAS::memalign(std::size_t alignment, std::size_t size) {
  if (size == 0) size = 1;
  if (alignment < sizeof(void*)) alignment = sizeof(void*);
  if (!is_pow2_local(alignment)) return nullptr;

  // If alignment is <= our normal alignment, just use malloc
  if (alignment <= kAlign) {
    return malloc(size);
  }

  // Over-allocate:
  // [base ... AlignTag ... padding ... aligned_ptr(user bytes)]
  const std::size_t extra = alignment + sizeof(AlignTag);
  void* base = malloc(size + extra);
  if (!base) return nullptr;

  auto* raw = reinterpret_cast<std::byte*>(base);
  auto* aligned = align_ptr(raw + sizeof(AlignTag), alignment);

  auto* tag = reinterpret_cast<AlignTag*>(aligned - sizeof(AlignTag));
  tag->magic = kAlignMagic;
  tag->base = base;
  tag->requested = size;

  emit({EventType::Alloc, current_data_layer_.load(), current_mem_layer_.load(), size, "memalign"});
  return aligned;
}

// --- Section A: usable_size (member) ---
// Works for:
//  - normal PICAS blocks
//  - memalign blocks (AlignTag)
//  - fallback blocks (fallback_usable_size)
std::size_t PICAS::usable_size(void* p) const {
  if (!p) return 0;

  // aligned?
  if (reinterpret_cast<std::uintptr_t>(p) >= sizeof(AlignTag)) {
    auto* tag = reinterpret_cast<AlignTag*>(reinterpret_cast<std::byte*>(p) - sizeof(AlignTag));
    if (tag->magic == kAlignMagic) return tag->requested;
  }

  if (fallback_owns(p)) return fallback_usable_size(p);

  BlockHeader* h = header_from_user_ptr(p);
  if (!h) return 0;
  if (h->magic != kMagic) return 0;
  return h->user_size;
}

// --- Phase control ---
void PICAS::set_data_layer(std::uint32_t layer) {
  if (layer >= num_layers_) layer = num_layers_ - 1;
  current_data_layer_.store(layer, std::memory_order_relaxed);
  current_mem_layer_.store(layer, std::memory_order_relaxed);
  data_alloc_count_in_layer_.store(0, std::memory_order_relaxed);
  data_alloc_bytes_in_layer_.store(0, std::memory_order_relaxed);
}

std::uint32_t PICAS::data_layer() const {
  return current_data_layer_.load(std::memory_order_relaxed);
}

// --- Stats ---
PICAS::Stats PICAS::stats() const {
  Stats s{};
  s.total_reserved = pages_.size;
  for (std::uint32_t i = 0; i < num_layers_; ++i) {
    s.total_capacity += layers_[i].capacity_bytes;
    s.total_live_est += layers_[i].live_bytes_est;
  }
  return s;
}

// ---------- Section A: global helpers (calloc / usable_size) ----------
void* picas_calloc(std::size_t n, std::size_t sz) {
  if (!g_alloc) return nullptr;
  if (n == 0 || sz == 0) return g_alloc->malloc(1);
  if (sz > (std::numeric_limits<std::size_t>::max() / n)) return nullptr;

  const std::size_t total = n * sz;
  void* p = g_alloc->malloc(total);
  if (p) std::memset(p, 0, total);
  return p;
}

std::size_t picas_usable_size(void* p) {
  return g_alloc ? g_alloc->usable_size(p) : 0;
}

// --- Global API ---
void picas_init(Config cfg) {
  if (g_alloc) return;
  g_alloc = new PICAS(cfg);
}

void picas_shutdown() {
  delete g_alloc;
  g_alloc = nullptr;
}

PICAS* picas_instance() { return g_alloc; }

void* picas_malloc(std::size_t size) {
  return g_alloc ? g_alloc->malloc(size) : nullptr;
}

void picas_free(void* p) {
  if (g_alloc) g_alloc->free(p);
}

void* picas_realloc(void* p, std::size_t size) {
  return g_alloc ? g_alloc->realloc(p, size) : nullptr;
}

void* picas_memalign(std::size_t alignment, std::size_t size) {
  return g_alloc ? g_alloc->memalign(alignment, size) : nullptr;
}

void picas_set_event_hook(EventHook hook) {
  if (g_alloc) g_alloc->set_event_hook(std::move(hook));
}

void picas_set_data_layer(std::uint32_t layer) {
  if (g_alloc) g_alloc->set_data_layer(layer);
}

} // namespace picas
