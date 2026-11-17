#pragma once
#include <string>
#include "game.hpp"

struct ShvOptions {
  bool gtaDamage = false;  // let GTA damage the car (off: AC is in charge; GTA-only objects can't wreck it)
  bool frozen = true;      // GTA physics never moves the car: no flips or pushes, but wheels may not turn
  // Engine sound of this GTA car model, so the engine can run (dashboard, speedometer) almost silently.
  // "off" = engine off as before (no dashboard).
  std::string engineAudio = "voltic";
};

class ShvGame : public Game {
 public:
  explicit ShvGame(ShvOptions o = {}) : opt_(o) {}
  bool playerDeadOrArrested() override;
  int playerVehicle() override;
  int spawnVehicle(const std::string& model, Vec3 pos, float headingDeg) override;
  void putPlayerIn(int vehicle) override;
  bool exists(int vehicle) override;
  void deleteVehicle(int vehicle) override;
  Vec3 position(int vehicle) override;
  void setPose(int vehicle, const Pose& pose) override;
  void disableVehicleControls() override;
  bool groundZ(Vec3 above, float& z) override;
  void drawText(float x, float y, const std::string& text) override;
  bool linkKeyPressed() override;
  float originHeight(int vehicle) override;
  void setSteer(int vehicle, float bias) override;
  void setPedals(int vehicle, float gas, float brake) override;

 private:
  ShvOptions opt_;
  bool keyWasDown_ = false;
  int repairCountdown_ = 0;
};
