-- GTA Link: publishes the player's car state into shared memory for the GTA V .asi to read.
-- Layout and names: link_layout.lua (must match common/link_protocol.h).
local L = require('link_layout')

local ITEMS, K = L.items(L.LINK_STATE)
local ok, mem = pcall(ac.writeMemoryMappedFile, L.STATE_NAME, ITEMS)
local status = ok and 'publishing' or ('shared memory failed: ' .. tostring(mem))
if not ok then mem = nil end

-- Seqlock writer: round down to even first (a writer that died mid-write leaves seq odd).
local seq = mem and (mem[K.seq] - mem[K.seq] % 2) or 0
local resets, lastPos, updates = 0, nil, 0
local TELEPORT_METERS = 20

local function set3(dst, v) dst[0], dst[1], dst[2] = v.x, v.y, v.z end

-- Height of car.position above the tyre contact patches, along the car's up (0 if CSP doesn't expose them).
local function rideHeight(car)
  local ok, h = pcall(function()
    local sum, n = 0, 0
    for i = 0, 3 do
      local c = car.wheels[i].contactPoint
      if c then
        local d = car.position - c
        sum = sum + d.x * car.up.x + d.y * car.up.y + d.z * car.up.z
        n = n + 1
      end
    end
    return n > 0 and sum / n or 0
  end)
  return ok and h or 0
end

function script.update(dt)
  if not mem then return end
  local car = ac.getCar(0)
  if not car then return end

  local p = car.position
  if lastPos and p:distance(lastPos) > TELEPORT_METERS then resets = resets + 1 end  -- reset / restart / teleport
  lastPos = p:clone()

  mem[K.seq] = seq + 1  -- odd: writing
  mem[K.magic] = L.MAGIC
  mem[K.version] = L.VERSION
  mem[K.resetCounter] = resets
  set3(mem[K.pos], p)
  set3(mem[K.fwd], car.look)
  set3(mem[K.up], car.up)
  set3(mem[K.vel], car.velocity)
  mem[K.angVel][0], mem[K.angVel][1], mem[K.angVel][2] = 0, 0, 0  -- GTA side derives it from successive orientations
  for i = 0, 3 do
    local w = car.wheels and car.wheels[i]
    mem[K.wheelSpin][i] = w and w.angularSpeed or 0
  end
  mem[K.steerDeg] = car.steer or 0
  mem[K.rpm] = car.rpm or 0
  mem[K.speedKmh] = car.speedKmh or 0
  mem[K.massKg] = car.mass or 0
  mem[K.gear] = car.gear or 0
  mem[K.rideHeight] = rideHeight(car)
  seq = seq + 2
  mem[K.seq] = seq  -- even: done
  updates = updates + 1
end

function script.windowMain(dt)
  ui.text('GTA Link ' .. status)
  ui.text(string.format('updates: %d   resets: %d', updates, resets))
  if lastPos then ui.text(string.format('pos  %.1f  %.1f  %.1f', lastPos.x, lastPos.y, lastPos.z)) end
end
