"""Exercise the actual bot perception and final steering bodies with a fake world."""

import argparse
import os
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def function(source, name):
    match = re.search(r"static (?:boolean|void) " + re.escape(name) + r"\([^;{]*\)\s*\{", source)
    if not match:
        raise AssertionError(f"Missing production test seam: {name}")
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def run(cc, sanitize=False, perception_only=False):
    source = (ROOT / "source/features/bots/bots.c").read_text()
    fixture = (ROOT / "tools/test_bot_perception_steering.c.in").read_text()
    names = ["bot_can_see", "bot_perceive"]
    if not perception_only:
        names += ["bot_movement_heading_clear", "bot_steer_movement", "bot_keep_moving", "bot_hear_human_alert", "bot_submit_action"]
    bodies = "\n\n".join(function(source, name) for name in names)
    fixture = fixture.replace("/* PRODUCTION_BODIES */", bodies)
    with tempfile.TemporaryDirectory(prefix="bot-perception-steering-") as directory:
        path = Path(directory) / "test.c"
        executable = Path(directory) / "test"
        path.write_text(fixture)
        command = [cc, "-std=gnu89", "-Wall", "-Wextra", "-Werror", str(path), "-lm", "-o", str(executable)]
        if perception_only:
            command += ["-DPERCEPTION_ONLY", "-Wno-unused-function"]
        if sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)


def test_bot_perception_steering():
    run(os.environ.get("CC", "clang"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--perception-only", action="store_true")
    args = parser.parse_args()
    run(args.cc, args.sanitize, args.perception_only)
