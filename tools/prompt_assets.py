#!/usr/bin/env python3
"""Renders the controller's button icons (port/assets/prompts/svg) into the
PNGs the builds embed (port/assets/prompts/*.png, committed), for the
overlay's prompts (port/linux/src/ui_overlay.c: Online Games):

    python3 tools/prompt_assets.py

Each icon is drawn square, PROMPT_ICON_SIZE pixels each way, as its SVG's
square viewBox lays it out. Needs rsvg-convert.
"""

import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROMPTS = ROOT / "port/assets/prompts"
PROMPT_ICON_SIZE = 128


def main() -> int:
    if not shutil.which("rsvg-convert"):
        sys.exit("needs rsvg-convert (librsvg)")
    for svg in sorted((PROMPTS / "svg").glob("*.svg")):
        png = PROMPTS / (svg.stem + ".png")
        subprocess.run(["rsvg-convert", "-w", str(PROMPT_ICON_SIZE), "-h", str(PROMPT_ICON_SIZE), "-o", str(png),
                        str(svg)], check=True)
        print(png.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
