// ffxii_modlab: the mod lab without the game (same flags as
// `ffxii_recomp --mod-lab`; with no flags it opens the page).

#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "mod_lab.h"

int main(int argc, char** argv) {
  std::string exe_dir;
#if defined(_WIN32)
  wchar_t buf[MAX_PATH * 4];
  const DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
  const auto u = std::filesystem::path(std::wstring(buf, n)).parent_path().u8string();
  exe_dir.assign(reinterpret_cast<const char*>(u.data()), u.size());
#else
  exe_dir = std::filesystem::absolute(argv[0]).parent_path().string();
#endif
  int rc = ffxii::modlab::Main(argc, argv, exe_dir);
  if (rc != -1) return rc;
  std::vector<char*> args(argv, argv + argc);
  static char lab[] = "--mod-lab";
  args.push_back(lab);
  return ffxii::modlab::Main(int(args.size()), args.data(), exe_dir);
}
