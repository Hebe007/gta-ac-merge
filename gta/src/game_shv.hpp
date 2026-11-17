#pragma once
#include "game.hpp"

class ShvGame : public Game {
 public:
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

 private:
  bool keyWasDown_ = false;
};
