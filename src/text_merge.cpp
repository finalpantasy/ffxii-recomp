// Mod text merging (text_merge.h).

#include "text_merge.h"

#include <set>
#include <sstream>

namespace ffxii::text {
namespace {

std::string Trim(const std::string& s) {
  const size_t a = s.find_first_not_of(" \t");
  if (a == std::string::npos) return "";
  const size_t b = s.find_last_not_of(" \t");
  return s.substr(a, b - a + 1);
}

std::string NormKey(const std::string& k) {
  std::istringstream in(k);
  std::string p, out;
  while (in >> p) out += (out.empty() ? "" : " ") + p;
  return out;
}

std::set<std::string> Keys(const RawUnits& u) {
  std::set<std::string> k;
  for (const auto& kv : u) k.insert(kv.first);
  return k;
}

}  // namespace

Replacements ParsePatch(const std::string& src, std::vector<std::string>& errors) {
  Replacements rep;
  std::istringstream in(src);
  std::string line;
  for (int no = 1; std::getline(in, line); ++no) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (no == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
    const std::string t = Trim(line);
    if (t.empty() || t[0] == '#') continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
      errors.push_back("line " + std::to_string(no) + ": no '='");
      continue;
    }
    const std::string key = NormKey(line.substr(0, eq));
    size_t at = eq + 1;
    if (at < line.size() && line[at] == ' ') ++at;
    std::string text;
    bool bad = false;
    for (size_t i = at; i < line.size(); ++i) {
      if (line[i] != '\\') {
        text += line[i];
        continue;
      }
      const char e = i + 1 < line.size() ? line[i + 1] : '\0';
      if (e == 'n') text += '\n';
      else if (e == '\\') text += '\\';
      else {
        errors.push_back("line " + std::to_string(no) + ": unknown escape (use \\n or \\\\)");
        bad = true;
        break;
      }
      ++i;
    }
    if (bad) continue;
    if (key.empty()) {
      errors.push_back("line " + std::to_string(no) + ": empty key");
      continue;
    }
    if (!rep.emplace(key, text).second) errors.push_back("line " + std::to_string(no) + ": key '" + key + "' repeated");
  }
  return rep;
}

bool Merge(const Bytes& base, const std::vector<Source>& sources, MergeResult& res, std::string& err) {
  res = {};
  RawUnits b;
  if (!Units(base, b, err)) return false;
  const std::set<std::string> keys = Keys(b);

  // Each source's version: its whole copy (or the original), then its patch.
  struct Version {
    const Source* src;
    RawUnits units;
  };
  std::vector<Version> versions;
  const Source* restructured = nullptr;
  for (const Source& s : sources) {
    const Bytes& ref = s.has_file ? s.file : base;
    RawUnits u;
    std::string e;
    if (!Units(ref, u, e)) {
      res.notes.push_back(s.mod + ": its copy is not a text file of the same kind (" + e + "); skipped");
      continue;
    }
    RawUnits patch;
    for (const auto& [key, text] : s.patch) {
      std::string pe;
      if (!EncodeUnits(ref, {{key, text}}, patch, pe)) res.notes.push_back(s.mod + ": patch " + pe + "; line skipped");
    }
    if (s.has_file && Keys(u) != keys) {
      if (!restructured) {
        restructured = &s;
        Bytes out;
        std::string re;
        if (!RebuildRaw(s.file, patch, out, re)) {
          res.notes.push_back(s.mod + ": patch on its own copy failed (" + re + ")");
          out = s.file;
        }
        res.out = std::move(out);
        res.notes.push_back(s.mod + ": its copy adds or removes units; used as it is (priority)");
      } else {
        res.notes.push_back(s.mod + ": its copy adds or removes units; ignored (needs-patch)");
      }
      continue;
    }
    for (auto& [k, v] : patch) u[k] = std::move(v);
    versions.push_back({&s, std::move(u)});
  }
  if (restructured) {
    // Only the restructured copy (and sources ranked above it, which hold no
    // whole copy of their own or one that lines up) could apply; keep it
    // simple and report everything else.
    for (const auto& v : versions)
      for (const auto& k : keys)
        if (v.units.at(k) != b.at(k)) {
          res.notes.push_back(v.src->mod + ": changes to this file dropped; " + restructured->mod +
                              " restructures it (needs-patch)");
          break;
        }
    return true;
  }

  RawUnits out;
  for (const auto& k : keys) {
    const Bytes& orig = b.at(k);
    const Version* win = nullptr;
    std::vector<std::string> same, lost;
    for (const auto& v : versions) {
      const Bytes& x = v.units.at(k);
      if (x == orig) continue;
      if (!win) win = &v;
      else if (x == win->units.at(k)) same.push_back(v.src->mod);
      else lost.push_back(v.src->mod);
    }
    if (!win) continue;
    out[k] = win->units.at(k);
    ++res.changed;
    if (win != &versions.front()) ++res.merged;
    if (!lost.empty()) {
      std::string n = k + ": " + win->src->mod + " over";
      for (const auto& m : lost) n += " " + m;
      res.notes.push_back(n + " (priority)");
    }
    if (!same.empty()) {
      std::string n = k + ": " + win->src->mod;
      for (const auto& m : same) n += ", " + m;
      res.notes.push_back(n + " (identical)");
    }
  }
  return RebuildRaw(base, out, res.out, err);
}

}  // namespace ffxii::text
