#include "race_shv.hpp"
#include <windows.h>
#include "main.h"
#include <cmath>
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

void ShvRaceWorld::driveRacer(int car, Vec3 dest, float maxSpeed, int style) {
  nat::freezeEntityPosition(car, FALSE);
  Ped d = nat::getPedInVehicleSeat(car, -1);
  if (!d) return;
  // stop range 0: never slows down for the checkpoint itself; the next one is given before it gets there
  nat::taskVehicleDriveToCoordLongrange(d, car, dest.x, dest.y, dest.z, maxSpeed, style, 0.f);
  nat::setPedKeepTask(d, TRUE);
}

void ShvRaceWorld::setRacerPower(int car, float mult) { nat::setVehicleCheatPowerIncrease(car, mult); }

void ShvRaceWorld::moveRacer(int car, Vec3 p, float heading) {
  nat::setEntityCoords(car, p.x, p.y, p.z);
  nat::setEntityHeading(car, heading);
  nat::setVehicleOnGroundProperly(car);
  nat::setVehicleForwardSpeed(car, 8.f);
}

float ShvRaceWorld::racerSpeed(int car) { return nat::getEntitySpeed(car); }

float ShvRaceWorld::approxHeight(float x, float y) { return nat::getApproxHeightForPoint(x, y); }

bool ShvRaceWorld::roadNode(Vec3 n, Vec3& out, float& heading) {
  Vector3 v{};
  float h = 0;
  if (!nat::getClosestVehicleNodeWithHeading(n.x, n.y, n.z, &v, &h)) return false;
  out = {v.x, v.y, v.z};
  heading = h;
  return true;
}

std::vector<Route> ShvRaceWorld::loadRoutes() {
  std::string text;
  if (FILE* f = std::fopen(routesFile.c_str(), "rb")) {
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    std::fclose(f);
  }
  return routesFromText(text);
}

void ShvRaceWorld::saveRoutes(const std::vector<Route>& routes) {
  if (FILE* f = std::fopen(routesFile.c_str(), "wb")) {
    std::string t = routesToText(routes);
    std::fwrite(t.data(), 1, t.size(), f);
    std::fclose(f);
  }
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

void ShvRaceWorld::target(Vec3 pos, bool show) {
  if (finishBlip_) { Blip b = finishBlip_; nat::removeBlip(&b); finishBlip_ = 0; }
  if (!show) return;
  finishBlip_ = nat::addBlipForCoord(pos.x, pos.y, pos.z);
  nat::setBlipColour(finishBlip_, 5);  // yellow
  nat::setBlipRoute(finishBlip_, TRUE);
}

void ShvRaceWorld::drawCheckpoint(Vec3 pos, bool finish) {
  float z = pos.z, gz;
  if (nat::getGroundZFor3dCoord(pos.x, pos.y, pos.z + 3.f, &gz) && std::fabs(gz - pos.z) < 6.f) z = gz;
  if (finish) nat::drawMarker(1, pos.x, pos.y, z - 1.f, 24.f, 24.f, 8.f, 255, 255, 255, 120);  // white: the finish
  else nat::drawMarker(1, pos.x, pos.y, z - 1.f, 40.f, 40.f, 4.f, 255, 210, 0, 90);            // yellow: checkpoint
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
