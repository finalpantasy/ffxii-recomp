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
