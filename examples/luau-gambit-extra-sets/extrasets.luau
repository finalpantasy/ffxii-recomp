-- 36 gambit slots without menu changes: when no gambit in a character's active set
-- fires, try the character's other two sets (12 slots each, edited in the menu as
-- usual), lowest set number first. Same idea as The Insurgent's Manifesto.
--
-- How (analysis/formats/gambits.md): the evaluator 0x305CC0(actor, list, group, flag)
-- walks a list that 0x327DE0 builds at 0x2089378 + character * 0x170 (count byte at
-- +0, 0x18-byte entries from +0x20). For each extra set this hook saves the list,
-- writes that set's enabled slots into it the same way, runs the evaluator again and
-- restores the list.
local EVAL = 0x305CC0
local LISTS, LSTRIDE, NCHAR = 0x2089378, 0x170, 16
local TABLE_PTR, ADDR_OF = 0x2EBF0D8, 0x20E600   -- gambit table header, offset -> address
local SAVE_PTR = 0x2EBF190                       -- sets at +0x5B10, active set index at +0x7370
local busy = false
local used = 0

local function gambit_record(cpu, id)
  local hdr = ffxii.read_u64(TABLE_PTR)
  if hdr == 0 then return 0 end
  local base = ffxii.call(cpu, ADDR_OF, ffxii.read_u32(hdr + 0x0C))
  if base == 0 then return 0 end
  return base + (id - 0x6000) * ffxii.read_u16(hdr + 0x08)
end

-- Write set `set` of character `ch` into list `list`; returns the entry count.
local function build(cpu, list, ch, set)
  local save = ffxii.read_u64(SAVE_PTR)
  local s = save + 0x5B10 + (set + ch * 3) * 0x34
  local mask = ffxii.read_u16(s + 0x30)
  local n = 0
  for i = 0, 11 do
    local cond, act = ffxii.read_u16(s + i * 4), ffxii.read_u16(s + i * 4 + 2)
    if bit32.band(mask, bit32.lshift(1, i)) ~= 0 and cond ~= 0xFFFF and act ~= 0xFFFF and n < 14 then
      local g = gambit_record(cpu, cond)
      if g ~= 0 then
        local e = list + 0x20 + n * 0x18
        ffxii.write_u16(e + 0x00, act)
        ffxii.write_u16(e + 0x04, ffxii.read_u16(g + 0x0E))
        for c = 0, 2 do
          ffxii.write_u16(e + 0x06 + c * 6, ffxii.read_u8(g + 0x10 + c))
          ffxii.write_u16(e + 0x08 + c * 6, ffxii.read_u16(g + 0x08 + c * 2))
          ffxii.write_u16(e + 0x0A + c * 6, ffxii.read_u16(g + 0x18 + c * 2))
        end
        n += 1
      end
    end
  end
  ffxii.write_u8(list, n)
  return n
end

ffxii.hook(EVAL, function(cpu, original, actor, list, group, flag)
  local off = list - LISTS
  if busy or group ~= 0 or off < 0 or off >= LSTRIDE * NCHAR or off % LSTRIDE ~= 0 then
    return original(cpu)
  end
  busy = true
  local r = bit32.band(ffxii.call(cpu, EVAL, actor, list, group, flag), 0xFFFFFFFF)
  if r == 0 then
    local ch = off // LSTRIDE
    local active = ffxii.read_i32(ffxii.read_u64(SAVE_PTR) + 0x7370 + ch * 4)
    local keep = {}
    for k = 0, LSTRIDE // 4 - 1 do keep[k] = ffxii.read_u32(list + k * 4) end
    for set = 0, 2 do
      if set ~= active and r == 0 and build(cpu, list, ch, set) > 0 then
        r = bit32.band(ffxii.call(cpu, EVAL, actor, list, group, flag), 0xFFFFFFFF)
        if r ~= 0 then
          used += 1
          if used == 1 or used % 500 == 0 then
            ffxii.log(string.format("character %d acted from set %d (%d times)", ch, set + 1, used))
          end
        end
      end
    end
    for k = 0, LSTRIDE // 4 - 1 do ffxii.write_u32(list + k * 4, keep[k]) end
  end
  busy = false
  cpu.rax = r
  return ffxii.ret(cpu)
end)
