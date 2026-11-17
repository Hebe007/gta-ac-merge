-- Memory layouts shared with the GTA .asi. Must match common/link_protocol.h field for field
-- (checked by tools/tests/test_layout_match.py). Loaded by csp/gta_link/script.lua.
local M = {}

M.STATE_NAME = 'GtaAcLink_State_v1'
M.HITS_NAME = 'GtaAcLink_Hits_v1'
M.MAGIC = 0x47544143
M.VERSION = 1

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

return M
