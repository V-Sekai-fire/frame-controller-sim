SteamVR driver plus a feeder: two Quest 3 Touch Plus controllers on the Steam Frame, beside the two the person holds.

# frame-controller-sim

Companion pens that draw beside the two real Steam Frame controllers, so the
system draws while the person draws. Part of RFD 2287; the goal is annotation of
drawing and annotation grading. The pens are real SteamVR devices, so more pens
draw more strokes at once within the limit of the available tracker roles.

## Two Quest 3 Touch Plus controllers

The person holds the Frame's two real controllers; the `vpen` driver adds two
more SteamVR controllers beside them, each one presenting as a Meta Quest 3
Touch Plus controller (`controller_type = "oculus_touch"`). An OpenXR app that
binds the Meta Quest 3 Touch profile sees them with trigger, grip, menu,
system, a pose and haptics. Slot 0 is the left hand, slot 1 the right. Their
hand-selection priority is -1000, so the person's own controllers keep the two
hands.

A feeder moves the extras by writing pose and buttons into shared memory
(`src/vpen_shm.h`) that the driver polls each frame. An app's haptic pulse on
an extra bumps its slot's `haptic_seq` for the feeder.

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
  adding the four controllers, seqlock-reading the shared memory.
- `src/vpen_shm.h` — the producer/consumer shared-memory contract.
- `driver/vpen/` — the SteamVR driver package (manifest, input profile,
  settings); `bin/linuxarm64/driver_vpen.so` is the build output.
- `feeder/` — the pose feeder (added next; frame-eye-osc shape: Slang -> C++,
  writes the shm the driver polls).
- `build.sh`, `docs/sysroot.md` — the cross-build.
