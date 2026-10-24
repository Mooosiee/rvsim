#!/usr/bin/env bash
# Run from the repository root on Linux/WSL after installing dependencies in TESTING.md.
set -euo pipefail
root_dir="$(cd "$(dirname "$0")/.." && pwd)"
spike_src="$root_dir/.tools/spike-src"
spike_build="$root_dir/build-spike"
revision=530af85d83781a3dae31a4ace84a573ec255fefa
mkdir -p "$root_dir/.tools" "$spike_build"
if [ ! -d "$spike_src/.git" ]; then
  git clone --branch v1.1.0 --depth 1 https://github.com/riscv-software-src/riscv-isa-sim.git "$spike_src"
fi
test "$(git -C "$spike_src" rev-parse HEAD)" = "$revision" || { echo "Wrong Spike revision; expected $revision" >&2; exit 1; }
cd "$spike_build"
"$spike_src/configure" --enable-commitlog --prefix="$spike_build/install"
make -j2
# Compile against the SAME generated config.h/headers as the libraries.
c++ -std=c++17 -O2 -I. -I"$spike_src" -I"$spike_src/riscv" -I"$spike_src/fesvr" -I"$spike_src/softfloat" \
  "$root_dir/tools/spike_adapter.cpp" -L. -Wl,--start-group -lriscv -ldisasm -lsoftfloat -lfesvr -Wl,--end-group \
  -lboost_regex -lboost_system -lpthread -ldl -o spike_adapter
printf 'Built pinned adapter: %s/spike_adapter\n' "$spike_build"
