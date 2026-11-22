// Write watch (FFXII_WATCH=<guest hex addr>[,<len hex>[,<start frame dec>]], Windows hosts).
// Makes the guest page read-only; every write to it faults into a vectored
// handler, which logs each distinct host RIP that writes inside
// [addr, addr+len) (default len 0x80), lets the write through with the trap
// flag set, and re-protects the page on the single-step. Map the logged RIPs
// to lifted functions with nm on the exe. Debug-only: it slows writes to that page.

#ifdef _WIN32
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>

#include "runtime/env.h"
#include "x86/lifted.h"
#include "x86/runtime.h"

namespace recomp::ffxii {
extern bool g_skip_frame;
}

namespace {

uintptr_t g_page = 0, g_lo = 0, g_hi = 0;
thread_local bool t_stepping = false;
SRWLOCK g_lock = SRWLOCK_INIT;
std::set<uintptr_t>* g_seen = nullptr;

void Protect(DWORD prot) {
  DWORD old;
  VirtualProtect(reinterpret_cast<void*>(g_page), 4096, prot, &old);
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* e) {
  const DWORD code = e->ExceptionRecord->ExceptionCode;
  if (code == EXCEPTION_ACCESS_VIOLATION && e->ExceptionRecord->NumberParameters >= 2 &&
      e->ExceptionRecord->ExceptionInformation[0] == 1) {
    const uintptr_t a = e->ExceptionRecord->ExceptionInformation[1];
    if (a < g_page || a >= g_page + 4096) return EXCEPTION_CONTINUE_SEARCH;
    if (a >= g_lo && a < g_hi) {
      const uintptr_t rip = e->ContextRecord->Rip;
      AcquireSRWLockExclusive(&g_lock);
      const bool skip = recomp::ffxii::g_skip_frame;
      const bool fresh = g_seen->insert(rip * 2 + skip).second;
      ReleaseSRWLockExclusive(&g_lock);
      if (fresh)
        std::fprintf(stderr, "[watch] write +0x%llx from host rip 0x%llx%s\n",
                     (unsigned long long)(a - g_lo), (unsigned long long)rip,
                     skip ? " (driver-skipped frame)" : "");
    }
    Protect(PAGE_READWRITE);
    t_stepping = true;
    e->ContextRecord->EFlags |= 0x100;  // trap after the write
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  if (code == EXCEPTION_SINGLE_STEP && t_stepping) {
    t_stepping = false;
    Protect(PAGE_READONLY);
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

namespace recomp::ffxii {

// Called every limited frame (from the frame limiter wrapper); arms the watch
// on the start frame (default 1).
void MaybeStartWatch(uint64_t frame) {
  static bool done = false;
  if (done) return;
  const char* e = recomp::Env("WATCH");
  if (!e) {
    done = true;
    return;
  }
  unsigned long long addr = 0, len = 0x80, start = 1;
  if (std::sscanf(e, "%llx,%llx,%llu", &addr, &len, &start) < 1) {
    done = true;
    return;
  }
  if (frame < start) return;
  done = true;
  g_lo = reinterpret_cast<uintptr_t>(recomp::g_guest_base) + addr;
  g_hi = g_lo + len;
  g_page = g_lo & ~uintptr_t(4095);
  g_seen = new std::set<uintptr_t>;
  AddVectoredExceptionHandler(1, OnException);
  Protect(PAGE_READONLY);
  std::fprintf(stderr, "[watch] guest 0x%llx len 0x%llx, exe base %p\n", addr, len,
               static_cast<void*>(GetModuleHandleW(nullptr)));
}

}  // namespace recomp::ffxii
#else
#include <cstdint>
namespace recomp::ffxii {
void MaybeStartWatch(uint64_t) {}
}  // namespace recomp::ffxii
#endif
