#include "picas/tracer.hpp"
#include <sstream>

namespace picas {

void Tracer::record(const TraceEntry& e) {
  if (!enabled_) return;
  std::lock_guard<std::mutex> lock(mtx_);
  entries_.push_back(e);
}

std::vector<TraceEntry> Tracer::snapshot() const {
  std::lock_guard<std::mutex> lock(mtx_);
  return entries_;
}

std::string Tracer::to_csv() const {
  std::lock_guard<std::mutex> lock(mtx_);
  std::ostringstream oss;
  oss << "seq,data_layer,mem_layer,size,addr,layer_offset,penalty_cost,note\n";
  for (const auto& e : entries_) {
    oss << e.seq << ","
        << e.data_layer << ","
        << e.mem_layer << ","
        << e.size << ","
        << e.addr << ","
        << e.layer_offset << ","
        << e.penalty_cost << ","
        << (e.note ? e.note : "")
        << "\n";
  }
  return oss.str();
}

} // namespace picas
