/* ffxii_mod.h: the native module interface of the FFXII recomp, version 1.
 *
 * A native module is a Windows x64 DLL that a mod declares in its mod.ini:
 *
 *     id = mymod
 *     native = mymod.dll          (comma list, paths inside the mod folder)
 *
 * The recomp loads the DLLs of every enabled mod before the game's entry
 * point runs, lowest-ranked mod first, and calls each one's exported
 *
 *     int ffxii_mod_init(const ffxii_api* api);
 *
 * which returns 0 on success. A nonzero return is logged as a failure; the
 * DLL stays loaded and hooks it installed stay. The api pointer and every
 * function in it stay valid for the life of the process. Native modules
 * are trusted code: they run with the game's full rights, unlike content
 * mods. --no-native-mods (FFXII_MODS_NO_NATIVE=1) skips them all.
 *
 * Guest code. The game is x64 code lifted to C++; it runs on guest CPU
 * state (ffxii_cpu: registers, flags, SSE) and guest memory (a flat arena
 * that guest_ptr turns into host pointers). Guest addresses are absolute,
 * with the image based at 0x120000, as in the project's notes and maps
 * (analysis/maps/functions.tsv).
 *
 * Hooks. hook(va, fn, &original) makes calls to the lifted function at va
 * run fn instead. fn receives the CPU state at function entry, Win64
 * convention: arguments in rcx, rdx, r8, r9 (xmm0-3 for floats), then on
 * the guest stack above the return address at [rsp] and 32 bytes of home
 * space; the result goes in rax (xmm0). fn either
 *   - calls *original(cpu), which runs the rest of the chain and returns
 *     to the caller (pops the return address); fn may then read or change
 *     rax, or
 *   - replaces the function: sets the result and calls api->ret(cpu),
 *     which pops the return address as a `ret` would.
 * Exactly one of the two must happen on every path.
 *
 * Coverage (version 1): a hook on any lifted function's entry catches
 * every call, through the dispatcher (function pointers, C++ virtual
 * calls, callbacks, thread entry points, window procedures) and direct
 * calls between lifted functions alike: the lift puts an entry-hook check
 * at the start of every function (manifest `entry_hooks`). A VA that is
 * not a lifted entry falls back to the dispatcher alone, which misses
 * direct calls; hook_covers_direct returns 1 when the hook covers both.
 * For the functions in src/direct_hooks.h the hook runs before the
 * project's own wrapper code; for other `replace` functions it runs after
 * the wrapper's code, in place of the lifted body.
 *
 * C++ modules can use include/ffxii_mod.hpp instead: typed arguments and
 * guest memory, Hook<VA, fn>() with a next() for the original, Return()
 * for replacing hooks, printf-style Log and FFXII_MOD_INIT (example:
 * examples/native-cpp-sample/axis_log.cpp).
 *
 * Threads. Hooks run on whatever guest thread made the call, so module
 * state shared between calls needs its own locking. Install hooks only in
 * ffxii_mod_init.
 */
#ifndef FFXII_MOD_H
#define FFXII_MOD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FFXII_MOD_ABI 1

#if defined(_WIN32)
#define FFXII_MOD_EXPORT __declspec(dllexport)
#else
#define FFXII_MOD_EXPORT __attribute__((visibility("default")))
#endif

typedef struct ffxii_cpu ffxii_cpu; /* opaque guest CPU state */
typedef void (*ffxii_fn)(ffxii_cpu* cpu);

enum ffxii_reg {
  FFXII_RAX, FFXII_RCX, FFXII_RDX, FFXII_RBX, FFXII_RSP, FFXII_RBP, FFXII_RSI, FFXII_RDI,
  FFXII_R8, FFXII_R9, FFXII_R10, FFXII_R11, FFXII_R12, FFXII_R13, FFXII_R14, FFXII_R15,
  FFXII_REG_COUNT
};

typedef struct ffxii_api ffxii_api;

struct ffxii_api {
  uint32_t abi;  /* FFXII_MOD_ABI of the recomp */
  uint32_t size; /* sizeof(ffxii_api) of the recomp; later versions only append */
  const char* game_version; /* "1.0.4.0" */
  const char* mod_id;       /* the mod.ini id of the mod loading this module */
  const char* mod_dir;      /* host path of the mod folder (UTF-8) */

  /* Integer registers (enum ffxii_reg). */
  uint64_t (*reg)(const ffxii_cpu* cpu, int reg);
  void (*set_reg)(ffxii_cpu* cpu, int reg, uint64_t value);
  /* xmm0-15 as 16 bytes each (floats: lane 0, little-endian). */
  void (*xmm)(const ffxii_cpu* cpu, int index, void* out16);
  void (*set_xmm)(ffxii_cpu* cpu, int index, const void* in16);

  /* Host pointer to guest address va (no bounds check beyond the arena). */
  void* (*guest_ptr)(uint64_t va);

  /* See "Hooks" above. Returns 0, or -1 when va is not the entry of a
   * lifted function (nothing changes). *original receives the function the
   * hook replaced; it is never null on success. */
  int (*hook)(uint64_t va, ffxii_fn fn, ffxii_fn* original);
  /* 1 when a hook on va also catches direct calls (every lifted entry). */
  int (*hook_covers_direct)(uint64_t va);
  /* Finishes a replacing hook: pops the return address. */
  void (*ret)(ffxii_cpu* cpu);
  /* Calls the guest function at va with up to 8 integer arguments (Win64
   * convention, through the dispatcher, so hooks on va run) and returns
   * rax. cpu must be the state the current hook received. */
  uint64_t (*call)(ffxii_cpu* cpu, uint64_t va, const uint64_t* args, uint32_t count);

  /* One line to the recomp's log, prefixed with the mod id. */
  void (*log)(const ffxii_api* api, const char* message);
};

/* The one export the recomp looks for. */
typedef int (*ffxii_mod_init_fn)(const ffxii_api* api);

#ifdef __cplusplus
}
#endif

#endif /* FFXII_MOD_H */
