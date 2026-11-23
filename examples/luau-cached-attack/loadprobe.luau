-- Logs which save-block functions change record 0's cached attack (+0x36).
local ATK = 0x21664B6
local fns = {0x229b40,0x233c10,0x235af0,0x238a80,0x253970,0x253a80,0x280140,0x2cf940,0x2ef2b0,0x2ef2d0,0x2ef300,0x2ef370,0x2ef390,0x3145e0,0x394130,0x57e310,0x57fe80,0x580cd0,0x580eb0,0x5819f0,0x581b70,0x581da0,0x583c00,0x583cb0,0x583d60,0x584210,0x5843e0,0x5844a0}
for _, va in ipairs(fns) do
  local calls = 0
  ffxii.hook(va, function(cpu, original)
    calls += 1
    local before = ffxii.read_u8(ATK)
    local ret = ffxii.read_u64(cpu.rsp)
    original(cpu)
    local after = ffxii.read_u8(ATK)
    if before ~= after or calls <= 2 then
      ffxii.log(string.format("%06x call %d from %06x: atk %d -> %d", va, calls, ret, before, after))
    end
  end)
end
print("loadprobe loaded")
