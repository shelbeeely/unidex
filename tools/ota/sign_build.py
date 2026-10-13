# PlatformIO post-build script (env:x4): signs firmware.bin for radio updates (tools/ota/sign.py) when the release
# key is at hand (OTA_SIGNING_KEY: the PEM, or a path to it), so `pio run -e x4 -t upload` flashes a signed release
# that this X4 can then pass on. Without the key, or for a build that isn't a release, it says so and leaves the
# build unsigned (it still takes updates from other X4s).
import os
import subprocess
import sys

Import("env")


def sign(target, source, env):
    if not os.environ.get("OTA_SIGNING_KEY"):
        return
    script = os.path.join(env.subst("$PROJECT_DIR"), "tools", "ota", "sign.py")
    r = subprocess.run([sys.executable, script, str(target[0])], capture_output=True, text=True)
    print((r.stdout + r.stderr).strip())


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", sign)
