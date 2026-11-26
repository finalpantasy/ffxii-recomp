#pragma once
// Frame Rate Limit choices above 60 fps in Config > Graphics Settings.
//
// The retail row toggles the game's FrameLimit field (settings +0x48) between
// 0 (30 FPS, label string 307 of menu_command.bin) and 1 (60 FPS, string
// 308). The recomp adds 120, 144, 165 and 240: the game field stays 1 for every choice
// from 60 up, the choice is saved in <exe dir>/ffxii_recomp.ini
// (`FrameRate = 120`), and string 308 is rewritten in the loaded file to show
// it. The choice takes effect at the next start, like the retail one; the
// command-line flag --fps overrides it.

#include <cstdint>
#include <memory>
#include <string>

#include "text_formats.h"

namespace ffxii::fps_menu {

// The row's choices, in order.
inline constexpr int kRates[] = {30, 60, 120, 144, 165, 240};
inline constexpr int kRateCount = int(sizeof(kRates) / sizeof(kRates[0]));
// A saved rate the row offers above 30.
inline bool IsMenuRate(int fps) {
  for (int r : kRates)
    if (r == fps && r >= 60) return true;
  return false;
}

// Rate saved in ffxii_recomp.ini, or 0.
int SavedRate();
void SaveRate(int fps);

// The choice the menu row stands on (one of kRates): the last one set,
// else 30 when the game field is 0, else the saved rate, else running_fps.
int Choice(bool game_field_60, int running_fps);
void SetChoice(int fps);

// menu_command.bin of any language (path as mods::Normalize gives it).
bool IsMenuCommand(const std::string& path);
// The file with string 308 widened to hold the longest label, or null when
// it does not look like the retail table.
std::shared_ptr<const text::Bytes> Patch(const text::Bytes& original);
// The game read [pos, pos+n) of the patched file to guest address dst.
void OnRead(uint64_t dst, uint64_t pos, uint64_t n);

}  // namespace ffxii::fps_menu
