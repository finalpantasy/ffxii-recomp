#!/usr/bin/env python3
"""Lists the Lua C function tables (luaL_Reg arrays) in an x64 PE.

A luaL_Reg array is a run of {const char* name, lua_CFunction func} pairs
ending in {NULL, NULL}. The scanner looks in the image's data sections for
8-byte aligned runs of at least MIN_RUN pairs whose name points to a short
identifier string inside the image and whose function is a known function
entry (analysis/maps/functions.tsv, first column a hex VA).

Usage: find_lua_regs.py <exe> <functions.tsv> [--min N]
Prints: table_va  index  func_va  name   (one line per entry)

Every function listed is reached from Lua through a function pointer, so
the runtime's dispatcher calls it: native-module hooks catch all its calls.
"""
import struct
import sys


def load_pe(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    base = struct.unpack_from("<Q", data, opt + 24)[0]
    secs = []
    for i in range(nsec):
        o = opt + opt_size + 40 * i
        name = data[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, o + 8)
        secs.append((name, base + va, max(vsize, rsize), raw, rsize))
    return data, base, secs


def main():
    args = sys.argv[1:]
    min_run = 2
    if "--min" in args:
        i = args.index("--min")
        min_run = int(args[i + 1])
        del args[i:i + 2]
    if len(args) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    data, base, secs = load_pe(args[0])
    funcs = set()
    for line in open(args[1], encoding="utf-8", errors="replace"):
        f = line.split("\t", 1)[0].strip()
        try:
            funcs.add(int(f, 16))
        except ValueError:
            pass

    def read(va, n):
        for _, sva, size, raw, rsize in secs:
            if sva <= va < sva + rsize and va + n <= sva + rsize:
                o = raw + (va - sva)
                return data[o:o + n]
        return None

    def ident(va):
        b = read(va, 64)
        if not b or b"\0" not in b:
            return None
        s = b.split(b"\0", 1)[0]
        if not 1 <= len(s) <= 48:
            return None
        if not all(c == 95 or 48 <= c <= 57 or 65 <= c <= 90 or 97 <= c <= 122 for c in s):
            return None
        if 48 <= s[0] <= 57:
            return None
        return s.decode()

    tables = 0
    for name, sva, size, raw, rsize in secs:
        if name not in (".rdata", ".data"):
            continue
        n = rsize // 8
        q = struct.unpack_from("<%dQ" % n, data, raw)
        i = 0
        while i + 1 < n:
            run = []
            j = i
            while j + 1 < n and q[j + 1] in funcs:
                nm = ident(q[j])
                if nm is None:
                    break
                run.append((nm, q[j + 1]))
                j += 2
            if len(run) >= min_run and j + 1 < n and q[j] == 0 and q[j + 1] == 0:
                tva = sva + 8 * i
                for k, (nm, fn) in enumerate(run):
                    print("%08X\t%d\t%08X\t%s" % (tva, k, fn, nm))
                tables += 1
                i = j + 2
            else:
                i += 1
    print("# %d tables" % tables, file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
