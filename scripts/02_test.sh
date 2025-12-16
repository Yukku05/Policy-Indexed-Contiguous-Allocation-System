#!/usr/bin/env bash
set -e

echo "==> Building PICAS (Release)"

cmake --build build -j$(sysctl -n hw.ncpu)
