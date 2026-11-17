"""python -m surfacegen.cli --xml-dir work/xml --out <AC>/content/tracks/gta_ls --layout downtown \
       --materials-dat work/materials.dat --bbox x0,y0,x1,y1 --start x,y,z,heading   (all GTA coordinates)"""
from __future__ import annotations

import argparse
import math
import logging
from pathlib import Path

from collections import Counter

from .surfaces import _inside, build_meshes, classify, load_table
from .track import write_track
from .ybn_xml import read_materials_dat, read_ybn_xml


def _floats(n: int):
    def parse(s: str):
        v = tuple(float(x) for x in s.split(","))
        if len(v) != n:
            raise argparse.ArgumentTypeError(f"expected {n} comma-separated numbers")
        return v
    return parse


def pick_files(xml_dir: Path) -> list[Path]:
    """All exported .xml files; when both "hi@<name>" (high-detail collision) and "<name>" exist, keep only hi@."""
    files = {p.name.lower(): p for p in xml_dir.iterdir() if p.name.lower().endswith(".xml")}
    keep = [p for name, p in files.items() if not ("hi@" + name) in files]
    return sorted(keep, key=lambda p: p.name.lower())


def coverage(tris, x: float, y: float):
    """GTA bounding box of all triangles, and the horizontal distance from (x, y) to the nearest triangle centre."""
    pts = [p for t in tris for p in (t.a, t.b, t.c)]
    box = tuple(min(p[i] for p in pts) for i in range(3)) + tuple(max(p[i] for p in pts) for i in range(3))
    nearest = min(math.hypot((t.a[0] + t.b[0] + t.c[0]) / 3 - x, (t.a[1] + t.b[1] + t.c[1]) / 3 - y) for t in tris)
    return box, nearest


def ground_under(tris, x: float, y: float, below: float) -> float | None:
    """Highest non-steep triangle surface at GTA (x, y) that is lower than z=below, or None."""
    best = None
    for t in tris:
        (x1, y1, z1), (x2, y2, z2), (x3, y3, z3) = t.a, t.b, t.c
        d = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3)
        if abs(d) < 1e-9:
            continue
        l1 = ((y2 - y3) * (x - x3) + (x3 - x2) * (y - y3)) / d
        l2 = ((y3 - y1) * (x - x3) + (x1 - x3) * (y - y3)) / d
        l3 = 1 - l1 - l2
        if min(l1, l2, l3) < 0:
            continue
        z = l1 * z1 + l2 * z2 + l3 * z3
        if z <= below and (best is None or z > best):
            best = z
    return best


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(prog="surfacegen")
    ap.add_argument("--xml-dir", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--layout", required=True)
    ap.add_argument("--materials-dat", type=Path)
    ap.add_argument("--materials-toml", type=Path)
    ap.add_argument("--bbox", type=_floats(4))
    ap.add_argument("--start", type=_floats(4), required=True)
    ap.add_argument("--visible", action="store_true", help="debug: draw the surfaces in AC")
    ap.add_argument("--double-sided", action="store_true", help="add every triangle with both windings")
    a = ap.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")

    names = read_materials_dat(a.materials_dat) if a.materials_dat else None
    files = pick_files(a.xml_dir)
    tris = [t for f in files for t in read_ybn_xml(f, names)]
    table = load_table(a.materials_toml)
    meshes = build_meshes(tris, table, a.bbox)
    summary = Counter((t.material, classify(t, table)) for t in tris if not a.bbox or _inside(t, a.bbox))
    logging.info("GTA material -> AC surface (triangles):")
    for (mat, key), n in sorted(summary.items(), key=lambda kv: -kv[1]):
        logging.info("  %8d  %s -> %s", n, mat, key)
    if not meshes:
        raise SystemExit("no collision triangles found (check --xml-dir and --bbox)")
    sx, sy, sz, _ = a.start
    (x0, y0, z0, x1, y1, z1), nearest = coverage(tris, sx, sy)
    logging.info("collision covers GTA x %.0f..%.0f, y %.0f..%.0f, z %.0f..%.0f; nearest triangle %.0f m from start",
                 x0, x1, y0, y1, z0, z1, nearest)
    g = ground_under(tris, sx, sy, below=sz + 2)
    if g is None:
        logging.warning("no collision found under the start point (%.1f, %.1f): the car will fall", sx, sy)
    else:
        logging.info("ground under start: z=%.2f (start z=%.2f, %.2f m above)", g, sz, sz - g)
    write_track(a.out, a.layout, meshes, a.start, visible=a.visible, double_sided=a.double_sided)
    total = sum(len(m.indices) // 3 for m in meshes.values())
    logging.info("wrote %s layout %s: %d files, %d triangles (%s)", a.out, a.layout, len(files), total,
                 ", ".join(f"{k} {len(m.indices) // 3}" for k, m in meshes.items()))


if __name__ == "__main__":
    main()
