#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <mutex>
#include <string>

namespace picas {

struct TraceEntry {
  std::uint64_t seq = 0;

  std::uint32_t data_layer = 0;
  std::uint32_t mem_layer  = 0;

  std::size_t size = 0;

  std::uintptr_t addr = 0;
  std::size_t layer_offset = 0;

  double penalty_cost = 0.0;
  const char* note = nullptr;
};

class Tracer {
public:
  void enable(bool on) { enabled_ = on; }
  bool enabled() const { return enabled_; }

  void record(const TraceEntry& e);
  std::vector<TraceEntry> snapshot() const;
  std::string to_csv() const;

private:
  bool enabled_ = true;
  mutable std::mutex mtx_;
  std::vector<TraceEntry> entries_;
};

} // namespace picas
