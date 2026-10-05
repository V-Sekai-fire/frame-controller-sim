# frame-controller-sim

A VR runtime driver that adds companion pen controllers beside the two a person holds, moved through a shared-memory contract.

## What it is for

The pens are real tracked devices, so the system draws while the person draws, for annotation of drawing and its grading (RFD 2287). Each pen presents as a standard hand-controller input profile with trigger, grip, menu, system, a pose and haptics, at a low hand-selection priority so the person's own controllers keep both hands. The driver polls a shared-memory contract, `src/vpen_shm.h`, every frame for each pen's pose and buttons, and reports a haptic pulse on a pen back through the same contract. A feeder fills it; `src/vpen_feeder.cpp` is a test feeder that moves the pens, presses their buttons and prints the haptic pulses.

## Build

The headset driver is cross-built for the headset's aarch64 system with LLVM, against a sysroot taken from the headset; `docs/sysroot.md` describes it. `build-win.sh` builds the desktop driver and the test feeder for Windows with llvm-mingw.

```sh
SYSROOT=/path/to/sysroot bash build.sh
bash build-win.sh
```

## Licence

MIT; see `LICENSE`. The vendored runtime driver headers are BSD-3-Clause.
