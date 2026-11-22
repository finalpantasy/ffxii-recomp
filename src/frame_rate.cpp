// Frame-rate experiment (FFXII_SIM_MULT=k, static trace 2026-11-22).
// 001EDDC0(fps60) sets the frame-timing globals the game reads in about
// 550 places: 0x1DFE0B4 frames per 30 Hz tick (int, 1 or 2), 0x1DFE0B8
// the same as a float, 0x1DFE0BC the per-frame time scale (1.0 or 0.5),
// and 0x1F82E38 the 60 fps flag. With FrameLimit=60 and k > 1 this wrapper
// rescales them for 60*k fps after the original runs.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "runtime/env.h"
#include "x86/lifted.h"
#include "x86/runtime.h"

namespace recomp::gen {
void f_001EDDC0_lifted(::recomp::Ctx& c);

void f_001EDDC0(::recomp::Ctx& c) {
  const bool fps60 = uint32_t(c.rcx) != 0;
  f_001EDDC0_lifted(c);
  const char* e = recomp::Env("SIM_MULT");
  const int k = e ? std::atoi(e) : 1;
  if (!fps60 || k <= 1) return;
  uint8_t* B = recomp::g_guest_base;
  const int32_t ticks = 2 * k;
  const float fticks = float(ticks), scale = 1.0f / float(ticks);
  std::memcpy(B + 0x1DFE0B4, &ticks, 4);
  std::memcpy(B + 0x1DFE0B8, &fticks, 4);
  std::memcpy(B + 0x1DFE0BC, &scale, 4);
  std::fprintf(stderr, "[frame_rate] SIM_MULT=%d: %d frames per 30 Hz tick, scale %g\n", k, ticks, scale);
}
}  // namespace recomp::gen
