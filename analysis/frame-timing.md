# Frame timing (static trace 2026-11-22, retail 1.0.4.0)

Goal: frame rates above 60 fps without changing game speed. This records what drives the simulation step today.

## Setting

- `GameSetting.ini` key names are a pointer table at 0x1DFD050 (31 names). `FrameLimit` is index 18.
- The settings object is `[0x1F82D20]`, with one 4-byte field per name, so FrameLimit is at `+0x48`. The ini writer is 0x1DEE90; the options menu reads the field at 0x1CD812.
- `0x17D8F0` returns the field. Two callers test it for 1 (60 fps): 0x1DD581, and 0x22BDF0. 0x22BDF0 also scans argv for `-nologo`.

## Timing globals (set by 0x1EDDC0, called with fps60 = FrameLimit == 1)

| address | 30 fps | 60 fps | meaning |
|---|---|---|---|
| 0x1F82E38 | 0 | 1 | 60 fps flag (getter 0x1EDE70; 22 callers) |
| 0x1DFE0B4 | 1 | 2 | frames per 30 Hz tick, int (219 refs) |
| 0x1DFE0B8 | 1.0 | 2.0 | the same as a float (71 refs) |
| 0x1DFE0BC | 1.0 | 0.5 | per-frame time scale (260 refs) |

- 0x1EDDC0 also calls the import pair at 0x8C2A90/0x8C2A98 (8-byte caps query, then begin with the result; likely the timeGetDevCaps/timeBeginPeriod pair), saves the period in 0x1F82E40, and sets 0x1F82E3C.
- The 30 fps path ends at 0x8C2AA0 (end).

## Per-frame driver

- 0x250540 runs once per presented frame.
  - In 60 fps mode it passes `n = 1` to the game update 0x184960.
  - In 30 fps mode it passes `n = 1 + (0x181DD0() != 0)`, which is 2 when a frame was late.
  - 0x208F570 accumulates n, so the game counts time in 1/60 s ticks.
- 0x184960(n) stores n in 0x1DFE0C0 (setter 0x1EDEF0; it is stored again at 0x1849A2) before the update.
- The 60 fps helpers 0x1EDDA0, 0x1EDE80 and 0x1EDEC0 combine the flag with 0x1DFE0C0. Together they have 136 call sites; these are where systems choose to run per 30 Hz tick or per frame.
- 0x1EC560 checks a per-mode table (0x1F832B0, index `[0x1DFE40C]`, at most 4). Eight functions in the module (0x1EC6B0 to 0x1EDCD0) gate on the flag.

## Experiment: FFXII_SIM_MULT=k (src/frame_rate.cpp)

- The wrapper on 0x1EDDC0 rescales the three numeric globals to 2k, 2.0k and 0.5/k.
- With k=2 (`win-sim2.log`, 45-100% machine load) the frame rate stayed at 43-59 fps.
- The game ran in slow motion: at 230 s it was still in a courtyard cutscene that the 60 fps baseline passes well before then.
- So these globals set the simulation step but not the frame cap, and 120 fps needs more than new values.

## Frame limiter (0x6234A0)

- The limiter sleeps until `r14 * 1/60 s` have passed since the last frame, then spins on Sleep(0).
  - r14 is 1 or 2.
  - The interval constant is 0x9010A0 = 1/60, or 0x900378 = 1.001/60 when the refresh rate is not a multiple of 10.
- 0x1E03F0 and 0x1D8240 use the same constants to turn the 60 Hz tick count into seconds, so they must not be changed globally.
- VSync off alone still caps at 60 (`win-nv1.log`), so this limiter is the cap.

## Experiment 2: SIM_MULT=2, limiter interval halved (`win-sim2b.log`)

- The wrapper on 0x6234A0 divides both constants by k only during the call. Run data `data-iw-nv` has VSync=Off.
- Result: 119.9 fps in calm stretches and 85-117 in combat, at 29% machine load. 120 fps rendering is possible.
- The 1/60 s tick counter 0x208F570 then advances once per frame (about 120 a second), so tick-driven logic runs twice as fast.
- The game measures time in whole 1/60 s ticks: update() takes an integer n, which is 1 at 60 fps and 1 or 2 at 30 fps. A 120 fps frame is half a tick, which these integer counters cannot represent.

Conclusion: higher frame rates need the simulation to stay at 60 Hz with rendered frames interpolated between ticks, not a smaller step. `FFXII_SIM_MULT` stays as a measurement switch only; its game speed is wrong.

Open questions:
1. Answered above: the cap is the limiter at 0x6234A0.
2. Whether code outside these globals assumes 60 Hz ticks: the 0x208F570 counter, the n=1/2 update argument, and hard-coded 2:1 parity at the 136 helper call sites.

The likely route past 60 fps is to keep the simulation at 60 Hz and render extra interpolated frames, rather than to shrink the step.

## Render side (first look, 2026-11-22)

- After the update, 0x250540 builds the frame's display lists into double buffers:
  - 0x2481A0 flips the buffer index 0x208ED5C (xor 1) and sets 0x208F560.
  - 0x248030 builds one list into that buffer. It is called for the three lists at 0x208EA20, then 0x208EA40 and 0x208EA48; it also checks 0x1EDDA0.
  - 0x248190 clears 0x208F560.
- 0x250CA0 reads the tick counter 0x208F570, so some list content is tick-timed.
- The render thread consumes the other buffer. Its busiest lifted function, f_00628B40 (called only from 0x629970), copies packet floats into GPU buffers.
- Shader reflection does not name the world matrices: only the 14 post-effect shaders call D3DReflect (`m44_ModelViewProject`, DOF, tonemap). World transforms arrive already baked into the packets.

Plan for interpolation:
1. Keep update() at 60 Hz and present twice per tick: draw the last packet pair again, with the camera blended between the two most recent ticks.
2. To blend the camera, find the view matrix in the packet stream. Approach: dump 0x208EA20-list packets for two consecutive ticks while only the camera moves (scripted right-stick input), and diff them.
3. Character transforms come after the camera and need the same diff method per object.

## Camera (found 2026-11-22 by snapshot diffing)

Method: `FFXII_SNAP=start,count,interval` (src/frame_rate.cpp) writes `.data` snapshots, and `build/run/input/camturn.txt` turns the camera with the right stick for 10 s at a time. Snapshot pairs are labelled idle or turning from the frame dumps, and `tools/snapdiff.py` keeps the floats that change in every turning pair and in no idle pair, then finds orthonormal 3x3 blocks among them (`win-camturn3.log`).

| address | content |
|---|---|
| 0x2097560 | view matrix (world to camera), 4x4 row-major; row 3 is the translation |
| 0x20975A0 | its inverse, the camera's world transform (position in row 3, for example 100.4, 27.7, 128.4) |
| 0x2AED580-0x2AEEC70 | several copies of both (render-side; 0x382660 builds one with `lea rdx, [0x2AED580]`) |
| 0x2CA7F80 | a second, slightly different view (sub-camera; written at 0x4FDDD7, used at 0x382C70, 0x53C862) |
| 0x1EED440 | other words that change only while turning |

- The camera objects are an array at 0x2097300, and 0x20972F0 points at the active one's data (slot + 0x20; set at 0x259893).
- Per-slot state uses stride 0x860 (0x25A2A7: `imul rcx, rax, 0x860`, table 0x2098574).
- So the view matrix is at camera data + 0x240.
- Writers, found with the write watch (`FFXII_WATCH=2097560,80`, src/watch.cpp, `win-watch2.log`):
  - f_00259E90 is camera-system init (one caller). It resets 8 camera objects of 0x110 bytes from 0x2097300, plus one at 0x2097B80, and constructs each with 0x3CBFF0. The 0x860 stride above is a different per-slot table.
  - f_003CBD60, f_003CC080, f_003CBCA0 and f_00202E10 write the camera transform (+0x40..+0x7C). These are camera-object methods; 0x3CBxxx is the camera class.
  - f_00217610 writes the whole view matrix (+0x00..+0x3C); it is likely the generic affine inverse.
- Plan for interpolation:
  1. Hook the render-side copy that puts the view into the packets (0x382660 builds the copy at 0x2AED580).
  2. Keep the view of the previous tick, and for an in-between frame write a blended view: lerp the camera position and slerp the rotation.
  3. Characters need the same treatment later.
