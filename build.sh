#!/bin/bash
# Cross-build the vpen SteamVR driver for aarch64 SteamOS on a Windows host with
# LLVM (the Frame has no compiler), against a sysroot assembled from the Frame
# (see docs/sysroot.md). -Wl,--no-undefined keeps the link honest; std resolves
# against the sysroot's libstdc++ (whose .so symlinks docs/sysroot.md says to
# dereference for Windows lld). Runs on a Linux host too (cygpath is skipped).
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SYSROOT=${SYSROOT:-$HERE/sysroot}
GCC_VER=${GCC_VER:-15.1.1}
GCC=$SYSROOT/usr/lib/gcc/aarch64-unknown-linux-gnu/$GCC_VER
CXX=${CXX:-clang++}
OPENVR=$HERE/third_party/openvr
SRCDIR=$HERE/src
SRC=${1:-$SRCDIR/driver_factory.cpp}
OUT=${2:-$HERE/driver/vpen/bin/linuxarm64/driver_vpen.so}
mkdir -p "$(dirname "$OUT")"

# scoop clang on Windows wants Windows paths; pass through on a Linux host.
w() { if command -v cygpath >/dev/null 2>&1; then cygpath -w "$1"; else echo "$1"; fi; }

"$CXX" --target=aarch64-unknown-linux-gnu \
  --sysroot="$(w "$SYSROOT")" --gcc-toolchain="$(w "$SYSROOT/usr")" \
  -B"$(w "$GCC")" -L"$(w "$SYSROOT/usr/lib")" -L"$(w "$GCC")" \
  -isystem "$(w "$SYSROOT/usr/include/c++/$GCC_VER")" \
  -isystem "$(w "$SYSROOT/usr/include/c++/$GCC_VER/aarch64-unknown-linux-gnu")" \
  -I"$(w "$OPENVR")" -I"$(w "$SRCDIR")" \
  -shared -fPIC -fvisibility=hidden -O2 -std=c++17 -fno-exceptions -fno-rtti \
  -fuse-ld=lld -Wl,--no-undefined \
  "$(w "$SRC")" -o "$(w "$OUT")"

file "$OUT"
readelf -d "$OUT" | grep NEEDED
