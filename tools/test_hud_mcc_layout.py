"""The MCC maps' stock HUD sheets' textures (port/assets/hud/mcc, made by
tools/hud_assets.py mcc-layout): each laid out as its MCC sheet, a whole
multiple of its size, for a sheet the Xbox maps' high-res HUD has too, with
its kept cells inside it and left empty for the map's own texels. A meter
sheet (mcc-meters) comes whole, every bitmap of it, and keeps no cells, so
a meter is never drawn half MCC's and half ours."""
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HUD = ROOT / "port/assets/hud"
METERS = {"ui\\hud\\bitmaps\\combined\\hud_unit_meters", "ui\\hud\\bitmaps\\combined\\hud_ammo_meters"}


def entries():
    return json.loads((HUD / "mcc_layout.json").read_text())["assets"]


def png(path):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n" and data[12:16] == b"IHDR", path
    width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", data[16:29])
    assert (depth, colour, interlace) == (8, 6, 0), f"{path.name}: must be 8-bit RGBA, not interlaced"
    return width, height


def test_sheets_match_their_textures():
    names = set()
    for entry in entries():
        width, height = png(HUD / "mcc" / f"{entry['name']}.png")
        assert entry["scale"] >= 1
        assert (width, height) == (entry["width"] * entry["scale"], entry["height"] * entry["scale"]), entry["name"]
        assert 0 <= entry["crc"] < 1 << 32
        assert entry["cells"], entry["name"]
        names.add(f"{entry['name']}.png")
    assert names == {path.name for path in (HUD / "mcc").glob("*.png")}, "a texture no sheet lists, or one missing"


def test_only_the_xbox_maps_sheets():
    xbox = {entry["tag"] for entry in json.loads((HUD / "layout.json").read_text())["assets"]}
    for entry in entries():
        assert entry["tag"] in xbox, entry["name"]


def test_meter_sheets_whole():
    # MCC's stock meter sheets: hud_unit_meters has four bitmaps, hud_ammo_meters two
    sizes = {"ui\\hud\\bitmaps\\combined\\hud_unit_meters": 4, "ui\\hud\\bitmaps\\combined\\hud_ammo_meters": 2}
    for tag, count in sizes.items():
        sheets = sorted(entry["bitmap"] for entry in entries() if entry["tag"] == tag)
        assert sheets == list(range(count)), f"{tag}: bitmaps {sheets}, not all {count}"
        for entry in entries():
            if entry["tag"] == tag:
                assert not entry.get("keep"), f"{entry['name']}: a meter keeps none of the map's own cells"
                assert all(cell["kind"] == "meter" for cell in entry["cells"]), entry["name"]
                assert entry["scale"] == 1, entry["name"]


def test_kept_cells_are_inside_and_empty():
    from PIL import Image

    for entry in entries():
        if not entry.get("keep"):
            continue
        image = Image.open(HUD / "mcc" / f"{entry['name']}.png").convert("RGBA")
        scale = entry["scale"]
        for left, top, right, bottom in entry["keep"]:
            assert 0 <= left < right <= entry["width"] and 0 <= top < bottom <= entry["height"], entry["name"]
            # (the game copies the map's texels there: hud_hires_mcc_keep)
            alpha = image.crop((left * scale, top * scale, right * scale, bottom * scale)).getchannel("A")
            assert alpha.getextrema()[1] == 0, f"{entry['name']}: kept cell {left, top, right, bottom} is drawn on"


def test_message_icons_keep_the_controller_buttons():
    # MCC's buttons are another controller's than the restored art's: every
    # message icons sheet keeps some cells as the map draws them
    sheets = [entry for entry in entries() if "msg_icons" in entry["tag"]]
    assert sheets and all(entry.get("keep") for entry in sheets)


def test_embedded_only_where_mcc_maps_are_played():
    source = (ROOT / "tools/embed_assets.py").read_text()
    assert '"#if HALO_MCC"' in source and "MCC_LAYOUT" in source


def test_counter_panels_stay_the_maps():
    # MCC's ammo and grenade counter panels are taller than the restored
    # art: drawn with it, they had a transparent strip along the bottom
    # (#1821), so they stay MCC's own
    names = {entry["name"] for entry in entries()}
    for index in range(3):
        assert f"hud_weapon_backgrounds__{index}" not in names


def test_counter_outlines_stay_the_maps():
    # the counter panels' outlines go with MCC's own panels: the restored
    # outlines have another shape, so the panel color ran past them (#1985)
    names = {entry["name"] for entry in entries()}
    for index in (0, 2, 3):
        assert f"hud_ammo_outlines__{index}" not in names
        assert not (ROOT / f"port/assets/hud/mcc/hud_ammo_outlines__{index}.png").exists()
