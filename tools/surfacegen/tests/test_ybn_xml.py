from pathlib import Path

import pytest

from surfacegen.ybn_xml import Tri, read_materials_dat, read_ybn_xml

FIX = Path(__file__).parent / "fixtures"


@pytest.fixture
def names():
    return read_materials_dat(FIX / "materials.dat")


def test_materials_dat_names_in_order(names):
    assert names == ["DEFAULT", "TARMAC", "GRASS", "CONCRETE"]


def test_triangle_and_box_counts(names):
    tris = read_ybn_xml(FIX / "small.ybn.xml", names)
    assert len(tris) == 2 + 12


def test_vertices_absolute_with_center_and_composite_transform(names):
    t = read_ybn_xml(FIX / "small.ybn.xml", names)[0]
    # vertex + GeometryCenter (10,0,5) + composite translation (100,200,0)
    assert t.a == pytest.approx((110, 200, 5))
    assert t.b == pytest.approx((114, 200, 5))
    assert t.c == pytest.approx((110, 204, 5))


def test_material_names_by_polygon_index(names):
    tris = read_ybn_xml(FIX / "small.ybn.xml", names)
    assert tris[0].material == "TARMAC"
    assert tris[1].material == "GRASS"
    assert tris[2].material == "CONCRETE"


def test_material_index_without_names():
    assert read_ybn_xml(FIX / "small.ybn.xml")[0].material == "#1"


def test_box_covers_unit_cube_with_outward_faces(names):
    box = read_ybn_xml(FIX / "small.ybn.xml", names)[2:]
    corners = {tuple(round(c) for c in p) for t in box for p in (t.a, t.b, t.c)}
    assert corners == {(x, y, z) for x in (0, 1) for y in (0, 1) for z in (0, 1)}
    for t in box:  # every face normal points away from the cube centre
        n = _cross(_sub(t.b, t.a), _sub(t.c, t.a))
        mid = [(t.a[i] + t.b[i] + t.c[i]) / 3 - 0.5 for i in range(3)]
        assert sum(n[i] * mid[i] for i in range(3)) > 0


def test_unsupported_shapes_skipped_and_logged(names, caplog):
    caplog.set_level("INFO")
    read_ybn_xml(FIX / "small.ybn.xml", names)
    assert "skipped 1 sphere" in caplog.text.lower()


def _sub(a, b):
    return tuple(a[i] - b[i] for i in range(3))


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def test_materials_dat_given_as_folder(tmp_path):
    folder = tmp_path / "materials.dat"  # CodeWalker's raw export can create a folder with this name
    folder.mkdir()
    (folder / "materials.dat").write_text((FIX / "materials.dat").read_text())
    assert read_materials_dat(folder)[:2] == ["DEFAULT", "TARMAC"]


def test_space_separated_numbers_like_real_codewalker_export(names):
    a = read_ybn_xml(FIX / "small.ybn.xml", names)
    b = read_ybn_xml(FIX / "spaces.ybn.xml", names)
    assert b == a


def test_single_line_matrix_and_mixed_separators(tmp_path, names):
    src = (FIX / "small.ybn.xml").read_text()
    start, end = src.index("<CompositeTransform>"), src.index("</CompositeTransform>")
    src = src[:start] + "<CompositeTransform>1 0 0 0 0 1 0 0 0 0 1 0 100 200 0 1" + src[end:]
    src = src.replace("4, 0, 0\n", "4 0 0\n")
    (tmp_path / "x.ybn.xml").write_text(src)
    assert read_ybn_xml(tmp_path / "x.ybn.xml", names) == read_ybn_xml(FIX / "small.ybn.xml", names)
