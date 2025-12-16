#!/usr/bin/env bash
set -euo pipefail

cmake -S . -B build_san \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPICAS_SANITIZE=ON \
  -DPICAS_BUILD_TESTS=ON \
  -DPICAS_BUILD_EXAMPLES=ON \
  -DPICAS_BUILD_BENCHMARKS=ON

cmake --build build_san -j
echo "[OK] sanitize build finished"
