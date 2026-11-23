// Native module loader (include/ffxii_mod.h): loads the DLLs that enabled
// mods declare with `native = ...` in mod.ini and gives each the version 1
// API. Runs from GameConfig::before_entry, after dispatch is set up and
// before the game's entry point, so hooks are in place before any guest
// code runs.

#include "native_mods.h"

#include "direct_hooks.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "ffxii_mod.h"
#include "luau_mods.h"
#include "mod_index.h"
#include "runtime/env.h"
#include "runtime/platform.h"
#include "x86/ctx.h"
#include "x86/lifted.h"
#include "x86/runtime.h"

namespace {

using recomp::Ctx;

// ffxii_cpu is Ctx; ffxii_fn(ffxii_cpu*) and recomp's GuestFn(Ctx&) are the
// same machine-level function (one pointer argument), so hooks go into the
// dispatch table as they are and originals come back the same way.
Ctx& C(ffxii_cpu* p) { return *reinterpret_cast<Ctx*>(p); }
const Ctx& C(const ffxii_cpu* p) { return *reinterpret_cast<const Ctx*>(p); }
using GuestFn = void (*)(Ctx&);

uint64_t* RegPtr(Ctx& c, int r) {
  switch (r) {
    case FFXII_RAX: return &c.rax;
    case FFXII_RCX: return &c.rcx;
    case FFXII_RDX: return &c.rdx;
    case FFXII_RBX: return &c.rbx;
    case FFXII_RSP: return &c.rsp;
    case FFXII_RBP: return &c.rbp;
    case FFXII_RSI: return &c.rsi;
    case FFXII_RDI: return &c.rdi;
    case FFXII_R8: return &c.r8;
    case FFXII_R9: return &c.r9;
    case FFXII_R10: return &c.r10;
    case FFXII_R11: return &c.r11;
    case FFXII_R12: return &c.r12;
    case FFXII_R13: return &c.r13;
    case FFXII_R14: return &c.r14;
    case FFXII_R15: return &c.r15;
    default: return nullptr;
  }
}

uint64_t ApiReg(const ffxii_cpu* cpu, int r) {
  const uint64_t* p = RegPtr(const_cast<Ctx&>(C(cpu)), r);
  return p ? *p : 0;
}
void ApiSetReg(ffxii_cpu* cpu, int r, uint64_t v) {
  if (uint64_t* p = RegPtr(C(cpu), r)) *p = v;
}
void ApiXmm(const ffxii_cpu* cpu, int i, void* out) {
  if (i >= 0 && i < 16) std::memcpy(out, C(cpu).xmm[i].b, 16);
}
void ApiSetXmm(ffxii_cpu* cpu, int i, const void* in) {
  if (i >= 0 && i < 16) std::memcpy(C(cpu).xmm[i].b, in, 16);
}
void* ApiGuestPtr(uint64_t va) { return recomp::g_guest_base + va; }

int ApiHook(uint64_t va, ffxii_fn fn, ffxii_fn* original) {
  GuestFn hook = reinterpret_cast<GuestFn>(fn);
  GuestFn prev = nullptr;
  if (ffxii::direct::Covers(va)) {
    // Project wrappers that offer the hook before their own code.
    prev = recomp::ReplaceDispatch(va, hook);
    if (!prev) return -1;
    prev = ffxii::direct::Install(va, hook, prev);
  } else if (!recomp::SetEntryHook(va, hook, &prev)) {
    // No entry hook (not a lifted entry, or the lift has none): the
    // dispatcher alone, which misses direct calls.
    prev = recomp::ReplaceDispatch(va, hook);
    if (!prev) return -1;
  }
  // An entry hook needs no dispatcher change: dispatched calls reach the
  // lifted function, whose entry runs the hook.
  if (original) *original = reinterpret_cast<ffxii_fn>(prev);
  std::fprintf(stderr, "[native] hook %08llX\n", static_cast<unsigned long long>(va));
  return 0;
}
int ApiCoversDirect(uint64_t va) {
  if (ffxii::direct::Covers(va)) return 1;
  if (!recomp::kLiftedEntryHooks) return 0;
  const recomp::LiftedFn* end = recomp::kLiftedFunctions + recomp::kLiftedFunctionCount;
  const recomp::LiftedFn* it = std::lower_bound(
      recomp::kLiftedFunctions, end, va,
      [](const recomp::LiftedFn& f, uint64_t v) { return f.va < v; });
  return it != end && it->va == va ? 1 : 0;
}
void ApiRet(ffxii_cpu* cpu) { C(cpu).rsp += 8; }

uint64_t ApiCall(ffxii_cpu* cpu, uint64_t va, const uint64_t* a, uint32_t n) {
  Ctx& c = C(cpu);
  switch (n) {
    case 0: return recomp::CallGuest(c, va, {});
    case 1: return recomp::CallGuest(c, va, {a[0]});
    case 2: return recomp::CallGuest(c, va, {a[0], a[1]});
    case 3: return recomp::CallGuest(c, va, {a[0], a[1], a[2]});
    case 4: return recomp::CallGuest(c, va, {a[0], a[1], a[2], a[3]});
    case 5: return recomp::CallGuest(c, va, {a[0], a[1], a[2], a[3], a[4]});
    case 6: return recomp::CallGuest(c, va, {a[0], a[1], a[2], a[3], a[4], a[5]});
    case 7: return recomp::CallGuest(c, va, {a[0], a[1], a[2], a[3], a[4], a[5], a[6]});
    default: return recomp::CallGuest(c, va, {a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]});
  }
}

void ApiLog(const ffxii_api* api, const char* msg) {
  std::fprintf(stderr, "[mod %s] %s\n", api && api->mod_id ? api->mod_id : "?", msg ? msg : "");
}

// Per-module API copies (mod_id and mod_dir differ) and their strings,
// alive for the whole process.
struct Loaded {
  std::string id, dir;
  ffxii_api api{};
};
std::deque<Loaded> g_loaded;

void* OpenLib(const std::string& path, std::string* err) {
#if defined(_WIN32)
  // The DLL's own dependencies resolve from its folder first.
  HMODULE h = LoadLibraryExW(std::filesystem::u8path(path).wstring().c_str(), nullptr,
                             LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!h) *err = "LoadLibrary error " + std::to_string(GetLastError());
  return reinterpret_cast<void*>(h);
#else
  void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!h) *err = dlerror();
  return h;
#endif
}
void* Sym(void* lib, const char* name) {
#if defined(_WIN32)
  return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
#else
  return dlsym(lib, name);
#endif
}
void CloseLib(void* lib) {
#if defined(_WIN32)
  FreeLibrary(reinterpret_cast<HMODULE>(lib));
#else
  dlclose(lib);
#endif
}

}  // namespace

namespace ffxii {

namespace {

// The api a mod's modules and scripts receive; it lives as long as the process.
const ffxii_api* ApiFor(const mods::ModInfo& m) {
  Loaded& l = g_loaded.emplace_back();
  l.id = m.id;
  l.dir = m.root;
  ffxii_api& a = l.api;
  a.abi = FFXII_MOD_ABI;
  a.size = sizeof(ffxii_api);
  a.game_version = "1.0.4.0";
  a.mod_id = l.id.c_str();
  a.mod_dir = l.dir.c_str();
  a.reg = ApiReg;
  a.set_reg = ApiSetReg;
  a.xmm = ApiXmm;
  a.set_xmm = ApiSetXmm;
  a.guest_ptr = ApiGuestPtr;
  a.hook = ApiHook;
  a.hook_covers_direct = ApiCoversDirect;
  a.ret = ApiRet;
  a.call = ApiCall;
  a.log = ApiLog;
  return &a;
}

}  // namespace

void LoadNativeModules() {
  const char* no_native = recomp::Env("MODS_NO_NATIVE");
  const bool native = !(no_native && *no_native && *no_native != '0');
  if (!native) std::fprintf(stderr, "[native] native modules off\n");
  const char* interp = recomp::Env("LUAU_INTERP");
  luau::Options lopt;
  lopt.native = !(interp && *interp && *interp != '0');
  const char* dir = recomp::Env("MODS_DIR");
  const char* off = recomp::Env("MODS_DISABLE");
  const std::string root = mods::ResolveRoot(dir ? dir : "", recomp::platform::ExeDir(),
                                             recomp::g_proc->game_dir);
  const mods::Index index = mods::Build(root, mods::ParseIdList(off ? off : ""));
  for (size_t i : mods::NativeLoadOrder(index)) {
    const mods::ModInfo& m = index.mods[i];
    const ffxii_api* api = nullptr;
    for (const std::string& rel : native ? m.native : std::vector<std::string>{}) {
      const std::string path = m.root + "/" + rel;
      std::string err;
      void* lib = OpenLib(path, &err);
      if (!lib) {
        std::fprintf(stderr, "[native] %s: cannot load %s: %s\n", m.id.c_str(), path.c_str(),
                     err.c_str());
        continue;
      }
      auto init = reinterpret_cast<ffxii_mod_init_fn>(Sym(lib, "ffxii_mod_init"));
      if (!init) {
        std::fprintf(stderr, "[native] %s: %s exports no ffxii_mod_init\n", m.id.c_str(),
                     rel.c_str());
        CloseLib(lib);
        continue;
      }
      if (!api) api = ApiFor(m);
      const int rc = init(api);
      // A failed module stays loaded: hooks it installed may point into it.
      std::fprintf(stderr, "[native] %s: %s init -> %d%s\n", m.id.c_str(), rel.c_str(), rc,
                   rc ? " (failed)" : "");
    }
    // Scripts are sandboxed, so --no-native-mods leaves them on.
    if (!m.scripts.empty()) {
      if (!api) api = ApiFor(m);
      std::vector<std::string> files;
      for (const std::string& rel : m.scripts) files.push_back(m.root + "/" + rel);
      std::string err;
      const bool ok = luau::Load(api, files, lopt, err) != nullptr;
      std::fprintf(stderr, "[luau] %s: %zu scripts%s%s%s\n", m.id.c_str(), files.size(),
                   lopt.native && luau::NativeSupported() ? " (native code)" : " (interpreter)",
                   ok ? "" : " failed: ", err.c_str());
    }
  }
}

}  // namespace ffxii
