#include "picas/picas.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static bool is_aligned(void* p, std::size_t a) {
  return (reinterpret_cast<std::uintptr_t>(p) & (a - 1)) == 0;
}

int main() {
  picas::Config cfg{};
  cfg.num_layers = 3;

  // give layers some space (adjust if your defaults differ)
  cfg.mem_layers[0].bytes = 8 * 1024 * 1024;
  cfg.mem_layers[1].bytes = 8 * 1024 * 1024;
  cfg.mem_layers[2].bytes = 8 * 1024 * 1024;

  cfg.enable_event_hooks = false;
  cfg.enable_tracing = false;
  cfg.safety.always_fallback_on_fail = true;

  picas::picas_init(cfg);

  const std::vector<std::size_t> aligns = {16, 32, 64, 128, 256, 4096};
  for (auto a : aligns) {
    for (int i = 0; i < 2000; ++i) {
      const std::size_t sz = (i % 257) + 1;
      void* p = picas::picas_memalign(a, sz);
      assert(p && "memalign returned null");
      assert(is_aligned(p, a) && "pointer not aligned");

      // fill / verify
      std::memset(p, 0xAB, sz);
      for (std::size_t k = 0; k < sz; ++k) {
        assert(reinterpret_cast<unsigned char*>(p)[k] == 0xAB);
      }

      // realloc grow
      void* q = picas::picas_realloc(p, sz * 2);
      assert(q && "realloc grow failed");
      // old bytes preserved
      for (std::size_t k = 0; k < sz; ++k) {
        assert(reinterpret_cast<unsigned char*>(q)[k] == 0xAB);
      }

      picas::picas_free(q);
    }
  }

  // also test normal malloc still OK
  for (int i = 0; i < 10000; ++i) {
    void* p = picas::picas_malloc(33);
    assert(p);
    picas::picas_free(p);
  }

  picas::picas_shutdown();
  std::puts("[OK] test_alignment");
  return 0;
}
