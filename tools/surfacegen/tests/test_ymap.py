from pathlib import Path

import numpy as np
import pytest

from surfacegen import bulk
from surfacegen.ymap_xml import read_mlo_instances
from surfacegen.ybn_xml import Tri

FIX = Path(__file__).parent / "fixtures"


def test_reads_interior_placements_only():
    inst = read_mlo_instances(FIX / "tunnel.ymap.xml")
    assert len(inst) == 1
    name, pos, rot = inst[0]
    assert name == "test_tunnel1"
    assert pos == pytest.approx((-2600, 2950, 15))
    assert rot == pytest.approx((0, 0, 0.70710678, 0.70710678))


def test_interiors_are_placed_where_the_map_puts_them():
    b = bulk._Builder()
    b.add("test_tunnel1.ybn.xml", [Tri((0, 0, 0), (10, 0, 0), (0, 5, 0), "TARMAC")])  # local coordinates
    b.add("road_0.ybn.xml", [Tri((0, 0, 30), (1, 0, 30), (0, 1, 30), "TARMAC")])
    s = b.soup()
    s2, placed, missing = bulk.place_interiors(s, [("test_tunnel1", (-2600, 2950, 15), (0, 0, 0.70710678, 0.70710678)),
                                                   ("nowhere", (0, 0, 0), (0, 0, 0, 1))])
    assert placed == 1 and missing == ["nowhere"]
    new = s2.tris[s2.file == s2.files.index("test_tunnel1@1")]
    v = s2.verts[new[0]]
    # +90 degrees about Z: local (10,0,0) -> (0,10,0), then moved to the instance position
    assert v[1] == pytest.approx((-2600, 2960, 15), abs=1e-3)
    assert v[2] == pytest.approx((-2605, 2950, 15), abs=1e-3)
    assert s2.files[s2.file[-1]] == "test_tunnel1@1"
    assert len(s2.tris) == 3
    # placed copies are not mistaken for interiors any more
    bad = bulk.interior_files(s2)
    assert s2.files.index("test_tunnel1@1") not in bad and s2.files.index("test_tunnel1.ybn.xml") in bad


def test_negative_coordinates_are_accepted():
    from surfacegen.cli import fix_negative_values
    assert fix_negative_values(["--at", "-2595,3041", "--x"]) == ["--at=-2595,3041", "--x"]
    assert fix_negative_values(["--start", "1,2,3,4"]) == ["--start", "1,2,3,4"]


def test_inverse_rotation_option(tmp_path, capsys):
    from surfacegen import cli
    xml = tmp_path / "xml"
    xml.mkdir()
    (xml / "test_tunnel1.ybn.xml").write_text((FIX / "small.ybn.xml").read_text())
    ymap = tmp_path / "ymap"
    ymap.mkdir()
    (ymap / "t.ymap.xml").write_text((FIX / "tunnel.ymap.xml").read_text())
    # the fixture's road triangle (x 110..114, y 200..204 locally) lands at x -2600-(200..204), y 2950+(110..114)
    # rotated +90 deg about Z, and at x -2600+(200..204), y 2950-(110..114) rotated the other way
    for rot, spot in (("as-is", "-2801,3061"), ("inverse", "-2399,2839")):
        cli.main(["--no-cache", "--xml-dir", str(xml), "--ymap-dir", str(ymap), "--out", str(tmp_path / rot), "--layout", "t",
                  "--start", "-2600,2950,20,0", "--interior-rotation", rot, "--probe", spot])
        out = capsys.readouterr().out
        assert "surfaces exactly at this spot" in out, rot
