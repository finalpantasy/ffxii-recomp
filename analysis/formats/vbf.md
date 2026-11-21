# VBF archive (FFXII_TZA.vbf)

Reader and extractor: `tools/vbf.py` (class `VbfArchive`). All integers little-endian.
Checked against the retail Steam archive (31,196,932,183 bytes).

## Structure

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | magic `SRYK` |
| 0x04 | u32 | header_len = offset of first data block (11,995,716 here); block table ends exactly here |
| 0x08 | u64 | file_count (89,414) |
| 0x10 | 16*N | MD5 of the virtual path (lowercase, no leading slash), same order as the records |
| 0x10+16N | 32*N | file records (below) |
| next | u32 | name table size; includes these 4 bytes |
| next | size-4 | NUL-terminated UTF-8 paths, `/` separators |
| next | u16 * blocks | block-size table (957,553 entries); ends at header_len |
| header_len | ... | file data, 64 KiB blocks |

File record (32 bytes): `u32 block_index, u32 block_index (duplicate, always equal), u64 size (uncompressed), u64 offset (absolute, first block), u64 name_offset (into name table)`.

Blocks: a file has ceil(size / 65536) blocks, starting at block_sizes[block_index]; the stored blocks are contiguous from `offset`. Table value v: `0` = raw full 64 KiB block; v equal to the block's expected raw size (short last block) = raw; otherwise v bytes of a zlib stream (with zlib header) inflating to the expected size. 189,861 of 957,553 table entries are 0.

## How verified on the real archive

- N=89,414; MD5(path) matched the stored hash for all 89,414 entries (so the name table and record order are right).
- Block table length computed from the first data offset equals the sum of per-file block counts (957,553); the last file's data ends exactly at EOF.
- Decompressed and size-checked: all of `jsondata` (832 files), all of `prefetchdata` (9,351 files, 270 MiB), `gamedata/titleimage/in/*`. 0 failures.
- Extracted samples: `prefetchdata/add-on/events/250.txt` (1,767 bytes, text), a `.dds.phyre` (352,101 bytes, magic `RYHPT` (Phyre engine asset)), a 5.9 MiB TGA.
- Not done: full-archive decompression (29 GB read). Run `vbf.py verify VBF "*" --all` when no other heavy job is running.

Counts: gamedata 39,653 files (35.5 GiB), ps2data 39,578 (19.1 GiB), prefetchdata 9,351, jsondata 832. Uncompressed total 54.85 GiB. Top extensions: phyre, txt, ebp, sab, mot, efx, ca, dat, ah, snd.

## Open questions

- Second u32 in the record: always equal to the first here; purpose unknown (maybe a second table index in other builds).
- 26 files have size 0 and a junk offset (0x6140000000000000-style), block_index still set; the reader treats them as empty. Meaning of the junk offset unknown.
- Role of `FileSizeTables/*.fst` (not examined) and whether the game checks the MD5 table or only uses it for lookup.
- Whether any entry uses a different compression than zlib (none seen in the verified subset).
