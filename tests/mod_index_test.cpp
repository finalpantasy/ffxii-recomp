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

  // Versions compare per dotted part, numerically.
  CHECK(CompareVersions("1.10", "1.9") > 0);
  CHECK(CompareVersions("2", "2.0.0") == 0);
  CHECK(CompareVersions("1.2", "1.2.1") < 0);
  CHECK(CompareVersions("1.0b", "1.0a") > 0);

  {
    ModInfo m;
    std::vector<std::string> errs;
    CHECK(ParseModIni("requires = base, lib >= 1.2\nconflicts = old, other\n", m, errs));
    CHECK(errs.empty() && m.required.size() == 2 && m.conflicts.size() == 2);
    CHECK(m.required[1].id == "lib" && m.required[1].min_version == "1.2");
    errs.clear();
    CHECK(!ParseModIni("requires = >=1\n", m, errs));
  }

  // Declared relations: requirements cascade, versions are checked, and the
  // lower-ranked side of a declared conflict is turned off.
  const fs::path rel = fs::temp_directory_path() / "ffxii_mod_index_rel";
  fs::remove_all(rel);
  Put(rel / "base/mod.ini", "id = base\nversion = 1.10\n");
  Put(rel / "base/files/x.bin", "base-x");
  Put(rel / "addon/mod.ini", "id = addon\nrequires = base >= 1.9\n");
  Put(rel / "addon/files/y.bin", "addon-y");
  Put(rel / "needsnew/mod.ini", "id = needsnew\nrequires = base >= 2\n");
  Put(rel / "needsnew/files/z.bin", "needsnew-z");
  Put(rel / "chain/mod.ini", "id = chain\nrequires = needsnew\n");
  Put(rel / "chain/files/w.bin", "chain-w");
  Put(rel / "ghost/mod.ini", "id = ghost\nrequires = absent\n");
  Put(rel / "hi/mod.ini", "id = hi\npriority = 3\n");
  Put(rel / "hi/files/x.bin", "hi-x");
  Put(rel / "lo/mod.ini", "id = lo\nconflicts = hi\n");
  Put(rel / "lo/files/v.bin", "lo-v");
  Index rx = Build(rel.string());
  auto mod = [&](const std::string& id) -> const ModInfo* {
    for (const auto& m : rx.mods)
      if (m.id == id) return &m;
    return nullptr;
  };
  CHECK(mod("addon") && mod("addon")->enabled);
  CHECK(rx.Find("y.bin"));
  CHECK(mod("needsnew") && !mod("needsnew")->enabled);
  CHECK(mod("needsnew")->off_reason.find(">= 2") != std::string::npos);
  CHECK(mod("chain") && !mod("chain")->enabled);  // cascades from needsnew
  CHECK(!rx.Find("z.bin") && !rx.Find("w.bin"));
  CHECK(mod("ghost") && !mod("ghost")->enabled);
  CHECK(mod("lo") && !mod("lo")->enabled && mod("hi")->enabled);
  CHECK(!rx.Find("v.bin"));
  const Entry* x = rx.Find("x.bin");
  CHECK(x && Slurp(x->host) == "hi-x");
  CHECK(rx.errors.size() == 4);
  // Turning hi off by flag lets lo run.
  Index rx2 = Build(rel.string(), {"hi"});
  CHECK(rx2.Find("v.bin"));
  const std::string rrep = rx.Report();
  CHECK(rrep.find("(conflicts with hi)") != std::string::npos);
  std::fputs(rrep.c_str(), stdout);
  fs::remove_all(rel);

  // No root folder: empty index.
  CHECK(Build((root / "missing").string()).files.empty());

  // TreeSignature: changes on add, rewrite and remove; ignores the report.
  {
    const fs::path w = root / "watch";
    fs::create_directories(w);
    const uint64_t s0 = TreeSignature(w.string());
    CHECK(s0 != 0);
    Put(w / "m" / "files" / "a.bin", "1");
    const uint64_t s1 = TreeSignature(w.string());
    CHECK(s1 != s0);
    CHECK(TreeSignature(w.string()) == s1);
    Put(w / "mods-report.txt", "report");
    CHECK(TreeSignature(w.string()) == s1);
    Put(w / "m" / "files" / "a.bin", "22");
    const uint64_t s2 = TreeSignature(w.string());
    CHECK(s2 != s1);
    fs::remove(w / "m" / "files" / "a.bin");
    CHECK(TreeSignature(w.string()) != s2);
    CHECK(TreeSignature((root / "missing").string()) == 0);
  }

  {
    ModInfo m;
    std::vector<std::string> errs;
    CHECK(ParseModIni("native = a.dll, bin/b.dll\n", m, errs));
    CHECK(m.native.size() == 2 && m.native[1] == "bin/b.dll" && errs.empty());
    ModInfo bad;
    CHECK(!ParseModIni("native = ../x.dll\n", bad, errs));
    CHECK(!ParseModIni("native = C:/x.dll\n", bad, errs));
    CHECK(bad.native.empty() && errs.size() == 2);
  }

  {
    const auto ids = ParseIdList("a,,b,a");
    CHECK(ids.size() == 2 && ids.count("a") && ids.count("b"));
    CHECK(ParseIdList("").empty());
    const fs::path exe = root / "exe", game = root / "game";
    fs::create_directories(exe);
    CHECK(ResolveRoot("X:/m", exe.string(), game.string()) == "X:/m");
    CHECK(ResolveRoot("", exe.string(), game.string()) == game.string() + "/mods");
    fs::create_directories(exe / "mods");
    CHECK(ResolveRoot("", exe.string(), game.string()) == exe.string() + "/mods");
  }

  {
    // Declared native modules must exist in the mod folder.
    const fs::path nr = fs::temp_directory_path() / "ffxii_mod_index_native";
    fs::remove_all(nr);
    Put(nr / "nat" / "mod.ini", "id = nat\nnative = here.dll, bin/gone.dll\n");
    Put(nr / "nat" / "here.dll", "x");
    Index nx = Build(nr.string());
    CHECK(nx.errors.size() == 1 && nx.errors[0].find("bin/gone.dll") != std::string::npos);
    CHECK(nx.mods.size() >= 1 && nx.mods[0].enabled);
    fs::remove_all(nr);
  }

  {
    // Text patches (patch/<path>.txt) and every whole copy of a clashing path
    // are kept, ranked, for text merging.
    const fs::path tr = fs::temp_directory_path() / "ffxii_mod_index_text";
    fs::remove_all(tr);
    Put(tr / "lo" / "mod.ini", "id = lo\npriority = 1\n");
    Put(tr / "lo" / "patch" / "Dir" / "A.bin.txt", "s 0 = x\n");
    Put(tr / "lo" / "files" / "dir" / "b.bin", "1");
    Put(tr / "hi" / "mod.ini", "id = hi\npriority = 5\n");
    Put(tr / "hi" / "patch" / "dir" / "a.bin.txt", "s 0 = y\n");
    Put(tr / "hi" / "patch" / "dir" / "notes.md", "?");
    Put(tr / "hi" / "files" / "dir" / "b.bin", "2");
    Put(tr / "off" / "mod.ini", "id = off\nenabled = false\n");
    Put(tr / "off" / "patch" / "dir" / "a.bin.txt", "s 0 = z\n");
    Index tx = Build(tr.string());
    CHECK(tx.patches.size() == 1 && tx.patches.count("dir/a.bin"));
    if (tx.patches.count("dir/a.bin")) {
      const auto& p = tx.patches.at("dir/a.bin");
      CHECK(p.size() == 2 && tx.mods[p[0].mod].id == "hi" && tx.mods[p[1].mod].id == "lo");
    }
    CHECK(tx.copies.size() == 1 && tx.copies.count("dir/b.bin") && tx.copies.at("dir/b.bin").size() == 2);
    CHECK(tx.MayMerge("dir/a.bin") && tx.MayMerge("dir/b.bin"));
    CHECK(tx.errors.size() == 1 && tx.errors[0].find("notes.md") != std::string::npos);
    CHECK(tx.Report().find("patch dir/a.bin: hi lo") != std::string::npos);
    fs::remove_all(tr);
  }

  fs::remove_all(root);
  std::printf("%s\n", g_fail ? "FAILED" : "ok");
  return g_fail ? 1 : 0;
}
