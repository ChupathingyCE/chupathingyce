#!/usr/bin/env python3
"""Embeds the high-res HUD textures (port/assets/hud, made by
tools/hud_assets.py), the menus' titles (port/assets/titles, made by
tools/title_assets.py), the fonts the text is drawn with
(port/assets/fonts), the menus' files (port/assets/menus, made by
tools/ce_menus.py), SMAA's shader and lookup textures
(port/third_party/smaa) and Chupathingy's loading picture (port/assets/loading, made by tools/make_loading_picture.py) in the
game as C data:

    python tools/embed_assets.py OUTPUT.c
    python tools/embed_assets.py --fonts OUTPUT.c
    python tools/embed_assets.py --fonts --prompt-fonts steam_deck,wiiu OUTPUT.c

writes OUTPUT.c with each PNG and the bitmap it stands for (its tag, index
and the checksum of its pixels, from port/assets/hud/layout.json and
port/assets/titles/titles.json), as port/linux/src/hud_hires.h declares
them, and the text's fonts, as port/linux/src/text_hires.h does; with
--fonts, the overlay's fonts (port/linux/ui/fonts, the game browser's), as
port/linux/src/posix_ui_font.c declares them. Some of the controllers'
prompt fonts are for a few devices (PROMPT_FONTS); --prompt-fonts names the
ones a build embeds (none: "none"), and the rest are left empty, their
buttons drawn as pills. Without it, a build embeds them all. The builds generate them
(hud_assets_build, called by tools/linux_build.py, windows_build.py and
android_build.py), so the PNGs are the committed source and Android needs
no files beside its guest image.

The data are 32-bit words, not bytes: the Android build passes the guest's
assembly through tools/android_asm_convert.py, which rewrites identifiers in
operands (it keeps .ascii strings as they are, but once garbled them).
"""

import json
import struct
import zlib
import sys
from pathlib import Path
from typing import Any, List, Optional

ROOT = Path(__file__).resolve().parent.parent
HUD_ASSETS = Path("port/assets/hud")
LAYOUT = HUD_ASSETS / "layout.json"
TITLE_ASSETS = Path("port/assets/titles")
TITLE_LIST = TITLE_ASSETS / "titles.json"
# the other set of titles, display.title_font = "cyborg" (tools/title_assets.py
# --set cyborg), with the menus' files of that set (ce/port_cyborg): only in
# the builds with HALO_TITLE_CHOICE (halo_console_features.h), so not on the
# consoles
TITLE_CYBORG_ASSETS = Path("port/assets/titles_cyborg")
TITLE_CYBORG_LIST = TITLE_CYBORG_ASSETS / "titles.json"
MENU_CYBORG_PREFIX = "ce/port_cyborg/"
FONT_ASSETS = Path("port/assets/fonts")
FONT_LIST = FONT_ASSETS / "fonts.json"
MENU_ASSETS = Path("port/assets/menus")
MENU_LIST = MENU_ASSETS / "menus.json"
# SMAA's files and the names port/linux/src/xgpu_post.c declares them by
SMAA_ASSETS = Path("port/third_party/smaa")
SMAA_FILES = (("SMAA.hlsl", "xgpu_smaa_shader"), ("area_tex.zlib", "xgpu_smaa_area_texture"),
              ("search_tex.zlib", "xgpu_smaa_search_texture"))
# the stock maps' shader key list (port/linux/src/xgpu_shader_keys.c; made
# by tools/shader_keys.py)
SHADER_KEYS_FILE = Path("port/assets/shaders/stock.keys")
# Chupathingy's loading picture (port/linux/game/loading_picture.c)
LOADING_PICTURE_FILE = Path("port/assets/loading/chupathingy_loading.tga")
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
# the overlay's fonts (the game browser's; posix_ui_font.c), in its order
UI_FONTS = Path("port/linux/ui/fonts")
UI_FONT_FILES = [UI_FONTS / name for name in ("NotoSans-Regular.ttf", "NotoSans-Bold.ttf", "input_xbox.ttf",
                                               "input_playstation.ttf", "input_nintendo.ttf", "input_keyboard.ttf")]


def role_font(role: str) -> str:
    """The font file fonts.json gives the role ("large" or "small") first."""
    for font in json.loads((ROOT / FONT_LIST).read_text())["fonts"]:
        if font.get("role", "small") == role:
            return font["file"]
    raise SystemExit(f"{FONT_LIST}: no font for the {role} role")


# (then the menus' faces, which the text's fonts are too, by their roles in
# fonts.json: Online Games is drawn in them; then the titles' in each of
# display.title_font's sets, as the menus' title pictures are set:
# Chupathingy Titles, and the large role's font)
UI_FONT_FILES += [FONT_ASSETS / role_font("large"), FONT_ASSETS / role_font("small"),
                  FONT_ASSETS / "ChupathingyTitles-Regular.ttf", FONT_ASSETS / role_font("large")]
# (then the scoreboard's platform logos)
UI_FONT_FILES += [UI_FONTS / "platform_icons.ttf"]
# (then the prompt fonts of a few devices, by the names --prompt-fonts takes:
# the Steam Deck's, the Steam Controller's and the Wii U's, as ui_font.h has
# them)
PROMPT_FONTS = {"steam_deck": UI_FONTS / "input_steam_deck.ttf",
                "steam_controller": UI_FONTS / "input_steam_controller.ttf",
                "wiiu": UI_FONTS / "input_wiiu.ttf"}
UI_FONT_FILES += list(PROMPT_FONTS.values())
# the controller's button icons the overlay's prompts draw (port/assets/prompts,
# tools/prompt_assets.py), in ui_overlay.c's order: A, B, X, Y, Start, LT, RT,
# LB, RB, Back
PROMPT_ICONS = [Path("port/assets/prompts") / f"{name}.png" for name in (
    "button-a", "button-b", "button-x", "button-y", "button-start", "trigger-left", "trigger-right", "bumper-left",
    "bumper-right", "button-back")]
# the medals' icons (port/assets/medals, tools/medal_assets.py from the site's
# medals): the 64-pixel atlas, in the builds with them (HALO_MEDAL_ICONS:
# halo_console_features.h). The 32-pixel one is kept for a console later
MEDAL_ATLAS = Path("port/assets/medals/medals-64.png")
MEDAL_ATLAS_CELL = 64


def font_files() -> List[str]:
    """The font files fonts.json uses, each once."""
    if not (ROOT / FONT_LIST).is_file():
        return []
    fonts = json.loads((ROOT / FONT_LIST).read_text())["fonts"]
    return sorted({font["file"] for font in fonts})


def textures() -> List[tuple]:
    """The textures: each one's folder, its entry in its list, and whether
    it is a title (0: the HUD's, 1: a classic title, 2: a cyborg one)."""
    result = []
    for folder, listing, title in ((HUD_ASSETS, LAYOUT, 0), (TITLE_ASSETS, TITLE_LIST, 1),
                                   (TITLE_CYBORG_ASSETS, TITLE_CYBORG_LIST, 2)):
        if (ROOT / listing).is_file():
            result += [(folder, asset, title) for asset in json.loads((ROOT / listing).read_text())["assets"]]
    return result


def title_tool_table(hud: bool) -> List[str]:
    """The titles the title tool makes from the player's own ui.map
    (port/linux/src/title_tool.h): titles.json's "from_files" ones, with
    their text and where each letter starts (measured from the maps'
    pictures; no font's drawing)."""
    lines = ['#include "title_tool.h"', ""]
    entries = []
    if hud and (ROOT / TITLE_LIST).is_file():
        for asset in json.loads((ROOT / TITLE_LIST).read_text())["assets"]:
            if not asset.get("from_files"):
                continue
            index = len(entries)
            lefts = ", ".join(f"{float(left)!r}f" for left in asset["lefts"])
            lines.append(f"static const float title_lefts{index}[] = {{ {lefts} }};")
            tag = asset["tag"].replace("\\", "\\\\")
            text = asset["text"].replace("\\", "\\\\").replace('"', '\\"')
            entries.append(f'\t{{ "{tag}", {asset["bitmap"]}, 0x{asset["crc"]:08x}u, {asset["scale"]}, "{text}", '
                           f"title_lefts{index} }},")
    lines.append("const struct title_tool_title title_tool_titles[] =")
    lines.append("{")
    lines.extend(entries or ["\t{ 0 },"])
    lines.append("};")
    lines.append("const unsigned int title_tool_title_count = " +
                 ("sizeof(title_tool_titles) / sizeof(title_tool_titles[0]);" if entries else "0;"))
    lines.append("")
    return lines


def menu_files() -> List[str]:
    """The menus' files menus.json lists, relative to their folder."""
    if not (ROOT / MENU_LIST).is_file():
        return []
    return json.loads((ROOT / MENU_LIST).read_text())["files"]


def smaa_files() -> List[tuple]:
    """SMAA's files that the checkout has, with their symbols."""
    return [(name, symbol) for name, symbol in SMAA_FILES if (ROOT / SMAA_ASSETS / name).is_file()]


def hud_asset_inputs() -> List[Path]:
    """The files the generated source is made from."""
    inputs = [listing for listing in (LAYOUT, TITLE_LIST, TITLE_CYBORG_LIST, FONT_LIST, MENU_LIST)
              if (ROOT / listing).is_file()]
    return [*inputs, *(folder / f"{asset['name']}.png" for folder, asset, _ in textures()),
            *(FONT_ASSETS / name for name in font_files()), *(MENU_ASSETS / name for name in menu_files()),
            *(SMAA_ASSETS / name for name, _ in smaa_files()),
            *([LOADING_PICTURE_FILE] if (ROOT / LOADING_PICTURE_FILE).is_file() else []),
            *([SHADER_KEYS_FILE] if (ROOT / SHADER_KEYS_FILE).is_file() else [])]


def hud_configure_inputs() -> List[Path]:
    """What configure.py is run again for: the lists of assets (and the
    folders, for files added or removed), not each file, which a change of a
    list may rename or remove."""
    inputs = []
    for folder, listing in ((HUD_ASSETS, LAYOUT), (TITLE_ASSETS, TITLE_LIST), (TITLE_CYBORG_ASSETS, TITLE_CYBORG_LIST),
                            (FONT_ASSETS, FONT_LIST),
                            (MENU_ASSETS, MENU_LIST)):
        if (ROOT / listing).is_file():
            inputs += [folder, listing]
    if (ROOT / SMAA_ASSETS).is_dir():
        inputs.append(SMAA_ASSETS)
    return inputs


def words(data: bytes) -> List[str]:
    """data as lines of 32-bit words (padded with zeros)."""
    padded = data + b"\0" * (-len(data) % 4)
    values = struct.unpack(f"<{len(padded) // 4}I", padded)
    return ["\t" + ", ".join(f"W(0x{word:08x})" for word in values[start:start + 8]) + ","
            for start in range(0, len(values), 8)]


def hud_assets_build(n: Any, prefix: str, output: Path, hud: bool = True) -> List[Path]:
    """Emits the rule that generates output; returns [output]. It is made
    whatever assets the checkout has (with none, its tables are empty), so
    that the symbols the platform layer refers to are always defined. With
    hud False the high-res HUD's and titles' pictures are left out (their
    table is empty) and only the fonts are embedded."""
    inputs = hud_asset_inputs()
    n.rule(
        name=f"{prefix}_embed_assets",
        command="$python tools/embed_assets.py " + ("" if hud else "--no-hud ") + "$out",
        description=f"{prefix.upper()} EMBED $out",
    )
    n.build(outputs=output, rule=f"{prefix}_embed_assets", implicit=[Path("tools/embed_assets.py"), *inputs])
    return [output]


def ui_fonts_build(n: Any, prefix: str, output: Path, sln: Any, prompt_fonts: Optional[str] = None) -> List[Path]:
    """Emits the rule that generates the overlay's fonts' source (with the
    game browser); returns [output], or nothing. prompt_fonts: the few
    devices' prompt fonts the build embeds (--prompt-fonts; None: all)"""
    if not getattr(sln, "game_browser", False):
        return []
    n.rule(
        name=f"{prefix}_embed_fonts",
        command="$python tools/embed_assets.py --fonts "
        + (f"--prompt-fonts {prompt_fonts} " if prompt_fonts is not None else "") + "$out",
        description=f"{prefix.upper()} EMBED $out",
    )
    n.build(outputs=output, rule=f"{prefix}_embed_fonts",
            implicit=[Path("tools/embed_assets.py"), FONT_LIST, *UI_FONT_FILES, *PROMPT_ICONS,
                      MEDAL_ATLAS])
    return [output]


# The words hold the files' bytes in little-endian order; a big-endian
# machine (the Xbox 360) gets each one swapped, so the bytes stay the file's.
WORD_MACRO = [
    "#if defined(__BIG_ENDIAN__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)",
    "#define W(x) ((((x) & 0xffU) << 24) | (((x) & 0xff00U) << 8) | (((x) >> 8) & 0xff00U) | (((x) >> 24) & 0xffU))",
    "#else",
    "#define W(x) (x)",
    "#endif",
    "",
]


def write(output: Path, lines: List[str]) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(lines) + "\n"
    if not output.is_file() or output.read_text() != text:
        output.write_text(text)


def prompt_fonts(names: str) -> List[Path]:
    """The prompt fonts --prompt-fonts names (comma-separated; "none")."""
    chosen = [] if names == "none" else [name for name in names.split(",") if name]
    for name in chosen:
        if name not in PROMPT_FONTS:
            sys.exit(f"--prompt-fonts: no prompt font {name} (there are {', '.join(PROMPT_FONTS)})")
    return [PROMPT_FONTS[name] for name in chosen]


def overlay_fonts(output: Path, embedded: List[Path]) -> None:
    lines = [
        "/* generated by tools/embed_assets.py --fonts from port/linux/ui/fonts and port/assets/fonts: do not edit */",
        "",
        *WORD_MACRO,
    ]
    # (each file once: one face can be several of the overlay's fonts)
    unique = list(dict.fromkeys(UI_FONT_FILES))
    for index, name in enumerate(unique):
        lines.append(f"static const unsigned int font{index}[] = {{")
        # (a prompt font the build leaves out: no font, which posix_ui_font.c
        # finds it cannot read)
        if name in PROMPT_FONTS.values() and name not in embedded:
            lines.append("\t0,")
        else:
            lines.extend(words((ROOT / name).read_bytes()))
        lines.append("};")
        lines.append("")
    lines.append("/* (posix_ui_font.c's: each font's data, as words) */")
    lines.append(f"const unsigned int *const ui_font_files[{len(UI_FONT_FILES)}] =")
    lines.append("{")
    lines.append("\t" + ", ".join(f"font{unique.index(name)}" for name in UI_FONT_FILES) + ",")
    lines.append("};")
    lines.append("")
    # the prompts' icons, as their pixels (RGBA, rows from the top), each
    # square and of one size
    size = 0
    for index, path in enumerate(PROMPT_ICONS):
        width, height, pixels = png_rgba((ROOT / path).read_bytes(), str(path))
        if width != height or (size and width != size):
            sys.exit(f"{path}: the prompts' icons must be square and of one size")
        size = width
        lines.append(f"static const unsigned int icon{index}[] = {{")
        lines.extend(words(pixels))
        lines.append("};")
        lines.append("")
    lines.append("/* (ui_overlay.c's: the prompts' icons, each ui_prompt_icon_size square) */")
    lines.append(f"const int ui_prompt_icon_size = {size};")
    lines.append(f"const unsigned int *const ui_prompt_icons[{len(PROMPT_ICONS)}] =")
    lines.append("{")
    lines.append("\t" + ", ".join(f"icon{index}" for index in range(len(PROMPT_ICONS))) + ",")
    lines.append("};")
    lines.append("")
    # the medals' icons: one atlas, RGBA, rows from the top, a cell each
    # (medal_icons.h's order)
    width, height, pixels = png_rgba((ROOT / MEDAL_ATLAS).read_bytes(), str(MEDAL_ATLAS))
    lines.append("/* (ui_overlay.c's: the medals' atlas, ui_medal_atlas_cell pixels a medal) */")
    lines.append("#ifdef HALO_MEDAL_ICONS")
    lines.append("static const unsigned int medal_atlas[] = {")
    lines.extend(words(pixels))
    lines.append("};")
    lines.append(f"const int ui_medal_atlas_cell = {MEDAL_ATLAS_CELL}, ui_medal_atlas_width = {width}, "
                 f"ui_medal_atlas_height = {height};")
    lines.append("const unsigned int *const ui_medal_atlas = medal_atlas;")
    lines.append("#endif")
    write(output, lines)


def png_rgba(data: bytes, name: str) -> tuple:
    """An 8-bit RGBA, non-interlaced PNG's width, height and pixels (rows from
    the top, 4 bytes a pixel), its filters undone."""
    width, height = png_size(data, name)
    compressed = b""
    offset = 8
    while offset < len(data):
        length, kind = struct.unpack(">I4s", data[offset:offset + 8])
        if kind == b"IDAT":
            compressed += data[offset + 8:offset + 8 + length]
        offset += 12 + length
    raw = zlib.decompress(compressed)
    stride = width * 4
    pixels = bytearray(stride * height)
    previous = bytearray(stride)
    for row in range(height):
        kind = raw[row * (stride + 1)]
        line = bytearray(raw[row * (stride + 1) + 1:(row + 1) * (stride + 1)])
        for index in range(stride):
            left = line[index - 4] if index >= 4 else 0
            up = previous[index]
            corner = previous[index - 4] if index >= 4 else 0
            if kind == 1:
                line[index] = (line[index] + left) & 0xFF
            elif kind == 2:
                line[index] = (line[index] + up) & 0xFF
            elif kind == 3:
                line[index] = (line[index] + (left + up) // 2) & 0xFF
            elif kind == 4:
                guess = left + up - corner
                a, b, c = abs(guess - left), abs(guess - up), abs(guess - corner)
                line[index] = (line[index] + (left if a <= b and a <= c else up if b <= c else corner)) & 0xFF
        pixels[row * stride:(row + 1) * stride] = line
        previous = line
    return width, height, bytes(pixels)


def png_size(data: bytes, name: str) -> tuple:
    """The width and height of an 8-bit RGBA, non-interlaced PNG (the only
    kind port/linux/src/hud_hires.c reads)."""
    if data[:8] != PNG_SIGNATURE or data[12:16] != b"IHDR":
        sys.exit(f"{name}: not a PNG")
    width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", data[16:29])
    if depth != 8 or colour != 6 or interlace != 0:
        sys.exit(f"{name}: must be 8-bit RGBA and not interlaced")
    return width, height


def main() -> None:
    if len(sys.argv) == 3 and sys.argv[1] == "--fonts":
        overlay_fonts(Path(sys.argv[2]), list(PROMPT_FONTS.values()))
        return
    if len(sys.argv) == 5 and sys.argv[1] == "--fonts" and sys.argv[2] == "--prompt-fonts":
        overlay_fonts(Path(sys.argv[4]), prompt_fonts(sys.argv[3]))
        return
    hud = True
    if len(sys.argv) == 3 and sys.argv[1] == "--no-hud":
        hud = False
        del sys.argv[1]
    if len(sys.argv) != 2:
        sys.exit("usage: embed_assets.py [--fonts [--prompt-fonts NAMES] | --no-hud] OUTPUT.c")
    lines = [
        "/* generated by tools/embed_assets.py from port/assets: do not edit */",
        "",
        *WORD_MACRO,
        "",
        '#include "hud_hires.h"',
        "",
    ]
    table = []
    for index, (folder, asset, title) in enumerate(textures() if hud else []):
        name = f"{asset['name']}.png"
        data = (ROOT / folder / name).read_bytes()
        width, height = png_size(data, name)
        scale = asset["scale"]
        if (width, height) != (asset["width"] * scale, asset["height"] * scale):
            sys.exit(f"{name}: {width}x{height}, not {scale}x its bitmap's {asset['width']}x{asset['height']}")
        # (the cyborg titles only with HALO_TITLE_CHOICE)
        choice = ["#ifdef HALO_TITLE_CHOICE"] if title == 2 else []
        lines.extend(choice)
        lines.append(f"static const unsigned int asset{index}[] = {{")
        lines.extend(words(data))
        lines.append("};")
        lines.extend(["#endif"] if choice else [])
        lines.append("")
        tag = asset["tag"].replace("\\", "\\\\")
        coverage = int(any(cell["kind"] == "meter" for cell in asset.get("cells", [])))
        point_threshold = int(any(cell.get("thresholds") for cell in asset.get("cells", [])))
        table.extend(choice)
        table.append(f'\t{{ "{tag}", {asset["bitmap"]}, {width}, {height}, 0x{asset["crc"]:08x}u, {coverage}, '
                     f'{point_threshold}, {title}, asset{index}, {len(data)} }},')
        table.extend(["#endif"] if choice else [])
    lines.append("const struct hud_hires_embedded hud_hires_embedded[] =")
    lines.append("{")
    lines.extend(table)
    if not table:
        lines.append("\t{ 0 },")
    lines.append("};")
    # (counted, as entries are left out of some builds)
    lines.append("const unsigned int hud_hires_embedded_count = " +
                 ("sizeof(hud_hires_embedded) / sizeof(hud_hires_embedded[0]);" if table else "0;"))
    lines.append("")
    lines.extend(title_tool_table(hud))
    # the fonts, and which draws each font tag (text_hires.h)
    lines.append('#include "text_hires.h"')
    lines.append("")
    files = font_files()
    for index, name in enumerate(files):
        lines.append(f"static const unsigned int font{index}[] = {{")
        lines.extend(words((ROOT / FONT_ASSETS / name).read_bytes()))
        lines.append("};")
        lines.append("")
    fonts = json.loads((ROOT / FONT_LIST).read_text())["fonts"] if files else []
    lines.append("const struct text_hires_embedded text_hires_embedded[] =")
    lines.append("{")
    for font in fonts:
        index = files.index(font["file"])
        tag = font["tag"].replace("\\", "\\\\")
        size = (ROOT / FONT_ASSETS / font["file"]).stat().st_size
        role = font.get("role", "small")
        width = float(font.get("width", 1.0))
        lines.append(f'\t{{ "{tag}", "{font["file"]}", font{index}, {size}, "{role}", {width!r}f }},')
    if not fonts:
        lines.append("\t{ 0 },")
    lines.append("};")
    lines.append(f"const unsigned int text_hires_embedded_count = {len(fonts)};")
    lines.append("")
    # the menus' files (menu_files.h)
    lines.append('#include "menu_files.h"')
    lines.append("")
    menus = menu_files()
    for index, name in enumerate(menus):
        data = (ROOT / MENU_ASSETS / name).read_bytes()
        if name.endswith(".png"):
            png_size(data, name)
        # (the cyborg titles only with HALO_TITLE_CHOICE)
        choice = name.startswith(MENU_CYBORG_PREFIX)
        lines.extend(["#ifdef HALO_TITLE_CHOICE"] if choice else [])
        lines.append(f"static const unsigned int menu{index}[] = {{")
        lines.extend(words(data))
        lines.append("};")
        lines.extend(["#endif"] if choice else [])
        lines.append("")
    lines.append("const struct menu_file_embedded menu_files_embedded[] =")
    lines.append("{")
    for index, name in enumerate(menus):
        size = (ROOT / MENU_ASSETS / name).stat().st_size
        choice = name.startswith(MENU_CYBORG_PREFIX)
        lines.extend(["#ifdef HALO_TITLE_CHOICE"] if choice else [])
        lines.append(f'\t{{ "{name}", menu{index}, {size} }},')
        lines.extend(["#endif"] if choice else [])
    if not menus:
        lines.append("\t{ 0 },")
    lines.append("};")
    lines.append("const unsigned int menu_files_embedded_count = " +
                 ("sizeof(menu_files_embedded) / sizeof(menu_files_embedded[0]);" if menus else "0;"))
    lines.append("")
    # SMAA's shader, as text a GLSL compiler takes (ASCII, ending in a NUL),
    # and its lookup textures (xgpu_post.c); each of size 0 that the
    # checkout does not have. Android has no SMAA.
    lines.append("#ifndef HALO_ANDROID")
    present = dict(smaa_files())
    for name, symbol in SMAA_FILES:
        data = (ROOT / SMAA_ASSETS / name).read_bytes() if name in present else b""
        if data and name.endswith(".hlsl"):
            data = bytes(byte if byte < 0x80 else 0x20 for byte in data) + b"\0"
        lines.append("")
        lines.append(f"const unsigned int {symbol}[] = {{")
        lines.extend(words(data) if data else ["\t0,"])
        lines.append("};")
        lines.append(f"const unsigned long {symbol}_size = {len(data)};")
    lines.append("")
    lines.append("#endif")
    # Chupathingy's loading picture (loading_picture.c), of size 0 if the
    # checkout does not have it
    data = (ROOT / LOADING_PICTURE_FILE).read_bytes() if (ROOT / LOADING_PICTURE_FILE).is_file() else b""
    lines.append("")
    lines.append("const unsigned int loading_picture_data[] = {")
    lines.extend(words(data) if data else ["\t0,"])
    lines.append("};")
    lines.append(f"const unsigned long loading_picture_data_size = {len(data)};")
    # the stock maps' shader key list, deflated (xgpu_shader_keys.c), of size
    # 0 if the checkout does not have it; only where it is used
    text = (ROOT / SHADER_KEYS_FILE).read_bytes() if (ROOT / SHADER_KEYS_FILE).is_file() else b""
    data = zlib.compress(text, 9) if text else b""
    lines.append("")
    lines.append("#ifdef HALO_SHADER_PREP")
    lines.append("const unsigned int xgpu_shader_keys_stock_data[] = {")
    lines.extend(words(data) if data else ["\t0,"])
    lines.append("};")
    lines.append(f"const unsigned long xgpu_shader_keys_stock_data_size = {len(data)};")
    lines.append(f"const unsigned long xgpu_shader_keys_stock_text_size = {len(text)};")
    lines.append("#endif")
    write(Path(sys.argv[1]), lines)


if __name__ == "__main__":
    main()
