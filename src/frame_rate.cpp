// Frame-rate experiment (FFXII_SIM_MULT=k, static trace 2026-11-22).
// 001EDDC0(fps60) sets the frame-timing globals the game reads in about
// 550 places: 0x1DFE0B4 frames per 30 Hz tick (int, 1 or 2), 0x1DFE0B8
// the same as a float, 0x1DFE0BC the per-frame time scale (1.0 or 0.5),
// and 0x1F82E38 the 60 fps flag. With FrameLimit=60 and k > 1 this wrapper
// rescales them for 60*k fps after the original runs.
// 006234A0 is the frame limiter: it sleeps until r14 * 1/60 s (0x9010A0, or
// 1.001/60 at 0x900378) have passed since the last frame. 0x1E03F0 shares
// those constants to turn the 60 Hz tick count into seconds, so the wrapper
// divides them by k only while the limiter runs. It also logs, once a
// second, the 1/60 s tick counter 0x208F570 (60 per second = real speed).

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "runtime/env.h"
#include "x86/lifted.h"
#include "x86/runtime.h"

namespace recomp::gen {
void f_001EDDC0_lifted(::recomp::Ctx& c);
void f_006234A0_lifted(::recomp::Ctx& c);

namespace {
int SimMult() {
  static const int k = [] {
    const char* e = recomp::Env("SIM_MULT");
    return e ? std::atoi(e) : 1;
  }();
  return k;
}
}  // namespace

void f_001EDDC0(::recomp::Ctx& c) {
  const bool fps60 = uint32_t(c.rcx) != 0;
  f_001EDDC0_lifted(c);
  const int k = SimMult();
  if (!fps60 || k <= 1) return;
  uint8_t* B = recomp::g_guest_base;
  const int32_t ticks = 2 * k;
  const float fticks = float(ticks), scale = 1.0f / float(ticks);
  std::memcpy(B + 0x1DFE0B4, &ticks, 4);
  std::memcpy(B + 0x1DFE0B8, &fticks, 4);
  std::memcpy(B + 0x1DFE0BC, &scale, 4);
  std::fprintf(stderr, "[frame_rate] SIM_MULT=%d: %d frames per 30 Hz tick, scale %g\n", k, ticks, scale);
}

// FFXII_SNAP=start,count,interval (wall seconds since the first limited
// frame): writes the image's .data section (0x1DF0000, 0x11A7530 bytes) to
// snap-NN-<frame>.bin, for finding camera and object state by diffing.
void MaybeSnap() {
  static const char* e = recomp::Env("SNAP");
  if (!e) return;
  static double start = 0, interval = 1;
  static int count = 0, taken = 0;
  static uint64_t frame = 0;
  static auto t0 = std::chrono::steady_clock::now();
  static const bool parsed = std::sscanf(e, "%lf,%d,%lf", &start, &count, &interval) >= 2;
  ++frame;
  if (!parsed || taken >= count) return;
  const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  if (t < start + taken * interval) return;
  char name[64];
  std::snprintf(name, sizeof name, "snap-%02d-%06llu.bin", taken, (unsigned long long)frame);
  if (FILE* f = std::fopen(name, "wb")) {
    std::fwrite(recomp::g_guest_base + 0x1DF0000, 1, 0x11A7530, f);
    std::fclose(f);
  }
  std::fprintf(stderr, "[snap] %s at %.2f s\n", name, t);
  ++taken;
}

void f_006234A0(::recomp::Ctx& c) {
  MaybeSnap();
  const int k = SimMult();
  if (k <= 1) return f_006234A0_lifted(c);
  uint8_t* B = recomp::g_guest_base;
  static double a, b;  // original constants, read once
  static const bool init = (std::memcpy(&a, B + 0x9010A0, 8), std::memcpy(&b, B + 0x900378, 8), true);
  (void)init;
  const double ak = a / k, bk = b / k;
  std::memcpy(B + 0x9010A0, &ak, 8);
  std::memcpy(B + 0x900378, &bk, 8);
  f_006234A0_lifted(c);
  std::memcpy(B + 0x9010A0, &a, 8);
  std::memcpy(B + 0x900378, &b, 8);
  static auto t0 = std::chrono::steady_clock::now();
  static uint32_t ticks0 = 0, frames = 0;
  ++frames;
  const auto now = std::chrono::steady_clock::now();
  const double dt = std::chrono::duration<double>(now - t0).count();
  if (dt >= 5.0) {
    uint32_t ticks;
    std::memcpy(&ticks, B + 0x208F570, 4);
    std::fprintf(stderr, "[frame_rate] %.1f fps, %.1f ticks/s\n", frames / dt, (ticks - ticks0) / dt);
    ticks0 = ticks;
    frames = 0;
    t0 = now;
  }
}
}  // namespace recomp::gen
