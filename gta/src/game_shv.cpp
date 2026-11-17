#include "game_shv.hpp"
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
  nat::setVehicleEngineOn(v, FALSE);  // AC plays the engine sound
  if (!opt_.gtaDamage) {
    nat::setEntityInvincible(v, TRUE);
    nat::setEntityCanBeDamaged(v, FALSE);
    nat::setEntityProofsAll(v);
    nat::setVehicleCanBeVisiblyDamaged(v, FALSE);
    nat::setVehicleCanBreak(v, FALSE);
    nat::setVehicleTyresCanBurst(v, FALSE);
    nat::setVehicleWheelsCanBreak(v, FALSE);
    nat::setVehicleStrong(v, TRUE);
    nat::setDisableVehiclePetrolTankDamage(v, TRUE);
    nat::setVehicleEngineCanDegrade(v, FALSE);
  }
  if (opt_.frozen) nat::freezeEntityPosition(v, TRUE);
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
    Follow f = followVelocity(p.pos, {c.x, c.y, c.z}, p.vel, 10.f, 2.f);
    if (f.snap) nat::setEntityCoordsNoOffset(v, p.pos.x, p.pos.y, p.pos.z);
    nat::setEntityQuaternion(v, p.rot.x, p.rot.y, p.rot.z, p.rot.w);
    nat::setEntityVelocity(v, f.vel.x, f.vel.y, f.vel.z);
    nat::setEntityAngularVelocity(v, p.angVel.x, p.angVel.y, p.angVel.z);
  }
  if (!opt_.gtaDamage && ++repairCountdown_ >= 60) {  // undo anything that slipped through, about once a second
    repairCountdown_ = 0;
    nat::setVehicleFixed(v);
  }
}

void ShvGame::disableVehicleControls() {
  // accelerate, brake, steer, handbrake, left/right-only, up/down; exiting the car stays allowed (unlinks)
  // Steering (59, 63, 64) stays enabled: setSteer feeds AC's steering in as player input so GTA animates it.
  for (int c : {71, 72, 60, 61, 62, 76, 87, 88, 89, 90}) nat::disableControlAction(0, c);
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
