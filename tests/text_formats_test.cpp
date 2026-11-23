// Host test for src/text_formats.cpp.
// With no arguments: character table and synthetic-file checks.
// With file arguments (extracted game files, never committed): each text file
// must rebuild byte for byte with no replacements, and replacing one unit
// must change only that unit.

#include "../src/text_formats.h"
#include "../src/text_merge.h"
#include "../src/text_mods.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using namespace ffxii::text;

static int g_fail = 0;
#define CHECK(c)                                                    \
  do {                                                              \
    if (!(c)) {                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);      \
      ++g_fail;                                                     \
    }                                                               \
  } while (0)

static Bytes Enc(const std::string& s, bool nul = false) {
  Bytes b;
  std::string err;
  if (!Encode(s, nul, b, err)) std::printf("encode '%s': %s\n", s.c_str(), err.c_str());
  return b;
}

static void Put32(Bytes& d, uint32_t v) {
  for (int i = 0; i < 4; ++i) d.push_back(uint8_t(v >> (8 * i)));
}

// st2e file with the given raw strings (terminators included).
static Bytes MakeSt2(const std::vector<Bytes>& s) {
  Bytes d = {'s', 't', '2', 'e'};
  Put32(d, uint32_t(s.size()));
  Put32(d, 4);
  Put32(d, 0x20);
  Put32(d, 0);
  const size_t sec = 0x20 + 4 * s.size();
  Put32(d, uint32_t(sec));
  Put32(d, 0);
  Put32(d, 0);
  for (size_t i = 0; i < s.size(); ++i) Put32(d, uint32_t(i));
  Put32(d, 1);
  Put32(d, 12);
  Put32(d, 0);  // size, below
  const size_t tab = d.size();
  Put32(d, uint32_t(s.size()));
  size_t pos = 4 + 4 * (s.size() + 1);
  for (const auto& x : s) {
    Put32(d, uint32_t(pos));
    pos += x.size();
  }
  Put32(d, uint32_t(pos));
  for (const auto& x : s) d.insert(d.end(), x.begin(), x.end());
  const uint32_t size = uint32_t(d.size() - sec);
  for (int i = 0; i < 4; ++i) d[sec + 8 + i] = uint8_t(size >> (8 * i));
  while (d.size() % 4) d.push_back(0);
  (void)tab;
  return d;
}

static void Synthetic() {
  CHECK(Enc("Az") == (Bytes{0x20, 0x53}));
  CHECK(Enc("a b\n09") == (Bytes{0x3A, 0x04, 0x3B, 0x02, 0x85, 0x8E}));
  CHECK(Enc("–!>") == (Bytes{0x8F, 0x99, 0xB3}));
  CHECK(Enc("…×{F0 01}") == (Bytes{0xC5, 0xD9, 0xF0, 0x01}));
  CHECK(Enc("ß“”") == (Bytes{0x84, 0xD1, 0xD2}));
  Bytes b;
  std::string err;
  CHECK(!Encode("{00}", false, b, err));
  CHECK(Encode("a{00}b", true, b, err));
  b.clear();
  CHECK(!Encode("é", false, b, err));
  CHECK(!Encode("{1", false, b, err));

  Bytes hi = Enc("Hi"), yo = Enc("Yo");
  hi.push_back(0);
  yo.push_back(0);
  const Bytes st2 = MakeSt2({hi, yo, hi});
  CHECK(Detect(st2) == Kind::kSt2e);
  Bytes out;
  CHECK(Rebuild(st2, {}, out, err));
  CHECK(out == st2);
  CHECK(Rebuild(st2, {{"s 1", "Longer text"}}, out, err));
  std::map<std::string, Bytes> u;
  CHECK(Units(out, u, err));
  Bytes want = Enc("Longer text");
  want.push_back(0);
  CHECK(u.size() == 3 && u["s 0"] == hi && u["s 1"] == want && u["s 2"] == hi);
  CHECK(Rebuild(st2, {{"s 0", "{ex00}Ab"}}, out, err));
  CHECK(Units(out, u, err));
  CHECK(u["s 0"] == (Bytes{'e', 'x', '0', '0', 'A', 0, 'b', 0, 0, 0}));
  CHECK(!Rebuild(st2, {{"s 3", "x"}}, out, err));
  CHECK(!Rebuild(st2, {{"w 0 0", "x"}}, out, err));
}

static Bytes Z(const std::string& s) {
  Bytes b = Enc(s);
  b.push_back(0);
  return b;
}

static bool HasNote(const MergeResult& r, const std::string& n) {
  for (const auto& x : r.notes)
    if (x == n) return true;
  std::printf("missing note '%s'; have:\n", n.c_str());
  for (const auto& x : r.notes) std::printf("  %s\n", x.c_str());
  return false;
}

static void Merging() {
  std::vector<std::string> errs;
  const Replacements p = ParsePatch(
      "\xEF\xBB\xBF# comment\r\n"
      "s 0 = One\\nTwo\r\n"
      "  s   1=Back\\\\slash\n"
      "\n"
      "s 2 =  two spaces\n"
      "no equals\n"
      "s 3 = bad \\t escape\n"
      "s 0 = again\n",
      errs);
  CHECK(p.size() == 3);
  CHECK(p.count("s 0") && p.at("s 0") == "One\nTwo");
  CHECK(p.count("s 1") && p.at("s 1") == "Back\\slash");
  CHECK(p.count("s 2") && p.at("s 2") == " two spaces");
  CHECK(errs.size() == 3);

  const Bytes base = MakeSt2({Z("Hi"), Z("Yo"), Z("Hi"), Z("End")});
  Source a{"a", false, {}, {{"s 0", "A0"}, {"s 1", "Same"}}};
  Source b{"b", true, MakeSt2({Z("Hi"), Z("Same"), Z("B2"), Z("End")}), {}};
  Source c{"c", false, {}, {{"s 0", "C0"}, {"s 9", "nope"}}};
  MergeResult r;
  std::string err;
  CHECK(Merge(base, {a, b, c}, r, err));
  std::map<std::string, Bytes> u;
  CHECK(Units(r.out, u, err));
  CHECK(u["s 0"] == Z("A0") && u["s 1"] == Z("Same") && u["s 2"] == Z("B2") && u["s 3"] == Z("End"));
  CHECK(r.changed == 3 && r.merged == 1);
  CHECK(HasNote(r, "s 0: a over c (priority)"));
  CHECK(HasNote(r, "s 1: a, b (identical)"));
  CHECK(r.notes.size() == 3);  // and c's bad key

  // A copy with an extra string cannot merge; the best such copy is used.
  Source d{"d", true, MakeSt2({Z("Hi"), Z("Yo"), Z("Hi"), Z("End"), Z("New")}), {{"s 4", "Newer"}}};
  CHECK(Merge(base, {d, a}, r, err));
  CHECK(Units(r.out, u, err));
  CHECK(u.size() == 5 && u["s 4"] == Z("Newer") && u["s 0"] == Z("Hi"));
  CHECK(HasNote(r, "a: changes to this file dropped; d restructures it (needs-patch)"));

  // No sources: the original comes back.
  CHECK(Merge(base, {}, r, err) && r.out == base && r.changed == 0);
}

static bool ReadFile(const char* p, Bytes& d) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  d.assign(std::istreambuf_iterator<char>(f), {});
  return true;
}

int main(int argc, char** argv) {
  // --apply <in> <out> <key> <text> [<key> <text>...]: for cross-checks with ff12text.py.
  if (argc >= 4 && std::string(argv[1]) == "--apply") {
    Bytes d, out;
    std::string err;
    Replacements rep;
    for (int i = 4; i + 1 < argc; i += 2) rep[argv[i]] = argv[i + 1];
    if (!ReadFile(argv[2], d) || !Rebuild(d, rep, out, err)) return std::printf("apply: %s\n", err.c_str()), 1;
    std::ofstream(argv[3], std::ios::binary).write(reinterpret_cast<const char*>(out.data()), out.size());
    return 0;
  }
  // --mods <mods root> <FFXII_TZA.vbf>: the text merge report for a mods folder.
  if (argc == 4 && std::string(argv[1]) == "--mods") {
    ffxii::vbf::Archive arc;
    std::string err;
    if (!arc.Open(argv[3], err)) return std::printf("%s\n", err.c_str()), 1;
    const ffxii::mods::Index ix = ffxii::mods::Build(argv[2]);
    const ffxii::text::ModTextMerges m = ffxii::text::MergeModText(ix, arc);
    std::printf("%s%s%zu merged files, %zu problems\n", ix.Report().c_str(), m.report.c_str(), m.files.size(),
                m.problems);
    return m.problems ? 1 : 0;
  }
  Synthetic();
  Merging();
  int files = 0, texts = 0, units = 0;
  for (int i = 1; i < argc; ++i) {
    Bytes d;
    if (!ReadFile(argv[i], d)) {
      std::printf("cannot read %s\n", argv[i]);
      ++g_fail;
      continue;
    }
    ++files;
    const Kind k = Detect(d);
    if (k == Kind::kNone) continue;
    ++texts;
    std::string err;
    Bytes out;
    if (!Rebuild(d, {}, out, err) || out != d) {
      std::printf("FAIL identity %s (%s): %s size %zu -> %zu\n", argv[i], KindName(k), err.c_str(), d.size(),
                  out.size());
      ++g_fail;
      continue;
    }
    std::map<std::string, Bytes> u;
    if (!Units(d, u, err)) {
      std::printf("FAIL units %s: %s\n", argv[i], err.c_str());
      ++g_fail;
      continue;
    }
    units += int(u.size());
    if (u.empty()) continue;
    // Replace the first and the last unit with longer text.
    const std::string a = u.begin()->first, z = u.rbegin()->first;
    if (!Rebuild(d, {{a, "Mod text A"}, {z, "Longer mod text Z, 123"}}, out, err)) {
      std::printf("FAIL edit %s [%s]: %s\n", argv[i], a.c_str(), err.c_str());
      ++g_fail;
      continue;
    }
    std::map<std::string, Bytes> v;
    if (!Units(out, v, err) || v.size() != u.size()) {
      std::printf("FAIL edit units %s: %zu -> %zu\n", argv[i], u.size(), v.size());
      ++g_fail;
      continue;
    }
    for (const auto& [key, raw] : u) {
      if (key == a || key == z) continue;
      if (v[key] != raw) {
        std::printf("FAIL edit %s: unit %s changed\n", argv[i], key.c_str());
        ++g_fail;
        break;
      }
    }
    if (Detect(out) != k) {
      std::printf("FAIL edit %s: kind changed\n", argv[i]);
      ++g_fail;
    }
  }
  std::printf("%d files, %d text files, %d units, %d failures\n", files, texts, units, g_fail);
  return g_fail ? 1 : 0;
}
