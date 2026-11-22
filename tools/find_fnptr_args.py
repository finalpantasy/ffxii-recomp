#!/usr/bin/env python3
"""Finds functions that receive code addresses as arguments, from the
lifter's generated C++ (whose comments carry the original instructions).

For every `call` it looks back a few instructions in the same function for
`lea <arg reg>, [0x...]` where the address is a known function entry
(analysis/maps/functions.tsv). Callees that take many function addresses
are registration points: Lua's lua_pushcclosure / lua_register, callback
tables, thread starts.

Usage: find_fnptr_args.py <generated dir> <functions.tsv> [--callee VA] [--top N]
  default: callees ranked by how many function-address arguments they get
  --callee VA: every call site of VA with its function-address arguments,
               plus string arguments (lea of a non-code address)
"""
import os
import re
import sys

CALL = re.compile(r"// ([0-9A-F]{8}) call 0x0*([0-9A-F]+)$")
LEA = re.compile(r"// ([0-9A-F]{8}) lea (rcx|rdx|r8|r9), \[0x0*([0-9A-F]+)\]$")
INSN = re.compile(r"// ([0-9A-F]{8}) ")
FUNC = re.compile(r"^void f_([0-9A-F]{8})\(")
LOOKBACK = 12


def main():
    args = sys.argv[1:]
    callee = None
    top = 40
    if "--callee" in args:
        i = args.index("--callee")
        callee = int(args[i + 1], 16)
        del args[i:i + 2]
    if "--top" in args:
        i = args.index("--top")
        top = int(args[i + 1])
        del args[i:i + 2]
    if len(args) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    gen_dir, tsv = args
    funcs = set()
    for line in open(tsv, encoding="utf-8", errors="replace"):
        try:
            funcs.add(int(line.split("\t", 1)[0], 16))
        except ValueError:
            pass

    counts = {}
    for name in sorted(os.listdir(gen_dir)):
        if not name.startswith("gen_") or not name.endswith(".cpp"):
            continue
        recent = []  # (reg, addr) of recent leas, cleared at function start
        cur = None
        with open(os.path.join(gen_dir, name), encoding="utf-8", errors="replace") as f:
            for line in f:
                m = FUNC.match(line)
                if m:
                    cur = int(m.group(1), 16)
                    recent = []
                    continue
                if "// " not in line:
                    continue
                s = line.strip()
                m = LEA.search(s)
                if m:
                    recent.append((m.group(2), int(m.group(3), 16)))
                    recent = recent[-LOOKBACK:]
                    continue
                m = CALL.search(s)
                if m:
                    site = int(m.group(1), 16)
                    target = int(m.group(2), 16)
                    regs = {}
                    for reg, a in recent:
                        regs[reg] = a  # latest load of each register wins
                    fn_args = {r: a for r, a in regs.items() if a in funcs}
                    if callee is None:
                        if fn_args:
                            counts[target] = counts.get(target, 0) + len(fn_args)
                    elif target == callee:
                        desc = " ".join("%s=%s%08X" % (r, "fn:" if a in funcs else "", a)
                                        for r, a in sorted(regs.items()))
                        print("%08X\tin %08X\t%s" % (site, cur or 0, desc))
                    recent = []
                    continue
                if INSN.search(s) and (" ret" in s or " jmp " in s):
                    recent = []
    if callee is None:
        for t, n in sorted(counts.items(), key=lambda kv: -kv[1])[:top]:
            print("%08X\t%d" % (t, n))
    return 0


if __name__ == "__main__":
    sys.exit(main())
