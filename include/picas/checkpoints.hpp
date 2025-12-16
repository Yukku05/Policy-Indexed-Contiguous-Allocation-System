#pragma once
#include <cstddef>
#include <cstdint>

namespace picas {

// half-open interval [start, end)
struct Range {
  std::size_t start = 0;
  std::size_t end   = 0;

  std::size_t len() const { return (end > start) ? (end - start) : 0; }
  bool reached_end(std::size_t x) const { return (end != 0) && (x >= end); }
};

enum class Logic : std::uint8_t { Any, All }; // OR / AND

// Hybrid point: triggers when count threshold and/or byte threshold is reached.
struct HybridPoint {
  Range count{}; // e.g., [0, 2000)
  Range bytes{}; // e.g., [0, 512MB)
  Logic logic = Logic::Any;

  bool configured() const { return count.end != 0 || bytes.end != 0; }

  bool reached(std::size_t c, std::size_t b) const {
    bool has_c = (count.end != 0);
    bool has_b = (bytes.end != 0);
    if (!has_c && !has_b) return false;

    bool rc = has_c ? count.reached_end(c) : true;
    bool rb = has_b ? bytes.reached_end(b) : true;

    if (logic == Logic::All) {
      // If a dimension isn't configured, treat it as satisfied
      return rc && rb;
    }
    // Any
    bool ok = false;
    if (has_c) ok = ok || count.reached_end(c);
    if (has_b) ok = ok || bytes.reached_end(b);
    return ok;
  }

  // “Length” in bytes is well-defined only for bytes-range. Count-length is symbolic.
  std::size_t bytes_len() const { return bytes.len(); }
  std::size_t count_len() const { return count.len(); }
};

// Per data layer:
// - tlp: checkpoint / transitory layer point (can trigger early jump)
// - data_lp: hard boundary (phase end) — generalized (5,3,1) becomes data_lp.count.end=...
struct DataLayerPoints {
  HybridPoint tlp{};
  HybridPoint data_lp{};
};

} // namespace picas
