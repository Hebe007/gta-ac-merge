// Key names in gta_link.ini -> Windows virtual-key codes: F1..F24, A..Z, 0..9, NUM0..NUM9, UP, DOWN, LEFT, RIGHT,
// ENTER, SPACE, TAB, BACKSPACE, INSERT, DELETE, HOME, END, PAGEUP, PAGEDOWN, or a number. 0 = not a key name.
#pragma once
#include <cctype>
#include <cstdlib>
#include <string>

inline int keyCode(std::string n) {
  size_t a = n.find_first_not_of(" \t"), b = n.find_last_not_of(" \t");
  if (a == std::string::npos) return 0;
  n = n.substr(a, b - a + 1);
  for (auto& c : n) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  if (n.size() == 1 && ((n[0] >= 'A' && n[0] <= 'Z') || (n[0] >= '0' && n[0] <= '9'))) return n[0];
  if (n.size() >= 2 && n[0] == 'F' && std::isdigit(static_cast<unsigned char>(n[1]))) {
    int f = std::atoi(n.c_str() + 1);
    return f >= 1 && f <= 24 ? 0x6F + f : 0;
  }
  if (n.rfind("NUM", 0) == 0 && n.size() == 4 && std::isdigit(static_cast<unsigned char>(n[3]))) return 0x60 + (n[3] - '0');
  struct { const char* name; int vk; } named[] = {
      {"UP", 0x26}, {"DOWN", 0x28}, {"LEFT", 0x25}, {"RIGHT", 0x27}, {"ENTER", 0x0D}, {"RETURN", 0x0D},
      {"SPACE", 0x20}, {"TAB", 0x09}, {"BACKSPACE", 0x08}, {"INSERT", 0x2D}, {"DELETE", 0x2E}, {"HOME", 0x24},
      {"END", 0x23}, {"PAGEUP", 0x21}, {"PAGEDOWN", 0x22}};
  for (auto& k : named) if (n == k.name) return k.vk;
  if (std::isdigit(static_cast<unsigned char>(n[0]))) {
    int v = std::atoi(n.c_str());
    return v > 0 && v < 256 ? v : 0;
  }
  return 0;
}
