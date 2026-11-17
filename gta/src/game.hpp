// Everything the puppet needs from GTA, behind an interface so the logic is testable without the game.
// The real implementation (game_shv.cpp) calls ScriptHookV natives; tests use a fake.
#pragma once
#include <string>
#include "link_math.hpp"

class Game {
 public:
  virtual ~Game() = default;
  virtual bool playerDeadOrArrested() = 0;
  virtual int playerVehicle() = 0;  // 0 when on foot
  // Spawns `model` at a GTA position/heading, returns the vehicle handle or 0 on failure.
  virtual int spawnVehicle(const std::string& model, Vec3 pos, float headingDeg) = 0;
  virtual void putPlayerIn(int vehicle) = 0;
  virtual bool exists(int vehicle) = 0;
  virtual void deleteVehicle(int vehicle) = 0;
  virtual Vec3 position(int vehicle) = 0;
  virtual void setPose(int vehicle, const Pose& pose) = 0;
  virtual void disableVehicleControls() = 0;  // call every frame while linked
  virtual bool groundZ(Vec3 above, float& z) = 0;  // false when GTA finds no ground
  virtual void drawText(float x, float y, const std::string& text) = 0;  // screen 0..1
  virtual bool linkKeyPressed() = 0;  // edge-triggered toggle key (F8)
  // Height of the vehicle's origin above its wheels' ground contact (measured right after spawning).
  virtual float originHeight(int vehicle) = 0;
  // Front wheel angle as GTA steer bias: -1 full right .. +1 full left. Call every frame.
  virtual void setSteer(int vehicle, float bias) = 0;
  // Throttle and brake 0..1 as player input: GTA revs its engine (rev counter) and lights the brake lights.
  virtual void setPedals(int vehicle, float gas, float brake) = 0;
  virtual Quat rotation(int vehicle) = 0;
  // Make an existing car (the one you're in) ready to follow AC / give it back afterwards.
  virtual void prepare(int vehicle) = 0;
  virtual void release(int vehicle) = 0;
  // F7 diagnostics: what GTA itself reports about the car (speed, engine) and the settings it runs with.
  virtual bool diagKeyPressed() = 0;
  virtual std::string diagnostics(int vehicle) = 0;
  // Remember a spot where the car fell through the generated track (appended to gta_link_holes.txt).
  virtual void logHole(Vec3 gtaPos) = 0;
};
