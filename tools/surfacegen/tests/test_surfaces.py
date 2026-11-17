import logging
import math

import pytest

from surfacegen.surfaces import AC_KEYS, Mesh, build_meshes, classify, load_table
from surfacegen.ybn_xml import Tri

TABLE = {"TARMAC": "ROAD", "GRASS": "GRASS", "RUMBLE_STRIPS": "KERB", "SAND_*": "SAND"}
FLAT = ((0, 0, 0), (1, 0, 0), (0, 1, 0))


def tri(pts=FLAT, mat="TARMAC"):
    return Tri(*pts, material=mat)


def test_steep_is_wall_whatever_the_material():
    vertical = ((0, 0, 0), (1, 0, 0), (0, 0, 1))
    assert classify(tri(vertical, "TARMAC"), TABLE) == "WALL"
    # 61 degrees from flat is a wall, 59 is not
    for deg, want in ((61, "WALL"), (59, "ROAD")):
        r = math.radians(deg)
        pts = ((0, 0, 0), (1, 0, 0), (0, math.cos(r), math.sin(r)))
        assert classify(tri(pts), TABLE) == want


def test_material_mapping_and_prefix_rules():
    assert classify(tri(mat="TARMAC"), TABLE) == "ROAD"
    assert classify(tri(mat="GRASS"), TABLE) == "GRASS"
    assert classify(tri(mat="RUMBLE_STRIPS"), TABLE) == "KERB"
    assert classify(tri(mat="SAND_LOOSE"), TABLE) == "SAND"


def test_unknown_material_is_road_with_warning(caplog):
    caplog.set_level(logging.WARNING)
    assert classify(tri(mat="MYSTERY_GOO"), TABLE) == "ROAD"
    assert "MYSTERY_GOO" in caplog.text


def test_vertices_converted_to_ac_space_and_face_up():
    meshes = build_meshes([tri(((0, 0, 0), (0, 1, 0), (1, 0, 0)))], TABLE, None)  # GTA normal points down
    m = meshes["ROAD"]
    assert set(m.vertices) == {(0, 0, 0), (0, 0, -1), (1, 0, 0)}  # AC = (x, z, -y)
    a, b, c = (m.vertices[i] for i in m.indices[:3])
    n_y = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2])
    assert n_y > 0


def test_bbox_drops_triangles_outside_by_centroid():
    inside = tri(((0, 0, 0), (1, 0, 0), (0, 1, 0)))
    outside = tri(((50, 50, 0), (51, 50, 0), (50, 51, 0)))
    meshes = build_meshes([inside, outside], TABLE, (-10, -10, 10, 10))
    assert len(meshes["ROAD"].indices) == 3


def test_vertices_deduplicated_per_mesh():
    quad = [tri(((0, 0, 0), (1, 0, 0), (0, 1, 0))), tri(((1, 0, 0), (1, 1, 0), (0, 1, 0)))]
    m = build_meshes(quad, TABLE, None)["ROAD"]
    assert len(m.vertices) == 4 and len(m.indices) == 6


def test_shipped_table_loads_and_covers_keys():
    table = load_table()
    assert table["TARMAC"] == "ROAD"
    assert set(table.values()) <= set(AC_KEYS)


def test_shipped_table_uses_gta_names_from_materials_dat():
    table = load_table()
    assert classify(tri(mat="RUMBLE_STRIP"), table) == "KERB"
    assert classify(tri(mat="TARMAC_POTHOLE"), table) == "ROAD"
    assert classify(tri(mat="BREEZE_BLOCK"), table) == "ROAD"
