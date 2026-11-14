#!/usr/bin/env python3
"""Renders DKrumpp's HD controller buttons (hd_buttons_v1.zip, his 4096-pixel
PNGs, unpacked) into the icons the builds embed (port/assets/buttons_hd,
committed), for the overlay's prompts (port/linux/src/ui_overlay.c):

    python3 tools/hd_button_assets.py .../hd_buttons_v1

Each icon is cut square around what it draws (its longer side, a little
room around it), then shrunk to ICON_SIZE pixels each way, as
port/assets/prompts' are. The art is used with DKrumpp's permission, with
credit; it is not under Chupathingy's license (port/assets/buttons_hd/
README.md, docs/permissions.md). Needs Pillow.
"""

import sys
from pathlib import Path

from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "port/assets/buttons_hd"
ICON_SIZE = 128
# (the room around an icon, of its longer side)
MARGIN = 0.02
# the icons, by their names here and his: the Duke's (the original Xbox
# controller's) in his gem style, its face buttons flat too, then the
# PlayStation's and the Switch's face buttons, gem and flat
ICONS = {
    "duke-a-gem": "A_gem", "duke-b-gem": "B_gem", "duke-x-gem": "X_gem", "duke-y-gem": "Y_gem",
    "duke-start": "start", "duke-trigger-left": "trigger_left", "duke-trigger-right": "trigger_right",
    "duke-white": "white_button", "duke-black": "black_button", "duke-back": "back",
    "duke-a-flat": "A_simple", "duke-b-flat": "B_simple", "duke-x-flat": "X_simple", "duke-y-flat": "Y_simple",
    "ps-cross-gem": "X-PS_gem", "ps-circle-gem": "circle_gem", "ps-square-gem": "square_gem",
    "ps-triangle-gem": "triangle_gem",
    "ps-cross-flat": "X-PS_simple", "ps-circle-flat": "circle_simple", "ps-square-flat": "square_simple",
    "ps-triangle-flat": "triangle_simple",
    "switch-a-gem": "A_switch", "switch-b-gem": "B_switch", "switch-x-gem": "X_switch", "switch-y-gem": "Y_switch",
    "switch-a-flat": "A-switch_simple", "switch-b-flat": "B-switch_simple", "switch-x-flat": "X-switch_simple",
    "switch-y-flat": "Y-switch_simple",
}
# the icons drawn with a light rim around them, as the others' white one:
# the black button, which is black on the menus' dark blues
RIMMED = {"duke-black"}


def rimmed(icon: Image.Image) -> Image.Image:
    """An icon over a light grey rim a few pixels wide, its shape grown"""
    alpha = icon.getchannel("A").filter(ImageFilter.MaxFilter(9))
    rim = Image.new("RGBA", icon.size, (200, 204, 212, 255))
    rim.putalpha(alpha)
    rim.alpha_composite(icon)
    return rim


def main() -> int:
    if len(sys.argv) != 2:
        sys.exit("usage: hd_button_assets.py FOLDER (hd_buttons_v1.zip unpacked)")
    source = Path(sys.argv[1])
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name, his in ICONS.items():
        image = Image.open(source / f"{his}.png").convert("RGBA")
        left, top, right, bottom = image.getbbox()
        side = max(right - left, bottom - top)
        side = int(side * (1 + 2 * MARGIN))
        x0 = (left + right) // 2 - side // 2
        y0 = (top + bottom) // 2 - side // 2
        square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
        square.paste(image.crop((x0, y0, x0 + side, y0 + side)), (0, 0))
        icon = square.resize((ICON_SIZE, ICON_SIZE), Image.LANCZOS)
        if name in RIMMED:
            icon = rimmed(icon)
        icon.save(OUTPUT / f"{name}.png", optimize=True)
        print((OUTPUT / f"{name}.png").relative_to(ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
