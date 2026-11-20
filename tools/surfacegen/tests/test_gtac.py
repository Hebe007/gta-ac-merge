"""The .gtac file written by tools/gtaextract (C#) must load into the same Soup the XML path makes."""
import shutil
import struct
import subprocess
from pathlib import Path

import numpy as np
import pytest

from surfacegen import bulk, cli
from surfacegen.kn5 import read_kn5

ROOT = Path(__file__).resolve().parents[3]
EXE = ROOT / "tools/gtaextract/bin/Release/net8.0/GtaExtract.dll"


def write_gtac(path, verts, tris, mats, files, names, sources):
    with open(path, "wb") as f:
        f.write(b"GTACOL1\0" + struct.pack("<5I", 1, len(verts), len(tris), len(names), len(sources)))
        f.write(np.asarray(verts, "<f4").tobytes() + np.asarray(tris, "<u4").tobytes())
        f.write(np.asarray(mats, "<u2").tobytes() + np.asarray(files, "<u4").tobytes())
        for s in list(names) + list(sources):
            b = s.encode()
            f.write(struct.pack("<H", len(b)) + b)


def test_load_gtac(tmp_path):
    p = tmp_path / "c.gtac"
    write_gtac(p, [(0, 0, 0), (1, 0, 0), (0, 1, 0)], [(0, 1, 2)], [0], [0], ["TARMAC"], ["obj:x"])
    s = bulk.load_gtac(p)
    assert s.verts.shape == (3, 3) and s.tris.tolist() == [[0, 1, 2]]
    assert s.names == ["TARMAC"] and s.files == ["obj:x"]


def test_rejects_other_files(tmp_path):
    p = tmp_path / "x.gtac"
    p.write_bytes(b"nope")
    with pytest.raises(SystemExit):
        bulk.load_gtac(p)


@pytest.mark.skipif(not (EXE.exists() and shutil.which("dotnet")), reason="GtaExtract not built")
def test_csharp_writer_round_trip(tmp_path):
    out = tmp_path / "selftest.gtac"
    subprocess.run(["dotnet", str(EXE), "--self-test", "--out", str(out)], check=True, capture_output=True)
    s = bulk.load_gtac(out)
    prop = s.tris[s.file == s.files.index("obj:test_prop")]
    corners = {tuple(np.round(s.verts[i].astype(float), 3)) for i in prop[0]}
    assert corners == {(100, 200, 30), (100, 202, 30), (98, 200, 30)}  # turned 90 deg, scaled x2, moved
    assert len(s.tris[s.file == s.files.index("box_0.ybn.xml")]) == 12


def test_cli_builds_a_track_from_gtac(tmp_path):
    p = tmp_path / "c.gtac"
    write_gtac(p, [(0, 0, 30), (10, 0, 30), (0, 10, 30), (100, 0, 30), (110, 0, 30), (100, 10, 30)],
               [(0, 1, 2), (3, 4, 5)], [0, 0], [0, 1], ["TARMAC"], ["dt1_rd1_0.ybn.xml", "obj:freeway_piece"])
    cli.main(["--collision", str(p), "--out", str(tmp_path / "gta_ls"), "--layout", "t", "--start", "2,2,31,0"])
    m, _ = read_kn5(tmp_path / "gta_ls" / "gta_ls_t.kn5")
    assert sum(len(x.indices) for x in m.values()) == 6
