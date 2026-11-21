#!/usr/bin/env python3
"""FFXII The Zodiac Age VBF ("Virtual Big File", magic 'SRYK') reader.

Layout as verified against FFXII_TZA.vbf (all integers little-endian):

  0x00  char[4]  magic 'SRYK'
  0x04  u32      header_len  (= offset of the first data block; the block
                 table ends exactly here)
  0x08  u64      file_count
  0x10  file_count * 16 bytes   MD5 of the lowercase virtual path (no
                                leading slash), in file-record order
  ....  file_count * 32 bytes   file records:
          u32 block_index   first entry in the block-size table
          u32 block_index2  always equal to block_index in this archive
          u64 size          uncompressed size
          u64 offset        absolute offset of the first block in the VBF
          u64 name_offset   offset into the name table (after its u32 size)
  ....  u32 name_table_size   counts its own 4 bytes
        NUL-terminated UTF-8 paths, forward slashes
  ....  u16 block_sizes[]    one per 64 KiB block; blocks of a file are
                             contiguous from `offset`

A file has ceil(size / 65536) blocks. Block i holds uncompressed bytes
[i*65536, min((i+1)*65536, size)). Block-table value v:
  v == 0                  stored raw, a full 65536 bytes
  v == expected raw size  stored raw (only possible for the short last block
                          or when v would equal 65536 -> written as 0)
  otherwise               v bytes of zlib stream (zlib header, inflates to
                          the expected raw size)
Zero-size files have zero blocks.

Read-only. Never writes to the archive.
"""
import argparse
import fnmatch
import hashlib
import json
import os
import struct
import sys
import zlib

BLOCK = 0x10000
MAGIC = b"SRYK"


class VbfError(Exception):
    pass


class VbfEntry:
    __slots__ = ("path", "block_index", "size", "offset", "name_offset", "md5", "nblocks")

    def __init__(self, path, block_index, size, offset, name_offset, md5):
        self.path = path
        self.block_index = block_index
        self.size = size
        self.offset = offset
        self.name_offset = name_offset
        self.md5 = md5
        self.nblocks = (size + BLOCK - 1) // BLOCK

    def as_dict(self):
        return {"path": self.path, "size": self.size, "blocks": self.nblocks,
                "offset": self.offset, "block_index": self.block_index}


class VbfArchive:
    """Read-only VBF reader. Use as a context manager or call close()."""

    def __init__(self, path):
        self.path = path
        self._f = open(path, "rb")  # read-only
        self.file_size = os.fstat(self._f.fileno()).st_size
        self._parse()

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()

    def close(self):
        self._f.close()

    def _parse(self):
        f = self._f
        head = f.read(16)
        if len(head) < 16 or head[:4] != MAGIC:
            raise VbfError("not a VBF (bad magic)")
        self.header_len, self.file_count = struct.unpack("<IQ", head[4:])
        n = self.file_count
        hashes = f.read(16 * n)
        recs = f.read(32 * n)
        (nt_size,) = struct.unpack("<I", f.read(4))
        names = f.read(nt_size - 4)
        if len(hashes) != 16 * n or len(recs) != 32 * n or len(names) != nt_size - 4:
            raise VbfError("truncated header")
        self.block_table_pos = f.tell()
        raw = list(struct.iter_unpack("<IIQQQ", recs))
        # Block table length: up to the first data offset.
        first_data = min((r[3] for r in raw if r[2] > 0), default=self.block_table_pos)
        nblk = (first_data - self.block_table_pos) // 2
        self.block_sizes = struct.unpack("<%dH" % nblk, f.read(nblk * 2))
        self.data_start = first_data
        self.entries = []
        for i, (bi, bi2, size, off, noff) in enumerate(raw):
            end = names.index(b"\0", noff)
            e = VbfEntry(names[noff:end].decode("utf-8"), bi, size, off, noff,
                         hashes[i * 16:(i + 1) * 16])
            self.entries.append(e)
        self._by_path = None

    def get(self, path):
        if self._by_path is None:
            self._by_path = {e.path: e for e in self.entries}
        return self._by_path[path]

    def match(self, pattern):
        """Entries whose virtual path matches a glob (case-insensitive;
        '*' also crosses '/'). A pattern with no wildcard matches a path
        exactly or as a folder prefix."""
        p = pattern.replace("\\", "/").lstrip("/").lower()
        if not any(c in p for c in "*?["):
            pre = p.rstrip("/") + "/"
            return [e for e in self.entries if e.path.lower() == p or e.path.lower().startswith(pre)]
        return [e for e in self.entries if fnmatch.fnmatchcase(e.path.lower(), p)]

    def iter_blocks(self, entry):
        """Yield the raw (decompressed) bytes of each block of an entry."""
        f = self._f
        f.seek(entry.offset)
        remaining = entry.size
        for i in range(entry.nblocks):
            want = min(BLOCK, remaining)
            v = self.block_sizes[entry.block_index + i]
            stored = BLOCK if v == 0 else v
            data = f.read(stored)
            if len(data) != stored:
                raise VbfError("%s: short read in block %d" % (entry.path, i))
            if v == 0 or stored == want:
                if stored != want:
                    raise VbfError("%s: raw block %d size %d != %d" % (entry.path, i, stored, want))
            else:
                try:
                    data = zlib.decompress(data)
                except zlib.error as ex:
                    raise VbfError("%s: block %d inflate failed: %s" % (entry.path, i, ex))
                if len(data) != want:
                    raise VbfError("%s: block %d inflated to %d, expected %d" % (entry.path, i, len(data), want))
            remaining -= want
            yield data

    def read(self, entry):
        return b"".join(self.iter_blocks(entry))

    def extract_to(self, entry, dest):
        total = 0
        os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
        with open(dest, "wb") as out:
            for b in self.iter_blocks(entry):
                out.write(b)
                total += len(b)
        if total != entry.size:
            raise VbfError("%s: wrote %d, expected %d" % (entry.path, total, entry.size))
        return total

    def verify_entry(self, entry):
        total = sum(len(b) for b in self.iter_blocks(entry))
        if total != entry.size:
            raise VbfError("%s: got %d bytes, expected %d" % (entry.path, total, entry.size))
        return total

    def check_hashes(self):
        """Count entries whose stored MD5 matches md5(path)."""
        return sum(hashlib.md5(e.path.encode()).digest() == e.md5 for e in self.entries)


def _fmt(n):
    for u in ("B", "KiB", "MiB", "GiB", "TiB"):
        if n < 1024 or u == "TiB":
            return "%d %s" % (n, u) if u == "B" else "%.2f %s" % (n, u)
        n /= 1024


def cmd_list(a):
    with VbfArchive(a.vbf) as v:
        es = v.entries
        if a.grep:
            g = a.grep.lower()
            es = [e for e in es if g in e.path.lower()]
        if a.json:
            json.dump([e.as_dict() for e in es], sys.stdout, indent=1)
            print()
        else:
            for e in es:
                print("%12d %6d  %s" % (e.size, e.nblocks, e.path))


def cmd_info(a):
    with VbfArchive(a.vbf) as v:
        tot = sum(e.size for e in v.entries)
        print("magic            SRYK")
        print("header_len       %d" % v.header_len)
        print("file_count       %d" % v.file_count)
        print("block table      %d entries at 0x%x" % (len(v.block_sizes), v.block_table_pos))
        print("data starts at   %d" % v.data_start)
        print("archive size     %s (%d)" % (_fmt(v.file_size), v.file_size))
        print("uncompressed sum %s (%d)" % (_fmt(tot), tot))
        print("md5 name hashes  %d / %d match" % (v.check_hashes(), v.file_count))
        folders = {}
        for e in v.entries:
            top = e.path.split("/", 1)[0] if "/" in e.path else "(root)"
            c, s = folders.get(top, (0, 0))
            folders[top] = (c + 1, s + e.size)
        print("top-level folders:")
        for k, (c, s) in sorted(folders.items(), key=lambda kv: -kv[1][1]):
            print("  %-28s %8d files %12s" % (k, c, _fmt(s)))


def _select(v, a):
    es = v.match(a.pattern)
    tot = sum(e.size for e in es)
    if not a.all and (len(es) > a.max_files or tot > a.max_bytes):
        sys.exit("refusing: %d files, %s (limits %d files, %s). Use --all or raise --max-files/--max-bytes."
                 % (len(es), _fmt(tot), a.max_files, _fmt(a.max_bytes)))
    return es, tot


def cmd_extract(a):
    out = os.path.abspath(a.out_dir)
    with VbfArchive(a.vbf) as v:
        es, tot = _select(v, a)
        for e in es:
            dest = os.path.abspath(os.path.join(out, *e.path.split("/")))
            if os.path.commonpath([out, dest]) != out:
                sys.exit("unsafe path in archive: " + e.path)
            v.extract_to(e, dest)
            print("%10d  %s" % (e.size, e.path))
        print("extracted %d files, %s" % (len(es), _fmt(tot)))


def cmd_verify(a):
    bad = 0
    with VbfArchive(a.vbf) as v:
        es, tot = _select(v, a)
        for e in es:
            try:
                v.verify_entry(e)
            except VbfError as ex:
                bad += 1
                print("FAIL", ex)
        print("verified %d files, %s, %d failures" % (len(es), _fmt(tot), bad))
    sys.exit(1 if bad else 0)


def cmd_mods(a):
    """Check a loose-file mods folder against the archive (src/mods.cpp).
    The game opens <mods>/<lowercase path>, so a name with uppercase
    letters never loads on a case-sensitive file system."""
    root = os.path.abspath(a.mods_dir)
    if not os.path.isdir(root):
        sys.exit("not a folder: " + root)
    counts = {"override": 0, "new": 0, "same": 0, "case": 0}
    with VbfArchive(a.vbf) as v:
        by_lower = {e.path.lower(): e for e in v.entries}
        for dirpath, _, files in os.walk(root):
            for fn in sorted(files):
                full = os.path.join(dirpath, fn)
                rel = os.path.relpath(full, root).replace(os.sep, "/")
                size = os.path.getsize(full)
                e = by_lower.get(rel.lower())
                if rel != rel.lower():
                    counts["case"] += 1
                    print("CASE      %s  (rename to lowercase: the game asks for %s)" % (rel, rel.lower()))
                    continue
                if e is None:
                    counts["new"] += 1
                    print("NEW       %s  %d bytes (no archive entry)" % (rel, size))
                    continue
                if size == e.size:
                    with open(full, "rb") as f:
                        same = all(f.read(len(b)) == b for b in v.iter_blocks(e))
                    if same:
                        counts["same"] += 1
                        print("SAME      %s  (identical to the archive; delete it)" % rel)
                        continue
                counts["override"] += 1
                print("OVERRIDE  %s  %d -> %d bytes" % (rel, e.size, size))
    print("%(override)d override, %(new)d new, %(same)d identical, %(case)d wrong case" % counts)
    sys.exit(1 if counts["case"] else 0)


def cmd_opened(a):
    """List the files a run opened, from a log made with FFXII_MODS_LOG=1
    (lines '[mods] open <path>'), in first-open order with open counts."""
    seen = {}
    with open(a.log, encoding="utf-8", errors="replace") as f:
        for line in f:
            i = line.find("[mods] open ")
            if i >= 0:
                p = line[i + 12:].rstrip("\r\n")
                seen[p] = seen.get(p, 0) + 1
    if a.grep:
        g = a.grep.lower()
        seen = {p: n for p, n in seen.items() if g in p}
    with VbfArchive(a.vbf) as v:
        by_lower = {e.path.lower(): e for e in v.entries}
        missing = 0
        for p, n in seen.items():
            e = by_lower.get(p)
            missing += e is None
            print("%12s %4dx  %s" % (e.size if e else "missing", n, p))
    print("%d paths, %d not in the archive" % (len(seen), missing))


def main():
    ap = argparse.ArgumentParser(description="FFXII TZA VBF archive tool (read-only)")
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("opened", help="list files a run opened (log from FFXII_MODS_LOG=1)")
    p.add_argument("vbf"); p.add_argument("log"); p.add_argument("--grep"); p.set_defaults(fn=cmd_opened)
    p = sp.add_parser("mods", help="check a loose-file mods folder against the archive")
    p.add_argument("vbf"); p.add_argument("mods_dir"); p.set_defaults(fn=cmd_mods)
    p = sp.add_parser("list"); p.add_argument("vbf"); p.add_argument("--grep"); p.add_argument("--json", action="store_true")
    p.set_defaults(fn=cmd_list)
    p = sp.add_parser("info"); p.add_argument("vbf"); p.set_defaults(fn=cmd_info)
    for name, fn in (("extract", cmd_extract), ("verify", cmd_verify)):
        p = sp.add_parser(name); p.add_argument("vbf"); p.add_argument("pattern")
        if name == "extract":
            p.add_argument("out_dir")
        p.add_argument("--all", action="store_true", help="lift the file/byte limits")
        p.add_argument("--max-files", type=int, default=500)
        p.add_argument("--max-bytes", type=int, default=512 * 1024 * 1024)
        p.set_defaults(fn=fn)
    a = ap.parse_args()
    try:
        a.fn(a)
    except VbfError as ex:
        sys.exit("error: %s" % ex)


if __name__ == "__main__":
    main()
