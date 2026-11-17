"""The NumPy pipeline (surfacegen.bulk) must build the same track as the pure-Python one, use far less memory,
and cache what it read."""
import time
from pathlib import Path

import numpy as np
import pytest

from surfacegen import bulk, cli
from surfacegen.kn5 import read_kn5
from surfacegen.surfaces import load_table
from surfacegen.ybn_xml import Tri

FIX = Path(__file__).parent / "fixtures"
COMMON = ["--keep-interiors", "--xml-dir", str(FIX), "--materials-dat", str(FIX / "materials.dat"),
          "--layout", "downtown", "--start", "105,202,6,0", "--weld", "0"]


def tri_sets(kn5_path):
    meshes, _ = read_kn5(kn5_path)
    out = {}
    for name, m in meshes.items():
        key = name[1:].rsplit("_", 1)[0]
        for t in range(0, len(m.indices), 3):
            out.setdefault(key, set()).add(tuple(tuple(round(c, 3) for c in m.vertices[i]) for i in m.indices[t:t + 3]))
    return out


def test_bulk_builds_the_same_track_as_python(tmp_path):
    cli.main(COMMON + ["--out", str(tmp_path / "py" / "gta_ls"), "--engine", "python"])
    cli.main(COMMON + ["--out", str(tmp_path / "np" / "gta_ls"), "--engine", "bulk", "--no-cache"])
    a = tri_sets(tmp_path / "py" / "gta_ls" / "gta_ls_downtown.kn5")
    b = tri_sets(tmp_path / "np" / "gta_ls" / "gta_ls_downtown.kn5")
    assert a.keys() == b.keys()
    for k in a:
        assert a[k] == b[k], k


def test_bulk_normals_match_triangles(tmp_path):
    cli.main(COMMON + ["--out", str(tmp_path / "gta_ls"), "--engine", "bulk", "--no-cache"])
    meshes, _ = read_kn5(tmp_path / "gta_ls" / "gta_ls_downtown.kn5")
    for m in meshes.values():
        for t in range(0, len(m.indices), 3):
            a, b, c = (np.array(m.vertices[i]) for i in m.indices[t:t + 3])
            n = np.cross(b - a, c - a)
            n /= np.linalg.norm(n)
            assert np.allclose(m.normals[m.indices[t]], n, atol=0.02)


def soup_of(tris):
    return bulk.soup_from_tris(tris)


def test_bulk_weld_closes_seams_and_drops_slivers():
    a = Tri((0, 0, 0), (1, 0, 0), (0, 1, 0), "T")
    b = Tri((1.01, 0.0, 0.0), (1, 1, 0), (0.0, 1.02, 0.0), "T")
    straddle = Tri((0.099, 5, 0), (1, 5, 0), (0, 6, 0), "T")
    straddle2 = Tri((0.101, 5, 0), (5, 5, 0), (5, 6, 0), "T")
    sliver = Tri((10, 0, 0), (10.01, 0, 0), (10, 1, 0), "T")
    s, merged, dropped = bulk.weld(soup_of([a, b, straddle, straddle2, sliver]), 0.05)
    assert dropped == 1
    assert merged == 4  # two seam corners, the straddling pair, the sliver's corner
    v = s.verts[s.tris]
    assert len({tuple(p) for p in v[0]} & {tuple(p) for p in v[1]}) == 2
    assert tuple(v[2][0]) == tuple(v[3][0])


def test_cache_round_trip_and_reuse(tmp_path, caplog):
    caplog.set_level("INFO")
    cache = tmp_path / "cache.npz"
    args = COMMON + ["--engine", "bulk", "--cache", str(cache)]
    cli.main(args + ["--out", str(tmp_path / "a" / "gta_ls")])
    assert cache.exists()
    caplog.clear()
    cli.main(args + ["--out", str(tmp_path / "b" / "gta_ls")])
    assert "using cache" in caplog.text and "reading" not in caplog.text
    assert tri_sets(tmp_path / "a" / "gta_ls" / "gta_ls_downtown.kn5") == tri_sets(tmp_path / "b" / "gta_ls" / "gta_ls_downtown.kn5")


def test_snow_and_ice_get_their_own_slippery_surfaces():
    table = load_table()
    keys = bulk.material_keys(["SNOW_COMPACT", "SNOW_DEEP", "ICE", "ICE_TARMAC", "TARMAC"], table)
    assert keys == ["SNOW", "SNOW", "ICE", "ICE", "ROAD"]
    from surfacegen.track import SURFACES
    assert SURFACES["ICE"]["FRICTION"] < SURFACES["SNOW"]["FRICTION"] < SURFACES["ROAD"]["FRICTION"]


def test_bulk_is_fast_on_a_million_triangles(tmp_path):
    n = 500  # 500 x 500 grid of quads = 500k quads = 1M triangles
    xs, ys = np.meshgrid(np.arange(n + 1, dtype=np.float32), np.arange(n + 1, dtype=np.float32))
    verts = np.stack([xs.ravel(), ys.ravel(), np.zeros(xs.size, np.float32)], 1)
    i = np.arange(n * (n + 1)).reshape(n, n + 1)[:, :n].ravel()
    tris = np.concatenate([np.stack([i, i + 1, i + n + 1], 1), np.stack([i + 1, i + n + 2, i + n + 1], 1)]).astype(np.uint32)
    s = bulk.Soup(verts, tris, np.zeros(len(tris), np.uint16), np.zeros(len(tris), np.uint32), ["TARMAC"], ["grid"])
    t0 = time.perf_counter()
    s, _, _ = bulk.weld(s, 0.05)
    keys = bulk.classify(s, load_table(), 0.35)
    meshes = bulk.build_meshes(s, keys)
    bulk.write_kn5(tmp_path / "big.kn5", meshes, {})
    assert time.perf_counter() - t0 < 60
    m, _ = read_kn5(tmp_path / "big.kn5")
    assert sum(len(x.indices) for x in m.values()) == 3 * len(tris)


def test_dedupe_drops_copies_of_the_same_triangle_in_any_order():
    t = Tri((0, 0, 0), (1, 0, 0), (0, 1, 0), "T")
    copy = Tri((1, 0, 0), (0, 1, 0), (0, 0, 0), "T")      # same corners, rotated
    flipped = Tri((0, 0, 0), (0, 1, 0), (1, 0, 0), "T")   # same corners, other winding: still the same surface
    other = Tri((5, 0, 0), (6, 0, 0), (5, 1, 0), "T")
    s, removed = bulk.dedupe(soup_of([t, copy, flipped, other]))
    assert removed == 2 and len(s.tris) == 2


def test_map_bounds_drop_far_away_and_seabed():
    keep = Tri((100, 100, 30), (101, 100, 30), (100, 101, 30), "T")
    yankton = Tri((3200, -4800, 100), (3201, -4800, 100), (3200, -4799, 100), "T")
    seabed = Tri((0, -3000, -200), (1, -3000, -200), (0, -2999, -200), "T")
    sky = Tri((0, 0, 2400), (1, 0, 2400), (0, 1, 2400), "T")
    s = soup_of([keep, yankton, seabed, sky])
    mask = bulk.in_bounds(s, bulk.GTA_MAP_BOUNDS, zmin=-20, zmax=1500)
    assert mask.tolist() == [True, False, False, False]


def test_water_is_left_out_of_the_track(tmp_path):
    from surfacegen.surfaces import build_meshes as py_build
    road = Tri((0, 0, 0), (1, 0, 0), (0, 1, 0), "TARMAC")
    water = Tri((5, 0, 0), (6, 0, 0), (5, 1, 0), "WATER")
    table = load_table()
    s = soup_of([road, water])
    keys = bulk.classify(s, table, 0.35)
    meshes = bulk.build_meshes(s, keys)
    assert set(meshes) == {"__verts__", "ROAD"}
    assert set(py_build([road, water], table, None)) == {"ROAD"}
