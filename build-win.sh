#!/bin/bash
# Build the vpen driver and test feeder for win64 SteamVR with llvm-mingw (scoop mingw-mstorsjo-llvm-ucrt).
# The one MSVC/Itanium vtable difference in openvr_driver.h, GetPose's by-value return, is patched into a generated header.
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
CXX=${CXX:-x86_64-w64-mingw32-clang++}
OPENVR=$HERE/third_party/openvr
GEN=$HERE/build/win64
OUT=$HERE/driver/vpen/bin/win64
mkdir -p "$GEN" "$OUT"

sed 's/virtual DriverPose_t GetPose() = 0;/virtual DriverPose_t *GetPose( DriverPose_t *pOut ) = 0;/' \
	"$OPENVR/openvr_driver.h" > "$GEN/openvr_driver_mingw.h"
if [ "$(grep -c 'DriverPose_t \*GetPose( DriverPose_t \*pOut )' "$GEN/openvr_driver_mingw.h")" != 1 ]; then
	echo "build-win: GetPose ABI patch did not apply to openvr_driver.h" >&2
	exit 1
fi

FLAGS=(-O2 -std=c++17 -Wall -static -static-libgcc -static-libstdc++ -I"$GEN" -I"$OPENVR" -I"$HERE/src")
"$CXX" "${FLAGS[@]}" -shared -fno-exceptions -fno-rtti -Wl,--no-undefined \
	"$HERE/src/driver_factory.cpp" -o "$OUT/driver_vpen.dll"
"$CXX" "${FLAGS[@]}" "$HERE/src/vpen_feeder.cpp" -o "$OUT/vpen_feeder.exe"

if llvm-objdump -p "$OUT/driver_vpen.dll" | grep -q 'DLL Name: \(libc++\|libunwind\|libwinpthread\)'; then
	echo "build-win: driver_vpen.dll links a mingw runtime DLL SteamVR will not find" >&2
	exit 1
fi
llvm-objdump -p "$OUT/driver_vpen.dll" | grep -E 'DLL Name|HmdDriverFactory'
