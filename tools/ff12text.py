#!/usr/bin/env python3
"""FFXII TZA game text: decode, dump and rebuild (analysis/formats/text.md).

Text in event scripts (EBP2, `*.ebp`: dialog), st2e string tables (menus,
help, battle messages), word tables (names) and battle_pack.bin uses the
game's own one-byte character set. This tool turns it into editable JSON and
back.

  ff12text.py dump FILE [-o OUT.json]   the text of one file as JSON
  ff12text.py build FILE IN.json -o OUT write FILE with the JSON's text
  ff12text.py verify DIR                decode, re-encode and rebuild every
                                        text file under DIR; the output
                                        must equal the input bytes

Text form: letters, digits, space and the mapped punctuation as themselves;
byte 0x02 as a line break ("\\n"); control sequences (0x0E or 0x0F, then the
code's argument bytes) as {0F 2F 81 80}; any other byte as {XX}. Decoding then encoding gives back the same bytes for
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


def encode(s, allow_nul=False):
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
    if 0 in out and not allow_nul:
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


# --- st2e string tables -------------------------------------------------------
# "st2e", u32 count, u32 record size (4 or 8), u32 0x20 (header size), u32 0,
# u32 section; count records from 0x20 (kept as they are); the section:
# u32 1, u32 12 (table, from the section), u32 size (section start to the
# end of the last string), then the table: u32 count, count + 1 u32 offsets
# from the table start (the last is the end of the strings), then the
# strings. The file is zero-padded to a multiple of 4.
#
# A string is game bytes ending in 00, or "ex00" then UTF-16LE text ending
# in a 16-bit 0 (menu names); the text form of the latter is "{ex00}" and
# the plain characters.

EX00 = b"ex00"
EX00_TEXT = "{ex00}"


def _st2_raw(d, at):
    """The string at `at` with its terminator."""
    if d[at:at + 4] == EX00:
        i = at + 4
        while d[i:i + 2] != b"\0\0":
            i += 2
        return d[at:i + 2]
    return d[at:d.index(b"\0", at) + 1]


def _st2_decode(raw):
    if raw.startswith(EX00):
        return EX00_TEXT + raw[4:-2].decode("utf-16-le")
    return decode(raw[:-1])


def _st2_encode(s):
    if s.startswith(EX00_TEXT):
        return EX00 + s[len(EX00_TEXT):].encode("utf-16-le") + b"\0\0"
    return encode(s) + b"\0"


def _st2_header(d):
    if d[:4] != b"st2e":
        raise ValueError("not an st2e file")
    sec = struct.unpack_from("<I", d, 0x14)[0]
    one, tab_rel, size = struct.unpack_from("<3I", d, sec)
    tab = sec + tab_rel
    n = struct.unpack_from("<I", d, tab)[0]
    if one != 1 or n != struct.unpack_from("<I", d, 4)[0]:
        raise ValueError("unexpected st2e section header")
    return sec, tab, n


def st2_read(d):
    sec, tab, n = _st2_header(d)
    out = []
    for i in range(n):
        off = struct.unpack_from("<I", d, tab + 4 + 4 * i)[0]
        out.append({"offset": off, "text": _st2_decode(_st2_raw(d, tab + off))})
    return out


def st2_build(d, strings):
    """d with its strings replaced by `strings` (list of {text})."""
    sec, tab, n = _st2_header(d)
    old = st2_read(d)
    if len(strings) != n:
        raise ValueError("string count %d differs from the file's %d" % (len(strings), n))
    table = 4 + 4 * (n + 1)
    body = bytearray()
    placed = {}
    offs = [0] * n
    for i in sorted(range(n), key=lambda k: (old[k]["offset"], k)):
        key = (old[i]["offset"], strings[i]["text"])
        if key not in placed:
            placed[key] = table + len(body)
            body += _st2_encode(strings[i]["text"])
        offs[i] = placed[key]
    out = bytearray(d[:tab])
    out += struct.pack("<%dI" % (n + 2), n, *offs, table + len(body))
    out += body
    struct.pack_into("<I", out, sec + 8, len(out) - sec)
    out += b"\0" * (-len(out) % 4)
    return bytes(out)


# --- word tables (word.bin, battle_pack section 61) ---------------------------
# u32 n, n + 1 u32 offsets of sub-tables (the last is the file size). Each
# sub-table: u32 count, count + 1 u32 offsets from the sub-table start (the
# last is its size), then the entries. An entry is 00 00, the word, and more
# zero-separated fields (grammar data), zero-padded so entries stay 4-byte
# aligned. Text form: the entry without its trailing zeros, inner zeros as
# {00}; "pad" keeps the number of trailing zeros.


def _u32s(d, at, n):
    return struct.unpack_from("<%dI" % n, d, at)


def _is_strtab(d, at, size):
    if size < 8:
        return False
    m = _u32s(d, at, 1)[0]
    if 4 + 4 * (m + 1) > size:
        return False
    o = _u32s(d, at + 4, m + 1)
    return o[0] == 4 + 4 * (m + 1) and o[-1] == size and list(o) == sorted(o)


def _is_words(d):
    if len(d) < 8:
        return False
    n = _u32s(d, 0, 1)[0]
    if n == 0 or 4 + 4 * (n + 1) > len(d):
        return False
    o = _u32s(d, 4, n + 1)
    if o[0] != 4 + 4 * (n + 1) or o[-1] != len(d) or list(o) != sorted(o):
        return False
    return all(_is_strtab(d, o[k], o[k + 1] - o[k]) for k in range(n))


def words_read(d):
    n = _u32s(d, 0, 1)[0]
    o = _u32s(d, 4, n + 1)
    tables = []
    for k in range(n):
        b = o[k]
        m = _u32s(d, b, 1)[0]
        e = _u32s(d, b + 4, m + 1)
        ents = []
        for i in range(m):
            raw = d[b + e[i]:b + e[i + 1]]
            s = raw.rstrip(b"\0")
            ents.append({"text": decode(s), "pad": len(raw) - len(s)})
        tables.append(ents)
    return tables


def words_build(d, tables):
    n = _u32s(d, 0, 1)[0]
    if len(tables) != n:
        raise ValueError("table count %d differs from the file's %d" % (len(tables), n))
    subs = []
    for ents in tables:
        body = bytearray()
        offs = []
        head = 4 + 4 * (len(ents) + 1)
        for e in ents:
            offs.append(head + len(body))
            raw = encode(e["text"], allow_nul=True) + b"\0" * int(e["pad"])
            body += raw + b"\0" * (-len(raw) % 4)
        subs.append(struct.pack("<%dI" % (len(ents) + 2), len(ents), *offs, head + len(body)) + body)
    out = bytearray()
    head = 4 + 4 * (n + 1)
    offs = []
    for s in subs:
        offs.append(head + len(out))
        out += s
    return struct.pack("<%dI" % (n + 2), n, *offs, head + len(out)) + bytes(out)


# --- packs (battle_pack.bin) --------------------------------------------------
# u32 n, n + 1 u32 section offsets (the last is the end of the last
# section); the header, every section and the file are zero-padded to 8
# bytes. The padding after the last section is not in its offsets. Sections that are st2e tables or
# word tables are edited; the rest are copied as they are.

PACK_ALIGN = 8


def _pack_sections(d):
    if len(d) < 8:
        return None
    n = _u32s(d, 0, 1)[0]
    if n == 0 or 4 + 4 * (n + 1) > len(d):
        return None
    o = _u32s(d, 4, n + 1)
    if not len(d) - PACK_ALIGN < o[-1] <= len(d) or list(o) != sorted(o) or o[0] < 4 + 4 * (n + 1):
        return None
    secs = [d[o[k]:o[k + 1]] for k in range(n)]
    if not any(s[:4] == b"st2e" for s in secs):
        return None
    return secs


def _words_end(s):
    """End of a word table's content, or 0 if s does not hold one."""
    if len(s) < 8:
        return 0
    n = _u32s(s, 0, 1)[0]
    if n == 0 or 4 + 4 * (n + 1) > len(s):
        return 0
    end = _u32s(s, 4 + 4 * n, 1)[0]
    return end if end <= len(s) and _is_words(s[:end]) else 0


def _section_kind(s):
    if s[:4] == b"st2e" and len(s) >= 0x20 and _u32s(s, 0x14, 1)[0]:  # data tables have no text
        return "st2e"
    if _words_end(s):
        return "words"
    return None


def _trim(s):
    """A pack section without the zero padding after its content."""
    if s[:4] == b"st2e":
        sec = _u32s(s, 0x14, 1)[0]
        end = sec + _u32s(s, sec + 8, 1)[0]
        return s[:end + (-end % 4)]
    return s[:_words_end(s)]


def pack_read(d):
    out = []
    for k, s in enumerate(_pack_sections(d)):
        kind = _section_kind(s)
        if kind == "st2e":
            out.append({"section": k, "kind": kind, "strings": _strip(st2_read(_trim(s)))})
        elif kind == "words":
            out.append({"section": k, "kind": kind, "tables": words_read(_trim(s))})
    return out


def pack_build(d, sections):
    secs = list(_pack_sections(d))
    for e in sections:
        k = int(e["section"])
        s = secs[k]
        if e["kind"] != _section_kind(s):
            raise ValueError("section %d is not %s" % (k, e["kind"]))
        secs[k] = st2_build(_trim(s), e["strings"]) if e["kind"] == "st2e" else words_build(_trim(s), e["tables"])
    n = len(secs)
    head = 4 + 4 * (n + 1)
    pos = head + (-head % PACK_ALIGN)
    offs = []
    body = bytearray()
    for k, s in enumerate(secs):
        offs.append(pos + len(body))
        body += s
        if k + 1 < n:
            body += b"\0" * (-len(body) % PACK_ALIGN)
    hdr = struct.pack("<%dI" % (n + 2), n, *offs, pos + len(body))
    out = hdr + b"\0" * (pos - head) + bytes(body)
    return out + b"\0" * (-len(out) % PACK_ALIGN)


# --- commands ---------------------------------------------------------------


def _strip(es):
    return [{kk: v for kk, v in e.items() if kk != "offset"} for e in es]


def _kind(d):
    if d[:4] == b"EBP2":
        return "ebp"
    if d[:4] == b"st2e":
        return "st2e"
    if _is_words(d):
        return "words"
    if _pack_sections(d):
        return "pack"
    return None


def to_doc(d, name=""):
    """The editable JSON document for file bytes d, or None."""
    k = _kind(d)
    doc = {"format": "ff12text-1", "kind": k, "source": name}
    if k == "ebp":
        t, es = ebp_read(d)
        if t == 0:
            return None
        doc["strings"] = _strip(es)
    elif k == "st2e":
        doc["strings"] = _strip(st2_read(d))
    elif k == "words":
        doc["tables"] = words_read(d)
    elif k == "pack":
        doc["sections"] = pack_read(d)
    else:
        return None
    return doc


def from_doc(d, doc):
    """File bytes d rebuilt with the document's text."""
    k = _kind(d)
    if k is None or doc.get("kind") != k:
        raise ValueError("file is not a %s file" % doc.get("kind"))
    if k == "ebp":
        return ebp_build(d, doc["strings"])
    if k == "st2e":
        return st2_build(d, doc["strings"])
    if k == "words":
        return words_build(d, doc["tables"])
    return pack_build(d, doc["sections"])


def _doc_texts(doc):
    for s in doc.get("strings", []):
        yield s["text"]
    for t in doc.get("tables", []):
        for e in t:
            yield e["text"]
    for sec in doc.get("sections", []):
        yield from _doc_texts(sec)


def cmd_dump(a):
    d = open(a.file, "rb").read()
    doc = to_doc(d, os.path.basename(a.file))
    if doc is None:
        sys.exit("%s: not a text file this tool knows (EBP2 with text, st2e, word table, pack)" % a.file)
    text = json.dumps(doc, ensure_ascii=False, indent=1) + "\n"
    if a.o:
        open(a.o, "w", encoding="utf-8", newline="\n").write(text)
    else:
        sys.stdout.write(text)


def cmd_build(a):
    d = open(a.file, "rb").read()
    doc = json.load(open(a.json, encoding="utf-8"))
    try:
        out = from_doc(d, doc)
    except ValueError as ex:
        sys.exit("%s: %s" % (a.file, ex))
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
            try:
                doc = to_doc(d, f)
                if doc is None:
                    continue
                if from_doc(d, doc) != d:
                    bad.append((p, "rebuild differs"))
                texts = list(_doc_texts(doc))
                for s in texts:
                    if s.startswith(EX00_TEXT):
                        continue
                    if decode(encode(s, allow_nul=True)) != s:
                        bad.append((p, "codec"))
                    for tok in s.split("{")[1:]:
                        h = tok.split("}")[0]
                        if len(h) == 2:
                            unknown[h] = unknown.get(h, 0) + 1
                files += 1
                strings += len(texts)
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
