"""python -m surfacegen.cli --xml-dir work/xml --out <AC>/content/tracks/gta_ls --layout downtown \
       --materials-dat work/materials.dat --bbox x0,y0,x1,y1 --start x,y,z,heading   (all GTA coordinates)"""
from __future__ import annotations

import argparse
import logging
from pathlib import Path

from .surfaces import build_meshes, load_table
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


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(prog="surfacegen")
    ap.add_argument("--xml-dir", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--layout", required=True)
    ap.add_argument("--materials-dat", type=Path)
    ap.add_argument("--materials-toml", type=Path)
    ap.add_argument("--bbox", type=_floats(4))
    ap.add_argument("--start", type=_floats(4), required=True)
    a = ap.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")

    names = read_materials_dat(a.materials_dat) if a.materials_dat else None
    files = pick_files(a.xml_dir)
    tris = [t for f in files for t in read_ybn_xml(f, names)]
    meshes = build_meshes(tris, load_table(a.materials_toml), a.bbox)
    if not meshes:
        raise SystemExit("no collision triangles found (check --xml-dir and --bbox)")
    write_track(a.out, a.layout, meshes, a.start)
    total = sum(len(m.indices) // 3 for m in meshes.values())
    logging.info("wrote %s layout %s: %d files, %d triangles (%s)", a.out, a.layout, len(files), total,
                 ", ".join(f"{k} {len(m.indices) // 3}" for k, m in meshes.items()))


if __name__ == "__main__":
    main()
