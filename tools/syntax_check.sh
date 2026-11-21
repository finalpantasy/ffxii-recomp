#!/usr/bin/env bash
# Syntax-checks SwARc or project sources with the flags of the configured
# build, without compiling objects or linking. Light enough to run without
# the build lock.
#
# Run inside Linux or WSL:  tools/syntax_check.sh win32/ucrt.cpp x86/hle.cpp
# Each argument is a path suffix matched against compile_commands.json.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${FFXII_BUILD:-$ROOT/build/lifted}"
cd "$BUILD"
python3 - "$@" <<'PY'
import json
import shlex
import subprocess
import sys

cc = json.load(open("compile_commands.json"))
failed = 0
for want in sys.argv[1:]:
    e = next((x for x in cc if x["file"].endswith(want) and "platforms/orbis" not in x["file"]),
             None)
    if not e:
        print(want, "has no compile command (new file? configure first)")
        failed += 1
        continue
    out, skip = [], False
    for t in shlex.split(e["command"]):
        if skip:
            skip = False
            continue
        if t in ("-o", "-MF", "-MT"):
            skip = True
            continue
        if t in ("-c", "-MD"):
            continue
        out.append(t)
    r = subprocess.run(["nice", "-n", "10"] + out + ["-fsyntax-only"], cwd=e["directory"],
                       capture_output=True, text=True)
    print(want, "ok" if r.returncode == 0 else "FAILED")
    if r.returncode:
        failed += 1
        print("\n".join(r.stderr.splitlines()[:40]))
sys.exit(1 if failed else 0)
PY
