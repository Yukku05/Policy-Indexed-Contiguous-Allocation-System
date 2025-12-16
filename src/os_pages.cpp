#include "picas/os_pages.hpp"

#if defined(_WIN32)
  #define NOMINMAX
  #include <windows.h>
#else
  #include <sys/mman.h>
  #include <unistd.h>
#endif

#include <stdexcept>

namespace picas {

std::size_t os_page_size() {
#if defined(_WIN32)
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return static_cast<std::size_t>(si.dwPageSize);
#else
  long ps = ::sysconf(_SC_PAGESIZE);
  return (ps > 0) ? static_cast<std::size_t>(ps) : 4096;
#endif
}

Pages os_reserve_and_commit(std::size_t bytes) {
  Pages p{};
#if defined(_WIN32)
  void* mem = ::VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (!mem) throw std::runtime_error("VirtualAlloc failed");
  p.base = mem;
  p.size = bytes;
#else
  void* mem = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mem == MAP_FAILED) throw std::runtime_error("mmap failed");
  p.base = mem;
  p.size = bytes;
#endif
  return p;
}

void os_release(Pages p) {
  if (!p.base || p.size == 0) return;
#if defined(_WIN32)
  ::VirtualFree(p.base, 0, MEM_RELEASE);
#else
  ::munmap(p.base, p.size);
#endif
}

} // namespace picas
