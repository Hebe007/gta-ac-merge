import pytest
import configparser
import json
from pathlib import Path

from surfacegen import cli
from surfacegen.kn5 import read_kn5
from surfacegen.surfaces import Mesh
from surfacegen.track import write_track

FIX = Path(__file__).parent / "fixtures"


def meshes():
    sq = Mesh([(0, 0, 0), (1, 0, 0), (0, 0, -1)], [0, 2, 1])
    return {"ROAD": sq, "GRASS": sq}


def test_track_folder_layout(tmp_path):
    out = tmp_path / "gta_ls"
    write_track(out, "downtown", meshes(), start_gta=(1, 2, 3, 0))
    assert (out / "gta_ls_downtown.kn5").exists()
    models = configparser.ConfigParser()
    models.read(out / "models_downtown.ini")
    assert models["MODEL_0"]["FILE"] == "gta_ls_downtown.kn5"
    ui = json.loads((out / "ui" / "downtown" / "ui_track.json").read_text())
    assert ui["pitboxes"] == "1"


def test_surfaces_ini_has_one_section_per_used_key(tmp_path):
    out = tmp_path / "gta_ls"
    write_track(out, "downtown", meshes(), start_gta=(1, 2, 3, 0))
    s = configparser.ConfigParser()
    s.optionxform = str
    s.read(out / "downtown" / "data" / "surfaces.ini")
    keys = sorted(s[sec]["KEY"] for sec in s.sections())
    assert keys == ["GRASS", "ROAD"]
    assert s.sections() == ["SURFACE_0", "SURFACE_1"]
    road = next(s[sec] for sec in s.sections() if s[sec]["KEY"] == "ROAD")
    assert road["IS_VALID_TRACK"] == "1"


def test_cli_end_to_end(tmp_path):
    out = tmp_path / "gta_ls"
    cli.main(["--xml-dir", str(FIX), "--materials-dat", str(FIX / "materials.dat"), "--out", str(out),
              "--layout", "downtown", "--start", "105,202,6,0"])
    m, dummies = read_kn5(out / "gta_ls_downtown.kn5")
    assert any(name.startswith("1ROAD") for name in m)
    assert "AC_START_0" in dummies


def test_cli_prefers_normal_mover_collision_over_hi(tmp_path):
    from surfacegen.cli import pick_files
    names = ["dt1_01_0.ybn.xml", "hi@dt1_01_0.ybn.xml", "hi@dt1_02_0.ybn.xml", "dt1_03_0.ybn.xml", "notes.txt"]
    for n in names:
        (tmp_path / n).write_text("<BoundsFile/>")
    # normal file wins; hi@ only where no normal file exists
    assert [p.name for p in pick_files(tmp_path)] == ["dt1_01_0.ybn.xml", "dt1_03_0.ybn.xml", "hi@dt1_02_0.ybn.xml"]
    assert [p.name for p in pick_files(tmp_path, prefer="hi")] == ["dt1_03_0.ybn.xml", "hi@dt1_01_0.ybn.xml", "hi@dt1_02_0.ybn.xml"]


def test_cli_prints_material_summary(tmp_path, caplog):
    caplog.set_level("INFO")
    cli.main(["--xml-dir", str(FIX), "--materials-dat", str(FIX / "materials.dat"), "--out", str(tmp_path / "gta_ls"),
              "--layout", "downtown", "--start", "105,202,6,0"])
    assert "TARMAC -> ROAD" in caplog.text
    assert "GRASS -> GRASS" in caplog.text


def test_ground_under_start():
    from surfacegen.cli import ground_under
    from surfacegen.ybn_xml import Tri
    floor = Tri((0, 0, 10), (10, 0, 10), (0, 10, 12), "TARMAC")
    roof = Tri((0, 0, 50), (10, 0, 50), (0, 10, 50), "TARMAC")
    assert ground_under([floor, roof], 2, 2, below=30) == pytest.approx(10.4)
    assert ground_under([floor], 20, 20, below=30) is None


def test_coverage_report():
    from surfacegen.cli import coverage
    from surfacegen.ybn_xml import Tri
    tris = [Tri((0, 0, 1), (10, 0, 1), (0, 10, 1), "T"), Tri((100, 50, 5), (110, 50, 5), (100, 60, 5), "T")]
    (x0, y0, z0, x1, y1, z1), nearest = coverage(tris, 103, 52)
    assert (x0, y0, x1, y1) == (0, 0, 110, 60)
    assert nearest == pytest.approx(0, abs=4)


def test_files_covering_point():
    from surfacegen.cli import files_covering
    from surfacegen.ybn_xml import Tri
    per_file = {"a.xml": [Tri((0, 0, 0), (10, 0, 0), (0, 10, 0), "T")], "b.xml": [Tri((50, 50, 0), (60, 50, 0), (50, 60, 0), "T")]}
    assert files_covering(per_file, 2, 2) == [("a.xml", 1)]


def test_probe_lists_nearest_triangles():
    from surfacegen.cli import probe
    from surfacegen.ybn_xml import Tri
    per_file = {"a.xml": [Tri((0, 0, 1), (2, 0, 1), (0, 2, 1), "TARMAC"), Tri((90, 90, 0), (91, 90, 0), (90, 91, 0), "GRASS")]}
    lines = probe(per_file, 1, 1, count=1)
    assert len(lines) == 1 and "a.xml" in lines[0] and "TARMAC" in lines[0]


def test_cli_welds_by_default_and_reports(tmp_path, caplog):
    caplog.set_level("INFO")
    cli.main(["--xml-dir", str(FIX), "--out", str(tmp_path / "gta_ls"), "--layout", "downtown", "--start", "105,202,6,0"])
    assert "welded" in caplog.text
    caplog.clear()
    cli.main(["--xml-dir", str(FIX), "--out", str(tmp_path / "gta_ls2"), "--layout", "downtown", "--start", "105,202,6,0",
              "--weld", "0"])
    assert "welded" not in caplog.text


def test_cli_wall_surface_option_renames_wall_meshes(tmp_path):
    out = tmp_path / "gta_ls"
    cli.main(["--xml-dir", str(FIX), "--out", str(out), "--layout", "downtown", "--start", "105,202,6,0",
              "--wall-surface", "ROAD"])
    m, _ = read_kn5(out / "gta_ls_downtown.kn5")
    assert not any(n.startswith("1WALL") for n in m)
    assert any(n.startswith("1ROAD_WALL") for n in m)


def test_cli_flip_walls_option(tmp_path):
    out = tmp_path / "gta_ls"
    cli.main(["--xml-dir", str(FIX), "--out", str(out), "--layout", "downtown", "--start", "105,202,6,0", "--flip-walls"])
    assert (out / "gta_ls_downtown.kn5").exists()


def test_walls_are_not_listed_as_a_drivable_surface(tmp_path):
    # AC treats every surfaces.ini entry as ground for the wheels; walls must stay out to be solid barriers
    out = tmp_path / "gta_ls"
    sq = Mesh([(0, 0, 0), (1, 0, 0), (0, 0, -1)], [0, 2, 1])
    write_track(out, "downtown", {"ROAD": sq, "WALL": sq}, start_gta=(1, 2, 3, 0))
    s = configparser.ConfigParser()
    s.read(out / "downtown" / "data" / "surfaces.ini")
    assert [s[sec]["KEY"] for sec in s.sections()] == ["ROAD"]
    m, _ = read_kn5(out / "gta_ls_downtown.kn5")
    assert any(n.startswith("1WALL") for n in m)


def _touch(p: Path):
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text("<BoundsFile/>")


def test_source_folders_newest_wins(tmp_path):
    from surfacegen.cli import pick_sources
    for f in ["base/dt1_01_0.ybn.xml", "base/dt1_02_0.ybn.xml", "base/hi@dt1_03_0.ybn.xml",
              "patchday2ng/dt1_01_0.ybn.xml", "patchday27ng/dt1_01_0.ybn.xml", "patchday3ng/dt1_02_0.ybn.xml",
              "mpheist/hei_dt1_x_0.ybn.xml", "update/dt1_02_0.ybn.xml", "mods/dt1_04_0.ybn.xml",
              "patchday27ng/dt1_03_0.ybn.xml"]:
        _touch(tmp_path / f)
    got = {p.name: p.parent.name for p in pick_sources(tmp_path)}
    assert got == {"dt1_01_0.ybn.xml": "patchday27ng",  # newest patch beats older patches and base
                   "dt1_02_0.ybn.xml": "update",        # update.rpf beats patches
                   "dt1_03_0.ybn.xml": "patchday27ng",  # a normal file anywhere beats hi@
                   "hei_dt1_x_0.ybn.xml": "mpheist",
                   "dt1_04_0.ybn.xml": "mods"}


def test_source_order():
    from surfacegen.cli import source_rank
    names = ["update", "patchday27ng", "base", "mods", "patchday2ng", "mpheist", "patchday10ng"]
    assert sorted(names, key=source_rank) == ["base", "mpheist", "patchday2ng", "patchday10ng", "patchday27ng",
                                              "update", "mods"]


def test_flat_folder_still_works(tmp_path):
    from surfacegen.cli import pick_sources
    _touch(tmp_path / "a_0.ybn.xml")
    _touch(tmp_path / "hi@a_0.ybn.xml")
    assert [p.name for p in pick_sources(tmp_path)] == ["a_0.ybn.xml"]
