# Game text (event scripts, st2e tables, word tables)

Tool: `tools/ff12text.py` (standard library only). It turns the strings of one file into JSON and builds the file again from edited JSON.

```
python -I tools/ff12text.py dump <file> [-o out.json]
python -I tools/ff12text.py build <file> <in.json> -o <out>
python -I tools/ff12text.py verify <dir>
```

`dump` and `build` take EBP2 scripts, st2e tables, word tables and `battle_pack.bin`. `verify` rebuilds every such file under a folder and fails if a single byte differs.

## Where the text is

- Dialog and cutscene subtitles: `ps2data/plan_master/<lang>/event/<area>/<script>/<script>.ebp`. Of the 1,115 English (`us`) scripts, 829 have a text section, holding 17,327 strings in total.
- Menus, help, battle messages and names: st2e tables, word tables and `battle_pack.bin` in `ps2data/image/ff12/test_battle/us/binaryfile/` (below).
- Cutscenes also open `jsondata/cutscenes/<name>.json`. That file holds no subtitle text: a subtitle edited in the .ebp shows up in the cutscene.

## Character set

One byte per glyph. The table in `ff12text.py` comes from the refinery cutscene (`ene_a0200`). Its text section was rebuilt so that one subtitle showed every byte from 0x80 to 0xDF in order, and each glyph was read from the screen (run `win-text5`, 2026-11-22).

| Bytes | Glyphs |
|---|---|
| 0x02 | line break |
| 0x04 | space |
| 0x20-0x39 | A-Z |
| 0x3A-0x53 | a-z |
| 0x80-0x84 | ù ú û ü ß |
| 0x85-0x8E | 0-9 |
| 0x8F-0x9F | – « » (box) ¡ ¿ „ (box) ~ ` ! ? @ # $ % ^ |
| 0xA0-0xAF | & * / _ + - = , . ; : \ ' " ( ) |
| 0xB0-0xBF | [ ] < > { } \| (box x4) → (box x2) ² ³ |
| 0xC0-0xC6 | ⁴ ⁵ (box) ≤ ≥ … (box) |
| 0xC7-0xCF | ? ! / ( ) + = < > again (second set) |
| 0xD0-0xDF | % “ ” (box x6) × ∞ • (box x4) |

"(box)" is a byte the subtitle font has no glyph for. The other accented letters are probably at 0x54 and up (after z), but nobody has checked them.

In the JSON, a byte with no single-character mapping appears as `{XX}`. That covers 0xB4 and 0xB5 ("{" and "}", which are the escape characters) and the second set at 0xC7-0xCF. Control sequences appear as one group, for example `{0F 2F 81 80}`.

## Control sequences

- 0x0E plus 5 argument bytes.
- 0x0F, then a code byte, then argument bytes (0x80 and up). The argument count depends on the code; the tool uses the count most often seen in the English scripts (`_ARGS_0F`). Dialog lines usually start with `{0F 2F 81 80}{0F 21}{0F 27 81 80}`. `{0F 2E 80 80}` stands in for a number (for example a gil amount).
- 0x03 is probably a page break inside a dialog box (it appears between sentences).

The argument counts only change how bytes are grouped in the JSON. Encoding gives back the exact bytes either way.

## EBP2 layout

All integers are little-endian.

- 0x00: `EBP2`.
- 0x08-0x7F: u32 section offsets. The text section offset is at 0x18; 0 means the script has no text. Other slots seen in use: 0x30, 0x34, 0x40, 0x48. Sections start on 0x40 boundaries.
- Text section: a table of u32 entries (u16 offset from the section start, u16 attribute), one per string. The first entry's offset equals the table size, which gives the count. After the table come NUL-terminated strings. Entries may share an offset.

`build` writes the strings in the order of their old offsets and keeps shared offsets where the text is still equal. If the new text fits in the old section, the section keeps its old size. Otherwise the section is padded to the next 0x40 boundary, and every header slot that points past it is moved by the difference.

## st2e tables

Menu, help and battle message files in `ps2data/image/ff12/test_battle/us/binaryfile/` are st2e tables (27 files). The same header also holds data tables with no text (record arrays in `battle_pack.bin`).

- 0x00: `st2e`; 0x04 u32 count; 0x08 u32 record size (4 or 8); 0x0C u32 0x20 (header size); 0x10 u32 0; 0x14 u32 offset of the string section (0 in data tables).
- 0x20: count records of the record size (ids or attributes; copied as they are).
- String section: u32 1, u32 12 (offset of the table from the section), u32 size (section start to the end of the last string). The table is a u32 count, then count + 1 u32 offsets counted from the table start (the last is the end of the strings), then the strings. The file is zero-padded to a multiple of 4.
- A string is game bytes ending in 00, or `ex00` followed by UTF-16LE text ending in a 16-bit 0 (menu names such as "Directors"). The JSON writes the second form as `{ex00}` plus the plain characters.

Older notes called these "st2ef": in `battle_message.bin` the count is 0x66, which reads as "f".

## Word tables

`word.bin` and section 61 of `battle_pack.bin` hold item, ability and command names, such as "Magicks & Technicks". The layout is u32 n, then n + 1 u32 offsets of sub-tables (the last is the end). Each sub-table is a string table like the st2e one: count, count + 1 offsets from the sub-table start, entries. An entry is 00 00, the word, then more zero-separated fields (grammar data), padded with zeros so the next entry starts on 4 bytes. The JSON keeps each entry without its trailing zeros (inner zeros as `{00}`) and a `pad` count, so an unchanged entry rebuilds exactly. A longer entry is padded to 4 again.

Sub-tables of the English `word.bin` by their first entries (guessed from content): 0 magicks and statuses (545), 1 weapons, armor and items (544; English entries carry a plural suffix after {00}{09} and an article), 3 abilities (362), 4 battle commands (63: Attack, Items, Gambits, Magicks & Technicks), 5 (32), 6 gambit targets (284), 8 characters and enemies (629), 9 and 10 (128, 129), 11 key items (512), 12 loot and maps (512), 13 species (16), 14 bestiary (64). Section 57 of the pack is an st2e table of 128 "FFFF" placeholders.

## battle_pack.bin

u32 n (71), then n + 1 u32 section offsets. The header, every section and the file are padded to 8 bytes; the last offset is the end of the last section before that padding. Section 2 is a copy of `battle_message.bin`, section 57 is another st2e text table and section 61 is the word table. The other sections are data. The game opens `battle_pack.bin` and never `battle_message.bin` or `word.bin` (checked with `FFXII_MODS_LOG=1` through a battle), so battle text edits go into the pack. `build` rewrites the three text sections and moves the later sections.

## Verified

- `verify` over the 1,115 English scripts: 828 files with text, 17,327 strings, 0 failures. Every file rebuilt byte for byte.
- `verify` over the 29 English files of `test_battle/us/binaryfile` that hold text (27 st2e tables, `word.bin`, `battle_pack.bin`): 15,011 strings and entries, 0 failures.
- In game (Windows GPU build, headless, muted, Steam save on the refinery platform): a mod serving a rebuilt `ene_a0200.ebp` whose text section grew from 0x8840 to 0x8880 (later sections moved by 0x40). The edited subtitle showed, and the subtitles after it still played in order (`win-text4.log`). The character probe ran in `win-text5.log`.
- In game (Inner Ward save, `fight7.txt`): a rebuilt `battle_pack.bin` 72 bytes longer, with " (mod)" added to four command names in the word section and to five battle messages, showed "Attack (mod)", "Magicks & Technicks (mod)" and "Items (mod)" in the battle menu through the corridor fights and the boss fight, with no fatal (`win-st2c.log`, 99% machine load). The edited messages were not caught on a dumped frame.

## Runtime merging (C++)

`src/text_formats.cpp` ports the layout code of `ff12text.py` to C++ on raw bytes: `Units` splits a file into keyed strings and entries, and `RebuildRaw` lays the file out again. Text patches and the per-string merge are in `src/text_merge.cpp`, and `src/text_mods.cpp` takes the originals from the archive through `src/vbf_reader.cpp` (zlib). Usage is in `modding-quickstart.md`, "Text patches and merging". `text_formats_test` checks the character table, synthetic st2e files and the merge rules. With extracted files as arguments it also rebuilds each one unchanged and checks that replacing its first and last unit changes only those. Checks on 2026-11-22:

- All 857 text files of the English set (828 scripts, 29 tables, 1,244 files given) rebuilt byte for byte. Two-unit edits changed only those units.
- 400 random edits of 1 to 6 units, including longer text, line breaks and `{ex00}` strings: the C++ output matched `ff12text.py build` byte for byte every time.
- `text_formats_test --mods <dir> <vbf>` on the real archive with two mods: a whole `battle_pack.bin` copy (priority 5) and a patch (priority 10). It reported 11 changed units, one identical and one priority overlap.

## Not done

- Edits that change the number of strings or entries.
- The `in/` copies of a few tables (`menu01.bin`, `board_*.bin`) that the game also opens.
- Other languages: their folders (`in`, `fr` and the rest) have not been checked against this table.
- Meaning of the attribute half of each EBP2 entry, of 0x03, and of most 0x0F codes.
