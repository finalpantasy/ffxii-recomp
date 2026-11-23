// Mod text merging over the index and the archive (text_mods.h).

#include "text_mods.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

#include "text_merge.h"

namespace ffxii::text {
namespace {

bool ReadHost(const std::string& p, Bytes& out) {
  std::ifstream f(std::filesystem::u8path(p), std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), {});
  return true;
}

}  // namespace

ModTextMerges MergeModText(const mods::Index& ix, vbf::Archive& archive) {
  ModTextMerges res;
  std::set<std::string> paths;
  for (const auto& kv : ix.copies) paths.insert(kv.first);
  for (const auto& kv : ix.patches) paths.insert(kv.first);
  std::ostringstream rep;
  for (const std::string& path : paths) {
    const auto pit = ix.patches.find(path);
    const bool patched = pit != ix.patches.end();
    auto problem = [&](const std::string& what) {
      rep << "  text " << path << ": " << what << "\n";
      ++res.problems;
    };
    if (!archive.IsOpen() || !archive.Has(path)) {
      // Nothing to merge against: copies stay whole-file, patches cannot apply.
      if (patched) problem(archive.IsOpen() ? "not in the archive; patch not applied" : "archive not open; patch not applied");
      continue;
    }
    // One source per mod: its whole copy (if any) and its patch (if any).
    std::map<size_t, Source> by_mod;
    std::vector<mods::Entry> copies;
    if (const auto c = ix.copies.find(path); c != ix.copies.end()) copies = c->second;
    else if (const mods::Entry* w = ix.Find(path)) copies.push_back(*w);
    for (const auto& e : copies) {
      Source& s = by_mod[e.mod];
      s.mod = ix.mods[e.mod].id;
      s.has_file = ReadHost(e.host, s.file);
      if (!s.has_file) problem("cannot read " + e.host);
    }
    if (patched) {
      for (const auto& e : pit->second) {
        Source& s = by_mod[e.mod];
        s.mod = ix.mods[e.mod].id;
        Bytes raw;
        if (!ReadHost(e.host, raw)) {
          problem("cannot read " + e.host);
          continue;
        }
        std::vector<std::string> errs;
        s.patch = ParsePatch(std::string(raw.begin(), raw.end()), errs);
        for (const auto& er : errs) problem(s.mod + " patch " + er);
      }
    }
    Bytes base;
    std::string err;
    if (!archive.Read(path, base, err)) {
      problem(err);
      continue;
    }
    if (Detect(base) == Kind::kNone) {
      if (patched) problem("not a text or data file this tool can patch; patch not applied");
      continue;  // a clash between whole binary files: the winner serves
    }
    std::vector<size_t> order;
    for (const auto& kv : by_mod) order.push_back(kv.first);
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return mods::RanksAbove(ix.mods[a], ix.mods[b]); });
    std::vector<Source> sources;
    for (size_t m : order) sources.push_back(std::move(by_mod[m]));
    MergeResult mr;
    if (!Merge(base, sources, mr, err)) {
      problem(err);
      continue;
    }
    rep << "  merge " << path << ": " << mr.changed << " units changed by";
    for (const auto& s : sources) rep << " " << s.mod;
    rep << " (" << KindName(Detect(base)) << ")\n";
    for (const auto& n : mr.notes) {
      rep << "        " << n << "\n";
      if (n.find("skipped") != std::string::npos || n.find("failed") != std::string::npos) ++res.problems;
    }
    res.files[path] = std::make_shared<const Bytes>(std::move(mr.out));
  }
  res.report = rep.str();
  return res;
}

}  // namespace ffxii::text
