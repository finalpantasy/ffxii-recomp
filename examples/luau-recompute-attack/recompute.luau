-- Recompute every character's cached Attack Power (+0x36) from the loaded
-- equipment table when a save loads, so old saves pick up weapon stat mods.
-- Formula (matches all 80 records of a vanilla save):
--   min(255, weapon[0x1A] + offhand[0x1A]), item id - 0x1000 = table index.
-- See analysis/formats/save.md.
local PACK_PTR = 0x208E680            -- battle_pack.bin base (global)
local COPIES = { 0x2166480, 0x21B290C } -- the two character tables
local REC, NREC = 0x1C8, 40

local function item_attack(sec, count, size, id)
  if id == 0xFFFF or id < 0x1000 then return 0 end
  local i = id - 0x1000
  if i >= count then return 0 end
  return ffxii.read_u8(sec + 0x20 + i * size + 0x1A)
end

ffxii.hook(0x233C10, function(cpu, original)
  local pack = ffxii.read_u64(PACK_PTR)
  if pack == 0 then
    ffxii.log("battle_pack not loaded; cached attack left as saved")
    return original(cpu)
  end
  -- Once loaded, the pack's section table holds absolute addresses, not the
  -- file's offsets (the loader rewrites them).
  local sec = ffxii.read_u32(pack + 4 + 13 * 4)
  if sec < pack then sec = pack + sec end
  local count, size = ffxii.read_u32(sec + 4), ffxii.read_u32(sec + 8)
  if ffxii.read_u32(sec) ~= 0x65327473 or size < 0x1B or count == 0 then -- "st2e"
    ffxii.log("equipment table not found; cached attack left as saved")
    return original(cpu)
  end
  local changed = 0
  for _, base in ipairs(COPIES) do
    for c = 0, NREC - 1 do
      local r = base + c * REC
      local old = ffxii.read_u8(r + 0x36)
      if old ~= 0 then
        local v = item_attack(sec, count, size, ffxii.read_u16(r + 0x58))
                + item_attack(sec, count, size, ffxii.read_u16(r + 0x5A))
        if v > 255 then v = 255 end
        if v ~= old then ffxii.write_u8(r + 0x36, v); changed += 1 end
      end
    end
  end
  ffxii.log(string.format("cached attack recomputed: %d values changed", changed))
  return original(cpu)
end)
