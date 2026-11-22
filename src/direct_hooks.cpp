// Direct-call coverage for native-module hooks; see direct_hooks.h.

#include "direct_hooks.h"

#include <cstdio>

// Functions in FFXII_DIRECT_HOOKS with no other project wrapper: their
// wrapper only offers the hook, then runs the lifted body.
#define FFXII_DIRECT_PLAIN(X) X(00244F50)

namespace recomp::gen {
#define FFXII_DECLARE(hex, what) void f_##hex(::recomp::Ctx& c);
FFXII_DIRECT_HOOKS(FFXII_DECLARE)
#undef FFXII_DECLARE

#define FFXII_PLAIN(hex)                          \
  void f_##hex##_lifted(::recomp::Ctx& c);        \
  void f_##hex(::recomp::Ctx& c) {                \
    if (::ffxii::direct::Enter<0x##hex>(c)) return; \
    f_##hex##_lifted(c);                          \
  }
FFXII_DIRECT_PLAIN(FFXII_PLAIN)
#undef FFXII_PLAIN
}  // namespace recomp::gen

namespace ffxii::direct {
namespace {

// The module's original: skip the hook once, then run the wrapper's body.
#define FFXII_TRAMPOLINE(hex, what)       \
  void Trampoline_##hex(recomp::Ctx& c) { \
    g_skip = 0x##hex;                     \
    ::recomp::gen::f_##hex(c);            \
  }
FFXII_DIRECT_HOOKS(FFXII_TRAMPOLINE)
#undef FFXII_TRAMPOLINE

struct Entry {
  uint32_t va;
  Slot* slot;
  recomp::GuestFn wrapper;
  recomp::GuestFn trampoline;
  const char* what;
};

#define FFXII_ENTRY(hex, what) {0x##hex, &g_slot<0x##hex>, ::recomp::gen::f_##hex, Trampoline_##hex, what},
const Entry kEntries[] = {FFXII_DIRECT_HOOKS(FFXII_ENTRY)};
#undef FFXII_ENTRY

const Entry* Find(uint64_t va) {
  for (const Entry& e : kEntries)
    if (e.va == va) return &e;
  return nullptr;
}

}  // namespace

bool Covers(uint64_t va) { return Find(va) != nullptr; }

recomp::GuestFn Install(uint64_t va, recomp::GuestFn fn, recomp::GuestFn prev) {
  const Entry* e = Find(va);
  if (!e) return prev;
  e->slot->head.store(fn, std::memory_order_release);
  std::fprintf(stderr, "[native] %08llX (%s) also catches direct calls\n", static_cast<unsigned long long>(va),
               e->what);
  // The first module's original would be the wrapper itself, which would
  // call the hook again; give it the trampoline. Later modules chain to the
  // module before them, as through the dispatcher.
  return prev == e->wrapper ? e->trampoline : prev;
}

}  // namespace ffxii::direct
