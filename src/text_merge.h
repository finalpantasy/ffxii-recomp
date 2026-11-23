#pragma once
// Merging several mods' changes to one game text file (host code).
//
// A mod changes text in two ways, and may use both:
//   files/<archive path>        a whole edited copy of the file
//   text/<archive path>.txt     a patch: one "key = text" line per unit
// Each mod's version of the file is diffed against the archive original unit
// by unit (text_formats.h keys). Changes to different units from different
// mods all apply. When two mods change the same unit, identical bytes count
// once ("identical"); otherwise the higher-ranked mod wins ("priority") and
// the overlap is reported. A whole copy whose units no longer line up with
// the original (strings added or removed) cannot be merged: the best-ranked
// such copy is used as it is, with only its own mod's patch on top, and the
// other mods' changes to that file are reported as "needs-patch".
//
// Patch file syntax (UTF-8):
//   # comment                  ('#' first on the line)
//   s 12 = New text            key, '=', one optional space, then the text
//   p 2 w 8 14 = Vaan
// In the text, "\n" is a line break, "\\" a backslash, and "{XX}" a raw
// game byte (ff12text.py text form). A key may appear once per file.

#include <string>
#include <vector>

#include "text_formats.h"

namespace ffxii::text {

// Parses a patch file. Errors name the line; bad lines are skipped.
Replacements ParsePatch(const std::string& src, std::vector<std::string>& errors);

struct Source {
  std::string mod;    // mod id, for the report
  bool has_file = false;
  Bytes file;         // whole edited copy (has_file)
  Replacements patch; // text/ patch lines
};

struct MergeResult {
  Bytes out;
  size_t changed = 0;     // units that differ from the original
  size_t merged = 0;      // changed units whose only changer is not the top source
  std::vector<std::string> notes;  // overlaps, identical changes, problems
};

// Merges `sources` (best-ranked first) onto `base`, the archive original.
// Returns false only when base is not a text file; problems with a single
// source (bad key, unencodable text, changed structure) are notes.
bool Merge(const Bytes& base, const std::vector<Source>& sources, MergeResult& res, std::string& err);

}  // namespace ffxii::text
