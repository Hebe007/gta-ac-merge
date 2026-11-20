"""Look up what the generator knows about one GTA position, from the cache (no XML reading):
    python -m surfacegen.probe --cache ..\\..\\work\\surfacegen_cache_xml.npz --at X,Y
Lists the collision files covering that spot, whether a filter leaves them out, and the surfaces right there."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import numpy as np

from . import bulk


def _load_any(path: Path) -> bulk.Soup:
    with np.load(path) as z:
        meta = json.loads(bytes(z["meta"]).decode())
        return bulk.Soup(z["verts"], z["tris"], z["mat"], z["fileid"], meta["names"], meta["files"])


def surfaces_at(s: bulk.Soup, x: float, y: float, keys: np.ndarray | None = None) -> list[tuple]:
    """Every surface exactly under/over GTA (x, y): (height, material, file, what AC gets), highest first.
    With keys (the AC surface per triangle), says what the track turns it into."""
    v = s.verts[s.tris].astype(np.float64)
    hit = ((v[:, :, 0].min(1) <= x) & (v[:, :, 0].max(1) >= x) & (v[:, :, 1].min(1) <= y) & (v[:, :, 1].max(1) >= y))
    idx = np.nonzero(hit)[0]
    out = []
    for i in idx:
        (x1, y1, z1), (x2, y2, z2), (x3, y3, z3) = v[i]
        d = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3)
        if abs(d) < 1e-9:
            continue
        l1 = ((y2 - y3) * (x - x3) + (x3 - x2) * (y - y3)) / d
        l2 = ((y3 - y1) * (x - x3) + (x1 - x3) * (y - y3)) / d
        if min(l1, l2, 1 - l1 - l2) < 0:
            continue
        out.append((l1 * z1 + l2 * z2 + (1 - l1 - l2) * z3, s.names[s.mat[i]], s.files[s.file[i]],
                    _ac_result(v[i], None if keys is None else int(keys[i]))))
    return sorted(out, reverse=True)


def _ac_result(t: np.ndarray, key: int | None) -> str:
    """What the generated track makes of one triangle: drivable surface, or wall and why."""
    if key is None:
        return ""
    n = np.cross(t[1] - t[0], t[2] - t[0])
    length = float(np.linalg.norm(n)) or 1.0
    if bulk.KEYS[key] == "NONE":
        return "left out (water)"
    if n[2] < -math.cos(math.radians(bulk.WALL_SLOPE_DEG)) * length:
        return "WALL: faces DOWN (a ceiling/underside, the car falls through it from above)"
    if bulk.KEYS[key] == "WALL":
        return "WALL (steep)"
    return f"drivable {bulk.KEYS[key]}"


def report(s: bulk.Soup, x: float, y: float, radius: float = 8.0, check_filters: bool = True,
           z: float | None = None, keys: np.ndarray | None = None) -> None:
    """Print the collision files with triangles within the radius of GTA (x, y), and the surfaces exactly there.
    keys: AC surface per triangle of s, to show what the track turns each surface into."""
    lo, hi = bulk.file_boxes(s)
    interiors = set(bulk.interior_files(s)) if check_filters else set()
    # every file whose area reaches within the radius (not only files whose box contains the spot)
    cover = np.nonzero((lo[:, 0] - radius <= x) & (hi[:, 0] + radius >= x)
                       & (lo[:, 1] - radius <= y) & (hi[:, 1] + radius >= y))[0]
    sel = np.isin(s.file, cover)
    s = s.subset(sel)
    if keys is not None:
        keys = keys[sel]
    v = s.verts[s.tris]
    r = radius
    near = ((v[:, :, 0].min(1) - r <= x) & (v[:, :, 0].max(1) + r >= x)
            & (v[:, :, 1].min(1) - r <= y) & (v[:, :, 1].max(1) + r >= y))
    # placed objects ("obj:name") span the whole map as one entry: only files with triangles near count
    cover = [f for f in cover if near[s.file == f].any()]
    where = f"at GTA {x:.1f}, {y:.1f}" + (f" (car was at z {z:.1f})" if z is not None else "")
    print(f"{where}: {len(cover)} collision file(s) within {radius:.0f} m")
    hits = surfaces_at(s, x, y, keys)
    if hits:
        print("  surfaces exactly at this spot (height, material, file, what AC gets):")
        for hz, mat, fname, ac in hits[:10]:
            mark = "   <- the car fell from about here" if z is not None and abs(hz - z) < 8 and hz > z else ""
            print(f"      z {hz:7.2f}  {mat:20s} {fname:28s} {ac}{mark}")
    else:
        print("  NO surface exactly at this spot: a hole in the generated track")
    inside_map = bulk.in_bounds(s, bulk.GTA_MAP_BOUNDS, -20, 1500)
    for f in cover:
        mine = near & (s.file == f)
        notes = []
        if f in interiors:
            notes.append("LEFT OUT as interior")
        if check_filters and mine.any() and not inside_map[mine].any():
            notes.append("LEFT OUT by map bounds")
        zs = v[mine][:, :, 2]
        zinfo = f"z {zs.min():.1f}..{zs.max():.1f}" if mine.any() else "nothing within radius"
        if mine.any():
            c = v[mine].mean(1)
            d = np.hypot(c[:, 0] - x, c[:, 1] - y)
            zinfo += f", closest {d.min():.1f} m away"
        print(f"  {s.files[f]:40s} {int((s.file == f).sum()):>8} triangles  near: {int(mine.sum()):>5} ({zinfo})"
              + (f"  <- {', '.join(notes)}" if notes else ""))
        mats = {}
        for m, z in zip(s.mat[mine], v[mine][:, :, 2].mean(1)):
            mats.setdefault(s.names[m], []).append(z)
        for name, zl in sorted(mats.items(), key=lambda kv: -len(kv[1]))[:6]:
            print(f"      {name:28s} {len(zl):>5} near, z {min(zl):.1f}..{max(zl):.1f}")


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(prog="surfacegen.probe")
    ap.add_argument("--cache", type=Path, required=True)
    ap.add_argument("--at", required=True, help="GTA x,y (from the trainer)")
    ap.add_argument("--radius", type=float, default=8.0)
    from .cli import fix_negative_values
    import sys
    a = ap.parse_args(fix_negative_values(sys.argv[1:] if argv is None else argv))
    x, y = (float(v) for v in a.at.split(",")[:2])
    report(_load_any(a.cache), x, y, a.radius)


if __name__ == "__main__":
    main()
