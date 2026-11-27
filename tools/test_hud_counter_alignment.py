"""The Xbox maps' high-res ammo and grenade counters (#1822): their digits
and icons are drawn where halopc-restored's own sprites put them, as a Custom
Edition map with its resource maps draws them, not nudged toward the Xbox's
blurred glyphs (tools/hud_assets.py, SPRITE_ALIGNED)."""
import json
import sys
from pathlib import Path

import numpy as np
import pytest
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
HUD = ROOT / "port/assets/hud"
sys.path.insert(0, str(ROOT / "tools"))

import hud_assets  # noqa: E402


def sprite_cells():
    """(entry, cell) of each aligned sheet's cell that is a restored sprite's
    rectangle (from its top left, the Xbox's cell as large or larger) at the
    art's scale"""
    found = []
    for entry in json.loads((HUD / "layout.json").read_text())["assets"]:
        if entry["tag"] not in hud_assets.SPRITE_ALIGNED:
            continue
        for cell in entry["cells"]:
            left, top, right, bottom = cell["xbox"]
            clip, scale = cell["clip"], cell["source_scale"]
            if "art" in cell and clip[2] - clip[0] <= (right - left) * scale and \
                    clip[3] - clip[1] <= (bottom - top) * scale:
                found.append((entry, cell))
    return found


def test_the_counters_sheets_are_aligned():
    assert hud_assets.SPRITE_ALIGNED == {
        "ui\\hud\\bitmaps\\combined\\hud_counter_numbers",
        "ui\\hud\\bitmaps\\combined\\hud_ammo_type_icons",
    }
    names = {entry["name"] for entry, _ in sprite_cells()}
    # (the digits' sheet, and the four icon bitmaps the Xbox's counters draw)
    assert "hud_counter_numbers__0" in names
    assert {f"hud_ammo_type_icons__{index}" for index in range(5)} <= names
    assert len(sprite_cells()) >= 20


@pytest.mark.parametrize("entry, cell", sprite_cells(), ids=lambda value: str(value.get("xbox", value.get("name"))))
def test_cell_takes_its_sprite_whole(entry, cell):
    assert cell["source"] == cell["clip"][:2]
    # and the committed texture has the art there, unmoved
    scale = entry["scale"]
    assert scale == cell["source_scale"]
    art = np.asarray(Image.open(HUD / "art" / cell["art"]).convert("RGBA"))
    expected = hud_assets.recipe(hud_assets.window(
        hud_assets.clipped(art, cell["clip"]), cell["source"][0], cell["source"][1],
        (cell["xbox"][2] - cell["xbox"][0]) * scale, (cell["xbox"][3] - cell["xbox"][1]) * scale), cell["kind"])
    texture = np.asarray(Image.open(HUD / f"{entry['name']}.png").convert("RGBA"))
    left, top, right, bottom = (value * scale for value in cell["xbox"])
    assert np.array_equal(texture[top:bottom, left:right], expected), entry["name"]
