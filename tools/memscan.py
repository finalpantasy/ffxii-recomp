"""memscan.py <pid> <hex pattern>...: print host addresses of each pattern in a
process's committed readable memory, and the guest address when the image
anchor (bytes at a known VA) is found."""
import ctypes, ctypes.wintypes as W, sys
k = ctypes.WinDLL("kernel32", use_last_error=True)
class MBI(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p), ("AllocationProtect", W.DWORD),
                ("PartitionId", W.WORD), ("RegionSize", ctypes.c_size_t), ("State", W.DWORD), ("Protect", W.DWORD), ("Type", W.DWORD)]
pid = int(sys.argv[1]); pats = [bytes.fromhex(p) for p in sys.argv[2:]]
ANCHOR_VA, ANCHOR = 0x234ED0, bytes.fromhex("31c041b97473000045")
h = k.OpenProcess(0x0410, False, pid)
k.VirtualQueryEx.argtypes = [W.HANDLE, ctypes.c_void_p, ctypes.POINTER(MBI), ctypes.c_size_t]
k.ReadProcessMemory.argtypes = [W.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
addr, m, hits, base = 0, MBI(), {p: [] for p in pats}, None
while addr < 1 << 47 and k.VirtualQueryEx(h, addr, ctypes.byref(m), ctypes.sizeof(m)):
    size = m.RegionSize
    if m.State == 0x1000 and m.Protect & 0xEE and not m.Protect & 0x100 and size < 1 << 31:
        buf = ctypes.create_string_buffer(size); n = ctypes.c_size_t()
        if k.ReadProcessMemory(h, addr, buf, size, ctypes.byref(n)):
            d = buf.raw[:n.value]
            i = d.find(ANCHOR)
            if i >= 0 and base is None: base = addr + i - ANCHOR_VA
            for p in pats:
                j = d.find(p)
                while j >= 0 and len(hits[p]) < 20:
                    hits[p].append(addr + j); j = d.find(p, j + 1)
    addr += size
print("guest base", hex(base) if base is not None else None)
for p, l in hits.items():
    print(p.hex()[:24], [hex(x) + (" va " + hex(x - base) if base is not None else "") for x in l])
