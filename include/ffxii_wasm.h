/* FFXII recomp WebAssembly mod API, version 1 (guest side).
 *
 * Include this in C or C++ compiled to wasm32, for example:
 *
 *     clang --target=wasm32 -O2 -nostdlib -Wl,--no-entry -Wl,--export-dynamic \
 *           -I include mymod.c -o mymod.wasm
 *
 * and list the module in mod.ini as `wasm = mymod.wasm`. The recomp runs it
 * in Wasmtime, compiled to native code at load. The module keeps its own
 * linear memory (its stack, globals and heap); the game's memory is reached
 * through the read and write calls below, which check every address. A read
 * or write of an address the game has not mapped stops the hook with a trap
 * (logged), and the game's function runs as if the hook were absent. The
 * module cannot open files, sockets or processes: it imports nothing but
 * these functions.
 *
 * After instantiation the recomp calls the export `ffxii_init` (no
 * arguments, returns int; 0 means success) if the module has one.
 *
 * A hook is an exported function taking the first four integer arguments
 * of the hooked function (rcx, rdx, r8, r9):
 *
 *     FFXII_EXPORT(on_wndproc) void on_wndproc(int64_t hwnd, int64_t msg,
 *                                              int64_t wp, int64_t lp) {
 *       ffxii_original();          // run the game's function (and lower hooks)
 *     }
 *     ... ffxii_hook_export(0x798FF0, "on_wndproc");
 *
 * Exactly one of ffxii_original() or ffxii_ret() per call. A hook that does
 * neither has the original run after it, and the log says so once.
 */
#ifndef FFXII_WASM_H
#define FFXII_WASM_H

#include <stdint.h>

#define FFXII_WASM_ABI 1

#define FFXII_IMPORT(name) __attribute__((import_module("ffxii"), import_name(#name)))
#define FFXII_EXPORT(name) __attribute__((export_name(#name)))

enum {
  FFXII_RAX, FFXII_RCX, FFXII_RDX, FFXII_RBX, FFXII_RSP, FFXII_RBP, FFXII_RSI, FFXII_RDI,
  FFXII_R8, FFXII_R9, FFXII_R10, FFXII_R11, FFXII_R12, FFXII_R13, FFXII_R14, FFXII_R15
};

#ifdef __cplusplus
extern "C" {
#endif

/* Hooks the lifted function at `va` with this module's export `name`
 * (signature (i64, i64, i64, i64) -> ()). Returns 0, or -1 when the export
 * is missing or has another signature, or the address is not a lifted
 * function entry. */
FFXII_IMPORT(hook) int32_t ffxii_hook(uint64_t va, const char* name, uint32_t name_len);

/* Inside a hook: */
FFXII_IMPORT(original) void ffxii_original(void); /* run the rest of the chain */
FFXII_IMPORT(ret) void ffxii_ret(void);           /* return without it (set rax first) */
FFXII_IMPORT(reg) uint64_t ffxii_reg(int32_t reg);
FFXII_IMPORT(set_reg) void ffxii_set_reg(int32_t reg, uint64_t value);
FFXII_IMPORT(arg) uint64_t ffxii_arg(int32_t index); /* 0-3 registers, 4+ stack */
FFXII_IMPORT(xmm_f32) float ffxii_xmm_f32(int32_t index);
FFXII_IMPORT(set_xmm_f32) void ffxii_set_xmm_f32(int32_t index, float value);
/* Calls a game function with `count` (up to 8) integer arguments read from
 * `args` in module memory; returns rax. */
FFXII_IMPORT(call) uint64_t ffxii_call(uint64_t va, const uint64_t* args, uint32_t count);

/* Game memory, anywhere: */
FFXII_IMPORT(read_u8) uint32_t ffxii_read_u8(uint64_t va);
FFXII_IMPORT(read_u16) uint32_t ffxii_read_u16(uint64_t va);
FFXII_IMPORT(read_u32) uint32_t ffxii_read_u32(uint64_t va);
FFXII_IMPORT(read_u64) uint64_t ffxii_read_u64(uint64_t va);
FFXII_IMPORT(read_f32) float ffxii_read_f32(uint64_t va);
FFXII_IMPORT(read_f64) double ffxii_read_f64(uint64_t va);
FFXII_IMPORT(write_u8) void ffxii_write_u8(uint64_t va, uint32_t value);
FFXII_IMPORT(write_u16) void ffxii_write_u16(uint64_t va, uint32_t value);
FFXII_IMPORT(write_u32) void ffxii_write_u32(uint64_t va, uint32_t value);
FFXII_IMPORT(write_u64) void ffxii_write_u64(uint64_t va, uint64_t value);
FFXII_IMPORT(write_f32) void ffxii_write_f32(uint64_t va, float value);
FFXII_IMPORT(write_f64) void ffxii_write_f64(uint64_t va, double value);
/* Bulk copies between game memory and module memory. */
FFXII_IMPORT(read_bytes) void ffxii_read_bytes(uint64_t va, void* dst, uint32_t len);
FFXII_IMPORT(write_bytes) void ffxii_write_bytes(uint64_t va, const void* src, uint32_t len);

/* One line to the recomp's log as `[mod <id>] text`. */
FFXII_IMPORT(log) void ffxii_log(const char* text, uint32_t len);

static inline uint32_t ffxii_strlen(const char* s) {
  uint32_t n = 0;
  while (s[n]) n++;
  return n;
}
static inline int32_t ffxii_hook_export(uint64_t va, const char* name) {
  return ffxii_hook(va, name, ffxii_strlen(name));
}
static inline void ffxii_log_str(const char* s) { ffxii_log(s, ffxii_strlen(s)); }

#ifdef __cplusplus
}
#endif

#endif /* FFXII_WASM_H */
