#pragma once
#include <cstddef>
#include <cstdint>
#include <array>

#include "checkpoints.hpp"
#include "safety.hpp"
#include "scavenger.hpp"

namespace picas {

struct LayerConfig {
  std::size_t bytes = 0;        // capacity of this memory layer
  std::size_t mem_tp_bytes = 0; // memory transitory point inside that layer
};

struct Config {
  static constexpr std::size_t kMaxLayers = 8;

  std::uint32_t num_layers = 3;

  // penalty applied when data layer alloc is placed in different memory layer
  double penalty_k = 1.0;

  // memory layers
  std::array<LayerConfig, kMaxLayers> mem_layers{};

  // data layers
  std::array<DataLayerPoints, kMaxLayers> data_layers{};

  // PICAS strict behavior toggles
  bool strict_picas_jumps = true;

  // hooks / debugging
  bool enable_event_hooks = true;
  bool enable_debug_pause = false;
  std::uint32_t debug_pause_ms = 0;

  // safety + maintenance
  SafetyConfig safety{};
  ScavengerConfig scavenger{};

  // tracing
  bool enable_tracing = true;
};

} // namespace picas
