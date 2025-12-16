#pragma once
#include "events.hpp"
#include <atomic>
#include <thread>
#include <chrono>

namespace picas {

class HaltingController {
public:
  void enable(bool on) { enabled_.store(on, std::memory_order_relaxed); }
  void set_pause_ms(std::uint32_t ms) { pause_ms_.store(ms, std::memory_order_relaxed); }

  void on_event(const Event& e) {
    if (!enabled_.load(std::memory_order_relaxed)) return;
    auto ms = pause_ms_.load(std::memory_order_relaxed);
    if (ms == 0) return;

    // Pause only on structural events
    switch (e.type) {
      case EventType::JumpToNextLayer:
      case EventType::DataAdvancedMemoryBackfill:
      case EventType::MemorySpillToOtherLayer:
      case EventType::LayerMemTPReached:
      case EventType::LayerTLPReached:
      case EventType::LayerDataLPReached:
      case EventType::FallbackAlloc:
      case EventType::OutOfMemory:
      case EventType::Scavenge:
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        break;
      default:
        break;
    }
  }

private:
  std::atomic<bool> enabled_{false};
  std::atomic<std::uint32_t> pause_ms_{0};
};

} // namespace picas
