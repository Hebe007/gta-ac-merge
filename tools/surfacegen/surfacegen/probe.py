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


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(prog="surfacegen.probe")
    ap.add_argument("--cache", type=Path, required=True)
    ap.add_argument("--at", required=True, help="GTA x,y (from the trainer)")
    ap.add_argument("--radius", type=float, default=8.0)
    a = ap.parse_args(argv)
    x, y = (float(v) for v in a.at.split(",")[:2])
    s = _load_any(a.cache)
    lo, hi = bulk.file_boxes(s)
    interiors = set(bulk.interior_files(s))
    cover = np.nonzero((lo[:, 0] <= x) & (hi[:, 0] >= x) & (lo[:, 1] <= y) & (hi[:, 1] >= y))[0]
    print(f"at GTA {x:.1f}, {y:.1f}: {len(cover)} collision file(s) cover this spot")
    v = s.verts[s.tris]
    r = a.radius  # triangles whose footprint reaches within r of the spot
    near = ((v[:, :, 0].min(1) - r <= x) & (v[:, :, 0].max(1) + r >= x)
            & (v[:, :, 1].min(1) - r <= y) & (v[:, :, 1].max(1) + r >= y))
    inside_map = bulk.in_bounds(s, bulk.GTA_MAP_BOUNDS, -20, 1500)
    for f in cover:
        mine = near & (s.file == f)
        notes = []
        if f in interiors:
            notes.append("LEFT OUT as interior")
        if mine.any() and not inside_map[mine].any():
            notes.append("LEFT OUT by map bounds")
        zs = v[mine][:, :, 2]
        zinfo = f"z {zs.min():.1f}..{zs.max():.1f}" if mine.any() else "nothing within radius"
        print(f"  {s.files[f]:40s} {int((s.file == f).sum()):>8} triangles  near: {int(mine.sum()):>5} ({zinfo})"
              + (f"  <- {', '.join(notes)}" if notes else ""))
        mats = {}
        for m, z in zip(s.mat[mine], v[mine][:, :, 2].mean(1)):
            mats.setdefault(s.names[m], []).append(z)
        for name, zl in sorted(mats.items(), key=lambda kv: -len(kv[1]))[:6]:
            print(f"      {name:28s} {len(zl):>5} near, z {min(zl):.1f}..{max(zl):.1f}")


if __name__ == "__main__":
    main()
