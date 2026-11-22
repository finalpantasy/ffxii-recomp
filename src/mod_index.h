#pragma once
// Mod discovery and the archive-path index (host code, no guest state).
//
// Layout under the mods root:
//   <root>/<mod folder>/mod.ini          a mod (see ParseModIni)
//   <root>/<mod folder>/files/<path>     files that replace or add archive paths
//   <root>/<path>                        legacy loose files (implicit mod "loose")
// A top-level folder without mod.ini belongs to the legacy loose tree.
//
// Paths match case-insensitively with '/' separators. When several enabled
// mods carry the same path, the higher priority wins; on equal priority the
// smaller id wins. Every such clash is recorded as a conflict.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ffxii::mods {

struct ModInfo {
  std::string id;       // unique; defaults to the folder name
  std::string name;     // defaults to id
  std::string version;  // free text
  int priority = 0;     // higher wins
  bool enabled = true;
  std::string dir;      // host folder holding the files
  size_t files = 0;     // paths this mod supplies (winning or not)
  size_t won = 0;       // paths this mod actually serves
};

struct Conflict {
  std::string path;
  std::string winner;
  std::vector<std::string> losers;
};

struct Entry {
  std::string host;  // host file path
  size_t mod;        // index into Index::mods
};

struct Index {
  std::vector<ModInfo> mods;
  std::unordered_map<std::string, Entry> files;  // normalized path -> winner
  std::vector<Conflict> conflicts;
  std::vector<std::string> errors;  // manifest problems, duplicate ids

  const Entry* Find(const std::string& archive_path) const;
  std::string Report() const;
};

// Lower case, '\\' to '/', leading "./" and '/' removed, repeated '/' merged.
std::string Normalize(const std::string& path);

// mod.ini: one "key = value" per line; '#' or ';' start a comment line.
// Keys: id, name, version, priority (integer), enabled (true/false/1/0).
// Unknown keys are kept out of ModInfo and reported in `errors`.
bool ParseModIni(const std::string& text, ModInfo& out, std::vector<std::string>& errors);

// Scans `root`. Mods whose id is in `disabled` are listed but not indexed.
Index Build(const std::string& root, const std::unordered_set<std::string>& disabled = {});

}  // namespace ffxii::mods
