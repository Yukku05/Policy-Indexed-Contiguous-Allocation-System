#!/usr/bin/env bash
set -e

echo "==> Configuring PICAS (Release)"

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DPICAS_BUILD_TESTS=ON \
  -DPICAS_BUILD_EXAMPLES=ON \
  -DPICAS_BUILD_BENCHMARKS=ON
