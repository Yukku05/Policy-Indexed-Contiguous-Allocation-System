#include "picas/picas.hpp"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>
#include <random>
#include <atomic>

static void worker(int tid, int iters) {
  std::mt19937_64 rng(12345 + tid);
  std::uniform_int_distribution<int> szd(1, 2048);
  std::uniform_int_distribution<int> coin(0, 99);

  std::vector<void*> live;
  live.reserve(4096);

  for (int i = 0; i < iters; ++i) {
    int sz = szd(rng);
    if (coin(rng) < 15) {
      // aligned sometimes
      std::size_t a = (coin(rng) < 50) ? 64 : 256;
      void* p = picas::picas_memalign(a, (std::size_t)sz);
      assert(p);
      live.push_back(p);
    } else {
      void* p = picas::picas_malloc((std::size_t)sz);
      assert(p);
      live.push_back(p);
    }

    // random frees
    if (live.size() > 2000 && coin(rng) < 60) {
      std::size_t idx = (std::size_t)(rng() % live.size());
      picas::picas_free(live[idx]);
      live[idx] = live.back();
      live.pop_back();
    }

    // random realloc
    if (!live.empty() && coin(rng) < 10) {
      std::size_t idx = (std::size_t)(rng() % live.size());
      void* p = live[idx];
      std::size_t ns = (std::size_t)szd(rng) + 1;
      void* q = picas::picas_realloc(p, ns);
      assert(q);
      live[idx] = q;
    }
  }

  for (void* p : live) picas::picas_free(p);
}

int main() {
  picas::Config cfg{};
  cfg.num_layers = 4;
  for (int i = 0; i < 4; ++i) cfg.mem_layers[i].bytes = 16 * 1024 * 1024;
  cfg.safety.always_fallback_on_fail = true;
  cfg.scavenger.enabled = true;
  cfg.scavenger.period_allocs = 5000;

  picas::picas_init(cfg);

  const int T = 8;
  const int I = 40000;
  std::vector<std::thread> ts;
  for (int t = 0; t < T; ++t) ts.emplace_back(worker, t, I);
  for (auto& th : ts) th.join();

  picas::picas_shutdown();
  std::puts("[OK] test_multithread");
  return 0;
}
