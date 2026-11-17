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
  nat::setVehicleEngineOn(v, FALSE);  // AC plays the engine sound
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
  nat::setEntityCoordsNoOffset(v, p.pos.x, p.pos.y, p.pos.z);
  nat::setEntityQuaternion(v, p.rot.x, p.rot.y, p.rot.z, p.rot.w);
  nat::setEntityVelocity(v, p.vel.x, p.vel.y, p.vel.z);
  nat::setEntityAngularVelocity(v, p.angVel.x, p.angVel.y, p.angVel.z);
}

void ShvGame::disableVehicleControls() {
  // accelerate, brake, steer, handbrake, left/right-only, up/down; exiting the car stays allowed (unlinks)
  for (int c : {71, 72, 59, 60, 61, 62, 63, 64, 76, 87, 88, 89, 90}) nat::disableControlAction(0, c);
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
