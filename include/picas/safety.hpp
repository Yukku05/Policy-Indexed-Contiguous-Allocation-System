#pragma once
#include "fallback.hpp"
#include "invariants.hpp"
#include <cstddef>
#include <cstdint>

namespace picas {

struct SafetyConfig {
  FallbackConfig fallback{};
  AntiStranding  anti_stranding{};

  // Hard cap for probing candidate layers (real-time guardrail)
  std::uint32_t max_layer_probes = 8;

  // If true, allocator will attempt fallback before returning nullptr.
  bool always_fallback_on_fail = true;
};

} // namespace picas
