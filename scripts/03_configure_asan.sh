#!/usr/bin/env bash
set -e

echo "==> Configuring PICAS (ASAN + UBSAN)"

cmake -S . -B build_asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPICAS_SANITIZE=ON \
  -DPICAS_BUILD_TESTS=ON \
  -DPICAS_BUILD_EXAMPLES=ON \
  -DPICAS_BUILD_BENCHMARKS=ON
