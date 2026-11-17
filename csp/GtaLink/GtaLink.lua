-- GTA Link: publishes the player's car state into shared memory for the GTA V .asi to read.
-- Layout and names: link_layout.lua (must match common/link_protocol.h).
local L = require('link_layout')

local PLACES = require('places')
local ITEMS, K = L.items(L.LINK_STATE)
local CMD_ITEMS, C = L.items(L.LINK_CMD)
local okCmd, cmd = pcall(ac.writeMemoryMappedFile, L.CMD_NAME, CMD_ITEMS)
if not okCmd then cmd = nil end
local ok, mem = pcall(ac.writeMemoryMappedFile, L.STATE_NAME, ITEMS)
local status = ok and 'publishing' or ('shared memory failed: ' .. tostring(mem))
if not ok then mem = nil end

-- Seqlock writer: round down to even first (a writer that died mid-write leaves seq odd).
local seq = mem and (mem[K.seq] - mem[K.seq] % 2) or 0
local resets, lastPos, updates = 0, nil, 0
-- Teleports: GTA asks through the command block (F8 in a GTA car); the window's place buttons use the same path.
local lastTeleportId = cmd and cmd[C.teleportId] or 0  -- don't act on a request left from before AC started
local teleportStatus, teleportMsg = 0, ''

local function teleport(pos, fwd)
  local ok, err = pcall(physics.setCarPosition, 0, pos, fwd)
  teleportMsg = ok and 'moved' or ('teleport refused: ' .. tostring(err))
  return ok
end

-- GTA coordinates (x, y, z, heading) -> AC position and forward vector
local function fromGta(x, y, z, heading)
  local h = math.rad(heading or 0)
  return vec3(x, z + 1, -y), vec3(-math.sin(h), 0, -math.cos(h))
end

local function handleCommand()
  if not cmd or cmd[C.magic] ~= L.MAGIC or cmd[C.seq] % 2 == 1 then return end
  local id = cmd[C.teleportId]
  if id == lastTeleportId then return end
  local p, f = cmd[C.pos], cmd[C.fwd]
  local pos, fwd = vec3(p[0], p[1], p[2]), vec3(f[0], f[1], f[2])
  lastTeleportId = id
  teleportStatus = teleport(pos, fwd) and 1 or -1
end
local TELEPORT_METERS = 20

local function set3(dst, v) dst[0], dst[1], dst[2] = v.x, v.y, v.z end

-- Height of car.position above the tyre contact patches, along the car's up (0 if CSP exposes neither way).
local rideSource = 'none'
local function along(car, v) return v.x * car.up.x + v.y * car.up.y + v.z * car.up.z end
local function rideHeight(car)
  local ok, h = pcall(function()  -- tyre contact points
    local sum, n = 0, 0
    for i = 0, 3 do
      local c = car.wheels[i].contactPoint
      if c then sum, n = sum + along(car, car.position - c), n + 1 end
    end
    if n > 0 then rideSource = 'contact'; return sum / n end
    return 0
  end)
  if ok and h > 0.05 then return h end
  ok, h = pcall(function()  -- wheel centres plus tyre radius
    local sum, n = 0, 0
    for i = 0, 3 do
      local w = car.wheels[i]
      if w.position and w.tyreRadius then sum, n = sum + along(car, car.position - w.position) + w.tyreRadius, n + 1 end
    end
    if n > 0 then rideSource = 'wheels'; return sum / n end
    return 0
  end)
  if ok and h > 0.05 then return h end
  rideSource = 'none'
  return 0
end

function script.update(dt)
  if not mem then return end
  handleCommand()
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
  local okLock, lock = pcall(function() return car.steerLock end)
  mem[K.steerLockDeg] = (okLock and type(lock) == 'number') and lock or 0
  mem[K.gas] = car.gas or 0
  mem[K.brake] = car.brake or 0
  mem[K.teleportAck] = lastTeleportId
  mem[K.teleportStatus] = teleportStatus
  seq = seq + 2
  mem[K.seq] = seq  -- even: done
  updates = updates + 1
end

function script.windowMain(dt)
  ui.text('GTA Link ' .. status)
  ui.text(string.format('updates: %d   resets: %d', updates, resets))
  if lastPos then ui.text(string.format('pos  %.1f  %.1f  %.1f', lastPos.x, lastPos.y, lastPos.z)) end
  ui.text(string.format('ride height %.2f m (%s)', mem and mem[K.rideHeight] or 0, rideSource))
  if teleportMsg ~= '' then ui.text('last teleport: ' .. teleportMsg) end
  ui.separator()
  ui.text('Teleport to:')
  for _, place in ipairs(PLACES) do
    if ui.button(place.name) then teleport(fromGta(place.x, place.y, place.z, place.heading)) end
  end
end
