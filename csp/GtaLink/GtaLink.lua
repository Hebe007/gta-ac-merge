-- GTA Link: publishes the player's car state into shared memory for the GTA V .asi to read.
-- Layout and names: link_layout.lua (must match common/link_protocol.h).
local L = require('link_layout')

local PLACES = require('places')
local ITEMS, K = L.items(L.LINK_STATE)
local CMD_ITEMS, C = L.items(L.LINK_CMD)
local HIT_ITEMS, H = L.items(L.HIT_BLOCK)
local okHits, hits = pcall(ac.writeMemoryMappedFile, L.HITS_NAME, HIT_ITEMS)
if not okHits then hits = nil end
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

local simTime, quietUntil = 0, 0  -- quietUntil: ignore jumps in speed we caused ourselves (teleport, repair)
local keepVel = nil  -- velocity to give back after a reset in place (F9): setCarPosition stops the car

local function teleport(pos, fwd)
  local car = ac.getCar(0)
  keepVel = nil
  if car and car.position:distance(pos) < 20 then  -- a reset where the car is, not a move to another car
    local f = fwd:clone():normalize()
    local v = car.velocity
    local along = v.x * car.look.x + v.y * car.look.y + v.z * car.look.z  -- negative when reversing
    keepVel = vec3(f.x * along, f.y * along, f.z * along)
  end
  -- setCarPosition's direction is the car's BACK (CSP's own debug app passes -cameraLook): send it reversed
  local ok, err = pcall(physics.setCarPosition, 0, pos, -fwd)
  quietUntil = simTime + 0.5
  teleportMsg = ok and 'moved' or ('teleport refused: ' .. tostring(err))
  return ok
end

-- GTA coordinates (x, y, z, heading) -> AC position and forward vector
local function fromGta(x, y, z, heading)
  local h = math.rad(heading or 0)
  return vec3(x, z + 1, -y), vec3(-math.sin(h), 0, -math.cos(h))
end

local lastBlowId = cmd and cmd[C.blowId] or 0  -- likewise for tyre requests
local tyreMsg = ''

local function handleCommand()
  if not cmd or cmd[C.magic] ~= L.MAGIC or cmd[C.seq] % 2 == 1 then return end
  local blowId = cmd[C.blowId]
  if blowId ~= lastBlowId then  -- GTA burst some tyres (spike strip, shots): blow the same ones here
    lastBlowId = blowId
    local mask = cmd[C.blowMask]
    if mask ~= 0 then
      local ok, err = pcall(physics.blowTyres, 0, mask)
      tyreMsg = ok and string.format('GTA burst tyres (mask %d) - "Repair now" fixes them', mask)
                   or ('blowing tyres refused: ' .. tostring(err))
    end
  end
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

-- Crash damage that AC's damage slider doesn't cover makes the car pull to one side after a hard crash or landing.
-- resetCarState repairs it, so repair shortly after every hard hit (fuel and speed kept), and note what had changed
-- (wheel angles, tyres, body) to learn what the damage was.
local autoRepair, repairMsg, repairs, lastRepair = true, '', 0, -10
local prevVel, impactAt, impactG = nil, nil, 0
local base = nil
local WHEEL = { [0] = 'FL', 'FR', 'RL', 'RR' }
local IMPACT_G = 5  -- a change of velocity this hard (in g, within one frame) counts as a crash or hard landing

local function snapshot(car)
  local ok, s = pcall(function()
    local t = { body = 0 }
    for i = 0, 3 do
      local w = car.wheels[i]
      t[i] = { toe = w.toeIn or 0, camber = w.camber or 0, blown = w.isBlown, susp = w.suspensionDamage or 0,
               flat = w.tyreFlatSpot or 0, radius = w.tyreLoadedRadius or 0 }
    end
    if car.damage then for i = 0, 3 do t.body = t.body + (car.damage[i] or 0) end end
    return t
  end)
  return ok and s or nil
end

local function changes(car)
  local now = snapshot(car)
  if not now or not base then return '' end
  local out = {}
  for i = 0, 3 do
    local a, b = base[i], now[i]
    if b.blown and not a.blown then out[#out + 1] = WHEEL[i] .. ' blown' end
    if b.susp - a.susp > 0.001 then out[#out + 1] = string.format('%s susp %.2f', WHEEL[i], b.susp) end
    if i >= 2 and math.abs(b.toe - a.toe) > 0.2 then out[#out + 1] = string.format('%s toe %+.1f', WHEEL[i], b.toe - a.toe) end
    if math.abs(b.camber - a.camber) > 0.5 then out[#out + 1] = string.format('%s camber %+.1f', WHEEL[i], b.camber - a.camber) end
    if b.flat - a.flat > 0.05 then out[#out + 1] = string.format('%s flat spot %.2f', WHEEL[i], b.flat) end
  end
  if now.body - base.body > 1 then out[#out + 1] = string.format('body %.0f', now.body) end
  return #out > 0 and table.concat(out, ', ') or 'nothing measurable changed'
end

-- A twisted wheel (AC's wheel joints after a hard hit) stays twisted through resetCarState; re-placing the car (what F9
-- does) puts the wheels back. Repair = both, right where the car is, keeping its speed and fuel.
local CAMBER_OFF, TOE_OFF = 12, 5  -- degrees away from the straight car that count as twisted
-- Kerbs and drifts lean the wheels too, but only for a moment and never steadily: a twisted wheel stays at the same
-- angle. So it must stay within STEADY degrees of where it was for HOLD seconds.
local STEADY, HOLD = 2, 1.0
local bentAngle = nil
local bentSince = nil

local function blownMask(car)
  local m = 0
  for i = 0, 3 do
    local ok, b = pcall(function() return car.wheels[i].isBlown end)
    if ok and b then m = m + 2 ^ i end
  end
  return m
end

local function repair(car, why)
  local fuel = car.fuel
  local changed = changes(car)
  local blown = why ~= 'by hand' and blownMask(car) or 0  -- automatic repairs leave blown tyres blown
  local ok, err = pcall(physics.resetCarState, 0, 1)
  if ok and fuel then pcall(physics.setCarFuel, 0, fuel) end
  if ok and blown ~= 0 then pcall(physics.blowTyres, 0, blown) end
  local f = vec3(car.look.x, 0, car.look.z)
  if f:length() < 1e-3 then f = vec3(0, 0, 1) end
  teleport(car.position:clone(), f:normalize())  -- in place: keeps the speed (see keepVel)
  repairs = repairs + 1
  lastRepair = simTime
  base = nil  -- measure the repaired car again
  bentSince = nil
  repairMsg = ok and string.format('repaired (%s): %s', why, changed) or ('repair refused: ' .. tostring(err))
end

local function twisted(car)
  if not base then return nil end
  local ok, why, which, angle = pcall(function()
    for i = 0, 3 do
      local w = car.wheels[i]
      local dc = (w.camber or 0) - base[i].camber
      if math.abs(dc) > CAMBER_OFF then return string.format('%s camber %+.0f', WHEEL[i], dc), i * 2, dc end
      if i >= 2 then
        local dt = (w.toeIn or 0) - base[i].toe
        if math.abs(dt) > TOE_OFF then return string.format('%s toe %+.0f', WHEEL[i], dt), i * 2 + 1, dt end
      end
    end
  end)
  if not ok then return nil end
  return why, which, angle
end

local function onGround(car)
  local ok, n = pcall(function()
    local n = 0
    for i = 0, 3 do if (car.wheels[i].load or 0) > 50 then n = n + 1 end end
    return n
  end)
  return ok and n >= 3
end

local function watchDamage(car, dt)
  simTime = simTime + dt
  if not base and simTime - lastRepair > 0.5 and onGround(car) then base = snapshot(car) end
  local v = car.velocity
  if prevVel and dt > 0 then
    local g = (v - prevVel):length() / dt / 9.81
    if g > IMPACT_G and simTime > quietUntil then impactAt, impactG = simTime, math.max(g, impactAt and impactG or 0) end
  end
  prevVel = v:clone()
  -- a wheel twisted steadily for HOLD s with the car on the ground: repair (in the air it would snap the car down)
  local why, which, angle
  if autoRepair and simTime - lastRepair > 1 then why, which, angle = twisted(car) end
  if not why or not onGround(car) then bentSince = nil; return end
  if not bentSince or bentAngle.which ~= which or math.abs(angle - bentAngle.angle) > STEADY then
    bentSince, bentAngle = simTime, { which = which, angle = angle }  -- (re)start: not steady yet
  end
  if simTime - bentSince > HOLD then
    local hit = impactAt and simTime - impactAt < 10 and string.format(', hit %.0f g', impactG) or ''
    repair(car, why .. hit)
    impactAt = nil
  end
end

-- GTA traffic and pedestrians hitting the GTA car: the GTA .asi measures how much each hit changed the car's
-- velocity and sends it as an impulse (hits block); here it becomes the same change of the AC car's velocity.
local hitsSeen = hits and hits[H.count] or 0  -- don't replay hits from before AC started
local hitsApplied, hitMsg = 0, ''

local function applyHits(car)
  if not hits then return end
  local seq = hits[H.seq]
  if seq % 2 == 1 then return end  -- GTA is writing
  local count = hits[H.count]
  if count < hitsSeen then hitsSeen = count end  -- GTA restarted
  if count - hitsSeen > L.HIT_RING then hitsSeen = count - L.HIT_RING end
  if count == hitsSeen then return end
  local mass = (car.mass and car.mass > 100) and car.mass or 1300
  local dx, dy, dz = 0, 0, 0
  for i = hitsSeen, count - 1 do
    local s = (i % L.HIT_RING) * 3
    dx = dx + hits[H.impulse][s] / mass
    dy = dy + hits[H.impulse][s + 1] / mass
    dz = dz + hits[H.impulse][s + 2] / mass
  end
  if hits[H.seq] ~= seq then return end  -- changed while reading: next frame
  hitsApplied = hitsApplied + (count - hitsSeen)
  hitsSeen = count
  local v = car.velocity
  pcall(physics.setCarVelocity, 0, vec3(v.x + dx, v.y + dy, v.z + dz))
  quietUntil = simTime + 0.3  -- our own change of speed is not a crash for the wheel watch
  hitMsg = string.format('last GTA hit: %.1f m/s', math.sqrt(dx * dx + dy * dy + dz * dz))
end

function script.update(dt)
  if not mem then return end
  if keepVel then  -- the frame after a reset in place or a repair: give the car its speed back
    pcall(physics.setCarVelocity, 0, keepVel)
    keepVel = nil
    quietUntil = simTime + 0.5  -- that jump in speed is not a crash
  end
  handleCommand()
  local car = ac.getCar(0)
  if not car then return end
  watchDamage(car, dt)
  applyHits(car)

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
  mem[K.tyresBlown] = blownMask(car)
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
  if ui.checkbox('Repair the car automatically after hard crashes and landings', autoRepair) then
    autoRepair = not autoRepair
  end
  if ui.button('Repair now') then local car = ac.getCar(0); if car then repair(car, 'by hand') end end
  ui.text(string.format('repairs: %d   %s', repairs, repairMsg))
  ui.text(string.format('GTA hits: %d   %s%s', hitsApplied, hitMsg, hits and '' or '   (hits memory failed)'))
  if tyreMsg ~= '' then ui.text(tyreMsg) end
  ui.separator()
  ui.text('Teleport to:')
  for _, place in ipairs(PLACES) do
    if ui.button(place.name) then teleport(fromGta(place.x, place.y, place.z, place.heading)) end
  end
end
