#pragma once
#include <cstddef>
#include <cstdint>

namespace picas {

struct ScavengerConfig {
  bool enabled = true;

  // Every N allocations, do a light maintenance pass.
  std::uint64_t period_allocs = 4096;

  // Coalescing is optional; when enabled it reduces fragmentation but costs time.
  // We'll implement a conservative version in src/scavenger.cpp.
  bool enable_coalescing = true;

  // Rebucket free lists (cheap) to reduce worst-case free-list scanning.
  bool enable_rebucket = true;

  // Returning pages to OS requires page accounting; we leave it off by default.
  bool enable_os_release = false;
};

} // namespace picas
