#include <filesystem>
#include <iostream>
#include <string>

#include "win32/process.h"

namespace {

void Usage() {
  std::cerr << "usage: ffxii_recomp --game-dir PATH [--exe PATH] [-- ARGS...]\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path game_dir;
  std::filesystem::path exe;
  std::string game_args;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--game-dir" && i + 1 < argc) {
      game_dir = argv[++i];
    } else if (arg == "--exe" && i + 1 < argc) {
      exe = argv[++i];
    } else if (arg == "--") {
      for (int j = i + 1; j < argc; j++) {
        if (!game_args.empty()) game_args += ' ';
        game_args += argv[j];
      }
      break;
    } else {
      Usage();
      return 2;
    }
  }
  if (game_dir.empty()) {
    Usage();
    return 2;
  }
  if (exe.empty()) exe = game_dir / "x64" / "FFXII_TZA.exe";
  if (!std::filesystem::is_regular_file(exe)) {
    std::cerr << "FFXII_TZA.exe not found: " << exe << '\n';
    return 2;
  }

  recomp::win32::GameConfig game;
  game.id = "ffxii";
  game.guest_dir = "C:\\Games\\FFXII";
  game.exe_name = "FFXII_TZA.exe";
  // The game opens its archive as "../FFXII_TZA.vbf", relative to x64.
  game.exe_dir = "x64";
  return recomp::RunProcess(game, exe.string(), game_dir.string(), game_args);
}
