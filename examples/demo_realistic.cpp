#include "picas/picas.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <random>
#include <cstring>
#include <iomanip>
#include <chrono>

using namespace picas;

static const char* etype(EventType t) {
  switch (t) {
    case EventType::Alloc: return "Alloc";
    case EventType::Free: return "Free";
    case EventType::Realloc: return "Realloc";
    case EventType::JumpToNextLayer: return "Jump";
    case EventType::DataAdvancedMemoryBackfill: return "Backfill";
    case EventType::MemorySpillToOtherLayer: return "Spill";
    case EventType::LayerMemTPReached: return "MEM-TP";
    case EventType::LayerTLPReached: return "TLP";
    case EventType::LayerDataLPReached: return "DATA-LP";
    case EventType::Scavenge: return "Scavenge";
    case EventType::FallbackAlloc: return "Fallback";
    case EventType::OutOfMemory: return "OOM";
  }
  return "?";
}

// ----- Utilities -----
static std::size_t parse_size(const std::string& s) {
  // supports suffix: K, M, G (binary-ish: 1024)
  if (s.empty()) return 0;
  char last = s.back();
  std::size_t mul = 1;
  std::string num = s;

  if (last == 'K' || last == 'k') { mul = 1024ull; num.pop_back(); }
  else if (last == 'M' || last == 'm') { mul = 1024ull * 1024; num.pop_back(); }
  else if (last == 'G' || last == 'g') { mul = 1024ull * 1024 * 1024; num.pop_back(); }

  std::size_t v = 0;
  try { v = static_cast<std::size_t>(std::stoull(num)); } catch (...) { return 0; }
  return v * mul;
}

static void print_help() {
  std::cout <<
R"(PICAS Interactive Demo (real allocator core)
Commands:
  help
  stats
  layer <L>                      (set current data layer)
  hook on|off                    (enable/disable event logging)
  trace on|off                   (enable/disable trace recording)
  dump                           (print last ~20 trace entries)
  csv                            (print full trace CSV to stdout)
  clear                          (clear local handle table, frees all outstanding)
  alloc <SIZE>                   (alloc bytes, e.g. alloc 128, alloc 4K, alloc 2M)
  calloc <N> <SIZE>              (alloc N*SIZE and memset 0)
  realloc <ID> <SIZE>            (realloc handle ID)
  free <ID>                      (free handle ID)

Workload runners:
  run_fixed <N> <SIZE>           (allocate N blocks, keep them)
  run_mix <N> <MIN> <MAX> <FREEP> (random sizes, free probability FREEP in [0..1])
  run_phases <P> <N> <MIN> <MAX>  (P phases, each phase runs N ops, auto layer++)
  fill <SIZE>                    (allocate until PICAS+fallback fail)

Notes:
- Handles are indices printed after alloc. Example:
    alloc 256
    free 0
- This demo uses picas_malloc/free/realloc (actual allocator).
)";
}

struct Handle {
  void* ptr = nullptr;
  std::size_t size = 0;
  bool alive = false;
};

static void print_stats() {
  auto* inst = picas_instance();
  if (!inst) return;
  auto st = inst->stats();
  std::cout << "Reserved(OS): " << st.total_reserved
            << " bytes | Capacity: " << st.total_capacity
            << " bytes | Live(est): " << st.total_live_est
            << " bytes | Current DL=" << inst->data_layer()
            << "\n";
}

static void dump_trace_last(std::size_t n = 20) {
  auto* inst = picas_instance();
  if (!inst) return;
  auto snap = inst->tracer().snapshot();
  if (snap.empty()) { std::cout << "(trace empty)\n"; return; }
  std::size_t start = (snap.size() > n) ? (snap.size() - n) : 0;

  std::cout << "Last " << (snap.size() - start) << " trace entries:\n";
  for (std::size_t i = start; i < snap.size(); ++i) {
    const auto& e = snap[i];
    std::cout
      << "  #" << e.seq
      << " DL=" << e.data_layer
      << " ML=" << e.mem_layer
      << " size=" << e.size
      << " offset=" << e.layer_offset
      << " penalty=" << e.penalty_cost
      << (e.note ? " | " : "") << (e.note ? e.note : "")
      << "\n";
  }
}

static void print_csv() {
  auto* inst = picas_instance();
  if (!inst) return;
  std::cout << inst->tracer().to_csv();
}

static void free_all(std::vector<Handle>& handles) {
  for (auto& h : handles) {
    if (h.alive && h.ptr) {
      picas_free(h.ptr);
      h.ptr = nullptr;
      h.size = 0;
      h.alive = false;
    }
  }
}

static int store_handle(std::vector<Handle>& handles, void* p, std::size_t sz) {
  for (std::size_t i = 0; i < handles.size(); ++i) {
    if (!handles[i].alive) {
      handles[i] = {p, sz, true};
      return static_cast<int>(i);
    }
  }
  handles.push_back({p, sz, true});
  return static_cast<int>(handles.size() - 1);
}

static void run_fixed(std::vector<Handle>& handles, std::size_t N, std::size_t sz) {
  for (std::size_t i = 0; i < N; ++i) {
    void* p = picas_malloc(sz);
    if (!p) { std::cout << "OOM at i=" << i << "\n"; break; }
    int id = store_handle(handles, p, sz);
    std::cout << "alloc id=" << id << " size=" << sz << "\n";
  }
}

static void run_mix(std::vector<Handle>& handles, std::size_t N,
                    std::size_t min_sz, std::size_t max_sz, double freep,
                    std::uint64_t seed = 123) {
  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<std::size_t> dist(min_sz, max_sz);
  std::uniform_real_distribution<double> prob(0.0, 1.0);

  for (std::size_t i = 0; i < N; ++i) {
    if (!handles.empty() && prob(rng) < freep) {
      // free a random alive handle
      std::uniform_int_distribution<std::size_t> pick(0, handles.size() - 1);
      std::size_t idx = pick(rng);
      // find alive
      for (std::size_t tries = 0; tries < handles.size(); ++tries) {
        std::size_t j = (idx + tries) % handles.size();
        if (handles[j].alive && handles[j].ptr) {
          picas_free(handles[j].ptr);
          handles[j].alive = false;
          handles[j].ptr = nullptr;
          handles[j].size = 0;
          break;
        }
      }
    } else {
      std::size_t sz = dist(rng);
      void* p = picas_malloc(sz);
      if (!p) { std::cout << "OOM at op=" << i << "\n"; break; }
      // write a tiny pattern (makes it feel more real)
      std::memset(p, 0xAB, std::min<std::size_t>(sz, 32));
      store_handle(handles, p, sz);
    }
  }
}

static void run_phases(std::vector<Handle>& handles, std::size_t P, std::size_t N,
                       std::size_t min_sz, std::size_t max_sz) {
  auto* inst = picas_instance();
  if (!inst) return;

  for (std::size_t phase = 0; phase < P; ++phase) {
    std::uint32_t dl = (inst->data_layer() + 1) % 3;
    picas_set_data_layer(dl);
    std::cout << "== Phase " << phase << " set DL=" << dl << " ==\n";
    run_mix(handles, N, min_sz, max_sz, 0.35, 1000 + phase);
    print_stats();
  }
}

static void fill_until_fail(std::vector<Handle>& handles, std::size_t sz) {
  std::size_t i = 0;
  while (true) {
    void* p = picas_malloc(sz);
    if (!p) {
      std::cout << "Allocation failed after " << i << " allocations of size " << sz << "\n";
      break;
    }
    store_handle(handles, p, sz);
    ++i;
    if (i % 1000 == 0) print_stats();
  }
}

int main() {
  // ---- Configure a practical default PICAS ----
  Config cfg;
  cfg.num_layers = 3;
  cfg.penalty_k = 10.0;
  cfg.strict_picas_jumps = true;
  cfg.enable_event_hooks = true;
  cfg.enable_tracing = true;

  // Memory layers: 64MB each by default (can be changed by editing here)
  constexpr std::size_t MB = 1024ull * 1024;
  cfg.mem_layers[0] = { 64 * MB, 48 * MB }; // MEM-TP at 75%
  cfg.mem_layers[1] = { 64 * MB, 48 * MB };
  cfg.mem_layers[2] = { 64 * MB, 48 * MB };

  // Hybrid points:
  // TLP = early checkpoint: 1000 allocs OR 16MB
  // DATA-LP = hard phase boundary: 5000 allocs OR 48MB
  for (std::size_t i = 0; i < cfg.num_layers; ++i) {
    cfg.data_layers[i].tlp.count = {0, 1000};
    cfg.data_layers[i].tlp.bytes = {0, 16 * MB};
    cfg.data_layers[i].tlp.logic = Logic::Any;

    cfg.data_layers[i].data_lp.count = {0, 5000};
    cfg.data_layers[i].data_lp.bytes = {0, 48 * MB};
    cfg.data_layers[i].data_lp.logic = Logic::Any;
  }

  // Safety (real-time friendly)
  cfg.safety.max_layer_probes = 8;
  cfg.safety.always_fallback_on_fail = true;            // guarantee progress
  cfg.safety.fallback.mode = FallbackMode::SystemMalloc; // safest default

  // Anti-stranding
  cfg.safety.anti_stranding.enabled = true;
  cfg.safety.anti_stranding.max_stranded_per_layer = 2 * MB;
  cfg.safety.anti_stranding.allow_jump_if_pressure = true;
  cfg.safety.anti_stranding.aggressive_backfill = true;

  // Scavenger maintenance
  cfg.scavenger.enabled = true;
  cfg.scavenger.period_allocs = 4096;
  cfg.scavenger.enable_coalescing = true;
  cfg.scavenger.enable_rebucket = true;

  // Debug pause off by default
  cfg.enable_debug_pause = false;

  picas_init(cfg);

  bool hook_on = false;
  picas_set_event_hook([&](const Event& e) {
    if (!hook_on) return;
    std::cout
      << "[" << etype(e.type) << "] DL=" << e.data_layer
      << " ML=" << e.mem_layer
      << " size=" << e.size
      << (e.note ? " | " : "") << (e.note ? e.note : "")
      << "\n";
  });

  std::vector<Handle> handles;
  std::cout << "PICAS demo started. Type 'help'.\n";

  std::string line;
  while (true) {
    std::cout << "picas> " << std::flush;
    if (!std::getline(std::cin, line)) break;
    if (line.empty()) continue;

    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    if (cmd == "help") {
      print_help();
    } else if (cmd == "quit" || cmd == "exit") {
      break;
    } else if (cmd == "stats") {
      print_stats();
    } else if (cmd == "layer") {
      std::uint32_t L = 0;
      iss >> L;
      picas_set_data_layer(L);
      std::cout << "set DL=" << L << "\n";
    } else if (cmd == "hook") {
      std::string v; iss >> v;
      hook_on = (v == "on");
      std::cout << "hook " << (hook_on ? "on" : "off") << "\n";
    } else if (cmd == "trace") {
      std::string v; iss >> v;
      auto* inst = picas_instance();
      if (inst) inst->tracer().enable(v == "on");
      std::cout << "trace " << v << "\n";
    } else if (cmd == "dump") {
      dump_trace_last(20);
    } else if (cmd == "csv") {
      print_csv();
    } else if (cmd == "clear") {
      free_all(handles);
      handles.clear();
      std::cout << "cleared handles (freed all)\n";
    } else if (cmd == "alloc") {
      std::string s; iss >> s;
      std::size_t sz = parse_size(s);
      if (sz == 0) { std::cout << "bad size\n"; continue; }
      void* p = picas_malloc(sz);
      if (!p) { std::cout << "alloc failed\n"; continue; }
      int id = store_handle(handles, p, sz);
      std::cout << "alloc id=" << id << " ptr=" << p << " size=" << sz << "\n";
    } else if (cmd == "calloc") {
      std::string a,b; iss >> a >> b;
      std::size_t n = parse_size(a);
      std::size_t sz = parse_size(b);
      if (n == 0 || sz == 0) { std::cout << "bad args\n"; continue; }
      std::size_t total = n * sz;
      void* p = picas_malloc(total);
      if (!p) { std::cout << "calloc failed\n"; continue; }
      std::memset(p, 0, total);
      int id = store_handle(handles, p, total);
      std::cout << "calloc id=" << id << " ptr=" << p << " bytes=" << total << "\n";
    } else if (cmd == "realloc") {
      int id = -1; std::string s;
      iss >> id >> s;
      std::size_t sz = parse_size(s);
      if (id < 0 || (std::size_t)id >= handles.size() || !handles[id].alive) {
        std::cout << "bad id\n"; continue;
      }
      void* np = picas_realloc(handles[id].ptr, sz);
      if (!np) { std::cout << "realloc failed\n"; continue; }
      handles[id].ptr = np;
      handles[id].size = sz;
      handles[id].alive = true;
      std::cout << "realloc id=" << id << " ptr=" << np << " size=" << sz << "\n";
    } else if (cmd == "free") {
      int id = -1; iss >> id;
      if (id < 0 || (std::size_t)id >= handles.size() || !handles[id].alive) {
        std::cout << "bad id\n"; continue;
      }
      picas_free(handles[id].ptr);
      handles[id].alive = false;
      handles[id].ptr = nullptr;
      handles[id].size = 0;
      std::cout << "freed id=" << id << "\n";
    } else if (cmd == "run_fixed") {
      std::size_t N; std::string s;
      iss >> N >> s;
      std::size_t sz = parse_size(s);
      if (N == 0 || sz == 0) { std::cout << "bad args\n"; continue; }
      run_fixed(handles, N, sz);
    } else if (cmd == "run_mix") {
      std::size_t N; std::string a,b; double freep = 0.3;
      iss >> N >> a >> b >> freep;
      std::size_t min_sz = parse_size(a);
      std::size_t max_sz = parse_size(b);
      if (N == 0 || min_sz == 0 || max_sz == 0 || min_sz > max_sz) { std::cout << "bad args\n"; continue; }
      run_mix(handles, N, min_sz, max_sz, freep, 777);
      std::cout << "done\n";
    } else if (cmd == "run_phases") {
      std::size_t P,N; std::string a,b;
      iss >> P >> N >> a >> b;
      std::size_t min_sz = parse_size(a);
      std::size_t max_sz = parse_size(b);
      if (P == 0 || N == 0 || min_sz == 0 || max_sz == 0 || min_sz > max_sz) { std::cout << "bad args\n"; continue; }
      run_phases(handles, P, N, min_sz, max_sz);
      std::cout << "done\n";
    } else if (cmd == "fill") {
      std::string a; iss >> a;
      std::size_t sz = parse_size(a);
      if (sz == 0) { std::cout << "bad size\n"; continue; }
      fill_until_fail(handles, sz);
    } else {
      std::cout << "unknown command. type 'help'\n";
    }
  }

  free_all(handles);
  picas_shutdown();
  std::cout << "bye\n";
  return 0;
}
