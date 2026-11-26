// Frame Rate Limit choices above 60 fps (fps_menu.h).

#include "fps_menu.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

#include "runtime/platform.h"
#include "x86/lifted.h"

namespace ffxii::fps_menu {
namespace {

constexpr const char* kKey = "FrameRate";
constexpr const char* kUnit = "s 308";      // "60 FPS" in menu_command.bin
constexpr const char* kWidest = "240 FPS";  // the longest label

std::mutex g_mu;
int g_choice = 0;
// Where the game loaded string 308, once read; 0 before that.
uint64_t g_label_addr = 0;
size_t g_label_pos = 0, g_label_size = 0;  // in the patched file
std::vector<uint8_t> g_term;               // the unit's terminator bytes

std::string IniPath() { return recomp::platform::ExeDir() + "/ffxii_recomp.ini"; }

std::string Label(int fps) { return std::to_string(fps) + " FPS"; }

// Writes the label for `fps` over string 308 in guest memory. g_mu held.
void WriteLabel(int fps) {
  if (!g_label_addr || fps < 60) return;
  text::Bytes enc;
  std::string err;
  if (!text::Encode(Label(fps), false, enc, err)) return;
  if (enc.size() + g_term.size() > g_label_size) return;
  uint8_t* p = recomp::g_guest_base + g_label_addr;
  std::memset(p, 0, g_label_size);
  std::memcpy(p, enc.data(), enc.size());
  std::memcpy(p + enc.size(), g_term.data(), g_term.size());
}

}  // namespace

int SavedRate() {
  std::ifstream in(IniPath());
  std::string line;
  while (std::getline(in, line)) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = line.substr(0, eq);
    while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
    if (k == kKey) return std::atoi(line.c_str() + eq + 1);
  }
  return 0;
}

void SaveRate(int fps) {
  std::vector<std::string> lines;
  {
    std::ifstream in(IniPath());
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      const size_t eq = line.find('=');
      std::string k = eq == std::string::npos ? "" : line.substr(0, eq);
      while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
      if (k != kKey) lines.push_back(line);
    }
  }
  lines.push_back(std::string(kKey) + " = " + std::to_string(fps));
  std::ofstream out(IniPath(), std::ios::binary | std::ios::trunc);
  for (const std::string& l : lines) out << l << "\n";
  if (!out) std::fprintf(stderr, "[fps_menu] cannot write %s\n", IniPath().c_str());
}

int Choice(bool game_field_60, int running_fps) {
  std::lock_guard lock(g_mu);
  if (g_choice) return g_choice;
  if (!game_field_60) return 30;
  const int saved = SavedRate();
  if (saved == 60 || saved == 120 || saved == 240) return saved;
  return running_fps >= 60 ? running_fps : 60;
}

void SetChoice(int fps) {
  {
    std::lock_guard lock(g_mu);
    g_choice = fps;
    WriteLabel(fps);
  }
  SaveRate(fps);
  std::fprintf(stderr, "[fps_menu] frame rate limit %d (applies at the next start)\n", fps);
}

bool IsMenuCommand(const std::string& path) {
  static const std::string head = "ps2data/image/ff12/test_battle/";
  static const std::string tail = "/binaryfile/menu_command.bin";
  return path.size() > head.size() + tail.size() && path.compare(0, head.size(), head) == 0 &&
         path.compare(path.size() - tail.size(), tail.size(), tail) == 0 &&
         path.find('/', head.size()) == path.size() - tail.size();
}

std::shared_ptr<const text::Bytes> Patch(const text::Bytes& original) {
  std::string err;
  text::RawUnits units;
  if (!text::Units(original, units, err)) return nullptr;
  const auto u = units.find(kUnit);
  text::Bytes sixty;
  if (u == units.end() || !text::Encode("60", false, sixty, err)) return nullptr;
  // Only the retail layout: string 308 holds "60" (other languages too).
  if (std::search(u->second.begin(), u->second.end(), sixty.begin(), sixty.end()) == u->second.end())
    return nullptr;
  auto out = std::make_shared<text::Bytes>();
  if (!text::Rebuild(original, {{kUnit, kWidest}}, *out, err)) {
    std::fprintf(stderr, "[fps_menu] cannot widen the label: %s\n", err.c_str());
    return nullptr;
  }
  text::RawUnits now;
  text::Bytes widest;
  if (!text::Units(*out, now, err) || !text::Encode(kWidest, false, widest, err)) return nullptr;
  const text::Bytes& unit = now[kUnit];
  const auto at = std::search(out->begin(), out->end(), unit.begin(), unit.end());
  if (at == out->end() || unit.size() < widest.size()) return nullptr;
  std::lock_guard lock(g_mu);
  g_label_pos = size_t(at - out->begin());
  g_label_size = unit.size();
  g_term.assign(unit.begin() + widest.size(), unit.end());
  g_label_addr = 0;
  return out;
}

void OnRead(uint64_t dst, uint64_t pos, uint64_t n) {
  std::lock_guard lock(g_mu);
  if (!g_label_size || g_label_pos < pos || g_label_pos + g_label_size > pos + n) return;
  g_label_addr = dst + (g_label_pos - pos);
  // Show the current choice; before the menu sets one, the saved or running
  // rate (the game field is 1 whenever this label is shown).
  int fps = g_choice;
  if (!fps) {
    const int saved = SavedRate();
    fps = saved >= 60 ? saved : 60;
  }
  WriteLabel(fps);
}

}  // namespace ffxii::fps_menu
