#!/usr/bin/env python3
"""The game list pages' pictures, from the game's own ui.map (the menus'
bitmaps, server/map_bitmaps.py): map screenshots and thumbnails, the game
types' icons, the Spartan in each armor color, the Halo logo, the
controller's buttons.

  server/site_art.py assets/maps/ui.map server/web/art

The pictures are the game's, so they are made from a copy of the game
rather than kept in the repository (server/web/art is ignored)."""

import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from map_bitmaps import CacheFile, image  # noqa: E402

# the menus' order (ui_widget_game_data_input_functions.c,
# multiplayer_game_set_bitmap_for_map): mp_map_grafix's bitmaps
MAP_ORDER = ["beavercreek", "sidewinder", "damnation", "ratrace", "prisoner", "hangemhigh", "chillout",
             "carousel", "boardingaction", "bloodgulch", "wizard", "putput", "longest", "unknown"]
# mp_map_ui's thumbnails, by level
THUMBNAILS = {"beavercreek": "beaver_creek", "bloodgulch": "bloodgulch", "boardingaction": "boardingaction",
              "carousel": "carousel", "chillout": "chillout", "hangemhigh": "hangemhigh", "prisoner": "prisoner",
              "ratrace": "ratrace", "sidewinder": "sidewinder", "wizard": "wizard"}
# game_type_grafix's bitmaps
GAME_TYPES = ["ctf", "king", "slayer", "oddball", "race", "unknown"]
# colors_sm and player_color_marine_large: the profile colors (0 to 17),
# then the unknown player
COLOR_COUNT = 18


def bitmaps(cache: CacheFile, name: str):
    for tag, base in cache.bitmap_tags():
        if tag == name:
            return [image(cache, bitmap) for bitmap in cache.bitmaps(base)]
    raise KeyError(name)


def trim(picture: Image.Image) -> Image.Image:
    """the picture without its transparent border"""
    box = picture.getchannel("A").getbbox()
    return picture.crop(box) if box else picture


def main():
    cache = CacheFile(Path(sys.argv[1]))
    out = Path(sys.argv[2])
    for folder in ("maps", "thumbs", "types", "spartans", "spartans/large", "buttons"):
        (out / folder).mkdir(parents=True, exist_ok=True)

    for name, picture in zip(MAP_ORDER, bitmaps(cache, "ui\\shell\\bitmaps\\mp_map_grafix")):
        # (the screenshot fills the bitmap's left part; the rest is black)
        box = picture.convert("RGB").getbbox()
        picture.convert("RGB").crop(box).save(out / "maps" / f"{name}.jpg", quality=90)
    for level, tag in THUMBNAILS.items():
        bitmaps(cache, f"ui\\mp_map_ui\\{tag}")[0].convert("RGB").save(out / "thumbs" / f"{level}.jpg", quality=90)
    for name, picture in zip(GAME_TYPES, bitmaps(cache, "ui\\shell\\bitmaps\\game_type_grafix")):
        box = picture.getchannel("A").getbbox()
        (picture.crop(box) if box else picture).save(out / "types" / f"{name}.png")
    small = bitmaps(cache, "ui\\shell\\bitmaps\\colors_sm")
    large = bitmaps(cache, "ui\\shell\\main_menu\\settings_select\\player_setup\\player_profile_edit\\color_edit\\"
                           "player_color_marine_large")
    for index in range(COLOR_COUNT + 1):
        name = str(index) if index < COLOR_COUNT else "unknown"
        trim(small[index]).save(out / "spartans" / f"{name}.png")
        trim(large[index]).save(out / "spartans" / "large" / f"{name}.png")
    trim(bitmaps(cache, "ui\\shell\\main_menu\\halo_logo")[0]).save(out / "logo.png")
    for button in "abxy":
        bitmaps(cache, f"ui\\shell\\bitmaps\\{button}_butn")[0].save(out / "buttons" / f"{button}.png")
    print(f"wrote the pictures to {out}")


if __name__ == "__main__":
    main()
