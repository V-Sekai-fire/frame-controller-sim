# frame-controller-sim

A VR runtime driver that adds companion pen controllers beside the two a person holds, moved by a feeder through shared memory.

## What it is for

The pens are real tracked devices, so the system draws while the person draws, for annotation of drawing and its grading (RFD 2287). Each pen presents as a standard hand-controller input profile with trigger, grip, menu, system, a pose and haptics, at a low hand-selection priority so the person's own controllers keep both hands. A feeder writes each pen's pose and buttons into shared memory that the driver polls every frame, and a haptic pulse on a pen is reported back to the feeder the same way.

## Build

The driver is cross-built for the headset's aarch64 system with LLVM, against a sysroot taken from the headset; `docs/sysroot.md` describes it.

```sh
SYSROOT=/path/to/sysroot bash build.sh
```

## Licence

MIT; see `LICENSE`. The vendored runtime driver headers are BSD-3-Clause.
