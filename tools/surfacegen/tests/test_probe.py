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
