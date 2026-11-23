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
logic.

Handlers take messages: `handler(task* rcx, msg* rdx)`. A hook on 15
handlers logged the return address of their 1st, 10th, 100th, ... calls
(`examples/native-sample` reads [rsp]; `win-task2.log`) and found these
call sites:
- 0x24674A, in the init 0x2465F0: the first call, at creation, with a
  message built on the stack.
- 0x2480FB, in the task-list runner 0x248030, `call [rbx]` with
  rdx = 0x915108: the per-tick update. The runner is described in
  `frame-timing.md`: it walks one task tree once per frame, using the
  parity bit 17 of +0x40, and the drivers 0x250540 and 0x2507C0 run it on
  the lists at 0x208EA20-0x208EA50. Bit 18 of +0x40 skips the handler, and
  bit 10 takes the 0x2482E0 path instead.
- 0x24635C and 0x2462D2, in 0x245E60 (called from 0x2450D2, 0x245132 and
  0x24637A), and 0x246C53, in 0x246C20 (called from 0x2449A9): other
  messages, not yet decoded.

So a hook on a handler sees its creation, every tick and the other
messages, and it can tell them apart by rdx.

### Message ids

A message is a record whose first dword is its id: the constants at
0x915108 (id 2), 0x915120 (25), 0x9151C0 (3), 0x9151D8 (4) and 0x9151F0
(5) are 24 bytes each, and others are built on the stack. The sample
module logs the first call of each id per hooked handler. 16 task handlers
hooked through a save-load run (`win-task3.log`) received these ids. A
sender is the function containing the call; meanings are given only where
the code shows them.

| id | sender (call site) | what the code shows |
|---|---|---|
| 1 | 0x2465F0 (0x24674A) | creation: the init, from create 0x244F50/0x245000 |
| 2 | 0x248030 (0x2480FB) | per-tick update from the list runner |
| 3 | 0x245E60 (0x2462D2) | draw walk: sent after the task's matrices are copied |
| 4 | 0x245E60 (0x24635C) | draw walk: draw, before the children (recursion at 0x24637A) |
| 5 | 0x245E60 (0x2463BE) | draw walk: after the children, when bit 12 of +0x40 is set and bit 24 matches 0x208F564 |
| 6, 7 | 0x2473F0 (0x247450), 0x247490 (0x2474D2) | |
| 8, 9 | 0x244830 (0x2448AF, 0x244880) | |
| 10, 11 | 0x250540 (0x25066B) | the per-frame driver |
| 12 | 0x247510 (0x247549) | |
| 14 | 0x246AD0 (0x246C0B), and a non-task path (0x3CB861) | |
| 15 | 0x244DD0 (0x244EF7) | |
| 16 | 0x2494F0 (0x249564), 0x2495C0 (0x249634), and a non-task path (0x3C216B) | |
| 18 | 0x245020 (0x24505C), 0x2473B0 (0x2473CB) | 0x245020 then calls 0x2482E0 |
| 19, 20, 25 | 0x246C20 (0x24703B, 0x24706D, 0x246C53) | called from 0x2449A9 |
| 24 | 0x2482E0 (0x2483B4) | the runner's bit-10 path; the runner saves the next sibling before calling it, so the task is probably removed (destroy) |

Ids 13, 17, 21-23 and 26+ did not appear in this run. The message
table at 0x9150F0-0x915200 also holds ids 0x0B, 0x11, 0x15, 0x16 and
0x17 with nonzero second fields.

### Replacing a message (2026-11-22)

`count.txt` lines of the form `VA skip=N` make the sample swallow message N:
the hook sets rax = 0 and returns through `api->ret` without running the
original. With id 4 (draw) swallowed on the 10 handlers that received it in
`win-task3.log` (0x366B80, 0x2D2DB0, 0x29D340, 0x582860, 0x2CA1F0, 0x57FE80,
0x57EDB0, 0x57E610, 0x2C0BE0, 0x3CB4F0), `load2.txt` still loaded the save
and played the refinery cutscene for 600 s with no fatal; each handler
swallowed the message more than 1,000 times (`win-skip4b.log`). The
cutscene frames came out grayscale and smeared, where the pass-through run
(`win-pass4b.log`, same script and dump window) shows the scene in full
color. So the replace path works, and at least one of those handlers draws
part of the post-processing chain.

## Lua

Lua 5.2.1 is linked in, but the game registers no C functions of its own:
- lua_pushcclosure is 0x810470.
- Its only callers are luaL_setfuncs (0x8135B0) and the standard library
  openers.
- `tools/find_lua_regs.py` finds 13 luaL_Reg tables, all of them standard
  libraries.

The openers (`luaopen_*`, table 0x1CCB3E0) were never dispatched in the
boot, save-load or field runs. Game scripting is not Lua.

## Direct calls

Direct calls between lifted functions do not go through the dispatcher.
For example, the field handler 0x22A770 is only called directly, and a
dispatcher hook on it saw no calls (`win-native2.log`).

`src/direct_hooks.h` lists functions whose hooks catch direct calls too:
0x22A770 (field frame handler), 0x2507C0 (field driver), 0x250540 (main
driver), 0x259240 (frame body step), 0x2E0FA0 (field state flags), 0x2647D0
(talk target), 0x248030 (task list run) and 0x244F50 (task create). Each
has a `replace` wrapper that first calls `direct::Enter<VA>`; a module's
`original` is a trampoline that skips the hook once, so chains of modules
work as through the dispatcher. `hook_covers_direct` returns 1 for them.
With the sample counter on all eight and the window procedure 0x798FF0,
`fight7.txt` at `--fps 120` counted more than 10,000 calls of 0x22A770,
0x2507C0, 0x248030 and 0x2E0FA0 and more than 100 of 0x244F50 and
0x2647D0, with no fatal (`win-direct2.log`).

Every other lifted function is covered by entry hooks (2026-11-22). The
manifest line `entry_hooks` makes the SwARc lifter start each of the
43,311 functions with `LIFT_ENTRY_HOOK(row)`, a load of slot `row` in
`g_entry_hooks` (generated `table.cpp`) and an untaken branch.
`recomp::SetEntryHook` (SwARc `x86/lifted_table.cpp`) fills the slot and
hands back the previous hook, or for the first one a trampoline that skips
the hook once for its thread and calls the table entry. `ApiHook` uses
`direct_hooks.h` for its eight functions and entry hooks for the rest,
without changing the dispatcher, so a dispatched call is hooked once. In
`win-entry1.log` (`fight7-x.txt`, `--fps 120`, 87% machine load) the sample
counted more than 10,000 calls of the axis filter 0x32D3D0 and its caller
0x32C910, which are only called directly, and more than 100,000 of the
probe-wrapped 0x3DA5A0, with no fatal. The exe grew from 322 to 366 MB. The
fps cost of the check is not measured yet: the machine was loaded.

## Tools

- `tools/find_fnptr_args.py <generated dir> <functions.tsv>` ranks callees
  by how many function-address arguments they receive (`lea r, [fn]` before
  a call). The top callee, 0x808B90, takes 813; it is probably the CRT's
  atexit registration, from the dynamic initialisers. `--callee VA` lists
  one callee's call sites.
- `tools/find_lua_regs.py <exe> <functions.tsv>` lists luaL_Reg tables.
- `examples/native-sample` counts calls to listed VAs in game.
