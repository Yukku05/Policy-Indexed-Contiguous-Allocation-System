#include "picas/fallback.hpp"
#include "picas/os_pages.hpp"

#include <cstdlib>
#include <mutex>
#include <cstring>
#include <algorithm>

namespace picas {

static FallbackConfig g_cfg{};
static Pages g_emergency{};
static std::byte* g_bump = nullptr;
static std::mutex g_mtx;

static constexpr std::uint32_t kFallbackMagic = 0x46414C4C; // 'FALL'
static constexpr std::size_t   kAlign = 16;

static inline std::size_t align_up(std::size_t x, std::size_t a) {
  return (x + (a - 1)) & ~(a - 1);
}

struct FallbackHeader {
  std::uint32_t magic = kFallbackMagic;
  std::uint32_t mode  = 0;
  std::size_t   user_size = 0;   // requested bytes
  std::size_t   total_size = 0;  // header + aligned payload (for internal use)
};

static inline FallbackHeader* hdr_from_user(void* p) {
  return reinterpret_cast<FallbackHeader*>(reinterpret_cast<std::byte*>(p) - sizeof(FallbackHeader));
}

static inline bool header_ok(const FallbackHeader* h) {
  return h && h->magic == kFallbackMagic;
}

void fallback_init(const FallbackConfig& cfg) {
  g_cfg = cfg;

  if (g_cfg.mode == FallbackMode::EmergencyReserve) {
    const std::size_t ps = os_page_size();
    const std::size_t bytes = align_up(std::max<std::size_t>(g_cfg.emergency_bytes, ps), ps);
    g_emergency = os_reserve_and_commit(bytes);
    g_bump = reinterpret_cast<std::byte*>(g_emergency.base);
  }
}

void fallback_shutdown() {
  if (g_emergency.base) os_release(g_emergency);
  g_emergency = {};
  g_bump = nullptr;
}

bool fallback_owns(void* p) {
  if (!p) return false;

  // Our ownership detection is header-based for ALL modes
  // so even SystemMalloc fallback can be recognized safely.
  auto* h = hdr_from_user(p);
  return header_ok(h);
}

std::size_t fallback_usable_size(void* p) {
  if (!p) return 0;
  auto* h = hdr_from_user(p);
  if (!header_ok(h)) return 0;
  return h->user_size;
}

void* fallback_alloc(std::size_t size) {
  if (size == 0) size = 1;

  const std::size_t payload = align_up(size, kAlign);
  const std::size_t total   = align_up(sizeof(FallbackHeader) + payload, kAlign);

  switch (g_cfg.mode) {
    case FallbackMode::None:
      return nullptr;

    case FallbackMode::SystemMalloc: {
      // Allocate header+payload as one block, return payload pointer
      void* raw = std::malloc(total);
      if (!raw) return nullptr;

      auto* h = reinterpret_cast<FallbackHeader*>(raw);
      h->magic = kFallbackMagic;
      h->mode  = static_cast<std::uint32_t>(FallbackMode::SystemMalloc);
      h->user_size  = size;
      h->total_size = total;

      void* user = reinterpret_cast<void*>(reinterpret_cast<std::byte*>(raw) + sizeof(FallbackHeader));
      return user;
    }

    case FallbackMode::EmergencyReserve: {
      std::lock_guard<std::mutex> lock(g_mtx);
      if (!g_emergency.base) return nullptr;

      auto* base = reinterpret_cast<std::byte*>(g_emergency.base);
      auto* end  = base + g_emergency.size;

      // bump allocation inside reserve
      if (!g_bump || g_bump + total > end) return nullptr;

      auto* h = reinterpret_cast<FallbackHeader*>(g_bump);
      h->magic = kFallbackMagic;
      h->mode  = static_cast<std::uint32_t>(FallbackMode::EmergencyReserve);
      h->user_size  = size;
      h->total_size = total;

      void* user = reinterpret_cast<void*>(g_bump + sizeof(FallbackHeader));
      g_bump += total;
      return user;
    }
  }

  return nullptr;
}

void fallback_free(void* p) {
  if (!p) return;

  auto* h = hdr_from_user(p);
  if (!header_ok(h)) {
    // Not ours => ignore (or assert in debug)
    return;
  }

  const auto mode = static_cast<FallbackMode>(h->mode);

  switch (mode) {
    case FallbackMode::None:
      return;

    case FallbackMode::SystemMalloc: {
      // free the original raw block
      std::free(reinterpret_cast<void*>(h));
      return;
    }

    case FallbackMode::EmergencyReserve:
      // bump-only => cannot free; no-op
      return;
  }
}

} // namespace picas
