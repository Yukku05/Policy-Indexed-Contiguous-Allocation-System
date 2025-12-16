cmake_minimum_required(VERSION 3.16)

project(picas LANGUAGES CXX)

# -----------------------------
# Build options
# -----------------------------
option(PICAS_BUILD_EXAMPLES   "Build PICAS examples" ON)
option(PICAS_BUILD_BENCHMARKS "Build PICAS benchmarks" ON)
option(PICAS_BUILD_TESTS      "Build PICAS tests" ON)
option(PICAS_SANITIZE         "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

# -----------------------------
# C++ standard
# -----------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default build type (nice for single-config generators)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# -----------------------------
# Threads (halting controller, mutex)
# -----------------------------
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# -----------------------------
# Sanitizer interface target (critical: applies to executables too)
# -----------------------------
if(PICAS_SANITIZE AND NOT MSVC)
  add_library(picas_sanitize INTERFACE)

  target_compile_options(picas_sanitize INTERFACE
    -O1 -g
    -fno-omit-frame-pointer
    -fsanitize=address,undefined
  )

  target_link_options(picas_sanitize INTERFACE
    -fsanitize=address,undefined
  )
endif()

function(picas_apply_sanitize target_name)
  if(PICAS_SANITIZE AND NOT MSVC)
    target_link_libraries(${target_name} PRIVATE picas_sanitize)
  endif()
endfunction()

# -----------------------------
# Library target
# -----------------------------
add_library(picas STATIC
  src/picas.cpp
  src/os_pages.cpp
  src/policy.cpp
  src/tracer.cpp
  src/safety.cpp
  src/scavenger.cpp
  src/fallback.cpp
)

target_include_directories(picas
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
  target_compile_options(picas PRIVATE /W4 /permissive-)
else()
  target_compile_options(picas PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(picas PUBLIC Threads::Threads)

if(UNIX AND NOT APPLE)
  target_link_libraries(picas PRIVATE m)
endif()

# instrument library too
picas_apply_sanitize(picas)

# -----------------------------
# Examples
# -----------------------------
if(PICAS_BUILD_EXAMPLES)
  add_executable(demo_realistic examples/demo_realistic.cpp)
  target_link_libraries(demo_realistic PRIVATE picas)
  picas_apply_sanitize(demo_realistic)

  add_executable(demo_trace_dump examples/demo_trace_dump.cpp)
  target_link_libraries(demo_trace_dump PRIVATE picas)
  picas_apply_sanitize(demo_trace_dump)
endif()

# -----------------------------
# Benchmarks
# -----------------------------
if(PICAS_BUILD_BENCHMARKS)
  add_executable(bench_mix benchmarks/bench_mix.cpp)
  target_link_libraries(bench_mix PRIVATE picas)
  picas_apply_sanitize(bench_mix)
endif()

# -----------------------------
# Tests
# -----------------------------
if(PICAS_BUILD_TESTS)
  enable_testing()

  add_executable(smoke tests/smoke.cpp)
  target_link_libraries(smoke PRIVATE picas)
  picas_apply_sanitize(smoke)
  add_test(NAME picas_smoke COMMAND smoke)

  add_executable(test_correctness tests/test_correctness.cpp)
  target_link_libraries(test_correctness PRIVATE picas)
  picas_apply_sanitize(test_correctness)
  add_test(NAME picas_correctness COMMAND test_correctness)
endif()
