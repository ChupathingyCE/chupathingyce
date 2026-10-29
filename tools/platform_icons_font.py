#!/usr/bin/env python3
"""Makes the scoreboard's platform logos, port/linux/ui/fonts/platform_icons.ttf,
from the one-color SVGs in port/linux/ui/fonts/platform_icons/:

    python tools/platform_icons_font.py

Each SVG is 24 x 24 with a single filled path (no transforms, strokes or
gradients; holes wind against their outline). It becomes one glyph, named by
the file, at U+E100 plus each of its platform numbers (delta.h's enum
delta_platform, PLATFORMS below): pc_windows.svg is platform 1, and
playstation.svg draws every PlayStation's (10, 11, 14, 15, 16). Needs
fontTools.
"""

import sys
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.svgLib.path import parse_path
from xml.etree import ElementTree

ROOT = Path(__file__).resolve().parent.parent
SOURCES = ROOT / "port/linux/ui/fonts/platform_icons"
OUTPUT = ROOT / "port/linux/ui/fonts/platform_icons.ttf"
BASE = 0xE100
# each SVG's platforms, delta.h's enum delta_platform (unknown has no logo,
# nor do the Nintendo 64, 17, and the Legacy PC, 18, yet)
PLATFORMS = {
    "pc_windows": (1,), "pc_macos": (2,), "pc_linux": (3,), "android": (4,), "steam_deck": (5,), "xbox": (6,),
    "xbox360": (7,), "wiiu": (8,), "switch": (9,), "xbox_uwp": (12,), "pc_freebsd": (13,),
    # (the PSP, the Vita, the PS3, the PS4 and the PS5)
    "playstation": (10, 11, 14, 15, 16),
}
UNITS = 1024
ASCENT = 880
DESCENT = 144
NAMES = {1: "Platform Icons", 2: "Regular", 3: "Platform Icons Regular", 4: "Platform Icons", 6: "PlatformIcons"}


def glyph(path: Path):
    """The glyph of an SVG's path, y flipped and scaled from 24 units to the em."""
    paths = [e.get("d") for e in ElementTree.parse(path).getroot().iter() if e.tag.endswith("path")]
    if len(paths) != 1:
        sys.exit(f"{path.name}: expected one path, found {len(paths)}")
    scale = UNITS / 24
    # (the logo spans the whole em: from the ascent down through the descent)
    pen = TTGlyphPen(None)
    parse_path(paths[0], TransformPen(Cu2QuPen(pen, 1.0, reverse_direction=True),
                                      (scale, 0, 0, -scale, 0, ASCENT)))
    return pen.glyph()


def main() -> None:
    files = sorted(SOURCES.glob("*.svg"))
    unknown = [f.name for f in files if f.stem not in PLATFORMS]
    if unknown or not files:
        sys.exit(f"no platform for {unknown}" if unknown else "no SVGs")
    files.sort(key=lambda f: PLATFORMS[f.stem][0])
    order = [".notdef"] + [f.stem for f in files]
    glyphs = {".notdef": TTGlyphPen(None).glyph()}
    metrics = {".notdef": (UNITS, 0)}
    cmap = {}
    for f in files:
        glyphs[f.stem] = glyph(f)
        metrics[f.stem] = (UNITS, 0)
        for platform in PLATFORMS[f.stem]:
            cmap[BASE + platform] = f.stem
    builder = FontBuilder(UNITS, isTTF=True)
    builder.setupGlyphOrder(order)
    builder.setupCharacterMap(cmap)
    builder.setupGlyf(glyphs)
    builder.setupHorizontalMetrics(metrics)
    builder.setupHorizontalHeader(ascent=ASCENT, descent=-DESCENT)
    builder.setupNameTable(NAMES)
    builder.setupOS2(sTypoAscender=ASCENT, sTypoDescender=-DESCENT, usWinAscent=ASCENT, usWinDescent=DESCENT)
    builder.setupPost()
    builder.save(OUTPUT)
    print(f"wrote {OUTPUT.relative_to(ROOT)}: {len(files)} logos")


if __name__ == "__main__":
    main()
