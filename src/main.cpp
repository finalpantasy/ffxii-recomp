#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "mod_index.h"
#include "native_mods.h"
#include "runtime/platform.h"
#include "win32/process.h"
#include "x86/sse.h"

namespace {

void Usage() {
  std::cerr << "usage: ffxii_recomp --game-dir PATH [--exe PATH] [--exact-sse-flags]\n"
               "         [--mods-dir PATH] [--mods-disable ID,ID] [--mods-log] [--mods-watch]\n"
               "         [--mods-list] [--no-native-mods]\n"
               "  --mods-watch      reload the mods folder when its files change\n"
               "  --mods-list       print the mods that would load and exit (1 on errors)\n"
               "  --no-native-mods  load no mod DLLs (mod.ini native = ...)\n"
               "         [--interp] [--interp-no-pace] [--interp-t T] [-- ARGS...]\n"
               "  --interp          show a blended frame between each two game frames\n"
               "                    (60 Hz game logic, twice the presented frame rate)\n"
               "  --interp-no-pace  present the blended and real frame together (testing)\n"
               "  --interp-t T      blend point between the frames, 0..1 (default 0.5)\n";
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
  bool mods_list = false;
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
    } else if (arg == "--mods-list") {
      mods_list = true;
    } else if (arg == "--no-native-mods") {
      SetSwitch("FFXII_MODS_NO_NATIVE", "1");
    } else if (arg == "--mods-watch") {
      SetSwitch("FFXII_MODS_WATCH", "1");
    } else if (arg == "--interp") {
      // SwARc d3d11.cpp/dxgi.cpp: record each frame on deferred contexts and
      // present a blended copy before it.
      SetSwitch("FFXII_D3D_RECORD", "1");
      SetSwitch("FFXII_D3D_INTERP", "1");
    } else if (arg == "--interp-no-pace") {
      SetSwitch("FFXII_D3D_PACE", "0");
    } else if (arg == "--interp-t" && i + 1 < argc) {
      SetSwitch("FFXII_D3D_INTERP_T", argv[++i]);
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
  if (mods_list) {
    // For launchers: the mods the game would load, which file each one
    // serves, conflicts and manifest errors, without starting the game.
    const char* dir = std::getenv("FFXII_MODS_DIR");
    const char* off = std::getenv("FFXII_MODS_DISABLE");
    const std::string root = ffxii::mods::ResolveRoot(dir ? dir : "", recomp::platform::ExeDir(),
                                                      game_dir.string());
    const ffxii::mods::Index index =
        ffxii::mods::Build(root, ffxii::mods::ParseIdList(off ? off : ""));
    std::cout << "mods folder: " << root << '\n' << index.Report();
    return index.errors.empty() ? 0 : 1;
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
  game.before_entry = ffxii::LoadNativeModules;
  return recomp::RunProcess(game, exe.string(), game_dir.string(), game_args);
}
