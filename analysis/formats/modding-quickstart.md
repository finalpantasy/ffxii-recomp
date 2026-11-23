# Modding quick start (recomp build)

The recompiled game reads loose files before the archive, so a mod is a folder of files. You don't repack `FFXII_TZA.vbf`.

1. Find the path. List the archive with `python tools/vbf.py list <FFXII_TZA.vbf> --grep <text>`, or boot with `FFXII_MODS_LOG=1` to log every path the game opens and then run `python tools/vbf.py opened <FFXII_TZA.vbf> <log> [--grep <text>]`. That prints each opened path once, in first-open order, with its archive size and open count. A "missing" path is one the game tries but the archive lacks, which a NEW loose file can supply.
2. Extract the original: `python tools/vbf.py extract <FFXII_TZA.vbf> "<path>" <mods dir>`. The extract keeps the archive's folder layout, so the file lands where the override looks for it.
3. Edit the file in place.
4. Check the folder: `python tools/vbf.py mods <FFXII_TZA.vbf> <mods dir>`. Each file is labeled OVERRIDE (replaces an archive entry), NEW (a path the archive lacks), SAME (byte-identical to the archive, so delete it) or CASE (has uppercase letters; the game asks for the lowercase path, so on Linux the file never loads). It exits 1 if any file is CASE.
5. Run the recomp. The mods folder is `<game dir>/mods`, or set `FFXII_MODS_DIR=<mods dir>`. Each file it loads is logged as `[mods] <path> (<bytes> bytes)`.

Notes:

- Paths are lowercase with `/`, as the game normalizes them.
- A loose file can add a path the archive lacks. The game tries language folders in order (`us/` before `in/` for title images), so a file in an earlier folder wins.
- The override covers files opened through the game's VFS open (`0x59EE90`). Anything read another way is untested.
- Implementation and layouts: `vbf-runtime-read-path.md`. Archive format: `vbf.md`.

## Editing dialog

1. Extract the script: `python tools/vbf.py extract <FFXII_TZA.vbf> "*ps2data/plan_master/us/event/<area>/<script>/<script>.ebp" <work dir>`. To find a line, boot with `FFXII_MODS_LOG=1`; the scene's script is among the paths it opens.
2. `python -I tools/ff12text.py dump <script>.ebp -o lines.json`, edit the `text` fields, and keep `{..}` groups as they are.
3. `python -I tools/ff12text.py build <script>.ebp lines.json -o <mod>/files/ps2data/plan_master/us/event/<area>/<script>/<script>.ebp`.

The character set and file layout are in `text.md`.

Menu, help and battle text works the same way with the files in `ps2data/image/ff12/test_battle/us/binaryfile/`. Battle messages and names (commands, items, abilities) come from `battle_pack.bin`: the game never opens the separate `battle_message.bin` or `word.bin`, so edit the pack.

## Patches and merging

A whole edited copy of a file blocks every other mod that edits the same file. A patch changes only the strings and data fields it names: `<mod>/patch/<archive path>.txt`, one line per change.

```
# <mod>/patch/ps2data/image/ff12/test_battle/us/binaryfile/battle_pack.bin.txt
p 61 w 4 1 = {00}{00}Magicks (patch)
p 61 w 4 3 = {00}{00}Items (patch)
p 13 r 1 0x1A u8 *= 2
p 13 r 1 0x12 u16 = 500
```

- String keys: `s <i>` is string i of an event script or st2e table, `w <t> <e>` is entry e of word sub-table t, and in a pack (`battle_pack.bin`) `p <k>` comes first and names the section. `ff12text.py dump` lists the strings in the same order: the key index is the position in the JSON list.
- Text uses the `ff12text.py` form: `{XX}` for a raw byte, `\n` for a line break and `\` for a backslash. Word entries start with `{00}{00}`, as in the dump.
- Data keys: `r <i>` is record i of an st2e data table (`p <k> r <i>` in a pack). A data line is `<record> <byte offset> <type> <op> <value>`. Types are `u8 u16 u32 i8 i16 i32 f32`, little-endian. Offsets and integers may be hex (`0x1A`) or negative.
- Operations: `=` sets the field. `=?` sets it only if no other mod changed it. `+=`, `-=` and `*=` add, subtract and multiply; integer results round and stop at the type's limits. `|=` and `&~=` set and clear bits.
- Lines starting with `#` are comments. One space after `=` is dropped from text; any further spaces are kept.

Example: section 13 of `battle_pack.bin` holds 557 equipment records of 52 bytes. Record 1 is the Broadsword (word table 1, entry 1); u16 0x12 looks like the price and u8 0x1A is attack (confirmed in game). Menus show an attack change at once, but battles use a copy of the stats kept in the save: a new game picks the change up, and on an old save the prologue battle still used the old value. The copy is byte +0x36 of each character record in the save (`save.md`); add `examples/luau-recompute-stats`, which recomputes it from the merged table when a save loads.

When the index is built, the recomp reads each patched or contested file from the archive and merges every mod's version against it. A version is a mod's whole copy (if it ships one) with its strings and `=` sets applied.

- Strings merge whole: a string that one mod changed is kept. If two mods change it to the same bytes the report says "identical"; if they differ, the higher-ranked mod wins and the report says "priority".
- Records merge byte by byte, so two mods that change different fields of one record both apply. Where they change the same bytes differently, the higher-ranked mod wins ("priority").
- `=?` lines then apply where the field still holds the original value; otherwise the report says the line was not applied.
- Relative operations (`+=`, `-=`, `*=`, `|=`, `&~=`) from every mod apply last, on top of the merged value, lowest-ranked mod first. Where several mods change one field this way, the report lists them in order ("operation"). Two mods that each multiply attack by 2 give 4 times the attack.
- A whole copy that adds or removes strings cannot be merged: the best-ranked such copy is used as it is, and every other mod's change to that file is listed as "needs-patch".

The report (`mods-report.txt`, `--mods-list`) has a `merge <path>` block for each merged file. A line naming a record that does not exist or a field past the record's end is skipped and reported. `--mods-list` exits 1 on a patch line it could not apply.

## WebAssembly modules

`wasm = a.wasm, lib/b.wasm` in `mod.ini` loads sandboxed WebAssembly modules (paths inside the mod folder; a `.wat` text file also works). The index checks that each file exists and lists it in `mods-report.txt` and `--mods-list`, which exits 1 when one is missing. The game runs the modules in Wasmtime 51, compiled to native code at load, and logs `[wasm] <id>: N modules (Wasmtime 51.0.0)`. A build configured without `FFXII_WASMTIME_DIR` logs `not loaded` instead and runs without them.

Write a module in C with `include/ffxii_wasm.h` and build it with clang's wasm32 target (no libc needed):

```
clang --target=wasm32 -O2 -nostdlib -Wl,--no-entry -Wl,--export-dynamic -I include mymod.c -o mymod.wasm
```

`examples/wasm-sample` is a complete mod (`sh build.sh` builds it). The module imports its functions from `ffxii` and nothing else, so it cannot open files, sockets or processes. After loading, the recomp calls the module's `ffxii_init` export (0 means success). A hook is an exported function `(i64, i64, i64, i64) -> ()` that receives rcx, rdx, r8 and r9; `ffxii_hook(va, "name", len)` installs it, and inside it `ffxii_original()` runs the rest of the chain or `ffxii_ret()` returns without it. Registers, stack arguments, xmm floats and `ffxii_call` work as in Luau.

The module keeps its own linear memory. It sees the game only through calls: game memory is reached with `ffxii_read_u8` ... `ffxii_read_f64`, the matching writes and `ffxii_read_bytes`/`ffxii_write_bytes`. Each call checks that the game has the pages mapped (writes also need writable pages). A bad address traps the hook, the log names the address and the function, and the game's function runs as if the hook were absent. Out-of-bounds access to the module's own memory traps the same way. Later calls run the hook again. The game's memory is not mapped into the module directly, because a stray pointer into an unmapped game page would then crash the game instead of trapping.

Verified in game (`win-wasm4.log`, Windows GPU build). The sample hooked the entry point and the window procedure, read the exe's `MZ` header and counted 2,000 window messages. Its deliberate bad read at message 500 trapped only that call. A second module's out-of-bounds read trapped too, and the title screen came up with both modules loaded.

## Luau scripts

A mod runs Luau scripts listed in its `mod.ini`:

```
id = wndcount
name = Window procedure counter
version = 1
language = Luau
scripts = count.luau
```

Paths are inside the mod folder, like `native`. Scripts load before the game's entry point, in the same order as native modules, and run as native x64 code when the CPU supports it. `--luau-interp` (`FFXII_LUAU_INTERP=1`) runs them in the interpreter instead. `--no-native-mods` skips DLLs only; scripts still load. Each script runs in the Luau sandbox: no file, network or process access, but full access to the game.

```lua
ffxii.hook(0x798FF0, function(cpu, original)
  ffxii.log(string.format("message %d", cpu.rdx))
  original(cpu)          -- run the game's function (and any hook below this one)
end)
```

The hook gets `cpu`, `original` and the first four integer arguments (the values of rcx, rdx, r8 and r9 at entry), so `function(cpu, original, a, b)` can read its arguments without touching `cpu`. Read or set registers as `cpu.rax`, `cpu.rcx`, ... `cpu.r15`; each such access is a call into the recomp (about 25 ns on a loaded machine), so read a register into a local once instead of in a loop. Call `original(cpu)` to run the next hook or the game's function, or set `cpu.rax` and call `ffxii.ret(cpu)` to return without it. A hook that does neither runs the original and logs one warning.

| Function | Does |
|---|---|
| `ffxii.hook(va, fn)` | hook a lifted function; returns true |
| `ffxii.ret(cpu)` | return from the hooked function (pops the return address) |
| `ffxii.arg(cpu, i)` | argument i (0-3 from rcx, rdx, r8, r9; 4 and up from the stack) |
| `ffxii.xmm_f32(cpu, i)`, `ffxii.set_xmm_f32(cpu, i, v)` | low float of xmm0-15 |
| `ffxii.call(cpu, va, ...)` | call a game function with up to 8 integer arguments; returns rax |
| `ffxii.read_u8` ... `read_u64`, `read_i8` ... `read_i32`, `read_f32`, `read_f64` | read guest memory at an address |
| `ffxii.write_u8` ... `write_f64` | write guest memory |
| `ffxii.log(text)`, `print(...)` | write to the game log as `[mod <id>]` |

Numbers are Luau doubles, so register and memory values above 2^53 lose their low bits. Game addresses (below 2^32) are exact.

## Mod Lab

`ffxii_modlab` (or `ffxii_recomp --mod-lab`) serves a local page at `http://127.0.0.1:8723/` and opens it. The page runs the same test mod written in C, C++ and Luau (`examples/modlab`, built into `bin/modlab`) against three stand-in game functions through the real mod API: a filter that calls the original, a replacement that reads a table, and a replacement with a 32-step loop. Each language's hooks load into a fresh hook chain, every call's result is checked against the expected value, and the page shows nanoseconds per call (median of the repeats), the cost over the game function alone, load time and the machine's CPU load during the run. Luau runs twice, as native code and in the interpreter. The game does not run; the numbers measure the hook boundary and the script itself.

Flags: `--mod-lab-dir PATH` (mods to test, default `<exe dir>/modlab`), `--mod-lab-port N`, `--no-browser`. `--mod-lab-run` runs once and prints JSON (`--mod-lab-iters N`, `--mod-lab-reps N`, `--mod-lab-only lab-c,lab-luau:native`). Any mod folder with `native` or `scripts` lines that hooks 0x1000, 0x2000 and 0x3000 (`examples/modlab/lab_targets.h`) can be added to the lab folder.

Entering a Luau hook costs about 60 ns on a loaded machine, almost all of it Luau's protected call and C-call machinery, so native code and the interpreter cost the same for short hooks. A hook that reads its argument ten times went from about 330 ns (through `cpu.rcx`) to 106 ns (through the argument). Caching registers in a Lua table was tried and was slower: emptying and walking the table on every call cost more than the C calls it saved.

WebAssembly (`examples/modlab/lab-wasm`, C built to wasm32 by CMake when Wasmtime is configured) runs the same tests through Wasmtime's unchecked call path. With checked calls each hook cost about 440 ns; unchecked calls and host functions brought it to 115-123 ns for the filter and table read and 358 ns for the 32-read loop, against 731 ns (Luau native) and 1,105 ns (Luau interpreter) in the same run (99% machine load, `modlab-wasm2.json`). All results matched.

First results (Ryzen 9 9950X3D2, machine at 100% CPU from other work, 1,000,000 calls, median of 5): game function alone about 2 ns; C and C++ add 3-4 ns for the filter and table read and about 56-59 ns for the loop; Luau adds about 105-136 ns for the filter and table read (native code and interpreter about the same, so the boundary dominates) and 796 ns (native) or 1,190 ns (interpreter) for the loop. All 15 language-test pairs returned correct results.
