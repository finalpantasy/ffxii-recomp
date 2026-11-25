-- After a save loads, on the first field frame, run the game's equipment stat
-- function (0x2FEF30, the one the Equip screen calls) on every record of the
-- character table the Equip screen updates (0x21B290C). The game's record
-- pointer is the save.md record + 8 (equipment at its +0x50 = save.md +0x58).
-- Caveat: on the old Steam save with no mods this changed 4 of 40 records
-- (33 -> 32, 19 -> 14, 12 -> 14, 20 -> 15; win-grc6.log, and the same through
-- 0x30FED0 in win-grc7.log), so its result differs from the saved bytes there.
-- examples/luau-recompute-stats (the save.md formula) matched every vanilla record.
local BASE, REC, NREC = 0x21B290C, 0x1C8, 40
local pending = false
ffxii.hook(0x233C10, function(cpu, original)
  pending = true
  return original(cpu)
end)
ffxii.hook(0x22A770, function(cpu, original)
  if pending then
    pending = false
    local changed = 0
    for i = 0, NREC - 1 do
      local r = BASE + i * REC
      if ffxii.read_u16(r + 0x58) ~= 0xFFFF or i < 27 then
        local before = ffxii.read_u8(r + 0x36)
        ffxii.call(cpu, 0x2FEF30, r + 8, 0, 0)
        local after = ffxii.read_u8(r + 0x36)
        if before ~= after then
          changed += 1
          if changed <= 8 then
            ffxii.log(string.format("rec %d attack %d -> %d", i, before, after))
          end
        end
      end
    end
    ffxii.log("records changed: " .. changed)
  end
  return original(cpu)
end)
