// GtaAcLink.asi: ScriptHookV entry point. Reads gta_link.ini next to the .asi, then ticks the puppet every frame.
#include <windows.h>
#include <cstdlib>
#include <string>
#include "game_shv.hpp"
#include "main.h"
#include "puppet.hpp"
#include "keys.hpp"
#include "race_shv.hpp"
#include "shm_win.hpp"

static HMODULE g_module = nullptr;

static std::string besideAsi(const char* file) {
  char path[MAX_PATH];
  GetModuleFileNameA(g_module, path, MAX_PATH);
  std::string p(path);
  return p.substr(0, p.find_last_of("\\/") + 1) + file;
}

static std::string iniValue(const char* key, const char* fallback) {
  char path[MAX_PATH];
  GetModuleFileNameA(g_module, path, MAX_PATH);
  std::string ini(path);
  ini = ini.substr(0, ini.find_last_of("\\/") + 1) + "gta_link.ini";
  char buf[256];
  GetPrivateProfileStringA("link", key, fallback, buf, sizeof buf, ini.c_str());
  return buf;
}

// A key from gta_link.ini (key_link=F8 ...); the default when missing or not a key name.
static int iniKey(const char* key, const char* fallback, std::string* name = nullptr) {
  std::string v = iniValue(key, fallback);
  int code = keyCode(v);
  if (!code) { v = fallback; code = keyCode(fallback); }
  if (name) *name = v;
  return code;
}

static void ScriptMain() {
  ShvOptions opt;
  opt.gtaDamage = iniValue("gta_damage", "0") == "1";
  opt.frozen = iniValue("frozen", "0") == "1";
  opt.iniPath = besideAsi("gta_link.ini");
  opt.iniFound = GetFileAttributesA(opt.iniPath.c_str()) != INVALID_FILE_ATTRIBUTES;
  opt.engineAudio = iniValue("engine_audio", "voltic");
  opt.tyresBurst = iniValue("tyres_burst", "1") == "1";
  std::string linkName, resetName;
  opt.keyLink = iniKey("key_link", "F8", &linkName);
  opt.keyDiag = iniKey("key_diag", "F7");
  opt.keyReset = iniKey("key_reset", "F9", &resetName);
  ShvGame game(opt);
  game.holesFile = besideAsi("gta_link_holes.txt");
  game.stuckFile = besideAsi("gta_link_stuck.txt");
  SharedLink shm(iniValue("shm_name", LINK_STATE_NAME));
  SharedCommand cmd(LINK_CMD_NAME);
  SharedHits hits(LINK_HITS_NAME);
  PuppetOptions po;
  po.heightTrim = std::strtof(iniValue("height_trim", "0").c_str(), nullptr);
  po.steerInvert = iniValue("steer_invert", "0") == "1";
  po.steerScale = std::strtof(iniValue("steer_scale", "1").c_str(), nullptr);
  po.steerOffset = std::strtof(iniValue("steer_offset", "0").c_str(), nullptr);
  po.pedals = iniValue("pedals", "1") == "1";
  po.autoReset = iniValue("auto_reset", "1") == "1";
  po.steerLead = std::strtof(iniValue("steer_lead", "0.06").c_str(), nullptr);
  po.hitStrength = std::strtof(iniValue("hit_strength", "1").c_str(), nullptr);
  po.hitSideways = std::strtof(iniValue("hit_sideways", "1.5").c_str(), nullptr);
  po.linkKey = linkName;
  po.resetKey = resetName;
  Puppet puppet(game, iniValue("model", "sultan"), po);
  RaceOptions ro;
  ro.opponents = std::atoi(iniValue("race_opponents", "3").c_str());
  if (ro.opponents < 1) ro.opponents = 1;
  if (ro.opponents > 7) ro.opponents = 7;
  std::string cars = iniValue("race_cars", "");
  if (!cars.empty()) {
    ro.cars.clear();
    size_t at = 0;
    while (at <= cars.size()) {
      size_t comma = cars.find(',', at);
      std::string c = cars.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
      c.erase(0, c.find_first_not_of(" \t"));
      c.erase(c.find_last_not_of(" \t") + 1);
      if (!c.empty()) ro.cars.push_back(c);
      if (comma == std::string::npos) break;
      at = comma + 1;
    }
    if (ro.cars.empty()) ro.cars = RaceOptions{}.cars;
  }
  ro.maxSpeed = std::strtof(iniValue("race_top_speed_kmh", "290").c_str(), nullptr) / 3.6f;
  ShvRaceWorld raceWorld;
  raceWorld.keys[RaceWorld::kPolice] = iniKey("key_police", "F10", &ro.policeKey);
  raceWorld.keys[RaceWorld::kRace] = iniKey("key_race", "F11", &ro.raceKey);
  raceWorld.keys[RaceWorld::kUp] = iniKey("key_menu_up", "UP");
  raceWorld.keys[RaceWorld::kDown] = iniKey("key_menu_down", "DOWN");
  raceWorld.keys[RaceWorld::kLeft] = iniKey("key_menu_left", "LEFT");
  raceWorld.keys[RaceWorld::kRight] = iniKey("key_menu_right", "RIGHT");
  raceWorld.keys[RaceWorld::kEnter] = iniKey("key_menu_enter", "ENTER");
  raceWorld.resultsFile = besideAsi("gta_race_results.txt");
  Race race(raceWorld, ro);
  LARGE_INTEGER freq;
  QueryPerformanceFrequency(&freq);
  while (true) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    double now = static_cast<double>(t.QuadPart) / static_cast<double>(freq.QuadPart);
    const volatile LinkState* raw = shm.get(now);
    puppet.tick(now, raw, shm.status(), cmd.get(), hits.get());
    race.tick(now);
    scriptWait(0);
  }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_module = module;
    scriptRegister(module, ScriptMain);
  } else if (reason == DLL_PROCESS_DETACH) {
    scriptUnregister(module);
  }
  return TRUE;
}
