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


def test_building_materials_are_known_and_trees_are_walls(caplog):
    caplog.set_level(logging.WARNING)
    table = load_table()
    for m in ("DEFAULT", "METAL_GRILLE", "GLASS_BULLETPROOF", "WOOD_SOLID_SMALL", "PLASTER_SOLID", "CARPET_SOLID",
              "PLASTIC_HOLLOW", "ROOF_FELT", "LINOLEUM", "CERAMIC", "POLYSTYRENE", "TARPAULIN", "CLOTH", "PAPER",
              "FIBREGLASS", "SLATTED_BLINDS", "LAMINATE", "ROOF_TILE", "PUDDLE", "LEATHER", "EMISSIVE_PLASTIC", "VFX_METAL_ELECTRIFIED", "PHYS_ELECTRIC_METAL", "CAR_GLASS_BULLETPROOF",
              "WATER"):
        assert classify(tri(mat=m), table) == "ROAD", m
    assert classify(tri(mat="TREE_BARK"), table) == "WALL"
    assert "unknown" not in caplog.text


from surfacegen.surfaces import weld  # noqa: E402


def test_weld_snaps_nearby_vertices_across_pieces():
    a = Tri((0, 0, 0), (1, 0, 0), (0, 1, 0), "TARMAC")
    b = Tri((1.01, 0.0, 0.0), (1, 1, 0), (0.0, 1.02, 0.0), "TARMAC")  # touches a, 1-2 cm off
    out, merged, dropped = weld([a, b], 0.05)
    pts_a = {out[0].a, out[0].b, out[0].c}
    pts_b = {out[1].a, out[1].b, out[1].c}
    assert len(pts_a & pts_b) == 2
    assert merged == 2 and dropped == 0


def test_weld_keeps_vertices_further_apart():
    a = Tri((0, 0, 0), (1, 0, 0), (0, 1, 0), "TARMAC")
    b = Tri((1.10, 0, 0), (2, 0, 0), (1.1, 1, 0), "TARMAC")
    out, merged, _ = weld([a, b], 0.05)
    assert merged == 0 and out == [a, b]


def test_weld_works_across_grid_cell_boundaries():
    # 0.099 and 0.101 straddle a 0.1 m cell edge but are 2 mm apart
    a = Tri((0.099, 0, 0), (1, 0, 0), (0, 1, 0), "T")
    b = Tri((0.101, 0, 0), (5, 0, 0), (5, 5, 0), "T")
    out, merged, _ = weld([a, b], 0.05)
    assert merged == 1 and out[0].a == out[1].a


def test_weld_drops_collapsed_triangles():
    sliver = Tri((0, 0, 0), (0.01, 0, 0), (0, 1, 0), "T")  # two corners 1 cm apart collapse
    out, _, dropped = weld([sliver], 0.05)
    assert out == [] and dropped == 1


def test_weld_zero_tolerance_is_a_no_op():
    a = Tri((0, 0, 0), (0.001, 0, 0), (0, 1, 0), "T")
    assert weld([a], 0)[0] == [a]


def test_weld_is_fast_enough_for_a_city_block():
    import time
    tris = [Tri((i, j, 0), (i + 1, j, 0), (i, j + 1, 0), "T") for i in range(400) for j in range(400)]
    t0 = time.perf_counter()
    weld(tris, 0.05)
    assert time.perf_counter() - t0 < 15


def test_low_steep_faces_like_kerbs_stay_drivable():
    kerb = Tri((0, 0, 0), (1, 0, 0), (0, 0, 0.2), "CONCRETE")  # 20 cm vertical face
    wall = Tri((0, 0, 0), (1, 0, 0), (0, 0, 1.0), "CONCRETE")  # 1 m vertical face
    table = {"CONCRETE": "ROAD"}
    assert classify(kerb, table) == "ROAD"
    assert classify(wall, table) == "WALL"
    assert classify(kerb, table, step_height=0.1) == "WALL"


def test_trees_stay_walls_even_when_short_pieces():
    bark = Tri((0, 0, 0), (0.2, 0, 0), (0, 0, 0.2), "TREE_BARK")
    assert classify(bark, {"TREE_BARK": "WALL"}) == "WALL"


def test_unknown_materials_are_guessed_from_keywords(caplog):
    caplog.set_level(logging.WARNING)
    table = {"TARMAC": "ROAD"}
    assert classify(tri(mat="BUSH_THICK"), table) == "GRASS"
    assert classify(tri(mat="LEAVES_PILE"), table) == "GRASS"
    assert classify(tri(mat="SAND_DRY_DEEP"), table) == "SAND"
    assert classify(tri(mat="MUD_UNDERWATER"), table) == "GRAVEL"
    assert classify(tri(mat="ROCK_NOINST"), table) == "GRAVEL"
    assert classify(tri(mat="ANIMAL_DEFAULT"), table) == "ROAD"
    assert "BUSH_THICK" in caplog.text  # still reported, as a guess


def test_keyword_guess_uses_whole_words():
    from surfacegen.surfaces import guess_surface
    assert guess_surface("OFFICE_CARPET") == "ROAD"
    assert guess_surface("ICE_THIN") == "ICE"
    assert guess_surface("SNOW_LIGHT") == "SNOW"
