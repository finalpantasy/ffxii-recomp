-- Example: rewrite every cached Attack Power when a save loads (see analysis/formats/save.md).
-- At 0x233C10 (runs right after a save is copied into the block), set every
-- character's cached Attack Power (+0x36, both tables) to 250.
ffxii.hook(0x233C10, function(cpu, original)
  for i = 0, 39 do
    for _, base in ipairs({0x2166480, 0x21B290C}) do
      local a = base + i * 0x1C8 + 0x36
      if ffxii.read_u8(a) ~= 0 then ffxii.write_u8(a, 250) end
    end
  end
  ffxii.log("cached attack set to 250")
  original(cpu)
end)
