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

## Possible follow-ups

- The lazy SSE path still reads `c.mxcsr` and `g_lazy_flags` on every SSE operation; a per-function local would remove those loads.
- `CvtFloat` and `CvtIntToF` have no inline fast path yet.
