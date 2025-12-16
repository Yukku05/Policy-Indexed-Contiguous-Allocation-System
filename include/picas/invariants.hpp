#pragma once
#include <cstddef>
#include <cstdint>

namespace picas {

struct AntiStranding {
  bool enabled = true;

  // If jump would strand more than this amount in current layer, we delay jump unless pressured.
  std::size_t max_stranded_per_layer = 256 * 1024; // 256KB

  // Permit jump if memory pressure is high (near full) even if stranding would be big.
  bool allow_jump_if_pressure = true;

  // If we detect stranding, bias toward backfill earlier incomplete layers aggressively.
  bool aggressive_backfill = true;
};

} // namespace picas
