"""poke.py <pid> <host addr hex> <stride hex> <count> <byte>: write one byte at
addr + i*stride for i < count, printing the old values. Debugging tool only."""
import ctypes, ctypes.wintypes as W, sys
k = ctypes.WinDLL("kernel32", use_last_error=True)
k.ReadProcessMemory.argtypes = k.WriteProcessMemory.argtypes = [W.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
pid, addr, stride, count, val = int(sys.argv[1]), int(sys.argv[2], 16), int(sys.argv[3], 16), int(sys.argv[4]), int(sys.argv[5])
h = k.OpenProcess(0x0438, False, pid)
import time
for _ in range(600):  # wait up to 5 min for the first record to be filled
    b = ctypes.create_string_buffer(1); n = ctypes.c_size_t()
    k.ReadProcessMemory(h, addr, b, 1, ctypes.byref(n))
    if b.raw[0]: break
    time.sleep(0.5)
print("filled at", time.strftime("%T"))
old = []
for i in range(count):
    b = ctypes.create_string_buffer(1); n = ctypes.c_size_t()
    k.ReadProcessMemory(h, addr + i * stride, b, 1, ctypes.byref(n)); old.append(b.raw[0])
    if b.raw[0]: k.WriteProcessMemory(h, addr + i * stride, bytes([val]), 1, ctypes.byref(n))
print("old", old)
