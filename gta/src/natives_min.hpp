// The few GTA natives we use, called by hash through the SDK's invoke<>. Hashes and parameters are from
// alloc8or/gta5-nativedb-data (natives.json); calling by hash keeps us independent of which names the
// SDK's natives.h uses.
#pragma once
#include <windows.h>
#include "main.h"
#include "types.h"
#include "nativeCaller.h"

namespace nat {
inline Ped playerPedId() { return invoke<Ped>(0xD80958FC74E988A6); }
inline Player playerId() { return invoke<Player>(0x4F8644AF03D0E0D6); }
inline BOOL isPlayerBeingArrested(Player p) { return invoke<BOOL>(0x388A47C51ABDAC8E, p, TRUE); }
inline BOOL isEntityDead(Entity e) { return invoke<BOOL>(0x5F9532F3B5CC2551, e, FALSE); }
inline BOOL doesEntityExist(Entity e) { return invoke<BOOL>(0x7239B21A38F536BA, e); }
inline Vector3 getEntityCoords(Entity e) { return invoke<Vector3>(0x3FEF770D40960D5A, e, TRUE); }
inline void setEntityCoordsNoOffset(Entity e, float x, float y, float z) {
  invoke<Void>(0x239A3351AC1DA385, e, x, y, z, FALSE, FALSE, FALSE);
}
inline void setEntityQuaternion(Entity e, float x, float y, float z, float w) { invoke<Void>(0x77B21BE7AC540F07, e, x, y, z, w); }
inline void setEntityVelocity(Entity e, float x, float y, float z) { invoke<Void>(0x1C99BB7B6E96D16F, e, x, y, z); }
inline void setEntityAngularVelocity(Entity e, float x, float y, float z) { invoke<Void>(0x8339643499D1222E, e, x, y, z); }
inline void setEntityAsMissionEntity(Entity e) { invoke<Void>(0xAD738C3085FE7E11, e, TRUE, TRUE); }
inline Vehicle getVehiclePedIsIn(Ped p) { return invoke<Vehicle>(0x9A9112A0FE9A4713, p, FALSE); }
inline void setPedIntoVehicle(Ped p, Vehicle v) { invoke<Void>(0xF75B0D629E1C063D, p, v, -1); }
inline Vehicle createVehicle(Hash model, float x, float y, float z, float heading) {
  return invoke<Vehicle>(0xAF35D0D2583051B0, model, x, y, z, heading, FALSE, FALSE, FALSE);
}
inline void deleteVehicle(Vehicle* v) { invoke<Void>(0xEA386986E786A54F, v); }
inline void setVehicleEngineOn(Vehicle v, BOOL on) { invoke<Void>(0x2497C4717C8B881E, v, on, TRUE, TRUE); }
inline void requestModel(Hash h) { invoke<Void>(0x963D27A58DF860AC, h); }
inline BOOL hasModelLoaded(Hash h) { return invoke<BOOL>(0x98A4EB5D89A0C952, h); }
inline void setModelAsNoLongerNeeded(Hash h) { invoke<Void>(0xE532F5D78798DAAB, h); }
inline BOOL isModelInCdimage(Hash h) { return invoke<BOOL>(0x35B9E0803292B641, h); }
inline BOOL isModelAVehicle(Hash h) { return invoke<BOOL>(0x19AAC8F07BFEC53E, h); }
inline Hash getHashKey(const char* s) { return invoke<Hash>(0xD24D37CC275948CC, s); }
inline BOOL getGroundZFor3dCoord(float x, float y, float z, float* out) {
  return invoke<BOOL>(0xC906A7DAB05C8D2B, x, y, z, out, FALSE, FALSE);
}
inline void disableControlAction(int group, int control) { invoke<Void>(0xFE99B66D079CF6BC, group, control, TRUE); }
inline void setTextFont(int f) { invoke<Void>(0x66E0276CC5F6B9DA, f); }
inline void setTextScale(float a, float b) { invoke<Void>(0x07C837F9A01C34C9, a, b); }
inline void setTextColour(int r, int g, int b, int a) { invoke<Void>(0xBE6B23FFA53FB442, r, g, b, a); }
inline void setTextOutline() { invoke<Void>(0x2513DFB0FB8400FE); }
inline void setTextCentre(BOOL c) { invoke<Void>(0xC02F4DBFB51D988B, c); }
inline void beginTextCommandDisplayText(const char* t) { invoke<Void>(0x25FBB336DF1804CB, t); }
inline void addTextComponentSubstringPlayerName(const char* t) { invoke<Void>(0x6C188BE134E074AA, t); }
inline void endTextCommandDisplayText(float x, float y) { invoke<Void>(0xCD015E5BB0D96A57, x, y, 0); }
inline float getEntityHeightAboveGround(Entity e) { return invoke<float>(0x1DD55701034110E5, e); }
inline BOOL setVehicleOnGroundProperly(Vehicle v) { return invoke<BOOL>(0x49733E92263139D1, v, 5.0f); }
inline void freezeEntityPosition(Entity e, BOOL on) { invoke<Void>(0x428CA6DBD1094446, e, on); }
inline void setEntityInvincible(Entity e, BOOL on) { invoke<Void>(0x3882114BDE571AD4, e, on, TRUE); }
inline void setEntityCanBeDamaged(Entity e, BOOL on) { invoke<Void>(0x1760FFA8AB074D66, e, on); }
inline void setEntityProofsAll(Entity e) { invoke<Void>(0xFAEE099C6F890BB8, e, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE); }
inline void setVehicleCanBeVisiblyDamaged(Vehicle v, BOOL on) { invoke<Void>(0x4C7028F78FFD3681, v, on); }
inline void setVehicleCanBreak(Vehicle v, BOOL on) { invoke<Void>(0x59BF8C3D52C92F66, v, on); }
inline void setVehicleTyresCanBurst(Vehicle v, BOOL on) { invoke<Void>(0xEB9DC3C7D8596C46, v, on); }
inline void setVehicleWheelsCanBreak(Vehicle v, BOOL on) { invoke<Void>(0x29B18B4FD460CA8F, v, on); }
inline void setVehicleStrong(Vehicle v, BOOL on) { invoke<Void>(0x3E8C8727991A8A0B, v, on); }
inline void setDisableVehiclePetrolTankDamage(Vehicle v, BOOL on) { invoke<Void>(0x37C8252A7C92D017, v, on); }
inline void setVehicleEngineCanDegrade(Vehicle v, BOOL on) { invoke<Void>(0x983765856F2564F9, v, on); }
inline void setVehicleFixed(Vehicle v) { invoke<Void>(0x115722B1B9C14C1C, v); }
inline Hash getEntityModel(Entity e) { return invoke<Hash>(0x9F47B058362C84B5, e); }
inline void getModelDimensions(Hash h, Vector3* mn, Vector3* mx) { invoke<Void>(0x03E8D3D5F549087A, h, mn, mx); }
}  // namespace nat
