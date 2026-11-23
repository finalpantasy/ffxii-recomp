// Sample WebAssembly mod: counts calls to the game's entry point and the
// PhyreEngine window procedure, reads the exe's "MZ" header from game memory,
// and on the 500th window message reads an unmapped address to show that a
// bad read traps the hook (logged) instead of crashing the game; later
// calls still run the hook.
#include "ffxii_wasm.h"

static uint32_t g_wnd;

static void log_count(const char* what, uint32_t n) {
  char buf[64];
  uint32_t i = 0;
  while (*what) buf[i++] = *what++;
  char digits[12];
  uint32_t d = 0;
  do {
    digits[d++] = (char)('0' + n % 10);
    n /= 10;
  } while (n);
  while (d) buf[i++] = digits[--d];
  ffxii_log(buf, i);
}

FFXII_EXPORT(on_entry) void on_entry(int64_t a, int64_t b, int64_t c, int64_t d) {
  (void)a, (void)b, (void)c, (void)d;
  const uint32_t mz = ffxii_read_u16(0x120000);
  ffxii_log_str(mz == 0x5A4D ? "entry: image header reads MZ" : "entry: image header is not MZ");
  ffxii_original();
}

FFXII_EXPORT(on_wndproc) void on_wndproc(int64_t hwnd, int64_t msg, int64_t wp, int64_t lp) {
  (void)hwnd, (void)msg, (void)wp, (void)lp;
  ffxii_original();
  g_wnd++;
  if (g_wnd == 1 || g_wnd % 1000 == 0) log_count("window messages: ", g_wnd);
  if (g_wnd == 500) (void)ffxii_read_u32(0x10);  // unmapped: traps this call only
}

FFXII_EXPORT(ffxii_init) int32_t ffxii_init(void) {
  if (ffxii_hook_export(0x808EE4, "on_entry") != 0) return 1;
  if (ffxii_hook_export(0x798FF0, "on_wndproc") != 0) return 2;
  ffxii_log_str("wasm sample loaded: hooks on 0x808EE4 and 0x798FF0");
  return 0;
}
