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
//
// Declared relations (mod.ini): "requires = a, b>=1.2" turns the mod off
// when a required mod is missing, off, or older than the given version; this
// repeats until nothing changes, so it cascades. "conflicts = c" turns off
// the lower-ranked mod of each enabled pair (lower priority, then larger id),
// whichever side declared it. The reason is kept in ModInfo::off_reason.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ffxii::mods {

struct Requirement {
  std::string id;
  std::string min_version;  // empty: any version
};

struct ModInfo {
  std::string id;       // unique; defaults to the folder name
  std::string name;     // defaults to id
  std::string version;  // free text
  int priority = 0;     // higher wins
  bool enabled = true;
  std::vector<Requirement> required;
  std::vector<std::string> conflicts;  // ids
  std::string off_reason;  // why Build turned it off (empty: by mod.ini or flag)
  std::vector<std::string> native;  // native module DLLs, relative to root
  std::string root;     // host folder of the mod (holds mod.ini)
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
// Keys: id, name, version, priority (integer), enabled (true/false/1/0),
// requires (comma list of id or id>=version), conflicts (comma list of ids),
// native (comma list of native module DLLs inside the mod folder; see
// include/ffxii_mod.h).
// Unknown keys are kept out of ModInfo and reported in `errors`.
bool ParseModIni(const std::string& text, ModInfo& out, std::vector<std::string>& errors);

// Compares dotted versions numerically ("1.10" > "1.9"; missing parts are 0;
// a non-numeric part compares as text). Returns <0, 0 or >0.
int CompareVersions(const std::string& a, const std::string& b);

// Scans `root`. Mods whose id is in `disabled` are listed but not indexed.
Index Build(const std::string& root, const std::unordered_set<std::string>& disabled = {});

// Enabled mods that declare native modules, lowest-ranked first: each
// later module's hooks wrap the earlier ones', so the highest-ranked mod
// runs first on every call.
std::vector<size_t> NativeLoadOrder(const Index& ix);

// "a,b,,c" -> {a, b, c}: the FFXII_MODS_DISABLE / --mods-disable list.
std::unordered_set<std::string> ParseIdList(const std::string& list);

// The mods folder: `flag_dir` when set, else <exe dir>/mods when it exists,
// else <game dir>/mods.
std::string ResolveRoot(const std::string& flag_dir, const std::string& exe_dir,
                        const std::string& game_dir);

// A hash of every file under `root` (relative path, size, modification
// time), mods-report.txt excepted: it changes when a mod file is added,
// removed, renamed or rewritten. 0 when `root` is missing.
uint64_t TreeSignature(const std::string& root);

}  // namespace ffxii::mods
