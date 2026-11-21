#include "race_shv.hpp"
#include <windows.h>
#include "main.h"
#include <cstdio>
#include <ctime>
#include "natives_min.hpp"

void ShvRaceWorld::saveResults(const std::string& t) {
  if (resultsFile.empty()) return;
  if (FILE* f = std::fopen(resultsFile.c_str(), "a")) {
    std::time_t now = std::time(nullptr);
    char when[32];
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M", std::localtime(&now));
    std::fprintf(f, "%s  %s\n", when, t.c_str());
    std::fclose(f);
  }
}

bool ShvRaceWorld::keyPressed(Key k) {
  bool down = (GetAsyncKeyState(keys[k]) & 0x8000) != 0;
  bool pressed = down && !wasDown_[k];
  wasDown_[k] = down;
  return pressed;
}

void ShvRaceWorld::blockPhone() {
  // phone, and its up/down/left/right/select/cancel (the arrow keys and Enter)
  for (int c : {27, 172, 173, 174, 175, 176, 177}) nat::disableControlAction(0, c);
}

int ShvRaceWorld::wanted() { return nat::getPlayerWantedLevel(nat::playerId()); }

void ShvRaceWorld::setWanted(int stars) {
  Player p = nat::playerId();
  if (stars <= 0) { nat::clearPlayerWantedLevel(p); return; }
  nat::setPlayerWantedLevel(p, stars);
  nat::setPlayerWantedLevelNow(p);
}

bool ShvRaceWorld::waypoint(Vec3& out) {
  if (!nat::isWaypointActive()) return false;
  Vector3 c = nat::getBlipInfoIdCoord(nat::getFirstBlipInfoId(8));  // 8 = waypoint
  out = {c.x, c.y, c.z};
  return true;
}

Vec3 ShvRaceWorld::playerPos() {
  Vector3 c = nat::getEntityCoords(nat::playerPedId());
  return {c.x, c.y, c.z};
}

float ShvRaceWorld::playerHeadingDeg() {
  Ped p = nat::playerPedId();
  Vehicle v = nat::getVehiclePedIsIn(p);
  return nat::getEntityHeading(v ? v : p);
}

bool ShvRaceWorld::groundZ(Vec3 a, float& z) { return nat::getGroundZFor3dCoord(a.x, a.y, a.z, &z); }

int ShvRaceWorld::spawnRacer(const std::string& model, Vec3 p, float heading) {
  Hash h = nat::getHashKey(model.c_str());
  if (!nat::isModelInCdimage(h) || !nat::isModelAVehicle(h)) return 0;
  nat::requestModel(h);
  for (int i = 0; i < 300 && !nat::hasModelLoaded(h); i++) scriptWait(0);
  if (!nat::hasModelLoaded(h)) return 0;
  Vehicle v = nat::createVehicle(h, p.x, p.y, p.z, heading);
  nat::setModelAsNoLongerNeeded(h);
  if (!v) return 0;
  nat::setEntityAsMissionEntity(v);
  nat::setVehicleOnGroundProperly(v);
  Ped d = nat::createRandomPedAsDriver(v);
  if (!d) { nat::deleteVehicle(&v); return 0; }
  nat::setEntityAsMissionEntity(d);
  nat::setBlockingOfNonTemporaryEvents(d, TRUE);  // races, doesn't flee or fight
  nat::setDriverAbility(d, 1.f);
  nat::setDriverAggressiveness(d, 1.f);
  nat::setDriverRacingModifier(d, 1.f);
  nat::setVehicleEngineOn(v, TRUE);
  nat::freezeEntityPosition(v, TRUE);  // until GO
  Blip b = nat::addBlipForEntity(v);
  nat::setBlipColour(b, 1);  // red
  blips_[v] = b;
  return v;
}

void ShvRaceWorld::startRacer(int car, Vec3 dest, float maxSpeed, int style) {
  nat::freezeEntityPosition(car, FALSE);
  Ped d = nat::getPedInVehicleSeat(car, -1);
  if (!d) return;
  nat::taskVehicleDriveToCoordLongrange(d, car, dest.x, dest.y, finishZKnown_ ? finishZ_ : dest.z, maxSpeed, style, 10.f);
  nat::setPedKeepTask(d, TRUE);
}

bool ShvRaceWorld::racerOk(int car) {
  if (!nat::doesEntityExist(car) || nat::isEntityDead(car)) return false;
  Ped d = nat::getPedInVehicleSeat(car, -1);
  return d && !nat::isEntityDead(d);
}

Vec3 ShvRaceWorld::racerPos(int car) {
  Vector3 c = nat::getEntityCoords(car);
  return {c.x, c.y, c.z};
}

void ShvRaceWorld::releaseRacer(int car, bool remove) {
  auto it = blips_.find(car);
  if (it != blips_.end()) { Blip b = it->second; nat::removeBlip(&b); blips_.erase(it); }
  if (!nat::doesEntityExist(car)) return;
  nat::freezeEntityPosition(car, FALSE);
  Ped d = nat::getPedInVehicleSeat(car, -1);
  Vehicle v = car;
  if (remove) {
    if (d) nat::deletePed(&d);
    nat::deleteVehicle(&v);
    return;
  }
  if (d) {
    nat::taskVehicleDriveWander(d, v, 20.f, 786603);  // drive off normally; GTA removes them later
    nat::setPedAsNoLongerNeeded(&d);
  }
  nat::setVehicleAsNoLongerNeeded(&v);
}

void ShvRaceWorld::finishPoint(Vec3 pos, bool show) {
  if (finishBlip_) { Blip b = finishBlip_; nat::removeBlip(&b); finishBlip_ = 0; }
  finishZKnown_ = false;
  if (!show) return;
  finishBlip_ = nat::addBlipForCoord(pos.x, pos.y, pos.z);
  nat::setBlipColour(finishBlip_, 5);  // yellow
  nat::setBlipRoute(finishBlip_, TRUE);
}

void ShvRaceWorld::drawFinish(Vec3 pos) {
  if (!finishZKnown_) {  // the waypoint has no height; the ground there loads once you get closer
    float z;
    if (nat::getGroundZFor3dCoord(pos.x, pos.y, 1000.f, &z)) { finishZ_ = z; finishZKnown_ = true; }
    else return;
  }
  nat::drawMarker(1, pos.x, pos.y, finishZ_ - 1.f, 24.f, 24.f, 6.f, 255, 210, 0, 110);  // yellow ring, 12 m radius
}

void ShvRaceWorld::text(float x, float y, const std::string& s, float scale) {
  nat::setTextFont(4);
  nat::setTextScale(0.0f, scale);
  nat::setTextColour(255, 255, 255, 255);
  nat::setTextOutline();
  nat::setTextCentre(TRUE);
  nat::beginTextCommandDisplayText("STRING");
  nat::addTextComponentSubstringPlayerName(s.c_str());
  nat::endTextCommandDisplayText(x, y);
}
