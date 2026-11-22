#!/usr/bin/env python3
"""Print lifter manifest `crt` lines for import stubs of memory primitives.

A stub is a function that is only `jmp [rip+disp32]` through an import slot.
For VCRUNTIME140 memcpy, memmove and memset the lifter can emit direct calls
to the stub inline (SwARc lifter_x86, manifest `crt`), which skips the
register spill, the dispatch and the HLE call for every small copy.

Usage: python -I tools/find_crt_stubs.py <FFXII_TZA.exe> analysis/maps/functions.tsv
"""
import struct
import sys

WANTED = {("vcruntime140.dll", "memcpy"), ("vcruntime140.dll", "memmove"),
          ("vcruntime140.dll", "memset")}


def main():
    exe, tsv = sys.argv[1], sys.argv[2]
    d = open(exe, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    opt = pe + 24
    osz = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<Q", d, opt + 24)[0]
    secs = [struct.unpack_from("<8sIIII", d, opt + osz + i * 40) for i in range(nsec)]

    def off(rva):
        for _, vs, va, rs, ro in secs:
            if va <= rva < va + max(vs, rs):
                return rva - va + ro
        return None

    def cstr(o):
        return d[o:d.index(b"\0", o)].decode("ascii")

    slots = {}  # import slot VA -> (dll, name)
    i = off(struct.unpack_from("<I", d, opt + 112 + 8)[0])
    while True:
        olt, _, _, name, ft = struct.unpack_from("<5I", d, i)
        if not name:
            break
        dll = cstr(off(name)).lower()
        k = 0
        while True:
            e = struct.unpack_from("<Q", d, off(olt or ft) + 8 * k)[0]
            if not e:
                break
            if not e >> 63:
                slots[base + ft + 8 * k] = (dll, cstr(off(e & 0x7FFFFFFF) + 2))
            k += 1
        i += 20

    for line in open(tsv):
        if line.startswith("entry"):
            continue
        va = int(line.split("\t")[0], 16)
        o = off(va - base)
        if o is None:
            continue
        code = d[o:o + 7]
        if code[:2] == b"\xff\x25":
            slot = va + 6 + struct.unpack_from("<i", code, 2)[0]
        elif code[:3] == b"\x48\xff\x25":
            slot = va + 7 + struct.unpack_from("<i", code, 3)[0]
        else:
            continue
        imp = slots.get(slot)
        if imp in WANTED:
            print("crt %08X %s" % (va, imp[1]))


if __name__ == "__main__":
    main()
