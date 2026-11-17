"""Classify GTA collision triangles into AC surface keys and build per-surface meshes in AC space."""
from __future__ import annotations

import logging
import math
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

from .ybn_xml import Tri

log = logging.getLogger(__name__)

AC_KEYS = ("ROAD", "KERB", "GRASS", "SAND", "GRAVEL", "WALL")
WALL_SLOPE_DEG = 60.0
STEP_HEIGHT = 0.35  # steep faces lower than this (kerbs, steps) stay drivable so the wheels climb them
_COS_WALL = math.cos(math.radians(WALL_SLOPE_DEG))
_warned: set[str] = set()

V3 = tuple[float, float, float]


@dataclass
class Mesh:
    vertices: list[V3] = field(default_factory=list)  # AC space (Y-up)
    indices: list[int] = field(default_factory=list)
    normals: list[V3] = field(default_factory=list)  # per vertex; empty = not computed yet


def load_table(path: Path | None = None) -> dict[str, str]:
    path = path or Path(__file__).with_name("materials.toml")
    return dict(tomllib.loads(Path(path).read_text())["materials"])


def classify(t: Tri, table: dict[str, str], step_height: float = STEP_HEIGHT) -> str:
    n = _normal(t.a, t.b, t.c)
    length = math.sqrt(sum(x * x for x in n))
    if length == 0:
        return "WALL"
    if abs(n[2]) / length < _COS_WALL - 1e-9:
        height = max(t.a[2], t.b[2], t.c[2]) - min(t.a[2], t.b[2], t.c[2])
        if height >= step_height or table.get(t.material) == "WALL":
            return "WALL"
    if t.material in table:
        return table[t.material]
    best = max((k for k in table if k.endswith("*") and t.material.startswith(k[:-1])), key=len, default=None)
    if best:
        return table[best]
    guess = guess_surface(t.material)
    if t.material not in _warned:
        _warned.add(t.material)
        log.warning("unknown GTA material %s -> %s (guessed from its name)", t.material, guess)
    return guess


KEYWORDS = (  # first match wins
    (("GRASS", "BUSH", "LEAVES", "HAY", "MOSS", "PLANT", "HEDGE", "TWIG"), "GRASS"),
    (("SAND",), "SAND"),
    (("GRAVEL", "MUD", "DIRT", "CLAY", "SOIL", "ROCK"), "GRAVEL"),
)


def guess_surface(material: str) -> str:
    m = material.upper()
    for words, key in KEYWORDS:
        if any(w in m for w in words):
            return key
    return "ROAD"


def gta_to_ac(p: V3) -> V3:
    return (p[0], p[2], -p[1])


def build_meshes(tris: list[Tri], table: dict[str, str], bbox_gta: tuple[float, float, float, float] | None,
                 step_height: float = STEP_HEIGHT) -> dict[str, Mesh]:
    meshes: dict[str, Mesh] = {}
    lookup: dict[str, dict[tuple, int]] = {}
    for t in tris:
        if bbox_gta and not _inside(t, bbox_gta):
            continue
        key = classify(t, table, step_height)
        a, b, c = (gta_to_ac(p) for p in (t.a, t.b, t.c))
        if key != "WALL" and _normal(a, b, c)[1] < 0:  # floors face up (+Y) in AC
            b, c = c, b
        mesh = meshes.setdefault(key, Mesh())
        index = lookup.setdefault(key, {})
        for p in (a, b, c):
            k = tuple(round(x, 3) for x in p)
            if k not in index:
                index[k] = len(mesh.vertices)
                mesh.vertices.append(p)
            mesh.indices.append(index[k])
    return meshes


def _inside(t: Tri, bbox) -> bool:
    x = (t.a[0] + t.b[0] + t.c[0]) / 3
    y = (t.a[1] + t.b[1] + t.c[1]) / 3
    x0, y0, x1, y1 = bbox
    return min(x0, x1) <= x <= max(x0, x1) and min(y0, y1) <= y <= max(y0, y1)


def _normal(a, b, c):
    u = [b[i] - a[i] for i in range(3)]
    v = [c[i] - a[i] for i in range(3)]
    return (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])


def weld(tris: list[Tri], tol: float) -> tuple[list[Tri], int, int]:
    """Snap vertices closer than `tol` metres onto one shared point, across all pieces and files, so neighbouring
    collision pieces share edges instead of leaving hairline cracks. Triangles that collapse are dropped.
    Returns (triangles, vertices merged, triangles dropped)."""
    if tol <= 0:
        return list(tris), 0, 0
    cell = 2 * tol  # with cells twice the tolerance, a neighbour can only be in 8 of the 27 nearby cells
    grid: dict[tuple[int, int, int], list[V3]] = {}
    snap: dict[V3, V3] = {}
    merged = 0
    tol2 = tol * tol
    for t in tris:
        for p in (t.a, t.b, t.c):
            if p in snap:
                continue
            fx, fy, fz = p[0] / cell, p[1] / cell, p[2] / cell
            cx, cy, cz = math.floor(fx), math.floor(fy), math.floor(fz)
            ox = -1 if fx - cx < 0.5 else 1
            oy = -1 if fy - cy < 0.5 else 1
            oz = -1 if fz - cz < 0.5 else 1
            best, best_d = None, tol2
            for dx in (0, ox):
                for dy in (0, oy):
                    for dz in (0, oz):
                        for q in grid.get((cx + dx, cy + dy, cz + dz), ()):
                            d = (q[0] - p[0]) ** 2 + (q[1] - p[1]) ** 2 + (q[2] - p[2]) ** 2
                            if d < best_d:
                                best, best_d = q, d
            if best is None:
                snap[p] = p
                grid.setdefault((cx, cy, cz), []).append(p)
            else:
                snap[p] = best
                merged += 1
    out, dropped = [], 0
    for t in tris:
        a, b, c = snap[t.a], snap[t.b], snap[t.c]
        if a == b or b == c or a == c:
            dropped += 1
            continue
        out.append(t if (a, b, c) == (t.a, t.b, t.c) else Tri(a, b, c, t.material))
    return out, merged, dropped
