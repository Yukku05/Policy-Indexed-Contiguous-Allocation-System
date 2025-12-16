#pragma once
#include "config.hpp"
#include "events.hpp"
#include "halting.hpp"
#include "os_pages.hpp"
#include "layer.hpp"
#include "policy.hpp"
#include "tracer.hpp"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <memory>

namespace picas {

class PICAS {
public:
  explicit PICAS(Config cfg);
  ~PICAS();

  PICAS(const PICAS&) = delete;
  PICAS& operator=(const PICAS&) = delete;

  void set_event_hook(EventHook hook);

  // malloc-like API
  void* malloc(std::size_t size);
  void  free(void* p);
  void* realloc(void* p, std::size_t new_size);

  // aligned allocation
  // alignment must be power-of-two and >= sizeof(void*)
  void* memalign(std::size_t alignment, std::size_t size);

  // returns the user-requested size for PICAS-owned ptrs, 0 if unknown
  std::size_t usable_size(void* p) const;

  // phase control
  void set_data_layer(std::uint32_t layer);
  std::uint32_t data_layer() const;

  // tracing access
  Tracer& tracer() { return tracer_; }
  const Tracer& tracer() const { return tracer_; }

  struct Stats {
    std::size_t total_reserved = 0;
    std::size_t total_capacity = 0;
    std::size_t total_live_est = 0;
  };
  Stats stats() const;

private:
  // internal helpers
  void emit(Event e);

  void* alloc_from_layer(std::uint32_t data_layer, std::uint32_t mem_layer,
                         std::size_t size, bool& out_oom);

  void  free_into_layer(BlockHeader* h);

  // bounded selection
  std::uint32_t choose_layer_bounded(std::uint32_t preferred);

  // find earliest incomplete layer < dl
  std::uint32_t find_earliest_incomplete(std::uint32_t dl);

  bool any_prev_layer_incomplete(std::uint32_t upto_layer) const;
  bool would_strand_too_much(std::uint32_t layer) const;

  void maybe_scavenge();

  static inline std::size_t align_up(std::size_t x, std::size_t a) {
    return (x + (a - 1)) & ~(a - 1);
  }
  static inline bool is_pow2(std::size_t x) { return x && ((x & (x - 1)) == 0); }

  // Resolve header from a user pointer:
  // 1) Normal: header is immediately before pointer
  // 2) Aligned: AlignTag is stored at (p - sizeof(AlignTag)) and points back to base
  BlockHeader* header_from_user_ptr(void* p) const;

  // quick range check: is an address inside our reserved arena?
  bool ptr_in_arena(const void* p) const;

  static constexpr std::size_t kAlign = 16;

  Config cfg_;
  Policy policy_;

  EventHook hook_{};
  HaltingController halter_{};

  Pages pages_{};
  std::uint32_t num_layers_ = 0;
  std::unique_ptr<LayerState[]> layers_;

  // current conceptual layers
  std::atomic<std::uint32_t> current_data_layer_{0};
  std::atomic<std::uint32_t> current_mem_layer_{0};

  // progress in current data layer
  std::atomic<std::size_t> data_alloc_count_in_layer_{0};
  std::atomic<std::size_t> data_alloc_bytes_in_layer_{0};

  // tracing
  Tracer tracer_;
  std::atomic<std::uint64_t> alloc_seq_{0};

  // bounded probing ring cursor
  std::atomic<std::uint32_t> ring_cursor_{0};

  // scavenger counter
  std::atomic<std::uint64_t> allocs_since_scavenge_{0};
};

// ---- Global singleton-style API ----
void   picas_init(Config cfg);
void   picas_shutdown();

void*  picas_malloc(std::size_t size);
void   picas_free(void* p);
void*  picas_realloc(void* p, std::size_t size);

// Aligned allocation (alignment must be power-of-two, >= sizeof(void*))
void*  picas_memalign(std::size_t alignment, std::size_t size);

// calloc + usable_size helpers (global, like libc)
void*      picas_calloc(std::size_t n, std::size_t size);
std::size_t picas_usable_size(void* p);

void   picas_set_event_hook(EventHook hook);
void   picas_set_data_layer(std::uint32_t layer);

PICAS* picas_instance(); // (for demos/benchmarks only)

} // namespace picas
