"""NumPy pipeline for big maps: the same track as the pure-Python path (surfaces.py + kn5.py), but triangles live
in flat arrays instead of one Python object each, so the full GTA map fits in a few GB and runs in minutes.
What was read can be cached, so later runs (other materials, kerb height, ...) skip the XML entirely."""
from __future__ import annotations

import hashlib
import json
import logging
import math
import struct
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from . import kn5 as _k
from .surfaces import WALL_SLOPE_DEG, material_key
from .ybn_xml import Tri, read_ybn_xml

log = logging.getLogger(__name__)

KEYS = ("ROAD", "KERB", "GRASS", "SAND", "GRAVEL", "SNOW", "ICE", "WALL")
WALL = KEYS.index("WALL")
CACHE_VERSION = 1


@dataclass
class Soup:
    verts: np.ndarray   # (n, 3) float32, GTA space
    tris: np.ndarray    # (m, 3) uint32 indices into verts
    mat: np.ndarray     # (m,) uint16 index into names
    file: np.ndarray    # (m,) uint32 index into files
    names: list[str]    # material names
    files: list[str]    # file names

    def subset(self, mask: np.ndarray) -> "Soup":
        return Soup(self.verts, self.tris[mask], self.mat[mask], self.file[mask], self.names, self.files)


# ---------- building a soup ----------

class _Builder:
    def __init__(self):
        self.verts, self.tris, self.mat, self.file = [], [], [], []
        self.names: dict[str, int] = {}
        self.files: list[str] = []
        self.nverts = 0

    def add(self, file_name: str, tris: list[Tri]) -> None:
        fid = len(self.files)
        self.files.append(file_name)
        if not tris:
            return
        index: dict[tuple, int] = {}
        vs, ts, ms = [], [], []
        for t in tris:
            row = []
            for p in (t.a, t.b, t.c):
                i = index.get(p)
                if i is None:
                    i = index[p] = len(vs)
                    vs.append(p)
                row.append(i + self.nverts)
            ts.append(row)
            ms.append(self.names.setdefault(t.material, len(self.names)))
        self.verts.append(np.asarray(vs, np.float32))
        self.tris.append(np.asarray(ts, np.uint32))
        self.mat.append(np.asarray(ms, np.uint16))
        self.file.append(np.full(len(ts), fid, np.uint32))
        self.nverts += len(vs)

    def soup(self) -> Soup:
        cat = lambda xs, shape, dt: np.concatenate(xs) if xs else np.zeros(shape, dt)
        return Soup(cat(self.verts, (0, 3), np.float32), cat(self.tris, (0, 3), np.uint32),
                    cat(self.mat, (0,), np.uint16), cat(self.file, (0,), np.uint32),
                    list(self.names), self.files)


def soup_from_tris(tris: list[Tri], file_name: str = "tris") -> Soup:
    b = _Builder()
    b.add(file_name, tris)
    return b.soup()


def read_files(files: list[Path], material_names: list[str] | None) -> Soup:
    b = _Builder()
    for i, f in enumerate(files, 1):
        b.add(f.name, read_ybn_xml(f, material_names))
        if i % 200 == 0:
            log.info("  read %d / %d files", i, len(files))
    return b.soup()


# ---------- cache ----------

def signature(files: list[Path], materials_dat: Path | None) -> str:
    h = hashlib.sha256(str(CACHE_VERSION).encode())
    for p in sorted(files) + ([materials_dat] if materials_dat else []):
        st = p.stat()
        h.update(f"{p}|{st.st_size}|{int(st.st_mtime)}\n".encode())
    return h.hexdigest()


def save_cache(path: Path, soup: Soup, sig: str) -> None:
    meta = json.dumps({"sig": sig, "names": soup.names, "files": soup.files})
    tmp = Path(str(path) + ".tmp")
    with open(tmp, "wb") as fh:
        np.savez(fh, verts=soup.verts, tris=soup.tris, mat=soup.mat, fileid=soup.file,
                 meta=np.frombuffer(meta.encode(), np.uint8))
    tmp.replace(path)  # a crash mid-write never leaves a broken cache behind


def load_cache(path: Path, sig: str) -> Soup | None:
    try:
        with np.load(path) as z:
            meta = json.loads(bytes(z["meta"]).decode())
            if meta.get("sig") != sig:
                return None
            return Soup(z["verts"], z["tris"], z["mat"], z["fileid"], meta["names"], meta["files"])
    except (OSError, ValueError, KeyError, EOFError):
        return None


# ---------- per-file checks ----------

def file_boxes(s: Soup) -> tuple[np.ndarray, np.ndarray]:
    """Per file: min and max corner (GTA space); files without triangles get inf/-inf."""
    n = len(s.files)
    lo = np.full((n, 3), np.inf, np.float32)
    hi = np.full((n, 3), -np.inf, np.float32)
    for k in range(3):
        v = s.verts[s.tris[:, k]]
        np.minimum.at(lo, s.file, v)
        np.maximum.at(hi, s.file, v)
    return lo, hi


def interior_files(s: Soup) -> list[int]:
    """Files that look like interiors stored in local coordinates (see cli.looks_like_interior)."""
    lo, hi = file_boxes(s)
    ok = np.isfinite(lo[:, 0])
    lo, hi = np.where(ok[:, None], lo, 0), np.where(ok[:, None], hi, 0)  # empty files: no inf maths
    c = (lo + hi) / 2
    size = np.maximum(hi[:, 0] - lo[:, 0], hi[:, 1] - lo[:, 1])
    hit = ok & (np.hypot(c[:, 0], c[:, 1]) < 300) & (size < 300) & (lo[:, 2] < 15) & (hi[:, 2] < 40)
    return list(np.nonzero(hit)[0])


# ---------- weld ----------

def weld(s: Soup, tol: float) -> tuple[Soup, int, int]:
    """Snap vertices closer than tol together (each onto the first kept vertex near it, no chaining), then drop
    triangles that collapsed. Returns (soup, vertices merged, triangles dropped)."""
    if tol <= 0 or len(s.tris) == 0:
        return s, 0, 0
    from scipy.spatial import cKDTree

    uniq, inv = np.unique(s.verts, axis=0, return_inverse=True)
    inv = inv.reshape(-1)
    pairs = cKDTree(uniq).query_pairs(tol, output_type="ndarray")
    rep = np.arange(len(uniq))
    if len(pairs):
        pairs = pairs[np.lexsort((pairs[:, 0], pairs[:, 1]))]  # by j, then smallest i first
        r = rep.tolist()
        for i, j in pairs.tolist():
            if r[j] == j and r[i] == i:
                r[j] = i
        rep = np.asarray(r)
    merged = int((rep != np.arange(len(uniq))).sum())
    tris = rep[inv[s.tris]]
    keep = (tris[:, 0] != tris[:, 1]) & (tris[:, 1] != tris[:, 2]) & (tris[:, 0] != tris[:, 2])
    used, local = np.unique(tris[keep], return_inverse=True)
    out = Soup(uniq[used].astype(np.float32), local.reshape(-1, 3).astype(np.uint32), s.mat[keep], s.file[keep],
               s.names, s.files)
    return out, merged, int((~keep).sum())


# ---------- classify ----------

def material_keys(names: list[str], table: dict[str, str]) -> list[str]:
    return [material_key(n, table) for n in names]


def classify(s: Soup, table: dict[str, str], step_height: float) -> np.ndarray:
    """AC surface index (into KEYS) per triangle, with the same rules as surfaces.classify."""
    v = s.verts[s.tris].astype(np.float64)  # (m, 3, 3)
    n = np.cross(v[:, 1] - v[:, 0], v[:, 2] - v[:, 0])
    length = np.linalg.norm(n, axis=1)
    steep = np.abs(n[:, 2]) < (math.cos(math.radians(WALL_SLOPE_DEG)) - 1e-9) * length
    height = v[:, :, 2].max(1) - v[:, :, 2].min(1)
    by_mat = np.array([KEYS.index(k) for k in material_keys(s.names, table)] or [0], np.uint8)
    keys = by_mat[s.mat]
    wall = (length == 0) | (steep & ((height >= step_height) | (keys == WALL)))
    keys[wall] = WALL
    return keys


# ---------- probes ----------

def ground_under(s: Soup, x: float, y: float, below: float) -> float | None:
    v = s.verts[s.tris].astype(np.float64)
    near = ((v[:, :, 0].min(1) <= x) & (v[:, :, 0].max(1) >= x) & (v[:, :, 1].min(1) <= y) & (v[:, :, 1].max(1) >= y))
    v = v[near]
    if not len(v):
        return None
    (x1, y1, z1), (x2, y2, z2), (x3, y3, z3) = (v[:, k].T for k in range(3))
    d = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3)
    ok = np.abs(d) > 1e-9
    d = np.where(ok, d, 1)
    l1 = ((y2 - y3) * (x - x3) + (x3 - x2) * (y - y3)) / d
    l2 = ((y3 - y1) * (x - x3) + (x1 - x3) * (y - y3)) / d
    l3 = 1 - l1 - l2
    z = l1 * z1 + l2 * z2 + l3 * z3
    hit = ok & (l1 >= 0) & (l2 >= 0) & (l3 >= 0) & (z <= below)
    return float(z[hit].max()) if hit.any() else None


def coverage(s: Soup, x: float, y: float):
    v = s.verts[s.tris]
    lo, hi = s.verts[np.unique(s.tris)].min(0), s.verts[np.unique(s.tris)].max(0)
    c = v.mean(1)
    nearest = float(np.hypot(c[:, 0] - x, c[:, 1] - y).min())
    return (float(lo[0]), float(lo[1]), float(lo[2]), float(hi[0]), float(hi[1]), float(hi[2])), nearest


# ---------- meshes ----------

@dataclass
class BulkMesh:
    verts: np.ndarray    # (n, 3) float32, AC space
    normals: np.ndarray  # (n, 3) float32
    tris: np.ndarray     # (m, 3) uint32


def _to_ac(v: np.ndarray) -> np.ndarray:
    return np.stack([v[:, 0], v[:, 2], -v[:, 1]], 1).astype(np.float32)


def build_meshes(s: Soup, keys: np.ndarray, bbox_gta=None) -> dict[str, np.ndarray]:
    """Per AC surface: triangle index arrays (into AC-space verts), floors turned to face up."""
    va = _to_ac(s.verts)
    out = {}
    mask_all = np.ones(len(s.tris), bool)
    if bbox_gta:
        c = s.verts[s.tris].mean(1)
        x0, y0, x1, y1 = bbox_gta
        mask_all = ((c[:, 0] >= min(x0, x1)) & (c[:, 0] <= max(x0, x1)) & (c[:, 1] >= min(y0, y1)) & (c[:, 1] <= max(y0, y1)))
    for ki, key in enumerate(KEYS):
        t = s.tris[(keys == ki) & mask_all].copy()
        if not len(t):
            continue
        if key != "WALL":
            a, b, c = va[t[:, 0]], va[t[:, 1]], va[t[:, 2]]
            ny = (b[:, 2] - a[:, 2]) * (c[:, 0] - a[:, 0]) - (b[:, 0] - a[:, 0]) * (c[:, 2] - a[:, 2])
            down = ny < 0
            t[down] = t[down][:, [0, 2, 1]]
        out[key] = t
    return {"__verts__": va, **out}


def _with_normals(va: np.ndarray, t: np.ndarray) -> BulkMesh:
    a, b, c = va[t[:, 0]], va[t[:, 1]], va[t[:, 2]]
    n = np.cross(b - a, c - a).astype(np.float64)
    ln = np.linalg.norm(n, axis=1, keepdims=True)
    n = (n / np.where(ln > 0, ln, 1)).astype(np.float32)
    q = np.clip(np.round(n * 100).astype(np.int64) + 128, 0, 255)
    qq = (q[:, 0] << 16) | (q[:, 1] << 8) | q[:, 2]
    code = (t.astype(np.int64) << 24) | qq[:, None]  # same corner + same facing = shared vertex
    uniq, first, inv = np.unique(code.reshape(-1), return_index=True, return_inverse=True)
    vidx = (uniq >> 24).astype(np.int64)
    normals = np.repeat(n, 3, 0)[first]
    return BulkMesh(va[vidx], normals, inv.reshape(-1, 3).astype(np.uint32))


# ---------- kn5 ----------

def _mesh_node(name: str, m: BulkMesh, visible: bool) -> bytes:
    b = bytearray(struct.pack("<i", _k.NODE_MESH) + _k._s(name) + struct.pack("<i?", 0, True))
    b += struct.pack("<???", False, visible, False)
    b += struct.pack("<i", len(m.verts))
    rec = np.zeros((len(m.verts), 11), "<f4")
    rec[:, 0:3] = m.verts
    rec[:, 3:6] = m.normals
    rec[:, 8] = 1.0  # tangent x
    b += rec.tobytes()
    idx = m.tris.reshape(-1).astype("<u2")
    b += struct.pack("<i", len(idx)) + idx.tobytes()
    b += struct.pack("<IIff", 0, 0, 0.0, 0.0)
    lo, hi = m.verts.min(0), m.verts.max(0)
    centre = (lo + hi) / 2
    b += struct.pack("<3ff", *centre.tolist(), float(np.linalg.norm(hi - centre)))
    b += struct.pack("<?", visible)
    return bytes(b)


def _chunks(va: np.ndarray, t: np.ndarray):
    """Split into pieces of at most MAX_VERTS vertices, grouped by 250 m cells so each piece is compact."""
    m = _with_normals(va, t)
    cen = m.verts[m.tris].mean(1)
    order = np.lexsort((np.floor(cen[:, 2] / 250), np.floor(cen[:, 0] / 250)))
    tris = m.tris[order]
    per = _k.MAX_VERTS // 3
    for s in range(0, len(tris), per):
        part = tris[s:s + per]
        used, local = np.unique(part, return_inverse=True)
        yield BulkMesh(m.verts[used], m.normals[used], local.reshape(-1, 3).astype(np.uint32))


def write_kn5(path: Path, meshes: dict[str, np.ndarray], dummies: dict[str, list[float]], visible: bool = False,
              double_sided: bool = False, flip_walls: bool = False) -> None:
    va = meshes.get("__verts__")
    if va is None:  # direct use with BulkMesh-free dicts (tests): nothing but dummies
        va = np.zeros((0, 3), np.float32)
    groups = {}
    for key, t in meshes.items():
        if key == "__verts__":
            continue
        if key in _k.WALL_KEYS:
            if flip_walls:
                t = t[:, [0, 2, 1]]
        elif double_sided:
            t = np.concatenate([t, t[:, [0, 2, 1]]])
        groups[key] = t
    nodes = []
    for key, t in groups.items():
        for i, piece in enumerate(_chunks(va, t)):
            nodes.append(_mesh_node(f"1{key}_{i}", piece, visible))
    with open(path, "wb") as fh:
        fh.write(b"sc6969" + struct.pack("<iii", _k.KN5_VERSION, 0, 1) + _k._material("physics"))
        fh.write(_k._base_node(Path(path).stem, _k.IDENTITY, len(nodes) + len(dummies)))
        for n in nodes:
            fh.write(n)
        for name, mtx in dummies.items():
            fh.write(_k._base_node(name, mtx, 0))


# ---------- size reduction ----------

# Los Santos + Blaine County, without North Yankton (prologue) and far-away DLC sets. GTA x0, y0, x1, y1.
GTA_MAP_BOUNDS = (-4200.0, -4200.0, 4700.0, 8300.0)


def in_bounds(s: Soup, bounds, zmin: float, zmax: float) -> np.ndarray:
    """Triangles whose centre is inside the map rectangle and between zmin and zmax (drops seabed and sky sets)."""
    c = s.verts[s.tris].mean(1)
    x0, y0, x1, y1 = bounds
    return (c[:, 0] >= x0) & (c[:, 0] <= x1) & (c[:, 1] >= y0) & (c[:, 1] <= y1) & (c[:, 2] >= zmin) & (c[:, 2] <= zmax)


def dedupe(s: Soup) -> tuple[Soup, int]:
    """Drop triangles that use exactly the same three (welded) corners as an earlier one, in any order or winding:
    DLC replacement blocks often duplicate the original block's collision."""
    if not len(s.tris):
        return s, 0
    key = np.sort(s.tris, axis=1)
    _, first = np.unique(key, axis=0, return_index=True)
    keep = np.zeros(len(s.tris), bool)
    keep[first] = True
    return s.subset(keep), int((~keep).sum())


def busiest_cells(s: Soup, cell: float = 2000.0, top: int = 8) -> list[tuple[int, int, int]]:
    """(cell x, cell y, triangles) of the densest cell x cell areas, to see where the triangles are."""
    c = s.verts[s.tris].mean(1)
    cx = np.floor(c[:, 0] / cell).astype(np.int64)
    cy = np.floor(c[:, 1] / cell).astype(np.int64)
    u, n = np.unique(np.stack([cx, cy], 1), axis=0, return_counts=True)
    order = np.argsort(-n)[:top]
    return [(int(u[i, 0] * cell), int(u[i, 1] * cell), int(n[i])) for i in order]
