# Final Fantasy XII: The Zodiac Age native recomp

## Goal

Build the definitive native version of Final Fantasy XII: The Zodiac Age and an open engine on which any content, gameplay system, renderer component, or game mode can be added, replaced, or removed. The original campaign is the default game and content implementation, not an untouchable constraint.

## Rules

- Use the owner's own copy only. Never download or request game files, keys, or proprietary SDKs. Keep game-derived material local and out of source control.
- No real name or user paths in anything pushed public.
- Release standards from the workspace-root `AGENTS.md` apply: one portable executable, first-run building from the owner's installation, direct play afterward, and CLI flags for launcher-independent operation.
- Inspect the actual target before claiming static-recompilation feasibility. Do not remove or bypass anti-tamper protection.
- Establish a correct vanilla baseline before optimizing or replacing systems. Keep enhancements outside generated code and retain reference/A-B paths for correctness checks.

## Owner decisions (2026-11-19)

- Target the Windows Steam release of Final Fantasy XII: The Zodiac Age, expected version 1.0.4.0, subject to local executable verification.
- Use ahead-of-time static recompilation, not a runtime emulator or CPU JIT.
- Do not preserve the retail executable's addresses, memory layouts, calling conventions, Lua Loader ABI, native DLL ABI, or unmodified Nexus mod compatibility as hard requirements.
- Existing mods may be rewritten or ported, including with AI assistance, subject to their licenses/permissions and behavioral verification. Asset conversion and semantic reimplementation are acceptable; one-to-one execution compatibility is not required.
- Favor optimization freedom, clean high-FPS work, renderer replacement, and portability over legacy compatibility.
- Use generated C for lifted game code, C++20 for the engine/runtime/renderer, and Vulkan for the replacement graphics backend. Keep the original D3D11 path initially as a correctness reference where practical.
- Use a small embeddable scripting language for ordinary gameplay mods and a versioned native C ABI with C++ conveniences for advanced modules. No language choice is a performance claim by itself.
- Support arbitrary transformation: levels, enemies, moves, weapons, loot, combat, progression, cinematics, UI, audio, rendering, entry points, game modes, and win/loss conditions must all be replaceable. A total conversion or survivors-like mode must be possible without retaining the campaign's rules.
- Provide open content formats, scripted gameplay, replaceable engine subsystems, native modules, and open source as the final escape hatch. Support universal replacement points, not only before/after notifications.
- Visual goals include native 4K+, verified 120+ FPS without changed game speed or mechanics, improved models/textures, DLSS/DLAA, ray tracing, anisotropic filtering, DOF, configurable shadow resolution and filtering, soft/hard shadows, and lighting/render-pass replacement. These follow a correct native baseline.
- Audio goals include a replaceable mixer/audio engine, proper 5.1/7.1 output, Windows Spatial Sound/Dolby Atmos where supported, HRTF/headphone modes, dynamic-range options, and moddable music/SFX/voice routing. Do not present simple stereo upmixing as true positional or Atmos support.
- Remove legacy 32-bit pointer/address assumptions during native subsystem replacement, including the tkMalloc failure class. The retail executable is already x86-64, but lifting alone does not make all internal data and address handling 64-bit.
- Do not build the example mods or game modes; they define the required capability envelope.

## Architecture direction

- Build a custom native engine/runtime, not a Unity, Unreal, Godot, or other general-purpose-engine remake.
- Treat original FFXII as the base game package. No campaign flow, combat system, resource, render pass, or ending condition is mandatory for a modded session.
- Use stable resource identifiers, versioned interfaces, dependency/conflict reporting, per-mod save-state namespaces, hot reload, inspection, and diagnostics.
- Distinguish trusted native modules from sandboxed content/script mods.
- Keep original-address metadata and generated dispatch information when inexpensive, but do not let legacy compatibility constrain data layouts, optimization, or subsystem replacement.

## Milestones

1. Inspect and lock the clean target executable; inventory PE layout, imports, middleware, function/exception metadata, and asset containers.
2. Build and validate the x64-to-C lifter and native runtime.
3. Reach playable, save-safe gameplay with the original graphics behavior.
4. Introduce Vulkan with reference parity and renderer-independent resolution scaling.
5. Decouple simulation/rendering and verify high-FPS behavior.
6. Profile and optimize or replace engine internals without legacy ABI restrictions.
7. Build the open content, scripting, native-module, and total-conversion platform.
8. Port selected existing mods by behavior and add advanced rendering features after their prerequisites exist.

## Current state

- **Target locked and inspected (2026-11-19):** Steam `FFXII_TZA.exe` 1.0.4.0, 32,864,752 bytes, SHA-256 `73461d0896204b1c259c52f61aef4efbc95a2ffc388cadb36493d5d1d89721f8`. Ordinary x86-64 PE sections; 7,997,440 file-backed `.text` bytes, 35,713 unwind entries, 538 imports in 30 DLLs, no delay imports, and no FFXV-style protected-section signature. See `FEASIBILITY.md` and `analysis/target-1.0.4.0.json`.
- `tools/inspect_pe.py` is the standard-library target inspector. Its import and unwind counts were independently checked against `pefile`; REA matched its SHA-256.
- **Primary lifter candidate:** MIT SwARc, cloned at `../ref/SwARc`. Its x64 PE, Win32, SEH/unwind, SSE/AVX, oracle-test, and arbitrary-function-replacement architecture matches this project. Evaluate it before extending the leaf-only workspace prototype.
- REA/Kimi updated to 6.9.0 with skill 62, workspace-local Node 26.12.1, and a 900,000 ms Ghidra startup budget. The live server reports aligned 6.9.0. The prior Kimi registration was removed after the new server passed its identity check; the setup recovery file preserves rollback. Full Ghidra analysis then completed with 35,703 procedures and 119,587 defined strings; the exe references unavailable `FF12X64.pdb`.
- SwARc requires a supported Linux or macOS build host. WSL2 and Ubuntu are installed and operational without a Windows restart. Linux dependency installation and the SwARc build follow. CMake 4.4.5, Ninja 1.13.2, and existing Clang 23.1.5 are also available on Windows, but they do not make SwARc's Windows host supported.
- **SwARc map export complete:** `analysis/maps/functions.tsv` has 35,183 unique function rows (280 multi-chunk, 815 thunks); `jumptables.tsv` has 1,029 rows (523 resolved, 506 unresolved). Structural checks passed. `exports.tsv` and `lifter.manifest` complete the initial symbol package. Hashes are recorded in `FEASIBILITY.md`.
- **First full lift completed (2026-11-19):** SwARc generated 577 MB of C++ in 361 files: 43,288 candidate functions and 4,770,969 instruction sites. It reported 1,120 unimplemented sites, including SIMD forms and apparent false code candidates from data pointers. After adding MMX and the observed AVX2 forms, `analysis/lift-classification.json` has **zero unimplemented sites in Ghidra-known functions**; the remaining 691 are all in discovered-only candidates, including all 384 undecodable sites and the privileged/instruction-looking noise. This is generation coverage, not proof of runtime correctness. The game-specific x64 oracle harness is in `CMakeLists.txt`; use `tools/lift_ffxii.sh` inside WSL.
- SwARc's complete fixture/runtime suite passes **58/58 tests** in WSL after the MMX/AVX2 additions, including x86/x64 integer, SSE, x87, MMX, PE, HLE, and unwind-related fixtures. FFXII's game-specific **35,945 integer and 6,589 SSE instruction-form oracle suites both pass** with zero failures. The full generated-code compile is the next check; missing MMX/AVX forms and runtime control flow remain open.
