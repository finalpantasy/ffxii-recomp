// Mod text and data merging (text_merge.h).

#include "text_merge.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
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

std::vector<std::string> Tokens(const std::string& s) {
  std::istringstream in(s);
  std::vector<std::string> out;
  std::string p;
  while (in >> p) out.push_back(p);
  return out;
}

std::string Join(const std::vector<std::string>& t, size_t from, size_t to) {
  std::string out;
  for (size_t i = from; i < to; ++i) out += (out.empty() ? "" : " ") + t[i];
  return out;
}

std::set<std::string> Keys(const RawUnits& u) {
  std::set<std::string> k;
  for (const auto& kv : u) k.insert(kv.first);
  return k;
}

bool IsRecord(const std::string& k) { return k.rfind("r ", 0) == 0 || k.find(" r ") != std::string::npos; }

size_t Width(FieldType t) {
  switch (t) {
    case FieldType::kU8:
    case FieldType::kI8: return 1;
    case FieldType::kU16:
    case FieldType::kI16: return 2;
    default: return 4;
  }
}

bool ParseType(const std::string& s, FieldType& t) {
  static const std::map<std::string, FieldType> m = {
      {"u8", FieldType::kU8},   {"u16", FieldType::kU16}, {"u32", FieldType::kU32}, {"i8", FieldType::kI8},
      {"i16", FieldType::kI16}, {"i32", FieldType::kI32}, {"f32", FieldType::kF32}};
  const auto it = m.find(s);
  if (it == m.end()) return false;
  t = it->second;
  return true;
}

bool ParseInt(const std::string& s, long long& v) {
  if (s.empty()) return false;
  char* end = nullptr;
  errno = 0;
  v = std::strtoll(s.c_str(), &end, 0);
  return errno == 0 && end && *end == '\0';
}

bool ParseDouble(const std::string& s, double& v) {
  if (s.empty()) return false;
  char* end = nullptr;
  v = std::strtod(s.c_str(), &end);
  return end && *end == '\0' && std::isfinite(v);
}

// Integer range of a field type.
void Range(FieldType t, long long& lo, long long& hi) {
  switch (t) {
    case FieldType::kU8: lo = 0, hi = 0xFF; break;
    case FieldType::kU16: lo = 0, hi = 0xFFFF; break;
    case FieldType::kU32: lo = 0, hi = 0xFFFFFFFFll; break;
    case FieldType::kI8: lo = -128, hi = 127; break;
    case FieldType::kI16: lo = -32768, hi = 32767; break;
    default: lo = std::numeric_limits<int32_t>::min(), hi = std::numeric_limits<int32_t>::max(); break;
  }
}

// Reads and writes a field as a number (integers as long long in `i`).
void Get(const Bytes& rec, const FieldOp& op, long long& i, double& f) {
  uint32_t raw = 0;
  std::memcpy(&raw, rec.data() + op.offset, Width(op.type));
  switch (op.type) {
    case FieldType::kU8:
    case FieldType::kU16:
    case FieldType::kU32: i = raw; break;
    case FieldType::kI8: i = int8_t(raw); break;
    case FieldType::kI16: i = int16_t(raw); break;
    case FieldType::kI32: i = int32_t(raw); break;
    case FieldType::kF32: {
      float x;
      std::memcpy(&x, &raw, 4);
      f = x;
      break;
    }
  }
}

void PutInt(Bytes& rec, const FieldOp& op, long long v) {
  long long lo, hi;
  Range(op.type, lo, hi);
  v = v < lo ? lo : v > hi ? hi : v;
  const uint32_t raw = uint32_t(v);
  std::memcpy(rec.data() + op.offset, &raw, Width(op.type));
}

void PutFloat(Bytes& rec, const FieldOp& op, double v) {
  const float x = float(v);
  std::memcpy(rec.data() + op.offset, &x, 4);
}

// Applies one operation to a record.
void Apply(Bytes& rec, const FieldOp& op) {
  long long i = 0;
  double f = 0;
  Get(rec, op, i, f);
  if (op.type == FieldType::kF32) {
    switch (op.op) {
      case FieldOpKind::kAdd: f += op.value; break;
      case FieldOpKind::kSub: f -= op.value; break;
      case FieldOpKind::kMul: f *= op.value; break;
      default: f = op.value; break;
    }
    return PutFloat(rec, op, f);
  }
  switch (op.op) {
    case FieldOpKind::kAdd: i += op.ivalue; break;
    case FieldOpKind::kSub: i -= op.ivalue; break;
    case FieldOpKind::kMul: i = (long long)std::llround(double(i) * op.value); break;
    case FieldOpKind::kOr: i |= op.ivalue; break;
    case FieldOpKind::kClear: i &= ~op.ivalue; break;
    default: i = op.ivalue; break;
  }
  PutInt(rec, op, i);
}

bool IsRelative(FieldOpKind k) { return k != FieldOpKind::kSet && k != FieldOpKind::kSetIfOriginal; }

std::string FieldName(const FieldOp& op) {
  char off[16];
  std::snprintf(off, sizeof off, "0x%zX", op.offset);
  return op.record + " " + off;
}

bool FieldEqual(const Bytes& a, const Bytes& b, const FieldOp& op) {
  return std::memcmp(a.data() + op.offset, b.data() + op.offset, Width(op.type)) == 0;
}

// A data line: "<record key> <offset> <type> <op> <value>".
bool ParseFieldLine(const std::string& line, FieldOp& op, std::string& err) {
  const size_t eq = line.find('=');
  std::vector<std::string> left = Tokens(line.substr(0, eq));
  std::string rest = Trim(line.substr(eq + 1));
  std::string opname = "=";
  if (!left.empty() && (left.back() == "+" || left.back() == "-" || left.back() == "*" || left.back() == "|" ||
                        left.back() == "&~")) {
    opname = left.back() + "=";
    left.pop_back();
  }
  if (opname == "=" && !rest.empty() && rest[0] == '?') {
    opname = "=?";
    rest = Trim(rest.substr(1));
  }
  // left: [p k] r i offset type
  if (left.size() < 4) return err = "a data line is '<record> <offset> <type> <op> <value>'", false;
  const std::string type = left.back(), offset = left[left.size() - 2];
  op.record = Join(left, 0, left.size() - 2);
  if (!ParseType(type, op.type)) return err = "unknown type '" + type + "' (u8 u16 u32 i8 i16 i32 f32)", false;
  long long o;
  if (!ParseInt(offset, o) || o < 0) return err = "bad offset '" + offset + "'", false;
  op.offset = size_t(o);
  static const std::map<std::string, FieldOpKind> ops = {
      {"=", FieldOpKind::kSet},  {"=?", FieldOpKind::kSetIfOriginal}, {"+=", FieldOpKind::kAdd},
      {"-=", FieldOpKind::kSub}, {"*=", FieldOpKind::kMul},           {"|=", FieldOpKind::kOr},
      {"&~=", FieldOpKind::kClear}};
  op.op = ops.at(opname);
  const bool is_float = op.type == FieldType::kF32;
  if ((op.op == FieldOpKind::kOr || op.op == FieldOpKind::kClear) && is_float)
    return err = "bit operations need an integer type", false;
  if (is_float || op.op == FieldOpKind::kMul) {
    if (!ParseDouble(rest, op.value)) return err = "bad number '" + rest + "'", false;
  } else if (!ParseInt(rest, op.ivalue)) {
    return err = "bad integer '" + rest + "'", false;
  }
  if (!is_float && op.op != FieldOpKind::kMul && (op.op == FieldOpKind::kSet || op.op == FieldOpKind::kSetIfOriginal)) {
    long long lo, hi;
    Range(op.type, lo, hi);
    if (op.ivalue < lo || op.ivalue > hi) return err = rest + " does not fit " + type, false;
  }
  op.text = Trim(line);
  return true;
}

}  // namespace

Patch ParsePatch(const std::string& src, std::vector<std::string>& errors) {
  Patch out;
  std::set<std::string> fields;
  std::istringstream in(src);
  std::string line;
  for (int no = 1; std::getline(in, line); ++no) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (no == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
    const std::string t = Trim(line);
    if (t.empty() || t[0] == '#') continue;
    const std::string where = "line " + std::to_string(no) + ": ";
    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
      errors.push_back(where + "no '='");
      continue;
    }
    const std::vector<std::string> left = Tokens(line.substr(0, eq));
    if (left.empty()) {
      errors.push_back(where + "empty key");
      continue;
    }
    if (IsRecord(Join(left, 0, left.size()))) {
      FieldOp op;
      std::string err;
      if (!ParseFieldLine(line, op, err)) {
        errors.push_back(where + err);
        continue;
      }
      // One absolute change per field and file; relative ones may repeat.
      if (!IsRelative(op.op) && !fields.insert(FieldName(op)).second) {
        errors.push_back(where + "field " + FieldName(op) + " set twice");
        continue;
      }
      out.ops.push_back(std::move(op));
      continue;
    }
    const std::string key = Join(left, 0, left.size());
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
        errors.push_back(where + "unknown escape (use \\n or \\\\)");
        bad = true;
        break;
      }
      ++i;
    }
    if (bad) continue;
    if (!out.strings.emplace(key, text).second) errors.push_back(where + "key '" + key + "' repeated");
  }
  return out;
}

bool Merge(const Bytes& base, const std::vector<Source>& sources, MergeResult& res, std::string& err) {
  res = {};
  RawUnits b;
  if (!Units(base, b, err)) return false;
  const std::set<std::string> keys = Keys(b);

  // An operation is usable when its record exists and the field fits.
  auto usable = [&](const Source& s, const FieldOp& op) {
    const auto it = b.find(op.record);
    if (it == b.end()) {
      res.notes.push_back(s.mod + ": no record '" + op.record + "'; '" + op.text + "' skipped");
      return false;
    }
    if (op.offset + Width(op.type) > it->second.size()) {
      res.notes.push_back(s.mod + ": '" + op.text + "' is past the end of a " +
                          std::to_string(it->second.size()) + "-byte record; skipped");
      return false;
    }
    return true;
  };

  // Each source's version: its whole copy (or the original), its strings,
  // and its plain sets.
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
      res.notes.push_back(s.mod + ": its copy is not a file of the same kind (" + e + "); skipped");
      continue;
    }
    RawUnits patch;
    for (const auto& [key, text] : s.patch.strings) {
      std::string pe;
      if (!EncodeUnits(ref, {{key, text}}, patch, pe)) res.notes.push_back(s.mod + ": patch " + pe + "; line skipped");
    }
    if (s.has_file && Keys(u) != keys) {
      if (!restructured) {
        restructured = &s;
        for (const auto& op : s.patch.ops) {
          const auto it = u.find(op.record);
          if (it == u.end() || op.offset + Width(op.type) > it->second.size()) {
            res.notes.push_back(s.mod + ": '" + op.text + "' does not fit its own copy; skipped");
            continue;
          }
          Apply(patch.count(op.record) ? patch[op.record] : (patch[op.record] = it->second), op);
        }
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
    for (const auto& op : s.patch.ops)
      if (op.op == FieldOpKind::kSet && usable(s, op)) Apply(u[op.record], op);
    versions.push_back({&s, std::move(u)});
  }
  if (restructured) {
    for (const auto& v : versions) {
      bool changes = !v.src->patch.ops.empty();
      for (const auto& k : keys) changes = changes || v.units.at(k) != b.at(k);
      if (changes)
        res.notes.push_back(v.src->mod + ": changes to this file dropped; " + restructured->mod +
                            " restructures it (needs-patch)");
    }
    return true;
  }

  // Absolute changes. Strings: the best-ranked changer wins the whole
  // string. Records: byte by byte.
  RawUnits out;
  for (const auto& k : keys) {
    const Bytes& orig = b.at(k);
    if (IsRecord(k)) {
      Bytes merged = orig;
      std::vector<const Version*> owner(orig.size(), nullptr);
      std::set<std::pair<std::string, std::string>> clashes;
      std::set<std::string> same;
      for (const auto& v : versions) {
        const Bytes& x = v.units.at(k);
        for (size_t i = 0; i < x.size(); ++i) {
          if (x[i] == orig[i]) continue;
          if (!owner[i]) owner[i] = &v, merged[i] = x[i];
          else if (x[i] != merged[i]) clashes.insert({owner[i]->src->mod, v.src->mod});
          else if (owner[i] != &v) same.insert(owner[i]->src->mod + ", " + v.src->mod);
        }
      }
      for (const auto& [w, l] : clashes) res.notes.push_back(k + ": " + w + " over " + l + " (priority)");
      for (const auto& m : same) res.notes.push_back(k + ": " + m + " (identical)");
      if (merged != orig) out[k] = std::move(merged);
      continue;
    }
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

  auto rec = [&](const std::string& k) -> Bytes& {
    auto it = out.find(k);
    return it != out.end() ? it->second : (out[k] = b.at(k));
  };
  // Set-if-original: best-ranked first; applies while the field is still
  // the original.
  for (const auto& v : versions)
    for (const auto& op : v.src->patch.ops) {
      if (op.op != FieldOpKind::kSetIfOriginal || !usable(*v.src, op)) continue;
      Bytes& r = rec(op.record);
      if (FieldEqual(r, b.at(op.record), op)) Apply(r, op);
      else res.notes.push_back(FieldName(op) + ": " + v.src->mod + "'s '" + op.text + "' not applied; another mod changed it");
    }
  // Relative operations: lowest-ranked mod first, so the best-ranked applies last.
  std::map<std::string, std::vector<std::string>> by_field;
  for (auto v = versions.rbegin(); v != versions.rend(); ++v)
    for (const auto& op : v->src->patch.ops) {
      if (!IsRelative(op.op) || !usable(*v->src, op)) continue;
      Apply(rec(op.record), op);
      auto& mods = by_field[FieldName(op)];
      if (mods.empty() || mods.back() != v->src->mod) mods.push_back(v->src->mod);
    }
  for (const auto& [field, mods] : by_field) {
    if (mods.size() < 2) continue;
    std::string n = field + ": ";
    for (size_t i = 0; i < mods.size(); ++i) n += (i ? ", then " : "") + mods[i];
    res.notes.push_back(n + " (operation)");
  }
  for (auto it = out.begin(); it != out.end();) it = it->second == b.at(it->first) ? out.erase(it) : std::next(it);
  res.changed = out.size();
  return RebuildRaw(base, out, res.out, err);
}

}  // namespace ffxii::text
