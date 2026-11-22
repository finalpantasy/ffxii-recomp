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
- The wait (0x6236D9-0x6237AB): 0x5CC930 returns the time in seconds (double).
  The remaining time is now - 0x2CB60E8 (start) - 0x1F2DF10 (last frame) - interval.
  If that is negative, the limiter sleeps once for the remaining milliseconds,
  rounded down to a multiple of 0x2CF2540 (the timer period) minus 1, then spins
  on Sleep(0) and 0x5CC930 until the time is reached. 0x1F2DF10 then advances by
  min(interval, overshoot), so late frames catch up.
- In combat that spin is about 300,000 Sleep(0) and QueryPerformanceCounter calls a
  second (`FFXII_IMPORT_STATS=1`, `win-imp2.log`), roughly 1-2 ms of one core per frame.
  It costs CPU, not frame rate. A host-side precise wait would need the deadline,
  which depends on r14 (frames per interval), held in a register inside the function.

## Experiment 2: SIM_MULT=2, limiter interval halved (`win-sim2b.log`)

- The wrapper on 0x6234A0 divides both constants by k only during the call. Run data `data-iw-nv` has VSync=Off.
- Result: 119.9 fps in calm stretches and 85-117 in combat, at 29% machine load. 120 fps rendering is possible.
- The 1/60 s tick counter 0x208F570 then advances once per frame (about 120 a second), so tick-driven logic runs twice as fast.
- The game measures time in whole 1/60 s ticks: update() takes an integer n, which is 1 at 60 fps and 1 or 2 at 30 fps. A 120 fps frame is half a tick, which these integer counters cannot represent.

Conclusion at the time: higher frame rates would need a 60 Hz simulation with interpolated frames. Superseded: the section "Native 120 fps" below keeps the game speed with whole ticks on every k-th frame.

Open questions:
1. Answered above: the cap is the limiter at 0x6234A0.
2. Whether code outside these globals assumes 60 Hz ticks: the 0x208F570 counter, the n=1/2 update argument, and hard-coded 2:1 parity at the 136 helper call sites.

The likely route past 60 fps is to keep the simulation at 60 Hz and render extra interpolated frames, rather than to shrink the step.

## Render side (first look, 2026-11-22)

- After the update, 0x250540 runs the game's task lists. These are task lists, not display lists; an earlier version of this note called them display lists, which was wrong.
  - 0x2481A0 flips the frame parity 0x208ED5C (xor 1) and sets 0x208F560.
  - 0x248030 walks one linked list of task objects. For each task whose parity bit (bit 17 of +0x40) differs from 0x208ED5C, it calls the task's callback (`call [rbx]`) and then marks the bit, so each task runs once per frame.
  - 0x248030 is called for the three lists at 0x208EA20, then 0x208EA40 and 0x208EA48. With 0x1EDDA0 it also counts frames in +0xB4.
  - 0x2465F0 creates a task: callback at +0, flags at +0x40/+0x44, and a default callback of 0x5C5230.
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

### Scene camera function (0x3820C0)

- f_003820C0, called only through the thin wrapper f_0036F510, runs the per-frame camera step end to end:
  - It calls the camera method 0x3CBD60, which writes the camera transform at 0x20975A0.
  - It sets up the render camera object at 0x2AED560: 0x3CBB30, with the aspect ratio stored at 0x2AED570.
  - It multiplies the source matrix into 0x2AEDD30 (0x202D70) and copies the view to 0x2AED580 (0x202C70).
  - It uses the object at 0x2AED4A0 (+0xC0) as scratch.
- One `replace` on 0x3820C0 can therefore record each tick's final camera (view at 0x2097560 and 0x2AED580) and, for an in-between frame, overwrite both with a blend.
- Still open: how to present an extra frame between ticks. That needs either re-running the render submission for the same display lists, or splitting 0x250540's update (0x184960) from its list build and present. 0x208ED5C is task bookkeeping, not a render buffer index, so drawing is done by some of these tasks. Next, sort the tasks in the 0x208EA20-0x208EA48 lists by callback into simulation tasks and draw tasks. An in-between frame then runs only the draw tasks with the blended camera.

## Frame structure from the top (2026-11-22)

- The app object is `GameApplication`, derived from PhyreEngine `Phyre::PFramework::PApplication` (RTTI at 0x1CEBFF8).
  - Vtable at 0x8C5760: 0x17CC70, 0x79F9E0, 0x17CEB0, 0x17CF30, **0x17CCD0**, 0x17CDE0, 0x17CDC0, 0x17CE50, 0x17D090, 0x17D0C0, 0x17D120.
- Slot 4, 0x17CCD0, is the per-frame method. Chain: 0x17CCD0 -> 0x17D6E0 -> 0x22A490 -> 0x22A580, the frame body.
  - The frame limiter 0x6234A0 is called from the framework loop (0x607588), not from this method.
- 0x22A580 switches on the game mode from 0x259240 and calls one handler. Each handler runs update and draw preparation together:

| handler | reaches |
|---|---|
| 0x22BC20 | the per-frame driver 0x250540, then the update 0x184960 |
| 0x22B7E0 | the update 0x184960 directly |
| 0x22B8C0 | the scene camera 0x36F510 -> 0x3820C0 |
| 0x22A770 | the scene camera through 0x36F510 (3 call sites) |
| 0x22B6E0, 0x22AF50, 0x22B000 | other modes |

- Task census (`FFXII_TASKS=<call>[,<call>...]` wraps 0x248030; tree links: parent +0x10, first child +0x18, next sibling +0x20, previous +0x28, child count +0x30):
  - The main list had 42 tasks in `win-tasks3.log` and 19 in `win-tasks4.log`; the other three lists had 1 each.
  - No task callback reaches 0x3820C0 through direct calls, so the scene camera runs from the mode handlers, not from tasks.
- `tools/callgraph.py` answers direct-call reachability questions over the lifted code.
- Correction (2026-11-22): 0x259240 has callers besides the frame body, so `FFXII_MODE_LOG` (which logs the byte at 0x2064AD3 after every 0x259240 call) does not show the handler in use. Call counters (`FFXII_CAMLOG=1` prints them) show the real split: 0x22B6E0 runs for the first 87-138 frames, 0x22B8C0 for a few hundred more during loading, and neither runs in the field.

## Field frame (2026-11-22)

- The field handler is 0x22A770 (the default case of the frame body switch).
- Its driver is **0x2507C0**, not 0x250540. Callers: 0x22AB83 and 0x22AC17 in 0x22A770, and 0x22B832 in 0x22B7E0. Found with the write watch on the tick counter, armed after the field loads (`FFXII_WATCH=208F570,4,4500`; the third field is the start frame).
  - It sets 0x2092748, calls the update 0x184960 with n = 1 or 2 (from 0x17D930), adds 0x184950() to the tick counter 0x208F570, runs the four task lists 0x208EA28, 0x208EA40, 0x208EA48 and 0x208EA50 (0x248030, then 0x244970 on each), and tail-calls 0x228930. With n = 1 it also calls 0x254CC0 and 0x367200.
  - 0x250730 is the same kind of driver for 0x22B6E0 (two task lists, ticks += 2 / [0x1DFE0B4]).
- The camera moves outside the driver. The camera transform at 0x20975A0 is written by 0x3CBD60 (through 0x3CC000 from 0x3829D0 and 0x259820), by 0x3CC080 (from 0x3D9150, 0x258350 and others) and by 0x3CBCA0 and 0x202E10, along several paths from 0x22A770 each frame. 0x3820C0 is only one of them.

### Experiment 3: 120 fps with the driver on tick frames only (`FFXII_SKIP_DRIVER=2`)

- The limiter runs at 1/120 s. A wall-clock accumulator decides once per frame whether a 1/60 s tick is due; on other frames the wrappers on 0x250540 and 0x2507C0 return at once (popping the return address: a `replace` wrapper that skips the lifted body must do `c.rsp += 8`).
- Ticks then follow wall time (about 60 a second while frames run faster). The first version skipped every other frame instead and ran the game at 75% speed when frames fell to 89 fps (`win-spd5.log`).
- The camera does not follow: `FFXII_CAMLOG` sums the yaw change separately for frames that ran or skipped the driver, and the right-stick turn was as large on skipped frames as on driver frames (`win-spd7.log`: 2.3 against 2.6 degrees per frame). At about 118 fps it turned at about 290 degrees a second against 150 at 60 fps. Holding 0x3820C0 to tick frames too did not stop it (`win-spd8.log`), because of the other camera paths above.
- `FFXII_SKIP_HANDLER=1` skips the whole of 0x22A770 on in-between frames. Those frames are then exact repeats with no black or broken images (`win-spd9.log`; consecutive dumps differ by 0.0).
- The render thread draws once per game frame, not per present: the render packet consumer 0x60EC80 (called from 0x608DD0 and 0x608F20, it runs 0x629970) ran about 394 times per tick and 239 per presented frame with the handler skipped, against 330-600 per frame at 60 fps (`win-spd10.log`, `win-spd11.log`). So an in-between frame is presented again, not drawn again.

Conclusion: 120 fps output needs the render thread to draw the last frame's packets a second time with a blended camera. Holding the game thread to 60 Hz is easy (skip 0x22A770), and the images stay clean. The open work is on the render side:
1. Find where the view and projection enter the packets: a per-view constant buffer (then a blended camera can be written there) or per-object matrices built on the game thread (then the blend must go through the packet build).
2. Make the render thread re-run its submission (0x608DD0/0x608F20) for the last packet buffer on an in-between frame.

### Where the view reaches the GPU (`FFXII_CBSCAN=1`, `win-cbs1.log`)

- SwARc's D3D11 forwarder has a debug hook on buffer uploads (`recomp::hle::g_buffer_upload_hook`, called from Unmap of a written buffer and from UpdateSubresource). `src/cbscan.cpp` uses it to search each upload for the camera of the last four frames: its world position, and the first row and first column of the view matrix at 0x2097560.
- Constant buffers (bind flag 0x4) carry the view, uploaded per draw:
  - 928 bytes, about 160 uploads a frame: view column-major at 0x50 and row-major at 0x110 in every upload.
  - 8864 bytes, about 550 uploads a frame: view column-major at 0x1F80 in about 72% of them.
  - A few 4096-byte uploads hold the view row-major at 0xE8.
- The camera position never appears as three plain floats, and vertex buffers (bind 0x1, 32736 bytes) match only by chance.
- So the view is not baked into vertices. An in-between frame can get a blended camera by patching these constant-buffer uploads. The view-projection and model-view-projection products in the same buffers still need to be found, and a replayed frame has to issue the uploads again.

### Render thread (2026-11-22)

- The thread starts at 0x5FE1F0 (a jump to 0x6106A0; its address is passed at 0x607DA8). 0x6106A0 loops over a 256-slot command ring at renderer + 0x40, 40 bytes a slot, with an index at +0x2830. It waits on each slot's event (0x5CD840). Command 1 calls 0x60A410(renderer, [slot+0x30], [slot+0x38]), clears the slot and signals [slot+0x50] (0x5CD650).
- 0x60A410 runs one ring command, not one frame: about 5.4 calls per presented frame in the field (`win-rt1.log`, `FFXII_CAMLOG` prints the count as `fr`). Calling it twice per command with the same arguments froze the game at boot (`win-rt2.log`), so a frame cannot be redrawn by repeating commands.
- Paths for an in-between frame:
  1. Record a frame's D3D11 immediate-context calls in the SwARc forwarder, then replay them with the view patched in the known constant-buffer offsets. This needs the view-projection products found as well.
  2. Have the game thread post a second set of ring commands for the same packets. This needs the packet lifetime and the command producer to be traced.

### Constant-buffer layout (`FFXII_CBSCAN_DUMP=<frame>`, `cbdump.bin` from `win-cbd1.log`)

The dump holds the first 64 uploads each of 928 and 8864 bytes after the given frame, each one with the view and camera matrices of that moment.
- **8864 bytes:** the view is at 0x1F80-0x1FBF, transposed, with the translation in the fourth column (rows are the view's columns: 1.000, -0.000, 0.0085, -97.917 ...). A separate screen-space projection follows at 0x1FC0-0x1FFF: 401.4 and -624.4 on the diagonal, -2048 offsets, and -1 in the w row.
- **928 bytes:** the combined projection times view (screen space, transposed) is at 0x2D0-0x30F. Its last row, at 0x300, is minus the view's third column (0.008, -0.3275, -0.9448, 137.076). The 3-float view matches at 0x50 and 0x110 from the first scan are rotation-only blocks. In these samples 0x50-0x7F holds NaN, so that block is unused for these draws.
- No buffer holds the plain camera position.
- To draw with a blended view V' instead of V:
  - write V' (transposed) at 8864:0x1F80;
  - replace the 928:0x2D0 block M = P·V with M·V⁻¹·V'. V⁻¹ is the camera transform at 0x20975A0, so the projection is not needed.
- How to issue a second frame is still open. A route to evaluate is to record each frame on two D3D11 deferred contexts, with the in-between one receiving the patched buffer data at Unmap, and then run both command lists with a present between them. Deferred contexts accept only WRITE_DISCARD (and NO_OVERWRITE for some buffers) and cannot do queries or readback, so the game's map types decide whether this works.

## Native 120 fps (2026-11-22): `--fps N`, `FFXII_SIM_MULT=k`

The game updates k*60 times a second at unchanged speed: 1/60 s of
tick-counted time passes once every k frames. Verified at k=2
(`win-sp120h.log`, `fight7.txt`, 300 s, 81% machine load from other work):
calm field stretches present 115-120 fps with 59.9-60.2 ticks a second, and
the tick rate never goes above 60. When the machine falls behind, the frame
rate drops toward 60 (two frame bodies per present) and, below that, the game
slows down as it does at 60 fps. The run logged no fatal errors.

Pieces (src/frame_rate.cpp):
1. **Timing globals:** the 0x1EDDC0 wrapper sets 0x1DFE0B4/0x1DFE0B8 to 2k
   and 0x1DFE0BC to 1/(2k).
2. **Tick frames:** each driver (0x250540, 0x2507C0, 0x250730) counts its own
   calls. Only every k-th call advances the tick counter 0x208F570, and on
   the other calls 0x184950 returns 0 to the callers that add the result to
   tick time (TickGatedCaller). 0x250730 adds the tick itself, because its
   own `2 / [0x1DFE0B4]` add is 0 at k > 1.
3. **Frame time:** 0x1E03F0 keeps a time debt in 0x1F82D28: wall time minus
   ticks times 1/60 s. 0x1E04F0 gives one frame's time. When the debt
   passes one frame, 0x1E05F0 sets 0x1F82D30, and the frame function
   0x17D6E0 runs the frame body 0x22A490 again (its loop at 0x17D836). Both
   callers keep 0x184950's tick count of 1, and a hook divides their
   interval by k (0x1E043A, 0x1E0528). An earlier build gated these to 0;
   then every frame looked late and each present ran two frame bodies
   (`win-sp120b.log`: 115 presents, 230 updates and 115 ticks a second,
   which is double speed).
4. **Limiter:** the render-thread limiter 0x6234A0 waits r14 * interval, with
   r14 = [0x1DF08F8] (frames per present, sent by the update 0x184960
   through render command 0x17ECB0). A hook at 0x62369C divides the
   interval by k in xmm0. The constants 0x9010A0/0x900378 stay unchanged
   in memory, because game-thread code (0x1E03F0, 0x1E04F0) reads them
   while the limiter sleeps.
5. **Game-thread pacing:** the 0x22A580 wrapper holds the game thread to a
   fixed 1/(60k) s step, up to 4 steps behind. The limiter alone does not
   hold the game thread to one frame per present above 60.
6. **Hard-coded 60 fps sites:** these are instruction hooks (manifest `hook`).
   - 0x518F50 adds an acceleration on odd countdown values. At k*60 it adds
     on every 2k-th value (hooks 0x519001 and 0x519015).
   - 0x51B150 samples a 30-entry table at counter/2 with a 1/2 blend. At
     k*60 it samples at counter/2k with blend (counter mod 2k)/2k (hooks
     0x51B1A5 and 0x51B1CF).
   - 0x32AE70's decay factor is 0.5^(1/(2k)).
   - 0x1EDEC0's every-second-count test becomes every 2k-th.
   - 0x32D4AA eases the float at [rdi+0xF8] toward a target with a fixed
     per-frame blend (keep 5/6 of the old value on the flagged path, 2/3 on
     the plain one). At k*60 the hooks at its stores (0x32D4E3, 0x32D505)
     recover the target from the 60 fps result and blend with
     1 - keep^(1/k), so the value covers the same ground per 1/60 s.
     0x32D3D0 is the horizontal analog-axis filter (input: byte axis 0 from
     0x46DB10 minus 0x80, dead zone from 0x46DB20; 0x32D620 takes axis 1),
     called from 0x32C910 about 100 times a second in battle. It reaches the
     stores only while the stick is outside the dead zone, so `fight7.txt`
     (stick straight north) never touches them. `FFXII_FILTER_LOG=1` logs
     each store. With right-stick X pushes in battle (`input/fight7-x.txt`,
     `win-fx1.log` and `win-fx2.log`) the plain path settles at -0.04189 per
     frame at 60 fps and -0.02083 at 120 fps (about 2.5 a second in both),
     with the same time constant: about 6% (60 fps) and 8% (120 fps) of the
     gap is left after 0.1 s. The flagged path (0x32D4E3) smooths the
     camera reset: pad word bit 0x0004 (R3, XInput `rs`; the word at
     0x2AD9E18 that 0x46DB40 returns uses the PS2 bit order, mapped with
     `FFXII_PADLOG=1` and `input/pad-map.txt`) makes 0x32C910 call
     0x32DC00, which sets flags 0x40300000. `input/fight7-r3.txt` presses
     R3 every 4 s in the fights. Its input shrinks as the camera turns back,
     so the curve is a closed loop rather than a plain 5/6 decay. As
     fractions of the settled value, the 60 fps run (`win-r3b.log`) reaches
     41/65/88% at 0.000/0.017/0.050 s and the 120 fps run (`win-r3a.log`) at
     0.007/0.024/0.058 s: the same curve, one 120 fps frame later.

Not adjusted:
- 0x3E5DF2, a one-step phase offset at setup (1/120 s instead of 1/60).
- 0x53B129, debug text formatting.
- The other 1EDE70/1EDDA0/1EDE80 sites. A game-clock A/B check covers them
  in aggregate: `fight7.txt` with `FFXII_TEST_CLOCK=game` (inputs land on
  the same tick at both rates) at k=1 (`win-gc1.log`, 58.6 fps mean) and
  k=2 (`win-gc2.log`, 77.9 fps mean, about 85% machine load) gives the
  same tick schedule of 2E0FA0 state changes (battle and event starts and
  ends) within about 1-2%: for example 2863/2868, 12078/12064, 14740/14679
  and 15760/15663 ticks. A per-frame system running at double speed in
  battle (ATB, movement, animation) would shorten those spans by about half.
  Battle randomness explains the small drift. Individually, they were checked by reading only,
  not one by one in game.

Lifter: the SwARc manifest directive `hook <VA> <name>` emits
`::recomp::Probe(c, VA, name)` before the instruction without making VA an
entry. The game takes the call with `recomp::SetProbeHook`
(`x86/probe_hook.h`).

Diagnostics: `FFXII_SPEEDLOG=1` logs every 5 s:
- fps and ticks a second;
- the ticks each update added;
- each driver's calls per present;
- the field handler's call sites;
- the limiter's wait and frames per present.

It works at 60 fps too, for a baseline.
