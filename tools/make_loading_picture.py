#!/usr/bin/env python3
"""Makes Chupathingy's loading picture, port/assets/loading/chupathingy_loading.tga,
which the loading screen shows unless display.xbox_loading_picture is on
(port/linux/game/loading_picture.c):

    python tools/make_loading_picture.py [--preview PREVIEW.png]

It imitates the Xbox's loading picture: a big shape, edge lit and partly off
the frame, with LOADING... in the middle in a soft blue glow. The shape is
the helmet and tusks of Chupathingy's icon.

Inputs, all in the repository:
- port/art/icon.svg, the icon, drawn at 1024 by 1024 by rsvg-convert
  (librsvg; Homebrew's or the distribution's librsvg package)
- port/assets/fonts/CyborgInterfaceGothic-Regular.ttf, the text's font,
  the menus' (by fizzy, SIL OFL, port/assets/fonts/CyborgInterfaceGothic-OFL.txt)
and Pillow and NumPy (pip install pillow numpy).

The output is what progress_bar.c reads: 320 by 240, uncompressed 24-bit
(TGA type 2), its first row at the bottom, as the disc's loading.tga. The
picture is drawn at 1280 by 960 and scaled down; --preview also writes it
at 640 by 480 as a PNG. The random detail is seeded, so the same inputs
make the same picture.
"""

import argparse
import random
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
ICON = ROOT / "port/art/icon.svg"
FONT = ROOT / "port/assets/fonts/CyborgInterfaceGothic-Regular.ttf"
OUTPUT = ROOT / "port/assets/loading/chupathingy_loading.tga"
WIDTH, HEIGHT = 1280, 960


def render_icon(directory: Path) -> Image.Image:
    """The icon at 1024 by 1024."""
    path = directory / "icon.png"
    try:
        subprocess.run(["rsvg-convert", "-w", "1024", "-h", "1024", str(ICON), "-o", str(path)], check=True)
    except FileNotFoundError:
        sys.exit("rsvg-convert not found: install librsvg")
    return Image.open(path).convert("RGB")


def make_picture(icon: Image.Image) -> Image.Image:
    """The picture, at WIDTH by HEIGHT."""
    random.seed(117)
    pixels = np.asarray(icon).astype(int)
    # the helmet and tusks: everything not the red field
    red = (abs(pixels[..., 0] - 163) < 40) & (pixels[..., 1] < 70) & (pixels[..., 2] < 70)
    mask = Image.fromarray(((~red) * 255).astype("uint8"))
    # big, turned and partly off the frame, as the ring sweeps through the
    # Xbox's picture
    size = 1180
    shape = mask.resize((size, size), Image.LANCZOS).rotate(-14, resample=Image.BICUBIC, expand=True)
    canvas_mask = Image.new("L", (WIDTH, HEIGHT), 0)
    canvas_mask.paste(shape, (-150, -190))
    # the surface's detail inside the shape: dim panels, rings and dots
    detail = Image.new("L", (WIDTH, HEIGHT), 0)
    draw = ImageDraw.Draw(detail)
    for _ in range(1600):
        x, y = random.randrange(WIDTH), random.randrange(HEIGHT)
        step = random.choice([6, 10, 16, 24, 40])
        value = random.randrange(35, 95)
        if random.random() < 0.6:
            draw.rectangle([x, y, x + step * random.uniform(0.5, 3), y + step], outline=value)
        else:
            draw.ellipse([x, y, x + step, y + step], outline=value)
    for _ in range(9000):
        x, y = random.randrange(WIDTH), random.randrange(HEIGHT)
        draw.point((x, y), fill=random.randrange(40, 110))
    for cx, cy, radius in ((420, 230, 70), (520, 560, 45)):
        for ring in range(4):
            inset = ring * 9
            draw.ellipse([cx - radius + inset, cy - radius + inset, cx + radius - inset, cy + radius - inset],
                         outline=90)
    detail = detail.filter(ImageFilter.GaussianBlur(0.6))
    inside = ImageChops.multiply(detail, canvas_mask)
    # the bright edge, as the Xbox picture's edge-lit ring
    edge = canvas_mask.filter(ImageFilter.FIND_EDGES).filter(ImageFilter.MaxFilter(3))
    edge_glow = edge.filter(ImageFilter.GaussianBlur(4))
    light = ImageChops.add(ImageChops.add(inside, edge.point(lambda v: v * 200 // 255)),
                           edge_glow.point(lambda v: v // 3))
    picture = Image.merge("RGB", (light, light, light))
    # LOADING... with a soft blue glow
    font = ImageFont.truetype(str(FONT), 62)
    text = "LOADING..."
    text_width = ImageDraw.Draw(picture).textlength(text, font=font)
    # (its baseline set where it was in Noto Sans Bold, the font before)
    tx, ty = (WIDTH - text_width) / 2, HEIGHT / 2 + 28
    glow = Image.new("L", (WIDTH, HEIGHT), 0)
    ImageDraw.Draw(glow).text((tx, ty), text, font=font, fill=255, anchor="ls")
    glow = glow.filter(ImageFilter.GaussianBlur(9))
    blue = Image.merge("RGB", (glow.point(lambda v: v * 40 // 255), glow.point(lambda v: v * 120 // 255), glow))
    picture = ImageChops.add(picture, blue)
    ImageDraw.Draw(picture).text((tx, ty), text, font=font, fill=(232, 242, 255), anchor="ls")
    return picture


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", type=Path, default=OUTPUT, help="the TGA to write")
    parser.add_argument("--preview", type=Path, help="also write the picture at 640x480 as this PNG")
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory() as directory:
        picture = make_picture(render_icon(Path(directory)))
    if arguments.preview:
        picture.resize((640, 480), Image.LANCZOS).save(arguments.preview)
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    # (Pillow writes 24-bit RGB uncompressed, bottom row first)
    picture.resize((320, 240), Image.LANCZOS).save(arguments.output, compression=None)
    print(f"wrote {arguments.output}")


if __name__ == "__main__":
    main()
