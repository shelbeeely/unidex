# PlatformIO pre-build script: the firmware version from git ("v1.4" on a release, "v1.4-3-gabc1234" between
# them, "-dirty" after it with uncommitted changes, "-test" for the test build, "dev" without git), written to a small header in the build folder (only rewritten when it changes, so
# just the files that show it are rebuilt). Settings shows it; the website's one-click sync compares it with the
# newest release.
import os
import subprocess

Import("env")

try:
    version = subprocess.check_output(["git", "describe", "--tags", "--always", "--dirty"], cwd=env.subst("$PROJECT_DIR"),
                                      stderr=subprocess.DEVNULL, text=True).strip()
except Exception:
    version = "dev"
if env["PIOENV"] in ("dev", "x4-dev"):
    version += "-test"

folder = os.path.join(env.subst("$BUILD_DIR"), "generated")
os.makedirs(folder, exist_ok=True)
header = os.path.join(folder, "unidex_version.h")
# The release number radio updates compare (src/core/ota_logic.h's seqOf): from the last tag, "v1.5" -> 1005000, so
# a build between releases never looks newer than its release. 0 without a release tag (and for the test build).
try:
    tag = subprocess.check_output(["git", "describe", "--tags", "--abbrev=0"], cwd=env.subst("$PROJECT_DIR"),
                                  stderr=subprocess.DEVNULL, text=True).strip()
except Exception:
    tag = ""
parts = tag[1:].split(".") if tag.startswith("v") else []
seq = 0
if 2 <= len(parts) <= 3 and all(p.isdigit() and int(p) <= 999 for p in parts):
    parts = [int(p) for p in parts] + [0] * (3 - len(parts))
    seq = parts[0] * 1000000 + parts[1] * 1000 + parts[2]
if env["PIOENV"] in ("dev", "x4-dev"):
    seq = 0
text = '#pragma once\n#define UNIDEX_VERSION "%s"\n#define UNIDEX_OTA_SEQ %d\n' % (version, seq)
if not os.path.exists(header) or open(header).read() != text:
    with open(header, "w") as f:
        f.write(text)
env.Append(CPPPATH=[folder])
