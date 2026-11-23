// WebAssembly mods over the native mod API: see wasm_mods.h and the guest
// header include/ffxii_wasm.h.

#include "wasm_mods.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <chrono>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <wasmtime.h>

namespace ffxii::wasm {

namespace {

constexpr size_t kSlots = 512;  // wasm hooks across all mods

// The hook call that is running: valid only inside it.
struct Call {
  ffxii_cpu* cpu = nullptr;
  ffxii_fn original = nullptr;
  bool finished = false;  // ffxii_original() or ffxii_ret() happened
};

std::string ErrText(wasmtime_error_t* e, wasm_trap_t* t) {
  wasm_byte_vec_t m{};
  if (e) {
    wasmtime_error_message(e, &m);
    wasmtime_error_delete(e);
  } else if (t) {
    wasm_trap_message(t, &m);
    wasm_trap_delete(t);
  } else {
    return "unknown error";
  }
  std::string s(m.data, m.size);
  wasm_byte_vec_delete(&m);
  while (!s.empty() && (s.back() == '\0' || s.back() == '\n')) s.pop_back();
  return s;
}

// Wasmtime's trap text is "error while executing at wasm backtrace:", one
// line per frame ("0: 0x2e9 - mod.wasm!fn"), then "Caused by:" and the
// cause. One line as "<cause> (in mod.wasm!fn)".
std::string OneLine(const std::string& s) {
  auto trim = [](std::string t) {
    const size_t a = t.find_first_not_of(" \t\r\n");
    const size_t b = t.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
  };
  auto flat = [](std::string t) {
    for (char& ch : t)
      if (ch == '\n' || ch == '\r') ch = ' ';
    return t;
  };
  const size_t c = s.find("Caused by:");
  if (c == std::string::npos) return flat(s);
  std::string cause = flat(trim(s.substr(c + 10)));
  const size_t d = s.find(" - ");
  if (d != std::string::npos && d < c) {
    const size_t e = s.find('\n', d);
    cause += " (in " + trim(s.substr(d + 3, e - d - 3)) + ")";
  }
  return cause;
}

// Epoch interruption bounds how long one hook call (or a module's init)
// may run in wasm code: a timer thread advances the engine's epoch every
// kTickMs, and each call gets a deadline in ticks. Time spent in game code
// (ffxii_original, ffxii_call) does not count: the deadline is renewed when
// the game code returns.
constexpr uint32_t kTickMs = 10;
constexpr uint64_t kNoDeadline = uint64_t(1) << 62;

wasm_engine_t* Engine() {
  static wasm_engine_t* e = [] {
    wasm_config_t* cfg = wasm_config_new();
    wasmtime_config_epoch_interruption_set(cfg, true);
    wasm_engine_t* engine = wasm_engine_new_with_config(cfg);
    std::thread([engine] {
      for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs));
        wasmtime_engine_increment_epoch(engine);
      }
    }).detach();
    return engine;
  }();
  return e;
}

uint64_t Ticks(uint32_t ms) { return ms ? (ms + kTickMs - 1) / kTickMs + 1 : kNoDeadline; }

}  // namespace

class Mod {
 public:
  const ffxii_api* api = nullptr;
  Options opt;
  wasmtime_store_t* store = nullptr;
  wasmtime_linker_t* linker = nullptr;
  std::recursive_mutex mu;
  Call* cur = nullptr;
  // While a host function runs game code (original, call), a hook of this
  // mod entered from there must use that host function's caller context.
  wasmtime_context_t* active = nullptr;
  bool warned = false;

  void Renew(wasmtime_context_t* ctx) const { wasmtime_context_set_epoch_deadline(ctx, Ticks(opt.time_limit_ms)); }
  std::string LimitNote(const std::string& text) const {
    if (text.find("interrupt") == std::string::npos || !opt.time_limit_ms) return text;
    return text + " (ran longer than " + std::to_string(opt.time_limit_ms) + " ms)";
  }

  void Log(const std::string& s) const { api->log(api, s.c_str()); }
  wasmtime_context_t* Ctx() { return active ? active : wasmtime_store_context(store); }
};

namespace {

struct Slot {
  std::atomic<Mod*> mod{nullptr};
  wasmtime_func_t func{};
  ffxii_fn original = nullptr;
};
std::array<Slot, kSlots> g_slots;
std::mutex g_slots_mu;
std::vector<std::shared_ptr<Mod>> g_alive;
std::mutex g_alive_mu;

void Invoke(size_t i, ffxii_cpu* raw) {
  Slot& s = g_slots[i];
  Mod* m = s.mod.load(std::memory_order_acquire);
  std::lock_guard<std::recursive_mutex> lock(m->mu);
  Call c{raw, s.original, false};
  Call* prev = m->cur;
  m->cur = &c;
  const ffxii_api* a = m->api;
  wasmtime_val_raw_t args[4];
  args[0].i64 = int64_t(a->reg(raw, FFXII_RCX));
  args[1].i64 = int64_t(a->reg(raw, FFXII_RDX));
  args[2].i64 = int64_t(a->reg(raw, FFXII_R8));
  args[3].i64 = int64_t(a->reg(raw, FFXII_R9));
  wasm_trap_t* trap = nullptr;
  wasmtime_context_t* ctx = m->Ctx();
  m->Renew(ctx);
  wasmtime_error_t* err = wasmtime_func_call_unchecked(ctx, &s.func, args, 4, &trap);
  if (err || trap) {
    m->Log("hook trapped: " + m->LimitNote(OneLine(ErrText(err, trap))));
  } else if (!c.finished && !m->warned) {
    m->warned = true;
    m->Log("a hook returned without ffxii_original() or ffxii_ret(); the original ran");
  }
  m->cur = prev;
  if (!c.finished) s.original(raw);
}

template <size_t... I>
constexpr std::array<ffxii_fn, sizeof...(I)> MakeThunks(std::index_sequence<I...>) {
  return {{[](ffxii_cpu* c) { Invoke(I, c); }...}};
}
const std::array<ffxii_fn, kSlots> kThunks = MakeThunks(std::make_index_sequence<kSlots>{});

// ---- host functions -------------------------------------------------------

using Val = wasmtime_val_t;
// Host functions use Wasmtime's unchecked form: arguments arrive in v[0..]
// and the result goes back in v[0], with no per-call type conversion. Each
// function reads all its arguments before it writes the result.
#define HOST(name) wasm_trap_t* name(void* env, wasmtime_caller_t* caller, wasmtime_val_raw_t* v, size_t nv)
#define UNUSED (void)env, (void)caller, (void)v, (void)nv

wasm_trap_t* Trap(const std::string& s) { return wasmtime_trap_new(s.data(), s.size()); }
Mod* M(void* env) { return static_cast<Mod*>(env); }

// The calling module's memory slice [p, p + n), or null.
uint8_t* ModMem(wasmtime_caller_t* caller, uint32_t p, uint32_t n) {
  wasmtime_extern_t item;
  if (!wasmtime_caller_export_get(caller, "memory", 6, &item) || item.kind != WASMTIME_EXTERN_MEMORY)
    return nullptr;
  wasmtime_context_t* ctx = wasmtime_caller_context(caller);
  const size_t size = wasmtime_memory_data_size(ctx, &item.of.memory);
  if (uint64_t(p) + n > size) return nullptr;
  return wasmtime_memory_data(ctx, &item.of.memory) + p;
}

wasm_trap_t* NoCall() { return Trap("called outside a hook"); }

HOST(HostHook) {
  UNUSED;
  Mod* m = M(env);
  const uint64_t va = uint64_t(v[0].i64);
  const uint32_t p = uint32_t(v[1].i32), n = uint32_t(v[2].i32);
  v[0].i32 = -1;  // va, p and n are read
  const uint8_t* name = ModMem(caller, p, n);
  if (!name) return Trap("ffxii_hook: name outside module memory");
  const std::string nm(reinterpret_cast<const char*>(name), n);
  wasmtime_extern_t item;
  char line[160];
  if (!wasmtime_caller_export_get(caller, nm.data(), nm.size(), &item) || item.kind != WASMTIME_EXTERN_FUNC) {
    std::snprintf(line, sizeof line, "hook %08llX: no exported function '%s'", (unsigned long long)va, nm.c_str());
    m->Log(line);
    return nullptr;
  }
  wasm_functype_t* ft = wasmtime_func_type(wasmtime_caller_context(caller), &item.of.func);
  const wasm_valtype_vec_t* ps = wasm_functype_params(ft);
  const wasm_valtype_vec_t* rs = wasm_functype_results(ft);
  bool ok = ps->size == 4 && rs->size == 0;
  for (size_t k = 0; ok && k < 4; k++) ok = wasm_valtype_kind(ps->data[k]) == WASM_I64;
  wasm_functype_delete(ft);
  if (!ok) {
    std::snprintf(line, sizeof line, "hook %08llX: '%s' must be (i64, i64, i64, i64) -> ()",
                  (unsigned long long)va, nm.c_str());
    m->Log(line);
    return nullptr;
  }
  size_t i = kSlots;
  {
    std::lock_guard<std::mutex> lock(g_slots_mu);
    for (size_t k = 0; k < kSlots; k++)
      if (!g_slots[k].mod.load()) {
        i = k;
        g_slots[k].func = item.of.func;
        g_slots[k].mod.store(m);
        break;
      }
  }
  if (i == kSlots) return Trap("too many wasm hooks");
  if (m->api->hook(va, kThunks[i], &g_slots[i].original) != 0) {
    g_slots[i].mod.store(nullptr);
    std::snprintf(line, sizeof line, "hook %08llX refused (not a lifted function entry)", (unsigned long long)va);
    m->Log(line);
    return nullptr;
  }
  v[0].i32 = 0;
  return nullptr;
}

HOST(HostOriginal) {
  UNUSED;
  Mod* m = M(env);
  Call* c = m->cur;
  if (!c) return NoCall();
  if (c->finished) return Trap("ffxii_original() or ffxii_ret() already ran in this call");
  c->finished = true;
  wasmtime_context_t* saved = m->active;
  m->active = wasmtime_caller_context(caller);
  c->original(c->cpu);
  m->Renew(m->active);
  m->active = saved;
  return nullptr;
}

HOST(HostRet) {
  UNUSED;
  Mod* m = M(env);
  Call* c = m->cur;
  if (!c) return NoCall();
  if (c->finished) return Trap("ffxii_original() or ffxii_ret() already ran in this call");
  c->finished = true;
  m->api->ret(c->cpu);
  return nullptr;
}

HOST(HostReg) {
  UNUSED;
  Mod* m = M(env);
  if (!m->cur) return NoCall();
  const int r = v[0].i32;
  if (r < 0 || r >= FFXII_REG_COUNT) return Trap("ffxii_reg: no such register");
  v[0].i64 = int64_t(m->api->reg(m->cur->cpu, r));
  return nullptr;
}

HOST(HostSetReg) {
  UNUSED;
  Mod* m = M(env);
  if (!m->cur) return NoCall();
  const int r = v[0].i32;
  if (r < 0 || r >= FFXII_REG_COUNT) return Trap("ffxii_set_reg: no such register");
  m->api->set_reg(m->cur->cpu, r, uint64_t(v[1].i64));
  return nullptr;
}

HOST(HostArg) {
  UNUSED;
  Mod* m = M(env);
  if (!m->cur) return NoCall();
  const int i = v[0].i32;
  const ffxii_api* a = m->api;
  static constexpr int kRegs[4] = {FFXII_RCX, FFXII_RDX, FFXII_R8, FFXII_R9};
  uint64_t x = 0;
  if (i >= 0 && i < 4) {
    x = a->reg(m->cur->cpu, kRegs[i]);
  } else if (i >= 4) {
    const uint64_t va = a->reg(m->cur->cpu, FFXII_RSP) + 8 + 8 * uint64_t(i);
    if (m->opt.readable && !m->opt.readable(va, 8)) return Trap("ffxii_arg: stack not mapped");
    std::memcpy(&x, a->guest_ptr(va), 8);
  } else {
    return Trap("ffxii_arg: index below 0");
  }
  v[0].i64 = int64_t(x);
  return nullptr;
}

HOST(HostXmm) {
  UNUSED;
  Mod* m = M(env);
  if (!m->cur) return NoCall();
  const int i = v[0].i32;
  if (i < 0 || i >= 16) return Trap("ffxii_xmm_f32: index outside 0-15");
  uint8_t b[16];
  m->api->xmm(m->cur->cpu, i, b);
  std::memcpy(&v[0].f32, b, 4);
  return nullptr;
}

HOST(HostSetXmm) {
  UNUSED;
  Mod* m = M(env);
  if (!m->cur) return NoCall();
  const int i = v[0].i32;
  if (i < 0 || i >= 16) return Trap("ffxii_set_xmm_f32: index outside 0-15");
  uint8_t b[16];
  m->api->xmm(m->cur->cpu, i, b);
  std::memcpy(b, &v[1].f32, 4);
  m->api->set_xmm(m->cur->cpu, i, b);
  return nullptr;
}

HOST(HostCall) {
  UNUSED;
  Mod* m = M(env);
  if (!m->cur) return NoCall();
  const uint64_t va = uint64_t(v[0].i64);
  const uint32_t p = uint32_t(v[1].i32), n = uint32_t(v[2].i32);
  if (n > 8) return Trap("ffxii_call: more than 8 arguments");
  uint64_t a[8] = {};
  if (n) {
    const uint8_t* src = ModMem(caller, p, n * 8);
    if (!src) return Trap("ffxii_call: arguments outside module memory");
    std::memcpy(a, src, n * 8);
  }
  wasmtime_context_t* saved = m->active;
  m->active = wasmtime_caller_context(caller);
  const uint64_t r = m->api->call(m->cur->cpu, va, a, n);
  m->Renew(m->active);
  m->active = saved;
  v[0].i64 = int64_t(r);
  return nullptr;
}

wasm_trap_t* BadRead(uint64_t va) {
  char s[80];
  std::snprintf(s, sizeof s, "game memory read at 0x%llX is not mapped", (unsigned long long)va);
  return Trap(s);
}
wasm_trap_t* BadWrite(uint64_t va) {
  char s[80];
  std::snprintf(s, sizeof s, "game memory write at 0x%llX is not mapped writable", (unsigned long long)va);
  return Trap(s);
}

template <class T>
HOST(HostRead) {
  UNUSED;
  Mod* m = M(env);
  const uint64_t va = uint64_t(v[0].i64);
  if (m->opt.readable && !m->opt.readable(va, sizeof(T))) return BadRead(va);
  T x;
  std::memcpy(&x, m->api->guest_ptr(va), sizeof x);
  if constexpr (std::is_same_v<T, float>) {
    v[0].f32 = x;
  } else if constexpr (std::is_same_v<T, double>) {
    v[0].f64 = x;
  } else if constexpr (sizeof(T) == 8) {
    v[0].i64 = int64_t(x);
  } else {
    v[0].i32 = int32_t(uint32_t(x));
  }
  return nullptr;
}

template <class T>
HOST(HostWrite) {
  UNUSED;
  Mod* m = M(env);
  const uint64_t va = uint64_t(v[0].i64);
  if (m->opt.writable && !m->opt.writable(va, sizeof(T))) return BadWrite(va);
  T x;
  if constexpr (std::is_same_v<T, float>) x = v[1].f32;
  else if constexpr (std::is_same_v<T, double>) x = v[1].f64;
  else if constexpr (sizeof(T) == 8) x = T(v[1].i64);
  else x = T(uint32_t(v[1].i32));
  std::memcpy(m->api->guest_ptr(va), &x, sizeof x);
  return nullptr;
}

HOST(HostReadBytes) {
  UNUSED;
  Mod* m = M(env);
  const uint64_t va = uint64_t(v[0].i64);
  const uint32_t p = uint32_t(v[1].i32), n = uint32_t(v[2].i32);
  uint8_t* dst = ModMem(caller, p, n);
  if (!dst) return Trap("ffxii_read_bytes: destination outside module memory");
  if (m->opt.readable && !m->opt.readable(va, n)) return BadRead(va);
  std::memcpy(dst, m->api->guest_ptr(va), n);
  return nullptr;
}

HOST(HostWriteBytes) {
  UNUSED;
  Mod* m = M(env);
  const uint64_t va = uint64_t(v[0].i64);
  const uint32_t p = uint32_t(v[1].i32), n = uint32_t(v[2].i32);
  const uint8_t* src = ModMem(caller, p, n);
  if (!src) return Trap("ffxii_write_bytes: source outside module memory");
  if (m->opt.writable && !m->opt.writable(va, n)) return BadWrite(va);
  std::memcpy(m->api->guest_ptr(va), src, n);
  return nullptr;
}

HOST(HostLog) {
  UNUSED;
  const uint32_t p = uint32_t(v[0].i32), n = uint32_t(v[1].i32);
  const uint8_t* s = ModMem(caller, p, n);
  if (!s) return Trap("ffxii_log: text outside module memory");
  M(env)->Log(std::string(reinterpret_cast<const char*>(s), n));
  return nullptr;
}

// Value type letters: i i32, I i64, f f32, d f64.
wasm_valtype_vec_t Types(const char* t) {
  wasm_valtype_vec_t v;
  const size_t n = std::strlen(t);
  if (!n) {
    wasm_valtype_vec_new_empty(&v);
    return v;
  }
  std::vector<wasm_valtype_t*> ts;
  for (size_t k = 0; k < n; k++) {
    const wasm_valkind_t kind = t[k] == 'i' ? WASM_I32 : t[k] == 'I' ? WASM_I64 : t[k] == 'f' ? WASM_F32 : WASM_F64;
    ts.push_back(wasm_valtype_new(kind));
  }
  wasm_valtype_vec_new(&v, n, ts.data());
  return v;
}

struct HostDef {
  const char* name;
  const char* params;
  const char* results;
  wasmtime_func_unchecked_callback_t cb;
};
const HostDef kHost[] = {
    {"hook", "Iii", "i", HostHook},
    {"original", "", "", HostOriginal},
    {"ret", "", "", HostRet},
    {"reg", "i", "I", HostReg},
    {"set_reg", "iI", "", HostSetReg},
    {"arg", "i", "I", HostArg},
    {"xmm_f32", "i", "f", HostXmm},
    {"set_xmm_f32", "if", "", HostSetXmm},
    {"call", "Iii", "I", HostCall},
    {"read_u8", "I", "i", HostRead<uint8_t>},
    {"read_u16", "I", "i", HostRead<uint16_t>},
    {"read_u32", "I", "i", HostRead<uint32_t>},
    {"read_u64", "I", "I", HostRead<uint64_t>},
    {"read_f32", "I", "f", HostRead<float>},
    {"read_f64", "I", "d", HostRead<double>},
    {"write_u8", "Ii", "", HostWrite<uint8_t>},
    {"write_u16", "Ii", "", HostWrite<uint16_t>},
    {"write_u32", "Ii", "", HostWrite<uint32_t>},
    {"write_u64", "II", "", HostWrite<uint64_t>},
    {"write_f32", "If", "", HostWrite<float>},
    {"write_f64", "Id", "", HostWrite<double>},
    {"read_bytes", "Iii", "", HostReadBytes},
    {"write_bytes", "Iii", "", HostWriteBytes},
    {"log", "ii", "", HostLog},
};

bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

bool EndsWith(const std::string& s, const char* suf) {
  const size_t n = std::strlen(suf);
  return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

// Calls an optional no-argument export; returns its i32 result (0 if it
// has none) or sets err.
bool CallInit(wasmtime_context_t* ctx, wasmtime_instance_t* inst, const char* name, int32_t& rc,
              std::string& err) {
  wasmtime_extern_t item;
  rc = 0;
  if (!wasmtime_instance_export_get(ctx, inst, name, std::strlen(name), &item) ||
      item.kind != WASMTIME_EXTERN_FUNC)
    return true;
  Val r[1]{};
  wasm_functype_t* ft = wasmtime_func_type(ctx, &item.of.func);
  const size_t nres = wasm_functype_results(ft)->size;
  const size_t npar = wasm_functype_params(ft)->size;
  wasm_functype_delete(ft);
  if (npar != 0 || nres > 1) {
    err = std::string(name) + " must take no arguments";
    return false;
  }
  wasm_trap_t* trap = nullptr;
  wasmtime_error_t* e = wasmtime_func_call(ctx, &item.of.func, nullptr, 0, r, nres, &trap);
  if (e || trap) {
    err = std::string(name) + " trapped: " + OneLine(ErrText(e, trap));
    return false;
  }
  if (nres == 1 && r[0].kind == WASMTIME_I32) rc = r[0].of.i32;
  return true;
}

}  // namespace

const char* RuntimeVersion() { return WASMTIME_VERSION; }

void Unload(std::shared_ptr<Mod>& mod) {
  if (!mod) return;
  {
    std::lock_guard<std::mutex> lock(g_slots_mu);
    for (Slot& s : g_slots)
      if (s.mod.load() == mod.get()) {
        s.original = nullptr;
        s.mod.store(nullptr);
      }
  }
  {
    std::lock_guard<std::mutex> lock(g_alive_mu);
    g_alive.erase(std::remove(g_alive.begin(), g_alive.end(), mod), g_alive.end());
  }
  if (mod->linker) wasmtime_linker_delete(mod->linker);
  if (mod->store) wasmtime_store_delete(mod->store);
  mod->linker = nullptr;
  mod->store = nullptr;
  mod.reset();
}

std::shared_ptr<Mod> Load(const ffxii_api* api, const std::vector<std::string>& files, const Options& opt,
                          std::string& err) {
  auto m = std::make_shared<Mod>();
  m->api = api;
  m->opt = opt;
  {
    std::lock_guard<std::mutex> lock(g_alive_mu);
    g_alive.push_back(m);
  }
  wasm_engine_t* engine = Engine();
  m->store = wasmtime_store_new(engine, nullptr, nullptr);
  m->linker = wasmtime_linker_new(engine);
  for (const HostDef& h : kHost) {
    wasm_valtype_vec_t ps = Types(h.params), rs = Types(h.results);
    wasm_functype_t* ft = wasm_functype_new(&ps, &rs);
    wasmtime_error_t* e =
        wasmtime_linker_define_func_unchecked(m->linker, "ffxii", 5, h.name, std::strlen(h.name), ft, h.cb, m.get(), nullptr);
    wasm_functype_delete(ft);
    if (e) {
      err = std::string("defining ffxii.") + h.name + ": " + ErrText(e, nullptr);
      return nullptr;
    }
  }
  std::lock_guard<std::recursive_mutex> lock(m->mu);
  wasmtime_context_t* ctx = wasmtime_store_context(m->store);
  for (const std::string& path : files) {
    // Instantiation checks the epoch too (a store starts at deadline 0).
    wasmtime_context_set_epoch_deadline(
        ctx, m->opt.time_limit_ms ? Ticks(m->opt.time_limit_ms * 10) : kNoDeadline);
    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes)) {
      err = "cannot read " + path;
      return nullptr;
    }
    wasm_byte_vec_t wat_out{};
    const uint8_t* code = bytes.data();
    size_t len = bytes.size();
    if (EndsWith(path, ".wat")) {
      if (wasmtime_error_t* e = wasmtime_wat2wasm(reinterpret_cast<const char*>(code), len, &wat_out)) {
        err = path + ": " + ErrText(e, nullptr);
        return nullptr;
      }
      code = reinterpret_cast<const uint8_t*>(wat_out.data);
      len = wat_out.size;
    }
    wasmtime_module_t* module = nullptr;
    wasmtime_error_t* e = wasmtime_module_new(engine, code, len, &module);
    if (wat_out.data) wasm_byte_vec_delete(&wat_out);
    if (e) {
      err = path + ": " + ErrText(e, nullptr);
      return nullptr;
    }
    wasmtime_instance_t inst;
    wasm_trap_t* trap = nullptr;
    e = wasmtime_linker_instantiate(m->linker, ctx, module, &inst, &trap);
    wasmtime_module_delete(module);
    if (e || trap) {
      err = path + ": " + ErrText(e, trap);
      return nullptr;
    }
    int32_t rc = 0;
    wasmtime_context_set_epoch_deadline(
        ctx, m->opt.time_limit_ms ? Ticks(m->opt.time_limit_ms * 10) : kNoDeadline);
    // A reactor module (clang -mexec-model=reactor) runs its constructors here.
    if (!CallInit(ctx, &inst, "_initialize", rc, err) || !CallInit(ctx, &inst, "ffxii_init", rc, err)) {
      err = path + ": " + err;
      return nullptr;
    }
    if (rc != 0) {
      err = path + ": ffxii_init returned " + std::to_string(rc);
      return nullptr;
    }
  }
  return m;
}

}  // namespace ffxii::wasm
