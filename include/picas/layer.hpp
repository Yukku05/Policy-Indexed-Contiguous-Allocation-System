#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <array>

namespace picas {

// Header placed immediately before returned user pointer.
struct BlockHeader {
  std::uint32_t magic = 0x50494341; // 'PICA'
  std::uint32_t mem_layer = 0;
  std::uint32_t data_layer = 0;
  std::uint32_t flags = 0;

  std::size_t user_size = 0;  // size requested by user
  std::size_t total_size = 0; // total block size including header and padding
};

// Free node stored in freed blocks; overlays BlockHeader.
struct FreeNode {
  FreeNode* next = nullptr;
  std::size_t size = 0; // total block size
};

struct MemLayerPoints {
  // MEM-LP is implicitly [0, capacity_bytes)
  std::size_t mem_tp = 0; // in bytes
};

struct LayerState {
  std::byte* begin = nullptr;
  std::byte* end   = nullptr;
  std::byte* bump  = nullptr;

  // accounting
  std::size_t capacity_bytes  = 0;
  std::size_t bump_used_bytes = 0; // monotonically increases
  std::size_t live_bytes_est  = 0; // decreases on free (approx)

  MemLayerPoints points{};
  bool mem_tp_reached = false;

  static constexpr std::size_t kBins = 20;
  std::array<FreeNode*, kBins> bins{};

  std::mutex mtx;

  bool has_space(std::size_t need) const {
    return bump + need <= end;
  }

  bool mem_lp_full() const { return bump >= end; }
  std::size_t used_bytes() const { return bump_used_bytes; }

  static std::size_t bin_index(std::size_t sz) {
    // log2-like binning
    std::size_t v = (sz < 32 ? 32 : sz);
    std::size_t idx = 0;
    while (v >>= 1) { ++idx; }
    if (idx >= kBins) idx = kBins - 1;
    return idx;
  }
};

} // namespace picas
