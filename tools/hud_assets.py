#!/usr/bin/env python3
"""Makes the high-res HUD textures (port/assets/hud/*.png) from Aerocatia's and
Jessie's high-resolution HUD art in Halo PC Restored (release r220: its tags'
HUD sheets, and the sniper rifle's scope ladder, which its weapon's folder
holds), laid out as the Xbox maps' sheets:

    python tools/hud_assets.py layout --map assets/maps/bloodgulch.map \\
        --hek ../halopc-restored --svg port/assets/hud/svg
    python tools/hud_assets.py build
    python tools/hud_assets.py thresholds  # update only discrete segment data
    python tools/hud_assets.py check --map assets/maps/bloodgulch.map --out /tmp/hud_check

The game draws a HUD bitmap at its tag's size and samples it with normalised
coordinates, so a texture of a whole multiple of its size in the same layout
draws in its place unchanged (port/linux/src/hud_hires.c swaps it in as the
bitmap is uploaded). The restored sheets are mostly 4x the Xbox bitmaps (a few at
their size), and the PC tags have more sprites than the Xbox ones and place
some elsewhere, so each Xbox sprite (or bitmap) is copied from the restored
sprite whose shape matches it best, aligned by its content. A texture is
made at the restored art's own scale (4x, mostly).

The restored sheets with English text (the message icons, the reticle's
warnings, the vehicles' seat plates, the sniper's captions) are used as
the others are: a map in another language has other pixels there, which the
game's checksum tells apart, so it keeps its own. Where the restored art
says something else than the Xbox's, it is edited to say what the Xbox's
does (the sniper's 10x: ten_times_label).

Where the restored art is no larger than the Xbox bitmap (the radar's blips
and sweeper, the health meter's base), an older SVG redraw of the PC sheet
is drawn instead, at 8x (--svg: the ones still used are kept in
port/assets/hud/svg).

layout: reads the Xbox map's HUD bitmaps (sizes, formats, sprite rectangles,
    pixels) and the restored (HEK) tags' sprites and pixels, matches and
    aligns each cell, and writes port/assets/hud/layout.json (numbers only),
    with the restored sheets used, as PNGs, in port/assets/hud/art (and the
    SVG redraws used in port/assets/hud/svg). Needs a map and the restored
    tags; its output is committed. A bitmap with a cell that nothing
    matches is left out.
build: draws port/assets/hud/art and svg with layout.json into
    port/assets/hud/*.png (committed; the builds embed them).
check: compares each PNG, reduced to the tag's size, with the map's bitmap
    and writes side-by-side images into --out.

Needs rsvg-convert, Pillow, NumPy and SciPy.
"""

import argparse
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "port/assets/hud"
LAYOUT = ASSETS / "layout.json"
# the textures' size over the Xbox bitmaps' where an SVG redraw is drawn (the
# restored art's are its own), but no side longer than every OpenGL ES 3
# device can take
SCALE = 8
MAXIMUM_SIZE = 2048

# the Xbox maps' tag data is loaded here (cache_files.c)
XBOX_TAG_BASE = 0x803A6000
BITMAP_LINEAR_FLAG = 0x10
FORMATS = {0: "a8", 1: "y8", 2: "ay8", 3: "a8y8", 6: "r5g6b5", 8: "a1r5g5b5", 9: "a4r4g4b4",
           10: "x8r8g8b8", 11: "a8r8g8b8", 14: "dxt1", 15: "dxt3", 16: "dxt5"}

HUD = "ui\\hud\\bitmaps\\"
# the folders of the HUD's bitmaps: the HUD's own, and the sniper rifle's
# (its scope's elevation ladder)
FOLDERS = (HUD, "weapons\\sniper rifle\\bitmaps\\")
# the meter sheets: their restored art in the Xbox channel order (the fill
# threshold the meter shader reads in the grey, the art in the alpha)
METERS = {
    HUD + "combined\\hud_unit_meters": ("hud_xbox_order/bitmaps/combined/hud_unit_meters",
                                       "extra/xbox_order_hud_meters/tags/ui/hud/bitmaps/combined/hud_unit_meters.bitmap"),
    HUD + "combined\\hud_ammo_meters": ("hud_xbox_order/bitmaps/combined/hud_ammo_meters",
                                       "extra/xbox_order_hud_meters/tags/ui/hud/bitmaps/combined/hud_ammo_meters.bitmap"),
}
# restored sheets (and redraws) not used, by name: a reticle the Xbox has
# none of, and the meters in the PC channel order. (Those with English text
# are used: a map in another language has other pixels there, which the
# game's checksum tells apart, and keeps its own.)
UNUSED = {
    "hud/bitmaps/combined/hud_reticles__13",
    "hud/bitmaps/combined/hud_unit_meters",
    "hud/bitmaps/combined/hud_ammo_meters",
}
# how each restored sheet's (or redraw's) texels are written, by its PC bitmap's format (as the
# Xbox decodes that format: xbox_textures.c, convert_texel): AY8 is its one
# value in every channel (the HUD blends premultiplied), A8Y8 grey and alpha,
# A8 white with alpha, Y8 grey and opaque, colour formats as drawn; DXT ones
# as drawn with the colour carried into the transparent texels, as theirs is
KINDS = {0: "a8", 1: "y8", 2: "alpha", 3: "grey", 6: "colour", 8: "colour", 9: "colour", 10: "colour", 11: "colour",
         14: "dxt", 15: "dxt", 16: "dxt"}

# how far (in Xbox texels) a redrawn cell may sit from its Xbox one, and how
# much it must overlap it (the overlap of their shapes over their union: thin
# and small shapes, drawn cleaner than the Xbox's blurred texels, overlap
# little even where they match)
SEARCH = 4
MATCH = 0.25
# where a cell's art goes where its shape does not say (bitmap, Xbox cell:
# the corner on the restored sheet): the shotgun's ammo meter, whose
# restored art is narrower than its Xbox cell, left-aligned as the Xbox's is
CORNERS = {
    ("hud_ammo_meters__0", (0, 27, 130, 45)): [-2, 108],
    ("hud_ammo_meters__0", (0, 78, 130, 88)): [-5, 312],
}


# ---------- Xbox maps


class XboxMap:
    """The tags of an Xbox cache file (its body after the 2 KB header is
    zlib-compressed on the disc)."""

    def __init__(self, path: Path):
        raw = path.read_bytes()
        self.data = raw[:0x800] + zlib.decompressobj().decompress(raw[0x800:])
        offset, size = struct.unpack_from("<ii", self.data, 0x10)
        self.tags_data = self.data[offset:offset + size]
        instances, = struct.unpack_from("<I", self.tags_data, 0)
        count, = struct.unpack_from("<I", self.tags_data, 0xC)
        self.tags = {}
        for index in range(count):
            group, _, _, _, name, address, _, _ = struct.unpack("<4sIIIIIII", self.read(instances + index * 0x20, 0x20))
            name = self.read(name, 256).split(b"\0")[0].decode()
            self.tags[(group[::-1].decode(), name)] = address

    def read(self, address: int, size: int) -> bytes:
        offset = address - XBOX_TAG_BASE
        return self.tags_data[offset:offset + size]

    def bitmap_group(self, name: str) -> dict:
        address = self.tags[("bitm", name)]
        header = self.read(address, 0x6C)
        sequence_count, sequences = struct.unpack_from("<II", header, 0x54)
        bitmap_count, bitmaps = struct.unpack_from("<II", header, 0x60)
        group = {"bitmaps": [], "sequences": []}
        for index in range(bitmap_count):
            bitmap = self.read(bitmaps + index * 0x30, 0x30)
            width, height, _, _, format, flags = struct.unpack_from("<hhhhhH", bitmap, 4)
            offset, size = struct.unpack_from("<ii", bitmap, 0x18)
            group["bitmaps"].append({"width": width, "height": height, "format": format, "flags": flags,
                                     "pixels": self.data[offset:offset + size]})
        for index in range(sequence_count):
            sequence = self.read(sequences + index * 0x40, 0x40)
            sprite_count, sprites = struct.unpack_from("<II", sequence, 0x34)
            group["sequences"].append([sprite_from(self.read(sprites + j * 0x20, 0x20), "<") for j in range(sprite_count)])
        return group


def sprite_from(data: bytes, order: str) -> tuple:
    bitmap, = struct.unpack_from(order + "h", data, 0)
    left, right, top, bottom = struct.unpack_from(order + "4f", data, 8)
    return bitmap, left, top, right, bottom


def morton_order(width: int, height: int) -> np.ndarray:
    """Each texel's index in the Xbox's swizzled order: x and y bits
    interleaved, x first, the longer side's leftover bits on top."""
    xs, ys = np.meshgrid(np.arange(width), np.arange(height))
    index = np.zeros((height, width), np.int64)
    bit = shift_x = shift_y = 0
    x_bits, y_bits = width.bit_length() - 1, height.bit_length() - 1
    while shift_x < x_bits or shift_y < y_bits:
        if shift_x < x_bits:
            index |= ((xs >> shift_x) & 1) << bit
            bit += 1
            shift_x += 1
        if shift_y < y_bits:
            index |= ((ys >> shift_y) & 1) << bit
            bit += 1
            shift_y += 1
    return index


def level0_size(bitmap: dict) -> int:
    """The bytes of a bitmap's first mip level."""
    width, height, kind = bitmap["width"], bitmap["height"], FORMATS[bitmap["format"]]
    if kind.startswith("dxt"):
        return max(1, width // 4) * max(1, height // 4) * (8 if kind == "dxt1" else 16)
    texel = {"a8": 1, "y8": 1, "ay8": 1, "a8y8": 2, "r5g6b5": 2, "a1r5g5b5": 2, "a4r4g4b4": 2}.get(kind, 4)
    return width * height * texel


def decode_dxt(kind: str, data: bytes, width: int, height: int) -> np.ndarray:
    """DXT blocks (row by row, not swizzled) as RGBA."""
    image = np.zeros((height, width, 4), np.uint8)
    block_size = 8 if kind == "dxt1" else 16
    blocks_wide = max(1, width // 4)
    for block in range(max(1, width // 4) * max(1, height // 4)):
        bx, by = block % blocks_wide * 4, block // blocks_wide * 4
        chunk = data[block * block_size:(block + 1) * block_size]
        colour = chunk[-8:]
        c0, c1, bits = struct.unpack("<HHI", colour)
        def rgb(value):
            return [((value >> 11) & 31) * 255 // 31, ((value >> 5) & 63) * 255 // 63, (value & 31) * 255 // 31]
        palette = [rgb(c0) + [255], rgb(c1) + [255]]
        if kind != "dxt1" or c0 > c1:
            palette += [[(2 * a + b) // 3 for a, b in zip(palette[0][:3], palette[1][:3])] + [255],
                        [(a + 2 * b) // 3 for a, b in zip(palette[0][:3], palette[1][:3])] + [255]]
        else:
            palette += [[(a + b) // 2 for a, b in zip(palette[0][:3], palette[1][:3])] + [255], [0, 0, 0, 0]]
        if kind == "dxt3":
            alphas = [((int.from_bytes(chunk[:8], "little") >> (4 * i)) & 15) * 17 for i in range(16)]
        elif kind == "dxt5":
            a0, a1 = chunk[0], chunk[1]
            table = [a0, a1] + ([((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)] if a0 > a1 else
                                [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255])
            codes = int.from_bytes(chunk[2:8], "little")
            alphas = [table[(codes >> (3 * i)) & 7] for i in range(16)]
        else:
            alphas = None
        for i in range(16):
            x, y = bx + i % 4, by + i // 4
            if x < width and y < height:
                texel = list(palette[(bits >> (2 * i)) & 3])
                if alphas is not None:
                    texel[3] = alphas[i]
                image[y, x] = texel
    return image


def decode_bitmap(bitmap: dict) -> np.ndarray:
    """An Xbox HUD bitmap's level 0 as RGBA, as xbox_textures.c decodes it."""
    width, height, kind = bitmap["width"], bitmap["height"], FORMATS[bitmap["format"]]
    pixels = np.frombuffer(bitmap["pixels"][:level0_size(bitmap)], np.uint8)
    if kind.startswith("dxt"):
        return decode_dxt(kind, bitmap["pixels"], width, height)
    order = (np.arange(width * height).reshape(height, width) if bitmap["flags"] & BITMAP_LINEAR_FLAG else
             morton_order(width, height))
    texel = level0_size(bitmap) // (width * height)
    raw = pixels.reshape(-1, texel)[order]
    image = np.zeros((height, width, 4), np.uint8)
    if kind == "a8":
        image[..., :3], image[..., 3] = 255, raw[..., 0]
    elif kind == "y8":
        image[..., :3], image[..., 3] = raw[..., :1], 255
    elif kind == "ay8":
        image[...] = raw[..., :1]
    elif kind == "a8y8":
        image[..., :3], image[..., 3] = raw[..., :1], raw[..., 1]
    elif kind == "a4r4g4b4":
        value = raw[..., 0].astype(np.uint16) | (raw[..., 1].astype(np.uint16) << 8)
        for channel, shift in ((3, 12), (0, 8), (1, 4), (2, 0)):
            image[..., channel] = ((value >> shift) & 15) * 17
    elif kind in ("a8r8g8b8", "x8r8g8b8"):
        image[..., 0], image[..., 1], image[..., 2] = raw[..., 2], raw[..., 1], raw[..., 0]
        image[..., 3] = raw[..., 3] if kind == "a8r8g8b8" else 255
    else:
        raise ValueError(f"no decoder for {kind}")
    return image


# ---------- PC (HEK) tags


def hek_bitmap_group(path: Path) -> dict:
    """A HEK bitmap tag's bitmaps (sizes and formats) and sprites (big-endian;
    its blocks follow the 0x40-byte file header and the 0x6C-byte tag in
    order: data, sequences with their sprites, bitmaps)."""
    data = path.read_bytes()
    header = data[0x40:0x40 + 0x6C]
    compressed_size, = struct.unpack_from(">I", header, 0x1C)
    pixels_size, = struct.unpack_from(">I", header, 0x30)
    sequence_count, = struct.unpack_from(">I", header, 0x54)
    bitmap_count, = struct.unpack_from(">I", header, 0x60)
    position = 0x40 + 0x6C + compressed_size + pixels_size
    counts = [struct.unpack_from(">I", data, position + index * 0x40 + 0x34)[0] for index in range(sequence_count)]
    position += sequence_count * 0x40
    group = {"sequences": [], "bitmaps": []}
    for count in counts:
        group["sequences"].append([sprite_from(data[position + j * 0x20:position + (j + 1) * 0x20], ">")
                                   for j in range(count)])
        position += count * 0x20
    for index in range(bitmap_count):
        width, height, _, _, format = struct.unpack_from(">HHHhh", data, position + index * 0x30 + 4)
        offset, = struct.unpack_from(">i", data, position + index * 0x30 + 0x18)
        group["bitmaps"].append({"width": width, "height": height, "format": format,
                                 "pixels": data[0x40 + 0x6C + compressed_size + offset:]})
    return group


def hek_bitmap_texels(bitmap: dict) -> np.ndarray:
    """A HEK bitmap's level 0 as RGBA, decoded as the Xbox's are (its pixels
    are in rows, not swizzled)."""
    return decode_bitmap(dict(bitmap, flags=BITMAP_LINEAR_FLAG))


# ---------- edits: where the restored art says something else than the Xbox's


# the sniper's zoom labels: the restored sheet's are 2x and 8x (Halo PC's),
# the Xbox's 2x and 10x; a 10x is made of "10" in the Xbox's font (Tahoma
# Bold) beside the restored "x", sized and placed as the Xbox's is
ZOOM_LABELS = HUD + "sniper\\hud_reticles_scope"
TAHOMA_BOLD = "/System/Library/Fonts/Supplemental/Tahoma Bold.ttf"


def ten_times_label(sheet: np.ndarray, font: str) -> np.ndarray:
    """A sheet the size of the restored zoom labels' with a 10x in its top
    left 172 by 96 texels (the Xbox's cell, at 4x): the "10" at the size and
    place that match the Xbox's best (Tahoma Bold, 73 px, at 17, 1), the
    "x" the restored 8x's, cut where it meets the 8 (where the fewest of the
    sheet's columns are covered), at 108, 12."""
    from PIL import ImageDraw, ImageFont

    result = np.zeros_like(sheet)
    text = Image.new("L", (172, 96))
    ImageDraw.Draw(text).text((17, 1), "10", font=ImageFont.truetype(font, 73), fill=255)
    result[:96, :172, :3] = 255
    result[:96, :172, 3] = np.asarray(text)
    # (the 8x sprite is 100 to 204 across and 72 down; its x from the gap)
    covered = (sheet[:72, 100:204, 3] > 128).sum(axis=0)
    cut = 100 + 30 + int(np.argmin(covered[30:80]))
    x = sheet[:72, cut:204]
    width = min(x.shape[1], 172 - 108)
    region = result[12:12 + 72, 108:108 + width]
    region[...] = np.where(x[:, :width, 3:4] > region[..., 3:4], x[:, :width], region)
    return result


# ---------- rendering and matching


def render_svg(svg: Path, zoom: float = 1) -> np.ndarray:
    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory) / "render.png"
        subprocess.run(["rsvg-convert", "-z", str(zoom), "-o", str(output), str(svg)], check=True)
        return np.asarray(Image.open(output).convert("RGBA")).copy()


def pixel_rectangle(sprite: tuple, width: int, height: int) -> list:
    _, left, top, right, bottom = sprite
    return [round(left * width), round(top * height), round(right * width), round(bottom * height)]


def window(image: np.ndarray, left: int, top: int, width: int, height: int) -> np.ndarray:
    """image[top:top+height, left:left+width], transparent outside it."""
    result = np.zeros((height, width) + image.shape[2:], image.dtype)
    x0, y0 = max(left, 0), max(top, 0)
    x1, y1 = min(left + width, image.shape[1]), min(top + height, image.shape[0])
    if x0 < x1 and y0 < y1:
        result[y0 - top:y1 - top, x0 - left:x1 - left] = image[y0:y1, x0:x1]
    return result


def clipped(image: np.ndarray, clip: list) -> np.ndarray:
    """image with only clip's texels (a sprite's own, not its neighbours')."""
    left, top, right, bottom = clip
    result = np.zeros_like(image)
    result[top:bottom, left:right] = image[top:bottom, left:right]
    return result


def overlap(a: np.ndarray, b: np.ndarray) -> float:
    union = np.maximum(a, b).sum()
    return float(np.minimum(a, b).sum() / union) if union else 0.0


def match(alpha: np.ndarray, render: np.ndarray, clip: list, scale: int) -> tuple:
    """How well the redrawn sprite in clip (on the render, scale times the
    Xbox texels) matches the Xbox cell's alpha, and where: (overlap, the
    render's corner for the cell). Searched in Xbox texels on the render
    reduced, then refined in its own texels."""
    height, width = alpha.shape
    middle = ((clip[0] + clip[2]) / 2, (clip[1] + clip[3]) / 2)
    corner = [round(middle[0] - width * scale / 2), round(middle[1] - height * scale / 2)]
    alpha = alpha.astype(float) / 255
    source = clipped(render[..., 3].astype(float) / 255, clip)
    best = (-1.0, corner)
    for dy in range(-SEARCH, SEARCH + 1):
        for dx in range(-SEARCH, SEARCH + 1):
            at = [corner[0] + dx * scale, corner[1] + dy * scale]
            reduced = window(source, at[0], at[1], width * scale, height * scale).reshape(
                height, scale, width, scale).mean(axis=(1, 3))
            score = overlap(reduced, alpha)
            if score > best[0]:
                best = (score, at)
    target = np.asarray(Image.fromarray((alpha * 255).astype(np.uint8)).resize(
        (width * scale, height * scale), Image.BILINEAR), float) / 255
    coarse = best[1]
    best = (-1.0, coarse)
    for dy in range(-scale + 1, scale):
        for dx in range(-scale + 1, scale):
            at = [coarse[0] + dx, coarse[1] + dy]
            score = overlap(window(source, at[0], at[1], width * scale, height * scale), target)
            if score > best[0]:
                best = (score, at)
    return best


def sources(svg_root, hek_root: Path, tag: str, font: str) -> tuple:
    """The restored art for a tag's bitmaps, and the SVG redraws of them:
    each a list of (name, PC bitmap index, PC bitmap group), the restored
    art's named as the redraws are (their path under ui, or the tags, with
    the bitmap's index when the tag has more than one)."""
    parts = tag.split("\\")
    if tag in METERS:
        relative, hek = METERS[tag]
        hek = hek_root / hek
    else:
        relative = str(Path(*parts[1:]) if parts[0] == "ui" else Path(*parts))
        hek = hek_root / "tags" / Path(*parts).with_suffix(".bitmap")
    if not hek.exists():
        return [], []
    group = hek_bitmap_group(hek)
    art = []
    for index in range(len(group["bitmaps"])):
        name = relative + (f"__{index}" if len(group["bitmaps"]) > 1 else "")
        if tag in METERS or name not in UNUSED:
            art.append((name, index, group))
    if tag == ZOOM_LABELS:
        sheet = hek_bitmap_texels(group["bitmaps"][0])
        height, width = sheet.shape[:2]
        edited = {"bitmaps": [{"width": width, "height": height, "format": group["bitmaps"][0]["format"],
                               "texels": ten_times_label(sheet, font)}],
                  "sequences": [[(0, 0.0, 0.0, 172 / width, 96 / height)]]}
        art.append((relative + "__10x", 0, edited))
    svgs = []
    if svg_root is not None:
        flat = relative.replace("/", "__").replace(" ", "_")
        for svg in sorted(svg_root.glob(flat + "*.svg")):
            parsed = re.fullmatch(re.escape(flat) + r"(?:__(\d+))?\.svg", svg.name)
            if parsed and (tag in METERS or relative + (f"__{parsed.group(1)}" if parsed.group(1) else "")
                           not in UNUSED):
                svgs.append((svg.name, int(parsed.group(1) or 0), group))
    return art, svgs


def score_cell(alpha: np.ndarray, cell: list, numbers: set, xbox_width: int, candidates: list, renders: dict,
               kind: str, svg_root, minimum_scale: int) -> tuple:
    """The best match for a cell of an Xbox bitmap xbox_width wide among
    candidates of one kind ("art" or "svg") at least minimum_scale times its
    size: (score, name, scale, corner, clip, kind, whether its sprite numbers
    are the cell's); or None."""
    left, top, right, bottom = cell
    scored = []
    for name, pc_index, pc_group in candidates:
        pc_bitmap = pc_group["bitmaps"][pc_index]
        key = (kind, name)
        if key not in renders:
            renders[key] = (pc_bitmap["texels"] if "texels" in pc_bitmap else hek_bitmap_texels(pc_bitmap)) \
                if kind == "art" else render_svg(svg_root / name)
        render = renders[key]
        if render.shape[1] % xbox_width:
            continue
        scale = render.shape[1] // xbox_width
        if scale < minimum_scale:
            continue
        pc_cells = [(pixel_rectangle(sprite, render.shape[1], render.shape[0]), (sequence_index, sprite_index))
                    for sequence_index, sequence in enumerate(pc_group["sequences"])
                    for sprite_index, sprite in enumerate(sequence) if sprite[0] == pc_index]
        for clip, pc_numbers in pc_cells or [([0, 0, render.shape[1], render.shape[0]], None)]:
            # (about the same size)
            if not (0.6 < (clip[2] - clip[0]) / ((right - left) * scale) < 1.6 and
                    0.6 < (clip[3] - clip[1]) / ((bottom - top) * scale) < 1.6):
                continue
            score, corner = match(alpha, render, clip, scale)
            scored.append((score, name, scale, corner, clip, KINDS.get(pc_bitmap["format"]), pc_numbers in numbers))
    # the best match; but the sprite of the same numbers where the sheets
    # agree and it matches nearly as well (small glyphs, a dot or a colon,
    # overlap others as much as their own)
    best = max(scored, default=None)
    same = max((entry for entry in scored if entry[6]), default=None)
    if same is not None and best is not None and same[0] >= max(MATCH, 0.6 * best[0]):
        best = same
    return best


def layout(arguments) -> None:
    xbox_map = XboxMap(Path(arguments.map))
    svg_root = Path(arguments.svg) if arguments.svg else None
    hek_root = Path(arguments.hek)
    renders = {}
    entries = []
    used = {}
    for group_tag, tag in sorted(xbox_map.tags):
        if group_tag != "bitm" or not tag.startswith(FOLDERS):
            continue
        art, svgs = sources(svg_root, hek_root, tag, arguments.font)
        if not art and not svgs:
            continue
        group = xbox_map.bitmap_group(tag)
        for index, bitmap in enumerate(group["bitmaps"]):
            width, height = bitmap["width"], bitmap["height"]
            # (the combined sheets by their own names, as before the others;
            # the weapon's by its name and theirs)
            stem = tag[len(HUD):].replace("combined\\", "") if tag.startswith(HUD) else \
                tag.split("\\", 1)[1].replace("\\bitmaps\\", "\\")
            name = stem.replace("\\", "__").replace(" ", "_") + f"__{index}"
            xbox = decode_bitmap(bitmap)
            # (each cell with the sprites, as sequence and sprite numbers, it is)
            cells = {}
            for sequence_index, sequence in enumerate(group["sequences"]):
                for sprite_index, sprite in enumerate(sequence):
                    if sprite[0] == index:
                        cells.setdefault(tuple(pixel_rectangle(sprite, width, height)), set()).add(
                            (sequence_index, sprite_index))
            cells = cells or {(0, 0, width, height): set()}
            matched = []
            for cell, numbers in sorted(cells.items()):
                cell = list(cell)
                left, top, right, bottom = cell
                # (the cell's shape: its alpha, or its grey where its alpha is
                # flat: the Xbox's DXT1 blip is opaque, and its "Km" glyph
                # has only grey)
                alpha = xbox[top:bottom, left:right, 3]
                grey = xbox[top:bottom, left:right, 0]
                # (or its grey where that is the shape drawn: the warning
                # sheet's pips are dots in the grey over triangles in the
                # alpha, where the restored ones are dots in both)
                if alpha.min() == alpha.max() or (
                        "reticle_warnings" in tag and overlap(alpha.astype(float), grey.astype(float)) < 0.5):
                    alpha = grey
                # (blank, or a few stray texels: the edges of a neighbour's
                # shape, which the Xbox's sheets have in some cells; left
                # blank)
                if np.count_nonzero(alpha) <= 4:
                    continue
                # (the restored art where it is larger than the Xbox's; else
                # an SVG redraw)
                kind = "art"
                best = score_cell(alpha, cell, numbers, width, art, renders, kind, svg_root, 2)
                if best is None or best[0] < MATCH or best[5] is None:
                    kind = "svg"
                    best = score_cell(alpha, cell, numbers, width, svgs, renders, kind, svg_root, 1)
                # (a faint cell that matches nothing: matched by its shape,
                # and drawn as faint; or, matching nothing still, left blank,
                # as a faint frame on the Xbox's message icons, all but
                # invisible, is)
                faint = (best is None or best[0] < MATCH) and alpha.max() < 64
                if faint:
                    kind = "art"
                    best = score_cell((alpha.astype(float) * 255 / alpha.max()).astype(np.uint8), cell, numbers,
                                      width, art, renders, kind, svg_root, 2)
                    if best is None or best[0] < MATCH:
                        print(f"{name}: cell {cell}, faint, matches nothing: left blank")
                        continue
                if best is None or best[0] < MATCH or best[5] is None:
                    matched = None
                    print(f"{name}: left out, cell {cell} " +
                          ("has no redraw" if best is None else f"matches best {best[1]} at {best[0]:.2f}"))
                    break
                score, source, scale, corner, clip, cell_kind, _ = best
                corner = CORNERS.get((name, tuple(cell)), corner)
                if tag in METERS:
                    cell_kind = "meter"
                flat = source.replace("/", "__").replace(" ", "_")
                if kind == "art":
                    flat += ".png"
                    used[flat] = renders[("art", source)]
                else:
                    used[flat] = svg_root / source
                found = {"xbox": cell, kind: flat, "source_scale": scale,
                         "source": corner, "clip": clip, "kind": cell_kind, "score": round(score, 3)}
                if tag == HUD + "combined\\hud_unit_meters":
                    original = xbox[top:bottom, left:right]
                    values = set(np.unique(original[..., 2][original[..., 3] > 0]))
                    health = set(range(30, 241, 30))
                    if health <= values <= health | {0}:
                        found["thresholds"] = sorted(health)
                if faint:
                    found["intensity"] = round(float(xbox[top:bottom, left:right, 3].max()) / 255, 3)
                matched.append(found)
            if not matched:
                if matched is not None:
                    print(f"{name}: left out, empty")
                continue
            # (the restored art's own scale; 8x where a redraw is drawn)
            scale = max(SCALE if "svg" in cell else cell["source_scale"] for cell in matched)
            while max(width, height) * scale > MAXIMUM_SIZE:
                scale //= 2
            entries.append({
                "name": name,
                "tag": tag,
                "bitmap": index,
                "width": width,
                "height": height,
                "format": FORMATS[bitmap["format"]],
                "scale": scale,
                # (the Xbox bitmap's, which the game checks before drawing this
                # in its place: other languages' maps and modified ones differ)
                "crc": zlib.crc32(bitmap["pixels"][:level0_size(bitmap)]),
                "cells": matched,
            })
            print(f"{name}: {width}x{height} {FORMATS[bitmap['format']]} at {scale}x, {len(matched)} cells, "
                  f"{sorted({'art' if 'art' in cell else 'svg' for cell in matched})}, "
                  f"worst match {min(cell['score'] for cell in matched):.2f}")
    for folder in ("art", "svg"):
        if (ASSETS / folder).exists() and Path(arguments.svg or "").resolve() != (ASSETS / folder).resolve():
            shutil.rmtree(ASSETS / folder)
    (ASSETS / "art").mkdir(parents=True, exist_ok=True)
    (ASSETS / "svg").mkdir(parents=True, exist_ok=True)
    kept_svgs = set()
    for flat, source in sorted(used.items(), key=lambda item: item[0]):
        if flat.endswith(".png"):
            Image.fromarray(source).save(ASSETS / "art" / flat, optimize=True)
        else:
            kept_svgs.add(flat)
            if source.resolve() != (ASSETS / "svg" / flat).resolve():
                shutil.copyfile(source, ASSETS / "svg" / flat)
    for stale in (ASSETS / "svg").glob("*.svg"):
        if stale.name not in kept_svgs:
            stale.unlink()
    LAYOUT.write_text(json.dumps({"assets": entries}, indent=1) + "\n")


# ---------- building


def bleed(image: np.ndarray) -> np.ndarray:
    """Gives each transparent texel the colour of the nearest covered one: so
    that filtering and mip levels at a meter's edges read its own fill
    thresholds, and a DXT texture keeps its colour where it is transparent."""
    from scipy import ndimage

    covered = image[:, :, 3] > 0
    if not covered.any():
        return image
    _, (rows, columns) = ndimage.distance_transform_edt(~covered, return_indices=True)
    result = image.copy()
    for channel in range(3):
        result[:, :, channel] = image[rows, columns, channel]
    result[:, :, 3] = image[:, :, 3]
    return result


def coverage(alpha: np.ndarray) -> np.ndarray:
    """How much of each texel a meter's shapes cover: all of it inside them
    (their dim cells too: their alpha is faint by design), and on their
    outline (covered texels next to uncovered ones) the share their alpha has
    of their brightest neighbour's. The meter shader reads only blue and
    alpha; the game eases the meter's darkening of what is behind it by this
    (port/linux/src/nv2a_psh.c, coverage_alpha), which on the Xbox stopped at
    its point-sampled texels' edges, and filtered would leave a dark fringe."""
    from scipy import ndimage

    covered = alpha > 0
    outline = covered & ndimage.binary_dilation(~covered, structure=np.ones((3, 3), bool))
    brightest = ndimage.maximum_filter(alpha, size=3).astype(float)
    result = np.where(covered, 255.0, 0.0)
    result[outline] = np.clip(alpha[outline] * 255.0 / np.maximum(brightest[outline], 1.0), 0, 255)
    return np.round(result).astype(np.uint8)


def recipe(texels: np.ndarray, kind: str) -> np.ndarray:
    """A redraw's texels written as its kind is (KINDS)."""
    result = texels.copy()
    grey = np.round(texels[..., :3].astype(float).mean(axis=-1)).astype(np.uint8)
    if kind == "alpha":
        result[..., :3] = texels[..., 3:4]
    elif kind in ("grey", "meter"):
        result[..., :3] = grey[..., None]
        result[texels[..., 3] == 0, :3] = 0
    elif kind == "a8":
        result[..., :3] = 255
    elif kind == "y8":
        result[..., :3], result[..., 3] = grey[..., None], 255
    return result


def meter_thresholds(texels: np.ndarray, values: list) -> np.ndarray:
    """Categorical segment data, independent of antialiased edge RGB/alpha.

    Each solid-colour shape's opaque stroke supplies its exact threshold.
    Assign that value to its whole core, then extend it to the nearest edge
    and transparent texel. Low-alpha SVG texels cannot supply reliable RGB:
    unpremultiplying 8-bit colour there can turn 150 into 128 (or 60 into 0).
    This is only for the explicitly listed discrete health sprites; shield
    and ammunition gradients retain their existing continuous data.
    """
    from scipy import ndimage

    cores, count = ndimage.label(texels[..., 3] >= 127)
    exact = np.zeros(texels.shape[:2], np.uint8)
    seen = set()
    for index in range(1, count + 1):
        core = cores == index
        opaque = texels[core & (texels[..., 3] == 255), :3]
        if not len(opaque):
            continue
        colours = np.unique(opaque, axis=0)
        if len(colours) != 1 or not np.all(colours[0] == colours[0, 0]):
            raise ValueError("discrete meter shape must have one solid grey threshold")
        value = int(colours[0, 0])
        if value not in values:
            raise ValueError(f"unexpected discrete meter threshold {value}")
        exact[core] = value
        seen.add(value)
    if seen != set(values):
        raise ValueError(f"missing discrete meter thresholds: {set(values) - seen}")
    _, (rows, columns) = ndimage.distance_transform_edt(exact == 0, return_indices=True)
    return exact[rows, columns]


def add_meter_thresholds(image: np.ndarray, entry: dict) -> np.ndarray:
    """Red is an unfiltered categorical field; zero selects normal blue data.
    Green coverage and alpha artwork remain filtered, including their mips.
    """
    cells = [cell for cell in entry["cells"] if cell.get("thresholds")]
    if cells:
        image[..., 0] = 0
        scale = entry["scale"]
        for cell in cells:
            left, top, right, bottom = [value * scale for value in cell["xbox"]]
            texels = image[top:bottom, left:right]
            # The meter's blue is still the original greyscale artwork data.
            source = texels.copy()
            source[..., :3] = source[..., 2:3]
            texels[..., 0] = meter_thresholds(source, cell["thresholds"])
    return image


def resized(texels: np.ndarray, zoom: float) -> np.ndarray:
    """Restored art at zoom times its size (premultiplied while resampled, so
    that transparent texels' colour does not bleed in); as it is at 1."""
    if zoom == 1:
        return texels
    size = (round(texels.shape[1] * zoom), round(texels.shape[0] * zoom))
    alpha = texels[..., 3:4].astype(float) / 255
    channels = [Image.fromarray(channel.astype(np.float32), "F").resize(size, Image.LANCZOS)
                for channel in np.moveaxis(np.concatenate([texels[..., :3] * alpha, alpha * 255], axis=-1), -1, 0)]
    result = np.clip(np.stack([np.asarray(channel) for channel in channels], axis=-1), 0, 255)
    covered = result[..., 3:4] > 0
    result[..., :3] = np.where(covered, result[..., :3] * 255 / np.maximum(result[..., 3:4], 1e-6), 0)
    return np.round(np.clip(result, 0, 255)).astype(np.uint8)


def build_asset(entry: dict, renders: dict) -> np.ndarray:
    scale = entry["scale"]
    image = np.zeros((entry["height"] * scale, entry["width"] * scale, 4), np.uint8)
    for cell in entry["cells"]:
        # (the restored art, or the redraw, at the texture's scale, its
        # coordinates with it)
        zoom = scale / cell["source_scale"]
        if "art" in cell:
            key = ("art", cell["art"], zoom)
            if key not in renders:
                renders[key] = resized(np.asarray(Image.open(ASSETS / "art" / cell["art"]).convert("RGBA")), zoom)
        else:
            assert zoom == int(zoom), entry["name"]
            key = ("svg", cell["svg"], zoom)
            if key not in renders:
                renders[key] = render_svg(ASSETS / "svg" / cell["svg"], int(zoom))
        left, top, right, bottom = cell["xbox"]
        width, height = (right - left) * scale, (bottom - top) * scale
        texels = window(clipped(renders[key], [round(value * zoom) for value in cell["clip"]]),
                        round(cell["source"][0] * zoom), round(cell["source"][1] * zoom), width, height)
        texels = recipe(texels, cell["kind"])
        if "intensity" in cell:
            texels = np.round(texels * cell["intensity"]).astype(np.uint8)
        image[top * scale:top * scale + height, left * scale:left * scale + width] = texels
    if any(cell["kind"] in ("meter", "dxt") for cell in entry["cells"]):
        image = bleed(image)
    if any(cell["kind"] == "meter" for cell in entry["cells"]):
        image[..., 1] = coverage(image[..., 3])
    return add_meter_thresholds(image, entry)


def thresholds(arguments) -> None:
    """Rebuild only categorical data in committed PNGs, retaining their art."""
    for entry in json.loads(LAYOUT.read_text())["assets"]:
        if any(cell.get("thresholds") for cell in entry["cells"]):
            path = ASSETS / f"{entry['name']}.png"
            image = np.asarray(Image.open(path).convert("RGBA")).copy()
            Image.fromarray(add_meter_thresholds(image, entry), "RGBA").save(path, optimize=True)
            print(f"{path.name}: rebuilt exact segment thresholds; artwork unchanged")


def build(arguments) -> None:
    description = json.loads(LAYOUT.read_text())
    names = {f"{entry['name']}.png" for entry in description["assets"]}
    for stale in ASSETS.glob("*.png"):
        if stale.name not in names:
            stale.unlink()
    renders = {}
    for entry in description["assets"]:
        image = build_asset(entry, renders)
        Image.fromarray(image, "RGBA").save(ASSETS / f"{entry['name']}.png", optimize=True)
        print(f"{entry['name']}.png: {image.shape[1]}x{image.shape[0]}")


# ---------- checking


def check(arguments) -> None:
    xbox_map = XboxMap(Path(arguments.map))
    output = Path(arguments.out)
    output.mkdir(parents=True, exist_ok=True)
    description = json.loads(LAYOUT.read_text())
    for entry in description["assets"]:
        bitmap = xbox_map.bitmap_group(entry["tag"])["bitmaps"][entry["bitmap"]]
        if zlib.crc32(bitmap["pixels"][:level0_size(bitmap)]) != entry["crc"]:
            print(f"{entry['name']}: this map's bitmap is not the one laid out")
        xbox = decode_bitmap(bitmap)
        image = Image.open(ASSETS / f"{entry['name']}.png").convert("RGBA")
        reduced = np.asarray(image.resize((entry["width"], entry["height"]), Image.BOX))
        score = overlap(reduced[..., 3].astype(float), xbox[..., 3].astype(float))
        print(f"{entry['name']}: alpha overlap {score:.2f}")
        # Xbox | ours reduced | ours, each over blue, then each one's alpha
        panels = [Image.fromarray(picture, "RGBA").resize(image.size, Image.NEAREST) for picture in (xbox, reduced)]
        panels.append(image)
        width, height = image.size
        sheet = Image.new("RGB", (width * 3 + 16, height * 2 + 8), (255, 0, 0))
        for index, picture in enumerate(panels):
            background = Image.new("RGBA", picture.size, (40, 40, 90, 255))
            sheet.paste(Image.alpha_composite(background, picture).convert("RGB"), (index * (width + 8), 0))
            sheet.paste(picture.getchannel("A").convert("RGB"), (index * (width + 8), height + 8))
        sheet.save(output / f"{entry['name']}.png")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("layout")
    command.add_argument("--map", required=True)
    command.add_argument("--hek", required=True)
    command.add_argument("--svg")
    command.add_argument("--font", default=TAHOMA_BOLD)
    commands.add_parser("build")
    commands.add_parser("thresholds")
    command = commands.add_parser("check")
    command.add_argument("--map", required=True)
    command.add_argument("--out", required=True)
    arguments = parser.parse_args()
    {"layout": layout, "build": build, "thresholds": thresholds, "check": check}[arguments.command](arguments)


if __name__ == "__main__":
    sys.exit(main())
