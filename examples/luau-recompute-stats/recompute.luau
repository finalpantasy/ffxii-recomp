-- Recompute every character's cached equipment stats from the loaded
-- equipment table when a save loads, so old saves pick up equipment stat mods.
-- A save stores these bytes in each character record and battles use them,
-- not the table. Formulas match all records of a vanilla save; see
-- analysis/formats/save.md. Item id - 0x1000 = table index; 0xFFFF = empty.
local PACK_PTR = 0x208E680              -- battle_pack.bin base (global)
local COPIES = { 0x2166480, 0x21B290C } -- the two character tables
local REC, NREC = 0x1C8, 40
local WEAPON, OFFHAND, HEAD, BODY, ACC = 0, 1, 2, 3, 4

-- record offset = sum of table offset over slots, capped at 255
local STATS = {
  { 0x36, "attack",        { { WEAPON, 0x1A }, { OFFHAND, 0x1A } } },
  { 0x37, "defense",       { { HEAD, 0x18 }, { BODY, 0x18 }, { ACC, 0x18 } } },
  { 0x38, "magick resist", { { HEAD, 0x19 }, { BODY, 0x19 }, { ACC, 0x19 } } },
  { 0x3A, "weapon evade",  { { WEAPON, 0x1D } } },
  { 0x3B, "shield evade",  { { OFFHAND, 0x18 } } },
  { 0x3D, "evade",         { { WEAPON, 0x1D }, { OFFHAND, 0x18 } } },
}

ffxii.hook(0x233C10, function(cpu, original)
  local pack = ffxii.read_u64(PACK_PTR)
  if pack == 0 then
    ffxii.log("battle_pack not loaded; cached stats left as saved")
    return original(cpu)
  end
  -- Once loaded, the pack's section table holds absolute addresses, not the
  -- file's offsets (the loader rewrites them).
  local sec = ffxii.read_u32(pack + 4 + 13 * 4)
  if sec < pack then sec = pack + sec end
  local count, size = ffxii.read_u32(sec + 4), ffxii.read_u32(sec + 8)
  if ffxii.read_u32(sec) ~= 0x65327473 or size < 0x1E or count == 0 then -- "st2e"
    ffxii.log("equipment table not found; cached stats left as saved")
    return original(cpu)
  end

  local changed = 0
  for _, base in ipairs(COPIES) do
    for c = 0, NREC - 1 do
      local r = base + c * REC
      if ffxii.read_u16(r + 0x58) ~= 0 or ffxii.read_u8(r + 0x36) ~= 0 then
        local item = {}
        for slot = 0, 4 do
          local id = ffxii.read_u16(r + 0x58 + slot * 2)
          if id ~= 0xFFFF and id >= 0x1000 and id - 0x1000 < count then
            item[slot] = sec + 0x20 + (id - 0x1000) * size
          end
        end
        for _, st in ipairs(STATS) do
          local v = 0
          for _, part in ipairs(st[3]) do
            local a = item[part[1]]
            if a then v += ffxii.read_u8(a + part[2]) end
          end
          if v > 255 then v = 255 end
          if ffxii.read_u8(r + st[1]) ~= v then
            ffxii.write_u8(r + st[1], v)
            changed += 1
          end
        end
      end
    end
  end
  ffxii.log(string.format("cached equipment stats recomputed: %d values changed", changed))
  return original(cpu)
end)
