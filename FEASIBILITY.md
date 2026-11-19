# FFXII native recomp: initial feasibility and research

## Verdict

Proceed to a SwARc-based x64 lifting prototype. The inspected Steam 1.0.4.0 executable exposes ordinary x86-64 code and standard PE metadata; it does not have the protected-section layout that blocked the FFXV target. This clears the first feasibility gate, not the entire full-game lift.

The next gate is to generate and differentially test a representative set of FFXII functions, then lift the complete executable and resolve unsupported instructions, indirect branches, callbacks, and Win64 exception handling.

## Verified target

- File: `FFXII_TZA.exe`, Windows file/product version **1.0.4.0**.
- Size: **32,864,752 bytes**.
- SHA-256: `73461d0896204b1c259c52f61aef4efbc95a2ffc388cadb36493d5d1d89721f8`.
- PE32+ x86-64 image with ten normally named sections.
- `.text`: **7,997,440 file-backed bytes**, entropy about 6.62; no large fill-only or high-entropy protected-code region was found.
- `.pdata`: 428,556 bytes, representing **35,713 runtime-function/unwind entries**.
- **538 standard imports across 30 DLLs**, no delay-load imports.
- Important boundaries include D3D11, DXGI, D3DCompiler, DInput8, XInput, WinMM, Steam, Win32, UCRT, and MSVCP140.
- The version and import/unwind counts were independently checked with Windows version metadata and `pefile`; REA independently reported the same target SHA-256.
- Full Ghidra 12.1.4 analysis completed after increasing REA's startup budget: **35,703 analyzed procedures** and 119,587 defined strings. Procedure discovery coverage remains an analysis-provider result, not proof that every indirect target is known.
- A direct SwARc-compatible export produced **35,183 function rows** (280 multi-chunk, 815 thunks) and **1,029 computed-jump rows** (523 resolved, 506 unresolved). These counts come from a separate direct Ghidra invocation and are not expected to be identical to REA's local/external procedure inventory.
- Export SHA-256 after LF normalization for SwARc: `functions.tsv` = `7d1c70fb33e9d74989d7d9b025828cec4e1a3b5715dea99d2566d5d68dd14b4d`; `jumptables.tsv` = `92fb0321c16992a100a83fe5b3cfc141746e3bedaa89280ea3c7b8b7e5e8fc34`; `exports.tsv` = `f874434ebec8b05d29850c953e151b64e88de4525947d0be005cc79393917ff6`. Both maps passed structural and unique-entry checks. `exports.tsv` records the observed absence of PE exports, and `lifter.manifest` is ready for discovered overrides.
- The executable references `FF12X64.pdb`; no companion PDB is available. Original names and types therefore remain unavailable unless another lawful source is found.

The local inspection tool is `tools/inspect_pe.py`; its output is `analysis/target-1.0.4.0.json`. It performs no target execution.

## Best reusable foundation

### SwARc — primary candidate

[SwARc](https://github.com/JTM-rootstorm/SwARc) is an MIT-licensed static recompiler with x86 and x86-64 PE support, portable C++ output, Win32 HLE, x64 unwinding/SEH, and exact SSE/AVX models. Its architecture explicitly treats lifted code as a scaffold and permits arbitrary functions to be replaced by handwritten C++ until no lifted code remains.

That matches this project's optimization and total-conversion goals much better than extending the workspace's leaf-only `x64-c-recompiler` prototype. SwARc also has instruction-oracle tests and manifest-driven function replacement.

Current limitation: its supported build hosts are Linux x86-64 and macOS ARM64, with SDL3 and other native dependencies. Windows is not a supported host. WSL2 with Ubuntu is installed and operational; no Windows restart was ultimately required. SwARc's Linux dependency installation and build are the next setup step. This is a host-tooling issue, not a defect in the target or lifter.

A local reference checkout is at `../ref/SwARc`.

### FFXII-specific tools

| Project | Value | License/use constraint |
| --- | --- | --- |
| [StyuDEV/ffxii-tza-tools](https://github.com/StyuDEV/ffxii-tza-tools) | Documented character animation/model formats; glTF export; Phyre meshes, textures, shader/reflection data; bit-exact CA/CM rebuilds; whole-library validation claims | MIT. Primary asset-pipeline reference. Cloned at `../ref/ffxii-tza-tools` |
| [Xeavin/The-Insurgents-Workshop](https://github.com/Xeavin/The-Insurgents-Workshop) | Binary-data-to/from-readable-data workflows and documentation | Restrictive permissions; no code/assets incorporated. Behavioral reference only |
| [LuminaSapphira/ff12tza-utils](https://github.com/LuminaSapphira/ff12tza-utils) | Open format documentation, treasure data, lists/maps, magic-order tooling | MIT. Cloned at `../ref/ff12tza-utils` |
| [davoodinator/ff12doc](https://github.com/davoodinator/ff12doc) | VBF documentation | Review individual material before reuse. Cloned at `../ref/ff12doc` |
| [mercurialuke/ffxii-save-editor](https://github.com/mercurialuke/ffxii-save-editor) | Save-format research and editing behavior | Review license before copying. Cloned at `../ref/ffxii-save-editor` |
| [ffgriever/ff12-tkmalloc](https://gitlab.com/ffgriever/ff12-tkmalloc) | Documented allocator failure and low-address reservation implementation | BSD-2-Clause. Cloned at `../ref/ff12-tkmalloc`; useful as a defect reference, not the desired final architecture |
| [Xeavin/The-Insurgents-Graveyard](https://github.com/Xeavin/The-Insurgents-Graveyard) | Readable examples of requested Lua/gameplay changes and compatibility cases | Restrictive permissions; do not copy or derive distributed ports without permission |

The StyuDEV tools report full decoding of 3,164 CA animation files and 1,066 CM character models, with byte-identical unmodified rebuilds. Their documented remaining workflow gap is a polished Blender-to-game authoring path for new animations. These are upstream validation claims until reproduced locally.

### General recomp/render references

- [Heavy Metal: FAKK2](https://github.com/sp00nznet/heavymetal) and `pcrecomp` (MIT): practical Win32 callback, import-forwarding, runtime-function-discovery, and bring-up patterns. Its current game lift is 32-bit, so it is not the x64 CPU solution.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) (MIT): function replacement, dispatch, and recomp architecture ideas; PowerPC rather than x64.
- [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) (MIT): shader translation and cache ideas.
- [RT64](https://github.com/rt64/rt64) (MIT): enhanced renderer, upscaling, ray-tracing, and material/lighting architecture ideas; N64-specific portions require adaptation.
- Existing Switch recomp work remains useful for differential testing, mod packaging, shader caches, and host-runtime organization, but not for the FFXII x64 instruction decoder.

Additional shallow reference checkouts, pinned to the observed commits, are under `../ref/`: `pcrecomp` 621860a9d22c, `heavymetal` fe5c1ab3c560, `XenonRecomp` ddd128bcca99, `XenosRecomp` 70a084c23923, `rt64` d9817c68ffd7, `N64Recomp` 598ce2080004, `Zelda64Recomp` b65c482ed672, `UnleashedRecomp` 700bbd2c0d33, `LASRecomp` f7e8a8f3d2bf, `ACNHRecomp` 765aa5409c79, `totk-pc-recomp` 6151ac4fbc2c, `totk-launcher` 69a77da6cf8b, `libultraship` 759b13401cd6, `dxvk` d9af36496fef, `tracy` fdd94f5533e0, `FF12-MO2-Plugin` 5d715540a116, and `FF12Randomizer` 63f3560c8125. Licenses must be checked before code reuse; several game runtimes are copyleft and are idea/behavior references only.

## Community requests and PC-engine findings

### High-priority defects to eliminate

1. **Legacy 32-bit address assumptions and tkMalloc crashes.** The current [FF12 tkMalloc Fix](https://www.nexusmods.com/finalfantasy12/mods/475) documents startup, save-load, and zone-transition crashes when allocator memory lands above the legacy 2 GiB boundary. The retail executable is x64, but affected game systems retain 32-bit pointer/address assumptions. The native replacement should remove the restriction, not merely pre-reserve low memory.
2. **Incomplete high-FPS behavior.** The November 2026 [Plus Menu](https://www.nexusmods.com/finalfantasy12/mods/562) describes field-only 120 FPS as in development, keeps menus/title/movies at 60, and records a fixed movie speed/audio-sync failure. This supplies useful scenarios but does not establish full-game 120 FPS correctness.
3. **5.1 channel routing.** The same tool identifies swapped back-right and subwoofer channels in 5.1 movies. [PCGamingWiki](https://www.pcgamingwiki.com/wiki/Final_Fantasy_XII:_The_Zodiac_Age) separately documents surround/audio limitations. These become explicit audio test cases before Atmos/spatial enhancements.
4. **Windows volume persistence.** Plus Menu reports that the game resets its Windows per-application volume/mute state. A native audio layer should preserve user choices.
5. **Performance-sensitive AO and high MSAA.** PCGamingWiki documents heavy costs from full-resolution ambient occlusion and notes the existing 30/60 limiter. Measure these in the native renderer rather than assuming the old settings are optimal.

### Modding capability gaps and test cases

- Existing loader ecosystems show demand for stable deployment, conflict handling, and predictable updates. The [External File Loader](https://www.nexusmods.com/finalfantasy12/mods/170) and [Lua Loader](https://www.nexusmods.com/finalfantasy12/mods/171) each have more than one hundred dependent mods listed.
- The current character tools prove substantial model/animation access, but a complete new-content workflow still needs open level geometry, collision, navigation, triggers, camera, minimap, and cinematic authoring. No mature end-to-end custom-level pipeline was found in the sources checked; this is a bounded research result, not proof of impossibility.
- Plus Menu exposes useful native-platform test scenarios: arbitrary monster spawning, unused locations, play-as-model, camera changes without wall collision, chest/RNG control, and sharing EXP. Its stated limitation that towns have no monster spawn points illustrates why the new engine must allow mods to create spawn logic rather than only edit existing records.
- The older Steam request thread [Mods you hope to see](https://steamcommunity.com/app/595520/discussions/0/2595630410188641049/) records demand for dual wielding, missing spells, Blue-Mage-style enemy abilities, original license boards, benched-member XP, and cutscene skipping. Several are addressed by later mods; treat them as compatibility/porting scenarios, not as unimplemented claims today.
- True total conversion requires replacing entry points, game-state flow, spawning, combat, progression, saves, cinematics, and win/loss conditions. Supporting only existing FFXII records would not satisfy the project goal.

## Immediate implementation gate

The Linux environment, SwARc build, metadata export, and first full lift now exist. The full lift generated 577 MB of C++ in 361 files, with 43,288 candidate functions and 4,770,969 instruction sites. Its initial report contained 1,120 unimplemented sites. MMX and observed AVX2 support reduced this to 691, all in discovered-only candidates; none remain in Ghidra-known functions. See `tools/classify_lift.py` and `analysis/lift-classification.json`. This does not prove every remaining candidate is false or every valid indirect target has been discovered.

1. The complete SwARc suite passes 58/58 tests after the MMX/AVX2 additions. The FFXII harness in `CMakeLists.txt` passes all 35,945 integer and 6,589 SSE instruction-form oracle suites with zero failures.
2. Classify unimplemented sites by genuine reachability and instruction family; remove false pointer-derived entries rather than implementing instructions for ASCII/data by default.
3. Add required instruction semantics with oracle coverage, then compile the generated game and resolve linkage/runtime boundaries.
4. Bring up the native executable incrementally, using runtime-discovered addresses only to improve the next ahead-of-time lift.

Do not begin the custom Vulkan renderer or mod API before the native baseline is correct. Design those layers so original systems can be replaced wholesale once the baseline works.
