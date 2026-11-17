"""Draws the main menus' DELTA row (port/assets/menus/chupathingy/main_menu),
in the look of the other main menu rows: their letters (the redraws' glyphs,
port/assets/menus/svg/shell/main_menu and beside it), at their size and
place, dim blue, and white with the blue glow while chosen.

    python3 tools/delta_menu_art.py

Writes menu_delta__0.svg and menu_delta__1.svg, and the PNGs the menus
load (4x, as tools/ce_menus.py draws the others). Needs rsvg-convert.
The row itself is chupathingy/main_menu.xml's chupathingy/main_menu_delta,
which the Gearbox and Xbox main menus get too (menu_tags.c)."""

import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MENUS = ROOT / "port" / "assets" / "menus"
REDRAWS = MENUS / "svg" / "shell"
OUT = MENUS / "chupathingy" / "main_menu"
WORD = "DELTA"
# as the redraws place theirs: the letters' scale, the row's top, a gap
# after each letter, and the pairs set closer
SCALE = 0.9037
TOP = 6.57
GAP = 2.4
KERNING = {("L", "T"): -1.2, ("T", "A"): -1.8}
WIDTH, HEIGHT, ZOOM = 256, 64, 4


def glyphs(letters: str) -> dict:
    """each letter's path data, from the first redraw that has it"""
    found = {}
    for svg in sorted(REDRAWS.rglob("*.svg")):
        text = svg.read_text()
        for letter in letters:
            if letter not in found:
                match = re.search(rf'<path id="g{letter}" d="([^"]+)"/>', text)
                if match:
                    found[letter] = match.group(1)
    missing = set(letters) - set(found)
    if missing:
        raise SystemExit(f"no redraw has the letters {sorted(missing)}")
    return found


def path_width(data: str) -> float:
    """the widest x a path reaches (its letter's width, from 0)"""
    numbers = [float(x) for x in re.findall(r"-?\d+(?:\.\d+)?", data)]
    return max(numbers[0::2])


def placed(word: str, shapes: dict) -> list:
    """(letter, x) for each letter, the word centered in the row"""
    advances = []
    for index, letter in enumerate(word):
        advance = path_width(shapes[letter]) * SCALE
        if index + 1 < len(word):
            advance += GAP + KERNING.get((letter, word[index + 1]), 0.0)
        advances.append(advance)
    x = (WIDTH - sum(advances)) / 2
    places = []
    for letter, advance in zip(word, advances):
        places.append((letter, round(x, 2)))
        x += advance
    return places


def svg(word: str, chosen: bool) -> str:
    shapes = glyphs(word)
    uses = "\n".join(f'    <use href="#g{letter}" transform="translate({x},{TOP}) scale({SCALE})"/>'
                     for letter, x in placed(word, shapes))
    paths = "\n".join(f'    <path id="g{letter}" d="{shapes[letter]}"/>' for letter in sorted(set(word)))
    lines = [f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
             f'width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">', "  <defs>", paths]
    if chosen:
        lines += [f'    <filter id="glow" filterUnits="userSpaceOnUse" x="0" y="0" width="{WIDTH}" height="{HEIGHT}">',
                  '      <feGaussianBlur stdDeviation="2.19"/>', "    </filter>", "  </defs>",
                  f'  <!-- "{word}" (highlighted: white with blue glow) -->',
                  '  <g fill="#2895ff" stroke="#2895ff" stroke-width="6.3" stroke-linejoin="round" opacity="0.650" '
                  'filter="url(#glow)">', uses, "  </g>", '  <g id="text" fill="#fff">', uses, "  </g>"]
    else:
        lines += ["  </defs>", f'  <!-- "{word}" (normal state: dim blue) -->',
                  '  <g id="text" fill="#2895ff" fill-opacity="0.467">', uses, "  </g>"]
    return "\n".join(lines + ["</svg>", ""])


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for state in (0, 1):
        source = OUT / f"menu_delta__{state}.svg"
        source.write_text(svg(WORD, state == 1))
        png = OUT / f"menu_delta__{state}.png"
        subprocess.run(["rsvg-convert", "-z", str(ZOOM), "-o", str(png), str(source)], check=True)
        print(png.relative_to(ROOT))


if __name__ == "__main__":
    main()
