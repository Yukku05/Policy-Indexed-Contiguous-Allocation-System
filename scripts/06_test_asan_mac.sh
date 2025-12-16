#!/usr/bin/env bash
set -e

echo "==> Running PICAS tests (ASAN + UBSAN)"

# macOS (AppleClang) ASan: LeakSanitizer is not supported => detect_leaks must be 0
if [[ "$(uname)" == "Darwin" ]]; then
  export ASAN_OPTIONS="detect_leaks=0:halt_on_error=1"
else
  export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1"
fi

export UBSAN_OPTIONS="halt_on_error=1"

ctest --test-dir build_asan \
  --output-on-failure \
  -V
