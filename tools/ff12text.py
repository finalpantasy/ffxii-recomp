#!/usr/bin/env python3
"""FFXII TZA game text: decode, dump and rebuild (analysis/formats/text.md).

Text in event scripts (EBP2, `*.ebp`: dialog) and in st2ef string tables
(battle, menu and help messages) uses the game's own one-byte character set.
This tool turns it into editable JSON and back.

  ff12text.py dump FILE [-o OUT.json]   strings of an .ebp or st2ef file
  ff12text.py build FILE IN.json -o OUT write FILE with the JSON's strings
  ff12text.py verify DIR                decode, re-encode and rebuild every
                                        .ebp and st2ef file under DIR; the
                                        output must equal the input bytes

Text form: letters, digits, space and the mapped punctuation as themselves;
byte 0x02 as a line break ("\\n"); control sequences (0x0E or 0x0F, then the
bytes up to the next one below 0x80 after the code) as {0F 2F 81 80}; any
other byte as {XX}. Decoding then encoding gives back the same bytes for
every string, whether or not the character table is complete.

Standard library only; reads and writes local files, never the archive.
"""

import argparse
import json
import os
import struct
import sys

# Bytes whose glyph is known. 0x80-0xDF were read off the screen: a cutscene
# subtitle (ene_a0200) rebuilt to show each byte (analysis/formats/text.md).
# Bytes that repeat a glyph (0xC7-0xCF, 0xD0) and "{" "}" (0xB4, 0xB5, the
# escape characters here) stay as {XX}, so every character has one byte.
_CHARS = {0x04: " ", 0x02: "\n"}
for _i in range(26):
    _CHARS[0x20 + _i] = chr(0x41 + _i)  # A-Z
    _CHARS[0x3A + _i] = chr(0x61 + _i)  # a-z
for _i in range(10):
    _CHARS[0x85 + _i] = chr(0x30 + _i)  # 0-9
for _b, _s in zip(range(0x80, 0x85), "ùúûüß"):
    _CHARS[_b] = _s
for _b, _s in zip(range(0x8F, 0xB4), "–«»\x00¡¿„\x00~`!?@#$%^&*/_+-=,.;:\\'\"()[]<>"):
    if _s != "\x00":
        _CHARS[_b] = _s
_CHARS.update({0xB6: "|", 0xBB: "→", 0xBE: "²", 0xBF: "³", 0xC0: "⁴", 0xC1: "⁵", 0xC3: "≤",
               0xC4: "≥", 0xC5: "…", 0xD1: "“", 0xD2: "”", 0xD9: "×", 0xDA: "∞", 0xDB: "•"})
_BYTES = {v: k for k, v in _CHARS.items()}
_CONTROL = (0x0E, 0x0F)
# Argument bytes (0x80 and up) after a control: 0x0E takes 5; 0x0F codes
# take the count most common in the English scripts (0x29 and 0x2E vary).
# Unlisted codes take every following byte from 0x80. Only the display
# grouping depends on this; bytes always round-trip.
_ARGS_0E = 5
_ARGS_0F = {0x20: 2, 0x21: 0, 0x26: 3, 0x27: 2, 0x28: 2, 0x29: 2, 0x2D: 2, 0x2E: 3, 0x2F: 2, 0x37: 2,
            0x38: 3, 0x3B: 2, 0x3C: 2, 0x3D: 2, 0x3E: 2, 0x40: 1, 0x41: 1, 0x44: 1, 0x48: 1, 0x49: 1,
            0x4A: 1, 0x4B: 1, 0x4D: 1, 0x60: 1}


def decode(b):
    """Game bytes (no terminator) to text."""
    out = []
    i = 0
    while i < len(b):
        x = b[i]
        if x in _CONTROL:
            j = i + 1
            limit = _ARGS_0E
            if x == 0x0F and j < len(b):
                limit = _ARGS_0F.get(b[j], len(b))
                j += 1  # the code byte
            while j < len(b) and b[j] >= 0x80 and limit > 0:
                j += 1
                limit -= 1
            out.append("{" + " ".join("%02X" % y for y in b[i:j]) + "}")
            i = j
            continue
        c = _CHARS.get(x)
        out.append(c if c is not None else "{%02X}" % x)
        i += 1
    return "".join(out)


def encode(s):
    """Text to game bytes (no terminator)."""
    out = bytearray()
    i = 0
    while i < len(s):
        c = s[i]
        if c == "{":
            j = s.index("}", i)
            out += bytes(int(h, 16) for h in s[i + 1:j].split())
            i = j + 1
            continue
        if c not in _BYTES:
            raise ValueError("no game byte for %r at %d in %r" % (c, i, s))
        out.append(_BYTES[c])
        i += 1
    if 0 in out:
        raise ValueError("string holds a 00 byte: %r" % s)
    return bytes(out)


def _cstr(d, at):
    return d[at:d.index(b"\0", at)]


# --- EBP2 event scripts ---------------------------------------------------
# Header: "EBP2", then u32 section offsets up to the code at 0x80 (text at
# 0x18). Sections are 0x40-aligned. The text section is a table of u32
# entries (u16 offset from the section start, u16 attribute), the first
# offset being the table size, then NUL-terminated strings.

EBP_TEXT_SLOT = 0x18
EBP_HEADER_END = 0x80


def ebp_read(d):
    if d[:4] != b"EBP2":
        raise ValueError("not an EBP2 file")
    t = struct.unpack_from("<I", d, EBP_TEXT_SLOT)[0]
    if t == 0:
        return t, []
    n = struct.unpack_from("<H", d, t)[0] // 4
    entries = []
    for i in range(n):
        off, attr = struct.unpack_from("<HH", d, t + 4 * i)
        entries.append({"attr": attr, "offset": off, "text": decode(_cstr(d, t + off))})
    return t, entries


def _section_end(d, t):
    """End of the text section: the next section offset above t, else EOF."""
    nxt = [v for v in struct.unpack_from("<%dI" % ((EBP_HEADER_END - 8) // 4), d, 8) if v > t]
    return min(nxt) if nxt else len(d)


def ebp_build(d, strings):
    """d with its text section replaced by `strings` (list of {attr, text})."""
    t, old = ebp_read(d)
    if t == 0:
        raise ValueError("file has no text section")
    if len(strings) != len(old):
        raise ValueError("string count %d differs from the file's %d" % (len(strings), len(old)))
    # Lay the strings out in the order of their old offsets; entries that
    # shared an offset and still hold the same text keep sharing.
    table = 4 * len(strings)
    body = bytearray()
    placed = {}  # (old offset, text) -> new offset
    offs = [0] * len(strings)
    for i in sorted(range(len(strings)), key=lambda k: (old[k]["offset"], k)):
        key = (old[i]["offset"], strings[i]["text"])
        if key not in placed:
            placed[key] = table + len(body)
            body += encode(strings[i]["text"]) + b"\0"
        offs[i] = placed[key]
    if table + len(body) > 0xFFFF:
        raise ValueError("text section over 64 KiB")
    sec = bytearray()
    for i, s in enumerate(strings):
        sec += struct.pack("<HH", offs[i], int(s["attr"]))
    sec += body
    old_end = _section_end(d, t)
    # Keep the old section's padding when the text still fits; otherwise
    # pad to the 0x40 section alignment.
    old_size = old_end - t
    size = old_size if len(sec) <= old_size else (len(sec) + 0x3F) & ~0x3F
    sec += b"\0" * (size - len(sec))
    delta = size - old_size
    out = bytearray(d[:t]) + sec + d[old_end:]
    for slot in range(8, EBP_HEADER_END, 4):
        v = struct.unpack_from("<I", out, slot)[0]
        if v > t:
            struct.pack_into("<I", out, slot, v + delta)
    return bytes(out)


# --- st2ef string tables -----------------------------------------------------
# "st2ef\0\0\0", u32 8, u32 0x20 (header size), u32 0, u32 section; then one
# 8-byte record per string; the section: u32 1, u32 table (from the
# section), u32 size, and at the table a u32 count and count u32 offsets
# from the table start, then NUL-terminated strings.


def st2_read(d):
    if d[:8] != b"st2ef\0\0\0":
        raise ValueError("not an st2ef file")
    sec = struct.unpack_from("<I", d, 0x14)[0]
    tab = sec + struct.unpack_from("<I", d, sec + 4)[0]
    n = struct.unpack_from("<I", d, tab)[0]
    out = []
    for i in range(n):
        off = struct.unpack_from("<I", d, tab + 4 + 4 * i)[0]
        out.append({"offset": off, "text": decode(_cstr(d, tab + off))})
    return out


# --- commands ---------------------------------------------------------------


def _kind(d):
    if d[:4] == b"EBP2":
        return "ebp"
    if d[:8] == b"st2ef\0\0\0":
        return "st2ef"
    return None


def cmd_dump(a):
    d = open(a.file, "rb").read()
    k = _kind(d)
    if k == "ebp":
        _, es = ebp_read(d)
    elif k == "st2ef":
        es = st2_read(d)
    else:
        sys.exit("%s: not an EBP2 or st2ef file" % a.file)
    doc = {"format": "ff12text-1", "kind": k, "source": os.path.basename(a.file),
           "strings": [{kk: v for kk, v in e.items() if kk != "offset"} for e in es]}
    text = json.dumps(doc, ensure_ascii=False, indent=1) + "\n"
    if a.o:
        open(a.o, "w", encoding="utf-8", newline="\n").write(text)
    else:
        sys.stdout.write(text)


def cmd_build(a):
    d = open(a.file, "rb").read()
    doc = json.load(open(a.json, encoding="utf-8"))
    if _kind(d) != "ebp" or doc.get("kind") != "ebp":
        sys.exit("build supports EBP2 event scripts only")
    out = ebp_build(d, doc["strings"])
    open(a.o, "wb").write(out)
    print("wrote %s (%d bytes, was %d)" % (a.o, len(out), len(d)))


def cmd_verify(a):
    files = strings = 0
    unknown = {}
    bad = []
    for r, _, fs in os.walk(a.dir):
        for f in sorted(fs):
            p = os.path.join(r, f)
            with open(p, "rb") as fh:
                d = fh.read()
            k = _kind(d)
            if k is None:
                continue
            try:
                if k == "ebp":
                    t, es = ebp_read(d)
                    if t == 0:
                        continue
                    if ebp_build(d, es) != d:
                        bad.append((p, "rebuild differs"))
                else:
                    es = st2_read(d)
                for e in es:
                    raw = encode(e["text"])
                    if decode(raw) != e["text"]:
                        bad.append((p, "codec"))
                    for tok in e["text"].split("{")[1:]:
                        h = tok.split("}")[0]
                        if len(h) == 2:
                            unknown[h] = unknown.get(h, 0) + 1
                files += 1
                strings += len(es)
            except Exception as ex:  # noqa: BLE001 - report and continue
                bad.append((p, repr(ex)))
    print("%d files, %d strings, %d failures" % (files, strings, len(bad)))
    for p, why in bad[:20]:
        print("  FAIL %s: %s" % (p, why))
    top = sorted(unknown.items(), key=lambda kv: -kv[1])[:12]
    print("unmapped bytes (most common):", ", ".join("%s x%d" % kv for kv in top))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("dump")
    p.add_argument("file")
    p.add_argument("-o")
    p = sp.add_parser("build")
    p.add_argument("file")
    p.add_argument("json")
    p.add_argument("-o", required=True)
    p = sp.add_parser("verify")
    p.add_argument("dir")
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")  # the text holds non-ASCII glyphs
    return {"dump": cmd_dump, "build": cmd_build, "verify": cmd_verify}[a.cmd](a) or 0


if __name__ == "__main__":
    sys.exit(main())
