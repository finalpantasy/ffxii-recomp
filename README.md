# FFXII Recomp: native PC port of Final Fantasy XII: The Zodiac Age

A static recompilation of **Final Fantasy XII: The Zodiac Age** (Steam, Windows, version 1.0.4.0) into native C++, with an open modding platform built into the runtime. The game's x86-64 code is translated ahead of time by the [SwARc](https://github.com/JTM-rootstorm/SwARc) lifter. It is not an emulator and has no CPU JIT.

You need your own copy of the game. This repository contains no game executable, assets, keys or generated game code.

## Status

Developer preview. There is no packaged release, and building it takes a developer setup (see below).

What works, tested on the owner's Steam copy:

- Boot to the title screen, the opening movie, New Game, and Load Game with existing Steam saves.
- Field play, talking to NPCs, menus, battles, cutscenes with subtitles, and autosaves.
- The Windows build runs on the real GPU through the system D3D11. The Linux/WSL build runs headless through DXVK.
- **60 fps** held through heavy multi-enemy fights.
- **High frame rates at normal game speed:** `--fps 120`, 144, 165, 240 or any rate from 60 up. The game updates every displayed frame and keeps 60 ticks a second. These are real updates, not interpolated frames. You can also pick the rate in Config > Graphics Settings > Frame Rate Limit.
- Audio output, keyboard, mouse and XInput pads.

Not done yet: a Vulkan renderer, the one-file release build that builds itself from your installation on first run, and a full playthrough check. Some lifted code paths (C++ rethrow, `longjmp`, parts of the CRT) are written but have not yet run in game.

## Performance work

Most of the CPU time saved so far came from these lifter and runtime changes:

- SSE math runs as host instructions instead of a soft-float emulator. Exact MXCSR behavior is kept wherever the game can observe it (`--exact-sse-flags` restores full sticky-flag tracking).
- XMM registers live in function locals instead of a context struct.
- `memcpy`, `memset`, `sqrtf` and other CRT calls are inlined at the call site.
- A per-thread cache for COM interface lookups, and a size-class guest heap.

Each step is checked against an instruction-level oracle: every integer and SSE instruction form in the game executable (35,949 and 6,589 forms) runs natively and lifted, and the results must match. The measurements and their logs are in `analysis/`.

## Modding

Mods live in `mods/` beside the executable, one folder per mod with a `mod.ini`. Start with [analysis/formats/modding-quickstart.md](analysis/formats/modding-quickstart.md).

- **File replacement:** loose files override entries in the game's VBF archive, with priorities, `requires`, `conflicts` and hot reload (`--mods-watch`).
- **Text and data merging:** patch single strings or single record fields (`=`, `+=`, `*=`, set-if-original). Several mods that change the same file are merged field by field instead of one winning. `--mods-list` prints every overlap and how it was resolved.
- **Luau scripts** (native code generation) and **WebAssembly modules** (Wasmtime) can hook, wrap or replace any lifted function, read and write game memory, and call game functions. The sandbox blocks access to the host (files outside the mod, the network, processes), never to the game.
- **Native modules:** trusted DLLs through a versioned C ABI ([include/ffxii_mod.h](include/ffxii_mod.h)) with C++ helpers ([include/ffxii_mod.hpp](include/ffxii_mod.hpp)).
- Examples are in [examples/](examples/): stat recomputation, gambit changes, and C, C++, Luau and WASM samples. `--mod-lab` opens a local page that times the same hook in each language.

Format notes: [VBF archive](analysis/formats/vbf.md), [game text](analysis/formats/text.md), [saves](analysis/formats/save.md), [gambits](analysis/formats/gambits.md), [hook points](analysis/hook-points.md), [frame timing](analysis/frame-timing.md).

## Command-line flags

| Flag | Effect |
|---|---|
| `--game-dir PATH`, `--exe PATH` | Game installation and executable |
| `--fps N` | Frame rate, 60 or higher (needs `FrameLimit=60` in the game settings) |
| `--fps-overlay` | Show presented fps and the slowest frame |
| `--interp`, `--interp-no-pace`, `--interp-t T` | Interpolated in-between frames (experimental) |
| `--mods-dir PATH`, `--mods-disable a,b`, `--mods-log`, `--mods-watch` | Mod folder, disabled mods, file-open log, hot reload |
| `--mods-list` | Print the resolved mod report and exit (exit code 1 on errors) |
| `--no-native-mods`, `--luau-interp`, `--wasm-time-limit MS` | Mod runtime controls |
| `--mod-lab`, `--mod-lab-run` | Hook-cost benchmark page, or its JSON output |
| `--exact-sse-flags` | Keep exact MXCSR exception flags (slower) |

## Building

You need the Steam `FFXII_TZA.exe` 1.0.4.0 with SHA-256 `73461d0896204b1c259c52f61aef4efbc95a2ffc388cadb36493d5d1d89721f8`. Other versions will not lift correctly.

1. **SwARc with this project's patches.** Clone SwARc next to this repository as `../ref/SwARc`, check out upstream commit `9170ecc`, and apply the series: `git am ../../final-fantasy-xii/patches/swarc/*.patch`. You can point `SWARC_DIR` at another location.
2. **Lift (Linux or WSL2).** Build SwARc's `recomp_lift`, then run `FFXII_EXE=/path/to/FFXII_TZA.exe tools/lift_ffxii.sh`. It reads the function maps in `analysis/maps/` and writes about 580 MB of C++ to `generated/full/`, which stays out of git.
3. **Build the runtime (Windows).** In MSYS2 UCRT64 with clang, run `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DRECOMP_LLVM_ROOT=<msys64>/ucrt64`, then `ninja -C build ffxii_recomp`. A full build of the generated code takes 45 to 60 minutes. The exe links its runtime statically and imports only system DLLs. For WebAssembly mods, add `-DFFXII_WASMTIME_DIR=<unpacked Wasmtime 51 mingw C API>`.
4. Run `ffxii_recomp.exe --game-dir "<Steam>/steamapps/common/FINAL FANTASY XII THE ZODIAC AGE"`.

## Repository layout

- `src/`: runtime, frame-rate hooks, mod loader, Luau and WASM runtimes, text and data merging.
- `include/`: mod ABI headers.
- `analysis/`: verified target facts, function and jump-table maps, format and timing notes.
- `tools/`: PE inspector, lift driver, VBF and text tools, memory scanner, hook generators.
- `patches/swarc/`: the SwARc changes this project needs, as a `git am` series.
- [FEASIBILITY.md](FEASIBILITY.md): target inspection and recompilation feasibility.
- [AGENTS.md](AGENTS.md): design decisions, constraints and a detailed progress log.

## Legal

Bring your own legally obtained copy of the game. Do not commit or distribute the game executable, assets, generated code or saves. This project is not affiliated with or endorsed by Square Enix.

## License

MIT for project-authored code. Third-party components keep their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [licenses/](licenses/).
