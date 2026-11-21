#include "game_shv.hpp"
#include <cstdio>
#include <cmath>
#include "natives_min.hpp"

bool ShvGame::playerDeadOrArrested() {
  return nat::isEntityDead(nat::playerPedId()) || nat::isPlayerBeingArrested(nat::playerId());
}

int ShvGame::playerVehicle() { return nat::getVehiclePedIsIn(nat::playerPedId()); }

int ShvGame::spawnVehicle(const std::string& model, Vec3 p, float heading) {
  Hash h = nat::getHashKey(model.c_str());
  if (!nat::isModelInCdimage(h) || !nat::isModelAVehicle(h)) return 0;
  nat::requestModel(h);
  for (int i = 0; i < 300 && !nat::hasModelLoaded(h); i++) scriptWait(0);  // up to ~5 s
  if (!nat::hasModelLoaded(h)) return 0;
  Vehicle v = nat::createVehicle(h, p.x, p.y, p.z, heading);
  nat::setModelAsNoLongerNeeded(h);
  if (!v) return 0;
  nat::setEntityAsMissionEntity(v);
  nat::setVehicleOnGroundProperly(v);
  prepare(v);
  return v;
}

void ShvGame::putPlayerIn(int v) { nat::setPedIntoVehicle(nat::playerPedId(), v); }
bool ShvGame::exists(int v) { return nat::doesEntityExist(v); }

void ShvGame::deleteVehicle(int v) {
  Vehicle h = v;
  nat::setEntityAsMissionEntity(h);
  nat::deleteVehicle(&h);
}

Vec3 ShvGame::position(int v) {
  Vector3 c = nat::getEntityCoords(v);
  return {c.x, c.y, c.z};
}

void ShvGame::setPose(int v, const Pose& p) {
  if (opt_.frozen) {
    nat::setEntityCoordsNoOffset(v, p.pos.x, p.pos.y, p.pos.z);
    nat::setEntityQuaternion(v, p.rot.x, p.rot.y, p.rot.z, p.rot.w);
  } else {
    // Drive it there instead of teleporting, so GTA's wheels roll and its suspension works.
    Vector3 c = nat::getEntityCoords(v);
    float ex = p.pos.x - c.x, ey = p.pos.y - c.y, ez = p.pos.z - c.z;
    // Touching something (barrier, pot, wall) and pushed away from where AC has the car: GTA's physics and AC's
    // disagree, and following by velocity makes the car shake against it. Hold it frozen in AC's place instead.
    if (!dynamicHit_ && nat::hasEntityCollidedWithAnything(v) && ex * ex + ey * ey + ez * ez > 0.3f * 0.3f)
      blockedFrames_ = 30;
    if (blockedFrames_ > 0) {
      blockedFrames_--;
      if (!heldFrozen_) { nat::freezeEntityPosition(v, TRUE); heldFrozen_ = true; }
      nat::setEntityCoordsNoOffset(v, p.pos.x, p.pos.y, p.pos.z);
      nat::setEntityQuaternion(v, p.rot.x, p.rot.y, p.rot.z, p.rot.w);
      haveCmd_ = false;  // frozen: GTA's velocity says nothing about hits
      return;
    }
    if (heldFrozen_) { nat::freezeEntityPosition(v, FALSE); heldFrozen_ = false; }
    Follow f = followVelocity(p.pos, {c.x, c.y, c.z}, p.vel, 10.f, 2.f);
    if (f.snap) nat::setEntityCoordsNoOffset(v, p.pos.x, p.pos.y, p.pos.z);
    nat::setEntityQuaternion(v, p.rot.x, p.rot.y, p.rot.z, p.rot.w);
    // Forward speed first: this native also spins the wheels; then the exact velocity on top.
    Vec3 fwd = rotate(p.rot, {0, 1, 0});
    nat::setVehicleForwardSpeed(v, p.vel.x * fwd.x + p.vel.y * fwd.y + p.vel.z * fwd.z);
    nat::setEntityVelocity(v, f.vel.x, f.vel.y, f.vel.z);
    cmdVel_ = f.vel;
    haveCmd_ = true;
    nat::setEntityAngularVelocity(v, p.angVel.x, p.angVel.y, p.angVel.z);
  }
  if (!opt_.gtaDamage && ++repairCountdown_ >= 60 && keepTyres_ == 0) {  // undo anything that slipped through, about once a second
    repairCountdown_ = 0;
    nat::setVehicleFixed(v);
  }
}

void ShvGame::disableVehicleControls() {
  // accelerate, brake, steer, handbrake, left/right-only, up/down; exiting the car stays allowed (unlinks)
  // Steering (59, 63, 64) and pedals (71, 72) stay enabled: setSteer/setPedals feed AC's inputs in as player
  // input so GTA animates steering, revs and brake lights. Handbrake, flying and exit-related moves stay off.
  for (int c : {60, 61, 62, 76, 87, 88, 89, 90}) nat::disableControlAction(0, c);
}

bool ShvGame::groundZ(Vec3 a, float& z) { return nat::getGroundZFor3dCoord(a.x, a.y, a.z, &z); }

void ShvGame::drawText(float x, float y, const std::string& text) {
  nat::setTextFont(4);
  nat::setTextScale(0.0f, 0.45f);
  nat::setTextColour(255, 255, 255, 255);
  nat::setTextOutline();
  nat::setTextCentre(TRUE);
  nat::beginTextCommandDisplayText("STRING");
  nat::addTextComponentSubstringPlayerName(text.c_str());
  nat::endTextCommandDisplayText(x, y);
}

bool ShvGame::linkKeyPressed() {
  bool down = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
  bool pressed = down && !keyWasDown_;
  keyWasDown_ = down;
  return pressed;
}

float ShvGame::originHeight(int v) {
  // The bottom of the model's bounding box is the bottom of the tyres; doesn't depend on the ground being loaded.
  Vector3 mn{}, mx{};
  nat::getModelDimensions(nat::getEntityModel(v), &mn, &mx);
  if (mn.z < -0.05f) return -mn.z;
  return nat::getEntityHeightAboveGround(v);
}

void ShvGame::setSteer(int v, float bias) {
  nat::setVehicleSteerBias(v, bias);
  nat::setControlValueNextFrame(0, 59, steerInputValue(bias));  // VehicleMoveLeftRight
}

void ShvGame::setPedals(int, float gas, float brake) {
  nat::setControlValueNextFrame(0, 71, std::fmax(0.f, std::fmin(1.f, gas)));    // VehicleAccelerate
  nat::setControlValueNextFrame(0, 72, std::fmax(0.f, std::fmin(1.f, brake)));  // VehicleBrake
}

Quat ShvGame::rotation(int v) {
  float x = 0, y = 0, z = 0, w = 1;
  nat::getEntityQuaternion(v, &x, &y, &z, &w);
  return {x, y, z, w};
}

void ShvGame::prepare(int v) {
  nat::setEntityAsMissionEntity(v);
  if (opt_.engineAudio == "off") {
    nat::setVehicleEngineOn(v, FALSE);  // AC plays the engine sound
  } else {
    nat::forceUseAudioGameObject(v, opt_.engineAudio.c_str());  // an electric car: near silent
    nat::setVehicleEngineOn(v, TRUE);  // engine running: dashboard and speedometer work
  }
  if (!opt_.gtaDamage) {
    nat::setEntityInvincible(v, TRUE);
    nat::setEntityCanBeDamaged(v, FALSE);
    nat::setEntityProofsAll(v);
    nat::setVehicleCanBeVisiblyDamaged(v, FALSE);
    nat::setVehicleCanBreak(v, FALSE);
    nat::setVehicleTyresCanBurst(v, opt_.tyresBurst ? TRUE : FALSE);
    nat::setVehicleWheelsCanBreak(v, FALSE);
    nat::setVehicleStrong(v, TRUE);
    nat::setDisableVehiclePetrolTankDamage(v, TRUE);
    nat::setVehicleEngineCanDegrade(v, FALSE);
  }
  if (opt_.frozen) nat::freezeEntityPosition(v, TRUE);
}

void ShvGame::release(int v) {
  // Give your car back as a normal GTA car (its engine sound stays the electric one until it's respawned).
  nat::freezeEntityPosition(v, FALSE);
  heldFrozen_ = false;
  blockedFrames_ = 0;
  keepTyres_ = 0;
  haveCmd_ = false;
  nat::setEntityInvincible(v, FALSE);
  nat::setEntityCanBeDamaged(v, TRUE);
  nat::setVehicleCanBeVisiblyDamaged(v, TRUE);
  nat::setVehicleCanBreak(v, TRUE);
  nat::setVehicleTyresCanBurst(v, TRUE);
  nat::setVehicleWheelsCanBreak(v, TRUE);
  nat::setVehicleStrong(v, FALSE);
  nat::setVehicleEngineOn(v, TRUE);
}

bool ShvGame::diagKeyPressed() {
  bool down = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
  bool pressed = down && !diagWasDown_;
  diagWasDown_ = down;
  return pressed;
}

std::string ShvGame::diagnostics(int v) {
  char b[200];
  std::snprintf(b, sizeof b, "GTA speed %.0f km/h  engine %s  |  frozen %d  engine_audio %s  gta_damage %d",
                nat::getEntitySpeed(v) * 3.6f, nat::getIsVehicleEngineRunning(v) ? "running" : "OFF", opt_.frozen ? 1 : 0,
                opt_.engineAudio.c_str(), opt_.gtaDamage ? 1 : 0);
  std::string path = opt_.iniPath;  // GTA's text drops everything after a backslash: show / instead
  for (char& c : path) if (c == '\\') c = '/';
  if (path.size() > 60) path = "..." + path.substr(path.size() - 57);  // GTA text pieces are limited in length
  return std::string(b) + "~n~settings from: " + path + (opt_.iniFound ? "  (found)" : "  (FILE NOT FOUND - using defaults)");
}

void ShvGame::logHole(Vec3 p) {
  if (holesFile.empty()) return;
  if (FILE* f = std::fopen(holesFile.c_str(), "a")) {
    std::fprintf(f, "%.1f,%.1f,%.1f\n", p.x, p.y, p.z);
    std::fclose(f);
  }
}

// GTA tyre indices (car): 0 front left, 1 front right, 4 rear left, 5 rear right; AC bits 1, 2, 4, 8.
static const int kGtaTyre[4] = {0, 1, 4, 5};

uint32_t ShvGame::burstTyres(int v) {
  uint32_t m = 0;
  for (int i = 0; i < 4; i++)
    if (nat::isVehicleTyreBurst(v, kGtaTyre[i], FALSE)) m |= 1u << i;
  return m;
}

void ShvGame::syncTyres(int v, uint32_t keep) {
  keepTyres_ = keep;
  for (int i = 0; i < 4; i++)
    if (!(keep & (1u << i)) && nat::isVehicleTyreBurst(v, kGtaTyre[i], FALSE)) nat::setVehicleTyreFixed(v, kGtaTyre[i]);
}

// GTA has no native for a vehicle's mass: a typical one per vehicle class (GET_VEHICLE_CLASS).
static float vehicleClassMass(int cls) {
  static const float kg[] = {1200, 1500, 2200, 1500, 1600, 1300, 1400, 1350, 250, 2000, 9000, 4000,
                             2500, 90, 2000, 4000, 8000, 6000, 2000, 10000, 9000, 50000, 750};
  return cls >= 0 && cls < static_cast<int>(sizeof kg / sizeof kg[0]) ? kg[cls] : 1500.f;
}

bool ShvGame::takeHit(int v, Hit& out) {
  dynamicHit_ = false;
  if (opt_.frozen || !haveCmd_ || !nat::hasEntityCollidedWithAnything(v)) return false;
  // What did we touch? GTA's "last entity hit" first; if that isn't a car or person, a car touching us.
  Entity other = nat::getLastEntityHitByEntity(v);
  bool dynamic = other && other != v && nat::doesEntityExist(other) &&
                 (nat::isEntityAVehicle(other) || nat::isEntityAPed(other));
  if (!dynamic) {
    Vector3 c = nat::getEntityCoords(v);
    Vehicle closest = nat::getClosestVehicle(c.x, c.y, c.z, 8.f, 0, 70);
    other = closest;
    dynamic = closest && closest != v && nat::isEntityTouchingEntity(v, closest);
  }
  if (!dynamic) return false;
  dynamicHit_ = true;
  Vector3 va = nat::getEntityVelocity(v), vo = nat::getEntityVelocity(other);
  Vector3 c = nat::getEntityCoords(v), o = nat::getEntityCoords(other);
  out.vCommanded = cmdVel_;
  out.vAfter = {va.x, va.y, va.z};
  out.vOther = {vo.x, vo.y, vo.z};
  out.point = {(c.x + o.x) / 2, (c.y + o.y) / 2, (c.z + o.z) / 2};
  out.away = {c.x - o.x, c.y - o.y, c.z - o.z};
  // A car's tyres and brakes resist being shoved, so in a short hit it acts heavier than it is.
  out.otherMass = nat::isEntityAPed(other) ? 120.f : 1.5f * vehicleClassMass(nat::getVehicleClass(other));
  return true;
}

void ShvGame::logStuck(const std::string& line) {
  if (stuckFile.empty()) return;
  if (FILE* f = std::fopen(stuckFile.c_str(), "a")) {
    std::fprintf(f, "%s\n", line.c_str());
    std::fclose(f);
  }
}

bool ShvGame::resetKeyPressed() {
  bool down = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
  bool pressed = down && !resetWasDown_;
  resetWasDown_ = down;
  return pressed;
}

Vec3 ShvGame::forward(int v) {
  Vector3 f = nat::getEntityForwardVector(v);
  return {f.x, f.y, f.z};
}
