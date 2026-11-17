"""Windows: print what the GTA side would read from AC's shared memory.
    python tools\\linkpeek.py            (Ctrl+C to stop)
    python tools\\linkpeek.py --raw      (every 4-byte slot as integer and float, once)
If everything stays 0 while AC drives, the GTA Link app isn't writing under this name."""
import struct
import sys
import time

NAME = "AcTools.CSP.GtaAcLink.State.v1"
FMT = "<4I3f3f3f3f3f4f4fiffff"
SIZE = struct.calcsize(FMT)
MAGIC = 0x47544143


def decode(buf: bytes) -> dict:
    v = struct.unpack(FMT, buf[:SIZE])
    return {"magic_ok": v[0] == MAGIC, "version": v[1], "seq": v[2], "resets": v[3],
            "pos": v[4:7], "fwd": v[7:10], "up": v[10:13], "vel": v[13:16],
            "steer_deg": v[23], "rpm": v[24], "speed_kmh": v[25], "mass": v[26], "gear": v[27], "ride_height": v[28], "steer_lock": v[29], "gas": v[30], "brake": v[31]}


def raw_lines(buf: bytes) -> list[str]:
    n = SIZE // 4
    ints = struct.unpack(f"<{n}I", buf[:SIZE])
    floats = struct.unpack(f"<{n}f", buf[:SIZE])
    return [f"slot {i:2d}  int {ints[i]:>12}  float {floats[i]:14.4f}" for i in range(n)]


def main() -> None:
    import mmap
    args = [a for a in sys.argv[1:] if a != "--raw"]
    name = args[0] if args else NAME
    m = mmap.mmap(-1, SIZE, tagname=name, access=mmap.ACCESS_READ)
    if "--raw" in sys.argv:
        print("\n".join(raw_lines(m[:SIZE])))
        return
    print(f"reading {name} ({SIZE} bytes)")
    while True:
        d = decode(m[:SIZE])
        print(f"magic {'OK' if d['magic_ok'] else 'missing'}  seq {d['seq']:>8}  resets {d['resets']}  "
              f"pos {d['pos'][0]:8.1f} {d['pos'][1]:7.1f} {d['pos'][2]:8.1f}  {d['speed_kmh']:5.0f} km/h  gear {d['gear']}  "
              f"steer {d['steer_deg']:.0f}/{d['steer_lock']:.0f}  gas {d['gas']:.2f} brake {d['brake']:.2f}  ride {d['ride_height']:.2f} m  v{d['version']}")
        time.sleep(0.5)


if __name__ == "__main__":
    main()
