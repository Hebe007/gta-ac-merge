"""Windows: print what the GTA side would read from AC's shared memory.
    python tools\\linkpeek.py            (Ctrl+C to stop)
If everything stays 0 while AC drives, the GTA Link app isn't writing under this name."""
import struct
import sys
import time

NAME = "AcTools.CSP.GtaAcLink.State.v1"
FMT = "<4I3f3f3f3f3f4f4fi"
SIZE = struct.calcsize(FMT)
MAGIC = 0x47544143


def decode(buf: bytes) -> dict:
    v = struct.unpack(FMT, buf[:SIZE])
    return {"magic_ok": v[0] == MAGIC, "version": v[1], "seq": v[2], "resets": v[3],
            "pos": v[4:7], "fwd": v[7:10], "up": v[10:13], "vel": v[13:16],
            "steer_deg": v[23], "rpm": v[24], "speed_kmh": v[25], "mass": v[26], "gear": v[27]}


def main() -> None:
    import mmap
    name = sys.argv[1] if len(sys.argv) > 1 else NAME
    m = mmap.mmap(-1, SIZE, tagname=name, access=mmap.ACCESS_READ)
    print(f"reading {name} ({SIZE} bytes)")
    while True:
        d = decode(m[:SIZE])
        print(f"magic {'OK' if d['magic_ok'] else 'missing'}  seq {d['seq']:>8}  resets {d['resets']}  "
              f"pos {d['pos'][0]:8.1f} {d['pos'][1]:7.1f} {d['pos'][2]:8.1f}  {d['speed_kmh']:5.0f} km/h  gear {d['gear']}")
        time.sleep(0.5)


if __name__ == "__main__":
    main()
