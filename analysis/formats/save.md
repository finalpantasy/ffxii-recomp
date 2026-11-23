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
| +0x36 | u8 | Attack Power with the current weapon, capped at 255 |

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
battles only when something recomputes the byte. A mod that wants old saves to
pick up new weapon stats can recompute +0x36 from the equipment table right
after the load copies the block in, before the actors are built. The function
that does that copy is not identified yet (the copy itself runs in a host
memcpy).

## Tools

- `tools/memscan.py <pid> <hex>...`: find byte patterns in the running
  process (host addresses; the guest base on the Windows build is
  0x20000000000).
- `tools/poke.py <pid> <host addr> <stride> <count> <byte>`: wait until the
  first byte is non-zero, then overwrite one byte per record.
- `tools/hwwatch.py`: hardware breakpoints on up to four host addresses;
  `HWWATCH_POKE` writes bytes while the game is stopped at a hit.

All three use the Win32 debugging and memory APIs on a process you started.
