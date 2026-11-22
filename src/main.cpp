#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "win32/process.h"
#include "x86/sse.h"

namespace {

void Usage() {
  std::cerr << "usage: ffxii_recomp --game-dir PATH [--exe PATH] [--exact-sse-flags]\n"
               "         [--mods-dir PATH] [--mods-disable ID,ID] [--mods-log] [-- ARGS...]\n";
}

// The mod loader reads FFXII_MODS_* (src/mods.cpp); a flag sets the same
// variable so launchers need no environment.
void SetSwitch(const char* name, const std::string& value) {
#ifdef _WIN32
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path game_dir;
  std::filesystem::path exe;
  std::string game_args;
  // FFXII never reads the SSE sticky exception flags (no STMXCSR in its
  // code; _controlfp reads only the control bits), so SSE arithmetic runs
  // bare by default. --exact-sse-flags or RECOMP_SSE_EXACT_FLAGS=1 tracks them.
  bool exact_flags = false;
  if (const char* e = std::getenv("RECOMP_SSE_EXACT_FLAGS")) exact_flags = *e && *e != '0';
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--game-dir" && i + 1 < argc) {
      game_dir = argv[++i];
    } else if (arg == "--exe" && i + 1 < argc) {
      exe = argv[++i];
    } else if (arg == "--exact-sse-flags") {
      exact_flags = true;
    } else if (arg == "--mods-dir" && i + 1 < argc) {
      SetSwitch("FFXII_MODS_DIR", argv[++i]);
    } else if (arg == "--mods-disable" && i + 1 < argc) {
      SetSwitch("FFXII_MODS_DISABLE", argv[++i]);
    } else if (arg == "--mods-log") {
      SetSwitch("FFXII_MODS_LOG", "1");
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
#if defined(__x86_64__)
  recomp::sse::g_lazy_flags = !exact_flags && recomp::sse::kNativeSse;
  if (recomp::sse::g_lazy_flags) recomp::sse::EnsureHostMxcsr();
#endif
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
