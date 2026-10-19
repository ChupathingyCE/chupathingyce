"""Exercise the bots' actual friendly-fire gates (shots, grenades, charged
triggers) with a fake world: bots.c's own function bodies, never copies."""

import argparse
import os
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NAMES = ["bot_weapon_unit", "bot_equipped_weapon", "bot_weapon_attribute", "bot_current_weapon_attribute",
         "bot_weapon_preference", "bot_preferred_range", "bot_wants_weapon", "bot_lane_near", "bot_lane_near_unit", "bot_blast_reaches", "bot_grenade_landing_safe", "bot_trigger_spread", "bot_attack_safe",
         "bot_carries_flag", "bot_control_grenade",
         "bot_other_weapon_slot", "bot_hold_fire", "bot_control_weapon"]


def function(source, name):
    match = re.search(r"^static [\w *]+\b" + re.escape(name) + r"\([^;{]*\)\s*\{", source, re.M)
    if not match:
        raise AssertionError(f"Missing production test seam: {name}")
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def run(cc, sanitize=False):
    source = (ROOT / "source/features/bots/bots.c").read_text()
    constants = "\n".join(line for line in source.splitlines()
                          if re.match(r"#define BOT_(FIRE_LANE_|GRENADE_SAFETY_|GRENADE_FUSE_|MAXIMUM_SPEED_)\w+ ", line))
    bodies = "\n\n".join(function(source, name) for name in NAMES)
    fixture = (ROOT / "tools/test_bot_fire_safety.c.in").read_text()
    fixture = fixture.replace("/* PRODUCTION_BODIES */", constants + "\n\n" + bodies)
    with tempfile.TemporaryDirectory(prefix="bot-fire-safety-") as directory:
        path = Path(directory) / "test.c"
        executable = Path(directory) / "test"
        path.write_text(fixture)
        # (bot_control_weapon compares a checked non-negative short with
        # NUMBEROF's size_t, as the game's build allows)
        command = [cc, "-std=gnu89", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                   "-Wno-sign-compare", "-I", str(ROOT / "source"), str(path),
                   str(ROOT / "source/features/bots/bot_combat.c"),
                   str(ROOT / "source/features/bots/bot_profile.c"), "-lm", "-o", str(executable)]
        if sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)


def test_bot_fire_safety():
    run(os.environ.get("CC", "clang"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    run(args.cc, args.sanitize)
