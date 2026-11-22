#!/usr/bin/env python3
"""Direct-call reachability over the lifted code.

Usage: python -I tools/callgraph.py generated/full <target hex> <root hex>...
Prints, for each root, whether it reaches the target through direct calls
and tail jumps, and the shortest path when it does.
"""
import glob
import re
import sys
from collections import deque

src, target, roots = sys.argv[1], int(sys.argv[2], 16), [int(r, 16) for r in sys.argv[3:]]
fn_re = re.compile(r"^void f_([0-9A-F]{8})\(::recomp::Ctx& c\) \{")
call_re = re.compile(r"// [0-9A-F]+ (?:call|jmp) 0x0*([0-9A-F]+)\s*$")
graph = {}
cur = None
for path in sorted(glob.glob(src + "/gen_*.cpp")):
    for line in open(path, encoding="utf-8", errors="replace"):
        m = fn_re.match(line)
        if m:
            cur = int(m.group(1), 16)
            graph.setdefault(cur, set())
            continue
        if cur is not None and "// " in line and ("call 0x" in line or "jmp 0x" in line):
            m = call_re.search(line.rstrip(" {\n"))
            if m:
                graph[cur].add(int(m.group(1), 16))
funcs = set(graph)
for root in roots:
    prev = {root: None}
    q = deque([root])
    while q:
        f = q.popleft()
        if f == target:
            break
        for t in graph.get(f, ()):
            if t in funcs and t not in prev:
                prev[t] = f
                q.append(t)
    if target in prev:
        p, path = target, []
        while p is not None:
            path.append("%X" % p)
            p = prev[p]
        print("%X reaches: %s" % (root, " <- ".join(path)))
    else:
        print("%X no (%d reachable)" % (root, len(prev)))
