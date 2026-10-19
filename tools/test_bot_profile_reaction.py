"""Asset-free production difficulty profile and reaction memory tests."""
import argparse
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run(cc, sanitize=False):
    with tempfile.TemporaryDirectory(prefix="bot-profile-reaction-") as directory:
        executable = Path(directory) / "test"
        command = [cc, "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Wconversion",
                   "-Wsign-conversion", "-Wshadow", "-Wstrict-prototypes", "-Wmissing-prototypes",
                   "-Wformat=2", "-Wundef", "-Wwrite-strings", "-Werror",
                   "-I", str(ROOT / "source"),
                   str(ROOT / "source/features/bots/bot_profile.c"),
                   str(ROOT / "source/features/bots/bot_reaction.c"),
                   str(ROOT / "tools/test_bot_profile_reaction.c"), "-o", str(executable)]
        if sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)


def test_bot_profile_reaction():
    run(os.environ.get("CC", "clang"), sanitize=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    run(args.cc, args.sanitize)
