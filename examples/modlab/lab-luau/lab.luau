-- Mod lab test mod in Luau (examples/modlab/lab_targets.h).
local DAMAGE, LOOKUP, SCORE, TABLE = 0x1000, 0x2000, 0x3000, 0x10000
local read_u16, ret = ffxii.read_u16, ffxii.ret

ffxii.hook(DAMAGE, function(cpu, original)
  original(cpu)
  cpu.rax = cpu.rax * 2 + 1
end)

ffxii.hook(LOOKUP, function(cpu)
  cpu.rax = read_u16(TABLE + (cpu.rcx % 4096) * 2) * 3 + 7
  ret(cpu)
end)

ffxii.hook(SCORE, function(cpu)
  local x = cpu.rcx
  local sum = 0
  for k = 0, 31 do
    sum += read_u16(TABLE + ((x + 7 * k) % 4096) * 2) * (k + 1)
  end
  cpu.rax = sum
  ret(cpu)
end)
