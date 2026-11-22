// Loose-file mod override for the VBF archive (analysis/formats/
// vbf-runtime-read-path.md). Mods are found and ordered by mod_index.h: the
// root is $FFXII_MODS_DIR, else <exe dir>/mods, else <game dir>/mods, and
// $FFXII_MODS_DISABLE lists mod ids to leave off (comma separated). A file a
// mod supplies replaces the archive entry of the same path: Find returns a
// synthetic reader whose reads come from that file. Seek (215220) and size (215280) need no hook: they only use
// reader+0x20 and [reader+0x18]+8, which the synthetic reader fills in.
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
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "mod_index.h"
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
constexpr uint64_t kSlab = 0x100000;  // 8192 open mod files at once

struct Open {
  std::FILE* f;
  uint64_t size;
};

std::mutex g_mu;
uint64_t g_slab = 0;
std::vector<uint64_t> g_free;
std::unordered_map<uint64_t, Open> g_open;  // reader va -> loose file
std::string g_root;
ffxii::mods::Index g_index;
bool g_root_set = false;
bool g_enabled = false;

template <class T>
T MRd(uint64_t a) {
  T v;
  std::memcpy(&v, g_guest_base + a, sizeof v);
  return v;
}
template <class T>
void MWr(uint64_t a, T v) {
  std::memcpy(g_guest_base + a, &v, sizeof v);
}

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

// Scans g_root, logs the report and writes mods-report.txt.
ffxii::mods::Index BuildIndex() {
  const char* d = recomp::Env("MODS_DISABLE");
  const std::unordered_set<std::string> off = ffxii::mods::ParseIdList(d ? d : "");
  ffxii::mods::Index index = ffxii::mods::Build(g_root, off);
  const std::string rep = index.Report();
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
    ffxii::mods::Index index = BuildIndex();
    std::lock_guard lock(g_mu);
    g_index = std::move(index);
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
  g_index = BuildIndex();
  static const bool watch = recomp::Env("MODS_WATCH") != nullptr;
  if (watch) std::thread(WatchLoop).detach();
  g_enabled = watch || !g_index.files.empty() || recomp::Env("MODS_LOG") != nullptr;
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

namespace recomp::gen {

void f_0019AEE0(Ctx& c) {
  const uint64_t mgr = c.rcx;
  std::lock_guard lock(g_mu);
  if (!Init() || !mgr || !::ffxii::guest::kVbfManager.Get()) return f_0019AEE0_lifted(c);
  const char* path = reinterpret_cast<const char*>(g_guest_base + c.rdx + MRd<uint64_t>(mgr + 0x70));
  std::string rel(path, strnlen(path, 1024));
  static const bool log_all = recomp::Env("MODS_LOG") != nullptr;
  if (log_all) std::fprintf(stderr, "[mods] open %s\n", rel.c_str());
  const ffxii::mods::Entry* hit = g_index.Find(rel);
  if (!hit) return f_0019AEE0_lifted(c);
  std::FILE* f = std::fopen(hit->host.c_str(), "rb");
  if (!f) {
    std::fprintf(stderr, "[mods] cannot open %s; using the archive\n", hit->host.c_str());
    return f_0019AEE0_lifted(c);
  }
  const uint64_t size = FileSize(f);
  const uint64_t r = NewSlot();
  if (!r) {
    std::fclose(f);
    std::fprintf(stderr, "[mods] out of reader slots; using the archive for %s\n", path);
    return f_0019AEE0_lifted(c);
  }
  const uint64_t entry = r + 0x30, cache = r + 0x50;
  MWr<uint8_t>(r + 0x00, 1);  // in use
  MWr<uint64_t>(r + 0x18, entry);
  MWr<uint64_t>(r + 0x20, 0);
  MWr<uint64_t>(r + 0x28, cache);
  MWr<uint64_t>(entry + 0x08, size);
  g_open[r] = {f, size};
  std::fprintf(stderr, "[mods] %s (%llu bytes)\n", path, static_cast<unsigned long long>(size));
  Return(c, r);
}

void f_00215110(Ctx& c) {
  std::unique_lock lock(g_mu);
  auto it = g_open.find(c.rcx);
  if (it == g_open.end()) {
    lock.unlock();
    return f_00215110_lifted(c);
  }
  const uint64_t pos = MRd<uint64_t>(c.rcx + 0x20);
  const uint64_t want = uint32_t(c.r8);
  const uint64_t left = pos < it->second.size ? it->second.size - pos : 0;
  const uint64_t n = want < left ? want : left;
  size_t got = 0;
  if (n && Seek(it->second.f, pos) == 0)
    got = std::fread(g_guest_base + c.rdx, 1, n, it->second.f);
  Return(c, uint32_t(got));
}

void f_002150D0(Ctx& c) {
  std::unique_lock lock(g_mu);
  auto it = g_open.find(c.rcx);
  if (it == g_open.end()) {
    lock.unlock();
    return f_002150D0_lifted(c);
  }
  std::fclose(it->second.f);
  g_free.push_back(it->first);
  g_open.erase(it);
  Return(c, 0);
}

}  // namespace recomp::gen
