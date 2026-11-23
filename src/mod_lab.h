#pragma once
// Mod lab: runs the same test mod written in different languages against
// stand-in game functions and compares cost per hook call
// (examples/modlab/lab_targets.h). Mods load through the real loaders
// (native DLLs with ffxii_mod_init, Luau scripts through luau_mods), so the
// numbers include each language's real call path into and out of the hook;
// the game itself is not running. `ffxii_recomp --mod-lab` serves a page
// for it; `--mod-lab-run` prints the JSON once.

#include <cstdint>
#include <string>
#include <vector>

namespace ffxii::modlab {

struct RunOptions {
  uint64_t iters = 1000000;          // calls per test and repeat
  int reps = 5;                      // repeats; the median counts
  std::vector<std::string> only;     // entry keys to run (empty: all)
};

// Lab mods in dir (mod.ini folders; see examples/modlab), as JSON:
// {"dir":..., "luau_native":bool, "entries":[{"key","id","name","language","runtime","files"}], "errors":[...]}
std::string ListJson(const std::string& dir);

// Runs the lab and returns the results as JSON (format in mod_lab.cpp).
std::string RunJson(const std::string& dir, const RunOptions& opt);

// Serves the Mod Lab page on 127.0.0.1:port until the process ends.
// Opens it in the default browser unless open_browser is false.
int Serve(const std::string& dir, int port, bool open_browser);

// --mod-lab* command-line handling shared by ffxii_recomp and ffxii_modlab.
// Returns -1 when argv has no --mod-lab flag, else the exit code.
int Main(int argc, char** argv, const std::string& exe_dir);

}  // namespace ffxii::modlab
