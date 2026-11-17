"""Minimal kn5 (Assetto Corsa model, version 5) writer for physics-only track meshes, plus a reader for tests.

Format reference: Content Manager's open-source AcTools.Kn5File (Kn5Writer.cs, ExtendedBinaryWriter.cs):
"sc6969", int32 version, textures, materials, then the node tree depth-first. Strings are int32 length +
UTF-8, bools are one byte, matrices are 16 floats row-major with the translation in row 4.
"""
from __future__ import annotations

import math
import struct
import sys
from array import array
from dataclasses import dataclass, field
from pathlib import Path

from .surfaces import Mesh

KN5_VERSION = 5
MAX_VERTS = 65535
NODE_BASE, NODE_MESH = 1, 2
IDENTITY = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]


@dataclass
class ReadMesh:
    vertices: list = field(default_factory=list)
    normals: list = field(default_factory=list)
    indices: list = field(default_factory=list)
    visible: bool = True
    renderable: bool = True


def start_matrix(gta_pos: tuple[float, float, float], gta_heading_deg: float) -> list[float]:
    """Dummy transform at a GTA position, facing a GTA heading (0 = north, counter-clockwise).
    The dummy's +Z axis (row 3) is the car's forward direction."""
    t = math.radians(gta_heading_deg + 180.0)
    s, c = math.sin(t), math.cos(t)
    x, y, z = gta_pos
    return [c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, x, z, -y, 1]


ALWAYS_DOUBLE_SIDED = ("WALL", "ROAD_WALL")  # GTA's wall winding varies; AC must stop the car from either side


def _flip_added(m: Mesh) -> Mesh:
    return Mesh(m.vertices, m.indices + [i for t in range(0, len(m.indices), 3)
                                         for i in (m.indices[t], m.indices[t + 2], m.indices[t + 1])])


def with_face_normals(m: Mesh) -> Mesh:
    """Give every corner its triangle's face normal (AC uses normals to decide which side of a wall is solid).
    Corners are shared only between triangles that face the same way, so flat areas stay compact."""
    out = Mesh()
    index: dict[tuple, int] = {}
    for t in range(0, len(m.indices), 3):
        ia, ib, ic = m.indices[t:t + 3]
        a, b, c = m.vertices[ia], m.vertices[ib], m.vertices[ic]
        u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        v = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        length = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2) or 1.0
        n = (n[0] / length, n[1] / length, n[2] / length)
        nk = (round(n[0], 2), round(n[1], 2), round(n[2], 2))
        for i in (ia, ib, ic):
            k = (i, nk)
            if k not in index:
                index[k] = len(out.vertices)
                out.vertices.append(m.vertices[i])
                out.normals.append(n)
            out.indices.append(index[k])
    return out


def write_kn5(path: Path, meshes: dict[str, Mesh], dummies: dict[str, list[float]],
              visible: bool = False, double_sided: bool = False) -> None:
    """visible: draw the meshes (debug). double_sided: add every triangle again with opposite winding
    (walls always get this)."""
    meshes = {k: _flip_added(m) if k in ALWAYS_DOUBLE_SIDED else m for k, m in meshes.items()}
    if double_sided:
        meshes = {k: _flip_added(m) if k not in ALWAYS_DOUBLE_SIDED else m for k, m in meshes.items()}
    meshes = {k: with_face_normals(m) for k, m in meshes.items()}
    out = bytearray(b"sc6969")
    out += struct.pack("<i", KN5_VERSION)
    out += struct.pack("<i", 0)  # textures
    out += struct.pack("<i", 1)  # materials
    out += _material("physics")
    chunks = [(f"1{key}_{i}", c) for key, mesh in meshes.items() for i, c in enumerate(_split(mesh))]
    out += _base_node(Path(path).stem, IDENTITY, len(chunks) + len(dummies))
    for name, mesh in chunks:
        out += _mesh_node(name, mesh, visible)
    for name, m in dummies.items():
        out += _base_node(name, m, 0)
    Path(path).write_bytes(bytes(out))


def _split(mesh: Mesh) -> list[Mesh]:
    chunks = [Mesh()]
    remap: dict[int, int] = {}
    for t in range(0, len(mesh.indices), 3):
        tri = mesh.indices[t:t + 3]
        new = [i for i in dict.fromkeys(tri) if i not in remap]
        if len(chunks[-1].vertices) + len(new) > MAX_VERTS:
            chunks.append(Mesh())
            remap = {}
        cur = chunks[-1]
        for i in tri:
            if i not in remap:
                remap[i] = len(cur.vertices)
                cur.vertices.append(mesh.vertices[i])
                if mesh.normals:
                    cur.normals.append(mesh.normals[i])
            cur.indices.append(remap[i])
    return [c for c in chunks if c.indices]


def _s(text: str) -> bytes:
    b = text.encode("utf-8")
    return struct.pack("<i", len(b)) + b


def _material(name: str) -> bytes:
    b = _s(name) + _s("ksPerPixel")
    b += struct.pack("<B?i", 0, False, 0)  # blend opaque, not alpha-tested, depth normal
    props = {"ksAmbient": 0.4, "ksDiffuse": 0.4, "ksSpecular": 0.0, "ksSpecularEXP": 1.0, "ksEmissive": 0.0}
    b += struct.pack("<i", len(props))
    for k, v in props.items():
        b += _s(k) + struct.pack("<f2f3f4f", v, *([0.0] * 9))
    b += struct.pack("<i", 0)  # texture mappings
    return b


def _base_node(name: str, matrix: list[float], children: int) -> bytes:
    return struct.pack("<i", NODE_BASE) + _s(name) + struct.pack("<i?", children, True) + struct.pack("<16f", *matrix)


def _mesh_node(name: str, m: Mesh, visible: bool = False) -> bytes:
    b = bytearray(struct.pack("<i", NODE_MESH) + _s(name) + struct.pack("<i?", 0, True))
    b += struct.pack("<???", False, visible, False)  # cast shadows, visible, transparent
    b += struct.pack("<i", len(m.vertices))
    flat = array("f")
    normals = m.normals or [(0.0, 1.0, 0.0)] * len(m.vertices)
    for v, n in zip(m.vertices, normals):  # position, normal, uv, tangent
        flat.extend((v[0], v[1], v[2], n[0], n[1], n[2], 0.0, 0.0, 1.0, 0.0, 0.0))
    if sys.byteorder != "little":
        flat.byteswap()
    b += flat.tobytes()
    idx = array("H", m.indices)
    if sys.byteorder != "little":
        idx.byteswap()
    b += struct.pack("<i", len(m.indices)) + idx.tobytes()
    b += struct.pack("<II", 0, 0)  # material id, layer
    b += struct.pack("<ff", 0.0, 0.0)  # lod in/out
    lo = [min(v[i] for v in m.vertices) for i in range(3)]
    hi = [max(v[i] for v in m.vertices) for i in range(3)]
    centre = [(lo[i] + hi[i]) / 2 for i in range(3)]
    radius = math.dist(centre, hi)  # half the bounding-box diagonal contains every vertex
    b += struct.pack("<3ff", *centre, radius)
    b += struct.pack("<?", visible)  # renderable
    return bytes(b)


# ---- reader (tests only) ----

class _R:
    def __init__(self, data: bytes):
        self.d, self.p = data, 0

    def take(self, fmt: str):
        vals = struct.unpack_from("<" + fmt, self.d, self.p)
        self.p += struct.calcsize("<" + fmt)
        return vals

    def s(self) -> str:
        (n,) = self.take("i")
        v = self.d[self.p:self.p + n].decode("utf-8")
        self.p += n
        return v


def read_kn5(path: Path):
    r = _R(Path(path).read_bytes())
    assert r.d[:6] == b"sc6969"
    r.p = 6
    (version,) = r.take("i")
    if version > 5:
        r.take("i")
    (ntex,) = r.take("i")
    for _ in range(ntex):
        r.take("i"); r.s(); (n,) = r.take("i"); r.p += n
    (nmat,) = r.take("i")
    for _ in range(nmat):
        r.s(); r.s(); r.take("B?i")
        (np_,) = r.take("i")
        for _ in range(np_):
            r.s(); r.take("f2f3f4f")
        (nt,) = r.take("i")
        for _ in range(nt):
            r.s(); r.take("i"); r.s()
    meshes, dummies = {}, {}

    def node(is_root: bool):
        (cls,) = r.take("i")
        name = r.s()
        children, _active = r.take("i?")
        if cls == NODE_BASE:
            m = list(r.take("16f"))
            if not is_root:
                dummies[name] = m
        else:
            rm = ReadMesh()
            _shadows, rm.visible, _transparent = r.take("???")
            (nv,) = r.take("i")
            vals = r.take(f"{nv * 11}f")
            rm.vertices = [tuple(vals[i:i + 3]) for i in range(0, len(vals), 11)]
            rm.normals = [tuple(vals[i + 3:i + 6]) for i in range(0, len(vals), 11)]
            (ni,) = r.take("i")
            rm.indices = list(r.take(f"{ni}H"))
            r.take("IIff3ff")
            (rm.renderable,) = r.take("?")
            meshes[name] = rm
        for _ in range(children):
            node(False)

    node(True)
    return meshes, dummies
