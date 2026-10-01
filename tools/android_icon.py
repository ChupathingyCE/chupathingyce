#!/usr/bin/env python3
"""Makes the Android app's launcher icon from its artwork,
port/android/art/android-icon.png (an emblem on a solid background; drawn
from port/art/icon.svg):

    python tools/android_icon.py

The icon is an adaptive one (Android 8 and later): the artwork's background
colour as its background layer, and the emblem, cut out onto transparency,
as its foreground and monochrome (themed icon) layers. Launchers mask
adaptive icons to their own shapes and show only the middle of the 108 dp
layers, so the emblem is scaled to fit Android's 66 dp safe zone, the circle
no mask cuts into. Writes the layers for every screen density into
port/android/app/src/main/res (mipmap-*), and the icon's definition
(mipmap-anydpi-v26/ic_launcher.xml); run it again when the artwork changes.
Needs Pillow.
"""

import math
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ARTWORK = ROOT / "port/android/art/android-icon.png"
RESOURCES = ROOT / "port/android/app/src/main/res"

# the layers' size, and the safe zone's diameter, in dp
LAYER_DP = 108
SAFE_DP = 64  # (66, less a little room)
# pixels per dp of each density
DENSITIES = {"mdpi": 1.0, "hdpi": 1.5, "xhdpi": 2.0, "xxhdpi": 3.0, "xxxhdpi": 4.0}


def main() -> None:
    artwork = Image.open(ARTWORK).convert("RGBA")
    width, height = artwork.size
    background = artwork.getpixel((0, 0))[:3]

    # the emblem on transparency: each pixel's opacity is how far it is from
    # the background, against the emblem's colour closest to the background
    # (the artwork's edges are blended between the two), and its colour the
    # one that, so blended, gives the artwork's
    def distance(pixel):
        return math.dist(pixel[:3], background)
    # (the emblem's colours: the artwork's common ones, not its blended edges)
    colours = [colour[:3] for count, colour in artwork.getcolors(width * height) if count >= width * height // 1000]
    edge = min((distance(colour) for colour in colours if distance(colour) > 0), default=1.0)
    emblem = Image.new("RGBA", artwork.size)
    furthest = 0.0
    for y in range(height):
        for x in range(width):
            pixel = artwork.getpixel((x, y))
            opacity = max(0.0, min(1.0, distance(pixel) / edge))
            if opacity > 0.0:
                colour = tuple(max(0, min(255, round(b + (p - b) / opacity))) for p, b in zip(pixel[:3], background))
                emblem.putpixel((x, y), (*colour, round(opacity * 255)))
                furthest = max(furthest, math.hypot(x + 0.5 - width / 2, y + 0.5 - height / 2))

    # the artwork scaled so that the emblem's furthest point from the middle
    # is on the safe zone's edge
    artwork_dp = (SAFE_DP / 2) / furthest * width
    for density, scale in DENSITIES.items():
        size = round(LAYER_DP * scale)
        side = round(artwork_dp * scale)
        layer = Image.new("RGBA", (size, size))
        layer.alpha_composite(emblem.resize((side, side), Image.LANCZOS), ((size - side) // 2, (size - side) // 2))
        directory = RESOURCES / f"mipmap-{density}"
        directory.mkdir(parents=True, exist_ok=True)
        layer.save(directory / "ic_launcher_foreground.png", optimize=True)
        print(f"{directory.relative_to(ROOT)}/ic_launcher_foreground.png: {size}x{size}")

    colour = "#{:02X}{:02X}{:02X}".format(*background)
    values = RESOURCES / "values"
    values.mkdir(parents=True, exist_ok=True)
    (values / "ic_launcher_background.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        "<!-- the launcher icon's background (tools/android_icon.py) -->\n"
        "<resources>\n"
        f'    <color name="ic_launcher_background">{colour}</color>\n'
        "</resources>\n", encoding="utf-8")
    adaptive = RESOURCES / "mipmap-anydpi-v26"
    adaptive.mkdir(parents=True, exist_ok=True)
    (adaptive / "ic_launcher.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        "<!-- the launcher icon (tools/android_icon.py, from port/android/art/android-icon.png) -->\n"
        '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
        '    <background android:drawable="@color/ic_launcher_background" />\n'
        '    <foreground android:drawable="@mipmap/ic_launcher_foreground" />\n'
        '    <monochrome android:drawable="@mipmap/ic_launcher_foreground" />\n'
        "</adaptive-icon>\n", encoding="utf-8")
    print(f"background {colour}; the emblem {artwork_dp:.1f} dp across its artwork")


if __name__ == "__main__":
    main()
