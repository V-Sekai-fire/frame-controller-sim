# Assembling the aarch64 sysroot from the Frame

`build.sh` cross-compiles on a Windows host with scoop LLVM clang against a
sysroot taken from the Steam Frame (SteamOS aarch64, GCC 15.1.1 runtime). The
Frame has the dev files but no compiler, so the sysroot is pulled to the desk.

Pull these from the Frame into `sysroot/` (mirroring their absolute paths under
`usr/`), over SSH with `rsync -a`:

- `/usr/include` (the C and C++ headers; the C++ headers are under
  `c++/15.1.1`, matching the GCC runtime, not `c++/16`).
- `/usr/lib/gcc/aarch64-unknown-linux-gnu/15.1.1` (crt objects, `libgcc`).
- from `/usr/lib`: `Scrt1.o crti.o crtn.o`, `libc.so` (a GNU ld script) and
  `libc.so.6`, `libm.so*`, `libstdc++.so*`, `libgcc_s.so*`, the loader
  `ld-linux-aarch64.so.1`.

Then dereference the `.so` symlinks: `libstdc++.so`, `libm.so`, `libgcc_s.so`
and the like are Linux symlinks Windows lld cannot open, so replace each with a
real copy of its target (the sonames it records stay correct, e.g.
`libstdc++.so.6`, all present on the Frame at load time):

    cd sysroot/usr/lib
    for l in $(find . -maxdepth 1 -type l -name "*.so*"); do
      t=$(readlink "$l"); [ -f "$t" ] && cp -f "$t" "$l"
    done

`build.sh` then produces `driver/vpen/bin/linuxarm64/driver_vpen.so` needing
only `libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1`, `libc.so.6`.
