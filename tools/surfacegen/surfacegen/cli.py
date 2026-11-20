"""python -m surfacegen.cli --xml-dir work/xml --out <AC>/content/tracks/gta_ls --layout downtown \
       --materials-dat work/materials.dat --bbox x0,y0,x1,y1 --start x,y,z,heading   (all GTA coordinates)"""
from __future__ import annotations

import argparse
import math
import sys
import logging
from pathlib import Path

from collections import Counter

import numpy as np

from .surfaces import _inside, build_meshes, classify, load_table, weld
from .track import write_track
from .ybn_xml import read_materials_dat, read_ybn_xml


def _floats(n: int):
    def parse(s: str):
        v = tuple(float(x) for x in s.split(","))
        if len(v) != n:
            raise argparse.ArgumentTypeError(f"expected {n} comma-separated numbers")
        return v
    return parse


NEGATIVE_OK = ("--at", "--start", "--bbox", "--probe")


def fix_negative_values(argv: list[str]) -> list[str]:
    """argparse reads '--at -2595,3041' as two options; join such values as '--at=-2595,3041'."""
    out, i = [], 0
    while i < len(argv):
        if argv[i] in NEGATIVE_OK and i + 1 < len(argv) and argv[i + 1].startswith("-"):
            out.append(f"{argv[i]}={argv[i + 1]}")
            i += 2
        else:
            out.append(argv[i])
            i += 1
    return out


def source_rank(folder: str) -> tuple:
    """Which export folder wins when the same file name appears in several: base game < other DLC packs <
    patchdayNng (by number) < update (update.rpf) < mods (e.g. NaturalVision)."""
    n = folder.lower()
    if n == "base":
        return (0, 0, n)
    if n.startswith("patchday"):
        digits = "".join(c for c in n if c.isdigit())
        return (2, int(digits or 0), n)
    if n == "update":
        return (3, 0, n)
    if n == "mods":
        return (4, 0, n)
    return (1, 0, n)


def pick_sources(xml_dir: Path, prefer: str = "normal") -> list[Path]:
    """All exported .xml files under xml_dir (directly, or in one sub-folder per source - see source_rank).
    The highest-ranked copy of each file name wins. Most blocks have a normal file plus "hi@" and "ma@" variants:
      normal - use the normal file; hi@ only where no normal one exists; ma@ never (the default)
      hi     - prefer hi@ over normal;  ma - prefer ma@ over normal;  all - every variant (experiments)"""
    best: dict[str, tuple[tuple, Path]] = {}
    folders = [(source_rank("base"), xml_dir)] + [(source_rank(d.name), d) for d in xml_dir.iterdir() if d.is_dir()]
    for rank, folder in folders:
        for p in folder.iterdir():
            name = p.name.lower()
            if name.startswith("ma@") and prefer not in ("ma", "all"):
                continue  # another special-purpose collision layer; cars drive on the normal files
            if p.is_file() and name.endswith(".xml") and (name not in best or rank > best[name][0]):
                best[name] = (rank, p)
    if prefer == "all":
        return sorted((p for _, p in best.values()), key=lambda p: p.name.lower())
    keep = []
    for name, (_, p) in best.items():
        base = name[3:] if name[:3] in ("hi@", "ma@") else name
        variant = name[:3] if name[:3] in ("hi@", "ma@") else ""
        if prefer == "normal":
            wanted = "" if base in best else ("hi@" if "hi@" + base in best else variant)
        else:
            want = prefer + "@"
            wanted = want if want + base in best else ("" if base in best else variant)
        if variant == wanted:
            keep.append(p)
    return sorted(keep, key=lambda p: p.name.lower())


def pick_files(xml_dir: Path, prefer: str = "normal") -> list[Path]:
    return pick_sources(xml_dir, prefer)


def files_covering(per_file: dict, x: float, y: float) -> list[tuple[str, int]]:
    """Files whose horizontal bounding box contains (x, y), with their triangle counts."""
    out = []
    for name, tris in per_file.items():
        if not tris:
            continue
        xs = [p[0] for t in tris for p in (t.a, t.b, t.c)]
        ys = [p[1] for t in tris for p in (t.a, t.b, t.c)]
        if min(xs) <= x <= max(xs) and min(ys) <= y <= max(ys):
            out.append((name, len(tris)))
    return out


def probe(per_file: dict, x: float, y: float, count: int = 8) -> list[str]:
    """The triangles whose centres are nearest (x, y), one readable line each."""
    near = []
    for name, tris in per_file.items():
        for t in tris:
            cx = (t.a[0] + t.b[0] + t.c[0]) / 3
            cy = (t.a[1] + t.b[1] + t.c[1]) / 3
            near.append((math.hypot(cx - x, cy - y), name, t))
    near.sort(key=lambda n: n[0])
    fmt = lambda p: "(%.2f, %.2f, %.2f)" % p
    return [f"{d:6.1f} m  {name}  {t.material}  {fmt(t.a)} {fmt(t.b)} {fmt(t.c)}" for d, name, t in near[:count]]


def looks_like_interior(tris) -> bool:
    """Interior (MLO) collision is stored around 0,0,0 in the interior's own coordinates; the game places it
    elsewhere. Real map blocks near the world origin sit at street level (z >= ~20)."""
    if not tris:
        return False
    xs = [p[0] for t in tris for p in (t.a, t.b, t.c)]
    ys = [p[1] for t in tris for p in (t.a, t.b, t.c)]
    zs = [p[2] for t in tris for p in (t.a, t.b, t.c)]
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    size = max(max(xs) - min(xs), max(ys) - min(ys))
    return math.hypot(cx, cy) < 300 and size < 300 and min(zs) < 15 and max(zs) < 40


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
    ap.add_argument("--prefer", choices=("normal", "hi", "ma", "all"), default="normal",
                    help="which collision to use when both a normal and an hi@ file exist")
    ap.add_argument("--weld", type=float, default=0.05,
                    help="snap vertices closer than this many metres together to close cracks (0 = off)")
    ap.add_argument("--wall-surface", choices=("WALL", "ROAD"), default="WALL",
                    help="surface key for walls (ROAD = test whether AC collides with them under the road surface)")
    ap.add_argument("--step-height", type=float, default=0.35,
                    help="steep faces lower than this many metres (kerbs) stay drivable instead of becoming walls")
    ap.add_argument("--kerb-ramp", type=float, default=2.5,
                    help="ramp in front of kerbs/steps, as a multiple of their height (0 = sharp steps)")
    ap.add_argument("--flip-walls", action="store_true", help="turn all wall faces around (if walls only stop you from inside)")
    ap.add_argument("--keep-interiors", action="store_true", help="don't leave out files that look like interiors")
    ap.add_argument("--visible", action="store_true", help="debug: draw the surfaces in AC")
    ap.add_argument("--double-sided", action="store_true", help="add every triangle with both windings")
    ap.add_argument("--ymap-dir", type=Path,
                    help="CodeWalker XML of .ymap files (one sub-folder per source): places interiors such as tunnels")
    ap.add_argument("--interior-rotation", choices=("as-is", "inverse"), default="as-is",
                    help="experiment: rotate placed interiors the other way (if tunnels come out twisted)")
    ap.add_argument("--map-bounds", choices=("gta", "none"), default="gta",
                    help="gta = keep Los Santos + Blaine County only (drops North Yankton and far-away DLC sets)")
    ap.add_argument("--zmin", type=float, default=-20.0, help="drop collision below this height (seabed)")
    ap.add_argument("--zmax", type=float, default=1500.0, help="drop collision above this height (sky sets)")
    ap.add_argument("--probe", action="append", default=[], metavar="X,Y",
                    help="after building, show what the finished track has at this GTA spot (repeatable)")
    ap.add_argument("--probe-file", type=Path,
                    help="probe every spot in this file (gta_link_holes.txt from the GTA folder: x,y,z per line)")
    ap.add_argument("--engine", choices=("bulk", "python"), default="bulk",
                    help="bulk = NumPy, for the full map (needs: pip install numpy scipy); python = original")
    ap.add_argument("--cache", type=Path, help="cache file (default: next to --xml-dir)")
    ap.add_argument("--no-cache", action="store_true", help="always read the XML, don't write a cache")
    a = ap.parse_args(fix_negative_values(sys.argv[1:] if argv is None else argv))
    # stdout, not stderr: PowerShell turns captured stderr lines into red "NativeCommandError" noise
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s", stream=sys.stdout)
    if a.engine == "bulk":
        _run_bulk(a)
    else:
        _run_python(a)


def _run_bulk(a) -> None:
    from . import bulk

    names = read_materials_dat(a.materials_dat) if a.materials_dat else None
    files = pick_sources(a.xml_dir, a.prefer)
    cache = None if a.no_cache else (a.cache or a.xml_dir.parent / f"surfacegen_cache_{a.xml_dir.name}.npz")
    sig = bulk.signature(files, a.materials_dat)
    soup = bulk.load_cache(cache, sig) if cache and cache.exists() else None
    if soup is not None:
        logging.info("using cache %s (%d triangles)", cache, len(soup.tris))
    else:
        logging.info("reading %d collision files", len(files))
        soup = bulk.read_files(files, names)
        if cache:
            bulk.save_cache(cache, soup, sig)
            logging.info("saved cache %s", cache)

    if a.ymap_dir:
        from .ymap_xml import read_mlo_instances
        ymaps = pick_sources(a.ymap_dir)
        instances = [i for y in ymaps for i in read_mlo_instances(y)]
        if a.interior_rotation == "inverse":
            instances = [(n, p, (-q[0], -q[1], -q[2], q[3])) for n, p, q in instances]
        soup, placed, missing = bulk.place_interiors(soup, instances)
        logging.info("placed %d interiors from %d map files (%d interior types have no collision file)",
                     placed, len(ymaps), len(missing))
    if not a.keep_interiors:
        bad = bulk.interior_files(soup)
        placed_names = {f.split("@")[0] for f in soup.files if "@" in f}
        unplaced = [soup.files[i] for i in bad
                    if soup.files[i].lower().removesuffix(".xml").removesuffix(".ybn") not in placed_names]
        if unplaced:
            out = a.xml_dir.parent / "unplaced_interiors.txt"
            out.write_text("\n".join(sorted(unplaced)) + "\n")
            logging.info("%d interiors have collision but no placement in the map files (listed in %s): %s%s",
                         len(unplaced), out, ", ".join(sorted(unplaced)[:12]), " ..." if len(unplaced) > 12 else "")
        if bad:
            soup = soup.subset(~np.isin(soup.file, bad))
            shown = [soup.files[i] for i in bad[:8]]
            logging.info("left out %d interior files (stored in local coordinates): %s%s", len(bad), ", ".join(shown),
                         " ..." if len(bad) > 8 else "")
    if a.map_bounds == "gta":
        before = len(soup.tris)
        soup = soup.subset(bulk.in_bounds(soup, bulk.GTA_MAP_BOUNDS, a.zmin, a.zmax))
        logging.info("map bounds: kept %d of %d triangles (dropped North Yankton, far-away sets, seabed, sky)",
                     len(soup.tris), before)
    counts = np.bincount(soup.file, minlength=len(soup.files))
    small = [f"{soup.files[i]} ({counts[i]})" for i in np.nonzero((counts > 0) & (counts < 50))[0]]
    if small:
        logging.warning("files with almost no collision (possible holes): %s", ", ".join(small[:20]))
    logging.info("%d triangles from %d files", len(soup.tris), int((counts > 0).sum()))

    if a.weld > 0:
        logging.info("welding seams...")
        soup, merged, dropped = bulk.weld(soup, a.weld)
        logging.info("welded %d vertices within %.0f cm; dropped %d collapsed triangles", merged, a.weld * 100, dropped)
    soup, dups = bulk.dedupe(soup)
    logging.info("removed %d duplicate triangles (copies of the same surface, e.g. DLC replacement blocks)", dups)
    logging.info("densest 2 km areas (GTA x, y: triangles): %s",
                 ", ".join(f"{x},{y}: {n}" for x, y, n in bulk.busiest_cells(soup)))

    logging.info("sorting into surfaces...")
    table = load_table(a.materials_toml)
    keys = bulk.classify(soup, table, a.step_height)
    code = soup.mat.astype(np.int64) * len(bulk.KEYS) + keys
    u, c = np.unique(code, return_counts=True)
    logging.info("GTA material -> AC surface (triangles):")
    for i in np.argsort(-c):
        logging.info("  %8d  %s -> %s", c[i], soup.names[u[i] // len(bulk.KEYS)], bulk.KEYS[u[i] % len(bulk.KEYS)])

    soup, keys, ramps = bulk.ramp_kerbs(soup, keys, a.step_height, a.kerb_ramp)
    if ramps:
        logging.info("added ramps in front of %d kerb and step faces", ramps)
    sx, sy, sz, _ = a.start
    (x0, y0, z0, x1, y1, z1), nearest = bulk.coverage(soup, sx, sy)
    logging.info("collision covers GTA x %.0f..%.0f, y %.0f..%.0f, z %.0f..%.0f; nearest triangle %.0f m from start",
                 x0, x1, y0, y1, z0, z1, nearest)
    g = bulk.ground_under(soup, sx, sy, below=sz + 2)
    if g is None:
        logging.warning("no collision found under the start point (%.1f, %.1f): the car will fall", sx, sy)
    else:
        logging.info("ground under start: z=%.2f (start z=%.2f, %.2f m above)", g, sz, sz - g)

    if a.probe_file:
        a.probe += [ln.strip() for ln in a.probe_file.read_text().splitlines() if ln.strip()]
    if a.probe:
        from .probe import report
        for spot in a.probe:
            parts = [float(v) for v in spot.split(",")]
            report(soup.subset(keys != bulk.KEYS.index("NONE")), parts[0], parts[1], check_filters=False,
                   z=parts[2] if len(parts) > 2 else None)
    meshes = bulk.build_meshes(soup, keys, a.bbox)
    if len(meshes) == 1:
        raise SystemExit("no collision triangles found (check --xml-dir and --bbox)")
    if a.wall_surface == "ROAD" and "WALL" in meshes:
        meshes["ROAD_WALL"] = meshes.pop("WALL")
    logging.info("writing the track file...")
    write_track(a.out, a.layout, meshes, a.start, visible=a.visible, double_sided=a.double_sided,
                flip_walls=a.flip_walls, writer=bulk.write_kn5)
    sizes = {k: len(t) for k, t in meshes.items() if k != "__verts__"}
    kn5 = a.out / f"{a.out.name}_{a.layout}.kn5"
    logging.info("wrote %s layout %s: %d triangles (%s), %.0f MB", a.out, a.layout, sum(sizes.values()),
                 ", ".join(f"{k} {n}" for k, n in sizes.items()), kn5.stat().st_size / 1e6)


def _run_python(a) -> None:
    names = read_materials_dat(a.materials_dat) if a.materials_dat else None
    files = pick_sources(a.xml_dir, a.prefer)
    logging.info("reading %d collision files", len(files))
    per_file = {}
    for i, f in enumerate(files, 1):
        per_file[f.name] = read_ybn_xml(f, names)
        if i % 200 == 0:
            logging.info("  read %d / %d files", i, len(files))
    interiors = [] if a.keep_interiors else [n for n, ts in per_file.items() if looks_like_interior(ts)]
    for n in interiors:
        del per_file[n]
    if interiors:
        logging.info("left out %d interior files (stored in local coordinates): %s%s", len(interiors),
                     ", ".join(interiors[:8]), " ..." if len(interiors) > 8 else "")
    tris = [t for ts in per_file.values() for t in ts]
    logging.info("%d triangles from %d files", len(tris), len(per_file))
    if a.weld > 0:
        logging.info("welding seams...")
        tris, merged, dropped = weld(tris, a.weld)
        logging.info("welded %d vertices within %.0f cm; dropped %d collapsed triangles", merged, a.weld * 100, dropped)
    small = [f"{n} ({len(ts)})" for n, ts in per_file.items() if len(ts) < 50]
    if small:
        logging.warning("files with almost no collision (possible holes): %s", ", ".join(small))
    table = load_table(a.materials_toml)
    logging.info("sorting into surfaces...")
    meshes = build_meshes(tris, table, a.bbox, a.step_height)
    summary = Counter((t.material, classify(t, table, a.step_height)) for t in tris if not a.bbox or _inside(t, a.bbox))
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
        cover = files_covering(per_file, sx, sy)
        logging.warning("files whose area contains the start: %s",
                        ", ".join(f"{n} ({c} triangles)" for n, c in cover) or "none")
        logging.warning("nearest triangles to the start (distance, file, material, corners):")
        for line in probe(per_file, sx, sy):
            logging.warning("  %s", line)
    else:
        logging.info("ground under start: z=%.2f (start z=%.2f, %.2f m above)", g, sz, sz - g)
    if a.wall_surface == "ROAD" and "WALL" in meshes:
        meshes["ROAD_WALL"] = meshes.pop("WALL")  # mesh name 1ROAD_WALL_n: starts like the working 1ROAD_n
    logging.info("writing the track file...")
    write_track(a.out, a.layout, meshes, a.start, visible=a.visible, double_sided=a.double_sided, flip_walls=a.flip_walls)
    total = sum(len(m.indices) // 3 for m in meshes.values())
    logging.info("wrote %s layout %s: %d files, %d triangles (%s)", a.out, a.layout, len(files), total,
                 ", ".join(f"{k} {len(m.indices) // 3}" for k, m in meshes.items()))


if __name__ == "__main__":
    main()
