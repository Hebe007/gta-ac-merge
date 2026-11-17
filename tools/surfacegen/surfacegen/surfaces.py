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
_COS_WALL = math.cos(math.radians(WALL_SLOPE_DEG))
_warned: set[str] = set()

V3 = tuple[float, float, float]


@dataclass
class Mesh:
    vertices: list[V3] = field(default_factory=list)  # AC space (Y-up)
    indices: list[int] = field(default_factory=list)


def load_table(path: Path | None = None) -> dict[str, str]:
    path = path or Path(__file__).with_name("materials.toml")
    return dict(tomllib.loads(Path(path).read_text())["materials"])


def classify(t: Tri, table: dict[str, str]) -> str:
    n = _normal(t.a, t.b, t.c)
    length = math.sqrt(sum(x * x for x in n))
    if length == 0 or abs(n[2]) / length < _COS_WALL - 1e-9:
        return "WALL"
    if t.material in table:
        return table[t.material]
    best = max((k for k in table if k.endswith("*") and t.material.startswith(k[:-1])), key=len, default=None)
    if best:
        return table[best]
    if t.material not in _warned:
        _warned.add(t.material)
        log.warning("unknown GTA material %s -> ROAD", t.material)
    return "ROAD"


def gta_to_ac(p: V3) -> V3:
    return (p[0], p[2], -p[1])


def build_meshes(tris: list[Tri], table: dict[str, str], bbox_gta: tuple[float, float, float, float] | None) -> dict[str, Mesh]:
    meshes: dict[str, Mesh] = {}
    lookup: dict[str, dict[tuple, int]] = {}
    for t in tris:
        if bbox_gta and not _inside(t, bbox_gta):
            continue
        key = classify(t, table)
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
