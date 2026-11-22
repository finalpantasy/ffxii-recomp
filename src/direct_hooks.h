#pragma once
// Native-module hooks that also catch direct calls.
//
// A lifted function calls another lifted function as a plain C++ call, so a
// hook installed through the dispatcher (ReplaceDispatch) misses those
// calls. Each function in FFXII_DIRECT_HOOKS has a `replace` wrapper (in
// analysis/maps/lifter.manifest) that first calls direct::Enter<VA>: when a
// module hooked VA, Enter runs that hook and the wrapper returns. The
// module's `original` is a trampoline that sets a one-shot skip flag for VA
// and calls the wrapper again, which then runs its own body. Recursion
// through the original is hooked again, as the flag clears on entry.
//
// Adding a function: a `replace` line in the manifest, an entry here, and
// either an existing project wrapper that starts with Enter<VA> or a plain
// one from FFXII_DIRECT_PLAIN in direct_hooks.cpp.

#include <atomic>
#include <cstdint>

#include "x86/lifted.h"

// X(hex, what): every direct-hookable function, by its VA in hex digits
// (pasted into 0x<hex> and f_<hex>).
#define FFXII_DIRECT_HOOKS(X)                         \
  X(0022A770, "field frame handler")                \
  X(002507C0, "field driver")                       \
  X(00250540, "main driver")                        \
  X(00259240, "frame body step")                    \
  X(002E0FA0, "field state flags")                  \
  X(002647D0, "talk target")                        \
  X(00248030, "task list run")                      \
  X(00244F50, "task create")

namespace ffxii::direct {

struct Slot {
  std::atomic<recomp::GuestFn> head{nullptr};  // the outermost module hook
};

template <uint32_t VA>
inline Slot g_slot;

// The VA whose next wrapper entry skips the module hook (one per thread).
inline thread_local uint32_t g_skip = 0;

// First statement of a direct-hookable wrapper: true when a module's hook
// ran in its place (the wrapper must then return at once).
template <uint32_t VA>
inline bool Enter(recomp::Ctx& c) {
  if (g_skip == VA) {
    g_skip = 0;
    return false;
  }
  recomp::GuestFn h = g_slot<VA>.head.load(std::memory_order_acquire);
  if (!h) return false;
  h(c);
  return true;
}

// Whether VA is direct-hookable.
bool Covers(uint64_t va);
// Records fn as VA's outermost hook. `prev` is what the dispatcher had for
// VA; the result is what the module should call as its original.
recomp::GuestFn Install(uint64_t va, recomp::GuestFn fn, recomp::GuestFn prev);

}  // namespace ffxii::direct
