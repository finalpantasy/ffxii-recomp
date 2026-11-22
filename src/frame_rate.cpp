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
//
// Measurement switches (analysis/frame-timing.md, experiment 3):
//   FFXII_SKIP_DRIVER=k   limiter at 1/(60k) s; the drivers 0x250540 and
//                         0x2507C0 and the camera step 0x3820C0 run only when a
//                         1/60 s tick of wall time is due.
//   FFXII_SKIP_HANDLER=1  also skip the field handler 0x22A770 on other frames.
//   FFXII_CAMLOG=1        log ticks, call counters and the camera twice a second.
//   FFXII_MODE_LOG=1      log the byte 0x2064AD3 after 0x259240 (not frame-only).

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "runtime/env.h"
#include "x86/lifted.h"
#include "x86/runtime.h"

namespace recomp::ffxii {
void MaybeStartWatch(uint64_t frame);
void CbScanFrame();
bool g_skip_frame = false;  // this frame skips the driver (FFXII_SKIP_DRIVER)
}

namespace recomp::gen {
void f_001EDDC0_lifted(::recomp::Ctx& c);
void f_006234A0_lifted(::recomp::Ctx& c);
void f_00248030_lifted(::recomp::Ctx& c);
void f_00259240_lifted(::recomp::Ctx& c);
void f_00250540_lifted(::recomp::Ctx& c);
void f_00250730_lifted(::recomp::Ctx& c);
void f_002507C0_lifted(::recomp::Ctx& c);
void f_003820C0_lifted(::recomp::Ctx& c);
void f_0022A770_lifted(::recomp::Ctx& c);
void f_0060EC80_lifted(::recomp::Ctx& c);
void f_0060A410_lifted(::recomp::Ctx& c);
void f_0022B6E0_lifted(::recomp::Ctx& c);
void f_0022B8C0_lifted(::recomp::Ctx& c);

namespace {
int SimMult() {
  static const int k = [] {
    const char* e = recomp::Env("SIM_MULT");
    return e ? std::atoi(e) : 1;
  }();
  return k;
}
// FFXII_SKIP_DRIVER=k: the limiter runs k times per 1/60 s, and the per-frame
// driver 0x250540 (update and task lists) runs on every k-th frame only.
int SkipDriver() {
  static const int k = [] {
    const char* e = recomp::Env("SKIP_DRIVER");
    return e ? std::atoi(e) : 1;
  }();
  return k;
}
int LimitDiv() { return SimMult() > SkipDriver() ? SimMult() : SkipDriver(); }
uint64_t g_frames = 0;  // limited frames so far

// With FFXII_SKIP_DRIVER=k > 1, a driver call runs only when a 1/60 s tick of
// wall time is due (fixed-step accumulator, at most 4 ticks behind), so the
// game advances at real speed at any frame rate. Decided once per frame.
bool SkipThisFrame() {
  if (SkipDriver() <= 1) return false;
  using clock = std::chrono::steady_clock;
  static uint64_t decided = ~0ull;
  static bool skip = false;
  static clock::time_point next = clock::now();
  if (decided == g_frames) return skip;
  decided = g_frames;
  const auto now = clock::now();
  const auto tick = std::chrono::nanoseconds(16666667);
  if (now < next) {
    skip = true;
  } else {
    skip = false;
    next += tick;
    if (now - next > 4 * tick) next = now;
  }
  recomp::ffxii::g_skip_frame = skip;
  return skip;
}
uint64_t g_driver_runs = 0, g_driver_skips = 0;
std::atomic<uint64_t> g_render_calls{0};  // 0x60EC80, render thread
std::atomic<uint64_t> g_frame_renders{0};  // 0x60A410, render thread
uint64_t g_calls_250730 = 0, g_calls_22B6E0 = 0, g_calls_22B8C0 = 0;

// FFXII_CAMLOG=1: twice a second, log the tick counter, the camera position
// and yaw (camera world transform at 0x20975A0), to measure game speed.
void MaybeCamLog() {
  static const bool on = recomp::Env("CAMLOG") != nullptr;
  if (!on) return;
  static auto t0 = std::chrono::steady_clock::now();
  static double next = 0;
  float m[16];
  std::memcpy(m, recomp::g_guest_base + 0x20975A0, sizeof m);
  // Yaw change per frame, summed by whether the frame that just ended ran
  // the driver (run) or skipped it (skip, FFXII_SKIP_DRIVER only).
  static float prev_yaw = 0;
  static double turn_run = 0, turn_skip = 0;
  static uint64_t skips_seen = 0;
  const float yaw = std::atan2(m[8], m[10]) * 57.29578f;
  float dy = yaw - prev_yaw;
  while (dy > 180) dy -= 360;
  while (dy < -180) dy += 360;
  prev_yaw = yaw;
  (g_driver_skips != skips_seen ? turn_skip : turn_run) += std::fabs(dy);
  skips_seen = g_driver_skips;
  const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  if (t < next) return;
  next = t + 0.5;
  uint32_t ticks;
  std::memcpy(&ticks, recomp::g_guest_base + 0x208F570, 4);
  std::fprintf(stderr, "[cam] t %.2f fr %llu rend %llu h0 %llu h2 %llu tk %llu drv %llu/%llu frame %llu ticks %u pos %.3f %.3f %.3f yaw %.2f turn %.1f/%.1f\n", t,
               (unsigned long long)g_frame_renders.load(), (unsigned long long)g_render_calls.load(),
               (unsigned long long)g_calls_22B6E0, (unsigned long long)g_calls_22B8C0,
               (unsigned long long)g_calls_250730, (unsigned long long)g_driver_runs, (unsigned long long)g_driver_skips,
               (unsigned long long)g_frames, ticks, m[12], m[13], m[14],
               yaw, turn_run, turn_skip);
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
  ++g_frames;
  recomp::ffxii::MaybeStartWatch(g_frames);
  recomp::ffxii::CbScanFrame();
  MaybeCamLog();
  const int k = LimitDiv();
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

void f_00250540(::recomp::Ctx& c) {
  if (SkipThisFrame()) {
    ++g_driver_skips;
    c.rsp += 8;  // ret
    return;
  }
  ++g_driver_runs;
  f_00250540_lifted(c);
}

// 0x2507C0 is the field driver (called from the frame handlers 0x22A770 and
// 0x22B7E0): update 0x184960, the tick add, four task lists, then 0x367200.
// FFXII_SKIP_DRIVER skips it the same way.
void f_002507C0(::recomp::Ctx& c) {
  if (SkipThisFrame()) {
    ++g_driver_skips;
    c.rsp += 8;  // ret
    return;
  }
  ++g_driver_runs;
  f_002507C0_lifted(c);
}

// 0x3820C0 is the scene camera step (reached through 0x36F510 from the frame
// handlers, once per rendered frame, outside the driver). It reads the
// camera stick, so with FFXII_SKIP_DRIVER it also runs on tick frames only.
uint64_t g_camera_skips = 0;
void f_003820C0(::recomp::Ctx& c) {
  if (SkipThisFrame()) {
    ++g_camera_skips;
    c.rsp += 8;  // ret
    return;
  }
  f_003820C0_lifted(c);
}

// FFXII_SKIP_HANDLER=1 (with FFXII_SKIP_DRIVER): skip the whole field frame
// handler 0x22A770 on in-between frames, to see what the render side then
// presents.
uint64_t g_handler_skips = 0;
void f_0022A770(::recomp::Ctx& c) {
  static const bool on = recomp::Env("SKIP_HANDLER") != nullptr;
  if (on && SkipThisFrame()) {
    ++g_handler_skips;
    c.rsp += 8;  // ret
    return;
  }
  f_0022A770_lifted(c);
}

// 0x60A410(renderer, a, b) runs one render-ring command on the render thread
// (the loop 0x6106A0 calls it for command 1 with the slot's +0x30/+0x38
// words); about 5.4 per presented frame in the field. Counted for the log.
void f_0060A410(::recomp::Ctx& c) {
  g_frame_renders.fetch_add(1, std::memory_order_relaxed);
  f_0060A410_lifted(c);
}

// 0x60EC80 runs the render packet consumer 0x629970 (render thread).
void f_0060EC80(::recomp::Ctx& c) {
  g_render_calls.fetch_add(1, std::memory_order_relaxed);
  f_0060EC80_lifted(c);
}

// Call counters for the camera log: frame handlers for modes 0 and 2, and
// 0x250730, the mode-0 tick driver.
void f_00250730(::recomp::Ctx& c) {
  ++g_calls_250730;
  f_00250730_lifted(c);
}
void f_0022B6E0(::recomp::Ctx& c) {
  ++g_calls_22B6E0;
  f_0022B6E0_lifted(c);
}
void f_0022B8C0(::recomp::Ctx& c) {
  ++g_calls_22B8C0;
  f_0022B8C0_lifted(c);
}

// FFXII_TASKS=<call>[,<call>...]: on those calls of the task runner 0x248030 per list
// head, walk the task tree (callback +0, parent +0x10, first child +0x18, next sibling +0x20; 0x2482E0 unlinks) and
// log each distinct callback with its count.
void f_00248030(::recomp::Ctx& c) {
  static const char* e = recomp::Env("TASKS");
  if (e) {
    static std::map<uint64_t, int> calls;
    const uint64_t head = c.rcx;
    const int k = ++calls[head];
    bool hit = false;
    for (const char* p = e; *p;) {  // comma-separated call numbers
      if (std::atoi(p) == k) hit = true;
      while (*p && *p != ',') ++p;
      if (*p) ++p;
    }
    if (hit) {
      uint8_t* B = recomp::g_guest_base;
      auto Rd = [B](uint64_t a) { uint64_t v; std::memcpy(&v, B + a, 8); return v; };
      std::map<uint64_t, int> cb;
      std::vector<uint64_t> stack{head};
      std::set<uint64_t> seen;
      while (!stack.empty() && seen.size() < 20000) {
        const uint64_t n = stack.back();
        stack.pop_back();
        if (!n || n >= 0x100000000ull || !seen.insert(n).second) continue;
        ++cb[Rd(n)];
        stack.push_back(Rd(n + 0x18));
        stack.push_back(Rd(n + 0x20));
      }
      std::fprintf(stderr, "[tasks] list 0x%llx: %zu nodes\n",(unsigned long long)head, seen.size());
      for (auto& [f, k] : cb) std::fprintf(stderr, "[tasks]   cb 0x%llx x%d\n",(unsigned long long)f, k);
    }
  }
  f_00248030_lifted(c);
}

// The frame body 0x22A580 calls 0x259240, then switches on the signed byte
// at 0x2064AD3 (cases 0-6). FFXII_MODE_LOG=1 logs each change of that byte
// with the call count.
void f_00259240(::recomp::Ctx& c) {
  f_00259240_lifted(c);
  static const bool on = recomp::Env("MODE_LOG") != nullptr;
  if (!on) return;
  static uint32_t prev = ~0u;
  static uint64_t calls = 0;
  ++calls;
  const uint32_t mode = uint32_t(int32_t(int8_t(recomp::g_guest_base[0x2064AD3])));
  if (mode != prev) {
    prev = mode;
    std::fprintf(stderr, "[mode] %u at call %llu\n",prev, (unsigned long long)calls);
  }
}
}  // namespace recomp::gen
