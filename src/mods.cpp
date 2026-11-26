// Loose-file mod override for the VBF archive (analysis/formats/
// vbf-runtime-read-path.md). Mods are found and ordered by mod_index.h: the
// root is $FFXII_MODS_DIR, else <exe dir>/mods, else <game dir>/mods, and
// $FFXII_MODS_DISABLE lists mod ids to leave off (comma separated). A file a
// mod supplies replaces the archive entry of the same path: Find returns a
// synthetic reader whose reads come from that file. Seek (215220) and size (215280) need no hook: they only use
// reader+0x20 and [reader+0x18]+8, which the synthetic reader fills in.
//
// Text files that several mods change (whole copies or text/ patches) are
// merged per string against the archive original when the index is built
// (text_mods.h), and Find serves the merged bytes from memory.
//
// The manifest replaces these three (`replace`), so the lifted bodies are
// f_X_lifted:
//   0019AEE0 Find(mgr, path) -> reader or 0; the path is rdx + [mgr+0x70]
//   00215110 Read(reader, dst, len) -> bytes read; does not move the position
//   002150D0 Release(reader): hands the reader back to its archive

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "fps_menu.h"
#include "mod_index.h"
#include "text_mods.h"
#include "guest_globals.h"
#include "runtime/env.h"
#include "runtime/platform.h"
#include "x86/lifted.h"
#include "x86/runtime.h"

namespace recomp::gen {
void f_0019AEE0_lifted(::recomp::Ctx& c);
void f_00215110_lifted(::recomp::Ctx& c);
void f_002150D0_lifted(::recomp::Ctx& c);
}  // namespace recomp::gen

namespace {

using recomp::Ctx;
using recomp::g_guest_base;

// One synthetic reader: reader (0x30), entry (0x20), block cache (0x18).
constexpr uint64_t kSlot = 0x80;
// Reader, entry and a 3-pointer block cache.
static_assert(sizeof(::ffxii::guest::VbfReader) + sizeof(::ffxii::guest::VbfEntry) + 3 * 8 <= kSlot);
constexpr uint64_t kSlab = 0x100000;  // 8192 open mod files at once

struct Open {
  std::FILE* f;                                    // a mod's file, or
  std::shared_ptr<const ffxii::text::Bytes> mem;   // merged text
  uint64_t size;
  bool fps_label = false;  // menu_command.bin with the widened frame-rate label
};

std::mutex g_mu;
uint64_t g_slab = 0;
std::vector<uint64_t> g_free;
std::unordered_map<uint64_t, Open> g_open;  // reader va -> loose file
std::string g_root;
ffxii::mods::Index g_index;
ffxii::text::ModTextMerges g_text;
ffxii::vbf::Archive g_vbf;  // originals for text merging; opened when needed
std::mutex g_vbf_mu;
bool g_root_set = false;
bool g_enabled = false;


void Return(Ctx& c, uint64_t v) {
  c.rax = v;
  c.rsp += 8;  // ret
}

int Seek(std::FILE* f, uint64_t pos) {
#if defined(_WIN32)
  return _fseeki64(f, int64_t(pos), SEEK_SET);
#else
  return fseeko(f, off_t(pos), SEEK_SET);
#endif
}

uint64_t FileSize(std::FILE* f) {
#if defined(_WIN32)
  _fseeki64(f, 0, SEEK_END);
  return uint64_t(_ftelli64(f));
#else
  fseeko(f, 0, SEEK_END);
  return uint64_t(ftello(f));
#endif
}

// Scans g_root, merges mod text, logs the report and writes mods-report.txt.
ffxii::mods::Index BuildIndex(ffxii::text::ModTextMerges& text) {
  const char* d = recomp::Env("MODS_DISABLE");
  const std::unordered_set<std::string> off = ffxii::mods::ParseIdList(d ? d : "");
  ffxii::mods::Index index = ffxii::mods::Build(g_root, off);
  text = {};
  if (!index.copies.empty() || !index.patches.empty()) {
    std::lock_guard lock(g_vbf_mu);
    static bool tried = false;
    if (!tried) {
      tried = true;
      std::string err;
      if (!g_vbf.Open(recomp::g_proc->game_dir + "/FFXII_TZA.vbf", err))
        std::fprintf(stderr, "[mods] %s; text merging off\n", err.c_str());
    }
    text = ffxii::text::MergeModText(index, g_vbf);
  }
  const std::string rep = index.Report() + text.report;
  std::fprintf(stderr, "[mods] %s\n%s", g_root.c_str(), rep.c_str());
  if (std::FILE* f = std::fopen((g_root + "/mods-report.txt").c_str(), "wb")) {
    std::fputs(rep.c_str(), f);
    std::fclose(f);
  }
  return index;
}

// FFXII_MODS_WATCH (--mods-watch): checks the mods folder once a second and
// rebuilds the index after a change, so a file the game opens from then on
// comes from the new set. Files already open keep reading what they opened.
void WatchLoop() {
  uint64_t seen = ffxii::mods::TreeSignature(g_root);
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    uint64_t now = ffxii::mods::TreeSignature(g_root);
    if (now == seen) continue;
    // Wait until a copy in progress settles.
    for (int i = 0; i < 10; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      const uint64_t again = ffxii::mods::TreeSignature(g_root);
      if (again == now) break;
      now = again;
    }
    seen = now;
    std::fprintf(stderr, "[mods] change in %s; reloading\n", g_root.c_str());
    ffxii::text::ModTextMerges text;
    ffxii::mods::Index index = BuildIndex(text);
    std::lock_guard lock(g_mu);
    g_index = std::move(index);
    g_text = std::move(text);
  }
}

// Called with g_mu held.
bool Init() {
  if (g_root_set) return g_enabled;
  g_root_set = true;
  namespace fs = std::filesystem;
  std::error_code ec;
  const char* env = recomp::Env("MODS_DIR");
  g_root = ffxii::mods::ResolveRoot(env ? env : "", recomp::platform::ExeDir(),
                                    recomp::g_proc->game_dir);
  if (!fs::is_directory(g_root, ec)) {
    std::fprintf(stderr, "[mods] no folder %s\n", g_root.c_str());
    return g_enabled = false;
  }
  g_index = BuildIndex(g_text);
  static const bool watch = recomp::Env("MODS_WATCH") != nullptr;
  if (watch) std::thread(WatchLoop).detach();
  g_enabled = watch || !g_index.files.empty() || !g_text.files.empty() || recomp::Env("MODS_LOG") != nullptr;
  return g_enabled;
}

uint64_t NewSlot() {
  if (!g_slab) {
    g_slab = recomp::g_proc->vm.Reserve(0, kSlab);
    if (!g_slab || !recomp::g_proc->vm.Commit(g_slab, kSlab, recomp::Prot::RW)) {
      g_slab = 0;
      return 0;
    }
    for (uint64_t s = g_slab + kSlab; s > g_slab;) g_free.push_back(s -= kSlot);
  }
  if (g_free.empty()) return 0;
  uint64_t s = g_free.back();
  g_free.pop_back();
  std::memset(g_guest_base + s, 0, kSlot);
  return s;
}

}  // namespace

namespace {
// menu_command.bin with room for the 120/240 FPS labels (fps_menu.h), built
// once per path from the mod-merged copy or the archive original. Null when
// the file cannot be read or does not match the retail layout. g_mu held.
std::shared_ptr<const ffxii::text::Bytes> FpsMenuFile(const std::string& norm) {
  static std::unordered_map<std::string, std::shared_ptr<const ffxii::text::Bytes>> built;
  const auto b = built.find(norm);
  if (b != built.end()) return b->second;
  std::shared_ptr<const ffxii::text::Bytes> src;
  const auto m = g_text.files.find(norm);
  if (m != g_text.files.end()) {
    src = m->second;
  } else if (!g_index.Find(norm)) {  // a single mod's whole copy keeps its own text
    std::lock_guard lock(g_vbf_mu);
    std::string err;
    if (!g_vbf.IsOpen() && !g_vbf.Open(recomp::g_proc->game_dir + "/FFXII_TZA.vbf", err))
      std::fprintf(stderr, "[fps_menu] %s\n", err.c_str());
    auto orig = std::make_shared<ffxii::text::Bytes>();
    if (g_vbf.IsOpen() && g_vbf.Read(norm, *orig, err)) src = orig;
  }
  auto out = src ? ffxii::fps_menu::Patch(*src) : nullptr;
  if (!out) std::fprintf(stderr, "[fps_menu] %s left as is\n", norm.c_str());
  return built[norm] = out;
}
}  // namespace

namespace recomp::gen {

void f_0019AEE0(Ctx& c) {
  const uint64_t mgr = c.rcx;
  std::lock_guard lock(g_mu);
  if (!mgr || !::ffxii::guest::kVbfManager.Get()) return f_0019AEE0_lifted(c);
  const char* path = reinterpret_cast<const char*>(g_guest_base + c.rdx + ::ffxii::guest::At<::ffxii::guest::VbfManager>(mgr)->path_base);
  std::string rel(path, strnlen(path, 1024));
  const std::string norm = ffxii::mods::Normalize(rel);
  const bool enabled = Init();
  std::shared_ptr<const ffxii::text::Bytes> fps_file =
      ffxii::fps_menu::IsMenuCommand(norm) ? FpsMenuFile(norm) : nullptr;
  if (!enabled && !fps_file) return f_0019AEE0_lifted(c);
  static const bool log_all = recomp::Env("MODS_LOG") != nullptr;
  if (log_all) std::fprintf(stderr, "[mods] open %s\n", rel.c_str());
  std::shared_ptr<const ffxii::text::Bytes> mem;
  std::FILE* f = nullptr;
  uint64_t size = 0;
  if (fps_file) {
    mem = fps_file, size = mem->size();
  } else if (!g_text.files.empty()) {
    const auto m = g_text.files.find(norm);
    if (m != g_text.files.end()) mem = m->second, size = mem->size();
  }
  if (!mem) {
    const ffxii::mods::Entry* hit = g_index.Find(rel);
    if (!hit) return f_0019AEE0_lifted(c);
    f = std::fopen(hit->host.c_str(), "rb");
    if (!f) {
      std::fprintf(stderr, "[mods] cannot open %s; using the archive\n", hit->host.c_str());
      return f_0019AEE0_lifted(c);
    }
    size = FileSize(f);
  }
  const uint64_t r = NewSlot();
  if (!r) {
    if (f) std::fclose(f);
    std::fprintf(stderr, "[mods] out of reader slots; using the archive for %s\n", path);
    return f_0019AEE0_lifted(c);
  }
  // A slot holds the reader, then its entry, then an empty block cache.
  const uint64_t entry = r + sizeof(::ffxii::guest::VbfReader), cache = entry + sizeof(::ffxii::guest::VbfEntry);
  auto* reader = ::ffxii::guest::At<::ffxii::guest::VbfReader>(r);
  reader->in_use = 1;
  reader->entry = entry;
  reader->position = 0;
  reader->cache = cache;
  ::ffxii::guest::At<::ffxii::guest::VbfEntry>(entry)->size = size;
  g_open[r] = {f, std::move(mem), size, fps_file != nullptr};
  std::fprintf(stderr, "[mods] %s (%llu bytes%s)\n", path, static_cast<unsigned long long>(size),
               f ? "" : ", merged text");
  Return(c, r);
}

void f_00215110(Ctx& c) {
  std::unique_lock lock(g_mu);
  auto it = g_open.find(c.rcx);
  if (it == g_open.end()) {
    lock.unlock();
    return f_00215110_lifted(c);
  }
  const uint64_t pos = ::ffxii::guest::At<::ffxii::guest::VbfReader>(c.rcx)->position;
  const uint64_t want = uint32_t(c.r8);
  const uint64_t left = pos < it->second.size ? it->second.size - pos : 0;
  const uint64_t n = want < left ? want : left;
  size_t got = 0;
  if (n && it->second.mem) {
    std::memcpy(g_guest_base + c.rdx, it->second.mem->data() + pos, n);
    got = n;
    if (it->second.fps_label) ffxii::fps_menu::OnRead(c.rdx, pos, n);
  } else if (n && Seek(it->second.f, pos) == 0) {
    got = std::fread(g_guest_base + c.rdx, 1, n, it->second.f);
  }
  Return(c, uint32_t(got));
}

void f_002150D0(Ctx& c) {
  std::unique_lock lock(g_mu);
  auto it = g_open.find(c.rcx);
  if (it == g_open.end()) {
    lock.unlock();
    return f_002150D0_lifted(c);
  }
  if (it->second.f) std::fclose(it->second.f);
  g_free.push_back(it->first);
  g_open.erase(it);
  Return(c, 0);
}

}  // namespace recomp::gen
