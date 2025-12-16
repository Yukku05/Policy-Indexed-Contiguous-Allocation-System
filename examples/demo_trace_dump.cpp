#include "picas/picas.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include <cstring>

using namespace picas;

int main(int argc, char** argv) {
  const char* out = (argc >= 2) ? argv[1] : "picas_trace.csv";

  Config cfg;
  cfg.num_layers = 3;
  cfg.penalty_k = 10.0;
  cfg.strict_picas_jumps = true;
  cfg.enable_event_hooks = false;
  cfg.enable_tracing = true;

  constexpr std::size_t MB = 1024ull * 1024;
  cfg.mem_layers[0] = { 32 * MB, 24 * MB };
  cfg.mem_layers[1] = { 32 * MB, 24 * MB };
  cfg.mem_layers[2] = { 32 * MB, 24 * MB };

  for (std::size_t i = 0; i < cfg.num_layers; ++i) {
    cfg.data_layers[i].tlp.count = {0, 500};
    cfg.data_layers[i].tlp.bytes = {0, 8 * MB};
    cfg.data_layers[i].tlp.logic = Logic::Any;

    cfg.data_layers[i].data_lp.count = {0, 2000};
    cfg.data_layers[i].data_lp.bytes = {0, 24 * MB};
    cfg.data_layers[i].data_lp.logic = Logic::Any;
  }

  cfg.safety.max_layer_probes = 8;
  cfg.safety.always_fallback_on_fail = true;
  cfg.safety.fallback.mode = FallbackMode::SystemMalloc;

  cfg.safety.anti_stranding.enabled = true;
  cfg.safety.anti_stranding.max_stranded_per_layer = 1 * MB;

  cfg.scavenger.enabled = true;
  cfg.scavenger.period_allocs = 2048;
  cfg.scavenger.enable_coalescing = true;

  picas_init(cfg);

  std::mt19937_64 rng(123);
  std::uniform_int_distribution<std::size_t> dist(16, 8192);
  std::uniform_real_distribution<double> prob(0.0, 1.0);

  std::vector<void*> live;
  live.reserve(10000);

  // Scripted: 20000 ops
  for (int i = 0; i < 20000; ++i) {
    if (!live.empty() && prob(rng) < 0.35) {
      std::size_t idx = (std::size_t)(rng() % live.size());
      picas_free(live[idx]);
      live[idx] = live.back();
      live.pop_back();
    } else {
      std::size_t sz = dist(rng);
      void* p = picas_malloc(sz);
      if (!p) break;
      std::memset(p, 0xCD, std::min<std::size_t>(sz, 32));
      live.push_back(p);
    }
    if ((i % 5000) == 0) {
      picas_set_data_layer((std::uint32_t)((i / 5000) % 3));
    }
  }

  for (void* p : live) picas_free(p);

  auto* inst = picas_instance();
  if (!inst) return 1;

  std::ofstream ofs(out, std::ios::binary);
  if (!ofs) {
    std::cerr << "failed to open " << out << "\n";
    return 2;
  }
  ofs << inst->tracer().to_csv();
  ofs.close();

  picas_shutdown();
  std::cout << "wrote trace to: " << out << "\n";
  return 0;
}
