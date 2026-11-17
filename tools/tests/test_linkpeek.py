import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import linkpeek  # noqa: E402


def test_decode_matches_layout():
    assert linkpeek.SIZE == 116
    vals = [0x47544143, 1, 42, 3, 1, 2, 3, 0, 0, -1, 0, 1, 0, 5, 0, 0, 0, 0, 0, 9, 9, 9, 9, 12.5, 4200, 88, 1300, 4, 0.31]
    d = linkpeek.decode(struct.pack(linkpeek.FMT, *vals))
    assert d["magic_ok"] and d["seq"] == 42 and d["resets"] == 3
    assert d["pos"] == (1, 2, 3) and d["fwd"] == (0, 0, -1)
    assert d["rpm"] == 4200 and d["speed_kmh"] == 88 and d["mass"] == 1300 and d["gear"] == 4
    assert abs(d["ride_height"] - 0.31) < 1e-6


def test_raw_lines_show_every_slot():
    buf = struct.pack("<29I", *range(29))
    lines = linkpeek.raw_lines(buf)
    assert len(lines) == 29 and "slot  5  int            5" in lines[5]
