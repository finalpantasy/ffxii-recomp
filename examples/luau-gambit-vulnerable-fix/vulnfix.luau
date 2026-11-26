-- "Foe: <element>-vulnerable" (condition 0x17nn) is the negated form of test 0x16,
-- which checks bit n of the target's Absorb mask (stat block +0x41). So the game
-- picks any foe that does not absorb the element, including foes immune to it.
-- The help text says "susceptible to <element> damage". This mod ORs each actor's
-- Immune mask (+0x43) into its Absorb mask for the length of one 0x17 test, then
-- restores the bytes, so the gambit skips foes that absorb or nullify the element.
-- Masks (analysis/formats/gambits.md): +0x40 weak, +0x41 absorb, +0x42 half,
-- +0x43 immune, +0x44 potency.
local TESTER = 0x301AD0   -- (self, entry, ?, case index, out): one condition case
local COUNT, ACTOR = 0x236850, 0x321170
local fired = 0
ffxii.hook(TESTER, function(cpu, original, self, entry, a3, case)
  case = bit32.band(case, 0xFFFFFFFF)
  local code = ffxii.read_u8(entry + case * 6 + 0x09)
  if code ~= 0x17 then
    return original(cpu)
  end
  local saved = {}
  local n = bit32.band(ffxii.call(cpu, COUNT), 0xFFFFFFFF)
  for i = 0, math.min(n, 64) - 1 do
    local a = ffxii.call(cpu, ACTOR, i)
    if a ~= 0 then
      local s = ffxii.read_u64(a + 0x698)
      if s ~= 0 then
        local absorb = ffxii.read_u8(s + 0x41)
        local immune = ffxii.read_u8(s + 0x43)
        if bit32.band(immune, bit32.bnot(absorb)) ~= 0 then
          saved[#saved + 1] = {s, absorb}
          ffxii.write_u8(s + 0x41, bit32.bor(absorb, immune))
        end
      end
    end
  end
  local r = original(cpu)
  for _, e in ipairs(saved) do ffxii.write_u8(e[1] + 0x41, e[2]) end
  fired += 1
  if fired == 1 or fired % 1000 == 0 then
    ffxii.log(string.format("vulnerable test %d (element %d, %d immune actors masked)",
      fired, ffxii.read_u8(entry + case * 6 + 0x08), #saved))
  end
  return r
end)
