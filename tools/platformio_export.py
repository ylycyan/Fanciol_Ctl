Import("env")

import subprocess
import sys
from pathlib import Path


def export_firmware(source, target, env):
    project = Path(env.subst("$PROJECT_DIR"))
    root = project.parent.parent
    # Child environments do not register this action; there is no recursion.
    result = subprocess.run([
        sys.executable, "-m", "platformio", "run", "-d", str(project),
        "-e", "ch583_updater", "-e", "ch583_jump",
    ])
    if result.returncode:
        return result.returncode
    build = Path(env.subst("$PROJECT_BUILD_DIR"))
    return subprocess.run([
        sys.executable, str(root / "tools/splitac_production.py"), "package",
        "--jump", str(build / "ch583_jump/firmware.bin"),
        "--app", str(Path(env.subst("$BUILD_DIR")) / "firmware.bin"),
        "--updater", str(build / "ch583_updater/firmware.bin"),
        "--output-dir", env.subst("$BUILD_DIR"),
    ]).returncode


if env.subst("$PIOENV") in ("ch583", "ch583_debug"):
    # Register after PlatformIO creates this alias, including cached builds.
    build_target = env.Alias("buildprog")
    env.AlwaysBuild(build_target)
    env.AddPostAction(build_target, export_firmware)
