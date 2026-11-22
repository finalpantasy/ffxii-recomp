// Host test for src/mod_index: discovery, priority, conflicts, legacy files.
#include "../src/mod_index.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace ffxii::mods;

static int g_fail = 0;
#define CHECK(x)                                                    \
  do {                                                              \
    if (!(x)) {                                                     \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); \
      g_fail++;                                                     \
    }                                                               \
  } while (0)

static void Put(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << s;
}

static std::string Slurp(const std::string& p) {
  std::ifstream f(fs::u8path(p), std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), {});
}

int main() {
  CHECK(Normalize("GameData\\TitleImage//US/A.tga") == "gamedata/titleimage/us/a.tga");
  CHECK(Normalize("./a/b") == "a/b");
  CHECK(Normalize("/a") == "a");

  {
    ModInfo m;
    std::vector<std::string> errs;
    CHECK(ParseModIni("# c\nid = x\nname = X Mod\npriority = -3\nenabled = no\nfoo = 1\n", m, errs));
    CHECK(m.id == "x" && m.name == "X Mod" && m.priority == -3 && !m.enabled);
    CHECK(errs.size() == 1);  // unknown key
    ModInfo bad;
    errs.clear();
    CHECK(!ParseModIni("priority = high\nnoequals\n", bad, errs));
    CHECK(errs.size() == 2);
  }

  const fs::path root = fs::temp_directory_path() / "ffxii_mod_index_test";
  fs::remove_all(root);
  Put(root / "alpha/mod.ini", "id = alpha\npriority = 5\n");
  Put(root / "alpha/files/gamedata/a.bin", "alpha-a");
  Put(root / "alpha/files/gamedata/b.bin", "alpha-b");
  Put(root / "beta/mod.ini", "id = beta\npriority = 10\nversion = 2.1\n");
  Put(root / "beta/files/GameData/A.bin", "beta-a");
  Put(root / "gamma/mod.ini", "id = gamma\npriority = 5\n");
  Put(root / "gamma/files/gamedata/b.bin", "gamma-b");
  Put(root / "off/mod.ini", "id = off\npriority = 99\nenabled = false\n");
  Put(root / "off/files/gamedata/a.bin", "off-a");
  Put(root / "dup/mod.ini", "id = Alpha\n");
  Put(root / "legacy/c.bin", "loose-c");  // no mod.ini: legacy tree
  Put(root / "gamedata/b.bin", "loose-b");

  Index ix = Build(root.string());
  // a: beta (10) beats alpha (5); off is disabled.
  const Entry* a = ix.Find("gamedata/a.bin");
  CHECK(a && Slurp(a->host) == "beta-a");
  // b: alpha and gamma tie at 5, smaller id wins; loose (0) loses.
  const Entry* b = ix.Find("GAMEDATA\\b.bin");
  CHECK(b && Slurp(b->host) == "alpha-b");
  const Entry* c = ix.Find("legacy/c.bin");
  CHECK(c && Slurp(c->host) == "loose-c");
  CHECK(!ix.Find("gamedata/none.bin"));
  CHECK(!ix.Find("alpha/mod.ini"));
  CHECK(ix.files.size() == 3);
  CHECK(ix.conflicts.size() == 2);
  CHECK(ix.errors.size() == 1);  // duplicate id Alpha
  bool saw_b = false;
  for (const auto& cf : ix.conflicts)
    if (cf.path == "gamedata/b.bin") {
      saw_b = true;
      CHECK(cf.winner == "alpha" && cf.losers.size() == 2);
    }
  CHECK(saw_b);

  // Disabling beta by id hands a back to alpha.
  Index ix2 = Build(root.string(), {"beta"});
  const Entry* a2 = ix2.Find("gamedata/a.bin");
  CHECK(a2 && Slurp(a2->host) == "alpha-a");

  const std::string rep = ix.Report();
  CHECK(rep.find("[off] off") != std::string::npos);
  CHECK(rep.find("conflict gamedata/a.bin: beta over alpha") != std::string::npos);
  std::fputs(rep.c_str(), stdout);

  // No root folder: empty index.
  CHECK(Build((root / "missing").string()).files.empty());

  fs::remove_all(root);
  std::printf("%s\n", g_fail ? "FAILED" : "ok");
  return g_fail ? 1 : 0;
}
