#!/usr/bin/env python3
# vpen_feeder -- test-motion producer for the vpen SteamVR driver. Animates the
# companion pens over the seqlock shm so their SketchTools draw visible strokes
# without the real feeder. One writer. --selftest writes a known pose and reads it
# back, plus a negative control (a wrong expectation must fail). RFD 2287.
import mmap, os, struct, sys, time, math

SHM = "/dev/shm/vpen_controllers"
MAGIC = 0x56504E32
HDR = struct.Struct("<IIQdII")   # magic, version, seq, t_write, count, _pad = 32
PEN = struct.Struct("<9fII")     # px py pz qw qx qy qz trigger grip, buttons haptic = 44
HDR_SIZE = HDR.size
PEN_SIZE = PEN.size


def open_shm():
    fd = os.open(SHM, os.O_RDWR)
    mm = mmap.mmap(fd, os.fstat(fd).st_size)
    os.close(fd)
    magic = struct.unpack_from("<I", mm, 0)[0]
    if magic != MAGIC:
        sys.exit("bad magic 0x%08x (vpen driver not up?)" % magic)
    return mm


def write_frame(mm, pens, count, t):
    seq = struct.unpack_from("<Q", mm, 8)[0]
    seq = (seq + 1) | 1                     # odd: writing
    struct.pack_into("<Q", mm, 8, seq)
    struct.pack_into("<d", mm, 16, t)       # t_write
    struct.pack_into("<I", mm, 24, count)   # count
    for i, p in enumerate(pens):
        PEN.pack_into(mm, HDR_SIZE + i * PEN_SIZE, *p)
    struct.pack_into("<Q", mm, 8, seq + 1)  # even: stable


def read_pen(mm, i):
    return PEN.unpack_from(mm, HDR_SIZE + i * PEN_SIZE)


def quat_axis_angle(ax, ay, az, angle):
    n = math.sqrt(ax * ax + ay * ay + az * az) or 1.0
    ax, ay, az = ax / n, ay / n, az / n
    h = angle * 0.5
    s = math.sin(h)
    return (math.cos(h), ax * s, ay * s, az * s)  # qw, qx, qy, qz


def selftest():
    mm = open_shm()
    pose = (0.11, 1.22, 0.33, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0, 0)
    write_frame(mm, [pose], 1, 1.0)
    got = read_pen(mm, 0)
    ok = all(abs(got[i] - pose[i]) < 1e-4 for i in range(3))
    print("selftest read-back pos=%s -> %s" % (got[:3], "OK" if ok else "FAIL"))
    bad_matches = abs(got[0] - 0.99) < 1e-4          # a wrong value must NOT match
    print("negative control (wrong value matches) -> %s" % ("FAIL" if bad_matches else "OK"))
    sys.exit(0 if ok and not bad_matches else 1)


def main():
    if "--selftest" in sys.argv:
        selftest()
    def argval(flag, default):
        return float(sys.argv[sys.argv.index(flag) + 1]) if flag in sys.argv else default
    count = int(argval("--count", 15))
    dur = argval("--seconds", 60.0)
    # OpenVR->Godot vertical offset measured on the Frame: a feeder y lands ~1.68 m
    # higher in Godot world. Aim the circles at --height in Godot (in the flat
    # camera's view near the body) by subtracting it. --depth is the Godot z.
    y_off = 1.68
    height = argval("--height", 1.1)
    depth = argval("--depth", 1.2)
    cy = height - y_off
    cz = depth - 1.2
    mm = open_shm()
    # Lay the devices out in a grid facing the flat camera, each drawing a small
    # circle. SteamVR world: y up, -z forward.
    cols = 5
    rows = (count + cols - 1) // cols
    row_gap = 0.30
    centers = []
    for i in range(count):
        col, row = i % cols, i // cols
        cx = -0.6 + 1.2 * (col / max(cols - 1, 1))
        cyy = cy + (rows - 1) * row_gap / 2.0 - row * row_gap
        centers.append((cx, cyy, cz))
    radius = 0.07
    t0 = time.monotonic()
    hz = 120.0
    n = 0
    while True:
        t = time.monotonic() - t0
        if t > dur:
            break
        pens = []
        for i in range(count):
            cx, cy, cz = centers[i % len(centers)]
            ang = 2 * math.pi * (0.5 * t) + i * 0.5      # 0.5 rev/s, phase per pen
            # Tumble about a tilted axis at twice the orbit rate so the render
            # model's orientation is plainly changing, not just its position.
            qw, qx, qy, qz = quat_axis_angle(1.0, 0.35, 0.0, ang * 2.0)
            pens.append((cx + radius * math.cos(ang), cy + radius * math.sin(ang), cz,
                         qw, qx, qy, qz, 1.0, 0.0, 1, 0))
        write_frame(mm, pens, count, time.monotonic())
        n += 1
        time.sleep(1.0 / hz)
    print("feeder done: %d frames over %.1fs, count=%d" % (n, dur, count))


if __name__ == "__main__":
    main()
