#!/usr/bin/env python3
"""Makes the scoreboard's platform logos, port/linux/ui/fonts/platform_icons.ttf,
from Font Awesome Free's fonts (its brands and solid sets):

    python tools/platform_icons_font.py fa-brands-400.ttf fa-solid-900.ttf

(from @fortawesome/fontawesome-free's webfonts; 6.7.2 made the one built
in). Keeps the brands' Apple, Windows, Android, Linux, Xbox and Steam logos
and the solid set's gamepad (the consoles Font Awesome has no logo for), as
one font at their own code points, and renames it: the OFL reserves the
name "Font Awesome" from modified fonts. Needs fontTools.
"""

import sys
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "port/linux/ui/fonts/platform_icons.ttf"
# apple, windows, android, linux, steam, xbox
BRANDS = [0xF179, 0xF17A, 0xF17B, 0xF17C, 0xF1B6, 0xF412]
# gamepad
SOLID = [0xF11B]
NAMES = {1: "Platform Icons", 3: "Platform Icons", 4: "Platform Icons", 6: "PlatformIcons",
         16: "Platform Icons", 17: "Regular"}


def cut(path: str, codes: list) -> TTFont:
    """The font with only the glyphs of codes."""
    font = TTFont(path)
    options = subset.Options()
    options.layout_features = []
    options.name_IDs = ["*"]
    options.notdef_outline = True
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=codes)
    subsetter.subset(font)
    return font


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    brands = cut(sys.argv[1], BRANDS)
    solid = cut(sys.argv[2], SOLID)
    if brands["head"].unitsPerEm != solid["head"].unitsPerEm:
        sys.exit("the fonts' units per em differ")
    # (the solid set's glyphs moved into the brands' font)
    order = brands.getGlyphOrder()
    solid_map = solid.getBestCmap()
    for code in SOLID:
        source = solid_map[code]
        name = f"solid_{code:04x}"
        order = order + [name]
        brands["glyf"].glyphs[name] = solid["glyf"][source]
        brands["hmtx"].metrics[name] = solid["hmtx"].metrics[source]
        for table in brands["cmap"].tables:
            if table.isUnicode():
                table.cmap[code] = name
    brands["glyf"].glyphOrder = order
    brands.setGlyphOrder(order)
    brands["maxp"].numGlyphs = len(order)
    for record in brands["name"].names:
        if record.nameID in NAMES:
            record.string = NAMES[record.nameID]
    brands.save(OUTPUT)
    print(f"wrote {OUTPUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
