#pragma once
// WebAssembly mods: a runtime pack over the native mod API (include/ffxii_mod.h),
// run by Wasmtime (Apache-2.0 with LLVM exception), compiled to native code
// at load. The guest-side API is include/ffxii_wasm.h.
//
// Each mod gets one Wasmtime store holding all its modules; the modules
// import only the "ffxii" functions, so they cannot reach the host. Game
// memory is reached through checked calls: an address the game has not
// mapped traps the hook instead of faulting the game. One mod's hooks run
// one at a time (a per-mod lock), whatever thread calls them.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ffxii_mod.h"

namespace ffxii::wasm {

struct Options {
  // True when [va, va + len) may be read or written. Null: no check (the
  // mod lab, whose stand-in memory is host memory).
  bool (*readable)(uint64_t va, uint64_t len) = nullptr;
  bool (*writable)(uint64_t va, uint64_t len) = nullptr;
};

class Mod;

// Loads one mod's modules (host paths), links them to `api` and runs each
// module's ffxii_init export. Returns null and sets err on failure; hooks
// installed before the failure stay.
std::shared_ptr<Mod> Load(const ffxii_api* api, const std::vector<std::string>& files, const Options& opt,
                          std::string& err);

// Releases the mod's store and hook slots. Its hooks must already be out of
// every chain (the mod lab resets its hook table first); the game never
// unloads.
void Unload(std::shared_ptr<Mod>& mod);

// Wasmtime's version string.
const char* RuntimeVersion();

}  // namespace ffxii::wasm
