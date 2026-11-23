# Save files

Findings from 2026-11-23 (Windows build, Steam saves and saves made by the recomp).

## File

`FFXII_<nnn>` in `My Games/FINAL FANTASY XII THE ZODIAC AGE/<id>/`. The file
is 12 bytes of header followed by RC4 ciphertext (CryptoAPI, see the
`RSA_SQEX_FFXII_TZA` notes in AGENTS.md). The game decrypts it in blocks of
1,000 bytes. The plaintext is one zlib stream (`78 da`) that inflates to
353,936 bytes (0x56690).

`RECOMP_CRYPT_DUMP=<dir>` (SwARc `advapi32_crypt.cpp`) writes the plaintext
of every CryptEncrypt/CryptDecrypt call to `<dir>/crypt-<n>-<enc|dec>-<len>.bin`.
Concatenate the blocks of one file and inflate them to get the image. Loading
from the title decrypts each save twice (once for the list, once to load).

## In memory

The inflated image is a copy of one guest block: VA 0x2164280, 0x56690 bytes,
so save offset `o` is VA `0x2164280 + o`.

- 0x2EF640 returns the block address; 28 functions call it.
- 0x2EF5F0 clears the block (memset, called at New Game).
- 0x2EF570 copies the whole block to 0x22676C0.
- 0x2EF4A0 copies 0x6000 bytes from 0x2166480 to 0x21B290C, the second
  character table below (both copies are in the save).

## Character table

40 records of 0x1C8 bytes at save offset 0x2200 (VA 0x2166480), and a copy at
save offset 0x4E68C (VA 0x21B290C). Records 0-26 hold the party and guests,
27-39 larger stats (later or guest characters). Known field:

| Offset | Type | Meaning |
|---|---|---|
| +0x36 | u8 | Attack Power: weapon + off-hand attack (+0x1A in section 13), capped at 255 |
| +0x37 | u8 | Defense: head + body + accessory +0x18 |
| +0x38 | u8 | Magick Resist: head + body + accessory +0x19 |
| +0x3A | u8 | Weapon evade: weapon +0x1D |
| +0x3B | u8 | Shield evade: off-hand +0x18 |
| +0x3D | u8 | Evade: weapon +0x1D + off-hand +0x18 |
| +0x58 | u16 x5 | Equipped item ids: weapon, off-hand, head, body, accessory (0xFFFF empty) |

Evidence: a New Game started under a mod that sets every weapon's attack
(battle_pack section 13 +0x1A) to 250 saved 250 (255 for three records) here;
the same point saved without the mod has 14, 20, 16 and so on. Nothing else in
the records differs between those two saves except three counters (+0x10,
+0x2C, +0x50).

The menus do not read this byte: an old save loaded with the mod shows Attack
Power 250 on the Equip screen. Battles do. With no mod at all, `tools/hwwatch.py`
stopped the game at the load's copy into the block (write breakpoints on the
last record of each table) and wrote 250 into +0x36 of all 80 records before it
went on; Reks's first hit was 883, against 39-43 without the write
(`win-hwpoke1.log`). Writing the same bytes half a second after the copy
(`tools/poke.py`, `win-poke2.log`, `win-poke3.log`) changed nothing, so the
game builds its battle actors from this byte during the load itself. During the
first fight nothing read the Broadsword's attack in the loaded equipment table
(read watch, `win-wwatch1.log`).

So the save keeps each character's attack as computed when it was last set
(New Game here), battles use that copy, and a table mod reaches an old save's
battles only when something recomputes the byte. A mod can rewrite the byte at load. 0x233C10 is the first save-system function
that runs after the load copies the block in (a Luau probe hooked all 28
functions that call the block getter 0x2EF640 and logged the byte before and
after each; `examples/luau-cached-attack/loadprobe.luau`). A Luau hook on
0x233C10 that sets +0x36 of every record to 250 before calling the original
made Reks's first hit 949 on the old save, with no table mod
(`examples/luau-cached-attack/atk.luau`, `win-lhook2.log`).

The cached byte is min(255, weapon +0x1A + off-hand +0x1A), from battle_pack
section 13. The record names the weapon at +0x58 and the off-hand at +0x5A (u16
item ids, 0xFFFF empty; table index = id - 0x1000). Ammunition in the off-hand
adds its own attack (1 for the early arrows and shot); shields add 0. The formula
matches all 80 records of the Inner Ward save, and in game a mod recomputing it
on vanilla data changed 0 values (`win-recomp4.log`).

The other equipment bytes above follow the same rule (sums of section 13 bytes
over the slots named, capped at 255; every record of the Inner Ward save
matches). Armor records keep Defense at +0x18 and Magick Resist at +0x19, and
shields keep evasion at +0x18. +0x32-0x35 and +0x30 do not come from equipment.
With all six stats recomputed, vanilla data changed 0 values (`win-stat1.log`).
A table mod setting body-armor Defense to 200 (73 records) changed 58 values,
and Reks stayed at 133/133 HP through the first fights (`win-def1.log`), where
the earlier run had him at 122. No enemy hit number was captured, so the
defense effect is likely but not measured.

The loaded battle_pack's base is the global 0x208E680 (set by 0x2368D0, read in 45
places). Once loaded, its section table holds absolute addresses instead of
file offsets, so section 13 is `u32 [base + 0x38]`. It is already loaded when
0x233C10 runs. `examples/luau-recompute-stats` recomputes the byte there: with
the attack-250 table mod, Reks hit for 904 on the old save (`win-recomp3.log`),
where the table mod alone left his hits at 39-43.

## Tools

- `tools/memscan.py <pid> <hex>...`: find byte patterns in the running
  process (host addresses; the guest base on the Windows build is
  0x20000000000).
- `tools/poke.py <pid> <host addr> <stride> <count> <byte>`: wait until the
  first byte is non-zero, then overwrite one byte per record.
- `tools/hwwatch.py`: hardware breakpoints on up to four host addresses;
  `HWWATCH_POKE` writes bytes while the game is stopped at a hit.

All three use the Win32 debugging and memory APIs on a process you started.
