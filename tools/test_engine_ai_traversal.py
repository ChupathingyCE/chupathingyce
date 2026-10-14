"""Compile and run the standalone C11 AI traversal primitive without Halo."""

import argparse
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def run(cc, sanitize=False):
    with tempfile.TemporaryDirectory(prefix="engine-ai-traversal-test-") as directory:
        executable = Path(directory) / "test_engine_ai_traversal"
        command = [
            cc, "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Wconversion",
            "-Wsign-conversion", "-Wshadow", "-Wstrict-prototypes", "-Wmissing-prototypes",
            "-Wformat=2", "-Wundef", "-Wwrite-strings", "-Werror",
            "-I", str(ROOT / "source"),
            str(ROOT / "source/engine_ai/navigation.c"),
            str(ROOT / "source/engine_ai/traversal.c"),
            str(ROOT / "tools/test_engine_ai_traversal.c"),
            "-o", str(executable),
        ]
        if sanitize:
            command.extend(["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])
        subprocess.run(command, cwd=ROOT, check=True, timeout=60)
        subprocess.run([str(executable)], cwd=ROOT, check=True, timeout=30)


def test_engine_ai_traversal():
    run(os.environ.get("CC", "clang"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    if not shutil.which(args.cc):
        parser.error(f"C compiler not found: {args.cc}")
    run(args.cc, args.sanitize)
