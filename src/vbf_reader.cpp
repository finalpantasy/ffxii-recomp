// FFXII_TZA.vbf host reader (vbf_reader.h).

#include "vbf_reader.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>

#include "mod_index.h"

namespace ffxii::vbf {
namespace {

constexpr uint64_t kBlock = 0x10000;

int Seek(std::FILE* f, uint64_t pos) {
#if defined(_WIN32)
  return _fseeki64(f, int64_t(pos), SEEK_SET);
#else
  return fseeko(f, off_t(pos), SEEK_SET);
#endif
}

bool ReadAll(std::FILE* f, void* dst, size_t n) { return std::fread(dst, 1, n, f) == n; }

template <typename T>
T Get(const uint8_t* p) {
  T v;
  std::memcpy(&v, p, sizeof v);
  return v;
}

}  // namespace

Archive::~Archive() {
  if (f_) std::fclose(f_);
}

bool Archive::Open(const std::string& host_path, std::string& err) {
  f_ = std::fopen(host_path.c_str(), "rb");
  if (!f_) return err = "cannot open " + host_path, false;
  uint8_t head[16];
  if (!ReadAll(f_, head, 16) || std::memcmp(head, "SRYK", 4) != 0) return err = "not a VBF: " + host_path, false;
  const uint64_t n = Get<uint64_t>(head + 8);
  if (n == 0 || n > 10'000'000) return err = "bad VBF file count", false;
  std::vector<uint8_t> recs(32 * n);
  if (Seek(f_, 16 + 16 * n) != 0 || !ReadAll(f_, recs.data(), recs.size())) return err = "truncated VBF header", false;
  uint32_t nt = 0;
  if (!ReadAll(f_, &nt, 4) || nt < 4) return err = "bad VBF name table", false;
  std::vector<char> names(nt - 4);
  if (!ReadAll(f_, names.data(), names.size())) return err = "truncated VBF name table", false;
  const uint64_t table = 16 + 48 * n + nt;
  uint64_t first = UINT64_MAX;
  recs_.resize(n);
  for (uint64_t i = 0; i < n; ++i) {
    const uint8_t* r = recs.data() + 32 * i;
    recs_[i] = {Get<uint32_t>(r), Get<uint64_t>(r + 8), Get<uint64_t>(r + 16)};
    if (recs_[i].size) first = std::min(first, recs_[i].offset);
    const uint64_t no = Get<uint64_t>(r + 24);
    if (no >= names.size()) return err = "bad VBF name offset", false;
    const char* s = names.data() + no;
    by_path_[mods::Normalize(std::string(s, strnlen(s, names.size() - no)))] = i;
  }
  if (first == UINT64_MAX || first < table) return err = "bad VBF block table", false;
  blocks_.resize((first - table) / 2);
  if (Seek(f_, table) != 0 || !ReadAll(f_, blocks_.data(), 2 * blocks_.size()))
    return err = "truncated VBF block table", false;
  return true;
}

bool Archive::Read(const std::string& path, std::vector<uint8_t>& out, std::string& err) {
  const auto it = by_path_.find(path);
  if (it == by_path_.end()) return err = "not in the archive: " + path, false;
  const Rec& r = recs_[it->second];
  out.assign(r.size, 0);
  if (!r.size) return true;
  const uint64_t nblocks = (r.size + kBlock - 1) / kBlock;
  if (r.block + nblocks > blocks_.size()) return err = "bad block index for " + path, false;
  std::lock_guard lock(mu_);
  if (Seek(f_, r.offset) != 0) return err = "seek failed for " + path, false;
  std::vector<uint8_t> stored;
  for (uint64_t i = 0; i < nblocks; ++i) {
    const uint64_t want = std::min(kBlock, r.size - i * kBlock);
    const uint16_t v = blocks_[r.block + i];
    const uint64_t n = v == 0 ? kBlock : v;
    stored.resize(n);
    if (!ReadAll(f_, stored.data(), n)) return err = "short read in " + path, false;
    uint8_t* dst = out.data() + i * kBlock;
    if (n == want) {
      std::memcpy(dst, stored.data(), n);
      continue;
    }
    uLongf got = uLongf(want);
    if (uncompress(dst, &got, stored.data(), uLong(n)) != Z_OK || got != want)
      return err = "bad zlib block " + std::to_string(i) + " in " + path, false;
  }
  return true;
}

}  // namespace ffxii::vbf
