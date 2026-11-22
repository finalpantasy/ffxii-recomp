#pragma once
// Game object layouts of FFXII TZA 1.0.4.0, as far as src/ uses them.
// Guest pointers are 64-bit guest addresses (uint64_t), not host pointers.
// Fields whose role is unknown are named unk_<offset>. static_asserts pin
// every offset to the one the game uses. Sources:
// analysis/formats/vbf-runtime-read-path.md, analysis/hook-points.md,
// analysis/frame-timing.md.

#include <cstddef>
#include <cstdint>

#include "x86/lifted.h"

namespace ffxii::guest {

// The object at guest address `va`, viewed through its layout.
template <class T>
T* At(uint64_t va) {
  return reinterpret_cast<T*>(::recomp::g_guest_base + va);
}

// --- VBF archive (analysis/formats/vbf-runtime-read-path.md) ---

// Entry record in the archive header (0x20 bytes).
struct VbfEntry {
  uint32_t first_block;  // index into the u16 block table
  uint32_t unk_04;
  uint64_t size;         // uncompressed size
  uint64_t data_offset;
  uint64_t unk_18;       // relocated by Archive::Open
};
static_assert(sizeof(VbfEntry) == 0x20);
static_assert(offsetof(VbfEntry, size) == 0x08);

// A pooled reader bound to one entry (0x30 bytes); Find (0x19AEE0) returns
// one, Read (0x215110), Seek (0x215220) and Release (0x2150D0) take it.
struct VbfReader {
  uint8_t in_use;
  uint8_t unk_01[7];
  uint64_t file;      // the reader's own FILE*
  uint64_t archive;
  uint64_t entry;     // VbfEntry*
  uint64_t position;  // read offset; the caller 0x2150E0 advances it
  uint64_t cache;     // one-block cache {buf, end, start}
};
static_assert(sizeof(VbfReader) == 0x30);
static_assert(offsetof(VbfReader, entry) == 0x18);
static_assert(offsetof(VbfReader, position) == 0x20);
static_assert(offsetof(VbfReader, cache) == 0x28);

// The archive manager singleton (guest_globals.h kVbfManager).
struct VbfManager {
  uint8_t unk_00[0x70];
  uint64_t path_base;  // Find's path argument is relative to this address
};
static_assert(offsetof(VbfManager, path_base) == 0x70);

// --- Tasks (analysis/hook-points.md) ---

// A node of the task lists walked by 0x248030: +0 is the handler, run
// through the dispatcher; +0x18 and +0x20 link to further nodes.
struct TaskNode {
  uint64_t handler;
  uint64_t unk_08;
  uint64_t unk_10;
  uint64_t link_18;
  uint64_t link_20;
};
static_assert(offsetof(TaskNode, link_18) == 0x18);
static_assert(offsetof(TaskNode, link_20) == 0x20);

// --- Input ---

// The analog-axis state that 0x32C910 updates and 0x32D3D0 (horizontal)
// and 0x32D620 (vertical) filter. Only the fields src/ or the notes name.
struct AxisInput {
  uint8_t unk_00[0xC0];
  uint32_t flags;        // 0x40300000 selects the 5/6 filter in 0x32D3D0
  uint8_t unk_c4[0x2C];
  float unk_f0;
  int32_t dead_zone;     // from 0x46DB20
  float filtered_x;      // the eased value 0x32D3D0 stores
  uint8_t unk_fc[0x278];
  int32_t analog_on;     // 0: 0x32C910 treats both axes as centered
};
static_assert(offsetof(AxisInput, flags) == 0xC0);
static_assert(offsetof(AxisInput, dead_zone) == 0xF4);
static_assert(offsetof(AxisInput, filtered_x) == 0xF8);
static_assert(offsetof(AxisInput, analog_on) == 0x374);

// --- Frame-counted tables ---

// The object 0x51B150 samples a 30-entry table for, by a per-frame counter
// that wraps at 30 * frames per tick.
struct CountedTable {
  uint8_t unk_00[0x30];
  uint16_t frame_counter;
};
static_assert(offsetof(CountedTable, frame_counter) == 0x30);

}  // namespace ffxii::guest
