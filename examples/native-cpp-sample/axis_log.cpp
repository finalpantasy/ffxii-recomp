// Sample native module in C++ (include/ffxii_mod.hpp): logs the first 20
// calls of the camera axis filter 0x32D3D0 that change the axis object's
// eased value, and a call count every 6,000 calls (analysis/frame-timing.md, src/guest_types.h
// AxisInput). 0x32D3D0 is only reached by direct calls from 0x32C910, so
// this also shows that entry hooks catch direct calls.

#include <atomic>

#include "ffxii_mod.hpp"

using namespace ffxii::mod;

namespace {

constexpr uint64_t kAxisFilter = 0x32D3D0;
constexpr uint64_t kFilteredX = 0xF8;  // AxisInput::filtered_x
std::atomic<uint64_t> g_calls{0};
std::atomic<int> g_changes{0};

void OnAxisFilter(Cpu cpu, Next next) {
  const uint64_t axis = cpu.arg(0);
  const uint64_t from = cpu.return_address();
  const float before = Guest<float>(axis + kFilteredX);
  next(cpu);
  const uint64_t n = ++g_calls;
  const float after = Guest<float>(axis + kFilteredX);
  if (after != before && g_changes.fetch_add(1) < 20)
    Log("0x32D3D0 call %llu from %08llX: filtered_x %.4f -> %.4f", static_cast<unsigned long long>(n),
        static_cast<unsigned long long>(from), before, after);
  if (n % 6000 == 0) Log("0x32D3D0: %llu calls", static_cast<unsigned long long>(n));
}

}  // namespace

FFXII_MOD_INIT() {
  Log("axis filter hook, covers direct calls: %s", CoversDirect(kAxisFilter) ? "yes" : "no");
  return Hook<kAxisFilter, OnAxisFilter>();
}
