#include "picas/policy.hpp"

namespace picas {

PolicyOutput Policy::decide(const PolicyInput& in) const {
  PolicyOutput out{};
  out.chosen_mem_layer = in.mem_layer;

  // Defensive defaults
  if (in.num_layers == 0) {
    out.hard_error = true;
    out.note = "Invalid: num_layers=0";
    return out;
  }

  // Evaluate points
  bool reached_tlp = false;
  bool reached_data_lp = false;

  if (in.data_points) {
    reached_tlp = in.data_points->tlp.reached(in.data_alloc_count, in.data_alloc_bytes);
    reached_data_lp = in.data_points->data_lp.reached(in.data_alloc_count, in.data_alloc_bytes);
  }

  out.reached_tlp = reached_tlp;
  out.reached_data_lp = reached_data_lp;

  // Practical hard-error rule:
  // If a byte-based TLP range is configured and its length exceeds memory layer capacity,
  // you are asking for a phase checkpoint larger than the entire target memory layer.
  if (in.data_points) {
    const auto tlp_bytes_len = in.data_points->tlp.bytes_len();
    if (tlp_bytes_len != 0 && in.mem_capacity_bytes != 0 && tlp_bytes_len > in.mem_capacity_bytes) {
      out.hard_error = true;
      out.note = "Hard error: TLP.bytes_len > mem layer capacity (len(TLP) > len(MEM-LP))";
      return out;
    }
  }

  // Rule 0 (hard boundary): DATA-LP reached means phase must advance.
  // In strict behavior we also prefer to keep memory and data layer aligned by advancing mem layer.
  if (reached_data_lp) {
    out.jump_data_layer = true;

    if (cfg_.strict_picas_jumps) out.jump_mem_layer = true;

    out.chosen_mem_layer = (in.data_layer < in.num_layers) ? in.data_layer : 0;
    out.note = "DATA-LP reached => hard advance data layer (and memory if strict)";
    return out;
  }

  // Rule 1 (your key PICAS rule):
  // If TLP reached before MEM-TP in same level => jump both to next layer (strict mode).
  if (cfg_.strict_picas_jumps && reached_tlp && !in.mem_tp_reached) {
    out.jump_data_layer = true;
    out.jump_mem_layer  = true;
    out.note = "TLP reached before MEM-TP => jump data+memory to next layer";
    return out;
  }

  // Rule 2:
  // If data has advanced but earlier memory layer has space, backfill it.
  if (cfg_.strict_picas_jumps && in.prev_layers_incomplete) {
    out.backfill_memory = true;
    out.note = "Earlier memory incomplete => backfill earlier layer";
    return out;
  }

  // Rule 3:
  // If current memory layer is full, allocator will spill (bounded-probe).
  if (in.mem_lp_full) {
    out.note = "Current MEM-LP full => spill to alternative layer";
    return out;
  }

  // Default: prefer same-layer allocation to minimize penalty.
  out.chosen_mem_layer = in.data_layer;
  out.note = "Default: same-layer allocation";
  return out;
}

} // namespace picas
