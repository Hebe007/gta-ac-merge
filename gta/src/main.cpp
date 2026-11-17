// GtaAcLink.asi: ScriptHookV entry point. Reads gta_link.ini next to the .asi, then ticks the puppet every frame.
#include <windows.h>
#include <cstdlib>
#include <string>
#include "game_shv.hpp"
#include "main.h"
#include "puppet.hpp"
#include "shm_win.hpp"

static HMODULE g_module = nullptr;

static std::string iniValue(const char* key, const char* fallback) {
  char path[MAX_PATH];
  GetModuleFileNameA(g_module, path, MAX_PATH);
  std::string ini(path);
  ini = ini.substr(0, ini.find_last_of("\\/") + 1) + "gta_link.ini";
  char buf[256];
  GetPrivateProfileStringA("link", key, fallback, buf, sizeof buf, ini.c_str());
  return buf;
}

static void ScriptMain() {
  ShvOptions opt;
  opt.gtaDamage = iniValue("gta_damage", "0") == "1";
  opt.frozen = iniValue("frozen", "1") == "1";
  ShvGame game(opt);
  SharedLink shm(iniValue("shm_name", LINK_STATE_NAME));
  Puppet puppet(game, iniValue("model", "sultan"), std::strtof(iniValue("height_trim", "0").c_str(), nullptr));
  LARGE_INTEGER freq;
  QueryPerformanceFrequency(&freq);
  while (true) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    double now = static_cast<double>(t.QuadPart) / static_cast<double>(freq.QuadPart);
    const volatile LinkState* raw = shm.get(now);
    puppet.tick(now, raw, shm.status());
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
