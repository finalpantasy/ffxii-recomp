// Game text containers (text_formats.h). A port of the layout code in
// tools/ff12text.py, working on raw bytes: the character table is needed
// only to encode replacement text.

#include "text_formats.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <unordered_map>

namespace ffxii::text {
namespace {

uint32_t U32(const Bytes& d, size_t at) {
  uint32_t v = 0;
  std::memcpy(&v, d.data() + at, 4);
  return v;
}
uint16_t U16(const Bytes& d, size_t at) {
  uint16_t v = 0;
  std::memcpy(&v, d.data() + at, 2);
  return v;
}
void Put32(Bytes& d, size_t at, uint32_t v) { std::memcpy(d.data() + at, &v, 4); }
void Add32(Bytes& d, uint32_t v) {
  const size_t at = d.size();
  d.resize(at + 4);
  Put32(d, at, v);
}
void Pad(Bytes& d, size_t align) {
  while (d.size() % align) d.push_back(0);
}

// --- character table (ff12text.py _CHARS) -----------------------------------

const std::unordered_map<uint32_t, uint8_t>& CharTable() {
  static const std::unordered_map<uint32_t, uint8_t> table = [] {
    std::unordered_map<uint32_t, uint8_t> t;
    t[' '] = 0x04;
    t['\n'] = 0x02;
    for (int i = 0; i < 26; ++i) {
      t['A' + i] = uint8_t(0x20 + i);
      t['a' + i] = uint8_t(0x3A + i);
    }
    for (int i = 0; i < 10; ++i) t['0' + i] = uint8_t(0x85 + i);
    const char32_t accents[] = U"ùúûüß";
    for (int i = 0; i < 5; ++i) t[accents[i]] = uint8_t(0x80 + i);
    // 0x8F-0xB3; 0 marks a byte with no glyph.
    const char32_t run[] = U"–«»\0¡¿„\0~`!?@#$%^&*/_+-=,.;:\\'\"()[]<>";
    for (int i = 0; i < 0xB4 - 0x8F; ++i)
      if (run[i]) t[run[i]] = uint8_t(0x8F + i);
    const std::pair<char32_t, uint8_t> more[] = {
        {U'|', 0xB6}, {U'→', 0xBB}, {U'²', 0xBE}, {U'³', 0xBF}, {U'⁴', 0xC0},
        {U'⁵', 0xC1}, {U'≤', 0xC3}, {U'≥', 0xC4}, {U'…', 0xC5}, {U'“', 0xD1},
        {U'”', 0xD2}, {U'×', 0xD9}, {U'∞', 0xDA}, {U'•', 0xDB}};
    for (const auto& [c, b] : more) t[c] = b;
    return t;
  }();
  return table;
}

// Next UTF-8 code point of s at i (advances i); 0xFFFD on bad input.
uint32_t NextCodePoint(const std::string& s, size_t& i) {
  const auto b = static_cast<unsigned char>(s[i++]);
  if (b < 0x80) return b;
  int extra = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : b >= 0xC0 ? 1 : -1;
  if (extra < 0) return 0xFFFD;
  uint32_t cp = b & (0x3F >> extra);
  for (int k = 0; k < extra; ++k) {
    if (i >= s.size()) return 0xFFFD;
    cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
  }
  return cp;
}

constexpr char kEx00Text[] = "{ex00}";

bool EncodeUtf16(const std::string& s, Bytes& out) {
  for (size_t i = 0; i < s.size();) {
    uint32_t cp = NextCodePoint(s, i);
    if (cp >= 0x10000) {
      cp -= 0x10000;
      const uint16_t hi = uint16_t(0xD800 + (cp >> 10)), lo = uint16_t(0xDC00 + (cp & 0x3FF));
      out.insert(out.end(), {uint8_t(hi), uint8_t(hi >> 8), uint8_t(lo), uint8_t(lo >> 8)});
    } else {
      out.insert(out.end(), {uint8_t(cp), uint8_t(cp >> 8)});
    }
  }
  return true;
}

// --- EBP2 -------------------------------------------------------------------

constexpr size_t kEbpTextSlot = 0x18;
constexpr size_t kEbpHeaderEnd = 0x80;

struct Unit {
  uint32_t offset;  // as stored (sharing key)
  Bytes raw;        // without terminator (EBP2) or with it (st2e, word entries)
};

bool CStr(const Bytes& d, size_t at, Bytes& out) {
  if (at >= d.size()) return false;
  const auto* p = static_cast<const uint8_t*>(std::memchr(d.data() + at, 0, d.size() - at));
  if (!p) return false;
  out.assign(d.data() + at, p);
  return true;
}

bool EbpRead(const Bytes& d, uint32_t& t, std::vector<Unit>& units, std::vector<uint16_t>& attrs) {
  if (d.size() < kEbpHeaderEnd || std::memcmp(d.data(), "EBP2", 4) != 0) return false;
  t = U32(d, kEbpTextSlot);
  if (t == 0) return true;
  if (t + 4 > d.size()) return false;
  const size_t n = U16(d, t) / 4;
  if (t + 4 * n > d.size()) return false;
  for (size_t i = 0; i < n; ++i) {
    Unit u;
    u.offset = U16(d, t + 4 * i);
    attrs.push_back(U16(d, t + 4 * i + 2));
    if (!CStr(d, t + u.offset, u.raw)) return false;
    units.push_back(std::move(u));
  }
  return true;
}

size_t EbpSectionEnd(const Bytes& d, uint32_t t) {
  size_t end = d.size();
  for (size_t slot = 8; slot < kEbpHeaderEnd; slot += 4) {
    const uint32_t v = U32(d, slot);
    if (v > t && v < end) end = v;
  }
  return end;
}

// Strings laid out in the order of their old offsets; equal (old offset,
// new bytes) pairs share one copy. `term` bytes follow each string.
void LayOut(const std::vector<Unit>& old, const std::vector<Bytes>& now, size_t table, const Bytes& term,
            Bytes& body, std::vector<uint32_t>& offs) {
  std::vector<size_t> order(old.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(),
                   [&](size_t a, size_t b) { return old[a].offset < old[b].offset; });
  std::map<std::pair<uint32_t, Bytes>, uint32_t> placed;
  offs.assign(old.size(), 0);
  for (size_t i : order) {
    auto key = std::make_pair(old[i].offset, now[i]);
    auto it = placed.find(key);
    if (it == placed.end()) {
      it = placed.emplace(std::move(key), uint32_t(table + body.size())).first;
      body.insert(body.end(), now[i].begin(), now[i].end());
      body.insert(body.end(), term.begin(), term.end());
    }
    offs[i] = it->second;
  }
}

bool EbpBuild(const Bytes& d, const std::map<size_t, Bytes>& rep, Bytes& out, std::string& err) {
  uint32_t t = 0;
  std::vector<Unit> old;
  std::vector<uint16_t> attrs;
  if (!EbpRead(d, t, old, attrs) || t == 0) return err = "not an EBP2 file with text", false;
  std::vector<Bytes> now;
  for (const auto& u : old) now.push_back(u.raw);
  for (const auto& [i, b] : rep) {
    if (i >= now.size()) return err = "string " + std::to_string(i) + " out of range", false;
    now[i] = b;
  }
  Bytes body;
  std::vector<uint32_t> offs;
  const size_t table = 4 * old.size();
  LayOut(old, now, table, Bytes{0}, body, offs);
  if (table + body.size() > 0xFFFF) return err = "text section over 64 KiB", false;
  Bytes sec;
  for (size_t i = 0; i < old.size(); ++i) {
    sec.insert(sec.end(), {uint8_t(offs[i]), uint8_t(offs[i] >> 8), uint8_t(attrs[i]), uint8_t(attrs[i] >> 8)});
  }
  sec.insert(sec.end(), body.begin(), body.end());
  const size_t old_end = EbpSectionEnd(d, t), old_size = old_end - t;
  const size_t size = sec.size() <= old_size ? old_size : (sec.size() + 0x3F) & ~size_t(0x3F);
  sec.resize(size, 0);
  const int64_t delta = int64_t(size) - int64_t(old_size);
  out.assign(d.begin(), d.begin() + t);
  out.insert(out.end(), sec.begin(), sec.end());
  out.insert(out.end(), d.begin() + old_end, d.end());
  for (size_t slot = 8; slot < kEbpHeaderEnd; slot += 4) {
    const uint32_t v = U32(out, slot);
    if (v > t) Put32(out, slot, uint32_t(int64_t(v) + delta));
  }
  return true;
}

// --- st2e -------------------------------------------------------------------

bool St2Raw(const Bytes& d, size_t at, Bytes& out) {
  if (at + 4 <= d.size() && std::memcmp(d.data() + at, "ex00", 4) == 0) {
    for (size_t i = at + 4; i + 1 < d.size(); i += 2)
      if (d[i] == 0 && d[i + 1] == 0) return out.assign(d.begin() + at, d.begin() + i + 2), true;
    return false;
  }
  if (!CStr(d, at, out)) return false;
  out.push_back(0);
  return true;
}

bool St2Header(const Bytes& d, size_t& sec, size_t& tab, size_t& n) {
  if (d.size() < 0x20 || std::memcmp(d.data(), "st2e", 4) != 0) return false;
  sec = U32(d, 0x14);
  if (sec == 0 || sec + 12 > d.size() || U32(d, sec) != 1) return false;
  tab = sec + U32(d, sec + 4);
  if (tab + 4 > d.size()) return false;
  n = U32(d, tab);
  return n == U32(d, 4) && tab + 4 + 4 * (n + 1) <= d.size();
}

// Fixed-size records from 0x20: count at 4, record size at 8, before any text
// section. Data tables (weapons, items, abilities) are st2e files with only
// records.
bool St2Records(const Bytes& d, size_t& count, size_t& size) {
  if (d.size() < 0x20 || std::memcmp(d.data(), "st2e", 4) != 0) return false;
  count = U32(d, 4);
  size = U32(d, 8);
  if (count == 0 || size == 0 || U32(d, 0xC) != 0x20) return false;
  const size_t sec = U32(d, 0x14);
  return 0x20 + count * size <= (sec ? sec : d.size());
}

void RecordUnits(const Bytes& d, const std::string& prefix, std::map<std::string, Bytes>& out) {
  size_t count, size;
  if (!St2Records(d, count, size)) return;
  for (size_t i = 0; i < count; ++i)
    out[prefix + "r " + std::to_string(i)] = Bytes(d.begin() + 0x20 + i * size, d.begin() + 0x20 + (i + 1) * size);
}

bool IsRecordKey(const std::string& key) { return key.size() > 2 && key[0] == 'r' && key[1] == ' '; }

// Writes "r <i>" units into d in place (records never change size).
bool PutRecords(Bytes& d, const std::map<std::string, Bytes>& rep, std::string& err);

bool St2Read(const Bytes& d, std::vector<Unit>& units) {
  size_t sec, tab, n;
  if (!St2Header(d, sec, tab, n)) return false;
  for (size_t i = 0; i < n; ++i) {
    Unit u;
    u.offset = U32(d, tab + 4 + 4 * i);
    if (!St2Raw(d, tab + u.offset, u.raw)) return false;
    units.push_back(std::move(u));
  }
  return true;
}

bool St2Build(const Bytes& d, const std::map<size_t, Bytes>& rep, Bytes& out, std::string& err) {
  size_t sec, tab, n;
  std::vector<Unit> old;
  if (!St2Header(d, sec, tab, n) || !St2Read(d, old)) return err = "not an st2e text table", false;
  std::vector<Bytes> now;
  for (const auto& u : old) now.push_back(u.raw);
  for (const auto& [i, b] : rep) {
    if (i >= n) return err = "string " + std::to_string(i) + " out of range", false;
    now[i] = b;
  }
  const size_t table = 4 + 4 * (n + 1);
  Bytes body;
  std::vector<uint32_t> offs;
  LayOut(old, now, table, Bytes{}, body, offs);
  out.assign(d.begin(), d.begin() + tab);
  Add32(out, uint32_t(n));
  for (uint32_t o : offs) Add32(out, o);
  Add32(out, uint32_t(table + body.size()));
  out.insert(out.end(), body.begin(), body.end());
  Put32(out, sec + 8, uint32_t(out.size() - sec));
  Pad(out, 4);
  return true;
}

// --- word tables --------------------------------------------------------------

bool IsStrTab(const Bytes& d, size_t at, size_t size) {
  if (size < 8 || at + size > d.size()) return false;
  const size_t m = U32(d, at);
  if (4 + 4 * (m + 1) > size) return false;
  uint32_t prev = 0;
  for (size_t i = 0; i <= m; ++i) {
    const uint32_t o = U32(d, at + 4 + 4 * i);
    if (o < prev) return false;
    prev = o;
  }
  return U32(d, at + 4) == 4 + 4 * (m + 1) && U32(d, at + 4 + 4 * m) == size;
}

bool IsWords(const Bytes& d) {
  if (d.size() < 8) return false;
  const size_t n = U32(d, 0);
  if (n == 0 || 4 + 4 * (n + 1) > d.size()) return false;
  if (U32(d, 4) != 4 + 4 * (n + 1) || U32(d, 4 + 4 * n) != d.size()) return false;
  for (size_t k = 0; k < n; ++k) {
    const uint32_t a = U32(d, 4 + 4 * k), b = U32(d, 8 + 4 * k);
    if (b < a || !IsStrTab(d, a, b - a)) return false;
  }
  return true;
}

using WordTables = std::vector<std::vector<Bytes>>;  // entries with padding

WordTables WordsRead(const Bytes& d) {
  WordTables t;
  const size_t n = U32(d, 0);
  for (size_t k = 0; k < n; ++k) {
    const size_t b = U32(d, 4 + 4 * k);
    const size_t m = U32(d, b);
    std::vector<Bytes> ents;
    for (size_t i = 0; i < m; ++i) {
      const size_t s = b + U32(d, b + 4 + 4 * i), e = b + U32(d, b + 8 + 4 * i);
      ents.emplace_back(d.begin() + s, d.begin() + e);
    }
    t.push_back(std::move(ents));
  }
  return t;
}

Bytes WordsBuild(const WordTables& tables) {
  std::vector<Bytes> subs;
  for (const auto& ents : tables) {
    Bytes sub;
    const size_t head = 4 + 4 * (ents.size() + 1);
    Add32(sub, uint32_t(ents.size()));
    size_t pos = head;
    for (const auto& e : ents) {
      Add32(sub, uint32_t(pos));
      pos += e.size();
    }
    Add32(sub, uint32_t(pos));
    for (const auto& e : ents) sub.insert(sub.end(), e.begin(), e.end());
    subs.push_back(std::move(sub));
  }
  Bytes out;
  const size_t head = 4 + 4 * (subs.size() + 1);
  Add32(out, uint32_t(subs.size()));
  size_t pos = head;
  for (const auto& s : subs) {
    Add32(out, uint32_t(pos));
    pos += s.size();
  }
  Add32(out, uint32_t(pos));
  for (const auto& s : subs) out.insert(out.end(), s.begin(), s.end());
  return out;
}

// --- packs --------------------------------------------------------------------

constexpr size_t kPackAlign = 8;

bool PackSections(const Bytes& d, std::vector<Bytes>& secs) {
  if (d.size() < 8) return false;
  const size_t n = U32(d, 0);
  if (n == 0 || 4 + 4 * (n + 1) > d.size()) return false;
  std::vector<uint32_t> o(n + 1);
  for (size_t k = 0; k <= n; ++k) o[k] = U32(d, 4 + 4 * k);
  if (!(d.size() - kPackAlign < o[n] && o[n] <= d.size())) return false;
  if (!std::is_sorted(o.begin(), o.end()) || o[0] < 4 + 4 * (n + 1)) return false;
  bool any = false;
  for (size_t k = 0; k < n; ++k) {
    secs.emplace_back(d.begin() + o[k], d.begin() + o[k + 1]);
    any |= secs.back().size() >= 4 && std::memcmp(secs.back().data(), "st2e", 4) == 0;
  }
  return any;
}

size_t WordsEnd(const Bytes& s) {
  if (s.size() < 8) return 0;
  const size_t n = U32(s, 0);
  if (n == 0 || 4 + 4 * (n + 1) > s.size()) return 0;
  const size_t end = U32(s, 4 + 4 * n);
  if (end > s.size()) return 0;
  return IsWords(Bytes(s.begin(), s.begin() + end)) ? end : 0;
}

Kind SectionKind(const Bytes& s) {
  if (s.size() >= 0x20 && std::memcmp(s.data(), "st2e", 4) == 0 && U32(s, 0x14) != 0) return Kind::kSt2e;
  if (WordsEnd(s)) return Kind::kWords;
  return Kind::kNone;
}

Bytes Trim(const Bytes& s) {
  if (SectionKind(s) == Kind::kSt2e) {
    const size_t sec = U32(s, 0x14);
    size_t end = sec + U32(s, sec + 8);
    end += (4 - end % 4) % 4;
    return Bytes(s.begin(), s.begin() + std::min(end, s.size()));
  }
  return Bytes(s.begin(), s.begin() + WordsEnd(s));
}

Bytes PackBuild(const std::vector<Bytes>& secs) {
  const size_t n = secs.size(), head = 4 + 4 * (n + 1);
  size_t pos = head + (kPackAlign - head % kPackAlign) % kPackAlign;
  Bytes body;
  std::vector<uint32_t> offs;
  for (size_t k = 0; k < n; ++k) {
    offs.push_back(uint32_t(pos + body.size()));
    body.insert(body.end(), secs[k].begin(), secs[k].end());
    if (k + 1 < n) Pad(body, kPackAlign);
  }
  Bytes out;
  Add32(out, uint32_t(n));
  for (uint32_t o : offs) Add32(out, o);
  Add32(out, uint32_t(pos + body.size()));
  out.resize(pos, 0);
  out.insert(out.end(), body.begin(), body.end());
  Pad(out, kPackAlign);
  return out;
}

// --- keys -----------------------------------------------------------------------

bool ParseKey(const std::string& key, std::vector<std::string>& parts) {
  std::istringstream in(key);
  std::string p;
  while (in >> p) parts.push_back(p);
  return !parts.empty();
}

bool ToIndex(const std::string& s, size_t& v) {
  if (s.empty() || s.size() > 9 || s.find_first_not_of("0123456789") != std::string::npos) return false;
  v = std::stoul(s);
  return true;
}

// Encodes the text form of one unit for its container.
bool EncodeUnit(Kind kind, const std::string& text, const Bytes& old, Bytes& out, std::string& err) {
  out.clear();
  if (kind == Kind::kSt2e && text.rfind(kEx00Text, 0) == 0) {
    out = {'e', 'x', '0', '0'};
    EncodeUtf16(text.substr(sizeof kEx00Text - 1), out);
    out.insert(out.end(), {0, 0});
    return true;
  }
  if (!Encode(text, kind == Kind::kWords, out, err)) return false;
  if (kind == Kind::kSt2e) out.push_back(0);
  if (kind == Kind::kWords) {
    size_t pad = 0;
    while (pad < old.size() && old[old.size() - 1 - pad] == 0) ++pad;
    out.insert(out.end(), pad, 0);
    Pad(out, 4);
  }
  return true;
}

bool RebuildKind(Kind kind, const Bytes& d, const RawUnits& rep, Bytes& out, std::string& err);

bool RebuildWords(const Bytes& d, const RawUnits& rep, Bytes& out, std::string& err) {
  WordTables t = WordsRead(d);
  for (const auto& [key, raw] : rep) {
    std::vector<std::string> p;
    size_t ti, ei;
    if (!ParseKey(key, p) || p.size() != 3 || p[0] != "w" || !ToIndex(p[1], ti) || !ToIndex(p[2], ei))
      return err = "bad word key '" + key + "'", false;
    if (ti >= t.size() || ei >= t[ti].size()) return err = "word " + key + " out of range", false;
    if (raw.size() % 4) return err = "word " + key + " is not padded to 4 bytes", false;
    t[ti][ei] = raw;
  }
  out = WordsBuild(t);
  return true;
}

bool PutRecords(Bytes& d, const std::map<std::string, Bytes>& rep, std::string& err) {
  size_t count = 0, size = 0;
  const bool has = St2Records(d, count, size);
  for (const auto& [key, raw] : rep) {
    std::vector<std::string> p;
    size_t i;
    if (!ParseKey(key, p) || p.size() != 2 || p[0] != "r" || !ToIndex(p[1], i))
      return err = "bad record key '" + key + "'", false;
    if (!has || i >= count) return err = "record " + p[1] + " out of range", false;
    if (raw.size() != size) return err = "record " + p[1] + " must stay " + std::to_string(size) + " bytes", false;
    std::copy(raw.begin(), raw.end(), d.begin() + 0x20 + i * size);
  }
  return true;
}

bool RebuildStrings(Kind kind, const Bytes& d0, const RawUnits& rep0, Bytes& out, std::string& err) {
  // Records first, in place; then the strings, if any changed.
  RawUnits recs, rep;
  for (const auto& kv : rep0) (IsRecordKey(kv.first) ? recs : rep).insert(kv);
  Bytes d = d0;
  if (!recs.empty() && (kind != Kind::kSt2e || !PutRecords(d, recs, err)))
    return err.empty() ? (err = "this file has no records", false) : false;
  if (rep.empty() && kind == Kind::kSt2e) return out = std::move(d), true;
  std::map<size_t, Bytes> idx;
  for (const auto& [key, raw] : rep) {
    std::vector<std::string> p;
    size_t i;
    if (!ParseKey(key, p) || p.size() != 2 || p[0] != "s" || !ToIndex(p[1], i))
      return err = "bad string key '" + key + "'", false;
    idx[i] = raw;
  }
  return kind == Kind::kEbp ? EbpBuild(d, idx, out, err) : St2Build(d, idx, out, err);
}

bool RebuildPack(const Bytes& d, const RawUnits& rep, Bytes& out, std::string& err) {
  std::vector<Bytes> secs;
  if (!PackSections(d, secs)) return err = "not a pack", false;
  std::map<size_t, RawUnits> per;
  for (const auto& [key, raw] : rep) {
    std::vector<std::string> p;
    size_t k;
    if (!ParseKey(key, p) || p.size() < 3 || p[0] != "p" || !ToIndex(p[1], k))
      return err = "bad pack key '" + key + "'", false;
    if (k >= secs.size()) return err = "section " + p[1] + " out of range", false;
    std::string sub = p[2];
    for (size_t j = 3; j < p.size(); ++j) sub += " " + p[j];
    per[k][sub] = raw;
  }
  for (auto& [k, r] : per) {
    const bool st2 = secs[k].size() >= 4 && std::memcmp(secs[k].data(), "st2e", 4) == 0;
    const Kind sk = st2 ? Kind::kSt2e : SectionKind(secs[k]);
    if (sk == Kind::kNone) return err = "section " + std::to_string(k) + " holds no text or records", false;
    // A records-only change keeps the section's bytes (and padding) as they are.
    const bool strings = std::any_of(r.begin(), r.end(), [](const auto& kv) { return !IsRecordKey(kv.first); });
    Bytes rebuilt;
    if (!RebuildKind(sk, strings ? Trim(secs[k]) : secs[k], r, rebuilt, err))
      return err = "section " + std::to_string(k) + ": " + err, false;
    secs[k] = std::move(rebuilt);
  }
  out = PackBuild(secs);
  return true;
}

bool RebuildKind(Kind kind, const Bytes& d, const RawUnits& rep, Bytes& out, std::string& err) {
  switch (kind) {
    case Kind::kEbp:
    case Kind::kSt2e: return RebuildStrings(kind, d, rep, out, err);
    case Kind::kWords: return RebuildWords(d, rep, out, err);
    case Kind::kPack: return RebuildPack(d, rep, out, err);
    default: return err = "not a text file", false;
  }
}

void UnitsKind(Kind kind, const Bytes& d, const std::string& prefix, std::map<std::string, Bytes>& out) {
  if (kind == Kind::kSt2e) RecordUnits(d, prefix, out);
  if (kind == Kind::kEbp || kind == Kind::kSt2e) {
    std::vector<Unit> u;
    uint32_t t = 0;
    std::vector<uint16_t> attrs;
    if (kind == Kind::kEbp ? EbpRead(d, t, u, attrs) : St2Read(d, u))
      for (size_t i = 0; i < u.size(); ++i) out[prefix + "s " + std::to_string(i)] = u[i].raw;
  } else if (kind == Kind::kWords) {
    const WordTables t = WordsRead(d);
    for (size_t k = 0; k < t.size(); ++k)
      for (size_t i = 0; i < t[k].size(); ++i)
        out[prefix + "w " + std::to_string(k) + " " + std::to_string(i)] = t[k][i];
  } else if (kind == Kind::kPack) {
    std::vector<Bytes> secs;
    if (!PackSections(d, secs)) return;
    for (size_t k = 0; k < secs.size(); ++k) {
      const std::string pre = prefix + "p " + std::to_string(k) + " ";
      if (secs[k].size() >= 4 && std::memcmp(secs[k].data(), "st2e", 4) == 0)
        UnitsKind(Kind::kSt2e, secs[k], pre, out);
      else if (SectionKind(secs[k]) == Kind::kWords)
        UnitsKind(Kind::kWords, Trim(secs[k]), pre, out);
    }
  }
}

}  // namespace

Kind Detect(const Bytes& d) {
  if (d.size() >= 4 && std::memcmp(d.data(), "EBP2", 4) == 0)
    return d.size() >= kEbpHeaderEnd && U32(d, kEbpTextSlot) ? Kind::kEbp : Kind::kNone;
  if (d.size() >= 4 && std::memcmp(d.data(), "st2e", 4) == 0) {
    size_t sec, tab, n, count, size;
    return St2Header(d, sec, tab, n) || St2Records(d, count, size) ? Kind::kSt2e : Kind::kNone;
  }
  if (IsWords(d)) return Kind::kWords;
  std::vector<Bytes> secs;
  if (PackSections(d, secs)) return Kind::kPack;
  return Kind::kNone;
}

const char* KindName(Kind k) {
  switch (k) {
    case Kind::kEbp: return "ebp";
    case Kind::kSt2e: return "st2e";
    case Kind::kWords: return "words";
    case Kind::kPack: return "pack";
    default: return "none";
  }
}

bool Encode(const std::string& text, bool allow_nul, Bytes& out, std::string& err) {
  const auto& table = CharTable();
  for (size_t i = 0; i < text.size();) {
    if (text[i] == '{') {
      const size_t j = text.find('}', i);
      if (j == std::string::npos) return err = "unclosed '{'", false;
      std::istringstream in(text.substr(i + 1, j - i - 1));
      std::string h;
      while (in >> h) {
        if (h.size() != 2 || h.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
          return err = "bad byte '" + h + "' in {}", false;
        out.push_back(uint8_t(std::stoul(h, nullptr, 16)));
      }
      i = j + 1;
      continue;
    }
    const size_t at = i;
    const uint32_t cp = NextCodePoint(text, i);
    const auto it = table.find(cp);
    if (it == table.end()) return err = "no game byte for '" + text.substr(at, i - at) + "'", false;
    out.push_back(it->second);
  }
  if (!allow_nul && std::find(out.begin(), out.end(), 0) != out.end()) return err = "text holds a 00 byte", false;
  return true;
}

bool Units(const Bytes& d, std::map<std::string, Bytes>& out, std::string& err) {
  const Kind k = Detect(d);
  if (k == Kind::kNone) return err = "not a text file", false;
  UnitsKind(k, d, "", out);
  return true;
}

bool RebuildRaw(const Bytes& d, const RawUnits& rep, Bytes& out, std::string& err) {
  return RebuildKind(Detect(d), d, rep, out, err);
}

bool EncodeUnits(const Bytes& d, const Replacements& rep, RawUnits& out, std::string& err) {
  const Kind k = Detect(d);
  RawUnits units;
  if (!Units(d, units, err)) return false;
  for (const auto& [key, text] : rep) {
    std::vector<std::string> p;
    ParseKey(key, p);
    std::string norm;
    for (const auto& x : p) norm += (norm.empty() ? "" : " ") + x;
    if (IsRecordKey(p[0] == "p" && p.size() > 2 ? norm.substr(norm.find(' ', 2) + 1) : norm))
      return err = "'" + norm + "' is a data record; use a field operation", false;
    const auto it = units.find(norm);
    if (it == units.end()) return err = "no unit '" + key + "' in this " + KindName(k) + " file", false;
    // The unit's own container: a pack section is st2e or a word table.
    const Kind uk = p[0] == "p" ? (p[2] == "w" ? Kind::kWords : Kind::kSt2e) : k;
    Bytes b;
    if (!EncodeUnit(uk, text, it->second, b, err)) return err = norm + ": " + err, false;
    out[norm] = std::move(b);
  }
  return true;
}

bool Rebuild(const Bytes& d, const Replacements& rep, Bytes& out, std::string& err) {
  RawUnits raw;
  return EncodeUnits(d, rep, raw, err) && RebuildRaw(d, raw, out, err);
}

}  // namespace ffxii::text
