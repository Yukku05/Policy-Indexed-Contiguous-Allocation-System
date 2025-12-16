#include "picas/config.hpp"
#include <string>
#include <algorithm>

namespace picas {

// These helpers are internal; picas.cpp declares them as extern.
static bool validate_positive(std::size_t x) { return x > 0; }

bool safety_validate_and_sanitize(Config& cfg, std::string* why) {
  // Clamp number of layers
  if (cfg.num_layers == 0) cfg.num_layers = 1;
  if (cfg.num_layers > Config::kMaxLayers) cfg.num_layers = Config::kMaxLayers;

  // Reasonable RT defaults if caller didn't tune
  if (cfg.safety.max_layer_probes == 0) cfg.safety.max_layer_probes = 1;
  if (cfg.safety.max_layer_probes > cfg.num_layers) cfg.safety.max_layer_probes = cfg.num_layers;

  // Fallback: default to system malloc (practical + safest)
  // If user explicitly set None, we allow it.
  // EmergencyReserve only makes sense if bytes > 0.
  if (cfg.safety.fallback.mode == FallbackMode::EmergencyReserve &&
      cfg.safety.fallback.emergency_bytes < 4096) {
    cfg.safety.fallback.emergency_bytes = 4096;
  }

  // Scavenger: if enabled but period is 0, set a sane value.
  if (cfg.scavenger.enabled && cfg.scavenger.period_allocs == 0) {
    cfg.scavenger.period_allocs = 4096;
  }

  // Memory layers: if total is zero, set a minimal arena.
  std::size_t total = 0;
  for (std::uint32_t i = 0; i < cfg.num_layers; ++i) {
    total += cfg.mem_layers[i].bytes;
  }
  if (total == 0) {
    // minimal 8MB split evenly (practical)
    constexpr std::size_t fallback_total = 8ull * 1024 * 1024;
    std::size_t per = fallback_total / cfg.num_layers;
    for (std::uint32_t i = 0; i < cfg.num_layers; ++i) {
      cfg.mem_layers[i].bytes = per;
      cfg.mem_layers[i].mem_tp_bytes = (per * 3) / 4;
    }
  }

  // mem_tp must not exceed capacity
  for (std::uint32_t i = 0; i < cfg.num_layers; ++i) {
    auto cap = cfg.mem_layers[i].bytes;
    auto tp  = cfg.mem_layers[i].mem_tp_bytes;
    if (tp > cap) cfg.mem_layers[i].mem_tp_bytes = cap;
  }

  // Anti-stranding invariants:
  if (cfg.safety.anti_stranding.enabled &&
      cfg.safety.anti_stranding.max_stranded_per_layer < 1024) {
    cfg.safety.anti_stranding.max_stranded_per_layer = 1024;
  }

  // Optional: warn if weird config
  if (why) {
    *why = "ok";
  }
  return true;
}

} // namespace picas
