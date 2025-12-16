#include "picas/picas.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <cstdint>

int main() {
  picas::Config cfg{};
  cfg.num_layers = 2;
  cfg.mem_layers[0].bytes = 8 * 1024 * 1024;
  cfg.mem_layers[1].bytes = 8 * 1024 * 1024;
  cfg.safety.always_fallback_on_fail = true;

  picas::picas_init(cfg);

  // allocate a bunch, free every other, then re-allocate same sizes
  std::vector<void*> ptrs;
  ptrs.reserve(20000);

  for (int i = 0; i < 20000; ++i) {
    std::size_t sz = (i % 512) + 1;
    void* p = picas::picas_malloc(sz);
    assert(p);
    ptrs.push_back(p);
  }

  for (int i = 0; i < 20000; i += 2) {
    picas::picas_free(ptrs[i]);
    ptrs[i] = nullptr;
  }

  // reuse pressure
  for (int i = 0; i < 10000; ++i) {
    std::size_t sz = (i % 512) + 1;
    void* p = picas::picas_malloc(sz);
    assert(p);
    picas::picas_free(p);
  }

  for (void* p : ptrs) if (p) picas::picas_free(p);

  picas::picas_shutdown();
  std::puts("[OK] test_reuse_bins");
  return 0;
}
