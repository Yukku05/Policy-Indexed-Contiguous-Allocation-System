#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

namespace picas {

enum class EventType : std::uint8_t {
  Alloc,
  Free,
  Realloc,

  // PICAS structure events
  JumpToNextLayer,            // data+memory jumped
  DataAdvancedMemoryBackfill, // data advanced but memory backfilled earlier layer
  MemorySpillToOtherLayer,    // allocated into different mem layer due to full/penalty
  LayerMemTPReached,          // mem layer hit MEM-TP
  LayerTLPReached,            // data layer hit TLP
  LayerDataLPReached,         // data layer hit DATA-LP (hard boundary)
  Scavenge,                   // maintenance pass executed
  FallbackAlloc,              // allocation satisfied by fallback
  OutOfMemory                 // no space, no fallback (or fallback failed)
};

struct Event {
  EventType type{};
  std::uint32_t data_layer = 0;
  std::uint32_t mem_layer  = 0;
  std::size_t size = 0;
  const char* note = nullptr;
};

using EventHook = std::function<void(const Event&)>;

} // namespace picas
