// Constant-buffer scan (FFXII_CBSCAN=1, debug only).
// Installs SwARc's D3D11 buffer-upload hook and looks in every upload for the
// camera of the last few frames: its world position (camera transform at
// 0x20975A0, row 3) and the first row or first column of the view matrix at
// 0x2097560 (HLSL constant buffers are often column-major). Every 5 s it logs
// the uploads per buffer size, and each (bind flags, size, offset, kind)
// where a match was found, with counts. This shows whether the view reaches
// the GPU as one per-frame constant block or inside per-object data.

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <tuple>

#include "runtime/env.h"
#include "x86/lifted.h"

namespace recomp::hle {
using BufferUploadHook = void (*)(const void* data, size_t size, uint32_t bind_flags,
                                  uint32_t byte_width);
extern BufferUploadHook g_buffer_upload_hook;
}  // namespace recomp::hle

namespace recomp::ffxii {
namespace {

struct Cam {
  float pos[3];
  float row[3];
  float col[3];
};
constexpr int kRing = 4;
std::array<Cam, kRing> g_cams{};
int g_cam_next = 0;

std::mutex g_mu;
std::map<std::tuple<uint32_t, uint32_t, size_t, int>, uint64_t> g_hits;
std::map<uint32_t, uint64_t> g_uploads;  // byte width -> count
uint64_t g_frames = 0;

bool Near3(const float* a, const float* b) {
  for (int i = 0; i < 3; ++i) {
    const float d = std::fabs(a[i] - b[i]);
    if (!(d <= 1e-3f + 1e-4f * std::fabs(b[i]))) return false;
  }
  return true;
}

// FFXII_CBSCAN_DUMP=<frame>: from that frame on, append the first 64 uploads
// each of 928 and 8864 bytes to cbdump.bin as records of
// [u32 size][64 floats: view 0x2097560, then camera 0x20975A0, 0x2AED580 copy
// and 0x2AED5C0][data].
uint64_t g_dump_from = ~0ull;
int g_dumped[2] = {0, 0};
FILE* g_dump = nullptr;

void MaybeDump(const void* data, size_t size) {
  const int k = size == 928 ? 0 : size == 8864 ? 1 : -1;
  if (k < 0 || g_frames < g_dump_from || g_dumped[k] >= 64) return;
  if (!g_dump) g_dump = std::fopen("cbdump.bin", "wb");
  if (!g_dump) return;
  ++g_dumped[k];
  const uint32_t sz = uint32_t(size);
  float cam[64];
  std::memcpy(cam, recomp::g_guest_base + 0x2097560, 128);
  std::memcpy(cam + 32, recomp::g_guest_base + 0x2AED580, 128);
  std::fwrite(&sz, 4, 1, g_dump);
  std::fwrite(cam, 4, 64, g_dump);
  std::fwrite(data, 1, size, g_dump);
  std::fflush(g_dump);
}

void OnUpload(const void* data, size_t size, uint32_t bind, uint32_t width) {
  if (bind & 0x4) MaybeDump(data, size);
  Cam cams[kRing];
  std::memcpy(cams, g_cams.data(), sizeof cams);
  const auto* f = static_cast<const float*>(data);
  const size_t n = size / 4;
  std::lock_guard<std::mutex> lock(g_mu);
  ++g_uploads[width];
  for (size_t i = 0; i + 3 <= n; ++i) {
    for (const Cam& c : cams) {
      if (c.pos[0] == 0 && c.pos[1] == 0 && c.pos[2] == 0) continue;
      int kind = -1;
      if (Near3(f + i, c.pos)) kind = 0;
      else if (Near3(f + i, c.row)) kind = 1;
      else if (Near3(f + i, c.col)) kind = 2;
      if (kind < 0) continue;
      ++g_hits[{bind, width, i * 4, kind}];
      break;
    }
  }
}

}  // namespace

// Called every limited frame from the frame limiter wrapper (game thread).
void CbScanFrame() {
  static const bool on = recomp::Env("CBSCAN") != nullptr;
  if (!on) return;
  if (!recomp::hle::g_buffer_upload_hook) {
    recomp::hle::g_buffer_upload_hook = OnUpload;
    if (const char* d = recomp::Env("CBSCAN_DUMP")) g_dump_from = std::strtoull(d, nullptr, 10);
  }
  const uint8_t* B = recomp::g_guest_base;
  float world[16], view[16];
  std::memcpy(world, B + 0x20975A0, sizeof world);
  std::memcpy(view, B + 0x2097560, sizeof view);
  Cam& c = g_cams[g_cam_next];
  g_cam_next = (g_cam_next + 1) % kRing;
  std::memcpy(c.pos, world + 12, 12);
  std::memcpy(c.row, view, 12);
  c.col[0] = view[0], c.col[1] = view[4], c.col[2] = view[8];
  if (++g_frames % 300 != 0) return;
  static const char* kKind[] = {"pos", "view-row0", "view-col0"};
  std::lock_guard<std::mutex> lock(g_mu);
  std::fprintf(stderr, "[cbscan] %llu frames; uploads by size:", (unsigned long long)g_frames);
  for (auto& [w, k] : g_uploads) std::fprintf(stderr, " %u:%llu", w, (unsigned long long)k);
  std::fprintf(stderr, "\n");
  for (auto& [key, k] : g_hits) {
    auto [bind, width, off, kind] = key;
    std::fprintf(stderr, "[cbscan]   bind 0x%x size %u off 0x%zx %s x%llu\n", bind, width, off,
                 kKind[kind], (unsigned long long)k);
  }
  g_hits.clear();
  g_uploads.clear();
}

}  // namespace recomp::ffxii
