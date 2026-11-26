# Gambits

Findings from 2026-11-25 (Windows build, Steam save FFXII_025).

## Data

battle_pack section 7 is the gambit table: 256 st2e records of 0x20 bytes
(layout from Xeavin's MIT-licensed The-Insurgents-Workshop, `Gambits.cs`). Each
record has an icon, description and name ids, a gil cost, menu page and order,
and up to three cases. A case is a condition (u16), a target type (u8: 1 foe,
2 ally, 4 self) and a parameter (u16). The name is `0x3000 + index` into word
table 6 of section 61 (284 names; 28 of them have no gambit record).
`tools/gambits.py <battle_pack.bin> <ff12text dump json>` prints the table.

The condition's high byte is the test and the low byte its argument. Examples:
`0x0Fnn` HP below (nn 0-8 = 90% down to 10%), `0x0Enn` HP at or above,
`0x3Fnn` MP below (nn = tenths: 9 = 90%), `0x06nn` status, `0x14nn`
element-weak, `0x17nn` element-vulnerable, `0x2Ann`/`0x2Bnn` HP above/below a
fixed amount, `0x3A01` foe count (parameter = count), `0x3800` nearest visible
foe. Within each family the arguments match the names, so no table entry is
mislabeled. Multi-case gambits ("Foe: character HP < 50%") chain a foe pick with
a test on the character.

Section 8 holds the default gambits (0x40 bytes: 12 condition ids, 8 bytes, 12
action ids, 8 bytes); section 16's party records link to it (`link + 0x5000`).

## Save and memory

Active gambits sit in the character record (save.md) at +0x90: 12 slots of
(condition u16 = `0x6000 + gambit index`, action u16), 0xFFFF empty. +0xC0 is
the u16 on/off mask and +0xC2 the number of slots the licenses unlock. The three
gambit sets are at save offset 0x7D10 (VA 0x2169F90), 0x9C bytes per character:
three blocks of 0x34 (12 slots, mask, count); the active set is copied into the
record. Both have mirrors (0x21B299C for the records, 0x216BF90 area and
0x5419C in the save for the sets) and two more copies near VA 0x2ED9A10.

0x327CB0(record + 8, link) fills the slots from section 8 (loop of 12, mask
bit per filled slot, count byte) and calls 0x327DE0. The gambit table header
pointer is the global 0x2EBF0D8 (set by 0x238880 from `[pack + 0x20]`); its
readers are 0x31C5D0, 0x322C50 and 0x327A10-0x328140.

## Not found yet

The evaluator. Hardware read watches on slot 0 of the party records, the set
storage and the far copies saw no per-tick reads in four runs, but none of
those runs had a fight (Ozmone Plain and Tomb of Raithwall saves, roaming
script). The set storage was read once in a burst by 0x229B40 and 0x3BB220, and
the far copies only by the CRT copy at 0x8666B0. Next: a save and script that
reach a fight, then the same watch. The 263- and 227-case switches at 0x1CD1D5
and 0x1D16AE are a debug menu, and the 100-case switch at 0x25E5E8 is a
bytecode interpreter (AI scripts); none of them is the evaluator.

## Known gambit problems to check

- Element-weak/vulnerable conditions (`0x14nn`, `0x17nn`) reported as not
  firing since the PS2 International version; FFXII Ultima Edition (Nexus 452)
  lists a fix. The table data is consistent, so the fault is in code.
- Steal gambits repeat on foes with nothing left to steal (The Insurgent's
  Manifesto, Nexus 218, "Smart Stealing Gambits").
- The Insurgent's Manifesto makes 36 slots by evaluating sets 2 and 3 after
  set 1; the same layout above allows that here without menu changes.
