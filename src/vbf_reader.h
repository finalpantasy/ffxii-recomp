#pragma once
// Read-only host reader for FFXII_TZA.vbf (layout: analysis/formats/vbf.md;
// tools/vbf.py is the reference). Used to fetch an archive file's original
// bytes, for example to merge mod text against vanilla. Never writes.

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ffxii::vbf {

class Archive {
 public:
  Archive() = default;
  Archive(const Archive&) = delete;
  Archive& operator=(const Archive&) = delete;
  ~Archive();

  // Parses the header (about 12 MB for the retail archive).
  bool Open(const std::string& host_path, std::string& err);
  bool IsOpen() const { return f_ != nullptr; }

  // `path` as mods::Normalize gives it (lower case, '/' separators).
  bool Has(const std::string& path) const { return by_path_.count(path) != 0; }
  // Whole file, decompressed. Thread safe.
  bool Read(const std::string& path, std::vector<uint8_t>& out, std::string& err);
  size_t Count() const { return recs_.size(); }

 private:
  struct Rec {
    uint32_t block;
    uint64_t size;
    uint64_t offset;
  };
  std::FILE* f_ = nullptr;
  std::vector<Rec> recs_;
  std::vector<uint16_t> blocks_;
  std::unordered_map<std::string, size_t> by_path_;
  std::mutex mu_;
};

}  // namespace ffxii::vbf
