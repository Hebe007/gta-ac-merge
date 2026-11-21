// RaceWorld on ScriptHookV natives: keys, wanted level, AI opponents, finish blip and marker.
#pragma once
#include <map>
#include "race.hpp"

class ShvRaceWorld : public RaceWorld {
 public:
  // Windows key codes for kPolice, kRace, kUp, kDown, kLeft, kRight, kEnter (key_police, key_race, key_menu_*)
  int keys[7] = {0x79, 0x7A, 0x26, 0x28, 0x25, 0x27, 0x0D};
  std::string resultsFile;  // race results are appended here (next to the .asi)
  void saveResults(const std::string& text) override;
  bool keyPressed(Key k) override;
  void blockPhone() override;
  int wanted() override;
  void setWanted(int stars) override;
  bool waypoint(Vec3& out) override;
  Vec3 playerPos() override;
  float playerHeadingDeg() override;
  bool groundZ(Vec3 above, float& z) override;
  int spawnRacer(const std::string& model, Vec3 pos, float headingDeg) override;
  void driveRacer(int car, Vec3 dest, float maxSpeed, int drivingStyle) override;
  void setRacerPower(int car, float mult) override;
  void moveRacer(int car, Vec3 pos, float headingDeg) override;
  float racerSpeed(int car) override;
  float approxHeight(float x, float y) override;
  bool roadNode(Vec3 near, Vec3& out, float& headingDeg) override;
  std::vector<Route> loadRoutes() override;
  void saveRoutes(const std::vector<Route>& routes) override;
  std::string routesFile;  // gta_race_routes.txt next to the .asi
  bool racerOk(int car) override;
  Vec3 racerPos(int car) override;
  void releaseRacer(int car, bool remove) override;
  void target(Vec3 pos, bool show) override;
  void drawCheckpoint(Vec3 pos, bool finish) override;
  void text(float x, float y, const std::string& s, float scale) override;

 private:
  bool wasDown_[7] = {};
  std::map<int, int> blips_;  // racer car -> its blip
  int finishBlip_ = 0;

};
