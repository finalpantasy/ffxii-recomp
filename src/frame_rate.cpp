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

#include "fps_menu.h"
#include <map>
#include <set>
#include <vector>

#include "direct_hooks.h"
#include "guest_globals.h"
#include "runtime/env.h"
#include "runtime/platform.h"
#include "x86/lifted.h"
#include "x86/probe_hook.h"
#include "x86/runtime.h"

namespace recomp::hle {
// SwARc dxgi.cpp, --fps-overlay: a third number under the frame rate.
extern double (*g_fps_overlay_extra)();
// SwARc xinput.cpp: true gives the game the same input as its last read.
extern bool (*g_input_hold)(int kind);
// SwARc dxgi.cpp: the sync interval for each present; null keeps the game's.
extern uint32_t (*g_dxgi_present_interval)(uint32_t interval);
}  // namespace recomp::hle

namespace {
// 1/60 s game ticks per second since the previous call (the overlay calls it
// every 0.5 s). 60 means the game runs at normal speed.
double OverlayTicksPerSecond() {
  static uint32_t last_ticks = 0;
  static auto last_time = std::chrono::steady_clock::now();
  const uint32_t t = ::ffxii::guest::kTicks.Get();
  const auto now = std::chrono::steady_clock::now();
  const double dt = std::chrono::duration<double>(now - last_time).count();
  const double rate = dt > 0 ? double(uint32_t(t - last_ticks)) / dt : 0;
  last_ticks = t;
  last_time = now;
  return rate > 1000 ? 0 : rate;  // the counter starts or resets
}
const bool kOverlayExtra = (::recomp::hle::g_fps_overlay_extra = &OverlayTicksPerSecond, true);

// The windowed and borderless limiter path (0x6237D0) presents with interval
// 0 whatever the Vsync setting says (Kimi, win-kimi-vsync-borderless.log).
// Keep a nonzero request; otherwise sync to the display when Vsync is On.
uint32_t PresentInterval(uint32_t requested) {
  if (requested) return requested;
  const uint64_t settings = ::ffxii::guest::kSettings.Get();
  if (!settings) return 0;
  return *::ffxii::guest::At<uint32_t>(settings + ::ffxii::guest::kSettingVSync) ? 1 : 0;
}
const bool kPresentInterval = (::recomp::hle::g_dxgi_present_interval = &PresentInterval, true);
}  // namespace

namespace recomp::ffxii {
void MaybeStartWatch(uint64_t frame);
void CbScanFrame();
bool g_skip_frame = false;  // this frame skips the driver (FFXII_SKIP_DRIVER)
uint64_t LimitedFrames();
void SpeedLog();
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
void f_0022A580_lifted(::recomp::Ctx& c);
void f_0060EC80_lifted(::recomp::Ctx& c);
void f_0060A410_lifted(::recomp::Ctx& c);
void f_0022B6E0_lifted(::recomp::Ctx& c);
void f_0022B8C0_lifted(::recomp::Ctx& c);
void f_00184950_lifted(::recomp::Ctx& c);
void f_001EDEC0_lifted(::recomp::Ctx& c);
void f_0032AE70_lifted(::recomp::Ctx& c);

namespace {
// Set at startup (0x1EDDC0) from the Frame Rate Limit choice saved by the
// options menu (fps_menu.h), unless --fps / FFXII_SIM_MULT gives one.
int g_menu_mult = 0;
int EnvSimMult() {
  static const int k = [] {
    const char* e = recomp::Env("SIM_MULT");
    return e ? std::atoi(e) : 0;
  }();
  return k;
}
int SimMult() {
  if (const int k = EnvSimMult(); k > 0) return k;
  return g_menu_mult > 0 ? g_menu_mult : 1;
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
  std::memcpy(m, ::ffxii::guest::kCameraWorld.Ptr(), sizeof m);
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
  ticks = ::ffxii::guest::kTicks.Get();
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
  if (fps60 && EnvSimMult() <= 0) {
    const int saved = ::ffxii::fps_menu::SavedRate();
    if (saved == 120 || saved == 240) g_menu_mult = saved / 60;
  }
  f_001EDDC0_lifted(c);
  const int k = SimMult();
  if (!fps60 || k <= 1) return;
  uint8_t* B = recomp::g_guest_base;
  const int32_t ticks = 2 * k;
  const float fticks = float(ticks), scale = 1.0f / float(ticks);
  ::ffxii::guest::kFramesPerTick.Set(ticks);
  ::ffxii::guest::kFramesPerTickF.Set(fticks);
  ::ffxii::guest::kFrameTimeScale.Set(scale);
  std::fprintf(stderr, "[frame_rate] SIM_MULT=%d: %d frames per 30 Hz tick, scale %g\n", k, ticks, scale);
}

namespace {
// Native k*60 fps (SIM_MULT=k in 60 fps mode): only every k-th frame is a
// tick frame, where 1/60 s of tick-counted game time passes.
bool HighRate() {
  return SimMult() > 1 && ::ffxii::guest::kFps60Flag.Get() != 0;
}
// A behind game runs 2-3 updates (driver calls) per presented frame, so
// tick frames are counted in updates, not in limited frames.
// Each driver keeps its own count: the field frame runs more than one driver
// (sharing one count made the same driver land on every tick).
enum Driver { kMain, kField, kMode0, kHandler, kDrivers };
uint64_t g_updates = 0;               // all driver calls
uint64_t g_driver_updates[kDrivers] = {};
uint64_t g_driver_frame[kDrivers] = {};  // calls in the current limited frame
uint64_t g_driver_hist[kDrivers][4] = {};  // calls per limited frame: 0/1/2/3+
bool g_tick_now = true;
uint64_t g_limiter_ns = 0, g_limiter_per[4] = {};
// Calls at the 0x32D3D0 filter stores (flagged, plain) and the last value.
uint64_t g_filter_calls[2] = {};
float g_filter_last[2] = {};
// Entry counts for manifest `trace_` hooks (game thread only).
constexpr int kTraceSlots = 16;
uint64_t g_trace_va[kTraceSlots] = {}, g_trace_calls[kTraceSlots] = {};
uint64_t g_field_pre = 0, g_field_loop = 0, g_field_other = 0;
bool TickFrame() { return g_tick_now; }
// Input order trace: FFXII_INPUT_TRACE=<frame> logs 300 events from that
// limited frame on (pad/key reads, pacing, driver calls) with the tick flag.
uint64_t g_frame_no = 0;
// The driver that ran last and the frame it ran in (for the pad drain gate).
Driver g_last_driver = kMain;
uint64_t g_last_driver_frame = ~0ull;
uint64_t TraceStart() {
  static const uint64_t v = [] {
    const char* e = recomp::Env("INPUT_TRACE");
    return e ? std::strtoull(e, nullptr, 10) : ~0ull;
  }();
  return v;
}
void TraceInput(const char* what, int arg) {
  static int left = 300;
  if (g_frame_no < TraceStart() || left <= 0) return;
  --left;
  std::fprintf(stderr, "[input] f%llu %s %d tick %d@", (unsigned long long)g_frame_no, what, arg,
               int(g_tick_now));
}
void BeginUpdate(Driver d) {
  ++g_updates;
  ++g_driver_frame[d];
  g_tick_now = g_driver_updates[d]++ % uint64_t(SimMult()) == 0;
  g_last_driver = d;
  g_last_driver_frame = g_frame_no;
  TraceInput("driver", int(d));
}
// Pad and keyboard reads (SwARc g_input_hold): traced only. Holding the read
// state does not help: the pad samples are queued and drained by 0x36FDF0.
bool InputHold(int kind) {
  TraceInput(kind ? "keys" : "pad", kind);
  return false;
}
const bool kInputHold = (::recomp::hle::g_input_hold = &InputHold, true);

// 0x36FDF0 drains the queued pad samples and ORs them into the game's pad
// words (held 0x2AD9E04, pressed 0x2AD9E08, released 0x2AD9E0C, repeat
// 0x2AD9E10; copies at +0x10). Menus and the field read "pressed" only on
// tick frames, so at k*60 fps a press drained on an in-between frame was
// cleared by the next drain before anything read it: confirm needed several
// presses (owner, 2026-11-26). On in-between frames the drain is skipped and
// the samples wait in the queue for the next tick frame.
// FFXII_PAD_EVERY_FRAME=1 drains every frame (the old behavior, for A/B).
recomp::GuestFn g_pad_update_orig = nullptr;
uint64_t g_pad_runs = 0, g_pad_skips = 0;
uint64_t g_pad_pressed = 0;  // tick-frame drains that left a "pressed" bit
uint64_t g_pad_words[4] = {};  // tick-frame drains with a bit in word +4/+8/+C/+10
bool PadEveryFrame() {
  static const bool v = recomp::Env("PAD_EVERY_FRAME") != nullptr;
  return v;
}
// FFXII_PAD_KEEP_EDGES=1 leaves the edge words set on skipped drains (A/B).
bool PadKeepEdges() {
  static const bool v = recomp::Env("PAD_KEEP_EDGES") != nullptr;
  return v;
}
void PadUpdateHook(::recomp::Ctx& c) {
  if (HighRate() && !g_tick_now && !PadEveryFrame()) {
    ++g_pad_skips;
    TraceInput("padupdate-skip", 0);
    // Do what the drain does with an empty queue: clear the edge words
    // (pressed, released, repeat and their copies; 0x36FE14..0x36FE3D), so an
    // edge lasts one update. Left set, code that runs every update saw each
    // press twice: Y opened the main menu and closed it again, B confirmed
    // twice (2026-11-26, win-lead-in120a/b). Held words stay.
    if (recomp::g_guest_base && !PadKeepEdges())
      for (const uint64_t va : {0x2AD9E08ull, 0x2AD9E0Cull, 0x2AD9E10ull, 0x2AD9E24ull, 0x2AD9E28ull,
                                0x2AD9E2Cull})
        std::memset(recomp::g_guest_base + va, 0, 4);
    c.rsp += 8;  // ret
    return;
  }
  ++g_pad_runs;
  TraceInput("padupdate", 0);
  g_pad_update_orig(c);
  // What tick-frame logic will see: count drains that leave a press.
  if (g_tick_now && recomp::g_guest_base) {
    // Words +4, +8, +C, +10 (two devices each): count drains with any bit.
    uint16_t w[8];
    std::memcpy(w, recomp::g_guest_base + 0x2AD9E04, sizeof w);
    for (int i = 0; i < 4; ++i)
      if (w[2 * i] | w[2 * i + 1]) ++g_pad_words[i];
    if (w[2] | w[3]) ++g_pad_pressed;
  }
}
// 0x35FCA0 (called from the per-frame callback 0x35EC90) queues one pad
// sample per pad in the ring at 0x22CAE90 (write head, read head, 16
// samples of 0x24 bytes, 0x294 bytes per pad). 0x35FF90 then lets the drain
// take at most one sample per tick (0x184950), and the key-repeat and rumble
// timers (pad +4/+5, 0x35FBD0; 0x35F690) count samples. Queued every
// rendered frame at k*60 fps, the backlog sat at 9-13 of 16 samples, so
// presses arrived about 10 ticks late or were overwritten
// (win-lead-ring120.log), and key repeat ran k times faster: one dpdown
// moved the menu cursor two rows (win-lead-fix120b). On in-between frames
// no sample is queued, so the game sees its 60 Hz stream.
recomp::GuestFn g_pad_sample_orig = nullptr;
uint64_t g_pad_samples = 0, g_pad_sample_skips = 0;
void PadSampleHook(::recomp::Ctx& c) {
  if (HighRate() && !g_tick_now && !PadEveryFrame()) {
    ++g_pad_sample_skips;
    TraceInput("padsample-skip", 0);
    c.rsp += 8;  // ret
    return;
  }
  ++g_pad_samples;
  TraceInput("padsample", 0);
  g_pad_sample_orig(c);
}
void InstallPadUpdateHook() {
  static bool done = false;
  if (done) return;
  done = true;
  if (!HighRate()) return;
  if (!recomp::SetEntryHook(0x36FDF0, &PadUpdateHook, &g_pad_update_orig) || !g_pad_update_orig)
    std::fprintf(stderr, "[frame_rate] pad update hook 36FDF0 not installed@");
  if (!recomp::SetEntryHook(0x35FCA0, &PadSampleHook, &g_pad_sample_orig) || !g_pad_sample_orig)
    std::fprintf(stderr, "[frame_rate] pad sample hook 35FCA0 not installed@");
}
uint64_t g_tick_hist[2][4] = {};  // ticks added per update: [tick update][0/1/2/3+]
uint32_t Ticks() {
  return ::ffxii::guest::kTicks.Get();
}
void SetTicks(uint32_t v) { ::ffxii::guest::kTicks.Set(v); }
void CountUpdate(uint32_t before) {
  const uint32_t d = Ticks() - before;
  g_tick_hist[TickFrame()][d < 3 ? d : 3]++;
}

// Return addresses of the 0x184950 calls whose caller advances tick-counted
// time by the result: the tick counter adds in the drivers (0x250540,
// 0x2506D0, 0x2507C0), the play-time and timeline steps the field driver
// passes it to (0x254CC0, 0x367200) and the timeline module
// 0x35FF90/0x360080. On other frames they get 0. Callers that divide by it
// (0x361170, 0x361200), compare it with 1 and 2 (0x1D7DD0, 0x1D8520,
// 0x22A580) or turn it into seconds per frame (0x1E03F0, 0x1E04F0, see
// FpsHook) keep the original 1.
bool TickGatedCaller(uint64_t ret) {
  switch (ret - 5) {
    case 0x250582: case 0x2506DD: case 0x2507FC: case 0x25080C: case 0x2508B5:
    case 0x35FFE3: case 0x360046: case 0x36008D:
      return true;
    default:
      return false;
  }
}
}  // namespace

// 0x184950 returns the 1/60 s ticks of this frame ([0x1F3DA18], 1 at 60 fps).
void f_00184950(::recomp::Ctx& c) {
  uint64_t ret;
  std::memcpy(&ret, recomp::g_guest_base + c.rsp, 8);
  f_00184950_lifted(c);
  if (HighRate() && !TickFrame() && TickGatedCaller(ret)) c.rax = 0;
}

// 0x32AE70 sets the per-frame decay factor [0x22C3180]: 0.5 at 30 fps and
// sqrt(0.5) at 60, so it halves per 1/30 s. At k*60 fps: 0.5^(1/(2k)).
void f_0032AE70(::recomp::Ctx& c) {
  f_0032AE70_lifted(c);
  if (!HighRate()) return;
  const float f = float(std::pow(0.5, 1.0 / (2.0 * SimMult())));
  ::ffxii::guest::kDecayPerFrame.Set(f);
}

// 0x1EDEC0(counter): whether a 30 Hz step falls on this frame for a task
// whose per-frame counter (+0xB4) is `counter`. At 60 fps it is every second
// count (~counter & 1); at k*60 fps it is every 2k-th.
void f_001EDEC0(::recomp::Ctx& c) {
  const uint32_t counter = uint32_t(c.rcx);
  f_001EDEC0_lifted(c);
  const int32_t flag = ::ffxii::guest::kStep30HzFlag.Get();
  if (HighRate() && flag == 0) c.rax = counter % uint32_t(2 * SimMult()) == 0 ? 1 : 0;
}

// Instruction-level fixes for k*60 fps (manifest `hook` lines, which run
// before the instruction with registers in Ctx). Each site hard-codes the
// 60 fps case as "every second frame" or "half a 30 Hz step".
namespace {
uint64_t g_518_addr = 0;  // the byte 0x519001 changed, restored at 0x519015
uint8_t g_518_byte = 0;
// FFXII_FILTER_LOG=1: one line per 0x32D3D0 store (seconds, site, old, new),
// to compare the filter's response at different --fps.
void LogFilter(uint64_t va, float before, float after) {
  static const bool on = recomp::Env("FILTER_LOG") != nullptr;
  if (!on) return;
  static const uint64_t t0 = recomp::platform::MonotonicNs();
  std::fprintf(stderr, "[filter] %.4f %s %.5g %.5g\n", (recomp::platform::MonotonicNs() - t0) / 1e9,
               va == 0x32D4E3 ? "flagged" : "plain", before, after);
}

bool FpsHook(recomp::Ctx& c, uint64_t va, const char* name) {
  (void)name;
  uint8_t* B = recomp::g_guest_base;
  const uint32_t per = uint32_t(2 * SimMult());  // frames per 30 Hz step
  if (va == 0x1D4965) {
    // Config > Graphics Settings > Frame Rate Limit. The retail row flips
    // FrameLimit (esi = new value, about to be stored); step through 30, 60,
    // 120 and 240 instead (bl != 0 steps down, as the handler's other rows
    // do). The game field is 1 for every choice from 60 up; fps_menu saves
    // the choice and relabels "60 FPS".
    static const int rates[] = {30, 60, 120, 240};
    const bool field60 = ::ffxii::guest::kSettings.Get() &&
                         *::ffxii::guest::At<uint32_t>(::ffxii::guest::kSettings.Get() +
                                                       ::ffxii::guest::kSettingFrameLimit) == 1;
    const int cur = ::ffxii::fps_menu::Choice(field60, 60 * SimMult());
    int i = 0;
    while (i < 3 && rates[i] != cur) ++i;
    i = (i + ((uint8_t(c.rbx) != 0) ? 3 : 1)) % 4;
    c.rsi = rates[i] != 30 ? 1 : 0;
    ::ffxii::fps_menu::SetChoice(rates[i]);
    return true;
  }
  if (name && std::strcmp(name, "fps_div") == 0) {
    // `div dword [0x1DFE0B4]` sites turn a per-tick amount (edx:eax) into a
    // per-update step. Integer division truncates: at k*60 fps `2 / 4` is 0,
    // so a fade or counter with that step never moves (the save-crystal
    // glow stayed on the party, 2026-11-26). Rewrite the dividend so this
    // update's quotient is its share of the amount, so that every `per`
    // consecutive updates add up to the amount exactly. The share is picked
    // by how many distinct updates have run this site, so all objects
    // handled by it in one update get the same step, and a site that only
    // runs on tick frames still cycles through every share.
    if (!HighRate() || uint32_t(c.rdx) != 0) return true;
    struct Site {
      uint64_t va, last_frame, updates;
    };
    static Site sites[64];
    Site* s = nullptr;
    for (Site& e : sites) {
      if (e.va == va || e.va == 0) {
        if (e.va == 0) e = Site{va, ~0ull, 0};
        s = &e;
        break;
      }
    }
    if (!s) return true;
    if (s->last_frame != g_frame_no) {
      s->last_frame = g_frame_no;
      ++s->updates;
    }
    const uint64_t a = uint32_t(c.rax);
    const uint64_t ph = s->updates % per;
    const uint64_t q = (ph + 1) * a / per - ph * a / per;
    c.rax = uint32_t(q * per);
    return true;
  }
  if (name && std::strncmp(name, "trace_", 6) == 0) {
    // Manifest `hook <VA> trace_<VA>` lines: count entries, printed with
    // the FFXII_SPEEDLOG stats.
    for (int i = 0; i < kTraceSlots; ++i) {
      if (g_trace_va[i] == va || g_trace_va[i] == 0) {
        g_trace_va[i] = va;
        g_trace_calls[i]++;
        break;
      }
    }
    return true;
  }
  switch (va) {
    case 0x519001: {
      // 0x518F50 counts [rbx] down from a scaled frame count and, at 60
      // fps, adds the acceleration [rbx+4] to the velocity [rbx-8] on odd
      // counts only (once per 30 Hz step); `test byte [rbx], 1` decides.
      // At k*60 the add belongs on every 2k-th count: set bit 0 of the
      // tested byte to that answer and put the byte back at 0x519015.
      if (!HighRate()) return true;
      uint32_t count;
      std::memcpy(&count, B + c.rbx, 4);
      g_518_addr = c.rbx;
      g_518_byte = B[c.rbx];
      B[c.rbx] = uint8_t((g_518_byte & ~1u) | (count % per == 1 ? 1u : 0u));
      return true;
    }
    case 0x519015:
      if (g_518_addr) {
        B[g_518_addr] = g_518_byte;
        g_518_addr = 0;
      }
      return true;
    case 0x3E5E03:
      // 0x3E5D60 sets up a fixed-point oscillator: its step [+0x4C6] is a
      // 30 Hz rate divided by frames per tick (2k), and in 60 fps mode
      // (0x3E5DF2) `add [+0x4C4], ax` advances the phase by one step, 1/60
      // s at k=1. Keep that 1/60 s at k*60: add k steps.
      if (HighRate()) {
        const uint32_t step = uint32_t(c.rax) & 0xFFFFu;
        c.rax = (c.rax & ~0xFFFFull) | ((step * uint32_t(SimMult())) & 0xFFFFu);
        static const bool log = recomp::Env("SPEEDLOG") != nullptr;
        static int logged = 0;
        if (log && logged < 8) {
          ++logged;
          std::fprintf(stderr, "[frame_rate] phase offset: step %u -> %u\n", step,
                       unsigned(c.rax & 0xFFFFu));
        }
      }
      return true;
    case 0x51B1A5:
      // 0x51B150 samples a 30-entry table by the frame counter [rbx+0x30]
      // (which wraps at 30 * [0x1DFE0B4]): at 60 fps entry counter / 2,
      // `shr r8d, 1` here, blended by (counter & 1) / 2. At k*60 the entry
      // is counter / 2k; make the shift produce it.
      if (HighRate()) c.r8 = (uint32_t(c.r8) / per) * 2;
      return true;
    case 0x51B1CF: {
      // xmm5 holds the blend fraction before `subss xmm8, xmm5`.
      if (!HighRate()) return true;
      const uint16_t counter = ::ffxii::guest::At<::ffxii::guest::CountedTable>(c.rbx)->frame_counter;
      const float f = float(counter % per) / float(per);
      std::memcpy(c.xmm[5].b, &f, 4);
      return true;
    }
    case 0x32D4E3:
    case 0x32D505: {
      {
        const int i = va == 0x32D4E3 ? 0 : 1;
        g_filter_calls[i]++;
        g_filter_last[i] = ::ffxii::guest::At<::ffxii::guest::AxisInput>(c.rdi)->filtered_x;
      }
      // 0x32D3D0 smooths [rdi+0xF8] toward x, a per-frame value (it is
      // divided by [0x1DFE0B4]). At 60 fps with flags 0x40300000 the step
      // is F = (x/2 + 5F)/6 (stored at 0x32D4E3 from xmm6), otherwise
      // F = (2F + x)/3 (stored at 0x32D505 from xmm0). At k*60, keep each
      // filter's retention per 1/60 s: (5/6)^(1/k) and (2/3)^(1/k) a frame.
      if (!HighRate()) {
        const float f0 = ::ffxii::guest::At<::ffxii::guest::AxisInput>(c.rdi)->filtered_x;
        float v0;
        std::memcpy(&v0, c.xmm[va == 0x32D4E3 ? 6 : 0].b, 4);
        LogFilter(va, f0, v0);
        return true;
      }
      const float f = ::ffxii::guest::At<::ffxii::guest::AxisInput>(c.rdi)->filtered_x;
      float v;
      const int reg = va == 0x32D4E3 ? 6 : 0;
      std::memcpy(&v, c.xmm[reg].b, 4);
      const double keep = va == 0x32D4E3 ? 5.0 / 6.0 : 2.0 / 3.0;
      const double target = va == 0x32D4E3 ? 6.0 * v - 5.0 * f : 3.0 * v - 2.0 * f;
      const double a = 1.0 - std::pow(keep, 1.0 / SimMult());
      v = float(f + (target - f) * a);
      std::memcpy(c.xmm[reg].b, &v, 4);
      LogFilter(va, f, v);
      return true;
    }
    case 0x1E043A:
    case 0x1E0528:
      // 0x1E03F0 (time debt: wall time minus ticks * 1/60 s, in 0x1F82D28)
      // and 0x1E04F0 (this frame's time) have loaded the 1/60 s interval
      // into xmm6 and now ask 0x184950 for this frame's ticks. When the debt
      // passes one frame, 0x17D6E0 runs the frame body again (0x1E05F0,
      // flag 0x1F82D30). At k*60 a frame is 1/(60k) s: keep its tick count
      // (not tick-gated) and divide the interval. Gating these to 0 made
      // every frame look late, so each present ran two frame bodies.
      if (HighRate()) {
        double v;
        std::memcpy(&v, c.xmm[6].b, 8);
        v /= SimMult();
        std::memcpy(c.xmm[6].b, &v, 8);
      }
      return true;
    case 0x62369C: {
      // The limiter 0x6234A0 has loaded its 1/60 s interval (0x9010A0 or
      // 0x900378) into xmm0; at k*60 fps it waits 1/(60k) s.
      const int k = LimitDiv();
      if (k > 1) {
        double v;
        std::memcpy(&v, c.xmm[0].b, 8);
        v /= k;
        std::memcpy(c.xmm[0].b, &v, 8);
      }
      return true;
    }
    default:
      return false;
  }
}
const bool g_fps_hook = (recomp::SetProbeHook(FpsHook), true);
}  // namespace

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
    std::fwrite(recomp::g_guest_base + ::ffxii::guest::kDataSectionVa, 1, ::ffxii::guest::kDataSectionSize, f);
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
  recomp::ffxii::SpeedLog();
  const int k = LimitDiv();
  uint8_t* B = recomp::g_guest_base;
  if (k <= 1) {
    f_006234A0_lifted(c);
    // The frame statistics below also serve as the 60 fps baseline
    // (FFXII_SPEEDLOG=1); otherwise nothing more to do.
    static const bool stats = recomp::Env("SPEEDLOG") != nullptr;
    if (!stats) return;
  } else {
    // The interval is divided by k at 0x62369C (FpsHook), not in memory:
    // game-thread code (0x1E03F0, 0x1D8240) reads the same constants while
    // this render-thread call sleeps.
    const uint64_t t0 = recomp::platform::MonotonicNs();
    f_006234A0_lifted(c);
    g_limiter_ns += recomp::platform::MonotonicNs() - t0;
    uint32_t per;
    per = ::ffxii::guest::kFramesPerPresent.Get();
    g_limiter_per[per < 3 ? per : 3]++;
  }
  static auto t0 = std::chrono::steady_clock::now();
  static uint32_t ticks0 = 0, frames = 0;
  ++frames;
  // Ticks added per update (0, 1, 2, 3+; g_tick_hist), split by tick update
  // or not: at k*60 fps a tick update should add 1 and any other 0. Also
  // the presented frames that ran more than one update (the game catching up).
  auto& hist = g_tick_hist;
  static uint64_t prev_updates = 0, multi_driver = 0;
  if (g_updates - prev_updates > 1) ++multi_driver;
  prev_updates = g_updates;
  for (int d = 0; d < kDrivers; ++d) {
    g_driver_hist[d][g_driver_frame[d] < 3 ? g_driver_frame[d] : 3]++;
    g_driver_frame[d] = 0;
  }
  const auto now = std::chrono::steady_clock::now();
  const double dt = std::chrono::duration<double>(now - t0).count();
  if (dt >= 5.0) {
    uint32_t ticks;
    ticks = ::ffxii::guest::kTicks.Get();
    std::fprintf(stderr,
                 "[frame_rate] %.1f fps, %.1f ticks/s; ticks per update: tick updates %llu/%llu/%llu/%llu, "
                 "others %llu/%llu/%llu/%llu (0/1/2/3+); frames with 2+ driver calls %llu\n",
                 frames / dt, (ticks - ticks0) / dt, (unsigned long long)hist[1][0], (unsigned long long)hist[1][1],
                 (unsigned long long)hist[1][2], (unsigned long long)hist[1][3], (unsigned long long)hist[0][0],
                 (unsigned long long)hist[0][1], (unsigned long long)hist[0][2], (unsigned long long)hist[0][3],
                 (unsigned long long)multi_driver);
    static const char* kName[kDrivers] = {"250540", "2507C0", "250730", "handler 22A770"};
    for (int d = 0; d < kDrivers; ++d)
      std::fprintf(stderr, "[frame_rate]   driver %s calls per frame %llu/%llu/%llu/%llu (0/1/2/3+)\n", kName[d],
                   (unsigned long long)g_driver_hist[d][0], (unsigned long long)g_driver_hist[d][1],
                   (unsigned long long)g_driver_hist[d][2], (unsigned long long)g_driver_hist[d][3]);
    std::fprintf(stderr, "[frame_rate]   2507C0 from 22AB88 %llu, 22AC1C %llu, other %llu\n",
                 (unsigned long long)g_field_pre, (unsigned long long)g_field_loop, (unsigned long long)g_field_other);
    g_field_pre = g_field_loop = g_field_other = 0;
    std::fprintf(stderr, "[frame_rate]   limiter %.2f ms per present; frames per present 0/1/2/3+: %llu/%llu/%llu/%llu"
                 "%c", frames ? g_limiter_ns / 1e6 / frames : 0.0, (unsigned long long)g_limiter_per[0],
                 (unsigned long long)g_limiter_per[1], (unsigned long long)g_limiter_per[2],
                 (unsigned long long)g_limiter_per[3], 10);
    std::fprintf(stderr, "[frame_rate]   filter 32D3D0 calls flagged %llu plain %llu, last %.4g %.4g\n",
                 (unsigned long long)g_filter_calls[0], (unsigned long long)g_filter_calls[1], g_filter_last[0],
                 g_filter_last[1]);
    g_filter_calls[0] = g_filter_calls[1] = 0;
    std::fprintf(stderr, "[frame_rate]   pad update 36FDF0 runs %llu skipped %llu, presses seen on tick frames %llu\n",
                 (unsigned long long)g_pad_runs, (unsigned long long)g_pad_skips,
                 (unsigned long long)g_pad_pressed);
    std::fprintf(stderr, "[frame_rate]   pad samples 35FCA0 queued %llu skipped %llu\n",
                 (unsigned long long)g_pad_samples, (unsigned long long)g_pad_sample_skips);
    std::fprintf(stderr, "[frame_rate]   pad words on tick frames +4 %llu +8 %llu +C %llu +10 %llu\n",
                 (unsigned long long)g_pad_words[0], (unsigned long long)g_pad_words[1],
                 (unsigned long long)g_pad_words[2], (unsigned long long)g_pad_words[3]);
    g_pad_runs = g_pad_skips = g_pad_pressed = 0;
    for (auto& n : g_pad_words) n = 0;
    for (int i = 0; i < kTraceSlots && g_trace_va[i]; ++i) {
      std::fprintf(stderr, "[frame_rate]   trace %06llX calls %llu\n", (unsigned long long)g_trace_va[i],
                   (unsigned long long)g_trace_calls[i]);
      g_trace_calls[i] = 0;
    }
    g_limiter_ns = 0;
    std::memset(g_limiter_per, 0, sizeof g_limiter_per);
    std::memset(g_driver_hist, 0, sizeof g_driver_hist);
    std::memset(hist, 0, sizeof hist);
    multi_driver = 0;
    ticks0 = ticks;
    frames = 0;
    t0 = now;
  }
}

// FFXII_TICKLOG=<first frame>,<count>: after each field-driver call, log the
// driver argument and the per-frame timing state the update leaves behind:
// 0x1DFE0C0 (30 Hz step flag), 0x1F3DA18 (ticks this frame), the tick
// counter 0x208F570, [[0x1F82CF0]+0x88] (0x17D930) and [[0x1F82E18]+0x118]
// (0x181DD0).
void MaybeTickLog(uint32_t arg) {
  static const char* e = recomp::Env("TICKLOG");
  if (!e) return;
  static uint64_t first = 0, count = 0;
  static bool parsed = false;
  if (!parsed) {
    parsed = true;
    std::sscanf(e, "%llu,%llu", (unsigned long long*)&first, (unsigned long long*)&count);
  }
  uint8_t* B = recomp::g_guest_base;
  auto rd32 = [&](uint64_t a) { uint32_t v; std::memcpy(&v, B + a, 4); return v; };
  auto rd64 = [&](uint64_t a) { uint64_t v; std::memcpy(&v, B + a, 8); return v; };
  const uint64_t o1 = ::ffxii::guest::kTimingObjA.Get(), o2 = ::ffxii::guest::kTimingObjB.Get();
  const unsigned f88 = o1 ? B[o1 + 0x88] : 255, f118 = o2 ? B[o2 + 0x118] : 255;
  static uint64_t calls = 0;
  ++calls;
  if (g_frames >= first && count) {
    --count;
    std::fprintf(stderr, "[ticklog] frame %llu arg %u flag %u n %u ticks %u o88 %u o118 %u\n",
                 (unsigned long long)g_frames, arg, unsigned(::ffxii::guest::kStep30HzFlag.Get()), unsigned(::ffxii::guest::kTicksThisFrame.Get()), ::ffxii::guest::kTicks.Get(), f88, f118);
  }
}

void f_00250540(::recomp::Ctx& c) {
  if (::ffxii::direct::Enter<0x00250540>(c)) return;
  if (SkipThisFrame()) {
    ++g_driver_skips;
    c.rsp += 8;  // ret
    return;
  }
  ++g_driver_runs;
  BeginUpdate(kMain);
  // At k*60 fps only tick updates advance the tick counter (its 0x2505C9 add
  // takes a register, not 0x184950).
  const bool hold = HighRate() && !TickFrame();
  const uint32_t ticks = Ticks();
  f_00250540_lifted(c);
  if (hold) SetTicks(ticks);
  CountUpdate(ticks);
}

// 0x2507C0 is the field driver (called from the frame handlers 0x22A770 and
// 0x22B7E0): update 0x184960, the tick add, four task lists, then 0x367200.
// FFXII_SKIP_DRIVER skips it the same way.
void f_002507C0(::recomp::Ctx& c) {
  if (::ffxii::direct::Enter<0x002507C0>(c)) return;
  if (SkipThisFrame()) {
    ++g_driver_skips;
    c.rsp += 8;  // ret
    return;
  }
  ++g_driver_runs;
  BeginUpdate(kField);
  {
    // Which call site in the field handler (0x22AB88 before its step loop,
    // 0x22AC1C inside it), for the 5 s log.
    uint64_t ret;
    std::memcpy(&ret, recomp::g_guest_base + c.rsp, 8);
    ++(ret == 0x22AB88 ? g_field_pre : ret == 0x22AC1C ? g_field_loop : g_field_other);
  }
  const uint32_t arg = uint32_t(c.rcx);
  const bool hold = HighRate() && !TickFrame();
  const uint32_t ticks = Ticks();
  f_002507C0_lifted(c);
  if (hold) SetTicks(ticks);
  CountUpdate(ticks);
  MaybeTickLog(arg);
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
// 0x22A580 is the frame body (one game frame: the mode handler, update and
// draw preparation). The frame limiter runs on the render thread, and at
// k*60 fps it does not hold the game thread to one frame per present (it ran
// two per present, so twice the game speed: win-sp120b.log). So at k*60 the
// game thread keeps its own fixed step of 1/(60k) s here, up to 4 frames
// behind before it drops the backlog.
void f_0022A580(::recomp::Ctx& c) {
  if (HighRate()) {
    static uint64_t next = 0;
    const uint64_t step = 1000000000ull / uint64_t(60 * SimMult());
    const uint64_t now = recomp::platform::MonotonicNs();
    if (next == 0 || now > next + 4 * step) next = now;
    if (now < next) recomp::platform::SleepUntilNs(next);
    next += step;
    // Frames that run no driver (pause, and the fade out of it) still need
    // every k-th frame to be a tick frame: the timeline module's 0x184950
    // calls are gated by TickFrame(). Without this the flag kept whatever
    // the last driver call left; when that was "no tick", the pause fade got
    // 0 ticks forever and the game stayed dimmed (alt-tab, 2026-11-26).
    // A driver called in this frame overrides it in BeginUpdate.
    //
    // When a driver ran in the last frame, predict its next call instead:
    // the pad drain 0x36FDF0 runs before the driver in each frame and gates
    // on this flag, so with the frame counter out of phase with the driver's
    // own count it drained on in-between updates and skipped tick updates
    // (presses then reached tick-frame logic only by luck; the main menu's
    // Y was lost in 2 of 3 runs, 2026-11-26, win-lead-intrace.log).
    static uint64_t frames = 0;
    if (g_last_driver_frame == g_frame_no)
      g_tick_now = g_driver_updates[g_last_driver] % uint64_t(SimMult()) == 0;
    else
      g_tick_now = frames % uint64_t(SimMult()) == 0;
    ++frames;
  }
  ++g_frame_no;
  TraceInput("pace", 0);
  InstallPadUpdateHook();
  f_0022A580_lifted(c);
}

void f_0022A770(::recomp::Ctx& c) {
  if (::ffxii::direct::Enter<0x0022A770>(c)) return;
  static const bool on = recomp::Env("SKIP_HANDLER") != nullptr;
  if (on && SkipThisFrame()) {
    ++g_handler_skips;
    c.rsp += 8;  // ret
    return;
  }
  ++g_driver_frame[kHandler];
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
  BeginUpdate(kMode0);
  // This driver adds 2 / [0x1DFE0B4] ticks a frame: 1 at 60 fps, but 0 in
  // integer division at k*60. Add the tick on tick updates instead.
  const uint32_t ticks = Ticks();
  f_00250730_lifted(c);
  if (HighRate()) SetTicks(ticks + (TickFrame() ? 1 : 0));
  CountUpdate(ticks);
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
  if (::ffxii::direct::Enter<0x00248030>(c)) return;
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
      std::map<uint64_t, int> cb;
      std::vector<uint64_t> stack{head};
      std::set<uint64_t> seen;
      while (!stack.empty() && seen.size() < 20000) {
        const uint64_t n = stack.back();
        stack.pop_back();
        if (!n || n >= 0x100000000ull || !seen.insert(n).second) continue;
        const auto* node = ::ffxii::guest::At<::ffxii::guest::TaskNode>(n);
        ++cb[node->handler];
        stack.push_back(node->link_18);
        stack.push_back(node->link_20);
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
  if (::ffxii::direct::Enter<0x00259240>(c)) return;
  f_00259240_lifted(c);
  static const bool on = recomp::Env("MODE_LOG") != nullptr;
  if (!on) return;
  static uint32_t prev = ~0u;
  static uint64_t calls = 0;
  ++calls;
  const uint32_t mode = uint32_t(int32_t(::ffxii::guest::kFrameMode.Get()));
  if (mode != prev) {
    prev = mode;
    std::fprintf(stderr, "[mode] %u at call %llu\n",prev, (unsigned long long)calls);
  }
}
}  // namespace recomp::gen

namespace recomp::ffxii {
uint64_t LimitedFrames() { return recomp::gen::g_frames; }
}  // namespace recomp::ffxii
