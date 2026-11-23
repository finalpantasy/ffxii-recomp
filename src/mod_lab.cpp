// Mod lab (mod_lab.h): a stand-in game, the lab runner and its JSON.
//
// The stand-in game has three functions (examples/modlab/lab_targets.h) on
// a fake CPU state and a 1 MiB guest arena. Its ffxii_api has the same
// shape and rules as the game's (src/native_mods.cpp): hook() swaps the
// entry of a chain and hands back the previous one, ret() pops the return
// address. Each lab entry (a native mod, or a Luau mod in one code mode)
// loads into a fresh chain, then every test calls the chain `iters` times
// per repeat with the same 4096 inputs, checking each result and the stack.

#include "mod_lab.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#if defined(__x86_64__) || defined(_M_X64)
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

#include "../examples/modlab/lab_targets.h"
#include "ffxii_mod.h"
#include "luau_mods.h"
#include "mod_index.h"

namespace ffxii::modlab {
namespace {

// ---------------------------------------------------------------- stand-in game

struct LabCpu {
  uint64_t r[FFXII_REG_COUNT];
  uint8_t x[16][16];
};
LabCpu& C(ffxii_cpu* c) { return *reinterpret_cast<LabCpu*>(c); }
const LabCpu& C(const ffxii_cpu* c) { return *reinterpret_cast<const LabCpu*>(c); }

constexpr uint64_t kArena = 1u << 20;
constexpr uint64_t kStack = 0x80000;
constexpr int kInputs = 4096;
enum Test { kDamage, kLookup, kScore, kTests };
constexpr uint64_t kVa[kTests] = {LAB_DAMAGE, LAB_LOOKUP, LAB_SCORE};
const char* const kTestId[kTests] = {"damage", "lookup", "score"};
const char* const kTestTitle[kTests] = {"Filter hook", "Replace + memory read", "Replace + loop"};
const char* const kTestDetail[kTests] = {
    "calls the original (rcx*3+rdx), then rax = rax*2+1",
    "rax = u16 table[rcx % 4096] * 3 + 7, then returns",
    "rax = sum of 32 table reads times (k+1), then returns"};

std::vector<uint8_t> g_arena(kArena);
ffxii_fn volatile g_entry[kTests];
std::mutex g_log_mu;
std::vector<std::string> g_log;

uint16_t Tab(uint64_t i) {
  uint16_t v;
  std::memcpy(&v, g_arena.data() + LAB_TABLE + (i % 4096) * 2, 2);
  return v;
}

void VDamage(ffxii_cpu* c) {
  LabCpu& x = C(c);
  x.r[FFXII_RAX] = x.r[FFXII_RCX] * 3 + x.r[FFXII_RDX];
  x.r[FFXII_RSP] += 8;
}
void VLookup(ffxii_cpu* c) {
  LabCpu& x = C(c);
  x.r[FFXII_RAX] = Tab(x.r[FFXII_RCX]);
  x.r[FFXII_RSP] += 8;
}
void VScore(ffxii_cpu* c) {
  LabCpu& x = C(c);
  x.r[FFXII_RAX] = 0;
  x.r[FFXII_RSP] += 8;
}
constexpr ffxii_fn kVanilla[kTests] = {VDamage, VLookup, VScore};

void ResetChains() {
  for (int t = 0; t < kTests; t++) g_entry[t] = kVanilla[t];
}

uint64_t ApiReg(const ffxii_cpu* c, int r) { return r >= 0 && r < FFXII_REG_COUNT ? C(c).r[r] : 0; }
void ApiSetReg(ffxii_cpu* c, int r, uint64_t v) {
  if (r >= 0 && r < FFXII_REG_COUNT) C(c).r[r] = v;
}
void ApiXmm(const ffxii_cpu* c, int i, void* out) {
  if (i >= 0 && i < 16) std::memcpy(out, C(c).x[i], 16);
}
void ApiSetXmm(ffxii_cpu* c, int i, const void* in) {
  if (i >= 0 && i < 16) std::memcpy(C(c).x[i], in, 16);
}
void* ApiGuestPtr(uint64_t va) { return g_arena.data() + (va & (kArena - 1)); }
int ApiHook(uint64_t va, ffxii_fn fn, ffxii_fn* original) {
  for (int t = 0; t < kTests; t++)
    if (kVa[t] == va) {
      if (original) *original = g_entry[t];
      g_entry[t] = fn;
      return 0;
    }
  return -1;
}
int ApiCovers(uint64_t va) { return ApiHook(va, nullptr, nullptr) == -1 ? 0 : 1; }
void ApiRet(ffxii_cpu* c) { C(c).r[FFXII_RSP] += 8; }
uint64_t ApiCall(ffxii_cpu* c, uint64_t va, const uint64_t* a, uint32_t n) {
  for (int t = 0; t < kTests; t++)
    if (kVa[t] == va) {
      const LabCpu saved = C(c);
      static constexpr int kArgs[4] = {FFXII_RCX, FFXII_RDX, FFXII_R8, FFXII_R9};
      for (uint32_t k = 0; k < n && k < 4; k++) C(c).r[kArgs[k]] = a[k];
      C(c).r[FFXII_RSP] -= 8;
      g_entry[t](c);
      const uint64_t rax = C(c).r[FFXII_RAX];
      C(c) = saved;
      return rax;
    }
  return 0;
}
void ApiLog(const ffxii_api* api, const char* msg) {
  std::lock_guard<std::mutex> lock(g_log_mu);
  g_log.push_back(std::string(api && api->mod_id ? api->mod_id : "?") + ": " + (msg ? msg : ""));
}

// ---------------------------------------------------------------- inputs

struct Inputs {
  uint64_t a[kInputs], b[kInputs];
  uint64_t want[kTests][kInputs];     // with the lab mod
  uint64_t vanilla[kTests][kInputs];  // without
};

Inputs& MakeInputs() {
  static Inputs in;
  static bool done = false;
  if (done) return in;
  uint64_t s = 0x9E3779B97F4A7C15ull;
  auto next = [&] {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
  };
  for (int i = 0; i < 4096; i++) {
    const uint16_t v = uint16_t(next());
    std::memcpy(g_arena.data() + LAB_TABLE + i * 2, &v, 2);
  }
  for (int i = 0; i < kInputs; i++) {
    in.a[i] = next() & 0xFFFFFFFF;
    in.b[i] = next() & 0xFFFFFFFF;
    const uint64_t x = in.a[i];
    in.vanilla[kDamage][i] = x * 3 + in.b[i];
    in.want[kDamage][i] = in.vanilla[kDamage][i] * 2 + 1;
    in.vanilla[kLookup][i] = Tab(x);
    in.want[kLookup][i] = uint64_t(Tab(x)) * 3 + 7;
    in.vanilla[kScore][i] = 0;
    uint64_t sum = 0;
    for (uint64_t k = 0; k < 32; k++) sum += uint64_t(Tab(x + 7 * k)) * (k + 1);
    in.want[kScore][i] = sum;
  }
  done = true;
  return in;
}

struct Timing {
  double median_ns = 0, min_ns = 0;
  uint64_t mismatches = 0, calls = 0;
};

// Calls test t's chain iters times per repeat.
Timing Time(int t, const RunOptions& opt, const uint64_t* want) {
  const Inputs& in = MakeInputs();
  std::vector<double> ns;
  Timing r;
  LabCpu cpu{};
  ffxii_cpu* raw = reinterpret_cast<ffxii_cpu*>(&cpu);
  for (int rep = 0; rep < std::max(1, opt.reps); rep++) {
    uint64_t bad = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < opt.iters; i++) {
      const size_t j = i & (kInputs - 1);
      cpu.r[FFXII_RSP] = kStack - 8;  // the caller's call pushed a return address
      cpu.r[FFXII_RCX] = in.a[j];
      cpu.r[FFXII_RDX] = in.b[j];
      g_entry[t](raw);
      bad += (cpu.r[FFXII_RAX] != want[j]) | (cpu.r[FFXII_RSP] != kStack);
    }
    const auto t1 = std::chrono::steady_clock::now();
    ns.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / double(opt.iters));
    r.mismatches += bad;
    r.calls += opt.iters;
  }
  std::sort(ns.begin(), ns.end());
  r.median_ns = ns[ns.size() / 2];
  r.min_ns = ns.front();
  return r;
}

// ---------------------------------------------------------------- lab entries

struct Entry {
  std::string key, id, name, language, runtime;
  std::vector<std::string> files;  // host paths
  bool script = false, native_code = false;
  std::string mod_dir;
};

struct Lab {
  std::vector<Entry> entries;
  std::vector<std::string> errors;
};

Lab Discover(const std::string& dir) {
  Lab lab;
  std::error_code ec;
  if (!std::filesystem::is_directory(std::filesystem::u8path(dir), ec)) {
    lab.errors.push_back("lab folder not found: " + dir);
    return lab;
  }
  const mods::Index ix = mods::Build(dir);
  for (const auto& e : ix.errors) lab.errors.push_back(e);
  std::vector<const mods::ModInfo*> order;
  for (const auto& m : ix.mods)
    if (m.enabled && m.id != "loose") order.push_back(&m);
  std::sort(order.begin(), order.end(), [](auto* a, auto* b) { return a->id < b->id; });
  for (const mods::ModInfo* m : order) {
    if (!m->native.empty()) {
      Entry e;
      e.key = m->id;
      e.id = m->id;
      e.name = m->name;
      e.language = m->language.empty() ? "native" : m->language;
      e.runtime = "native DLL";
      e.mod_dir = m->root;
      for (const auto& f : m->native) e.files.push_back(m->root + "/" + f);
      lab.entries.push_back(e);
    }
    if (!m->scripts.empty()) {
      Entry e;
      e.id = m->id;
      e.name = m->name;
      e.language = m->language.empty() ? "Luau" : m->language;
      e.script = true;
      e.mod_dir = m->root;
      for (const auto& f : m->scripts) e.files.push_back(m->root + "/" + f);
      if (luau::NativeSupported()) {
        e.key = m->id + ":native";
        e.runtime = "Luau native code";
        e.native_code = true;
        lab.entries.push_back(e);
      }
      e.key = m->id + ":interp";
      e.runtime = "Luau interpreter";
      e.native_code = false;
      lab.entries.push_back(e);
    }
  }
  return lab;
}

void* OpenLib(const std::string& path, std::string& err) {
  static std::map<std::string, void*> cache;
  if (auto it = cache.find(path); it != cache.end()) return it->second;
#if defined(_WIN32)
  HMODULE h = LoadLibraryExW(std::filesystem::u8path(path).wstring().c_str(), nullptr,
                             LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!h) err = "LoadLibrary error " + std::to_string(GetLastError());
  void* p = reinterpret_cast<void*>(h);
#else
  void* p = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!p) err = dlerror();
#endif
  if (p) cache[path] = p;
  return p;
}

void* Sym(void* lib, const char* name) {
#if defined(_WIN32)
  return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
#else
  return dlsym(lib, name);
#endif
}

ffxii_api* ApiFor(const Entry& e) {
  static std::deque<std::pair<std::string, std::string>> names;
  static std::deque<ffxii_api> apis;
  names.emplace_back(e.id, e.mod_dir);
  ffxii_api& a = apis.emplace_back();
  a.abi = FFXII_MOD_ABI;
  a.size = sizeof(ffxii_api);
  a.game_version = "mod lab";
  a.mod_id = names.back().first.c_str();
  a.mod_dir = names.back().second.c_str();
  a.reg = ApiReg;
  a.set_reg = ApiSetReg;
  a.xmm = ApiXmm;
  a.set_xmm = ApiSetXmm;
  a.guest_ptr = ApiGuestPtr;
  a.hook = ApiHook;
  a.hook_covers_direct = ApiCovers;
  a.ret = ApiRet;
  a.call = ApiCall;
  a.log = ApiLog;
  return &a;
}

// ---------------------------------------------------------------- JSON

std::string U8(const std::filesystem::path& p) {
  const auto s = p.u8string();
  return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

std::string Q(const std::string& s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20) {
          char b[8];
          std::snprintf(b, sizeof b, "\\u%04x", c);
          o += b;
        } else {
          o += char(c);
        }
    }
  }
  return o + "\"";
}

std::string Num(double v) {
  char b[32];
  std::snprintf(b, sizeof b, "%.3f", v);
  return b;
}

std::string CpuName() {
#if defined(__x86_64__) || defined(_M_X64)
  unsigned int r[12] = {};
  for (unsigned int k = 0; k < 3; k++) {
#if defined(_MSC_VER)
    int x[4];
    __cpuid(x, int(0x80000002 + k));
    std::memcpy(r + 4 * k, x, 16);
#else
    __get_cpuid(0x80000002 + k, &r[4 * k], &r[4 * k + 1], &r[4 * k + 2], &r[4 * k + 3]);
#endif
  }
  std::string s(reinterpret_cast<const char*>(r), 48);
  s = s.c_str();
  while (!s.empty() && s.back() == ' ') s.pop_back();
  while (!s.empty() && s.front() == ' ') s.erase(0, 1);
  return s;
#else
  return "";
#endif
}

// Whole-machine CPU use while the lab ran, in percent (-1 if unknown).
struct LoadMeter {
#if defined(_WIN32)
  FILETIME idle0{}, kernel0{}, user0{};
  LoadMeter() { GetSystemTimes(&idle0, &kernel0, &user0); }
  static uint64_t U(const FILETIME& f) { return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; }
  double Percent() const {
    FILETIME i, k, u;
    if (!GetSystemTimes(&i, &k, &u)) return -1;
    const double idle = double(U(i) - U(idle0));
    const double total = double(U(k) - U(kernel0) + U(u) - U(user0));  // kernel includes idle
    return total > 0 ? 100.0 * (1.0 - idle / total) : -1;
  }
#else
  double Percent() const { return -1; }
#endif
};

}  // namespace

std::string ListJson(const std::string& dir) {
  const Lab lab = Discover(dir);
  std::ostringstream o;
  o << "{\"dir\":" << Q(dir) << ",\"luau_native\":" << (luau::NativeSupported() ? "true" : "false")
    << ",\"cpu\":" << Q(CpuName()) << ",\"tests\":[";
  for (int t = 0; t < kTests; t++)
    o << (t ? "," : "") << "{\"id\":" << Q(kTestId[t]) << ",\"title\":" << Q(kTestTitle[t])
      << ",\"detail\":" << Q(kTestDetail[t]) << "}";
  o << "],\"entries\":[";
  for (size_t i = 0; i < lab.entries.size(); i++) {
    const Entry& e = lab.entries[i];
    o << (i ? "," : "") << "{\"key\":" << Q(e.key) << ",\"id\":" << Q(e.id) << ",\"name\":" << Q(e.name)
      << ",\"language\":" << Q(e.language) << ",\"runtime\":" << Q(e.runtime) << ",\"files\":[";
    for (size_t k = 0; k < e.files.size(); k++)
      o << (k ? "," : "") << Q(U8(std::filesystem::u8path(e.files[k]).filename()));
    o << "]}";
  }
  o << "],\"errors\":[";
  for (size_t i = 0; i < lab.errors.size(); i++) o << (i ? "," : "") << Q(lab.errors[i]);
  o << "]}";
  return o.str();
}

std::string RunJson(const std::string& dir, const RunOptions& opt) {
  const Lab lab = Discover(dir);
  MakeInputs();
  const LoadMeter meter;
  const auto started = std::chrono::steady_clock::now();
  std::ostringstream o;
  o << "{\"iters\":" << opt.iters << ",\"reps\":" << opt.reps << ",\"cpu\":" << Q(CpuName());

  // The stand-in game alone: what every hook is measured against.
  ResetChains();
  o << ",\"vanilla\":{";
  for (int t = 0; t < kTests; t++) {
    const Timing v = Time(t, opt, MakeInputs().vanilla[t]);
    o << (t ? "," : "") << Q(kTestId[t]) << ":{\"ns\":" << Num(v.median_ns) << ",\"min_ns\":" << Num(v.min_ns)
      << ",\"mismatches\":" << v.mismatches << "}";
  }
  o << "},\"entries\":[";
  bool first = true;
  for (const Entry& e : lab.entries) {
    if (!opt.only.empty() && std::find(opt.only.begin(), opt.only.end(), e.key) == opt.only.end()) continue;
    {
      std::lock_guard<std::mutex> lock(g_log_mu);
      g_log.clear();
    }
    ResetChains();
    ffxii_api* api = ApiFor(e);
    std::string err;
    bool ok = true;
    std::shared_ptr<luau::Mod> script;
    const auto l0 = std::chrono::steady_clock::now();
    if (e.script) {
      luau::Options lo;
      lo.native = e.native_code;
      script = luau::Load(api, e.files, lo, err);
      ok = script != nullptr;
    } else {
      for (const auto& f : e.files) {
        void* lib = OpenLib(f, err);
        auto init = lib ? reinterpret_cast<ffxii_mod_init_fn>(Sym(lib, "ffxii_mod_init")) : nullptr;
        if (lib && !init) err = "no ffxii_mod_init export";
        if (!init || init(api) != 0) {
          if (err.empty()) err = "ffxii_mod_init failed";
          ok = false;
          break;
        }
      }
    }
    const double load_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - l0).count();
    o << (first ? "" : ",") << "{\"key\":" << Q(e.key) << ",\"name\":" << Q(e.name) << ",\"language\":"
      << Q(e.language) << ",\"runtime\":" << Q(e.runtime) << ",\"ok\":" << (ok ? "true" : "false")
      << ",\"error\":" << Q(err) << ",\"load_ms\":" << Num(load_ms) << ",\"tests\":[";
    first = false;
    for (int t = 0; ok && t < kTests; t++) {
      const bool hooked = g_entry[t] != kVanilla[t];
      const Timing r = Time(t, opt, MakeInputs().want[t]);
      o << (t ? "," : "") << "{\"test\":" << Q(kTestId[t]) << ",\"hooked\":" << (hooked ? "true" : "false")
        << ",\"ns\":" << Num(r.median_ns) << ",\"min_ns\":" << Num(r.min_ns) << ",\"calls\":" << r.calls
        << ",\"mismatches\":" << r.mismatches << "}";
    }
    o << "],\"log\":[";
    {
      std::lock_guard<std::mutex> lock(g_log_mu);
      for (size_t i = 0; i < g_log.size() && i < 50; i++) o << (i ? "," : "") << Q(g_log[i]);
    }
    o << "]}";
    ResetChains();
    luau::Unload(script);
  }
  const double secs =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  o << "],\"errors\":[";
  for (size_t i = 0; i < lab.errors.size(); i++) o << (i ? "," : "") << Q(lab.errors[i]);
  o << "],\"seconds\":" << Num(secs) << ",\"machine_load\":" << Num(meter.Percent()) << "}";
  return o.str();
}

}  // namespace ffxii::modlab
