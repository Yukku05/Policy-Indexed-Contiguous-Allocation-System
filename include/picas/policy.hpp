#pragma once
#include "config.hpp"
#include "checkpoints.hpp"
#include <cstddef>
#include <cstdint>

namespace picas {

struct PolicyInput {
  std::uint32_t num_layers = 0;
  std::uint32_t data_layer = 0;
  std::uint32_t mem_layer  = 0;

  std::size_t request_size = 0;

  // Data progress inside current data layer
  std::size_t data_alloc_count = 0;
  std::size_t data_alloc_bytes = 0;

  // Points for this data layer
  const DataLayerPoints* data_points = nullptr;

  // Memory layer status
  bool mem_tp_reached = false;
  bool mem_lp_full = false;
  std::size_t mem_used_bytes = 0;
  std::size_t mem_capacity_bytes = 0;
  std::size_t mem_tp_bytes = 0;

  // global condition
  bool prev_layers_incomplete = false;
};

struct PolicyOutput {
  std::uint32_t chosen_mem_layer = 0;

  // control decisions
  bool jump_data_layer = false;
  bool jump_mem_layer  = false;
  bool backfill_memory = false;

  // phase boundary signals (for events/debug)
  bool reached_tlp = false;
  bool reached_data_lp = false;

  // error (insufficient memory constraint)
  bool hard_error = false;
  const char* note = nullptr;
};

class Policy {
public:
  explicit Policy(const Config& cfg) : cfg_(cfg) {}
  PolicyOutput decide(const PolicyInput& in) const;

private:
  const Config& cfg_;
};

} // namespace picas
