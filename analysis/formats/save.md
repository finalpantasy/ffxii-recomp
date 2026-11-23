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
Power 250 on the Equip screen. Battles did not read it either, at least not
after load: writing 250 into both tables right after the load (`tools/poke.py`)
left the first hit at 39, the same as without the write (`win-poke2.log`,
`win-poke3.log`). A read watch on the byte during load (`tools/hwwatch.py`)
caught only the two block copies (f_002EF570) and f_00583CB0 at +0x1DD; with
the debugger attached the game then stopped before the field.

What builds a battle actor's attack, and whether an equip change refreshes it
on an old save, is still open.

## Tools

- `tools/memscan.py <pid> <hex>...`: find byte patterns in the running
  process (host addresses; the guest base on the Windows build is
  0x20000000000).
- `tools/poke.py <pid> <host addr> <stride> <count> <byte>`: wait until the
  first byte is non-zero, then overwrite one byte per record.
- `tools/hwwatch.py`: hardware breakpoints on up to four host addresses.

All three use the Win32 debugging and memory APIs on a process you started.
