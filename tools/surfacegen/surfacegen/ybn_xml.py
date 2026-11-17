"""Read GTA collision exported by CodeWalker ("Export XML" on .ybn files) into world-space triangles.

Vertex world position = CompositeTransform(vertex + GeometryCenter), CodeWalker's GetVertexPos.
CompositeTransform is a row-vector 4x4 (SharpDX): p' = [x y z 1] * M, translation in the last row.
"""
from __future__ import annotations

import logging
import xml.etree.ElementTree as ET
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

log = logging.getLogger(__name__)

V3 = tuple[float, float, float]
IDENTITY = [[1.0, 0, 0, 0], [0, 1.0, 0, 0], [0, 0, 1.0, 0], [0, 0, 0, 1.0]]
SKIPPED_SHAPES = ("Sphere", "Capsule", "Cylinder")


@dataclass(frozen=True)
class Tri:
    a: V3
    b: V3
    c: V3
    material: str


def read_materials_dat(path: Path) -> list[str]:
    """Material names in index order from GTA's materials.dat (export it with CodeWalker from common.rpf)."""
    path = Path(path)
    if path.is_dir():  # CodeWalker's raw export can create a folder named materials.dat
        inner = sorted(path.rglob("materials.dat"))
        if not inner:
            raise SystemExit(f"{path} is a folder with no materials.dat inside")
        path = inner[0]
    names = []
    for line in path.read_text(errors="replace").splitlines():
        tok = line.split()
        if not tok or tok[0].startswith("#") or _is_number(tok[0]):
            continue
        names.append(tok[0])
    return names


def read_ybn_xml(path: Path, material_names: list[str] | None = None) -> list[Tri]:
    root = ET.parse(path).getroot()
    bounds = root if root.tag == "Bounds" else root.find("Bounds")
    tris: list[Tri] = []
    skipped: Counter[str] = Counter()
    if bounds is not None:
        _walk(bounds, material_names, tris, skipped)
    for shape, n in skipped.items():
        log.info("%s: skipped %d %s polygon(s)", Path(path).name, n, shape.lower())
    return tris


def _walk(node: ET.Element, names, out: list[Tri], skipped: Counter) -> None:
    kind = node.get("type", "")
    if kind == "Composite":
        for child in node.findall("./Children/Item"):
            _walk(child, names, out, skipped)
    elif kind in ("Geometry", "GeometryBVH"):
        _geometry(node, names, out, skipped)


def _geometry(node: ET.Element, names, out: list[Tri], skipped: Counter) -> None:
    m = _matrix(node.find("CompositeTransform"))
    cg = node.find("GeometryCenter")
    centre = (float(cg.get("x")), float(cg.get("y")), float(cg.get("z"))) if cg is not None else (0.0, 0.0, 0.0)
    verts = [_xform(m, (v[0] + centre[0], v[1] + centre[1], v[2] + centre[2])) for v in _raw_v3(node.find("Vertices"))]
    mats = [int(it.find("Type").get("value")) for it in node.findall("./Materials/Item")]

    def mat_name(m_attr: str) -> str:
        idx = mats[int(m_attr)] if int(m_attr) < len(mats) else 0
        return names[idx] if names and idx < len(names) else f"#{idx}"

    polys = node.find("Polygons")
    if polys is None:
        return
    for p in polys:
        if p.tag == "Triangle":
            out.append(Tri(verts[int(p.get("v1"))], verts[int(p.get("v2"))], verts[int(p.get("v3"))], mat_name(p.get("m"))))
        elif p.tag == "Box":
            corners = [verts[int(p.get(k))] for k in ("v1", "v2", "v3", "v4")]
            out.extend(Tri(a, b, c, mat_name(p.get("m"))) for a, b, c in box_triangles(corners))
        elif p.tag in SKIPPED_SHAPES:
            skipped[p.tag] += 1


def box_triangles(v: list[V3]) -> list[tuple[V3, V3, V3]]:
    """GTA box polygons store 4 alternating corners; the other 4 are their mirrors through the centre.
    Each pair of stored corners shares one face with the mirrors of the other two."""
    c = tuple(sum(p[i] for p in v) / 4 for i in range(3))
    o = [tuple(2 * c[i] - p[i] for i in range(3)) for p in v]
    out = []
    for i in range(4):
        for j in range(i + 1, 4):
            k, l = [x for x in range(4) if x not in (i, j)]
            for tri in ((v[i], o[k], v[j]), (v[i], v[j], o[l])):
                out.append(_outward(tri, c))
    return out


def _outward(t, centre):
    a, b, c = t
    n = _cross(_sub(b, a), _sub(c, a))
    mid = tuple((a[i] + b[i] + c[i]) / 3 - centre[i] for i in range(3))
    return t if sum(n[i] * mid[i] for i in range(3)) >= 0 else (a, c, b)


def _raw_v3(node) -> list[V3]:
    if node is None or not node.text:
        return []
    return [tuple(float(x) for x in line.split(",")) for line in node.text.strip().splitlines() if line.strip()]


def _matrix(node) -> list[list[float]]:
    if node is None or not node.text or not node.text.strip():
        return IDENTITY
    rows = [[float(x) for x in line.split(",")] for line in node.text.strip().splitlines() if line.strip()]
    return rows if len(rows) == 4 else IDENTITY


def _xform(m, p: V3) -> V3:
    x, y, z = p
    return tuple(x * m[0][i] + y * m[1][i] + z * m[2][i] + m[3][i] for i in range(3))


def _sub(a, b):
    return tuple(a[i] - b[i] for i in range(3))


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _is_number(s: str) -> bool:
    try:
        float(s)
        return True
    except ValueError:
        return False
