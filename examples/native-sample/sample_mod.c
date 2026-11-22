/* Sample native module (include/ffxii_mod.h): a call counter.
 *
 * Reads count.txt in its mod folder: one guest function VA (hex) per line,
 * '#' starts a comment, at most 16. Hooks each and logs its 1st, 10th,
 * 100th, ... call, then runs the original. Install as
 *
 *   mods/native-sample/mod.ini     id = native-sample
 *                                  native = sample_mod.dll
 *   mods/native-sample/sample_mod.dll
 *   mods/native-sample/count.txt
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ffxii_mod.h"

#define SLOTS 16

static const ffxii_api* g_api;
static uint64_t g_va[SLOTS];
static ffxii_fn g_orig[SLOTS];
static volatile long long g_calls[SLOTS];

static void Count(int i, ffxii_cpu* cpu) {
  long long n = __atomic_add_fetch(&g_calls[i], 1, __ATOMIC_RELAXED);
  long long p = 1;
  while (p < n) p *= 10;
  if (p == n) {
    char msg[128];
    uint64_t ret;
    memcpy(&ret, g_api->guest_ptr(g_api->reg(cpu, FFXII_RSP)), 8);
    snprintf(msg, sizeof msg, "%08llX call %lld (rcx %llX, returns to %08llX)",
             (unsigned long long)g_va[i], n, (unsigned long long)g_api->reg(cpu, FFXII_RCX),
             (unsigned long long)ret);
    g_api->log(g_api, msg);
  }
  g_orig[i](cpu);
}

#define STUB(i) static void Stub##i(ffxii_cpu* cpu) { Count(i, cpu); }
STUB(0) STUB(1) STUB(2) STUB(3) STUB(4) STUB(5) STUB(6) STUB(7)
STUB(8) STUB(9) STUB(10) STUB(11) STUB(12) STUB(13) STUB(14) STUB(15)
static const ffxii_fn kStubs[SLOTS] = {Stub0, Stub1, Stub2,  Stub3,  Stub4,  Stub5,  Stub6,  Stub7,
                                       Stub8, Stub9, Stub10, Stub11, Stub12, Stub13, Stub14, Stub15};

FFXII_MOD_EXPORT int ffxii_mod_init(const ffxii_api* api) {
  if (api->abi < 1 || api->size < sizeof(ffxii_api)) return 1;
  g_api = api;
  char path[1024], line[256], msg[160];
  snprintf(path, sizeof path, "%s/count.txt", api->mod_dir);
  FILE* f = fopen(path, "r");
  if (!f) {
    api->log(api, "no count.txt; nothing to hook");
    return 0;
  }
  int n = 0;
  while (n < SLOTS && fgets(line, sizeof line, f)) {
    char* hash = strchr(line, '#');
    if (hash) *hash = 0;
    char* end;
    uint64_t va = strtoull(line, &end, 16);
    if (end == line) continue;
    g_va[n] = va;
    if (api->hook(va, kStubs[n], &g_orig[n]) != 0) {
      snprintf(msg, sizeof msg, "%08llX is not a lifted function entry", (unsigned long long)va);
      api->log(api, msg);
      continue;
    }
    n++;
  }
  fclose(f);
  snprintf(msg, sizeof msg, "counting %d functions", n);
  api->log(api, msg);
  return 0;
}
