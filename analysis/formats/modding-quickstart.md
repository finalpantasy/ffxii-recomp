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

Example: section 13 of `battle_pack.bin` holds 557 records of 52 bytes, probably weapons and equipment. Record 1 lines up with the Broadsword (word table 1, entry 1); u16 0x12 looks like the price and u8 0x1A like attack, but a run that multiplied 0x1A showed no change in damage, so treat these offsets as unconfirmed.

When the index is built, the recomp reads each patched or contested file from the archive and merges every mod's version against it. A version is a mod's whole copy (if it ships one) with its strings and `=` sets applied.

- Strings merge whole: a string that one mod changed is kept. If two mods change it to the same bytes the report says "identical"; if they differ, the higher-ranked mod wins and the report says "priority".
- Records merge byte by byte, so two mods that change different fields of one record both apply. Where they change the same bytes differently, the higher-ranked mod wins ("priority").
- `=?` lines then apply where the field still holds the original value; otherwise the report says the line was not applied.
- Relative operations (`+=`, `-=`, `*=`, `|=`, `&~=`) from every mod apply last, on top of the merged value, lowest-ranked mod first. Where several mods change one field this way, the report lists them in order ("operation"). Two mods that each multiply attack by 2 give 4 times the attack.
- A whole copy that adds or removes strings cannot be merged: the best-ranked such copy is used as it is, and every other mod's change to that file is listed as "needs-patch".

The report (`mods-report.txt`, `--mods-list`) has a `merge <path>` block for each merged file. A line naming a record that does not exist or a field past the record's end is skipped and reported. `--mods-list` exits 1 on a patch line it could not apply.
