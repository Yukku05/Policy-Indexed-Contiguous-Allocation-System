#!/usr/bin/env bash
set -euo pipefail

dir="${1:-build}"

echo "== demo_realistic =="
"$dir/demo_realistic" || true
echo

echo "== demo_trace_dump =="
"$dir/demo_trace_dump" || true
echo

echo "== bench_mix =="
"$dir/bench_mix" || true
echo
