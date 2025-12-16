#!/usr/bin/env bash
set -e

echo "==> Running PICAS tests (ASAN + UBSAN)"

# ONLY FOR LINUX
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
ctest --test-dir build_asan \
  --output-on-failure \
  -V
