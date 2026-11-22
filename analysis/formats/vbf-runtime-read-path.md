# VBF runtime read path and loose-file override

Read from the lifted code and the 1.0.4.0 exe on 2026-11-20 (image base 0x120000, addresses absolute). Only the `fopen(../FFXII_TZA.vbf, rb)` call was checked at runtime. Everything else needs a runtime dump before code depends on it.

## Call chain

| Address | Role |
|---|---|
| `17CEB0` | Opens `../FFXII_TZA.vbf`. Creates the manager singleton at `[1F81110]` (`19B190`, init `19AE70`), then `19B070(mgr, path)` makes an archive (ctor `1F4060`, 0xB0 bytes, up to 5 per manager). |
| `1F4340` | `Archive::Open`. stdio `fopen`/`fread`/`fseek`; checks magic `SRYK` ("BigFile head wrong!"), reads the header into `archive+0x00`, builds a `std::map` md5 -> entry (`1F4720`), and keeps the u16 block table (`archive+0x68`) and a compressed-flag byte array (`archive+0x70`). Also maps the file with CreateFileMapping/MapViewOfFile. |
| `59EE90` | `VfsOpen(File*, path, flag)`, the single open path for game files (callers include the File ctor `597930` via `1D6F20`). Strips leading `.`/`/`/`\`, converts `\` to `/`, lowercases, then calls Find. Stores the entry at `f+0x10` and the size at `f+0x00`. |
| `19AEE0` | Find: MD5 of the normalized path (CryptoAPI, no NUL), then searches the map of each of up to 5 archives (`1F47D0`). Called only from `59EE90`. |
| `1F4250`, `2150B0` | Acquire a pooled reader (15 per archive at `archive+0xA8`) and bind it to an entry. |
| `215110` | `Reader::Read(reader, dst, len)`. Clamps to the size, serves from a one-block cache, otherwise `19AFE0` -> `19A860` (fseek/fread of the stored block) -> `19AA20` -> `80C5A0` (statically linked zlib `uncompress`). Raw blocks are copied. |
| `215220`, `215280` | Seek and size. |

Reader object (0x30 bytes): +0 in-use byte, +8 own `FILE*`, +0x10 archive, +0x18 entry, +0x20 position, +0x28 one-block cache {buf, end, start}.

Entry record in the header buffer (0x20 bytes): +0x00 u32 first block index, +0x08 u64 uncompressed size, +0x10 u64 data offset, +0x18 u64 relocated by Open (role unknown).

`59EE90` also has a loose-file branch (`1D6D20` -> `20C1B0`) gated on `1D6B20()` and `865390(path)`. Logs `[FFXII_data]` / `[FFXII_data_notcache]`. Whether retail ever takes it is unconfirmed.

## Loose-file override (built 2026-11-21)

`src/mods.cpp`, wired by three `replace` lines in `analysis/maps/lifter.manifest`:

| Hook | Behavior |
|---|---|
| `19AEE0` Find(mgr, path) | Find returns a reader, not an entry: `1F47D0` looks the entry up and binds a pooled reader (`1F4250`). The path is `rdx + [mgr+0x70]`. If `<mods>/<path>` exists, the hook returns a synthetic reader from a 1 MiB guest slab (reader 0x30, entry 0x20 with +8 = size, empty block cache). Otherwise it calls the lifted Find. |
| `215110` Read(reader, dst, len) | Synthetic reader: reads up to `len` bytes from the loose file at `[reader+0x20]` and returns the count. It does not move the position: the caller `2150E0` adds it. |
| `2150D0` Release(reader) | Synthetic reader: closes the file and frees the slot. Otherwise it jumps to `1F4840(archive, reader)` as before. |

Seek (`215220`) and size (`215280`) are not hooked. They only touch `reader+0x20` and `[reader+0x18]+8`, which the synthetic reader fills in.

- Mods (since 2026-11-21, `src/mod_index.cpp`): the root is `FFXII_MODS_DIR`, else `<exe dir>/mods`, else `<game dir>/mods`. Each folder with a `mod.ini` is a mod and serves the files under its `files/` folder; anything else under the root is the legacy loose tree (implicit mod `loose`, priority 0). `mod.ini` takes `id`, `name`, `version`, `priority` (integer, higher wins) and `enabled`; on equal priority the smaller id wins. Paths match case-insensitively. `FFXII_MODS_DISABLE=id1,id2` turns mods off without editing them. At startup the index is built once and its report (mods, files served, every conflict, manifest errors) goes to the log and to `<root>/mods-report.txt`. Example:

  ```
  mods/
    hd-logo/mod.ini        id = hd-logo / priority = 10
    hd-logo/files/gamedata/titleimage/us/sqex logo size_ffx.tga
  ```
- Before the index, the folder was only `<game dir>/mods` or `FFXII_MODS_DIR`, and every open tried `fopen` on the loose path. `FFXII_MODS_LOG=1` logs every path the game opens: 1,342 in the first 60 s of boot, mostly `jsondata/maps`, `jsondata/cutscenes` and `gamedata/d3d11/artdata/...`.
- A loose file can also add a path the archive lacks. The game tries `gamedata/titleimage/us/sqex logo size_ffx.tga` before the `in/` copy, so a file at the `us/` path takes over the boot logo.
- Verified 2026-11-21 (log `lead-0016-mods5`): a magenta-banded logo at that path showed on frames 240 to 270, and boot went on to the title screen.

## Open questions

- Gate conditions of the loose branch in `59EE90`.
- Role of entry +0x18 and the exact end of the header.
- Confirm the entry layout and size semantics with a runtime dump.
