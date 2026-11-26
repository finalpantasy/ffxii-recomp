#pragma once
// Named globals of FFXII TZA 1.0.4.0, at absolute guest addresses (image base
// 0x120000). Declare a global here once, with its type, and reach it by name
// in src/ instead of by a raw offset. Sources: analysis/frame-timing.md,
// analysis/formats/vbf-runtime-read-path.md.

#include <cstdint>
#include <cstring>

#include "guest_types.h"
#include "x86/lifted.h"

namespace ffxii::guest {

// A typed global at a fixed guest address. Get and Set copy through
// memcpy, so unaligned addresses are fine.
template <class T>
struct Global {
  uint32_t va;
  uint8_t* Ptr() const { return ::recomp::g_guest_base + va; }
  T Get() const {
    T v;
    std::memcpy(&v, Ptr(), sizeof v);
    return v;
  }
  void Set(const T& v) const { std::memcpy(Ptr(), &v, sizeof v); }
};

// A 4x4 float matrix as the game stores it: row-major, row 3 the
// translation.
struct Mat4 {
  float m[4][4];
};

// GameSetting.ini settings object: one int per key of the name table at
// 0x1DFD050 (setter 0x1DF340); FrameLimit is index 18, +0x48 (0 = 30 fps).
inline constexpr Global<uint64_t> kSettings{0x1F82D20};
inline constexpr uint64_t kSettingFrameLimit = 0x48;
inline constexpr uint64_t kSettingVSync = 0x14;  // key 5 "VSync" (0x0C is "Quality"), nonzero = On

// --- Frame timing (set by 0x1EDDC0 from the FrameLimit setting) ---
// Frames per 30 Hz tick: 1 at 30 fps, 2 at 60 fps (2k at --fps 60k).
inline constexpr Global<int32_t> kFramesPerTick{0x1DFE0B4};
// The same count as a float.
inline constexpr Global<float> kFramesPerTickF{0x1DFE0B8};
// Per-frame time scale, 1 / kFramesPerTick (1.0 or 0.5).
inline constexpr Global<float> kFrameTimeScale{0x1DFE0BC};
// Nonzero on frames where a 30 Hz step happens.
inline constexpr Global<int32_t> kStep30HzFlag{0x1DFE0C0};
// Nonzero in 60 fps mode (getter 0x1EDE70).
inline constexpr Global<int32_t> kFps60Flag{0x1F82E38};
// 0x17D6E0 sets 1 while it repeats the frame body to pay back frame-time debt
// (0x17D846), 0 at the start of each frame.
inline constexpr Global<int32_t> kFrameRepeat{0x1F7F4B8};
// The 1/60 s tick counter: 60 a second at real game speed.
inline constexpr Global<uint32_t> kTicks{0x208F570};
// Ticks of the current frame, returned by 0x184950 (1 at 60 fps).
inline constexpr Global<int32_t> kTicksThisFrame{0x1F3DA18};
// Frames per present (r14 in the limiter 0x6234A0), set by render command
// 0x17ECB0.
inline constexpr Global<int32_t> kFramesPerPresent{0x1DF08F8};
// Per-frame decay factor set by 0x32AE70: 0.5 at 30 fps, sqrt(0.5) at 60.
inline constexpr Global<float> kDecayPerFrame{0x22C3180};
// Objects whose bytes +0x88 (0x17D930) and +0x118 (0x181DD0) TICKLOG prints.
inline constexpr Global<uint64_t> kTimingObjA{0x1F82CF0};
inline constexpr Global<uint64_t> kTimingObjB{0x1F82E18};
// Signed mode byte the frame body 0x22A580 switches on (cases 0-6).
inline constexpr Global<int8_t> kFrameMode{0x2064AD3};

// --- Camera ---
// View matrix, then the follow camera's world transform right after it.
inline constexpr Global<Mat4> kView{0x2097560};
inline constexpr Global<Mat4> kCameraWorld{0x20975A0};
// Render-side copies built by 0x382660 (view, then a second matrix).
inline constexpr Global<Mat4> kRenderView{0x2AED580};
inline constexpr Global<Mat4> kRenderView2{0x2AED5C0};

// --- Input ---
// Pad buttons as 0x46DB40 returns them, in PS2 bit order (R3 = 0x0004
// makes 0x32C910 run the camera reset 0x32DC00).
inline constexpr Global<uint16_t> kPadButtons{0x2AD9E18};

// --- Archive ---
// The VBF archive manager singleton (created by 0x19B190).
inline constexpr Global<uint64_t> kVbfManager{0x1F81110};

// --- Image ---
// The .data section, for whole-state dumps.
inline constexpr uint32_t kDataSectionVa = 0x1DF0000;
inline constexpr uint32_t kDataSectionSize = 0x11A7530;

}  // namespace ffxii::guest
