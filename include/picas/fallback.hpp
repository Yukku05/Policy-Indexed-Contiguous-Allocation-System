#pragma once
#include <cstddef>
#include <cstdint>

namespace picas {

enum class FallbackMode : std::uint8_t {
  None,
  SystemMalloc,
  EmergencyReserve
};

struct FallbackConfig {
  FallbackMode mode = FallbackMode::SystemMalloc;
  std::size_t emergency_bytes = 2 * 1024 * 1024; // used if EmergencyReserve
};

// Initialize/shutdown fallback subsystem
void  fallback_init(const FallbackConfig& cfg);
void  fallback_shutdown();

// Allocate/free from fallback
void* fallback_alloc(std::size_t size);
void  fallback_free(void* p);

// True if pointer was allocated by fallback (works for SystemMalloc too, via header magic)
bool  fallback_owns(void* p);

// NEW: safe size query for fallback-owned pointers (0 if not fallback-owned)
std::size_t fallback_usable_size(void* p);

} // namespace picas
