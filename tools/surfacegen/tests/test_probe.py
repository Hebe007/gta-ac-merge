from pathlib import Path

from surfacegen import bulk, probe
from surfacegen.ybn_xml import Tri


def test_probe_reports_files_and_filters(tmp_path, capsys):
    near_origin_underpass = [Tri((-50, -50, 8), (60, -50, 8), (-50, 60, 12), "TARMAC")]  # looks like an interior
    road = [Tri((200, 200, 30), (260, 200, 30), (200, 260, 30), "TARMAC")]
    b = bulk._Builder()
    b.add("underpass_0.ybn.xml", near_origin_underpass)
    b.add("road_0.ybn.xml", road)
    cache = tmp_path / "c.npz"
    bulk.save_cache(cache, b.soup(), "sig")
    probe.main(["--cache", str(cache), "--at", "0,0"])
    out = capsys.readouterr().out
    assert "underpass_0.ybn.xml" in out and "LEFT OUT as interior" in out
    assert "road_0.ybn.xml" not in out
    probe.main(["--cache", str(cache), "--at", "220,210"])
    out = capsys.readouterr().out
    assert "road_0.ybn.xml" in out and "z 30.0" in out and "TARMAC" in out


def test_generator_probe_shows_placed_interiors(tmp_path, capsys):
    from pathlib import Path
    from surfacegen import cli
    fix = Path(__file__).parent / "fixtures"
    xml = tmp_path / "xml"
    xml.mkdir()
    (xml / "test_tunnel1.ybn.xml").write_text((fix / "small.ybn.xml").read_text())
    ymap = tmp_path / "ymap"
    ymap.mkdir()
    (ymap / "t.ymap.xml").write_text((fix / "tunnel.ymap.xml").read_text())
    cli.main(["--no-cache", "--xml-dir", str(xml), "--ymap-dir", str(ymap), "--out", str(tmp_path / "gta_ls"),
              "--layout", "t", "--start", "-2600,2950,20,0", "--probe", "-2600,2955"])
    out = capsys.readouterr().out
    assert "test_tunnel1@1" in out


def test_probe_file_probes_every_spot(tmp_path, capsys):
    from pathlib import Path
    from surfacegen import cli
    fix = Path(__file__).parent / "fixtures"
    holes = tmp_path / "gta_link_holes.txt"
    holes.write_text("111.0,201.0,1.0\n-50.0,-50.0,0.0\n")
    cli.main(["--keep-interiors", "--no-cache", "--xml-dir", str(fix), "--out", str(tmp_path / "gta_ls"), "--layout", "t",
              "--start", "105,202,6,0", "--probe-file", str(holes)])
    out = capsys.readouterr().out
    assert "at GTA 111.0, 201.0" in out and "at GTA -50.0, -50.0" in out


def test_probe_lists_surfaces_exactly_at_the_spot(capsys):
    b = bulk._Builder()
    b.add("ground_0.ybn.xml", [Tri((0, 0, 28), (20, 0, 28), (0, 20, 28), "CONCRETE")])
    b.add("bridge_0.ybn.xml", [Tri((0, 0, 42), (20, 0, 42), (0, 20, 42), "TARMAC")])
    probe.report(b.soup(), 5, 5, check_filters=False, z=36)
    out = capsys.readouterr().out
    assert "car was at z 36.0" in out
    assert out.index("z   42.00") < out.index("z   28.00")
    assert "the car fell from about here" in out
    probe.report(b.soup(), 50, 50, check_filters=False)
    assert "0 collision file(s)" in capsys.readouterr().out


def test_probe_radius_finds_neighbouring_pieces(capsys):
    b = bulk._Builder()
    b.add("here_0.ybn.xml", [Tri((0, 0, 28), (2, 0, 28), (0, 2, 28), "CONCRETE")])
    b.add("next_door@1", [Tri((20, 0, 28), (24, 0, 28), (20, 4, 28), "CONCRETE")])  # 20 m away
    probe.report(b.soup(), 1, 1, radius=8, check_filters=False)
    assert "next_door@1" not in capsys.readouterr().out
    probe.report(b.soup(), 1, 1, radius=30, check_filters=False)
    out = capsys.readouterr().out
    assert "next_door@1" in out and "closest" in out


def test_probe_says_what_ac_makes_of_each_surface_and_skips_far_files(capsys):
    b = bulk._Builder()
    b.add("deck_0.ybn.xml", [Tri((0, 0, 30), (20, 0, 30), (0, 20, 30), "TARMAC")])          # faces up
    b.add("under_0.ybn.xml", [Tri((0, 0, 25), (0, 20, 25), (20, 0, 25), "TARMAC")])         # faces down
    b.add("obj:far_prop", [Tri((0, 0, 0), (1, 0, 0), (0, 1, 0), "TARMAC"),
                            Tri((900, 900, 0), (901, 900, 0), (900, 901, 0), "TARMAC")])  # spans the map, far here
    s = b.soup()
    keys = bulk.classify(s, {"TARMAC": "ROAD"}, 0.4)
    probe.report(s, 5, 5, radius=3, check_filters=False, keys=keys)
    out = capsys.readouterr().out
    assert "drivable ROAD" in out and "faces DOWN" in out
    assert "obj:far_prop" not in out and "2 collision file(s)" in out
