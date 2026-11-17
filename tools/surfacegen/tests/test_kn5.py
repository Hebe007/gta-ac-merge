import math

import pytest

from surfacegen.kn5 import MAX_VERTS, read_kn5, start_matrix, write_kn5
from surfacegen.surfaces import Mesh


def square(offset=0.0):
    return Mesh(vertices=[(offset, 0, 0), (offset + 1, 0, 0), (offset, 0, -1), (offset + 1, 0, -1)], indices=[0, 2, 1, 1, 2, 3])


def tris(m):
    return [tuple(tuple(round(c, 5) for c in m.vertices[i]) for i in m.indices[t:t + 3]) for t in range(0, len(m.indices), 3)]


DUMMIES = {n: start_matrix((1, 2, 3), 0) for n in ("AC_START_0", "AC_PIT_0", "AC_HOTLAP_START_0", "AC_TIME_0_L", "AC_TIME_0_R")}


def test_round_trip_keeps_meshes_and_dummies(tmp_path):
    p = tmp_path / "t.kn5"
    write_kn5(p, {"ROAD": square(), "WALL": square(5)}, DUMMIES)
    meshes, dummies = read_kn5(p)
    assert set(meshes) == {"1ROAD_0", "1WALL_0"}
    assert tris(meshes["1ROAD_0"]) == tris(square())
    assert set(dummies) == set(DUMMIES)
    assert dummies["AC_START_0"] == pytest.approx(DUMMIES["AC_START_0"])


def test_file_starts_with_magic_and_version(tmp_path):
    p = tmp_path / "t.kn5"
    write_kn5(p, {"ROAD": square()}, {})
    raw = p.read_bytes()
    assert raw[:6] == b"sc6969"
    assert int.from_bytes(raw[6:10], "little") == 5


def test_mesh_nodes_are_not_rendered(tmp_path):
    p = tmp_path / "t.kn5"
    write_kn5(p, {"ROAD": square()}, {})
    meshes, _ = read_kn5(p)
    assert meshes["1ROAD_0"].renderable is False and meshes["1ROAD_0"].visible is False


def test_big_meshes_split_under_ushort_limit(tmp_path):
    n = MAX_VERTS // 3 + 10  # disjoint triangles -> 3 verts each, more than one chunk
    verts, idx = [], []
    for i in range(n):
        verts += [(i, 0, 0), (i, 0, -1), (i + 0.5, 0, 0)]
        idx += [3 * i, 3 * i + 1, 3 * i + 2]
    p = tmp_path / "t.kn5"
    write_kn5(p, {"ROAD": Mesh(verts, idx)}, {})
    meshes, _ = read_kn5(p)
    assert set(meshes) == {"1ROAD_0", "1ROAD_1"}
    assert all(len(m.vertices) <= MAX_VERTS for m in meshes.values())
    assert sum(len(m.indices) for m in meshes.values()) == len(idx)


def test_start_matrix_faces_gta_heading():
    # GTA heading 0 = north (+Y GTA) = -Z in AC. Row-vector matrix: row 3 is the dummy's forward (+Z).
    m = start_matrix((10, 20, 30), 0)
    assert m[8:11] == pytest.approx([0, 0, -1], abs=1e-6)
    assert m[12:15] == pytest.approx([10, 30, -20])  # GTA (10,20,30) -> AC (10,30,-20)
    m90 = start_matrix((0, 0, 0), 90)  # GTA heading 90 = west (-X)
    assert m90[8:11] == pytest.approx([-1, 0, 0], abs=1e-6)


def test_large_mesh_writes_fast(tmp_path):
    import time
    n = 200_000  # triangles; downtown is ~1M
    verts = [(float(i % 1000), 0.0, -float(i // 1000)) for i in range(n + 2)]
    idx = [k for i in range(n) for k in (i, i + 1, i + 2)]
    t0 = time.perf_counter()
    write_kn5(tmp_path / "big.kn5", {"ROAD": Mesh(verts, idx)}, {})
    assert time.perf_counter() - t0 < 10
