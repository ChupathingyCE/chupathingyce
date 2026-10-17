#!/usr/bin/env python3
"""Makes the Windows executable's icon, port/windows/halo.ico, from the
artwork, port/art/icon.svg:

    python tools/windows_icon.py

The icon holds the artwork at 16, 24, 32, 48, 64, 128 and 256 pixels, each
rendered from the SVG on its own (so the small ones are not scaled down from
a large one), as PNG images. Needs rsvg-convert (librsvg). The resource
script, port/windows/halo.rc, puts the icon in the executable; run this
again when the artwork changes.
"""

import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ARTWORK = ROOT / "port/art/icon.svg"
ICON = ROOT / "port/windows/halo.ico"

SIZES = (16, 24, 32, 48, 64, 128, 256)


def render(size: int) -> bytes:
    """the artwork as a PNG of size by size pixels"""
    return subprocess.run(
        ["rsvg-convert", "-w", str(size), "-h", str(size), str(ARTWORK)],
        check=True, capture_output=True).stdout


def main() -> None:
    images = [render(size) for size in SIZES]
    # the icon's header, then an entry for each image (a width or height of
    # 256 is written 0), then the images
    data = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for size, image in zip(SIZES, images):
        edge = size % 256
        data += struct.pack("<BBBBHHII", edge, edge, 0, 0, 1, 32, len(image), offset)
        offset += len(image)
    ICON.write_bytes(data + b"".join(images))
    print(f"wrote {ICON.relative_to(ROOT)} ({len(SIZES)} sizes, {len(data) + sum(map(len, images))} bytes)")


if __name__ == "__main__":
    main()
