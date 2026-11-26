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

## Evaluator (found 2026-11-25, static reading)

Battle code never reads the slots or section 7 per tick. 0x327DE0 expands a
character's active set into a list at VA 0x2089378 + character × 0x170: a
header of count bytes (byte 0 is the gambit count), then from +0x20 entries of
0x18 bytes, one per enabled slot:

| Entry offset | Copied from |
|---|---|
| +0x00 | action (slot word 2) |
| +0x04 | flags (record +0x0E) |
| +0x06, +0x0C, +0x12 | target types 1-3 (record +0x10..+0x12) |
| +0x08, +0x0E, +0x14 | conditions 1-3 (record +0x08..+0x0C) |
| +0x0A, +0x10, +0x16 | parameters 1-3 (record +0x18..+0x1C) |

0x23A570 builds a battle actor and stores that list's address at actor +0x6E0
(+0x6F0 gets a fallback list at 0x1E09350). 0x305AB0 calls the evaluator
**0x305CC0(actor, list, group, flag)**, first with +0x6E0 and then with +0x6F0.
0x305CC0 skips the counts of earlier groups, then for each entry calls 0x3014C0
(conditions and target; it calls 0x301AD0 per case) and, if that passes,
0x30F760 (try the action). The first entry whose action starts wins. With only
0x150 bytes of entries a list holds at most 14.

0x301AD0 switches on the condition's high byte (0x00-0x45; table bytes at
0x1E3680, targets at 0x1E3604). Codes come in pairs: the even code requires the
test to be true, the odd code requires it false (`r12 = ~code & 1`). The test
reads the target's stat block at actor +0x698. Element tests:

- 0x14/0x15: bit n of stat +0x40. 0x14nn is "Foe: <element>-weak".
- 0x16/0x17: bit n of stat +0x41. 0x17nn is "Foe: <element>-vulnerable" (help
  text: "susceptible to fire damage"), so it picks foes where the bit is clear.

Stat +0x40..+0x44 are five element masks, copied from the actor's base record
(+0x6A0, bytes +0x78..+0x7C) by 0x30FED0. Their order differs from the
equipment attribute records (section 13 after the items, 0x18 bytes each, masks
at +0x10..+0x14 in the order Absorb, Immune, Half, Weak, Potency; ff12doc
`eitm_data.bin.md`). A probe gave every attribute record a different bit per
mask (+0x10 bit 0, +0x11 bit 1, +0x12 bit 2, +0x13 bit 3, +0x14 bit 4), ran the
game's stat function 0x2FEF30 on the party, and read the block
(`win-gw16.log`): +0x40 = 08, +0x41 = 01, +0x42 = 04, +0x43 = 02, +0x44 = 10. So:

| Stat offset | Mask |
|---|---|
| +0x40 | Weak |
| +0x41 | Absorb |
| +0x42 | Half |
| +0x43 | Immune |
| +0x44 | Potency |

"Foe: X-weak" (0x14) therefore tests the Weak mask, which is correct.
"Foe: X-vulnerable" (0x17) is "does not absorb X", so it also picks foes that
are immune to X. `examples/luau-gambit-vulnerable-fix` ORs the Immune mask into
the Absorb mask of every actor for the length of one 0x17 test and restores it.
After loading, the section 13 header's attribute pointer (+0x18) is absolute,
like the section table.

The 263- and 227-case switches at 0x1CD1D5 and 0x1D16AE are a debug menu, and
the 100-case switch at 0x25E5E8 is a bytecode interpreter (AI scripts). 0x322C50
is a generic id range check over 32 tables (type 0xB is the gambit table).

## Mods

- `examples/luau-gambit-vulnerable-fix`: "Foe: X-vulnerable" also skips foes
  immune to X (see the mask table above). It loads and runs with no errors
  (`win-gfix1.log`), but the test saves have no vulnerable gambit, so a battle
  with one is still unverified.
- `examples/luau-gambit-extra-sets`: 36 slots. When 0x305CC0 finds nothing in
  a character's active list, the hook writes each other set (lowest number
  first) into the same list, runs the evaluator again and restores the list.
  The sets are edited in the menu as before. Check (Tomb of Raithwall save,
  field, test mod `zsetup` copying each party member's active set into the next
  set and emptying the built list at the 60th field frame): with the mod off the
  party's own condition tests (codes 0x0F, 0x2E, 0x06) stopped
  (`win-gfix2.log`); with it on they ran at the same rate as without the test
  mod, and character 4 acted from set 2 (`win-gfix3.log`). The evaluator runs
  about 24 times per field frame, battles or not.

Both are ordinary mod folders; `--mods-disable gambitvulnfix,gambitextrasets`
turns them off.

## Known gambit problems

- Element-weak (`0x14nn`) reads the Weak mask and is correct in this version.
  The reports that it never fires (since the PS2 International version; FFXII
  Ultima Edition, Nexus 452, lists a fix) do not match the code here.
- Element-vulnerable (`0x17nn`) picks immune foes; fixed by the mod above.
- Steal gambits repeat on foes with nothing left to steal (The Insurgent's
  Manifesto, Nexus 218, "Smart Stealing Gambits"). Not looked at yet.
