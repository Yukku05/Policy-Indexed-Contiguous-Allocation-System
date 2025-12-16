#!/usr/bin/env bash
set -euo pipefail

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DPICAS_BUILD_TESTS=ON \
  -DPICAS_BUILD_EXAMPLES=ON \
  -DPICAS_BUILD_BENCHMARKS=ON

cmake --build build -j
echo "[OK] release build finished"
