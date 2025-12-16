#include "picas/picas.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <cstring>

using namespace picas;

static double secs_since(const std::chrono::high_resolution_clock::time_point& t0) {
  using namespace std::chrono;
  return duration_cast<duration<double>>(high_resolution_clock::now() - t0).count();
}

int main(int argc, char** argv) {
  std::size_t ops = (argc >= 2) ? std::stoull(argv[1]) : 200000;
  std::size_t min_sz = (argc >= 3) ? std::stoull(argv[2]) : 16;
  std::size_t max_sz = (argc >= 4) ? std::stoull(argv[3]) : 8192;

  Config cfg;
  cfg.num_layers = 3;
  cfg.penalty_k = 10.0;
  cfg.strict_picas_jumps = true;
  cfg.enable_event_hooks = false;
  cfg.enable_tracing = false;

  constexpr std::size_t MB = 1024ull * 1024;
  cfg.mem_layers[0] = { 128 * MB, 96 * MB };
  cfg.mem_layers[1] = { 128 * MB, 96 * MB };
  cfg.mem_layers[2] = { 128 * MB, 96 * MB };

  for (std::size_t i = 0; i < cfg.num_layers; ++i) {
    cfg.data_layers[i].tlp.count = {0, 2000};
    cfg.data_layers[i].tlp.bytes = {0, 32 * MB};
    cfg.data_layers[i].tlp.logic = Logic::Any;

    cfg.data_layers[i].data_lp.count = {0, 12000};
    cfg.data_layers[i].data_lp.bytes = {0, 96 * MB};
    cfg.data_layers[i].data_lp.logic = Logic::Any;
  }

  cfg.safety.max_layer_probes = 8;
  cfg.safety.always_fallback_on_fail = true;
  cfg.safety.fallback.mode = FallbackMode::SystemMalloc;

  cfg.safety.anti_stranding.enabled = true;
  cfg.safety.anti_stranding.max_stranded_per_layer = 2 * MB;

  cfg.scavenger.enabled = true;
  cfg.scavenger.period_allocs = 8192;
  cfg.scavenger.enable_coalescing = true;

  picas_init(cfg);

  std::mt19937_64 rng(42);
  std::uniform_int_distribution<std::size_t> dist(min_sz, max_sz);
  std::uniform_real_distribution<double> prob(0.0, 1.0);

  std::vector<void*> live;
  live.reserve(50000);

  auto t0 = std::chrono::high_resolution_clock::now();

  for (std::size_t i = 0; i < ops; ++i) {
    if (!live.empty() && prob(rng) < 0.35) {
      std::size_t idx = (std::size_t)(rng() % live.size());
      picas_free(live[idx]);
      live[idx] = live.back();
      live.pop_back();
    } else {
      std::size_t sz = dist(rng);
      void* p = picas_malloc(sz);
      if (!p) break;
      std::memset(p, 0xEF, (sz < 32 ? sz : 32));
      live.push_back(p);
    }

    // periodic phase change
    if ((i % 50000) == 0) picas_set_data_layer((std::uint32_t)((i / 50000) % 3));
  }

  for (void* p : live) picas_free(p);

  auto elapsed = secs_since(t0);
  auto* inst = picas_instance();
  auto st = inst->stats();

  std::cout << "bench_mix ops=" << ops
            << " elapsed=" << elapsed << " sec"
            << " ops/sec=" << (ops / elapsed)
            << " reserved=" << st.total_reserved
            << " live_est=" << st.total_live_est
            << "\n";

  picas_shutdown();
  return 0;
}
