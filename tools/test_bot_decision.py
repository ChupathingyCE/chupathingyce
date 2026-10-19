"""Compile and run the production C bot scoring policy without Halo assets."""
import argparse
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run(cc, sanitize=False):
    with tempfile.TemporaryDirectory(prefix="bot-decision-test-") as directory:
        executable = Path(directory) / "test_bot_decision"
        command = [
            cc, "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Wconversion",
            "-Wsign-conversion", "-Wshadow", "-Wstrict-prototypes", "-Wmissing-prototypes",
            "-Wformat=2", "-Wundef", "-Wwrite-strings", "-Werror",
            "-I", str(ROOT / "source"),
            str(ROOT / "source/features/bots/bot_decision.c"),
            str(ROOT / "source/engine_ai/utility.c"),
            str(ROOT / "tools/test_bot_decision.c"),
            "-o", str(executable),
        ]
        if sanitize:
            command.extend(["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])
        subprocess.run(command, cwd=ROOT, check=True, timeout=60)
        subprocess.run([str(executable)], cwd=ROOT, check=True, timeout=30)


def test_bot_decision():
    run(os.environ.get("CC", "clang"), sanitize=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    run(args.cc, args.sanitize)
