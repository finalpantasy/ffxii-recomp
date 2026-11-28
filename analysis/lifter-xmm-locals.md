# XMM registers in locals (2026-11-26)

## What changed

The lifter kept general registers in locals already (`LIFT_ENTER`, `LIFT_SPILL`, `LIFT_RELOAD` in SwARc `x86/lifted.h`), but every XMM access went through `c.xmm[N]`. Guest memory stores go through a `uint8_t*`, which may alias `c`, so clang reloaded XMM values from `Ctx` after each store.

SwARc `tools/lifter_x86/emit.cpp`, `Emitter::CacheXmm`, rewrites each finished function:

- `c.xmm[N]` becomes the local `XN`, declared after `LIFT_ENTER` and loaded from `c.xmm[N]`.
- After every `LIFT_SPILL;` the used locals are stored back to `c.xmm[]`, and after every `LIFT_RELOAD;` they are loaded again, so calls, dispatch and HLE thunks see the same state as before.
- `VZEROALL` now writes all 16 `c.xmm[]` slots through the same text, and `fxsave`/`fxrstor` spill and reload around the helper.
- `SWARC_NO_XMM_LOCALS=1` at lift time turns the pass off.

A local whose address reaches an out-of-line function counts as escaped, and clang then keeps it in memory. The out-of-line SSE helpers in `x86/sse.h` were renamed `...Ool` (`Cmp`, `Comi`, `Rsqrt`, the conversions, `HorizOp`, `AddSub`, `Round`, `Dp`, `Rcp`, `Cvtdq2ps`) and the old names are now `always_inline` wrappers that pass copies (`RECOMP_SSE_DS`). The slow paths in `sse_quiet.h` take copies the same way. `Comi` gained an inline path for the lazy-flags case: a native compare of the low lanes.

## Compiler crash

MSYS2 clang 23's SLP vectorizer crashes (access violation in the slp-vectorizer pass) on many lifted functions once XMM values live in locals: f_007422F0, f_007AF740, f_007651F0, f_0075D4E0, f_007DFBB0, f_007D3CB0 and more. The project `CMakeLists.txt` builds every `gen_*.cpp` with `-fno-slp-vectorize`. Turning it off only for the files that crashed was not enough.

## Result

A/B at `--fps 120`, Inner Ward save, `fight7.txt`, 300 s each, same data, run back to back:

| exe | busiest thread | second thread | fps |
|---|---|---|---|
| before (`ffxii_recomp-pre-xmmlocals.exe`, 414 MB) | 71% | 63% | 119.9 at 60 ticks/s, one dip to 55 |
| XMM locals (465 MB) | 63% | 48% | 119.9 at 60 ticks/s, one dip to 49 |

Busy shares come from the thread sampler over 150-210 s. Machine load was 36-97% (new) and 36-88% (old) from other work, so the busy shares are the comparison; both runs hold the cap outside the dips, and neither logged a fatal. Logs: `win-xlnew.log`, `win-xlold.log`.

## Lazy test in a local, inline alignment check (2026-11-26, later)

`quiet::Lazy(c)` (lazy flags on and the default MXCSR) read `c.mxcsr` and `g_lazy_flags` on every SSE operation, and guest stores through `B` may alias both, so clang reloaded them each time. `CacheXmm` now also keeps the test in a local `bool LZ` and calls `Arith`, `Sqrt`, `MinMax` and `Comi` through new overloads that take it first (549,943 call sites, 5,046 functions). LZ is set at entry, after every `LIFT_RELOAD` and after every `Ldmxcsr`. That is enough because MXCSR changes only in LDMXCSR and in calls, and every call ends in `LIFT_RELOAD`, which the cached GPRs already depend on. The FFXII code has no LDMXCSR at all.

`sse::CheckAligned`, the #GP check before each aligned 16-byte move (89,475 sites), was an out-of-line call that only tests four bits; it took 1.1% of the busiest thread by itself. It is now inline, with the failure in the cold `CheckAlignedFail`.

Checks (2026-11-27): SwARc ctest 59 of 59; `lift_int64_test` 35,949 forms, 0 failed; `lift_sse64_test 300` 6,589 forms, 0 failed both native and with `RECOMP_SOFT_SSE=1` (the soft run now takes about 2 minutes, not 10).

A/B at `FFXII_FPS=120`, `fight7-x.txt` from `data-iw-nv`, thread sampler over 150-210 s, machine load sampled every 2 s:

| Run | Machine load (mean) | Game thread | Render thread | fps mean (whole run) |
|---|---|---|---|---|
| LZ, pair 1 (`win-lzNew.log`) | 49% | 65% | 68% | 115 |
| XMM locals, pair 1 (`win-lzOld.log`) | 61% | 76% | 54% | 83 |
| XMM locals, pair 2 (`win-lzOld3.log`) | 35% | 65% | 60% | 114 |
| LZ, pair 2 (`win-lzNew2.log`) | 35% | 53% | 58% | 109 |

Pair 2 ran at the same machine load and is the clean comparison: the game thread (top items f_001E2AF0, f_001E25F0) fell from 65% to 53% busy; the render thread, about two thirds driver and system DLL time, barely moved. On the game thread `sse::` helpers fell from 2.2% to 1.0% of samples and `CheckAligned` (0.8%) is gone. The fps means include load screens and dips, so they do not separate the builds. Pair 1's baseline ran under heavier load and fell well below the cap. The baseline exe once crashed at boot with exit 139 just after loading `nvapi64.dll` (`win-lzOld2-bootcrash.log`) and booted normally on the rerun; that exe predates this change.

## Inline conversions (2026-11-28)

`CvtFloat` (cvtss2sd, cvtsd2ss, cvtps2pd, cvtpd2ps), `CvtIntToF` (cvtsi2ss/sd from 32 or 64 bits) and `Cvtdq2ps` now take `LZ` and run as the host instruction in lazy mode, which already guarantees round to nearest, every exception masked, no DAZ or FTZ, and the default host MXCSR. Out of lazy mode they still call the `...Ool` helpers. int32 to double (`Cvtsi2sd`) is exact, so it is inline in every mode. Sites: 12,452 `CvtFloat`, 3,479 `CvtIntToF`, 6,958 `Cvtdq2ps`. Checks: ctest 59 of 59, `lift_int64_test` and `lift_sse64_test 300` (native and soft) 0 failed.

A/B at 120 fps (`win-cvtNew.log`, `win-cvtOld.log` against the LZ exe): the machine was busy with other work (69% and 77% mean load), so the numbers are rough. The game thread was 72% busy at 118 fps mean against 76% at 107 fps, about 14% less work per frame, and its `sse::` helper samples fell from 1.4% to 0.2%. The render thread was 70% against 63%, mostly driver time. The remaining out-of-line conversions are `CvtToI32` and `CvtToI64` (11,409 and 2,186 sites).

## Oracle with lazy flags on (2026-11-28)

`lift_sse64_test` never turned `g_lazy_flags` on, so until now the oracle compared only the exact SSE path, not the lazy paths `ffxii_recomp` runs by default (inline `Arith`, `Sqrt`, `MinMax`, `Comi` and the conversions under `LZ`). SwARc's x64 test driver now takes `LIFT_TEST_LAZY=1`, which turns lazy flags on for every trial whose MXCSR has the default control (about a quarter to a third of trials) and leaves the six sticky exception bits out of the MXCSR compare, because lazy mode does not keep them by design. The first run without that mask reported `ucomiss` forms failing only on the sticky invalid bit. With it: `LIFT_TEST_LAZY=1 lift_sse64_test 300` passes 6,589 forms, 0 failed. Run it next to the plain and `RECOMP_SOFT_SSE=1` runs for every SSE change.

## Compare flags from the host (2026-11-28)

In lazy mode `Comi` (comiss, ucomiss, comisd, ucomisd) now runs `ucomiss` or `ucomisd` on the host and writes ZF, PF and CF straight from the host flags through asm flag outputs (`=@ccz`, `=@ccp`, `=@ccc`), instead of returning a 0-3 code that the emitted code turned back into three flags with a second compare and NaN tests. COMIS and UCOMIS differ only in raising invalid on a quiet NaN, which lazy mode masks and does not record, so one host instruction serves both. Out of lazy mode the exact `ComiOol` path still runs. In a scratch test the common `if (!(cf||zf))` consumer went from about 14 host instructions to 4. The relift rewrote all 63,319 sites (38,203 comiss, 21,280 ucomiss, 3,741 comisd, 95 ucomisd).

SwARc ba794da. Checks (`build/run/comi-checks.log`): ctest 59 of 59; `lift_int64_test` 35,949 forms, 0 failed; `lift_sse64_test 300` 6,589 forms, 0 failed native, with `LIFT_TEST_LAZY=1` and with `RECOMP_SOFT_SSE=1`. The Windows exe with it is saved as `A:/AI/ffxii-win/ffxii_recomp-comi.exe` (symbols in `nm-comi.txt`) once the full rebuild ends; it has not been measured in game yet. Its A/B baseline is `ffxii_recomp-heap.exe`.

## Possible follow-ups

- `CvtToI32` and `CvtToI64` have no inline fast path yet.
