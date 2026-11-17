"""Runs csp/GtaLink/link_layout.lua under LuaJIT with a fake CSP `ac.StructItem` and checks the resulting
memory order matches common/link_protocol.h, whether CSP orders fields by name (what CSP 2.11 does) or by
creation order."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

FAKE_AC = r"""
local counter = 0
local function mk(t) counter = counter + 1; return {kind = t, order = counter} end
ac = {StructItem = {
  float = function() return mk('float') end,
  int32 = function() return mk('int32_t') end,
  uint32 = function() return mk('uint32_t') end,
  array = function(item, n) item.n = n; return item end,
}}
package.path = ARGV_DIR .. '/?.lua;' .. package.path
local L = require('link_layout')
local items, keys = L.items(L.LINK_STATE)
local names = {}
for k in pairs(items) do names[#names + 1] = k end
if MODE == 'alpha' then table.sort(names) else table.sort(names, function(a, b) return items[a].order < items[b].order end) end
local inv = {}
for field, key in pairs(keys) do inv[key] = field end
for _, k in ipairs(names) do
  local it = items[k]
  io.write(it.kind, ' ', inv[k], it.n and ('[' .. it.n .. ']') or '', '\n')
end
"""


def c_fields() -> list[str]:
    h = (ROOT / "common/link_protocol.h").read_text()
    body = re.search(r"struct\s+LinkState\s*\{(.*?)\n\};", h, re.S).group(1)
    body = re.sub(r"//.*", "", body)
    return [" ".join(f.split()) for f in body.split(";") if f.strip()]


@pytest.mark.skipif(not shutil.which("luajit"), reason="luajit not installed")
@pytest.mark.parametrize("mode", ["alpha", "creation"])
def test_memory_order_matches_c_header(mode, tmp_path):
    script = tmp_path / "run.lua"
    script.write_text(f"ARGV_DIR = [[{ROOT / 'csp/GtaLink'}]]\nMODE = '{mode}'\n" + FAKE_AC)
    out = subprocess.run(["luajit", str(script)], capture_output=True, text=True, check=True).stdout
    assert out.strip().splitlines() == c_fields()
