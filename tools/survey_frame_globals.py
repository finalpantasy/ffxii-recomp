# Survey of code that reads the frame-timing globals 0x1DFE0B4 (int), 0x1DFE0B8 (float), 0x1DFE0BC (float).
# Usage: python tools/survey_frame_globals.py <FFXII_TZA.exe> analysis/maps/functions.tsv <out.tsv>
# Linear capstone disassembly per function range; for each RIP-relative access the first use of the loaded value is classified.
import sys,struct,re,capstone
from capstone import x86
d=open(sys.argv[1],'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]; n=struct.unpack_from('<H',d,pe+6)[0]; oh=struct.unpack_from('<H',d,pe+20)[0]
base=struct.unpack_from('<Q',d,pe+24+24)[0]
secs=[]
for i in range(n):
    o=pe+24+oh+40*i; vs,va,rs,ra=struct.unpack_from('<IIII',d,o+8); secs.append((va,vs,ra,rs))
def v2f(v):
    v-=base
    for va,vs,ra,rs in secs:
        if va<=v<va+rs: return ra+v-va
def rd(v,n): f=v2f(v); return d[f:f+n]
G={0x1DFE0B4:'B4',0x1DFE0B8:'B8',0x1DFE0BC:'BC'}
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64); md.detail=True
fam={}
for grp in [('rax','eax','ax','al'),('rbx','ebx','bx','bl'),('rcx','ecx','cx','cl'),('rdx','edx','dx','dl'),('rsi','esi','si','sil'),('rdi','edi','di','dil')]+[(f'r{k}',f'r{k}d',f'r{k}w',f'r{k}b') for k in range(8,16)]:
    for r in grp: fam[r]=grp[0]
for k in range(16): fam[f'xmm{k}']=f'xmm{k}'
def rn(i,r): 
    s=i.reg_name(r); return fam.get(s,s)
funcs=[]
for ln in open(sys.argv[2]):
    p=ln.split('\t')
    if p[0]=='entry': continue
    funcs.append((int(p[0],16),int(p[1],16),int(p[2],16)))
out=open(sys.argv[3],'w')
out.write('func\tva\tglobal\tinsn\tuse\tuse_insn\n')
for e,lo,hi in funcs:
    code=rd(lo,hi-lo+1)
    if not code: continue
    ins=list(md.disasm(code,lo))
    for k,i in enumerate(ins):
        m=re.search(r'rip ([+-]) 0x([0-9a-f]+)',i.op_str)
        if not m: continue
        t=i.address+i.size+(1 if m.group(1)=='+' else -1)*int(m.group(2),16)
        g=None
        for a,nm in G.items():
            if a<=t<a+4: g=nm
        if not g: continue
        mn=i.mnemonic; ops=i.operands
        use='?'; ui=''
        memfirst = ops and ops[0].type==x86.X86_OP_MEM
        if mn in('idiv','div'): use='divisor(mem)'
        elif memfirst and mn.startswith('mov'): use='WRITE'
        elif mn in('cmp',) and memfirst and len(ops)>1 and ops[1].type==x86.X86_OP_IMM: use=f'cmp_imm {ops[1].imm}'
        elif mn=='cmp': use='cmp_reg'
        elif mn.startswith('imul') or mn.startswith('mul'): use='mul(mem)'
        elif mn in('add','sub') and not memfirst: use=f'{mn}(mem)'
        elif mn.startswith(('mulss','mulsd','divss','divsd','addss','subss','comiss','ucomiss','cvt','movss','movd','maxss','minss')) and not memfirst:
            if mn.startswith(('movss','movd','movsx','movzx','cvt')): pass
            else: use=f'{mn}(mem)'
        if use=='?':
            # register load: follow destination
            if ops and ops[0].type==x86.X86_OP_REG:
                r=rn(i,ops[0].reg)
                for j in ins[k+1:k+16]:
                    rr,ww=j.regs_access()
                    rr=[rn(j,x) for x in rr]; ww=[rn(j,x) for x in ww]
                    jm=j.mnemonic
                    if r in rr:
                        jo=j.operands
                        if jm in('idiv','div'):
                            use='divisor' if (jo[0].type==x86.X86_OP_REG and rn(j,jo[0].reg)==r) else 'dividend'
                        elif jm=='cdq' or jm=='cqo': use='dividend'
                        elif jm.startswith(('imul','mul')): use='mul'
                        elif jm in('cmp','test'): use='cmp'
                        elif jm.startswith('cvtsi2') : use='to_float'
                        elif jm in('add','sub','adc','sbb','inc','dec','neg','lea'): use=jm
                        elif jm in('shl','sar','shr','sal'): use='shift'
                        elif jm in('and','or','xor'): use=jm
                        elif jm.startswith('mov') and jo[0].type==x86.X86_OP_MEM: use='store'
                        elif jm.startswith('mov') or jm.startswith('cmov'): use='copy:'+jm
                        elif jm.startswith(('mulss','mulsd','mulps')): use='fmul'
                        elif jm.startswith(('divss','divsd','divps')):
                            use='fdivisor' if rn(j,jo[1].reg if jo[1].type==x86.X86_OP_REG else 0)==r else 'fdividend'
                        elif jm.startswith(('addss','subss','addsd','subsd')): use='fadd'
                        elif jm.startswith(('comiss','ucomiss','comisd','ucomisd')): use='fcmp'
                        elif jm=='call': use='arg'
                        else: use='other:'+jm
                        ui=f'{j.address:x} {jm} {j.op_str}'
                        break
                    if r in ww: use='overwritten'; break
                    if jm in('ret','jmp') : use='call/ret' if jm=='ret' else 'jmp'; break
                    if jm=='call': use='call_before_use'; break
        out.write(f'{e:x}\t{i.address:x}\t{g}\t{mn} {i.op_str}\t{use}\t{ui}\n')
