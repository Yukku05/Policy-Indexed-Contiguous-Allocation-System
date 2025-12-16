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
    default: return "Unknown";
  }
}

int main() {
  picas::Config cfg{};
  cfg.num_layers = 3;

  cfg.mem_layers[0].bytes = 128 * 1024;
  cfg.mem_layers[1].bytes = 128 * 1024;
  cfg.mem_layers[2].bytes = 128 * 1024;

  cfg.enable_event_hooks = true;
  cfg.enable_tracing = false;

  picas::picas_init(cfg);

  // Hook: print events
  picas::picas_set_event_hook([](const picas::Event& e) {
    std::printf("[event] %-14s dl=%u ml=%u size=%zu note=%s\n",
      et(e.type),
      e.data_layer,
      e.mem_layer,
      e.size,
      e.note ? e.note : ""
    );
  });

  auto* A = picas::picas_instance();
  picas::picas_set_data_layer(0);

  void* a = A->malloc(60000);
  void* b = A->malloc(60000);
  void* c = A->malloc(60000); // likely spill / jump / oom depending on policy

  A->free(b);
  c = A->realloc(c, 90000);

  A->free(a);
  A->free(c);

  picas::picas_shutdown();
  return 0;
}
