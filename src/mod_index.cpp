#include "mod_index.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace ffxii::mods {

namespace {

std::string Trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
  return s.substr(a, b - a);
}

std::string Lower(std::string s) {
  for (char& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}

std::string ReadText(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::string Utf8(const fs::path& p) {
  const auto u = p.generic_u8string();
  return std::string(u.begin(), u.end());
}

// Adds every regular file under `dir` as a candidate for mod `m`.
void Collect(const fs::path& dir, const fs::path& base, size_t m,
             std::map<std::string, std::vector<std::pair<size_t, std::string>>>& cand,
             const fs::path* skip_top = nullptr) {
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec),
       end;
       it != end; it.increment(ec)) {
    if (ec) break;
    if (skip_top && it.depth() == 0 && it->is_directory(ec) &&
        fs::exists(it->path() / "mod.ini", ec)) {
      it.disable_recursion_pending();  // a mod of its own
      continue;
    }
    if (!it->is_regular_file(ec)) continue;
    const fs::path rel = it->path().lexically_relative(base);
    const std::string key = Normalize(Utf8(rel));
    if (skip_top && (key == "mods-report.txt" || key == "mod.ini")) continue;
    cand[key].push_back({m, Utf8(it->path())});
  }
}

std::vector<std::string> SplitList(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  std::istringstream in(s);
  while (std::getline(in, cur, ',')) {
    cur = Trim(cur);
    if (!cur.empty()) out.push_back(cur);
  }
  return out;
}

// Ranking used for path clashes and declared conflicts: true if a beats b.
bool Ranks(const ModInfo& a, const ModInfo& b) {
  if (a.priority != b.priority) return a.priority > b.priority;
  return Lower(a.id) < Lower(b.id);
}

// Applies conflicts, then requirements until stable. Mods turned off here get
// an off_reason and an entry in `errors`.
void ResolveRelations(std::vector<ModInfo>& mods, std::vector<std::string>& errors) {
  std::map<std::string, size_t> by_id;
  for (size_t i = 0; i < mods.size(); i++) by_id[Lower(mods[i].id)] = i;
  auto off = [&](size_t i, const std::string& why) {
    mods[i].enabled = false;
    mods[i].off_reason = why;
    errors.push_back(mods[i].id + ": turned off, " + why);
  };
  for (size_t i = 0; i < mods.size(); i++) {
    for (const auto& other : mods[i].conflicts) {
      auto it = by_id.find(Lower(other));
      if (it == by_id.end() || it->second == i) continue;
      const size_t j = it->second;
      if (!mods[i].enabled || !mods[j].enabled) continue;
      const size_t lose = Ranks(mods[i], mods[j]) ? j : i, win = lose == i ? j : i;
      off(lose, "conflicts with " + mods[win].id);
    }
  }
  for (bool changed = true; changed;) {
    changed = false;
    for (size_t i = 0; i < mods.size(); i++) {
      if (!mods[i].enabled) continue;
      for (const auto& r : mods[i].required) {
        auto it = by_id.find(Lower(r.id));
        std::string why;
        if (it == by_id.end()) why = "requires " + r.id + ", which is not installed";
        else if (!mods[it->second].enabled) why = "requires " + r.id + ", which is off";
        else if (!r.min_version.empty() &&
                 CompareVersions(mods[it->second].version, r.min_version) < 0)
          why = "requires " + r.id + " >= " + r.min_version + ", found " +
                (mods[it->second].version.empty() ? "no version" : mods[it->second].version);
        if (!why.empty()) {
          off(i, why);
          changed = true;
          break;
        }
      }
    }
  }
}

}  // namespace

int CompareVersions(const std::string& a, const std::string& b) {
  size_t i = 0, j = 0;
  while (i < a.size() || j < b.size()) {
    const size_t ei = std::min(a.find('.', i), a.size()), ej = std::min(b.find('.', j), b.size());
    const std::string pa = i < a.size() ? a.substr(i, ei - i) : "0";
    const std::string pb = j < b.size() ? b.substr(j, ej - j) : "0";
    const bool na = !pa.empty() && std::all_of(pa.begin(), pa.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
    const bool nb = !pb.empty() && std::all_of(pb.begin(), pb.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
    int c;
    if (na && nb) {
      const unsigned long long va = std::strtoull(pa.c_str(), nullptr, 10);
      const unsigned long long vb = std::strtoull(pb.c_str(), nullptr, 10);
      c = va < vb ? -1 : va > vb ? 1 : 0;
    } else {
      c = pa.compare(pb);
      c = c < 0 ? -1 : c > 0 ? 1 : 0;
    }
    if (c) return c;
    i = ei + 1;
    j = ej + 1;
  }
  return 0;
}

std::string Normalize(const std::string& path) {
  std::string out;
  out.reserve(path.size());
  for (char ch : path) {
    ch = ch == '\\' ? '/' : char(std::tolower(static_cast<unsigned char>(ch)));
    if (ch == '/' && (out.empty() || out.back() == '/')) continue;
    out.push_back(ch);
  }
  while (out.size() >= 2 && out[0] == '.' && out[1] == '/') out.erase(0, 2);
  return out;
}

bool ParseModIni(const std::string& text, ModInfo& out, std::vector<std::string>& errors) {
  std::istringstream in(text);
  std::string line;
  int n = 0;
  bool ok = true;
  while (std::getline(in, line)) {
    n++;
    line = Trim(line);
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
      errors.push_back("line " + std::to_string(n) + ": expected key = value");
      ok = false;
      continue;
    }
    const std::string key = Lower(Trim(line.substr(0, eq)));
    const std::string val = Trim(line.substr(eq + 1));
    if (key == "id") {
      out.id = val;
    } else if (key == "name") {
      out.name = val;
    } else if (key == "version") {
      out.version = val;
    } else if (key == "priority") {
      char* end = nullptr;
      const long v = std::strtol(val.c_str(), &end, 10);
      if (val.empty() || *end) {
        errors.push_back("line " + std::to_string(n) + ": priority is not an integer");
        ok = false;
      } else {
        out.priority = int(v);
      }
    } else if (key == "enabled") {
      const std::string v = Lower(val);
      if (v == "true" || v == "1" || v == "yes") out.enabled = true;
      else if (v == "false" || v == "0" || v == "no") out.enabled = false;
      else {
        errors.push_back("line " + std::to_string(n) + ": enabled must be true or false");
        ok = false;
      }
    } else if (key == "requires") {
      for (const auto& item : SplitList(val)) {
        Requirement r;
        const size_t ge = item.find(">=");
        r.id = Trim(item.substr(0, ge));
        if (ge != std::string::npos) r.min_version = Trim(item.substr(ge + 2));
        if (r.id.empty() || (ge != std::string::npos && r.min_version.empty())) {
          errors.push_back("line " + std::to_string(n) + ": bad requirement '" + item + "'");
          ok = false;
          continue;
        }
        out.required.push_back(r);
      }
    } else if (key == "conflicts") {
      for (const auto& item : SplitList(val)) out.conflicts.push_back(item);
    } else {
      errors.push_back("line " + std::to_string(n) + ": unknown key '" + key + "'");
    }
  }
  return ok;
}

const Entry* Index::Find(const std::string& archive_path) const {
  if (files.empty()) return nullptr;
  auto it = files.find(Normalize(archive_path));
  return it == files.end() ? nullptr : &it->second;
}

Index Build(const std::string& root_s, const std::unordered_set<std::string>& disabled) {
  Index ix;
  std::error_code ec;
  const fs::path root = fs::u8path(root_s);
  if (!fs::is_directory(root, ec)) return ix;

  // Mods with a manifest, in folder-name order so ids default predictably.
  std::vector<fs::path> dirs;
  for (const auto& e : fs::directory_iterator(root, ec))
    if (e.is_directory(ec) && fs::exists(e.path() / "mod.ini", ec)) dirs.push_back(e.path());
  std::sort(dirs.begin(), dirs.end());

  std::unordered_set<std::string> ids;
  for (const fs::path& d : dirs) {
    ModInfo m;
    std::vector<std::string> errs;
    ParseModIni(ReadText(d / "mod.ini"), m, errs);
    const std::string folder = Utf8(d.filename());
    for (const auto& e : errs) ix.errors.push_back(folder + "/mod.ini " + e);
    if (m.id.empty()) m.id = folder;
    if (m.name.empty()) m.name = m.id;
    if (!ids.insert(Lower(m.id)).second) {
      ix.errors.push_back(folder + ": duplicate id '" + m.id + "'; mod skipped");
      continue;
    }
    if (disabled.count(m.id)) m.enabled = false;
    m.dir = Utf8(d / "files");
    ix.mods.push_back(m);
  }
  ResolveRelations(ix.mods, ix.errors);

  std::map<std::string, std::vector<std::pair<size_t, std::string>>> cand;
  for (size_t i = 0; i < ix.mods.size(); i++) {
    const fs::path files = fs::u8path(ix.mods[i].dir);
    if (ix.mods[i].enabled && fs::is_directory(files, ec)) Collect(files, files, i, cand);
  }

  // Legacy loose files directly under the root.
  ModInfo loose;
  loose.id = loose.name = "loose";
  loose.dir = Utf8(root);
  if (!disabled.count(loose.id) && !ids.count("loose")) {
    const size_t li = ix.mods.size();
    ix.mods.push_back(loose);
    Collect(root, root, li, cand, &root);
    if (std::none_of(cand.begin(), cand.end(),
                     [&](const auto& kv) {
                       return std::any_of(kv.second.begin(), kv.second.end(),
                                          [&](const auto& c) { return c.first == li; });
                     }))
      ix.mods.pop_back();  // no loose files: leave it out of the report
  }

  for (auto& [path, list] : cand) {
    auto better = [&](const auto& a, const auto& b) {
      return Ranks(ix.mods[a.first], ix.mods[b.first]);
    };
    std::stable_sort(list.begin(), list.end(), better);
    // One mod may hold two spellings of a path ("A.tga", "a.tga"): first wins.
    std::unordered_set<size_t> seen;
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const auto& c) { return !seen.insert(c.first).second; }),
               list.end());
    for (const auto& c : list) ix.mods[c.first].files++;
    ix.files[path] = {list[0].second, list[0].first};
    ix.mods[list[0].first].won++;
    if (list.size() > 1) {
      Conflict cf{path, ix.mods[list[0].first].id, {}};
      for (size_t k = 1; k < list.size(); k++) cf.losers.push_back(ix.mods[list[k].first].id);
      ix.conflicts.push_back(std::move(cf));
    }
  }
  return ix;
}

std::string Index::Report() const {
  std::ostringstream o;
  o << "mods: " << mods.size() << ", files served: " << files.size()
    << ", conflicts: " << conflicts.size() << "\n";
  std::vector<size_t> order(mods.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = i;
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    if (mods[a].priority != mods[b].priority) return mods[a].priority > mods[b].priority;
    return Lower(mods[a].id) < Lower(mods[b].id);
  });
  for (size_t i : order) {
    const ModInfo& m = mods[i];
    o << (m.enabled ? "  [on]  " : "  [off] ") << m.id;
    if (m.name != m.id) o << " (" << m.name << ")";
    if (!m.version.empty()) o << " " << m.version;
    o << "  priority " << m.priority << ", " << m.won << " of " << m.files << " files";
    if (!m.off_reason.empty()) o << "  (" << m.off_reason << ")";
    o << "\n";
  }
  for (const auto& c : conflicts) {
    o << "  conflict " << c.path << ": " << c.winner << " over";
    for (const auto& l : c.losers) o << " " << l;
    o << "\n";
  }
  for (const auto& e : errors) o << "  error " << e << "\n";
  return o.str();
}

}  // namespace ffxii::mods
