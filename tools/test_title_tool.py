"""Tests of the title tool (port/linux/src/title_tool.c), which makes the
menus' titles in Halo's own lettering on the player's machine from the
player's own ui.map (display.title_font = "halo"): nothing of that lettering
ships, as a font or as pictures.

The tool is built with its test program (port/linux/tests/title_tool_test.c)
and run on a small Xbox map made here, whose titles are drawn with Newtown
(port/assets/fonts, public domain) as the maps' are, on a dark plate, at
1x: the titles it makes must cover the same letters as Newtown drawn at
their size, and a title whose pixels are not the ones its letters were
measured on is not made. With HALO_DATA_ROOT set, it is run on that data
root's maps/ui.map too, which must give every "from_files" title."""

import json
import os
import shutil
import struct
import subprocess
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
ZLIB = ROOT / "port" / "third_party" / "zlib"
TITLES = ROOT / "port" / "assets" / "titles"
NEWTOWN = ROOT / "port" / "assets" / "fonts" / "Newtown-Regular.ttf"
TAG_BASE = 0x803A6000
PLATE = (0, 8, 24, 200)
TEXT = (40, 150, 255, 255)
SCALE = 4
CAP = 22


def compiler():
    """The first of clang, cc and gcc found, or None."""
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


@pytest.fixture(scope="module")
def tool(tmp_path_factory):
    """The tool's test program, built with warnings as errors."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path_factory.mktemp("title_tool") / "title_tool_test"
    sources = [ROOT / "port" / "linux" / "tests" / "title_tool_test.c", ROOT / "port" / "linux" / "src" / "title_tool.c",
               *(ZLIB / f"{name}.c" for name in ("adler32", "crc32", "inffast", "inflate", "inftrees", "zutil"))]
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-O2", "-DZ_PREFIX", f"-I{ZLIB}", *map(str, sources), "-lm",
         "-o", str(binary)], capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    # (the tool's own file with warnings as errors: zlib's are others')
    strict = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-DZ_PREFIX", f"-I{ZLIB}", "-c",
         str(ROOT / "port" / "linux" / "src" / "title_tool.c"), "-o", str(binary) + ".o"],
        capture_output=True, text=True)
    assert strict.returncode == 0, strict.stdout + strict.stderr
    return binary


def dxt3(image) -> bytes:
    """RGBA texels (NumPy, height by width by 4) as DXT3 blocks, row by row:
    each block's lightest and darkest colors as its ends."""
    import numpy as np

    height, width = image.shape[:2]
    out = bytearray()

    def pack(rgb):
        return (int(rgb[0]) * 31 // 255 << 11) | (int(rgb[1]) * 63 // 255 << 5) | (int(rgb[2]) * 31 // 255)

    def unpack(value):
        return np.array([((value >> 11) & 31) * 255 // 31, ((value >> 5) & 63) * 255 // 63, (value & 31) * 255 // 31])

    for by in range(0, height, 4):
        for bx in range(0, width, 4):
            block = image[by:by + 4, bx:bx + 4].reshape(16, 4).astype(int)
            alpha = 0
            for i, texel in enumerate(block):
                alpha |= (int(round(texel[3] / 17)) & 15) << (4 * i)
            light = block[:, :3].sum(axis=1)
            c0, c1 = pack(block[light.argmax(), :3]), pack(block[light.argmin(), :3])
            p0, p1 = unpack(c0), unpack(c1)
            palette = [p0, p1, (2 * p0 + p1) // 3, (p0 + 2 * p1) // 3]
            bits = 0
            for i, texel in enumerate(block):
                code = int(np.argmin([((texel[:3] - color) ** 2).sum() for color in palette]))
                bits |= code << (2 * i)
            out += struct.pack("<QHHI", alpha, c0, c1, bits)
    return bytes(out)


def draw_title(text: str, scale: int):
    """text drawn with Newtown on the plate (a 512 by 64 picture at scale,
    the text from texel 30, its capitals CAP texels tall on a baseline at
    texel 44), its coverage, and where each letter's ink starts (texels)."""
    import numpy as np
    from PIL import Image, ImageDraw, ImageFont

    over = 8 if scale == 1 else 2
    size = scale * over
    probe = ImageFont.truetype(str(NEWTOWN), 1000)
    _, top, _, bottom = probe.getbbox("H")
    font = ImageFont.truetype(str(NEWTOWN), round(CAP * size * 1000 / (bottom - top)))
    canvas = Image.new("L", (512 * size, 64 * size), 0)
    ImageDraw.Draw(canvas).text((30 * size, 44 * size), text, font=font, fill=255, anchor="ls")
    coverage = np.asarray(canvas.resize((512 * scale, 64 * scale), Image.BOX), float) / 255
    lefts, pen = [], 30 * size
    for index, character in enumerate(text):
        if character != " ":
            left = font.getbbox(character, anchor="ls")[0]
            lefts.append((pen + left) / size)
        pen += font.getlength(text[:index + 1]) - font.getlength(text[:index])
    return coverage, lefts


def picture(coverage):
    """The coverage as the maps' headers are: the text over a dark plate."""
    import numpy as np

    plate, text = np.array(PLATE, float), np.array(TEXT, float)
    return np.round(plate + (text - plate) * coverage[..., None]).astype(np.uint8)


def xbox_map(bitmaps: dict) -> bytes:
    """An Xbox cache file (its body zlib-compressed) holding the bitmaps
    (name: DXT3 blocks, each 512 by 64) as bitmap tags."""
    names = list(bitmaps)
    tags = bytearray(0x10 + 0x20 * len(names))
    struct.pack_into("<I", tags, 0, TAG_BASE + 0x10)
    struct.pack_into("<I", tags, 0xC, len(names))
    pixels = bytearray()
    entries = []
    for index, name in enumerate(names):
        name_address = TAG_BASE + len(tags)
        tags += name.encode() + b"\0"
        tags += b"\0" * (-len(tags) % 4)
        header_address = TAG_BASE + len(tags)
        tags += bytearray(0x6C)
        bitmap_address = TAG_BASE + len(tags)
        tags += bytearray(0x30)
        struct.pack_into("<II", tags, header_address - TAG_BASE + 0x60, 1, bitmap_address)
        entries.append((index, name_address, header_address, bitmap_address, len(pixels)))
        pixels += bitmaps[name]
    for index, name_address, header_address, bitmap_address, offset in entries:
        struct.pack_into("<4sIIIIIII", tags, 0x10 + index * 0x20, b"mtib", 0, 0, 0, name_address, header_address, 0, 0)
    tags_offset = 0x800
    pixels_offset = tags_offset + len(tags)
    for index, name_address, header_address, bitmap_address, offset in entries:
        at = bitmap_address - TAG_BASE
        struct.pack_into("<hhhhhH", tags, at + 4, 512, 64, 1, 0, 15, 0)
        struct.pack_into("<ii", tags, at + 0x18, pixels_offset + offset, len(bitmaps[names[index]]))
    body = bytes(tags) + bytes(pixels)
    header = bytearray(0x800)
    struct.pack_into("<4sIIIII", header, 0, b"daeh", 5, 0x800 + len(body), 0, tags_offset, len(tags))
    return bytes(header) + zlib.compress(body)


def run(tool, path: Path, lines: list, out: Path) -> dict:
    """The tool run on the map at path for the titles (tag, crc, text,
    lefts): each one made, by its index, as NumPy RGBA."""
    import numpy as np

    out.mkdir(exist_ok=True)
    spec = out / "titles.txt"
    spec.write_text("".join(f"{tag}\t0\t{crc}\t{SCALE}\t{text}\t{','.join(map(str, lefts))}\n"
                            for tag, crc, text, lefts in lines))
    result = subprocess.run([str(tool), str(path), str(spec), str(out)], capture_output=True, text=True, timeout=120)
    if result.returncode:
        return None
    made = {}
    for index in map(int, result.stdout.split()):
        raw = (out / f"{index}.rgba").read_bytes()
        width, height = struct.unpack("<II", raw[:8])
        made[index] = np.frombuffer(raw[8:], np.uint8).reshape(height, width, 4)
    return made


TEXTS = ["SELECT PROFILE", "PROFILE NAME", "SELECT MAP", "PLAYER OPTIONS", "EDIT PROFILE SETTINGS",
         "MULTIPLAYER COLOR", "LOAD LEVEL", "CONTROLLER SETUP"]


def test_titles_from_a_map(tool, tmp_path):
    """Each title is made, scale times its bitmap, its letters where the
    map's are (their ink, read by the text's color, covering what Newtown
    drawn at that size covers), and its plate kept around them."""
    import numpy as np

    bitmaps, lines = {}, []
    for index, text in enumerate(TEXTS):
        coverage, lefts = draw_title(text, 1)
        blocks = dxt3(picture(coverage))
        tag = f"ui\\shell\\test\\header_{index}"
        bitmaps[tag] = blocks
        lines.append((tag, zlib.crc32(blocks), text, lefts))
    (tmp_path / "ui.map").write_bytes(xbox_map(bitmaps))
    made = run(tool, tmp_path / "ui.map", lines, tmp_path / "out")
    assert made is not None and sorted(made) == list(range(len(TEXTS)))
    for index, text in enumerate(TEXTS):
        image = made[index]
        assert image.shape == (64 * SCALE, 512 * SCALE, 4)
        expected, _ = draw_title(text, SCALE)
        green = image[..., 1].astype(float)
        ours = (green - PLATE[1]) / (TEXT[1] - PLATE[1]) > 0.5
        theirs = expected > 0.5
        overlap = (ours & theirs).sum() / (ours | theirs).sum()
        assert overlap > 0.85, f"{text}: the letters cover {overlap:.2f} of Newtown's"
        # (the plate: dark and nearly opaque away from the text)
        assert abs(int(image[10, 10, 3]) - PLATE[3]) <= 8
        assert image[10, 10, :3].max() < 64


def test_other_pixels_are_not_used(tool, tmp_path):
    """A title whose pixels are not the ones its letters were measured on
    (another language's maps, or a modified ui.map) is not made; a file
    that is not a map gives nothing at all."""
    coverage, lefts = draw_title("SELECT MAP", 1)
    blocks = dxt3(picture(coverage))
    path = tmp_path / "ui.map"
    path.write_bytes(xbox_map({"ui\\a\\header_a": blocks, "ui\\a\\header_b": blocks}))
    made = run(tool, path, [("ui\\a\\header_a", zlib.crc32(blocks), "SELECT MAP", lefts),
                            ("ui\\a\\header_b", zlib.crc32(blocks) ^ 1, "SELECT MAP", lefts),
                            ("ui\\a\\header_c", zlib.crc32(blocks), "SELECT MAP", lefts)], tmp_path / "out")
    assert made is not None and sorted(made) == [0]
    (tmp_path / "other.map").write_bytes(b"not a map" * 400)
    assert run(tool, tmp_path / "other.map", [("ui\\a\\header_a", 0, "SELECT MAP", lefts)], tmp_path / "out2") is None


def test_no_halo_lettering_ships():
    """The titles in Halo's own lettering are made, never shipped: the
    shipped sets are the classic one (Chupathingy Titles) and Cyborg, and
    the tool's titles are the screens' headers, each one a classic title."""
    folders = sorted(path.name for path in (ROOT / "port" / "assets").glob("titles*") if path.is_dir())
    assert folders == ["titles", "titles_cyborg"]
    listing = json.loads((TITLES / "titles.json").read_text())
    assert listing["font"] == "ChupathingyTitles-Regular.ttf"
    from_files = [asset for asset in listing["assets"] if asset.get("from_files")]
    assert len(from_files) == 26
    assert all(asset["tag"].split("\\")[-1].startswith("header_") for asset in from_files)
    assert all(len(asset["lefts"]) == len(asset["text"].replace(" ", "")) for asset in from_files)


def test_embedded_table(tmp_path):
    """tools/embed_assets.py gives the game the tool's titles: their text
    and letters, no pictures."""
    import importlib.util

    spec = importlib.util.spec_from_file_location("embed_assets", ROOT / "tools" / "embed_assets.py")
    embed_assets = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(embed_assets)
    lines = embed_assets.title_tool_table(True)
    assert "const unsigned int title_tool_title_count = sizeof(title_tool_titles) / sizeof(title_tool_titles[0]);" \
        in lines
    assert sum(line.startswith('\t{ "ui\\\\shell') for line in lines) == 26
    assert embed_assets.title_tool_table(False)[-2] == "const unsigned int title_tool_title_count = 0;"


def test_real_ui_map(tool, tmp_path):
    """With HALO_DATA_ROOT, its maps/ui.map gives every one of the tool's
    titles."""
    root = os.environ.get("HALO_DATA_ROOT")
    path = Path(root) / "maps" / "ui.map" if root else None
    if not path or not path.is_file():
        pytest.skip("no HALO_DATA_ROOT with maps/ui.map")
    assets = [asset for asset in json.loads((TITLES / "titles.json").read_text())["assets"] if asset.get("from_files")]
    out = tmp_path / "out"
    out.mkdir()
    spec = out / "titles.txt"
    spec.write_text("".join(f"{a['tag']}\t{a['bitmap']}\t{a['crc']}\t{a['scale']}\t{a['text']}\t"
                            f"{','.join(map(str, a['lefts']))}\n" for a in assets))
    result = subprocess.run([str(tool), str(path), str(spec), str(out)], capture_output=True, text=True, timeout=300)
    assert result.returncode == 0
    assert sorted(map(int, result.stdout.split())) == list(range(len(assets)))
