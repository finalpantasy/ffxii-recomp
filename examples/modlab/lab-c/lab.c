/* Mod lab test mod in C over the plain ffxii_mod.h API (lab_targets.h). */
#include <string.h>

#include "ffxii_mod.h"
#include "../lab_targets.h"

static const ffxii_api* A;
static ffxii_fn damage_original;

static uint16_t Table(uint64_t i) {
  uint16_t v;
  memcpy(&v, A->guest_ptr(LAB_TABLE + i * 2), 2);
  return v;
}

static void Damage(ffxii_cpu* c) {
  damage_original(c);
  A->set_reg(c, FFXII_RAX, A->reg(c, FFXII_RAX) * 2 + 1);
}

static void Lookup(ffxii_cpu* c) {
  A->set_reg(c, FFXII_RAX, (uint64_t)Table(A->reg(c, FFXII_RCX) % 4096) * 3 + 7);
  A->ret(c);
}

static void Score(ffxii_cpu* c) {
  const uint64_t x = A->reg(c, FFXII_RCX);
  uint64_t sum = 0;
  for (uint64_t k = 0; k < 32; k++) sum += (uint64_t)Table((x + 7 * k) % 4096) * (k + 1);
  A->set_reg(c, FFXII_RAX, sum);
  A->ret(c);
}

FFXII_MOD_EXPORT int ffxii_mod_init(const ffxii_api* api) {
  ffxii_fn unused;
  A = api;
  if (A->hook(LAB_DAMAGE, Damage, &damage_original) != 0) return 1;
  if (A->hook(LAB_LOOKUP, Lookup, &unused) != 0) return 1;
  if (A->hook(LAB_SCORE, Score, &unused) != 0) return 1;
  return 0;
}
