"""The CSP Lua layout string must match common/link_protocol.h field for field."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def c_struct_fields(src: str, name: str) -> list[str]:
    body = re.search(r"struct\s+%s\s*\{(.*?)\n\};" % name, src, re.S).group(1)
    body = re.sub(r"//.*", "", body)
    return [" ".join(f.split()) for f in body.split(";") if f.strip()]


def lua_layout(src: str, name: str) -> list[str]:
    body = re.search(r"%s\s*=\s*\[\[(.*?)\]\]" % name, src, re.S).group(1)
    return [" ".join(f.split()) for f in body.split(";") if f.strip()]


def test_link_state_layout_matches():
    h = (ROOT / "common/link_protocol.h").read_text()
    lua = (ROOT / "csp/GtaLink/link_layout.lua").read_text()
    assert lua_layout(lua, "LINK_STATE") == c_struct_fields(h, "LinkState")


def test_command_layout_matches():
    h = (ROOT / "common/link_protocol.h").read_text()
    lua = (ROOT / "csp/GtaLink/link_layout.lua").read_text()
    assert lua_layout(lua, "LINK_CMD") == c_struct_fields(h, "LinkCommand")


def test_hit_block_layout_matches():
    h = (ROOT / "common/link_protocol.h").read_text()
    lua = (ROOT / "csp/GtaLink/link_layout.lua").read_text()
    assert lua_layout(lua, "HIT_ITEM") == c_struct_fields(h, "HitItem")
    assert lua_layout(lua, "HIT_BLOCK") == c_struct_fields(h, "HitBlock")


def test_names_and_constants_match():
    h = (ROOT / "common/link_protocol.h").read_text()
    lua = (ROOT / "csp/GtaLink/link_layout.lua").read_text()
    c = lambda name: re.search(r"%s\s*=\s*([^;]+);" % name, h).group(1).strip().strip('"')
    l = lambda name: re.search(r"M\.%s\s*=\s*([^\n]+)" % name, lua).group(1).strip().strip("'")
    assert c("LINK_STATE_NAME") == l("STATE_NAME")
    assert c("LINK_HITS_NAME") == l("HITS_NAME")
    assert int(c("LINK_MAGIC").split()[0], 16) == int(l("MAGIC"), 16)
    assert int(c("LINK_VERSION")) == int(l("VERSION"))
