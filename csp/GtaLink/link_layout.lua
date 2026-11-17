-- Memory layouts shared with the GTA .asi. Must match common/link_protocol.h field for field
-- (checked by tools/tests/test_layout_match.py). Loaded by GtaLink.lua.
local M = {}

M.STATE_NAME = 'AcTools.CSP.GtaAcLink.State.v1'
M.HITS_NAME = 'AcTools.CSP.GtaAcLink.Hits.v1'
M.MAGIC = 0x47544143
M.VERSION = 1

-- Writers: round seq down to even before writing (seq = seq - seq % 2), then seq+1 (odd, writing),
-- data, seq+2 (even, done). A writer that died mid-write leaves seq odd; the next one must not inherit it.

M.LINK_STATE = [[
  uint32_t magic;
  uint32_t version;
  uint32_t seq;
  uint32_t resetCounter;
  float pos[3];
  float fwd[3];
  float up[3];
  float vel[3];
  float angVel[3];
  float wheelSpin[4];
  float steerDeg;
  float rpm;
  float speedKmh;
  float massKg;
  int32_t gear;
]]

M.HIT_ITEM = [[
  float point[3];
  float impulse[3];
]]

M.HIT_BLOCK = [[
  uint32_t seq;
  uint32_t count;
  HitItem items[16];
]]

-- Builds CSP StructItems from a layout string. CSP orders struct fields by the order StructItems are created,
-- so walking the string top to bottom reproduces the C layout.
function M.items(layout)
  local t = {}
  for typ, name, n in layout:gmatch('([%w_]+)%s+([%w_]+)%[?(%d*)%]?;') do
    local item
    if typ == 'float' then item = ac.StructItem.float()
    elseif typ == 'int32_t' then item = ac.StructItem.int32()
    elseif typ == 'uint32_t' then item = ac.StructItem.uint32()
    else error('link_layout: unsupported type ' .. typ) end
    if n ~= '' then item = ac.StructItem.array(item, tonumber(n)) end
    t[name] = item
  end
  return t
end

return M
