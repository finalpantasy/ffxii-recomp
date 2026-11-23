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

## Text patches and merging

A whole edited copy of a text file blocks every other mod that edits the same file. A text patch changes only the strings it names: `<mod>/text/<archive path>.txt`, one `key = text` line per string.

```
# <mod>/text/ps2data/image/ff12/test_battle/us/binaryfile/battle_pack.bin.txt
p 61 w 4 1 = {00}{00}Magicks (patch)
p 61 w 4 3 = {00}{00}Items (patch)
```

- Keys: `s <i>` is string i of an event script or st2e table, `w <t> <e>` is entry e of word sub-table t, and in a pack (`battle_pack.bin`) `p <k>` comes first and names the section. `ff12text.py dump` lists the strings in the same order: the key index is the position in the JSON list.
- Text uses the `ff12text.py` form: `{XX}` for a raw byte, `\n` for a line break and `\\` for a backslash. Word entries start with `{00}{00}`, as in the dump.
- Lines starting with `#` are comments. One space after `=` is dropped; any further spaces are kept.

When the index is built, the recomp reads each patched or contested text file from the archive and merges every mod's version against it string by string. A version is a mod's whole copy (if it ships one) with its patch applied. Strings that only one mod changed are all kept. If two mods change the same string to the same bytes, the change is kept once and the report says "identical". If they change it differently, the higher-ranked mod wins and the report says "priority". A whole copy that adds or removes strings cannot be merged: the best-ranked such copy is used as it is, and every other mod's change to that file is listed as "needs-patch". The report (`mods-report.txt`, `--mods-list`) has a `text <path>` block for each merged file. `--mods-list` exits 1 on a patch line it could not apply.
