#pragma once
// Mod text merging over the mod index and the archive (host code): for each
// path the index marks (Index::MayMerge), the original comes from the VBF,
// each mod's whole copy and text patch become one text_merge Source, and the
// merged file replaces whatever single mod would have won the path.

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>

#include "mod_index.h"
#include "text_formats.h"
#include "vbf_reader.h"

namespace ffxii::text {

struct ModTextMerges {
  // Normalized archive path -> merged file. Paths that turned out not to be
  // text files, or are not in the archive, are absent (the winner serves).
  std::unordered_map<std::string, std::shared_ptr<const Bytes>> files;
  std::string report;   // one block per merged path, for mods-report.txt
  size_t problems = 0;  // bad patch lines, unreadable files
};

// `archive` may be closed: then patches cannot apply and each is a problem.
ModTextMerges MergeModText(const mods::Index& ix, vbf::Archive& archive);

}  // namespace ffxii::text
