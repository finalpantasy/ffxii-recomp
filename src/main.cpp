#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "mod_index.h"
#include "mod_lab.h"
#include "native_mods.h"
#include "text_mods.h"
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
               "  --no-native-mods  load no mod DLLs (mod.ini native = ...); scripts still load\n"
               "         [--luau-interp]\n"
               "  --luau-interp     run Luau script mods in the interpreter (no native code)\n"
               "         [--wasm-time-limit MS]\n"
               "  --wasm-time-limit MS  longest one WebAssembly hook call may run in mod code\n"
               "                    before it traps (default 1000, 0 for none; init gets 10x)\n"
               "       ffxii_recomp --mod-lab [--mod-lab-dir PATH] [--mod-lab-port N] [--no-browser]\n"
               "       ffxii_recomp --mod-lab-run [--mod-lab-dir PATH] [--mod-lab-iters N]\n"
               "                    [--mod-lab-reps N] [--mod-lab-only KEY,KEY]\n"
               "  --mod-lab         serve the Mod Lab page (127.0.0.1:8723) and open it: the\n"
               "                    same test mod in each language, timed per hook call\n"
               "  --mod-lab-run     run the lab once and print the results as JSON\n"
               "         [--interp] [--fps-overlay] [--interp-no-pace] [--interp-t T] [-- ARGS...]\n"
               "  --interp          show a blended frame between each two game frames\n"
               "                    (60 Hz game logic, twice the presented frame rate)\n"
               "  --fps-overlay     show frame rate, slowest frame and game ticks per second\n"
               "  --interp-no-pace  present the blended and real frame together (testing)\n"
               "  --interp-t T      blend point between the frames, 0..1 (default 0.5)\n"
               "         [--fps N]\n"
               "  --fps N           native frame rate, a multiple of 60 (120, 180, 240):\n"
               "                    the game updates N times a second at unchanged speed;\n"
               "                    needs FrameLimit 60 in the game's settings\n";
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
  // --mod-lab / --mod-lab-run: the mod lab page or runner; no game needed.
  if (const int rc = ffxii::modlab::Main(argc, argv, recomp::platform::ExeDir()); rc != -1) return rc;
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
    } else if (arg == "--luau-interp") {
      SetSwitch("FFXII_LUAU_INTERP", "1");
    } else if (arg == "--wasm-time-limit" && i + 1 < argc) {
      SetSwitch("FFXII_WASM_TIME_LIMIT_MS", argv[++i]);
    } else if (arg == "--mods-watch") {
      SetSwitch("FFXII_MODS_WATCH", "1");
    } else if (arg == "--interp") {
      // SwARc d3d11.cpp/dxgi.cpp: record each frame on deferred contexts and
      // present a blended copy before it.
      SetSwitch("FFXII_D3D_RECORD", "1");
      SetSwitch("FFXII_D3D_INTERP", "1");
    } else if (arg == "--fps-overlay") {
      // SwARc dxgi.cpp: presented fps, slowest frame of the last second and
      // game ticks per second in the top-left corner.
      SetSwitch("FFXII_FPS_OVERLAY", "1");
    } else if (arg == "--interp-no-pace") {
      SetSwitch("FFXII_D3D_PACE", "0");
    } else if (arg == "--interp-t" && i + 1 < argc) {
      SetSwitch("FFXII_D3D_INTERP_T", argv[++i]);
    } else if (arg == "--fps" && i + 1 < argc) {
      // src/frame_rate.cpp reads FFXII_SIM_MULT: updates per 1/60 s tick.
      const int fps = std::atoi(argv[++i]);
      if (fps < 60 || fps % 60 != 0) {
        std::cerr << "--fps takes a multiple of 60\n";
        return 2;
      }
      SetSwitch("FFXII_SIM_MULT", std::to_string(fps / 60));
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
    // Text merging reads the originals from the archive.
    ffxii::vbf::Archive archive;
    std::string verr;
    if ((!index.copies.empty() || !index.patches.empty()) &&
        !archive.Open((game_dir / "FFXII_TZA.vbf").string(), verr))
      std::cout << "archive: " << verr << '\n';
    const ffxii::text::ModTextMerges text = ffxii::text::MergeModText(index, archive);
    std::cout << "mods folder: " << root << '\n' << index.Report() << text.report;
    return index.errors.empty() && text.problems == 0 ? 0 : 1;
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
