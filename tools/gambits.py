"""gambits.py <battle_pack.bin> <bp.json>: print every gambit (section 7) with its three cases and name."""
import struct, sys, json
d = open(sys.argv[1], 'rb').read()
n = struct.unpack_from('<I', d, 0)[0]; offs = struct.unpack_from('<%dI' % (n + 1), d, 4)
def sec(i): return d[offs[i]:offs[i + 1]]
s = sec(7)
cnt, = struct.unpack_from('<I', s, 4); size, = struct.unpack_from('<H', s, 8); eo, = struct.unpack_from('<I', s, 0xC)
words = json.load(open(sys.argv[2]))['sections'][2]['tables'][6]
print('count', cnt, 'size', hex(size), 'entries at', hex(eo))
for i in range(cnt):
    r = s[eo + i * size: eo + (i + 1) * size]
    icon, desc, gil, c1, c2, c3, flags, t1, t2, t3, name, page, order, p1, p2, p3 = struct.unpack_from('<3xBHHHHHHBBBxHBBHHH', r)
    name -= 0x3000; nm = words[name].get('text', '').replace('{00}{00}', '').replace('the gambit ', '') if name < len(words) else f'?{name}'
    print(f'{i:3} name={name:3} {nm:42} cases=({c1:#06x},{t1},{p1:#06x}) ({c2:#06x},{t2},{p2:#06x}) ({c3:#06x},{t3},{p3:#06x}) fl={flags:#06x} page={page} ord={order} gil={gil}')
