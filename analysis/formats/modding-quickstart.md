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

Example: section 13 of `battle_pack.bin` holds 557 equipment records of 52 bytes. Record 1 is the Broadsword (word table 1, entry 1); u16 0x12 looks like the price and u8 0x1A is attack (confirmed in game). The game copies equipment stats into the character when the equipment is set and the save keeps that copy, so an attack change shows on a new game or after re-equipping, not on an old save as it is.

When the index is built, the recomp reads each patched or contested file from the archive and merges every mod's version against it. A version is a mod's whole copy (if it ships one) with its strings and `=` sets applied.

- Strings merge whole: a string that one mod changed is kept. If two mods change it to the same bytes the report says "identical"; if they differ, the higher-ranked mod wins and the report says "priority".
- Records merge byte by byte, so two mods that change different fields of one record both apply. Where they change the same bytes differently, the higher-ranked mod wins ("priority").
- `=?` lines then apply where the field still holds the original value; otherwise the report says the line was not applied.
- Relative operations (`+=`, `-=`, `*=`, `|=`, `&~=`) from every mod apply last, on top of the merged value, lowest-ranked mod first. Where several mods change one field this way, the report lists them in order ("operation"). Two mods that each multiply attack by 2 give 4 times the attack.
- A whole copy that adds or removes strings cannot be merged: the best-ranked such copy is used as it is, and every other mod's change to that file is listed as "needs-patch".

The report (`mods-report.txt`, `--mods-list`) has a `merge <path>` block for each merged file. A line naming a record that does not exist or a field past the record's end is skipped and reported. `--mods-list` exits 1 on a patch line it could not apply.

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

First results (Ryzen 9 9950X3D2, machine at 100% CPU from other work, 1,000,000 calls, median of 5): game function alone about 2 ns; C and C++ add 3-4 ns for the filter and table read and about 56-59 ns for the loop; Luau adds about 105-136 ns for the filter and table read (native code and interpreter about the same, so the boundary dominates) and 796 ns (native) or 1,190 ns (interpreter) for the loop. All 15 language-test pairs returned correct results.
