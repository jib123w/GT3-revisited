#!/bin/bash
# Build the PlayStation Vita package, build-vita/gt3r.vpk (docs/dev/VITA.md). Run it in Ubuntu
# (WSL) or any Linux with VitaSDK installed; extra arguments go to CMake's configure step.
set -e
cd "$(dirname "$0")/../.."
: "${VITASDK:=$HOME/vitasdk}"
export VITASDK PATH="$VITASDK/bin:$PATH"
python3 packaging/vita/make_art.py
cmake -S . -B build-vita -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
      -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build-vita -j"$(nproc)"
