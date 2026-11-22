#!/usr/bin/env python3
# Usage: python -I tools/snapdiff.py <snap dir> <idle pairs a-b,..> <turn pairs a-b,..>
# Pure-Python diff of .data snapshots (frame_rate.cpp FFXII_SNAP): floats that
# change in every "turn" pair and in no "idle" pair, then 3x3 rotation blocks.
import sys, glob, struct, math
R, idle, turn = sys.argv[1], sys.argv[2], sys.argv[3]
idle = [tuple(map(int, p.split("-"))) for p in idle.split(",")]
turn = [tuple(map(int, p.split("-"))) for p in turn.split(",")]
files = {int(f.split("snap-")[1][:2]): f for f in glob.glob(R + "/snap-*.bin")}
need = sorted({i for p in idle + turn for i in p})
D = {i: open(files[i], "rb").read() for i in need}
n = len(D[need[0]])
C = 64
cand_words = set()
for off in range(0, n, C):
    if any(D[a][off:off+C] == D[b][off:off+C] for a, b in turn):
        # a chunk equal across a turn pair may still hold changed words elsewhere; check words
        pass
    if all(D[a][off:off+C] == D[b][off:off+C] for a, b in turn):
        continue
    for w in range(off, min(off + C, n), 4):
        if all(D[a][w:w+4] != D[b][w:w+4] for a, b in turn) and all(D[a][w:w+4] == D[b][w:w+4] for a, b in idle):
            cand_words.add(w)
print("words changed only while turning:", len(cand_words))
base = 0x1DF0000
def f32(buf, o): return struct.unpack_from("<f", buf, o)[0]
def rot(buf, o, stride):
    m = [[f32(buf, o + r * stride + c * 4) for c in range(3)] for r in range(3)]
    if not all(math.isfinite(x) for row in m for x in row): return None
    for i in range(3):
        for j in range(3):
            d = sum(m[i][k] * m[j][k] for k in range(3))
            if abs(d - (1 if i == j else 0)) > 3e-3: return None
    return m
ref = D[turn[0][0]]
hits = {}
for w in sorted(cand_words):
    for stride in (16, 12):
        for o in range(w - 2 * stride - 8, w + 1, 4):
            if o < 0 or (o, stride) in hits: continue
            if all(rot(D[a], o, stride) and rot(D[b], o, stride) for a, b in turn):
                hits[(o, stride)] = rot(ref, o, stride)
print(len(hits), "rotation blocks")
for (o, s), m in sorted(hits.items())[:30]:
    t = [round(f32(ref, o + 48 + 4 * k), 2) for k in range(4)] if s == 16 else []
    print(hex(base + o), "stride", s, [[round(x, 3) for x in r] for r in m], "next row", t)
print("sample changed words:", [hex(base + w) for w in sorted(cand_words)[:40]])
