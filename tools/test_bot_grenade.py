"""Production ballistic solver and CE grenade/danger adapter without Halo assets."""
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def compile_run(source, strict=False):
    with tempfile.TemporaryDirectory(prefix="bot-grenade-") as directory:
        path = Path(directory) / "test.c"
        executable = Path(directory) / "test"
        path.write_text(source)
        flags = ["-Wall", "-Wextra", "-Werror"]
        if strict:
            flags += ["-Wpedantic", "-Wconversion", "-Wsign-conversion", "-Wshadow",
                      "-Wstrict-prototypes", "-Wmissing-prototypes", "-Wformat=2", "-Wundef"]
        subprocess.run([os.environ.get("CC", "clang"), "-std=c11", *flags,
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-I", str(ROOT / "source"), str(path),
                        str(ROOT / "source/features/bots/bot_ballistics.c"),
                        "-lm", "-o", str(executable)], check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)


def test_ballistics():
    source = (ROOT / "tools/test_bot_ballistics.c").read_text()
    # The temporary translation unit cannot resolve the test's relative include.
    source = source.replace('../source/features/bots/bot_ballistics.h',
                            'features/bots/bot_ballistics.h')
    compile_run(source, strict=True)


def test_grenade_danger_adapter():
    source = (ROOT / "source/features/bots/bot_grenade.c").read_text()
    # Strip engine includes only, not any production declarations or bodies.
    adapter = source[source.index("struct bot_grenade_parameters"):]
    fixture = (ROOT / "tools/test_bot_grenade.c.in").read_text()
    compile_run(fixture.replace("/* PRODUCTION_ADAPTER */", adapter))
