#!/usr/bin/env python3
"""Puts the Xbox's menu colors on the menus' high-res redraws
(port/assets/menus/ce/shell/bitmaps), which the Chupathingy menus draw:

    python3 tools/xbox_menu_colors.py --ui-map .../maps/ui.map [--check]

The redraws were drawn from Halo PC's bitmaps, and a few kept Gearbox's
tint where the Xbox's ui.map has its own (the rows' backgrounds,
option_bkds, a dark teal where the Xbox's are navy). For each redraw whose
picture the Xbox's map has under the same name, the redraw is shrunk to
the Xbox's size and compared with it, texel for texel; where their colors
differ by more than TOLERANCE in a channel, each channel gets the gain and
offset that best take the redraw's colors to the Xbox's (least squares,
weighted by coverage), applied to the redraw at its own size. Its alpha is
left as it is. No color is chosen by hand: they all come from your copy of
the Xbox's ui.map, which is only read (none of its texels are committed; the
redraws, recolored, are).

--check: only says which redraws differ, and by how much. Needs Pillow and
NumPy (and tools/hud_assets.py, which reads the Xbox's map).
"""

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hud_assets import XboxMap, decode_bitmap  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
REDRAWS = ROOT / "port/assets/menus/ce/shell/bitmaps"
XBOX_FOLDER = "ui\\shell\\bitmaps\\"
# (a channel's mean, 0 to 255, may differ this much: the redraws' finer
# edges against the Xbox's blurred ones)
TOLERANCE = 2.0
# the Chupathingy menus' own pictures made from a redraw (flipped or cut),
# given that redraw's colors in the same way
COPIES = {ROOT / "port/assets/menus/chupathingy/settings/list_item_bkd_bottom__1.png": "list_item_bkd__1"}


def mean_color(rgba: np.ndarray) -> np.ndarray:
    """The color of what a picture covers (weighted by its alpha)."""
    weight = rgba[..., 3] / 255.0
    if weight.sum() < 1.0:
        return np.zeros(3)
    return (rgba[..., :3] * weight[..., None]).sum((0, 1)) / weight.sum()


def fit(redraw: np.ndarray, xbox: np.ndarray) -> list:
    """Each channel's gain and offset taking the redraw's colors (shrunk to
    the Xbox's size) to the Xbox's, where both cover"""
    weight = np.minimum(redraw[..., 3], xbox[..., 3]).reshape(-1) / 255.0
    result = []
    for channel in range(3):
        source = redraw[..., channel].reshape(-1)
        target = xbox[..., channel].reshape(-1)
        matrix = np.stack([source, np.ones_like(source)], 1) * weight[:, None]
        solution, *_ = np.linalg.lstsq(matrix, target * weight, rcond=None)
        gain, offset = solution
        # (a flat channel, all one value, has no gain to fit: the offset alone)
        covered = source[weight > 0.05]
        if covered.size == 0 or np.ptp(covered) < 1.0:
            gain, offset = 1.0, 0.0
        # (then the offset that brings its mean to the Xbox's, as the
        # comparison measures it: the fit weighs the texels both cover, the
        # mean every texel the redraw covers)
        moved = redraw.copy()
        moved[..., channel] = moved[..., channel] * gain + offset
        offset += float(mean_color(xbox)[channel] - mean_color(moved)[channel])
        result.append((float(gain), float(offset)))
    return result


def recolor(path: Path, gains: list) -> None:
    """A picture's channels through gains (gain, offset each), its alpha kept"""
    redraw = np.array(Image.open(path).convert("RGBA")).astype(float)
    for channel, (gain, offset) in enumerate(gains):
        redraw[..., channel] = redraw[..., channel] * gain + offset
    Image.fromarray(np.clip(np.rint(redraw), 0, 255).astype(np.uint8)).save(path, optimize=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--ui-map", type=Path, required=True, help="the Xbox's ui.map (the player's)")
    parser.add_argument("--check", action="store_true", help="only list the redraws that differ")
    arguments = parser.parse_args()

    xbox_map = XboxMap(arguments.ui_map)
    names = {name[len(XBOX_FOLDER):]: name for (group, name) in xbox_map.tags
             if group == "bitm" and name.startswith(XBOX_FOLDER)}
    changed = 0
    fits = {}
    for path in sorted(REDRAWS.glob("*__*.png")):
        base, frame = path.stem.rsplit("__", 1)
        if base not in names:
            continue
        bitmaps = xbox_map.bitmap_group(names[base])["bitmaps"]
        if int(frame) >= len(bitmaps):
            continue
        xbox = decode_bitmap(bitmaps[int(frame)]).astype(float)
        image = Image.open(path).convert("RGBA")
        small = np.array(image.resize((xbox.shape[1], xbox.shape[0]), Image.BOX)).astype(float)
        difference = np.abs(mean_color(small) - mean_color(xbox))
        if difference.max() <= TOLERANCE:
            continue
        print(f"{path.name}: redraw {tuple(int(v) for v in mean_color(small))}, "
              f"Xbox {tuple(int(v) for v in mean_color(xbox))}")
        changed += 1
        if arguments.check:
            continue
        fits[path.stem] = fit(small, xbox)
        recolor(path, fits[path.stem])
    for path, source in COPIES.items():
        if source in fits:
            print(f"{path.name}: as {source}")
            recolor(path, fits[source])
    print(f"{changed} redraws {'differ' if arguments.check else 'recolored'}")
    return 1 if arguments.check and changed else 0


if __name__ == "__main__":
    sys.exit(main())
