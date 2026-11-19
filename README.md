# FFXII Recomp

A native-first static recompilation of **Final Fantasy XII: The Zodiac Age** for the owner's Steam 1.0.4.0 installation, with deep engine replacement and total-conversion modding as long-term goals.

**Work in progress:** the project does not yet boot the recompiled game. The first full lift generates and compiles, and the supported x64 integer/SSE instruction-form oracle suites pass. Initial MMX and observed AVX2 forms are integrated; runtime bring-up is active work.

## Goals

- Ahead-of-time native code, not an emulator or runtime CPU JIT.
- Generated lifting as a scaffold; replace original systems with optimized C++ over time.
- A Vulkan renderer with replaceable passes, lighting, shadows, post-processing, and upscaling.
- Verified high-FPS behavior without changing game speed or mechanics.
- Replaceable audio, including surround and spatial-output paths.
- Open content, scripting, and native-module APIs supporting total conversions and new game modes.
- One portable executable, with user files beside it and CLI flags for launcher-independent operation.

No compatibility guarantee is made for existing executable patches, Lua Loader scripts, or native DLL mods. Existing mods may be ported semantically, subject to their permissions and behavioral verification.

## Legal and content policy

You must supply your own legally obtained game. This repository contains no game executable, assets, keys, generated game code, or proprietary SDK binaries. Do not commit or distribute them.

The initial target is `FFXII_TZA.exe` version 1.0.4.0 with SHA-256 `73461d0896204b1c259c52f61aef4efbc95a2ffc388cadb36493d5d1d89721f8`.

## Current technical base

- SwARc for x64 PE lifting, Win32/SEH infrastructure, and incremental replacement.
- C++20 engine/runtime direction and generated C++ from the current lifter.
- Ghidra function and jump-table exports.
- WSL2/Ubuntu for the supported Linux build path.

See `FEASIBILITY.md` for verified target facts, tool references, community issues, and the next implementation gates. See `AGENTS.md` for the project's durable constraints and architecture decisions.

## Development tools

- `tools/inspect_pe.py`: standard-library PE32+ inventory and hashing.
- `tools/lift_ffxii.sh`: repeatable SwARc lift driver for WSL.
- `tools/classify_lift.py`: classifies unimplemented sites by function-discovery provenance.

The SwARc integration currently requires the upstream source plus the patches under `patches/`. Apply them to the matching upstream checkout before building. Generated output and local analysis projects are intentionally ignored.

## License

MIT for project-authored code. Third-party components retain their licenses; see `THIRD_PARTY_NOTICES.md` and `licenses/`.
