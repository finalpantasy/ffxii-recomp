/* Mod lab test mod in C compiled to WebAssembly (lab_targets.h), over
 * include/ffxii_wasm.h. Built by CMake with clang --target=wasm32. */
#include "ffxii_wasm.h"
#include "../lab_targets.h"

FFXII_EXPORT(damage) void damage(int64_t a, int64_t b, int64_t c, int64_t d) {
  (void)a, (void)b, (void)c, (void)d;
  ffxii_original();
  ffxii_set_reg(FFXII_RAX, ffxii_reg(FFXII_RAX) * 2 + 1);
}

FFXII_EXPORT(lookup) void lookup(int64_t x, int64_t b, int64_t c, int64_t d) {
  (void)b, (void)c, (void)d;
  const uint64_t i = (uint64_t)x % 4096;
  ffxii_set_reg(FFXII_RAX, (uint64_t)ffxii_read_u16(LAB_TABLE + i * 2) * 3 + 7);
  ffxii_ret();
}

FFXII_EXPORT(score) void score(int64_t x, int64_t b, int64_t c, int64_t d) {
  (void)b, (void)c, (void)d;
  uint64_t sum = 0;
  for (uint64_t k = 0; k < 32; k++)
    sum += (uint64_t)ffxii_read_u16(LAB_TABLE + (((uint64_t)x + 7 * k) % 4096) * 2) * (k + 1);
  ffxii_set_reg(FFXII_RAX, sum);
  ffxii_ret();
}

FFXII_EXPORT(ffxii_init) int32_t ffxii_init(void) {
  if (ffxii_hook_export(LAB_DAMAGE, "damage") != 0) return 1;
  if (ffxii_hook_export(LAB_LOOKUP, "lookup") != 0) return 1;
  if (ffxii_hook_export(LAB_SCORE, "score") != 0) return 1;
  return 0;
}
