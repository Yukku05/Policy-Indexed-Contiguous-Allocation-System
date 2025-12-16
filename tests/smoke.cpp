#include "picas/picas.hpp"
#include <iostream>
#include <vector>
#include <cstring>

using namespace picas;

static bool smoke_basic_alloc_free() {
  void* p = picas_malloc(128);
  if (!p) return false;
  std::memset(p, 0xAA, 128);
  picas_free(p);
  return true;
}

static bool smoke_realloc_grow_shrink() {
  void* p = picas_malloc(64);
  if (!p) return false;
  std::memset(p, 0x11, 64);

  void* q = picas_realloc(p, 1024);
  if (!q) return false;
  // old bytes should still be there
  auto* qb = reinterpret_cast<unsigned char*>(q);
  if (qb[0] != 0x11) return false;

  void* r = picas_realloc(q, 32);
  if (!r) return false;
  picas_free(r);
  return true;
}

static bool smoke_many_mixed() {
  std::vector<void*> v;
  for (int i = 0; i < 10000; ++i) {
    void* p = picas_malloc((i % 256) + 1);
    if (!p) break;
    v.push_back(p);
    if (i % 3 == 0 && !v.empty()) {
      picas_free(v.back());
      v.pop_back();
    }
  }
  for (auto* p : v) picas_free(p);
  return true;
}

// An interactive “test playground” so users can validate behavior quickly.
static void interactive_test_mode() {
  std::cout <<
R"(PICAS Smoke Interactive Mode
Type:
  a <bytes>      alloc
  f <id>         free
  r <id> <bytes> realloc
  l <layer>      set data layer
  s              stats
  q              quit
)";
  struct H { void* p=nullptr; std::size_t sz=0; bool alive=false; };
  std::vector<H> Hs;

  auto add = [&](void* p, std::size_t sz) {
    for (std::size_t i=0;i<Hs.size();++i) if (!Hs[i].alive) { Hs[i]={p,sz,true}; return (int)i; }
    Hs.push_back({p,sz,true});
    return (int)Hs.size()-1;
  };

  std::string cmd;
  while (true) {
    std::cout << "smoke> " << std::flush;
    if (!(std::cin >> cmd)) break;
    if (cmd == "q") break;
    if (cmd == "a") {
      std::size_t b; std::cin >> b;
      void* p = picas_malloc(b);
      if (!p) { std::cout << "alloc failed\n"; continue; }
      int id = add(p,b);
      std::cout << "id=" << id << " ptr=" << p << "\n";
    } else if (cmd == "f") {
      int id; std::cin >> id;
      if (id < 0 || (std::size_t)id >= Hs.size() || !Hs[id].alive) { std::cout << "bad id\n"; continue; }
      picas_free(Hs[id].p);
      Hs[id].alive=false; Hs[id].p=nullptr; Hs[id].sz=0;
      std::cout << "freed\n";
    } else if (cmd == "r") {
      int id; std::size_t b; std::cin >> id >> b;
      if (id < 0 || (std::size_t)id >= Hs.size() || !Hs[id].alive) { std::cout << "bad id\n"; continue; }
      void* np = picas_realloc(Hs[id].p, b);
      if (!np) { std::cout << "realloc failed\n"; continue; }
      Hs[id].p = np; Hs[id].sz = b; Hs[id].alive = true;
      std::cout << "ok ptr=" << np << "\n";
    } else if (cmd == "l") {
      std::uint32_t L; std::cin >> L;
      picas_set_data_layer(L);
      std::cout << "DL=" << L << "\n";
    } else if (cmd == "s") {
      auto* inst = picas_instance();
      auto st = inst->stats();
      std::cout << "reserved=" << st.total_reserved << " cap=" << st.total_capacity << " live_est=" << st.total_live_est
                << " DL=" << inst->data_layer() << "\n";
    } else {
      std::cout << "unknown\n";
    }
  }

  for (auto& h : Hs) if (h.alive && h.p) picas_free(h.p);
}

int main(int argc, char** argv) {
  Config cfg;
  cfg.num_layers = 3;
  cfg.penalty_k = 10.0;
  cfg.strict_picas_jumps = true;
  cfg.enable_event_hooks = false;
  cfg.enable_tracing = false;

  constexpr std::size_t MB = 1024ull * 1024;
  cfg.mem_layers[0] = { 16 * MB, 12 * MB };
  cfg.mem_layers[1] = { 16 * MB, 12 * MB };
  cfg.mem_layers[2] = { 16 * MB, 12 * MB };

  for (std::size_t i = 0; i < cfg.num_layers; ++i) {
    cfg.data_layers[i].tlp.count = {0, 300};
    cfg.data_layers[i].tlp.bytes = {0, 2 * MB};
    cfg.data_layers[i].tlp.logic = Logic::Any;

    cfg.data_layers[i].data_lp.count = {0, 2000};
    cfg.data_layers[i].data_lp.bytes = {0, 12 * MB};
    cfg.data_layers[i].data_lp.logic = Logic::Any;
  }

  cfg.safety.max_layer_probes = 8;
  cfg.safety.always_fallback_on_fail = true;
  cfg.safety.fallback.mode = FallbackMode::SystemMalloc;

  cfg.safety.anti_stranding.enabled = true;
  cfg.safety.anti_stranding.max_stranded_per_layer = 512 * 1024;

  cfg.scavenger.enabled = true;
  cfg.scavenger.period_allocs = 1024;
  cfg.scavenger.enable_coalescing = true;

  picas_init(cfg);

  // interactive toggle
  if (argc >= 2 && std::string(argv[1]) == "--interactive") {
    interactive_test_mode();
    picas_shutdown();
    return 0;
  }

  bool ok = true;
  ok = ok && smoke_basic_alloc_free();
  ok = ok && smoke_realloc_grow_shrink();
  ok = ok && smoke_many_mixed();

  picas_shutdown();

  if (!ok) {
    std::cerr << "SMOKE FAILED\n";
    return 1;
  }
  std::cout << "SMOKE OK\n";
  return 0;
}
