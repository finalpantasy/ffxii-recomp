# Hook points for native modules

Native modules (`include/ffxii_mod.h`) hook a function through the
dispatcher (SwARc `ReplaceDispatch`), so a hook catches only calls that
reach the function through a pointer. This note records which functions
those are and how to find more. Addresses are absolute (image base
0x120000).

## Measuring: the dispatch census

`FFXII_DISPATCH_STATS=1` counts every indirect call into the EXE by target.
Every 30 s it logs the 40 busiest targets in calls per second
(`[dispatch]` lines), and it writes `dispatch-targets.tsv` (target, total
calls) to the run folder. A target in that file is hookable.

`maps/dispatch-targets-load2.tsv` is the census of one run (2026-11-22,
`win-dstat1.log`): boot, then `load2.txt` loads the newest Steam save
(Ogir-Yensa Sandsea refinery cutscene) and walks in the field. It has
2,942 targets. 2,678 are Ghidra function entries; the rest are
lifter-discovered entries, among them the cases of unresolved switch tables
(the runs at 0x616EA8-0x616EF4 have 4- and 8-byte spacing). A different
scene calls other targets, so the list is a sample, not a bound.

The busiest targets were 0x148B70 (5.4 million calls), 0x5A0120 and
0x5A0240 (3.6 million each). Calls per second in the field are in the log.

## The task system

0x244F50 creates a task: `task* Create(int size, handler fn, void* arg)`.
- size 0 means 0xC0; a size above 0x120 skips the free-slot search.
- The pool pointer is at 0x208EA58: 0x70 slots of 0x120 bytes. A slot is in
  use when bit 0x4000 of its dword at +0x40 is set or its byte at +0xA2 is
  nonzero.
- 0x2497B0 allocates the task, and 0x2465F0 initialises it: the handler
  goes at task+0, replaced by a default handler when fn is null.
- 0x247AE0 sets up the pool at startup.

Creation sites pass the handler in rdx. `tools/find_fnptr_args.py
generated/full analysis/maps/functions.tsv --callee 244F50` lists them; with
0x245000, a variant, there are 292 call sites and 202 distinct handlers. 47
of the 202 appear in the census above, so task handlers run through the
dispatcher. A native module can therefore hook every task's per-frame
logic. The scheduler that calls them has not been located yet.

## Lua

Lua 5.2.1 is linked in, but the game registers no C functions of its own:
- lua_pushcclosure is 0x810470.
- Its only callers are luaL_setfuncs (0x8135B0) and the standard library
  openers.
- `tools/find_lua_regs.py` finds 13 luaL_Reg tables, all of them standard
  libraries.

The openers (`luaopen_*`, table 0x1CCB3E0) were never dispatched in the
boot, save-load or field runs. Game scripting is not Lua.

## Not hookable yet

Direct calls between lifted functions do not go through the dispatcher.
For example, the field handler 0x22A770 is only called directly, and a hook
on it saw no calls (`win-native2.log`). Covering those needs the lifter to
emit an entry check in every function and a full regeneration of the
generated code.

## Tools

- `tools/find_fnptr_args.py <generated dir> <functions.tsv>` ranks callees
  by how many function-address arguments they receive (`lea r, [fn]` before
  a call). The top callee, 0x808B90, takes 813; it is probably the CRT's
  atexit registration, from the dynamic initialisers. `--callee VA` lists
  one callee's call sites.
- `tools/find_lua_regs.py <exe> <functions.tsv>` lists luaL_Reg tables.
- `examples/native-sample` counts calls to listed VAs in game.
