// Field-interaction probe (FFXII_PROBE=1). Wraps the functions the talk
// check depends on (static trace, 2026-11-21) and logs what they return:
//   002E0FA0 flags OR of 0x215F1C0..0x215F200; bit 0 blocks the menu, bit 4 talk
//   002647D0 current talk target, 0 = none (also feeds "wait talk")
//   003DA5A0 signed ellipse distance (xmm0); a candidate needs < 0
//   003A1BB0 facing-cone test (al)
// Without FFXII_PROBE each wrapper only calls the lifted body.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "direct_hooks.h"
#include "guest_globals.h"
#include "runtime/env.h"
#include "x86/lifted.h"
#include "x86/runtime.h"

namespace recomp::ffxii {
uint64_t LimitedFrames();
}
namespace recomp::gen {
void f_002E0FA0_lifted(::recomp::Ctx& c);
void f_002647D0_lifted(::recomp::Ctx& c);
void f_003DA5A0_lifted(::recomp::Ctx& c);
void f_003A1BB0_lifted(::recomp::Ctx& c);
}  // namespace recomp::gen

namespace recomp {
using TestCondition = bool (*)(const char* arg, double waited, float* lx, float* ly, uint16_t* buttons);
void RegisterTestCondition(const char* name, TestCondition fn);
using TestClock = double (*)();
void SetTestClock(TestClock fn);
}  // namespace recomp

namespace {

std::atomic<uint64_t> g_player_pos{0};  // guest address of the player's x, y, z
std::atomic<uint64_t> g_talk_target{0};  // last value 2647D0 returned
std::atomic<uint32_t> g_flags{~0u};      // last value 2E0FA0 returned
std::atomic<int64_t> g_search_ns{0};     // steady-clock time of the last talk search

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

bool On() {
  static const bool on = recomp::Env("PROBE") != nullptr;
  return on;
}

struct Counts {
  uint64_t neg = 0, pos = 0, nan = 0, facing_yes = 0, facing_no = 0;
  float min = INFINITY;
  char why[160] = "";
};
Counts g_n;
std::map<uint64_t, uint64_t> g_callers;  // 3DA5A0 return address -> calls

void MaybeFlush() {
  using clock = std::chrono::steady_clock;
  static auto last = clock::now();
  const auto now = clock::now();
  if (now - last < std::chrono::seconds(1)) return;
  last = now;
  if (g_n.neg + g_n.pos + g_n.nan + g_n.facing_yes + g_n.facing_no)
    std::fprintf(stderr, "[probe] ellipse neg %llu pos %llu nan %llu min %g (%s) | facing yes %llu no %llu\n",
                 (unsigned long long)g_n.neg, (unsigned long long)g_n.pos, (unsigned long long)g_n.nan,
                 g_n.min, g_n.why, (unsigned long long)g_n.facing_yes, (unsigned long long)g_n.facing_no);
  if (!g_callers.empty()) {
    std::string line;
    char buf[48];
    for (auto& [ra, n] : g_callers) {
      std::snprintf(buf, sizeof buf, " %llx:%llu", (unsigned long long)ra, (unsigned long long)n);
      line += buf;
    }
    std::fprintf(stderr, "[probe] 3DA5A0 callers%s\n", line.c_str());
    g_callers.clear();
  }
  g_n = {};
}

// ---- conditions for FFXII_TEST_INPUT "wait" lines -------------------------
// (RegisterTestCondition lives in recomp_win32, win32/input_state.h.)
//   wait talk                 a talk target is in range (2647D0 returns non-zero)
//   wait near x,z,r           the player is within r of (x, z)
//   wait walkto x,z,r         steer the left stick until within r of (x, z)
//   wait free                 the player has control (no event or dialog)
//   wait busy                 an event or dialog holds the player
//   wait advance              tap B until the player has control for 1 s
//   skipto <t2> field           the field runs (talk search seen in 0.5 s)
// Events and dialogs set bit 0x10000 of the 2E0FA0 flags (0x0013xxxx);
// in free play the flags are 0x14x-0x17x.
// The player's position comes from the talk-target search (field only).

float Rf(uint64_t a) {
  float v;
  std::memcpy(&v, recomp::g_guest_base + a, 4);
  return v;
}

bool PlayerXZ(float* x, float* z) {
  const uint64_t p = g_player_pos.load(std::memory_order_relaxed);
  if (!p) return false;
  *x = Rf(p);
  *z = Rf(p + 8);
  return true;
}

}  // namespace

namespace recomp::ffxii {
// FFXII_SPEEDLOG=1: once per 60 ticks (1 s of game time), log the camera's
// path length and the camera's summed turn over those ticks, with the frames
// they took. At a correct speed both match between 60 and k*60 fps; only
// the frame count differs. Called once per limited frame.
void SpeedLog() {
  // FFXII_PADLOG=1: log each change of the pad word that 0x46DB40 returns
  // (guest_globals.h kPadButtons).
  static const bool pad_on = recomp::Env("PADLOG") != nullptr;
  if (pad_on && recomp::g_guest_base) {
    static uint16_t prev = 0;
    const uint16_t w = ::ffxii::guest::kPadButtons.Get();
    if (w != prev) {
      std::fprintf(stderr, "[pad] 0x%04x -> 0x%04x ticks %u\n", prev, w, ::ffxii::guest::kTicks.Get());
      prev = w;
    }
  }
  static const bool on = recomp::Env("SPEEDLOG") != nullptr;
  if (!on || !recomp::g_guest_base) return;
  static uint32_t start = 0, prev_ticks = 0;
  static uint64_t frames = 0;
  static double path = 0, turn = 0;
  static float px = 0, pz = 0, pyaw = 0;
  static bool have = false;
  uint32_t ticks;
  ticks = ::ffxii::guest::kTicks.Get();
  float x = 0, z = 0;
  // The follow camera's position (row 3 of its world transform 0x20975A0):
  // the player's position is known only near talk targets.
  const ::ffxii::guest::Mat4 cam = ::ffxii::guest::kCameraWorld.Get();
  x = cam.m[3][0];
  z = cam.m[3][2];
  const bool pos = true;
  const float yaw = std::atan2(cam.m[2][0], cam.m[2][2]) * 57.29578f;
  if (!have || ticks - prev_ticks > 8 || !pos) {  // first frame, or a load reset the counter
    have = pos;
    start = ticks;
    frames = 0;
    path = turn = 0;
  } else {
    path += std::hypot(x - px, z - pz);
    float dy = yaw - pyaw;
    while (dy > 180) dy -= 360;
    while (dy < -180) dy += 360;
    turn += std::fabs(dy);
    ++frames;
    if (ticks - start >= 60) {
      std::fprintf(stderr, "[speed] ticks %u-%u frames %llu path %.3f turn %.2f\n", start, ticks,
                   (unsigned long long)frames, path, turn);
      start = ticks;
      frames = 0;
      path = turn = 0;
    }
  }
  prev_ticks = ticks;
  px = x;
  pz = z;
  pyaw = yaw;
}
}  // namespace recomp::ffxii

namespace {
bool ParseXZR(const char* arg, float* x, float* z, float* r) {
  *r = 1.0f;
  return std::sscanf(arg, "%f,%f,%f", x, z, r) >= 2;
}

bool CondTalk(const char*, double, float*, float*, uint16_t*) {
  return g_talk_target.load(std::memory_order_relaxed) != 0;
}

bool CondNear(const char* arg, double, float*, float*, uint16_t*) {
  float tx, tz, r, x, z;
  if (!ParseXZR(arg, &tx, &tz, &r) || !PlayerXZ(&x, &z)) return false;
  return std::hypot(tx - x, tz - z) <= r;
}

// Steering: world angle = theta + sigma * stick angle. Learned from the
// player's own motion: a calibration push up then right fixes sigma (the
// camera may mirror) and theta; every later straight stretch refines theta,
// as the follow camera turns.
struct Steer {
  int phase = 0;  // 0 idle, 1 pushing up, 2 pushing right, 3 steering
  double t_seg = 0;
  float x0 = 0, z0 = 0, d1 = 0;  // segment start, angle of the up push
  float theta = 0, sigma = 1, cmd = 0;
  double stuck_since = -1;
  float best = 1e9f;
};
Steer g_steer;

float Wrap(float a) {
  while (a > 3.14159265f) a -= 6.2831853f;
  while (a < -3.14159265f) a += 6.2831853f;
  return a;
}

bool CondWalkTo(const char* arg, double waited, float* lx, float* ly, uint16_t*) {
  float tx, tz, r, x, z;
  if (!ParseXZR(arg, &tx, &tz, &r)) return true;
  Steer& s = g_steer;
  if (waited == 0 || !PlayerXZ(&x, &z)) {
    if (waited == 0) s = {};
    *lx = *ly = 0;
    return false;
  }
  const float dist = std::hypot(tx - x, tz - z);
  if (dist <= r) {
    *lx = *ly = 0;
    return true;
  }
  auto push = [&](float ang, float mag) {
    *lx = mag * std::cos(ang);
    *ly = mag * std::sin(ang);
  };
  const float moved = std::hypot(x - s.x0, z - s.z0);
  const float heading = std::atan2(z - s.z0, x - s.x0);
  switch (s.phase) {
    case 0:
      s = {};
      s.phase = 1, s.t_seg = waited, s.x0 = x, s.z0 = z;
      push(1.5707963f, 0.7f);
      return false;
    case 1:  // up
      if (waited - s.t_seg < 0.8 || moved < 0.4f) {
        push(1.5707963f, 0.7f);
        return false;
      }
      s.d1 = heading;
      s.phase = 2, s.t_seg = waited, s.x0 = x, s.z0 = z;
      push(0, 0.7f);
      return false;
    case 2: {  // right: stick angle -90 deg from up
      if (waited - s.t_seg < 0.8 || moved < 0.4f) {
        push(0, 0.7f);
        return false;
      }
      s.sigma = Wrap(heading - s.d1) < 0 ? 1.0f : -1.0f;
      s.theta = Wrap(heading - s.sigma * 0.0f);
      s.phase = 3, s.t_seg = waited, s.x0 = x, s.z0 = z;
      std::fprintf(stderr, "[walkto] calibrated: sigma %+g theta %.0f deg\n", s.sigma, s.theta * 57.29578f);
      break;
    }
    default:
      // Refine theta from the stretch just driven, then re-aim.
      if (moved >= 0.5f) {
        const float th = Wrap(heading - s.sigma * s.cmd);
        s.theta = Wrap(s.theta + 0.5f * Wrap(th - s.theta));
        s.t_seg = waited, s.x0 = x, s.z0 = z;
      }
      break;
  }
  // Stuck (wall, NPC): sidestep for a moment.
  if (dist < s.best - 0.2f) {
    s.best = dist;
    s.stuck_since = waited;
  } else if (s.stuck_since >= 0 && waited - s.stuck_since > 2.5) {
    s.stuck_since = waited + 1.0;  // one second of sidestep, then retry
    s.cmd = Wrap(s.cmd + 1.5707963f);
    s.x0 = x, s.z0 = z, s.t_seg = waited;
  }
  if (waited < s.stuck_since) {
    push(s.cmd, 0.8f);
    return false;
  }
  const float want = std::atan2(tz - z, tx - x);
  const float cmd = Wrap(s.sigma * (want - s.theta));
  if (std::fabs(Wrap(cmd - s.cmd)) > 0.35f) s.x0 = x, s.z0 = z, s.t_seg = waited;  // new stretch
  s.cmd = cmd;
  push(cmd, dist > 3 ? 1.0f : 0.5f);
  return false;
}

bool Free() {
  const uint32_t f = g_flags.load(std::memory_order_relaxed);
  return f != ~0u && f != 0 && !(f & 0x10000);
}

bool CondField(const char*, double, float*, float*, uint16_t*) {
  return NowNs() - g_search_ns.load(std::memory_order_relaxed) < 500'000'000;
}

bool CondFree(const char*, double, float*, float*, uint16_t*) { return Free(); }
bool CondBusy(const char*, double, float*, float*, uint16_t*) { return !Free(); }

bool CondAdvance(const char*, double waited, float*, float*, uint16_t* buttons) {
  static double free_since = -1;
  if (waited == 0) free_since = -1;
  if (Free()) {
    if (free_since < 0) free_since = waited;
    return waited - free_since >= 1.0;
  }
  free_since = -1;
  if (std::fmod(waited, 1.5) < 0.35) *buttons = 0x2000;  // B: next page / close
  return false;
}

// FFXII_TEST_CLOCK=game: test scripts run on game time, the 1/60 s tick
// counter 0x208F570 in seconds, instead of wall time. Only forward steps of
// at most 8 ticks count, because the counter is reset when a game loads.
// The clock is read on the event-pump thread.
double GameClock() {
  static uint32_t last = 0;
  static uint64_t total = 0;
  if (!recomp::g_guest_base) return 0;
  uint32_t ticks;
  ticks = ::ffxii::guest::kTicks.Get();
  const uint32_t d = ticks - last;
  last = ticks;
  if (d <= 8) total += d;
  return total / 60.0;
}

struct RegisterConditions {
  RegisterConditions() {
    recomp::RegisterTestCondition("talk", CondTalk);
    recomp::RegisterTestCondition("near", CondNear);
    recomp::RegisterTestCondition("walkto", CondWalkTo);
    recomp::RegisterTestCondition("free", CondFree);
    recomp::RegisterTestCondition("field", CondField);
    recomp::RegisterTestCondition("busy", CondBusy);
    recomp::RegisterTestCondition("advance", CondAdvance);
    if (const char* e = recomp::Env("TEST_CLOCK"); e && std::strcmp(e, "game") == 0) recomp::SetTestClock(GameClock);
  }
} g_register_conditions;

}  // namespace

namespace recomp::gen {

void f_002E0FA0(Ctx& c) {
  if (::ffxii::direct::Enter<0x002E0FA0>(c)) return;
  f_002E0FA0_lifted(c);
  g_flags.store(uint32_t(c.rax), std::memory_order_relaxed);
  if (!On()) return;
  static uint32_t prev = ~0u;
  const uint32_t v = uint32_t(c.rax);
  if (v != prev) {
    // With the 1/60 s tick counter 0x208F570 and the limited-frame count, so
    // event lengths can be compared across frame rates.
    uint32_t ticks;
    ticks = ::ffxii::guest::kTicks.Get();
    std::fprintf(stderr, "[probe] 2E0FA0 flags 0x%08x -> 0x%08x (ticks %u, frame %llu)\n", prev, v, ticks,
                 (unsigned long long)recomp::ffxii::LimitedFrames());
    prev = v;
  }
}

void f_002647D0(Ctx& c) {
  if (::ffxii::direct::Enter<0x002647D0>(c)) return;
  f_002647D0_lifted(c);
  g_talk_target.store(c.rax, std::memory_order_relaxed);
  if (!On()) return;
  static uint64_t prev = ~0ull;
  if (c.rax != prev) {
    std::fprintf(stderr, "[probe] talk target 0x%llx -> 0x%llx\n", (unsigned long long)prev,
                 (unsigned long long)c.rax);
    prev = c.rax;
  }
  MaybeFlush();
}

// 3DA5A0(out, a, pa, b, pb): sqrt(dx^2 + dz^2) - (ra + rb + a[3] + b[3]),
// ra/rb from 3DA730 -> 3A1D30. Logs the closest call's terms each second.
void f_003DA5A0(Ctx& c) {
  uint64_t ret;
  std::memcpy(&ret, recomp::g_guest_base + c.rsp, 8);
  if (ret == 0x25BC76) {  // talk search: a = player
    g_player_pos.store(c.r8, std::memory_order_relaxed);
    g_search_ns.store(NowNs(), std::memory_order_relaxed);
  }
  if (!On()) return f_003DA5A0_lifted(c);
  const uint64_t a = c.rdx, pa = c.r8, b = c.r9;
  ++g_callers[ret];
  uint64_t pb;
  std::memcpy(&pb, recomp::g_guest_base + c.rsp + 0x28, 8);
  auto F = [](uint64_t p) {
    float v;
    std::memcpy(&v, recomp::g_guest_base + p, 4);
    return v;
  };
  const float dx = F(pb) - F(pa), dz = F(pb + 8) - F(pa + 8);
  if (ret == 0x25BC76) {  // talk-target search: log each distinct pair once a second
    static std::map<std::pair<uint64_t, uint64_t>, std::chrono::steady_clock::time_point> seen;
    const auto now = std::chrono::steady_clock::now();
    auto& t = seen[{a, b}];
    if (now - t > std::chrono::seconds(1)) {
      t = now;
      std::fprintf(stderr, "[probe] search a=%llx pa=%llx (%.2f %.2f %.2f) b=%llx pb=%llx (%.2f %.2f %.2f)\n",
                   (unsigned long long)a, (unsigned long long)pa, F(pa), F(pa + 4), F(pa + 8),
                   (unsigned long long)b, (unsigned long long)pb, F(pb), F(pb + 4), F(pb + 8));
    }
  }
  const float a3 = F(a + 12), b3 = F(b + 12);
  f_003DA5A0_lifted(c);
  float d;
  std::memcpy(&d, c.xmm[0].b, 4);
  if (std::isnan(d)) ++g_n.nan;
  else if (d < 0) ++g_n.neg;
  else ++g_n.pos;
  if (d < g_n.min) {
    g_n.min = d;
    const float center = std::sqrt(dx * dx + dz * dz);
    std::snprintf(g_n.why, sizeof g_n.why, "center %g radii(ra+rb) %g a3 %g b3 %g a=%llx b=%llx", center,
                  center - d - a3 - b3, a3, b3, (unsigned long long)a, (unsigned long long)b);
  }
}

void f_003A1BB0(Ctx& c) {
  f_003A1BB0_lifted(c);
  if (!On()) return;
  (uint8_t(c.rax) ? g_n.facing_yes : g_n.facing_no)++;
}

}  // namespace recomp::gen
