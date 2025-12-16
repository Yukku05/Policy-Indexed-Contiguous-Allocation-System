#include "picas/scavenger.hpp"
#include "picas/layer.hpp"
#include <vector>
#include <algorithm>
#include <cstdint>

namespace picas {

// This function is used by picas.cpp (declared there).
void scavenger_run_light(LayerState* layers,
                         std::uint32_t num_layers,
                         const ScavengerConfig& cfg) {
  if (!cfg.enabled) return;
  if (!layers || num_layers == 0) return;

  // For each layer: rebucket and optionally coalesce free blocks.
  for (std::uint32_t li = 0; li < num_layers; ++li) {
    LayerState& L = layers[li];
    std::lock_guard<std::mutex> lock(L.mtx);

    // Collect all free blocks across bins.
    std::vector<FreeNode*> blocks;
    blocks.reserve(1024);

    for (std::size_t b = 0; b < LayerState::kBins; ++b) {
      FreeNode* cur = L.bins[b];
      while (cur) {
        blocks.push_back(cur);
        cur = cur->next;
      }
      L.bins[b] = nullptr;
    }

    if (blocks.empty()) continue;

    // Sort by address to enable coalescing.
    std::sort(blocks.begin(), blocks.end(),
              [](FreeNode* a, FreeNode* b) { return a < b; });

    if (cfg.enable_coalescing) {
      // Merge contiguous blocks:
      // If block A ends exactly where block B begins, merge into A.
      std::vector<FreeNode*> merged;
      merged.reserve(blocks.size());

      FreeNode* cur = blocks[0];
      for (std::size_t i = 1; i < blocks.size(); ++i) {
        FreeNode* nxt = blocks[i];
        auto* cur_b = reinterpret_cast<std::byte*>(cur);
        auto* nxt_b = reinterpret_cast<std::byte*>(nxt);

        std::byte* cur_end = cur_b + cur->size;
        if (cur_end == nxt_b) {
          // contiguous: merge
          cur->size += nxt->size;
        } else {
          merged.push_back(cur);
          cur = nxt;
        }
      }
      merged.push_back(cur);
      blocks.swap(merged);
    }

    // Rebucket into bins (this is cheap but improves bin locality).
    if (cfg.enable_rebucket) {
      for (auto* n : blocks) {
        n->next = nullptr;
        std::size_t bi = LayerState::bin_index(n->size);
        n->next = L.bins[bi];
        L.bins[bi] = n;
      }
    } else {
      // Put everything into a “big bin” if rebucket disabled
      for (auto* n : blocks) {
        n->next = L.bins[LayerState::kBins - 1];
        L.bins[LayerState::kBins - 1] = n;
      }
    }
  }
}

} // namespace picas
