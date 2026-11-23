// ffxii_mod.hpp: C++20 conveniences over the native module C ABI
// (ffxii_mod.h). Header-only; a module built with it still exports only
// ffxii_mod_init and talks to the recomp only through ffxii_api, so it
// works with any recomp of the same ABI version.
//
//   #include "ffxii_mod.hpp"
//   using namespace ffxii::mod;
//
//   // Runs in place of the function at 0x32D3D0 (Win64 arguments: rcx...).
//   void OnFilter(Cpu cpu, Next next) {
//     const uint64_t axis = cpu.arg(0);  // read arguments before next()
//     next(cpu);                         // the original, or the next hook
//     Log("filtered %f", Guest<float>(axis + 0xF8));
//   }
//
//   FFXII_MOD_INIT() { return Hook<0x32D3D0, OnFilter>(); }
//
// A hook either calls next(cpu) once, or replaces the function with
// cpu.Return(value) (ffxii_mod.h, "Hooks"). Hooks run on the guest thread
// that made the call; module state shared between calls needs its own
// locking.
#ifndef FFXII_MOD_HPP
#define FFXII_MOD_HPP

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "ffxii_mod.h"

namespace ffxii::mod {

// The API the recomp passed to ffxii_mod_init (set by FFXII_MOD_INIT).
inline const ffxii_api* g_api = nullptr;

// Host pointer to guest address va.
template <class T = uint8_t>
T* GuestPtr(uint64_t va) {
  return static_cast<T*>(g_api->guest_ptr(va));
}

// The value at guest address va (copied, so unaligned addresses are fine).
template <class T>
T Guest(uint64_t va) {
  T v;
  std::memcpy(&v, g_api->guest_ptr(va), sizeof v);
  return v;
}

template <class T>
void SetGuest(uint64_t va, const T& v) {
  std::memcpy(g_api->guest_ptr(va), &v, sizeof v);
}

// printf-style line to the recomp's log, prefixed with the mod id.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
inline void Log(const char* fmt, ...) {
  char line[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  g_api->log(g_api, line);
}

// The guest CPU state a hook received.
class Cpu {
 public:
  explicit Cpu(ffxii_cpu* raw) : raw_(raw) {}
  ffxii_cpu* raw() const { return raw_; }

  uint64_t reg(ffxii_reg r) const { return g_api->reg(raw_, r); }
  void set_reg(ffxii_reg r, uint64_t v) const { g_api->set_reg(raw_, r, v); }

  // Integer argument i (0-based) at function entry, Win64 convention:
  // rcx, rdx, r8, r9, then the stack above the return address and the
  // 32-byte home space. Valid only before next() runs.
  uint64_t arg(int i) const {
    static constexpr ffxii_reg kRegs[4] = {FFXII_RCX, FFXII_RDX, FFXII_R8, FFXII_R9};
    if (i < 4) return reg(kRegs[i]);
    return Guest<uint64_t>(reg(FFXII_RSP) + 8 + 8 * uint64_t(i));
  }
  // Float argument i (0-3) from xmm lane 0.
  float arg_f(int i) const { return xmm_f(i); }
  // The address the function returns to (at entry: [rsp]).
  uint64_t return_address() const { return Guest<uint64_t>(reg(FFXII_RSP)); }

  float xmm_f(int i) const {
    uint8_t b[16];
    g_api->xmm(raw_, i, b);
    float f;
    std::memcpy(&f, b, 4);
    return f;
  }
  void set_xmm_f(int i, float f) const {
    uint8_t b[16];
    g_api->xmm(raw_, i, b);
    std::memcpy(b, &f, 4);
    g_api->set_xmm(raw_, i, b);
  }

  // Result after next() returns.
  uint64_t result() const { return reg(FFXII_RAX); }
  float result_f() const { return xmm_f(0); }
  void set_result(uint64_t v) const { set_reg(FFXII_RAX, v); }
  void set_result_f(float f) const { set_xmm_f(0, f); }

  // Replaces the function: sets rax and returns to the caller.
  void Return(uint64_t v = 0) const {
    set_result(v);
    g_api->ret(raw_);
  }
  void ReturnF(float f) const {
    set_result_f(f);
    g_api->ret(raw_);
  }

  // Calls the guest function at va (through the dispatcher, so hooks on va
  // run) with integer arguments; returns rax.
  uint64_t Call(uint64_t va, std::initializer_list<uint64_t> args = {}) const {
    return g_api->call(raw_, va, args.begin(), uint32_t(args.size()));
  }

 private:
  ffxii_cpu* raw_;
};

// The rest of the hook chain: the hook installed before this one, or the
// game's function.
struct Next {
  ffxii_fn fn;
  void operator()(Cpu cpu) const { fn(cpu.raw()); }
};

namespace detail {
template <uint64_t VA, auto Fn>
struct HookSlot {
  static inline ffxii_fn original = nullptr;
  static void Thunk(ffxii_cpu* c) { Fn(Cpu(c), Next{original}); }
};
}  // namespace detail

// Hooks the lifted function at VA with Fn, a function or captureless
// lambda taking (Cpu, Next). True on success; logs the failure otherwise.
// Call it from FFXII_MOD_INIT only. Each (VA, Fn) pair hooks once.
template <uint64_t VA, auto Fn>
bool Hook() {
  using Slot = detail::HookSlot<VA, Fn>;
  if (g_api->hook(VA, &Slot::Thunk, &Slot::original) != 0) {
    Log("hook %08llX refused (not a lifted function entry)", static_cast<unsigned long long>(VA));
    return false;
  }
  return true;
}

// Whether a hook on va also catches direct calls between lifted functions.
inline bool CoversDirect(uint64_t va) { return g_api->hook_covers_direct(va) != 0; }

}  // namespace ffxii::mod

// Defines the module's entry point. The body runs with g_api set and
// returns true on success (any hooks it needs installed).
#define FFXII_MOD_INIT()                                                   \
  static bool ffxii_mod_user_init();                                       \
  extern "C" FFXII_MOD_EXPORT int ffxii_mod_init(const ffxii_api* api) {   \
    if (!api || api->abi < 1 || api->size < sizeof(ffxii_api)) return 1;   \
    ::ffxii::mod::g_api = api;                                             \
    return ffxii_mod_user_init() ? 0 : 1;                                  \
  }                                                                        \
  static bool ffxii_mod_user_init()

#endif  // FFXII_MOD_HPP
