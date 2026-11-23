# Game text (EBP2 event scripts and st2ef tables)

Tool: `tools/ff12text.py` (standard library only). It turns the strings of one file into JSON and builds the file again from edited JSON.

```
python -I tools/ff12text.py dump <file> [-o out.json]
python -I tools/ff12text.py build <file.ebp> <in.json> -o <out.ebp>
python -I tools/ff12text.py verify <dir>
```

`build` handles EBP2 only. `verify` decodes, encodes and rebuilds every EBP2 and st2ef file under a folder and fails if a single byte differs.

## Where the text is

- Dialog and cutscene subtitles: `ps2data/plan_master/<lang>/event/<area>/<script>/<script>.ebp`. Of the 1,115 English (`us`) scripts, 829 have a text section, holding 17,327 strings in total.
- Battle, menu and help messages: st2ef tables, each starting with `st2ef\0\0\0`.
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

## st2ef layout

- 0x00: `st2ef\0\0\0`; 0x08 u32 8; 0x0C u32 0x20 (header size); 0x10 u32 0; 0x14 u32 section offset.
- 0x20: one 8-byte record per string (contents not decoded).
- Section +4: u32 offset of the string table, counted from the section start. The table is a u32 count, then that many u32 offsets counted from the table start, then NUL-terminated strings.

`dump` reads st2ef. `build` does not write it yet.

## Verified

- `verify` over the 1,115 English scripts: 828 files with text, 17,327 strings, 0 failures. Every file rebuilt byte for byte.
- In game (Windows GPU build, headless, muted, Steam save on the refinery platform): a mod serving a rebuilt `ene_a0200.ebp` whose text section grew from 0x8840 to 0x8880 (later sections moved by 0x40). The edited subtitle showed, and the subtitles after it still played in order (`win-text4.log`). The character probe ran in `win-text5.log`.

## Not done

- Writing st2ef tables, and edits that change the string count.
- Other languages: their folders (`in`, `fr` and the rest) have not been checked against this table.
- Meaning of the attribute half of each EBP2 entry, of 0x03, and of most 0x0F codes.
