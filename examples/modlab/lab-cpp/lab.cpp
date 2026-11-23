// Mod lab test mod in C++ over ffxii_mod.hpp (lab_targets.h).
#include "ffxii_mod.hpp"
#include "../lab_targets.h"

using namespace ffxii::mod;

namespace {

uint16_t Table(uint64_t i) { return Guest<uint16_t>(LAB_TABLE + i * 2); }

void Damage(Cpu cpu, Next next) {
  next(cpu);
  cpu.set_result(cpu.result() * 2 + 1);
}

void Lookup(Cpu cpu, Next) { cpu.Return(uint64_t(Table(cpu.arg(0) % 4096)) * 3 + 7); }

void Score(Cpu cpu, Next) {
  const uint64_t x = cpu.arg(0);
  uint64_t sum = 0;
  for (uint64_t k = 0; k < 32; k++) sum += uint64_t(Table((x + 7 * k) % 4096)) * (k + 1);
  cpu.Return(sum);
}

}  // namespace

FFXII_MOD_INIT() {
  return Hook<LAB_DAMAGE, Damage>() && Hook<LAB_LOOKUP, Lookup>() && Hook<LAB_SCORE, Score>();
}
