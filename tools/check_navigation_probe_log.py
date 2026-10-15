"""Check opt-in geometry navigation acceptance evidence, not every collision frame."""
import argparse
import re
from pathlib import Path


def check(text, mode):
    if re.search(r"FAILURE|assertion failed|Assertion failed|AddressSanitizer|runtime error:", text):
        raise ValueError("crash/assertion/sanitizer failure in log")
    publications = re.findall(r"nav: published (\d+) walkable polygons", text)
    if not publications or max(map(int, publications)) == 0:
        raise ValueError("no nonempty published geometry resource")
    if mode == "walk":
        if not re.search(r"nav-probe: route found, \d+ waypoints", text):
            raise ValueError("no successful route")
        errors = re.findall(r"nav-probe: reached goal .*error ([\d.]+), normal player controls", text)
        if not errors or min(map(float, errors)) >= 0.18:
            raise ValueError("no goal reached within probe tolerance")
        if "nav-probe: following waypoint" not in text:
            raise ValueError("no sampled following movement")
    elif "query/path failed (bad-goal)" not in text:
        raise ValueError("expected explicit bad-goal failure missing")
    print(f"navigation {mode} evidence passed ({max(map(int, publications))} polygons)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("walk", "bad-goal"))
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    try:
        check(args.log.read_text(errors="replace"), args.mode)
    except ValueError as error:
        parser.exit(1, f"FAIL: {error}\n")
