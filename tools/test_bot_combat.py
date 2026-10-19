"""Strict asset-free production native-style combat evaluator tests."""
import argparse
import os
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run(cc, sanitize=False):
    with tempfile.TemporaryDirectory(prefix="bot-combat-") as directory:
        executable = Path(directory) / "test"
        command = [cc, "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Wconversion",
                   "-Wsign-conversion", "-Wshadow", "-Wstrict-prototypes", "-Wmissing-prototypes",
                   "-Wformat=2", "-Wundef", "-Wwrite-strings", "-Werror",
                   "-I", str(ROOT / "source"),
                   str(ROOT / "source/features/bots/bot_profile.c"),
                   str(ROOT / "source/features/bots/bot_combat.c"),
                   str(ROOT / "tools/test_bot_combat.c"), "-o", str(executable)]
        if sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)


def production_body(source, name):
    match = re.search(r"(?:^|\n)static [\w *]+\b" + name + r"\([^;{]*\)\s*\{", source)
    assert match, name
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def run_adapter(cc, sanitize=False):
    source = (ROOT / "source/features/bots/bots.c").read_text()
    names = ("bot_unit_vitality", "bot_unit_melee_active", "bot_unit_combat_busy", "bot_update_combat",
             "bot_combat_inputs", "bot_update_aim_error", "bot_engage")
    fixture = (ROOT / "tools/test_bot_combat_adapter.c.in").read_text()
    fixture = fixture.replace("/* PRODUCTION_BODIES */", "\n".join(production_body(source, name) for name in names))
    with tempfile.TemporaryDirectory(prefix="bot-combat-adapter-") as directory:
        path = Path(directory) / "test.c"
        executable = Path(directory) / "test"
        path.write_text(fixture)
        command = [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "source"),
                   str(path), str(ROOT / "source/features/bots/bot_combat.c"),
                   str(ROOT / "source/features/bots/bot_profile.c"),
                   str(ROOT / "source/features/bots/bot_reaction.c"),
                   str(ROOT / "source/engine_ai/aim.c"), "-lm", "-o", str(executable)]
        if sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)


def test_bot_combat_adapter():
    run_adapter(os.environ.get("CC", "clang"), sanitize=True)


def test_bot_combat():
    run(os.environ.get("CC", "clang"), sanitize=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    run(args.cc, args.sanitize)
    run_adapter(args.cc, args.sanitize)
