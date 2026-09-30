SteamVR virtual-controller driver plus a feeder: companion pens on the Steam Frame that draw alongside a person, for annotation and grading.

# frame-controller-sim

Companion pens that draw beside the two real Steam Frame controllers, so the
system draws while the person draws. Part of RFD 2287; the goal is annotation of
drawing and annotation grading. The pens are real SteamVR devices, so more pens
draw more strokes at once within the limit of the available tracker roles.

## Real pens only

The companions are **real SteamVR tracked devices**, never faked at the app
layer. The pen reads them through OpenXR like any controller, and any other
OpenXR app sees them too, with real haptics.

OpenXR exposes only two hand roles (the person's controllers hold them), and
Godot surfaces vive trackers **by role, not by serial**. The person's own VR
trackers own the body-part htcx roles, so the roles left for a pen are the
non-conflicting *object* roles: `handheld_object` (always), and `camera` /
`keyboard` when the person's trackers do not use them. So the `vpen` driver
registers up to three real companion pens on those roles.

A feeder moves them by writing pose + trigger into shared memory
(`src/vpen_shm.h`) that the driver polls each frame.

## Status

- Driver builds to an aarch64 SteamOS `.so` and **SteamVR enumerates the pens**
  (`Loaded server driver vpen`, `vpen_*` devices added). Verified 2026-09-29.
- Surfacing the native pen on `handheld_object` (action-map binding + role
  autobind), the feeder, the UDP channel, and the pen-scene companion nodes are
  in progress.

## Build

The driver is aarch64 SteamOS native, cross-built on Windows with LLVM (the
Frame has no compiler), against a sysroot assembled from the Frame; see
`docs/sysroot.md`. Then:

    SYSROOT=/path/to/sysroot bash build.sh          # -> driver/vpen/bin/linuxarm64/driver_vpen.so

`third_party/openvr` vendors the OpenVR driver headers (BSD-3-Clause).

## Layout

- `src/driver_factory.cpp` — the driver: an `IServerTrackedDeviceProvider`
  adding the native pen(s), seqlock-reading the shared memory.
- `src/vpen_shm.h` — the producer/consumer shared-memory contract.
- `driver/vpen/` — the SteamVR driver package (manifest, input profile,
  settings); `bin/linuxarm64/driver_vpen.so` is the build output.
- `feeder/` — the pose feeder (added next; frame-eye-osc shape: Slang -> C++,
  writes the shm the driver polls).
- `build.sh`, `docs/sysroot.md` — the cross-build.
