#include "picas/picas.hpp"
#include <cstdio>
#include <vector>
#include <string>

static const char* et(picas::EventType t) {
  using picas::EventType;
  switch (t) {
    case EventType::Alloc: return "Alloc";
    case EventType::Free: return "Free";
    case EventType::Realloc: return "Realloc";
    case EventType::OutOfMemory: return "OOM";
    case EventType::FallbackAlloc: return "FallbackAlloc";
    case EventType::JumpToNextLayer: return "JumpToNextLayer";
    case EventType::MemorySpillToOtherLayer: return "MemorySpill";
    case EventType::DataAdvancedMemoryBackfill: return "Backfill";
    case EventType::LayerMemTPReached: return "MemTP";
    case EventType::LayerTLPReached: return "TLP";
    case EventType::LayerDataLPReached: return "DataLP";
    case EventType::Scavenge: return "Scavenge";
    default: return "Other";
  }
}

int main() {
  picas::Config cfg{};
  cfg.num_layers = 4;

  // Basic arena sizing (adjust if your Config defaults already set these)
  cfg.mem_layers[0].bytes = 4 * 1024 * 1024;
  cfg.mem_layers[1].bytes = 4 * 1024 * 1024;
  cfg.mem_layers[2].bytes = 4 * 1024 * 1024;
  cfg.mem_layers[3].bytes = 4 * 1024 * 1024;

  cfg.enable_event_hooks = true;
  cfg.enable_tracing = false;

  cfg.scavenger.enabled = true;
  cfg.scavenger.period_allocs = 2000;

  cfg.safety.always_fallback_on_fail = true;

  picas::picas_init(cfg);

  // Print events live
  picas::picas_set_event_hook([](picas::Event e) {
    std::printf("[event] %-14s dl=%u ml=%u size=%zu note=%s\n",
      et(e.type),
      e.data_layer,
      e.mem_layer,
      e.size,
      e.note ? e.note : ""
    );
  });

  std::vector<void*> v;
  v.reserve(20000);

  // Phase 0: many small allocations
  picas::picas_set_data_layer(0);
  for (int i = 0; i < 4000; ++i) {
    std::size_t sz = (std::size_t)((i % 512) + 1);
    v.push_back(picas::picas_malloc(sz));
  }

  // Phase 1: more allocations (some aligned)
  picas::picas_set_data_layer(1);
  for (int i = 0; i < 4000; ++i) {
    std::size_t sz = (std::size_t)((i % 1024) + 1);
    v.push_back(picas::picas_memalign(64, sz));
  }

  // Free every other block
  for (std::size_t i = 0; i < v.size(); i += 2) {
    picas::picas_free(v[i]);
    v[i] = nullptr;
  }

  // Phase 2: churn to exercise bins + scavenger
  picas::picas_set_data_layer(2);
  for (int i = 0; i < 12000; ++i) {
    std::size_t sz = (std::size_t)((i % 128) + 1);
    void* p = picas::picas_malloc(sz);
    picas::picas_free(p);
  }

  // Print stats at end
  if (auto* A = picas::picas_instance()) {
    auto s = A->stats();
    std::printf("\n[stats] reserved=%zu capacity=%zu live_est=%zu\n",
      s.total_reserved, s.total_capacity, s.total_live_est);
  }

  // Cleanup remaining pointers
  for (void* p : v) {
    if (p) picas::picas_free(p);
  }

  picas::picas_shutdown();
  std::puts("[OK] demo_observe");
  return 0;
}
