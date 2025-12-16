#!/usr/bin/env bash
set -e

echo "==> Building PICAS (ASAN + UBSAN)"

cmake --build build_asan -j$(sysctl -n hw.ncpu)
