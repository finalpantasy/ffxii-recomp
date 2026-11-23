// Luau script mods (luau_mods.h).

#include "luau_mods.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>

#include "lua.h"
#include "luacode.h"
#include "luacodegen.h"
#include "lualib.h"

namespace ffxii::luau {

namespace {

constexpr int kCpuTag = 1;
constexpr size_t kSlots = 512;  // script hooks across all mods

const char* const kRegNames[FFXII_REG_COUNT] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
                                                "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15"};

uint64_t ToU64(double d) {
  if (!(d == d)) return 0;
  if (d < 0) return d <= -9223372036854775808.0 ? uint64_t(INT64_MIN) : uint64_t(int64_t(d));
  if (d >= 18446744073709551615.0) return UINT64_MAX;
  return uint64_t(d);
}

// The cpu userdata: valid only while its hook runs.
struct CpuUd {
  ffxii_cpu* cpu = nullptr;
  ffxii_fn original = nullptr;
  bool finished = false;  // original(cpu) or ffxii.ret(cpu) happened
};

}  // namespace

class Mod {
 public:
  const ffxii_api* api = nullptr;
  lua_State* L = nullptr;
  std::recursive_mutex mu;
  CpuUd* cpu = nullptr;  // the one cpu userdata, reused by every call
  int cpu_ref = 0;
  std::vector<size_t> slots;
  bool warned = false;

  void Log(const std::string& s) const { api->log(api, s.c_str()); }
};

namespace {

struct Slot {
  std::atomic<Mod*> mod{nullptr};
  int fn_ref = 0;
  int orig_ref = 0;
  ffxii_fn original = nullptr;
};
std::array<Slot, kSlots> g_slots;
std::mutex g_slots_mu;
// Every loaded mod stays alive until Unload: its hooks may be installed
// even when a later script of the same mod failed.
std::vector<std::shared_ptr<Mod>> g_alive;
std::mutex g_alive_mu;

Mod* ModOf(lua_State* L) { return static_cast<Mod*>(lua_callbacks(L)->userdata); }

CpuUd* CheckCpu(lua_State* L, int idx) {
  void* p = lua_touserdatatagged(L, idx, kCpuTag);
  if (!p) luaL_typeerror(L, idx, "cpu");
  CpuUd* u = static_cast<CpuUd*>(p);
  if (!u->cpu) luaL_error(L, "cpu used outside its hook");
  return u;
}

// One hook call: runs the script function with (cpu, original).
void Invoke(size_t i, ffxii_cpu* raw) {
  Slot& s = g_slots[i];
  Mod* m = s.mod.load(std::memory_order_acquire);
  std::lock_guard<std::recursive_mutex> lock(m->mu);
  CpuUd* u = m->cpu;
  const CpuUd saved = *u;
  u->cpu = raw;
  u->original = s.original;
  u->finished = false;
  lua_State* L = m->L;
  lua_rawgeti(L, LUA_REGISTRYINDEX, s.fn_ref);
  lua_rawgeti(L, LUA_REGISTRYINDEX, m->cpu_ref);
  lua_rawgeti(L, LUA_REGISTRYINDEX, s.orig_ref);
  if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
    m->Log(std::string("hook error: ") + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "?"));
    lua_pop(L, 1);
  } else if (!u->finished && !m->warned) {
    m->warned = true;
    m->Log("a hook returned without original(cpu) or ffxii.ret(cpu); the original ran");
  }
  const bool finished = u->finished;
  *u = saved;
  if (!finished) s.original(raw);
}

template <size_t... I>
constexpr std::array<ffxii_fn, sizeof...(I)> MakeThunks(std::index_sequence<I...>) {
  return {{[](ffxii_cpu* c) { Invoke(I, c); }...}};
}
const std::array<ffxii_fn, kSlots> kThunks = MakeThunks(std::make_index_sequence<kSlots>{});

int16_t Atom(lua_State*, const char* s, size_t l) {
  if (l < 2 || l > 3) return -1;
  for (int r = 0; r < FFXII_REG_COUNT; r++)
    if (std::strlen(kRegNames[r]) == l && std::memcmp(kRegNames[r], s, l) == 0) return int16_t(r);
  return -1;
}

int CpuIndex(lua_State* L) {
  CpuUd* u = CheckCpu(L, 1);
  int atom = -1;
  const char* k = lua_tostringatom(L, 2, &atom);
  if (atom < 0 || atom >= FFXII_REG_COUNT) luaL_error(L, "cpu has no field '%s'", k ? k : "?");
  lua_pushnumber(L, double(ModOf(L)->api->reg(u->cpu, atom)));
  return 1;
}

int CpuNewIndex(lua_State* L) {
  CpuUd* u = CheckCpu(L, 1);
  int atom = -1;
  const char* k = lua_tostringatom(L, 2, &atom);
  if (atom < 0 || atom >= FFXII_REG_COUNT) luaL_error(L, "cpu has no register '%s'", k ? k : "?");
  ModOf(L)->api->set_reg(u->cpu, atom, ToU64(luaL_checknumber(L, 3)));
  return 0;
}

int Original(lua_State* L) {
  CpuUd* u = CheckCpu(L, 1);
  if (u->finished) luaL_error(L, "original(cpu) or ffxii.ret(cpu) already ran in this call");
  u->finished = true;
  ffxii_fn fn = u->original;
  ffxii_cpu* raw = u->cpu;
  fn(raw);
  return 0;
}

int Ret(lua_State* L) {
  CpuUd* u = CheckCpu(L, 1);
  if (u->finished) luaL_error(L, "original(cpu) or ffxii.ret(cpu) already ran in this call");
  u->finished = true;
  ModOf(L)->api->ret(u->cpu);
  return 0;
}

int Hook(lua_State* L) {
  Mod* m = ModOf(L);
  const uint64_t va = ToU64(luaL_checknumber(L, 1));
  luaL_checktype(L, 2, LUA_TFUNCTION);
  size_t i = kSlots;
  {
    std::lock_guard<std::mutex> lock(g_slots_mu);
    for (size_t k = 0; k < kSlots; k++)
      if (!g_slots[k].mod.load()) {
        i = k;
        g_slots[k].mod.store(m);
        break;
      }
  }
  if (i == kSlots) luaL_error(L, "too many script hooks (%d)", int(kSlots));
  Slot& s = g_slots[i];
  lua_pushvalue(L, 2);
  s.fn_ref = lua_ref(L, -1);
  lua_pop(L, 1);
  lua_pushcfunction(L, Original, "original");
  s.orig_ref = lua_ref(L, -1);
  lua_pop(L, 1);
  if (m->api->hook(va, kThunks[i], &s.original) != 0) {
    lua_unref(L, s.fn_ref);
    lua_unref(L, s.orig_ref);
    s.mod.store(nullptr);
    char line[96];
    std::snprintf(line, sizeof line, "hook %08llX refused (not a lifted function entry)",
                  static_cast<unsigned long long>(va));
    m->Log(line);
    lua_pushboolean(L, 0);
    return 1;
  }
  m->slots.push_back(i);
  lua_pushboolean(L, 1);
  return 1;
}

int Arg(lua_State* L) {
  CpuUd* u = CheckCpu(L, 1);
  const int i = luaL_checkinteger(L, 2);
  const ffxii_api* a = ModOf(L)->api;
  static constexpr int kRegs[4] = {FFXII_RCX, FFXII_RDX, FFXII_R8, FFXII_R9};
  uint64_t v;
  if (i >= 0 && i < 4) {
    v = a->reg(u->cpu, kRegs[i]);
  } else if (i >= 4) {
    std::memcpy(&v, a->guest_ptr(a->reg(u->cpu, FFXII_RSP) + 8 + 8 * uint64_t(i)), 8);
  } else {
    luaL_argerror(L, 2, "argument index below 0");
  }
  lua_pushnumber(L, double(v));
  return 1;
}

int XmmF32(lua_State* L) {
  CpuUd* u = CheckCpu(L, 1);
  const int i = luaL_checkinteger(L, 2);
  if (i < 0 || i > 15) luaL_argerror(L, 2, "xmm index 0-15");
  uint8_t b[16];
  ModOf(L)->api->xmm(u->cpu, i, b);
  float f;
  std::memcpy(&f, b, 4);
  lua_pushnumber(L, f);
  return 1;
}

int SetXmmF32(lua_State* L) {
  CpuUd* u = CheckCpu(L, 1);
  const int i = luaL_checkinteger(L, 2);
  if (i < 0 || i > 15) luaL_argerror(L, 2, "xmm index 0-15");
  const float f = float(luaL_checknumber(L, 3));
  const ffxii_api* a = ModOf(L)->api;
  uint8_t b[16];
  a->xmm(u->cpu, i, b);
  std::memcpy(b, &f, 4);
  a->set_xmm(u->cpu, i, b);
  return 0;
}

int Call(lua_State* L) {
  CpuUd* u = CheckCpu(L, 1);
  const uint64_t va = ToU64(luaL_checknumber(L, 2));
  uint64_t args[8];
  const int n = lua_gettop(L) - 2;
  if (n > 8) luaL_error(L, "ffxii.call takes up to 8 arguments");
  for (int k = 0; k < n; k++) args[k] = ToU64(luaL_checknumber(L, 3 + k));
  lua_pushnumber(L, double(ModOf(L)->api->call(u->cpu, va, args, uint32_t(n))));
  return 1;
}

template <class T>
int Read(lua_State* L) {
  T v;
  std::memcpy(&v, ModOf(L)->api->guest_ptr(ToU64(luaL_checknumber(L, 1))), sizeof v);
  lua_pushnumber(L, double(v));
  return 1;
}

template <class T>
int Write(lua_State* L) {
  const uint64_t va = ToU64(luaL_checknumber(L, 1));
  const double d = luaL_checknumber(L, 2);
  T v;
  if constexpr (std::is_floating_point_v<T>) v = T(d);
  else v = T(ToU64(d));
  std::memcpy(ModOf(L)->api->guest_ptr(va), &v, sizeof v);
  return 0;
}

int LogFn(lua_State* L) {
  std::string line;
  const int n = lua_gettop(L);
  for (int k = 1; k <= n; k++) {
    size_t len = 0;
    const char* s = luaL_tolstring(L, k, &len);
    if (k > 1) line += '\t';
    line.append(s, len);
    lua_pop(L, 1);
  }
  ModOf(L)->Log(line);
  return 0;
}

void OpenApi(lua_State* L, Mod* m) {
  static const luaL_Reg kFns[] = {
      {"hook", Hook},           {"ret", Ret},
      {"arg", Arg},             {"xmm_f32", XmmF32},
      {"set_xmm_f32", SetXmmF32}, {"call", Call},
      {"log", LogFn},           {"read_u8", Read<uint8_t>},
      {"read_u16", Read<uint16_t>}, {"read_u32", Read<uint32_t>},
      {"read_u64", Read<uint64_t>}, {"read_i8", Read<int8_t>},
      {"read_i16", Read<int16_t>}, {"read_i32", Read<int32_t>},
      {"read_f32", Read<float>}, {"read_f64", Read<double>},
      {"write_u8", Write<uint8_t>}, {"write_u16", Write<uint16_t>},
      {"write_u32", Write<uint32_t>}, {"write_u64", Write<uint64_t>},
      {"write_i8", Write<int8_t>}, {"write_i16", Write<int16_t>},
      {"write_i32", Write<int32_t>}, {"write_f32", Write<float>},
      {"write_f64", Write<double>}, {nullptr, nullptr}};
  lua_newtable(L);
  luaL_register(L, nullptr, kFns);
  lua_pushstring(L, m->api->mod_id ? m->api->mod_id : "");
  lua_setfield(L, -2, "mod_id");
  lua_pushstring(L, m->api->game_version ? m->api->game_version : "");
  lua_setfield(L, -2, "game_version");
  lua_setglobal(L, "ffxii");
  lua_pushcfunction(L, LogFn, "print");
  lua_setglobal(L, "print");

  // cpu userdata type and the one shared instance.
  luaL_newmetatable(L, "ffxii.cpu");
  lua_pushcfunction(L, CpuIndex, "cpu.__index");
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, CpuNewIndex, "cpu.__newindex");
  lua_setfield(L, -2, "__newindex");
  lua_pushstring(L, "cpu");
  lua_setfield(L, -2, "__type");
  lua_setreadonly(L, -1, true);
  lua_setuserdatametatable(L, kCpuTag);
  m->cpu = static_cast<CpuUd*>(lua_newuserdatataggedwithmetatable(L, sizeof(CpuUd), kCpuTag));
  new (m->cpu) CpuUd();
  m->cpu_ref = lua_ref(L, -1);
  lua_pop(L, 1);
}

bool RunChunk(Mod* m, const std::string& name, const std::string& src, const Options& opt, std::string& err) {
  lua_CompileOptions co{};
  co.optimizationLevel = 2;
  co.debugLevel = 1;
  size_t size = 0;
  char* bc = luau_compile(src.data(), src.size(), &co, &size);
  lua_State* T = lua_newthread(m->L);
  luaL_sandboxthread(T);
  const std::string chunk = "=" + name;
  const int rc = luau_load(T, chunk.c_str(), bc, size, 0);
  std::free(bc);
  if (rc != 0) {
    err = name + ": " + (lua_tostring(T, -1) ? lua_tostring(T, -1) : "load failed");
    lua_pop(m->L, 1);
    return false;
  }
  if (opt.native && NativeSupported()) luau_codegen_compile(T, -1);
  std::lock_guard<std::recursive_mutex> lock(m->mu);
  if (lua_pcall(T, 0, 0, 0) != LUA_OK) {
    err = name + ": " + (lua_tostring(T, -1) ? lua_tostring(T, -1) : "error");
    lua_pop(m->L, 1);
    return false;
  }
  lua_pop(m->L, 1);  // the thread
  return true;
}

std::shared_ptr<Mod> NewMod(const ffxii_api* api, const Options& opt) {
  auto m = std::make_shared<Mod>();
  m->api = api;
  m->L = luaL_newstate();
  lua_Callbacks* cb = lua_callbacks(m->L);
  cb->userdata = m.get();
  cb->useratom = Atom;
  if (opt.native && NativeSupported()) luau_codegen_create(m->L);
  luaL_openlibs(m->L);
  OpenApi(m->L, m.get());
  luaL_sandbox(m->L);
  std::lock_guard<std::mutex> lock(g_alive_mu);
  g_alive.push_back(m);
  return m;
}

}  // namespace

bool NativeSupported() { return luau_codegen_supported() != 0; }

std::shared_ptr<Mod> LoadSource(const ffxii_api* api, const std::string& name, const std::string& source,
                                const Options& opt, std::string& err) {
  auto m = NewMod(api, opt);
  if (!RunChunk(m.get(), name, source, opt, err)) return nullptr;
  return m;
}

std::shared_ptr<Mod> Load(const ffxii_api* api, const std::vector<std::string>& files, const Options& opt,
                          std::string& err) {
  auto m = NewMod(api, opt);
  for (const auto& f : files) {
    std::ifstream in(f, std::ios::binary);
    if (!in) {
      err = "cannot read " + f;
      return nullptr;
    }
    const std::string src((std::istreambuf_iterator<char>(in)), {});
    const size_t slash = f.find_last_of("/\\");
    if (!RunChunk(m.get(), slash == std::string::npos ? f : f.substr(slash + 1), src, opt, err)) return nullptr;
  }
  return m;
}

void Unload(std::shared_ptr<Mod>& mod) {
  if (!mod) return;
  {
    std::lock_guard<std::mutex> lock(g_slots_mu);
    for (size_t i : mod->slots) g_slots[i].mod.store(nullptr);
  }
  lua_close(mod->L);
  mod->L = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_alive_mu);
    for (auto it = g_alive.begin(); it != g_alive.end(); ++it)
      if (*it == mod) {
        g_alive.erase(it);
        break;
      }
  }
  mod.reset();
}

}  // namespace ffxii::luau
