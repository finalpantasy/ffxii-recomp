#pragma once
// Luau script mods: a runtime pack over the native mod API (include/ffxii_mod.h).
//
// A mod declares scripts in its mod.ini:
//
//     id = mymod
//     scripts = main.luau, more.luau     (paths inside the mod folder)
//
// Each mod gets its own sandboxed Luau state (luaL_sandbox: read-only
// libraries, no io, os.execute or require), and its scripts run once at
// load, in the order given. Scripts reach the game through the global
// `ffxii` table; everything goes through the same ffxii_api a native
// module gets, so a script hook sits in the same hook chain:
//
//     ffxii.hook(va, function(cpu, original) ... end) -> true or false
//         cpu.rax ... cpu.r15   registers (numbers), readable and writable
//         original(cpu)         runs the rest of the chain and returns
//         ffxii.ret(cpu)        replaces the function: pops the return address
//       Exactly one of original(cpu) or ffxii.ret(cpu) per call; if the
//       hook errors or does neither, the original runs and the log says so.
//     ffxii.arg(cpu, i)                     argument i (0-3 in registers, then stack)
//     ffxii.xmm_f32(cpu, i), ffxii.set_xmm_f32(cpu, i, v)
//     ffxii.call(cpu, va, a1, ...)          calls a guest function, returns rax
//     ffxii.read_u8/u16/u32/u64/i8/i16/i32/f32/f64(va)
//     ffxii.write_u8/... (va, value)
//     ffxii.log(text), print(...)           one line to the recomp's log
//     ffxii.mod_id, ffxii.game_version
//
// Numbers are doubles, so register and memory values above 2^53 lose low
// bits. Guest addresses (image at 0x120000, heap in the guest arena) fit.
// One mod's hooks run one at a time (a per-mod lock), whatever thread
// calls them. With native code generation (default on x64) Luau compiles
// each script to machine code at load.

#include <memory>
#include <string>
#include <vector>

#include "ffxii_mod.h"

namespace ffxii::luau {

struct Options {
  bool native = true;  // Luau CodeGen where the host supports it
};

class Mod;  // one mod's Luau state and hooks

// Loads and runs one mod's scripts (host paths) against `api` (its
// mod_id names the mod in the log). Returns null and sets err on failure;
// hooks installed before the failure stay.
std::shared_ptr<Mod> Load(const ffxii_api* api, const std::vector<std::string>& files, const Options& opt,
                          std::string& err);

// Loads scripts from source text instead of files (name is the chunk name).
std::shared_ptr<Mod> LoadSource(const ffxii_api* api, const std::string& name, const std::string& source,
                                const Options& opt, std::string& err);

// True when this build can compile Luau to native code.
bool NativeSupported();

// Releases the mod's state. Its hooks must already be out of every chain
// (the mod lab resets its hook table first); the game never unloads.
void Unload(std::shared_ptr<Mod>& mod);

}  // namespace ffxii::luau
