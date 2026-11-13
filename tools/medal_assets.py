#!/usr/bin/env python3
"""Makes the game's medals from the site's: their names and icons, one set
for both (stats.chupathingy.org and the game's HUD and DELTA's SERVICE
RECORD). Run by hand when the site's medals change, with a checkout of
ChupathingyCE/puma:

    python3 tools/medal_assets.py PUMA_CHECKOUT

It reads the site's tables (web/site.js: MEDALS, TIERS, SHAPES, GLYPHS),
draws each medal's icon as the site's medalSVG draws it (the family's shape
in the tier's gradient, the glyph in white), renders it with rsvg-convert,
and writes, all committed:

- port/assets/medals/medals.json: each medal's key, name, description,
  tier and family, in the site's order, its place in the atlases (index:
  row index // columns, column index % columns) and the digest of the
  site's tables it was made from;
- port/assets/medals/medals-64.png and medals-32.png: the icons, a cell
  each of 64 and 32 pixels, with a transparent margin;
- port/linux/src/medal_icons.h: the same table for the game's C (the
  builds embed the PNG's pixels: tools/embed_assets.py --fonts).

The site's check (puma's test_medals_sync.py) keeps the same digest, so a
change to the site's medals says to run this again.
"""

import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from embed_assets import png_rgba  # noqa: E402

ASSETS = ROOT / "port/assets/medals"
HEADER = ROOT / "port/linux/src/medal_icons.h"
COLUMNS = 8
SIZES = (32, 64)


def digest_region(source: str) -> str:
    """The part of site.js the medals are drawn from: the tables and
    medalSVG, up to medalChip. puma's test_medals_sync.py hashes the same."""
    start = source.index("const MEDALS = {")
    end = source.index("function medalChip(")
    return source[start:end]


def digest(source: str) -> str:
    return hashlib.sha256(digest_region(source).encode("utf-8")).hexdigest()


def block(source: str, name: str) -> str:
    """A table's body (const NAME = { ... }; its closing brace at the start
    of a line), its comments taken out."""
    start = source.find(f"const {name} = {{")
    end = source.find("\n};", start)
    if start < 0 or end < 0:
        sys.exit(f"site.js: no {name}")
    return re.sub(r"/\*.*?\*/", "", source[start + len(f"const {name} = {{"):end], flags=re.S)


def parse(source: str) -> tuple:
    medals = []
    for match in re.finditer(r"^\s*(\w+):\s*\{(.*?)\},?\s*$", block(source, "MEDALS"), re.M):
        fields = dict(re.findall(r'(\w+):\s*"((?:[^"\\]|\\.)*)"', match.group(2)))
        for field in ("name", "desc", "family", "tier", "glyph"):
            if field not in fields:
                sys.exit(f"site.js: MEDALS.{match.group(1)} has no {field}")
        medals.append({"key": match.group(1), **fields})
    tiers = {name: list(colors) for name, *colors in
             re.findall(r'(\w+):\s*\["(#[0-9a-fA-F]+)",\s*"(#[0-9a-fA-F]+)",\s*"(#[0-9a-fA-F]+)"\]',
                        block(source, "TIERS"))}
    shapes = dict(re.findall(r'(\w+):\s*"([^"]*)"', block(source, "SHAPES")))
    glyphs = dict(re.findall(r'(\w+):\s*"([^"]*)"', block(source, "GLYPHS")))
    if not medals or not tiers or not shapes or not glyphs:
        sys.exit("site.js: a medal table is empty")
    return medals, tiers, shapes, glyphs


def svg(medal: dict, tiers: dict, shapes: dict, glyphs: dict) -> str:
    """site.js's medalSVG, as markup."""
    light, mid, dark = tiers.get(medal["tier"], tiers["gray"])
    shape = shapes.get(medal["family"], shapes["objective"])
    glyph = glyphs.get(medal["glyph"], glyphs["star"])
    tall = medal["family"] == "award"
    middle = 39 if tall else 32
    return (
        f'<svg viewBox="0 {"0 64 78" if tall else "0 64 64"}" xmlns="http://www.w3.org/2000/svg">'
        f'<defs><linearGradient id="m1f" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{light}"/>'
        f'<stop offset=".55" stop-color="{mid}"/><stop offset="1" stop-color="{dark}"/></linearGradient>'
        f'<linearGradient id="m1s" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".55"/>'
        f'<stop offset=".5" stop-color="#fff" stop-opacity="0"/></linearGradient></defs>'
        f'<path d="{shape}" fill="{dark}" transform="translate(0 1.5)" opacity=".7"/>'
        f'<path d="{shape}" fill="url(#m1f)" stroke="{light}" stroke-width="1.6" stroke-linejoin="round"/>'
        f'<path d="{shape}" fill="url(#m1s)" transform="translate(32 {middle}) scale(.86) translate(-32 -{middle})"/>'
        f'<g transform="translate(0 {7 if tall else 0})"><path d="{glyph}" fill="{dark}" opacity=".55" '
        f'transform="translate(0 1.4)"/><path d="{glyph}" fill="#fff" fill-rule="evenodd"/></g></svg>')


def render(markup: str, width: int, height: int, folder: Path) -> bytes:
    """The SVG as width by height RGBA pixels (rsvg-convert)."""
    source, output = folder / "medal.svg", folder / "medal.png"
    source.write_text(markup)
    subprocess.run(["rsvg-convert", "-w", str(width), "-h", str(height), "-o", str(output), str(source)], check=True)
    got_width, got_height, pixels = png_rgba(output.read_bytes(), str(output))
    if (got_width, got_height) != (width, height):
        sys.exit(f"rsvg-convert drew {got_width}x{got_height}, not {width}x{height}")
    return pixels


def png(width: int, height: int, pixels: bytes) -> bytes:
    """An 8-bit RGBA PNG (no filters: the kind embed_assets.py reads)."""
    raw = b"".join(b"\x00" + pixels[row * width * 4:(row + 1) * width * 4] for row in range(height))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return (len(data).to_bytes(4, "big") + kind + data +
                (zlib.crc32(kind + data) & 0xFFFFFFFF).to_bytes(4, "big"))

    header = width.to_bytes(4, "big") + height.to_bytes(4, "big") + bytes((8, 6, 0, 0, 0))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) +
            chunk(b"IEND", b""))


def atlas(medals: list, tiers: dict, shapes: dict, glyphs: dict, size: int, folder: Path) -> tuple:
    """Every medal's icon in a cell size pixels square, a margin of size/32
    pixels round it (the filtering's), the tall award shape centered."""
    rows = (len(medals) + COLUMNS - 1) // COLUMNS
    width, height = COLUMNS * size, rows * size
    pixels = bytearray(width * height * 4)
    margin = max(1, size // 32)
    inner = size - 2 * margin
    for index, medal in enumerate(medals):
        tall = medal["family"] == "award"
        icon_width = round(inner * 64 / 78) if tall else inner
        icon = render(svg(medal, tiers, shapes, glyphs), icon_width, inner, folder)
        left = (index % COLUMNS) * size + (size - icon_width) // 2
        top = (index // COLUMNS) * size + margin
        for row in range(inner):
            at = ((top + row) * width + left) * 4
            pixels[at:at + icon_width * 4] = icon[row * icon_width * 4:(row + 1) * icon_width * 4]
    return width, height, bytes(pixels)


def c_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def header(medals: list, tiers: dict, site_digest: str) -> str:
    rows = (len(medals) + COLUMNS - 1) // COLUMNS
    entries = []
    for medal in medals:
        light, mid, _ = tiers.get(medal["tier"], tiers["gray"])
        entries.append(f'\t{{ {c_string(medal["key"])}, {c_string(medal["name"])}, {c_string(medal["desc"])}, '
                       f'{c_string(medal["tier"])}, 0x{mid[1:].upper()}FFU, 0x{light[1:].upper()}FFU }},')
    return "\n".join([
        "/* generated by tools/medal_assets.py from the site's medals (ChupathingyCE/puma web/site.js): do not edit */",
        "/*",
        "MEDAL_ICONS.H",
        "",
        "The medals as the site has them, one set for the site and the game: each",
        "one's key, name, description and tier, and its tier's colors (0xRRGGBBAA,",
        "the middle and the light), in the site's order, which is their icons' order",
        "in the atlases (port/assets/medals: a cell each, MEDAL_ICON_COLUMNS to a",
        "row; ui_overlay_medal draws one). medal_rules.h's keys are all here",
        "(tools/test_medal_rules.py).",
        "",
        f"site digest {site_digest}",
        "*/",
        "",
        "#ifndef MEDAL_ICONS_H",
        "#define MEDAL_ICONS_H",
        "",
        "#include <string.h>",
        "",
        "enum",
        "{",
        f"\tMEDAL_ICON_COUNT = {len(medals)},",
        f"\tMEDAL_ICON_COLUMNS = {COLUMNS},",
        f"\tMEDAL_ICON_ROWS = {rows},",
        "};",
        "",
        "struct medal_icon",
        "{",
        "\tchar const *key, *name, *description, *tier;",
        "\tunsigned int color, light;",
        "};",
        "",
        "/* a medal's entry by its index (NULL past the last) */",
        "static inline struct medal_icon const *medal_icon(int index)",
        "{",
        "\tstatic struct medal_icon const icons[MEDAL_ICON_COUNT] = {",
        *entries,
        "\t};",
        "",
        "\treturn index >= 0 && index < MEDAL_ICON_COUNT ? &icons[index] : NULL;",
        "}",
        "",
        "/* a key's index, -1 for none */",
        "static inline int medal_icon_find(char const *key)",
        "{",
        "\tint index;",
        "",
        "\tfor (index = 0; key && index < MEDAL_ICON_COUNT; index++)",
        "\t{",
        "\t\tif (!strcmp(medal_icon(index)->key, key))",
        "\t\t\treturn index;",
        "\t}",
        "\treturn -1;",
        "}",
        "",
        "#endif",
        "",
    ])


def main() -> int:
    if len(sys.argv) != 2:
        sys.exit("usage: medal_assets.py PUMA_CHECKOUT")
    if not shutil.which("rsvg-convert"):
        sys.exit("needs rsvg-convert (librsvg)")
    site = Path(sys.argv[1]) / "web" / "site.js"
    source = site.read_text(encoding="utf-8")
    medals, tiers, shapes, glyphs = parse(source)
    site_digest = digest(source)
    ASSETS.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as scratch:
        for size in SIZES:
            width, height, pixels = atlas(medals, tiers, shapes, glyphs, size, Path(scratch))
            (ASSETS / f"medals-{size}.png").write_bytes(png(width, height, pixels))
    data = {
        "generated": "tools/medal_assets.py from ChupathingyCE/puma web/site.js: do not edit",
        "site_digest": site_digest,
        "columns": COLUMNS,
        "sizes": list(SIZES),
        "atlases": {str(size): f"medals-{size}.png" for size in SIZES},
        "medals": [{"key": medal["key"], "name": medal["name"], "desc": medal["desc"], "tier": medal["tier"],
                    "family": medal["family"], "index": index} for index, medal in enumerate(medals)],
    }
    (ASSETS / "medals.json").write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n")
    HEADER.write_text(header(medals, tiers, site_digest))
    print(f"{len(medals)} medals, digest {site_digest[:12]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
