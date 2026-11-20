"""Look up what the generator knows about one GTA position, from the cache (no XML reading):
    python -m surfacegen.probe --cache ..\\..\\work\\surfacegen_cache_xml.npz --at X,Y
Lists the collision files covering that spot, whether a filter leaves them out, and the surfaces right there."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

from . import bulk


def _load_any(path: Path) -> bulk.Soup:
    with np.load(path) as z:
        meta = json.loads(bytes(z["meta"]).decode())
        return bulk.Soup(z["verts"], z["tris"], z["mat"], z["fileid"], meta["names"], meta["files"])


def surfaces_at(s: bulk.Soup, x: float, y: float) -> list[tuple[float, str, str]]:
    """Every surface exactly under/over GTA (x, y): (height, material, file), highest first."""
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
        out.append((l1 * z1 + l2 * z2 + (1 - l1 - l2) * z3, s.names[s.mat[i]], s.files[s.file[i]]))
    return sorted(out, reverse=True)


def report(s: bulk.Soup, x: float, y: float, radius: float = 8.0, check_filters: bool = True,
           z: float | None = None) -> None:
    """Print the collision files covering GTA (x, y) and the surfaces there."""
    lo, hi = bulk.file_boxes(s)
    interiors = set(bulk.interior_files(s)) if check_filters else set()
    # every file whose area reaches within the radius (not only files whose box contains the spot)
    cover = np.nonzero((lo[:, 0] - radius <= x) & (hi[:, 0] + radius >= x)
                       & (lo[:, 1] - radius <= y) & (hi[:, 1] + radius >= y))[0]
    where = f"at GTA {x:.1f}, {y:.1f}" + (f" (car was at z {z:.1f})" if z is not None else "")
    print(f"{where}: {len(cover)} collision file(s) within {radius:.0f} m")
    s = s.subset(np.isin(s.file, cover))
    hits = surfaces_at(s, x, y)
    if hits:
        print("  surfaces exactly at this spot (height, material, file):")
        for hz, mat, fname in hits[:10]:
            mark = "   <- the car fell from about here" if z is not None and abs(hz - z) < 8 and hz > z else ""
            print(f"      z {hz:7.2f}  {mat:24s} {fname}{mark}")
    else:
        print("  NO surface exactly at this spot: a hole in the generated track")
    v = s.verts[s.tris]
    r = radius
    near = ((v[:, :, 0].min(1) - r <= x) & (v[:, :, 0].max(1) + r >= x)
            & (v[:, :, 1].min(1) - r <= y) & (v[:, :, 1].max(1) + r >= y))
    inside_map = bulk.in_bounds(s, bulk.GTA_MAP_BOUNDS, -20, 1500)
    for f in cover:
        mine = near & (s.file == f)
        if not mine.any() and not ((lo[f, 0] <= x <= hi[f, 0]) and (lo[f, 1] <= y <= hi[f, 1])):
            continue  # nothing of it within the radius, and its box doesn't contain the spot
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
