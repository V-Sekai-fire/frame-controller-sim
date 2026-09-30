#!/usr/bin/env python3
# xr_cua -- the cheapest computer-use-for-XR, cua's shape for a headset. cua drives
# a desktop through a daemon; here the vpen SteamVR driver IS the daemon (it injects
# the devices, the shared memory is the control channel), this is the client SDK,
# and the compositor still is the screenshot. Semantic-first like cua: set a device
# pose/input directly, never poke pixels. RFD 2287.
import mmap, os, struct, subprocess, sys

SHM = r"C:\ProgramData\vpen\vpen_controllers.bin" if sys.platform == "win32" else "/dev/shm/vpen_controllers"
MAGIC = 0x56504E32
HDR = struct.Struct("<IIQdII")  # magic, version, seq, t_write, count, _pad
PEN = struct.Struct("<9fII")    # px py pz qw qx qy qz trigger grip, buttons haptic
IDENT = (1.0, 0.0, 0.0, 0.0)


class XRSession:
    """A cua-style session over the SteamVR devices the vpen driver injects."""

    def __init__(self, shm=SHM):
        fd = os.open(shm, os.O_RDWR)
        self.mm = mmap.mmap(fd, os.fstat(fd).st_size)
        os.close(fd)
        if struct.unpack_from("<I", self.mm, 0)[0] != MAGIC:
            raise RuntimeError("vpen driver not up (bad magic)")

    def devices(self):  # cua's list_windows: how many devices the daemon exposes
        return struct.unpack_from("<I", self.mm, 24)[0]

    def act(self, poses):  # cua's click/type: set device pose + input, one seqlock frame
        seq = (struct.unpack_from("<Q", self.mm, 8)[0] + 1) | 1
        struct.pack_into("<Q", self.mm, 8, seq)
        struct.pack_into("<I", self.mm, 24, len(poses))
        for i, p in enumerate(poses):
            PEN.pack_into(self.mm, HDR.size + i * PEN.size, *p)
        struct.pack_into("<Q", self.mm, 8, seq + 1)

    def pose(self, x, y, z, quat=IDENT, trigger=0.0, grip=0.0, buttons=0):
        return (x, y, z, quat[0], quat[1], quat[2], quat[3], trigger, grip, buttons, 0)

    def screenshot(self, out):  # cua's screenshot: the compositor still
        subprocess.run(["bash", os.path.expanduser("~/rfd2287/still.sh"), out], check=False)
        return out


if __name__ == "__main__":
    s = XRSession()
    print("xr-cua: %d devices exposed by the vpen daemon" % s.devices())
