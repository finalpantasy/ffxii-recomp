#pragma once
// Merging several mods' changes to one game text or data file (host code).
//
// A mod changes a file in two ways, and may use both:
//   files/<archive path>        a whole edited copy of the file
//   patch/<archive path>.txt    a patch: one line per string or data field
// Each mod's version of the file is diffed against the archive original unit
// by unit (text_formats.h keys). Changes to different units from different
// mods all apply. When two mods change the same string, identical bytes count
// once ("identical"); otherwise the higher-ranked mod wins ("priority").
// Data records merge byte by byte the same way, so two mods that change
// different fields of one record both apply. A whole copy whose units no
// longer line up with the original (strings added or removed) cannot be
// merged: the best-ranked such copy is used as it is, with only its own mod's
// patch on top, and the other mods' changes are reported as "needs-patch".
//
// Patch file syntax (UTF-8):
//   # comment                    ('#' first on the line)
//   s 12 = New text              string: key, '=', one optional space, text
//   p 2 w 8 14 = {00}{00}Vaan
//   p 13 r 1 0x1A u8 = 40        data field: record, byte offset, type, op, value
//   p 13 r 1 0x12 u16 *= 0.5
// Text: "\n" is a line break, "\\" a backslash, "{XX}" a raw game byte
// (ff12text.py text form). Field types: u8 u16 u32 i8 i16 i32 f32
// (little-endian). Operations, in the order they apply after the merge:
//   =   set (merges like a whole copy's change: the higher rank wins)
//   =?  set if no other mod changed the field ("set-if-original")
//   +=  -=  *=  add, subtract, multiply (integers round and saturate)
//   |=  &~=  set bits, clear bits (integer types)
// Relative operations from every mod apply, lowest-ranked mod first, on top
// of the merged value ("operation" in the report when several mods do it).

#include <string>
#include <vector>

#include "text_formats.h"

namespace ffxii::text {

enum class FieldType { kU8, kU16, kU32, kI8, kI16, kI32, kF32 };
enum class FieldOpKind { kSet, kSetIfOriginal, kAdd, kSub, kMul, kOr, kClear };

struct FieldOp {
  std::string record;  // normalized record key ("p 13 r 1")
  size_t offset = 0;
  FieldType type = FieldType::kU8;
  FieldOpKind op = FieldOpKind::kSet;
  double value = 0;     // f32 values and multipliers
  long long ivalue = 0; // integer values and bit masks
  std::string text;     // the line as written, for the report
};

struct Patch {
  Replacements strings;
  std::vector<FieldOp> ops;
};

// Parses a patch file. Errors name the line; bad lines are skipped.
Patch ParsePatch(const std::string& src, std::vector<std::string>& errors);

struct Source {
  std::string mod;    // mod id, for the report
  bool has_file = false;
  Bytes file;         // whole edited copy (has_file)
  Patch patch;
};

struct MergeResult {
  Bytes out;
  size_t changed = 0;              // units that differ from the original
  std::vector<std::string> notes;  // overlaps, identical changes, problems
};

// Merges `sources` (best-ranked first) onto `base`, the archive original.
// Returns false only when base is not a supported file; problems with a
// single source (bad key, unencodable text, changed structure) are notes.
bool Merge(const Bytes& base, const std::vector<Source>& sources, MergeResult& res, std::string& err);

}  // namespace ffxii::text
