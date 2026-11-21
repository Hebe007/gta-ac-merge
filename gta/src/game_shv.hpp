#pragma once
#include <string>
#include "game.hpp"

struct ShvOptions {
  bool gtaDamage = false;
  bool tyresBurst = true;  // GTA may burst the tyres (spike strips, shots); the AC app blows the same ones  // let GTA damage the car (off: AC is in charge; GTA-only objects can't wreck it)
  bool frozen = false;     // true: GTA physics never moves the car (no flips or pushes), but wheels/speedo don't move
  // Engine sound of this GTA car model, so the engine can run (dashboard, speedometer) almost silently.
  // "off" = engine off as before (no dashboard).
  std::string engineAudio = "voltic";
  std::string iniPath;     // shown by F7 so you can see which file the script reads
  bool iniFound = false;
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
  Quat rotation(int vehicle) override;
  Vec3 forward(int vehicle) override;
  void prepare(int vehicle) override;
  void release(int vehicle) override;
  bool diagKeyPressed() override;
  bool resetKeyPressed() override;
  std::string diagnostics(int vehicle) override;
  void logHole(Vec3 gtaPos) override;
  void logStuck(const std::string& line) override;
  bool takeHit(int vehicle, Hit& out) override;
  uint32_t burstTyres(int vehicle) override;
  void syncTyres(int vehicle, uint32_t keepBurst) override;
  std::string holesFile;  // set by main: next to the .asi
  std::string stuckFile;

 private:
  ShvOptions opt_;
  bool keyWasDown_ = false;
  bool diagWasDown_ = false;
  bool resetWasDown_ = false;
  int repairCountdown_ = 0;
  int blockedFrames_ = 0;   // > 0: GTA is pushing the car back from something; place it directly, frozen
  bool heldFrozen_ = false;
  uint32_t keepTyres_ = 0;  // tyres to leave burst (blown in AC): the once-a-second repair waits
  bool dynamicHit_ = false;  // this frame's contact is a GTA car or pedestrian (handled as a hit, not frozen)
  bool haveCmd_ = false;
  Vec3 cmdVel_{};            // velocity setPose gave the car last frame
};
