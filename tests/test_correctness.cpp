// tests/test_correctness.cpp
#include "picas/picas.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <random>
#include <atomic>

#define REQUIRE(cond) do { \
  if (!(cond)) { \
    std::fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    std::exit(1); \
  } \
} while (0)

static bool is_aligned(void* p, std::size_t a) {
  return (reinterpret_cast<std::uintptr_t>(p) & (a - 1)) == 0;
}

static picas::Config make_test_config() {
  picas::Config cfg{};
  // Keep it simple: define a small-ish arena that still allows tests to run.
  // These fields exist in your codebase (you used them in picas.cpp):
  cfg.num_layers = 3;

  // layer capacities
  cfg.mem_layers[0].bytes = 256 * 1024;   // 256KB
  cfg.mem_layers[1].bytes = 256 * 1024;
  cfg.mem_layers[2].bytes = 256 * 1024;

  // mem-tp points (optional)
  cfg.mem_layers[0].mem_tp_bytes = 128 * 1024;
  cfg.mem_layers[1].mem_tp_bytes = 128 * 1024;
  cfg.mem_layers[2].mem_tp_bytes = 128 * 1024;

  // Enable event hooks for visibility (safe)
  cfg.enable_event_hooks = true;

  // Make allocator resilient in tests (if your fallback supports it)
  cfg.safety.always_fallback_on_fail = true;

  // Keep scavenger light (or off) for deterministic behavior
  cfg.scavenger.enabled = false;

  return cfg;
}

static void test_basic_malloc_free() {
  std::fprintf(stderr, "[TEST] basic malloc/free...\n");

  void* p = picas::picas_malloc(64);
  REQUIRE(p != nullptr);
  std::memset(p, 0xAB, 64);
  picas::picas_free(p);

  // free(nullptr) should be safe
  picas::picas_free(nullptr);
}

static void test_realloc_grow_shrink() {
  std::fprintf(stderr, "[TEST] realloc grow/shrink...\n");

  void* p = picas::picas_malloc(32);
  REQUIRE(p != nullptr);
  std::memset(p, 0x11, 32);

  // grow
  void* q = picas::picas_realloc(p, 128);
  REQUIRE(q != nullptr);

  // old bytes preserved (at least first 32)
  REQUIRE(std::memcmp(q, std::vector<unsigned char>(32, 0x11).data(), 32) == 0);

  // shrink (should keep pointer or move; both OK, but data must remain for first N)
  void* r = picas::picas_realloc(q, 16);
  REQUIRE(r != nullptr);
  REQUIRE(std::memcmp(r, std::vector<unsigned char>(16, 0x11).data(), 16) == 0);

  picas::picas_free(r);

  // realloc(nullptr, n) == malloc(n)
  void* a = picas::picas_realloc(nullptr, 40);
  REQUIRE(a != nullptr);
  picas::picas_free(a);

  // realloc(p,0) == free(p) and return nullptr
  void* b = picas::picas_malloc(10);
  REQUIRE(b != nullptr);
  void* c = picas::picas_realloc(b, 0);
  REQUIRE(c == nullptr);
}

static void test_memalign_and_usable_size() {
  std::fprintf(stderr, "[TEST] memalign + usable_size...\n");

  constexpr std::size_t A = 64;
  constexpr std::size_t N = 100;

  void* p = picas::picas_memalign(A, N);
  REQUIRE(p != nullptr);
  REQUIRE(is_aligned(p, A));

  // usable size should be >= requested (your implementation returns requested for memalign)
  std::size_t us = picas::picas_usable_size(p);
  REQUIRE(us == N);

  std::memset(p, 0xCD, N);
  picas::picas_free(p);
}

static void test_calloc_zeroed() {
  std::fprintf(stderr, "[TEST] calloc zeroed...\n");

  constexpr std::size_t n = 64;
  constexpr std::size_t sz = 8;
  void* p = picas::picas_calloc(n, sz);
  REQUIRE(p != nullptr);

  const std::size_t total = n * sz;
  const unsigned char* b = reinterpret_cast<const unsigned char*>(p);
  for (std::size_t i = 0; i < total; ++i) {
    REQUIRE(b[i] == 0);
  }

  picas::picas_free(p);
}

static void test_stress_mix() {
  std::fprintf(stderr, "[TEST] stress mix alloc/free...\n");

  std::vector<void*> ptrs;
  ptrs.reserve(2000);

  // allocate many small blocks
  for (int i = 0; i < 1500; ++i) {
    std::size_t sz = (i % 128) + 1;
    void* p = picas::picas_malloc(sz);
    REQUIRE(p != nullptr);
    std::memset(p, (i & 0xFF), sz);
    ptrs.push_back(p);
  }

  // shuffle then free
  std::mt19937 rng(123);
  std::shuffle(ptrs.begin(), ptrs.end(), rng);

  for (void* p : ptrs) {
    picas::picas_free(p);
  }
}

static void test_fallback_path_best_effort() {
  std::fprintf(stderr, "[TEST] fallback path (best effort)...\n");

  // Request something "very large" relative to small arena — should force fallback or nullptr.
  // We don't hard-require fallback, because fallback config could be disabled by user.
  void* p = picas::picas_malloc(64 * 1024 * 1024); // 64MB
  if (!p) {
    std::fprintf(stderr, "  (note) large alloc returned nullptr (fallback may be disabled) — OK\n");
    return;
  }

  // If it succeeded, we can still touch a little, and free safely.
  std::memset(p, 0xEE, 4096);
  picas::picas_free(p);
}

int main() {
  std::fprintf(stderr, "=== PICAS correctness tests ===\n");

  // capture a few events just to ensure hook path doesn't crash
  std::atomic<int> ev_count{0};
  picas::picas_set_event_hook([&](picas::Event e) {
    (void)e;
    ev_count.fetch_add(1, std::memory_order_relaxed);
  });

  picas::Config cfg = make_test_config();
  picas::picas_init(cfg);

  test_basic_malloc_free();
  test_realloc_grow_shrink();
  test_memalign_and_usable_size();
  test_calloc_zeroed();
  test_stress_mix();
  test_fallback_path_best_effort();

  auto* inst = picas::picas_instance();
  if (inst) {
    auto st = inst->stats();
    std::fprintf(stderr, "[INFO] reserved=%zu capacity=%zu live_est=%zu\n",
                 st.total_reserved, st.total_capacity, st.total_live_est);
  }

  picas::picas_shutdown();

  std::fprintf(stderr, "[OK] all tests passed. events_seen=%d\n", ev_count.load());
  return 0;
}
