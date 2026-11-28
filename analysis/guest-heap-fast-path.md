# GuestHeap small-block fast path (2026-11-28)

`recomp::GuestHeap` (SwARc `src/runtime/guest_alloc.cpp`) serves the game's malloc, free, calloc, realloc, `_msize`, `_aligned_malloc`, new and delete and the `Heap*`, `Global*` and `Local*` imports. Before this change every call took one mutex and ran three tree operations (used, free by address, free by size), with about three host allocations and two frees per call. At 120 fps that was 4.4% of the game thread's samples and 2.1% of the render thread's.

## Change

- Requests up to 1,024 bytes in growable heaps (the process heap and `HeapCreate` heaps without a maximum) come from 64 KiB chunks cut into 16-byte size classes. Bounded heaps and larger blocks keep the old path.
- Each chunk records the requested size of every slot host-side (`std::atomic<uint16_t>`), so `Size`, `Free` and in-place `ReAlloc` are O(1). A lock-free table, insert-only and published with release stores, maps an address to its chunk.
- Free lists live in eight shards, picked per thread. A freed slot goes back to the shard that created its chunk. A thread whose own list is empty first steals from the other shards (`try_lock`) before it takes a new chunk, so a thread that only allocates still reuses what other threads free.
- A repeated free returns false, also when two threads race, because the slot's size entry is cleared with one atomic exchange.
- The shards and the table are allocated on the first small allocation, and chunk reservations are aligned to 64 KiB even when the allocator's low bound is not.

## Checks

- SwARc `guest_alloc` test, extended: sizes at the 1,024 boundary, zeroing on reuse, double free, overlap, realloc paths, a bounded heap, cross-thread frees, a producer and consumer thread over 1.6 million blocks (chunk count stays at 4), racing double frees, an unaligned low bound. It passes in a normal build and under ThreadSanitizer.
- Micro-benchmark in WSL (alloc and free pairs): 162 ns old against 44 ns new on one thread. With four threads on one heap the old code took about 9,700 ns a pair and the new code about 190 ns; one thread allocating while another frees took about 340 ns, because each freed block takes the lock of the shard that made its chunk. The multi-thread figures are wall-clock times under other machine load.
- In game, `fight7-x.txt` at 120 fps (`win-heapNew.log` against the inline-conversion exe in `win-heapOld.log`): GuestHeap samples on the game thread fell from 4.4% to 0.1%, and on the render thread from 2.1% to 0.9% (its remaining blocks are larger than 1,024 bytes). Both runs had 88-94% machine load from other work, so fps and busy shares do not compare; measured as CPU per frame inside the sample window, the busiest thread went from 7.7 to 6.2 ms and the second from 6.0 to 5.2 ms.

## Limits

- Small-block memory is never returned to the general pool: each size class keeps its peak.
- At most 8,192 chunks (512 MiB of small blocks); past that, small requests use the old path.
- Blocks go back to LIFO order on reuse, which differs from the old allocator's address order. Guest code that depends on allocation addresses would see different ones.
