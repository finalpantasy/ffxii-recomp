"""hwwatch.py <pid> <seconds> <host addr>[:len[:rw]] ...  (up to 4)
Attach as a debugger, set hardware breakpoints (DR0-DR3) on the given host
addresses in every thread (writes, or reads and writes with ":rw"), and print
the RIP after each access with the current value, plus the exe's load base.
Detaches after <seconds>. Guest VA = host - 0x20000000000 on the Windows
build; turn a RIP into a lifted function with llvm-nm (RVA = RIP - base, VA =
0x140000000 + RVA). The game can stall while attached. Debugging tool only."""
import ctypes, ctypes.wintypes as W, sys, time, struct
k = ctypes.WinDLL("kernel32", use_last_error=True)
pid, secs = int(sys.argv[1]), float(sys.argv[2])
watch = []
for a in sys.argv[3:7]:
    parts = a.split(":")
    watch.append((int(parts[0], 16), int(parts[1]) if len(parts) > 1 else 1, parts[2] if len(parts) > 2 else "w"))
LEN = {1: 0, 2: 1, 8: 2, 4: 3}
dr7 = 0
for i, (_, ln, mode) in enumerate(watch):
    dr7 |= 1 << (2 * i)
    dr7 |= ((0b11 if mode == "rw" else 0b01) | (LEN[ln] << 2)) << (16 + 4 * i)
class DEBUG_EVENT(ctypes.Structure):
    _fields_ = [("code", W.DWORD), ("pid", W.DWORD), ("tid", W.DWORD), ("u", ctypes.c_byte * 164)]
k.WaitForDebugEvent.argtypes = [ctypes.POINTER(DEBUG_EVENT), W.DWORD]
k.OpenThread.restype = W.HANDLE
k.GetThreadContext.argtypes = [W.HANDLE, ctypes.c_void_p]
k.SetThreadContext.argtypes = [W.HANDLE, ctypes.c_void_p]
k.ReadProcessMemory.argtypes = [W.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
raw = ctypes.create_string_buffer(1232 + 16)
ctx = (ctypes.addressof(raw) + 15) & ~15
def cx(off, fmt="<Q", val=None):
    if val is None:
        return struct.unpack(fmt, ctypes.string_at(ctx + off, struct.calcsize(fmt)))[0]
    ctypes.memmove(ctx + off, struct.pack(fmt, val), struct.calcsize(fmt))
threads = {}
def set_dr(tid, on):
    h = threads.get(tid) or k.OpenThread(0x001A, False, tid)  # get/set context, suspend
    if not h: return
    threads[tid] = h
    cx(0x30, "<I", 0x100010)
    if not k.GetThreadContext(h, ctx): return
    for i in range(4):
        cx(0x48 + 8 * i, val=(watch[i][0] if on and i < len(watch) else 0))
    cx(0x68, val=0); cx(0x70, val=(dr7 if on else 0))
    k.SetThreadContext(h, ctx)
if not k.DebugActiveProcess(pid): sys.exit(f"attach failed {ctypes.get_last_error()}")
k.DebugSetProcessKillOnExit(False)
hp = k.OpenProcess(0x0410, False, pid)
ev, end, hits = DEBUG_EVENT(), time.time() + secs, 0
while time.time() < end:
    if not k.WaitForDebugEvent(ctypes.byref(ev), 200): continue
    cont = 0x00010002  # DBG_CONTINUE
    if ev.code in (2, 3):  # CREATE_THREAD, CREATE_PROCESS
        if ev.code == 3:
            print("image base", hex(struct.unpack_from("<Q", bytes(ev.u), 28)[0]), flush=True)
        set_dr(ev.tid, True)
    elif ev.code == 1:  # EXCEPTION
        exc = struct.unpack_from("<I", bytes(ev.u), 4)[0]
        if exc == 0x80000004:  # single step: our breakpoint
            h = threads.get(ev.tid) or k.OpenThread(0x001A, False, ev.tid); threads[ev.tid] = h
            cx(0x30, "<I", 0x100011); k.GetThreadContext(h, ctx)
            rip, dr6 = cx(0xF8), cx(0x68)
            vals = []
            for i, (a, ln, _m) in enumerate(watch):
                if dr6 & (1 << i):
                    b = ctypes.create_string_buffer(ln); n = ctypes.c_size_t()
                    k.ReadProcessMemory(hp, a, b, ln, ctypes.byref(n)); vals.append((i, hex(a), b.raw.hex()))
            print(f"{time.time()-end+secs:7.2f}s tid {ev.tid} rip {rip:#x} {vals}", flush=True)
            cx(0x68, val=0); k.SetThreadContext(h, ctx)
            hits += 1
        elif exc == 0x80000003:  # attach breakpoint
            pass
        else:
            cont = 0x80010001  # DBG_EXCEPTION_NOT_HANDLED
    k.ContinueDebugEvent(ev.pid, ev.tid, cont)
for tid in list(threads): set_dr(tid, False)
k.DebugActiveProcessStop(pid)
print("detached, hits", hits)
