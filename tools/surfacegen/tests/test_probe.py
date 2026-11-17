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
