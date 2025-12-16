#pragma once
#include <cstddef>

namespace picas {

struct Pages {
  void* base = nullptr;
  std::size_t size = 0;
};

Pages os_reserve_and_commit(std::size_t bytes);
void  os_release(Pages p);

std::size_t os_page_size();

} // namespace picas
