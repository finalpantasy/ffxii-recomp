# Generates manifest `hook` lines that rescale products of the frames-per-step
# global 0x1DFE0B4 (B4) for frame rates that are not multiples of 30.
#
# B4 holds whole frames per 30 Hz step (2 at 60 fps). At 144 fps a step is 4.8
# frames, so the recomp stores round(4.8) = 5 and fixes every place the game
# turns B4 into a frame count: after `imul`, `lea`, `shl` and int-to-float
# conversions of B4 a hook rewrites the result to round(result * F / B4), with
# F = rate / 30 (src/frame_rate.cpp, FpsHook, names fps_scale_* and
# fps_fscale_*). `div [B4]` sites are handled separately (fps_div).
#
# Each load of B4 is followed through register copies to its uses. A site is
# emitted only when every use is a clean product or conversion and the hook
# address is not a branch or jump-table target; everything else is printed
# for manual handling.
#
# Usage: python tools/gen_fps_scale_hooks.py <FFXII_TZA.exe> analysis/maps/functions.tsv
#        analysis/maps/jumptables.tsv > out.txt
import re
import struct
import sys

import capstone
from capstone import x86

B4 = 0x1DFE0B4
d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
n = struct.unpack_from('<H', d, pe + 6)[0]
oh = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<Q', d, pe + 24 + 24)[0]
secs = []
for i in range(n):
    o = pe + 24 + oh + 40 * i
    vs, va, rs, ra = struct.unpack_from('<IIII', d, o + 8)
    secs.append((va, vs, ra, rs))


def rd(v, size):
    v -= base
    for va, vs, ra, rs in secs:
        if va <= v < va + rs:
            f = ra + v - va
            return d[f:f + size]
    return b''


md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
FAM = {}
for grp in [('rax', 'eax', 'ax', 'al'), ('rbx', 'ebx', 'bx', 'bl'), ('rcx', 'ecx', 'cx', 'cl'),
            ('rdx', 'edx', 'dx', 'dl'), ('rsi', 'esi', 'si', 'sil'), ('rdi', 'edi', 'di', 'dil'),
            ('rbp', 'ebp', 'bp', 'bpl')] + [(f'r{k}', f'r{k}d', f'r{k}w', f'r{k}b') for k in range(8, 16)]:
    for r in grp:
        FAM[r] = grp[0]
VOLATILE = {'rax', 'rcx', 'rdx', 'r8', 'r9', 'r10', 'r11'}


def fam(i, r):
    s = i.reg_name(r)
    return FAM.get(s, s)


def b4_mem(i):
    m = re.search(r'rip ([+-]) 0x([0-9a-f]+)', i.op_str)
    if not m:
        return False
    t = i.address + i.size + (1 if m.group(1) == '+' else -1) * int(m.group(2), 16)
    return t == B4


targets = set()
for ln in open(sys.argv[3]):
    p = ln.split('\t')
    if p[0] == 'jmp' or len(p) < 2:
        continue
    for t in p[1].strip().split(','):
        if t:
            targets.add(int(t, 16))

funcs = []
for ln in open(sys.argv[2]):
    p = ln.split('\t')
    if p[0] == 'entry':
        continue
    funcs.append((int(p[1], 16), int(p[2], 16)))

hooks = {}   # va -> (name, site)
manual = []  # (site, reason, text)


def is_branch(i):
    return i.group(capstone.CS_GRP_JUMP) or i.group(capstone.CS_GRP_RET)


def accesses(u):
    rr, ww = u.regs_access()
    reads = {FAM.get(u.reg_name(r), u.reg_name(r)) for r in rr}
    writes = {FAM.get(u.reg_name(r), u.reg_name(r)) for r in ww}
    for op in u.operands:
        if op.type == x86.X86_OP_MEM:
            for r in (op.mem.base, op.mem.index):
                if r:
                    reads.add(fam(u, r))
    # `xor r, r` and `sub r, r` only write
    o = u.operands
    if u.mnemonic in ('xor', 'sub') and len(o) == 2 and o[0].type == o[1].type == x86.X86_OP_REG and o[0].reg == o[1].reg:
        reads.discard(fam(u, o[0].reg))
    # a 32-bit write replaces the whole register; narrower writes merge
    for op in o[:1]:
        if op.type == x86.X86_OP_REG and op.size < 4 and fam(u, op.reg) in writes:
            reads.add(fam(u, op.reg))
    return reads, writes


def emit(found, u, nxt, srcs, live, maybe):
    # srcs: operands holding B4 (None: the lea's address registers)
    o = u.operands
    if srcs is None:
        mem = o[1].mem
        names = [u.reg_name(r) for r in (mem.base, mem.index) if r]
    else:
        names = [u.reg_name(x.reg) for x in srcs if x.type == x86.X86_OP_REG]
    cond = [r for r in names if FAM.get(r, r) in maybe]
    dst = u.reg_name(o[0].reg)
    if cond:
        found.append((nxt, 'fps_cscale_' + dst, u.address, 'fps_pre_' + cond[0]))
    else:
        found.append((nxt, 'fps_scale_' + dst, None, None))
    live.discard(fam(u, o[0].reg))
    maybe.discard(fam(u, o[0].reg))


def va_of_prev(ins, index, va):
    return ins[index[va] - 1].address


def dead(ins, index, regs, budget=80, depth=3):
    """True when no path from ins[index] reads any of regs before writing it."""
    regs = set(regs)
    k = index
    while regs and budget > 0 and k < len(ins):
        u = ins[k]
        budget -= 1
        reads, writes = accesses(u)
        if reads & regs:
            return False
        m = u.mnemonic
        if m.startswith('call'):
            if regs & {'rcx', 'rdx', 'r8', 'r9'}:
                return False
            regs -= VOLATILE
            k += 1
            continue
        if u.group(capstone.CS_GRP_RET):
            # a stale B4 left in rax by a function whose result is something
            # else; none of these functions returns the frame count itself
            return True
        if u.group(capstone.CS_GRP_JUMP):
            if not (u.operands and u.operands[0].type == x86.X86_OP_IMM) or depth == 0:
                return False
            t = u.operands[0].imm
            ti = INDEX.get(t)
            if ti is None:
                return False
            if not dead(ins, ti, regs, budget, depth - 1):
                return False
            if m == 'jmp':
                return True
            k += 1
            continue
        regs -= writes
        k += 1
    return not regs


for lo, hi in funcs:
    code = rd(lo, hi - lo + 1)
    if not code:
        continue
    ins = list(md.disasm(code, lo))
    INDEX = {x.address: q for q, x in enumerate(ins)}
    local_targets = set(targets)
    for i in ins:
        if (i.group(capstone.CS_GRP_JUMP)) and i.operands and i.operands[0].type == x86.X86_OP_IMM:
            local_targets.add(i.operands[0].imm)
    for k, i in enumerate(ins):
        if not b4_mem(i):
            continue
        site = i.address
        mn = i.mnemonic
        ops = i.operands
        if mn in ('div', 'idiv'):
            continue  # fps_div
        if ops and ops[0].type == x86.X86_OP_MEM:
            manual.append((site, 'write', f'{mn} {i.op_str}'))
            continue
        found = []   # (hook va, name, pre-hook va or None, pre-hook name)
        bad = None
        maybe = set()  # registers holding B4 or another value (cmov)
        if mn == 'imul':
            found.append((i.address + i.size, 'fps_scale_' + i.reg_name(ops[0].reg), None, None))
            live = set()
        elif mn in ('mov', 'movzx', 'movsxd', 'movsx'):
            live = {fam(i, ops[0].reg)}
        else:
            manual.append((site, 'first use', f'{mn} {i.op_str}'))
            continue
        j = k + 1
        while live and not bad:
            if j >= len(ins) or j > k + 40:
                if not (found and dead(ins, j, live)):
                    bad = 'window'
                break
            u = ins[j]
            j += 1
            nxt = u.address + u.size
            try:
                rr, ww = u.regs_access()
            except capstone.CsError:
                bad = 'decode'
                break
            reads = {FAM.get(u.reg_name(r), u.reg_name(r)) for r in rr}
            writes = {FAM.get(u.reg_name(r), u.reg_name(r)) for r in ww}
            # lea's address registers are reads too
            for op in u.operands:
                if op.type == x86.X86_OP_MEM:
                    for r in (op.mem.base, op.mem.index):
                        if r:
                            reads.add(fam(u, r))
            if u.address in local_targets and live:
                if found and dead(ins, j - 1, live):
                    break
                # another path joins: from here on the registers hold B4 or
                # whatever that path left, so products need the value check
                maybe |= live
            uses = reads & live
            if u.mnemonic.startswith('call'):
                if uses:
                    bad = 'passed to call'
                    break
                live -= VOLATILE
                continue
            if is_branch(u):
                if found and dead(ins, j - 1, live):
                    break
                cond = u.group(capstone.CS_GRP_JUMP) and u.mnemonic != 'jmp'
                imm = u.operands and u.operands[0].type == x86.X86_OP_IMM
                if cond and imm and INDEX.get(u.operands[0].imm) is not None and dead(ins, INDEX[u.operands[0].imm], live):
                    continue  # the taken path does not read B4; follow the other
                bad = 'live across branch'
                break
            m = u.mnemonic
            o = u.operands
            if m.startswith('cmov'):
                dst = fam(u, o[0].reg)
                src_live = o[1].type == x86.X86_OP_REG and fam(u, o[1].reg) in live
                if dst in live or src_live:
                    live.add(dst)
                    maybe.add(dst)
                continue
            if not uses:
                live -= writes
                maybe -= writes
                continue
            if m in ('mov', 'movzx', 'movsx', 'movsxd') and o[0].type == x86.X86_OP_REG and o[1].type == x86.X86_OP_REG:
                live.add(fam(u, o[0].reg))
                if fam(u, o[1].reg) in maybe:
                    maybe.add(fam(u, o[0].reg))
                else:
                    maybe.discard(fam(u, o[0].reg))
                continue
            if m == 'imul' and o[0].type == x86.X86_OP_REG and not (len(o) == 1):
                srcs = [x for x in o[1:] if x.type == x86.X86_OP_REG and fam(u, x.reg) in live]
                if len(o) == 2 and fam(u, o[0].reg) in live and o[1].type == x86.X86_OP_REG and fam(u, o[1].reg) in live:
                    bad = 'B4 squared'
                    break
                emit(found, u, nxt, [x for x in o[1:] if x.type == x86.X86_OP_REG and fam(u, x.reg) in live] or [o[0]], live, maybe)
                continue
            if m == 'lea':
                mem = o[1].mem
                regs = [fam(u, r) for r in (mem.base, mem.index) if r]
                if sum(1 for r in regs if r in live) and all((r in live) for r in regs) and mem.disp == 0:
                    emit(found, u, nxt, None, live, maybe)
                    continue
                bad = f'lea {u.op_str}'
                break
            if m in ('shl', 'sal') and o[0].type == x86.X86_OP_REG and fam(u, o[0].reg) in live and o[1].type == x86.X86_OP_IMM:
                emit(found, u, nxt, [o[0]], live, maybe)
                continue
            if m in ('cvtsi2ss', 'cvtsi2sd'):
                src = o[1]
                if src.type == x86.X86_OP_REG and fam(u, src.reg) in maybe:
                    found.append((nxt, f'fps_cfscale_{m[-2:]}_' + u.reg_name(o[0].reg), u.address, 'fps_pre_' + u.reg_name(src.reg)))
                else:
                    found.append((nxt, f'fps_fscale_{m[-2:]}_' + u.reg_name(o[0].reg), None, None))
                continue
            bad = f'{m} {u.op_str}'
        if bad:
            manual.append((site, ('partial: ' if found else '') + bad, f'{mn} {i.op_str}'))
            if not found:
                continue
        if not found:
            manual.append((site, 'no product found', f'{mn} {i.op_str}'))
            continue
        for va, name, pva, pname in found:
            if pva is None and va in local_targets:
                # reached from elsewhere too: scale only after passing the product
                pva, pname = va_of_prev(ins, INDEX, va), 'fps_pre_always'
                name = name.replace('fps_scale_', 'fps_cscale_').replace('fps_fscale_', 'fps_cfscale_')
            for hva, hname in ((va, name), (pva, pname)):
                if hva is None:
                    continue
                if hva in hooks and hooks[hva][0] != hname:
                    manual.append((site, f'hook {hva:X} conflicts', hname))
                hooks[hva] = (hname, site)

print('# Generated by tools/gen_fps_scale_hooks.py: rescale B4 products (fps_scale_*).')
for va in sorted(hooks):
    print(f'hook {va:08X} {hooks[va][0]}')
print(f'# {len(hooks)} hooks; manual sites:', file=sys.stderr)
for site, why, txt in sorted(manual):
    print(f'{site:X}\t{why}\t{txt}', file=sys.stderr)
