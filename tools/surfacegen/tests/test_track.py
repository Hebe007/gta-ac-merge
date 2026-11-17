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
