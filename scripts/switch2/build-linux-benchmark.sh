#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:-"$root/dist-switch2/linux-benchmarks"}
mkdir -p "$out"
"${CXX:-aarch64-linux-gnu-g++}" -O2 -std=c++17 -ffp-contract=off \
  -march=armv8-a -mtune=generic -static -pthread -Wall -Wextra \
  -I"$root/source/core" "$root/source/test/switch_linux_benchmark.cpp" \
  -o "$out/xsb-bench-aarch64"
file "$out/xsb-bench-aarch64"
