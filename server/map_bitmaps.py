#!/usr/bin/env python3
"""The bitmaps in an Xbox Halo cache file (a .map), as PNG files: the game's
own pictures for the game list's pages (server/list_server.py).

  server/map_bitmaps.py list assets/maps/ui.map [PATTERN]
  server/map_bitmaps.py extract assets/maps/ui.map OUTDIR [PATTERN ...]

The layout is the game's (source/cache/cache_files.c, bitmaps/bitmap_group.h):
a 2 KB header, then the rest of the file compressed with zlib; in it, the tag
data at the header's offset, loaded at the Xbox address 0x803A6000; a bitmap
tag's bitmaps point at their pixels in the (decompressed) file, swizzled
unless compressed. Needs Pillow and numpy."""

import fnmatch
import struct
import sys
import zlib
from pathlib import Path

import numpy
from PIL import Image

TAG_BASE = 0x803A6000
HEADER_SIZE = 0x800

# bitmap_group.h's formats (bytes a pixel, or a DXT block's)
FORMATS = {
    0: ("A8", 1), 1: ("Y8", 1), 2: ("AY8", 1), 3: ("A8Y8", 2),
    6: ("R5G6B5", 2), 8: ("A1R5G5B5", 2), 9: ("A4R4G4B4", 2),
    10: ("X8R8G8B8", 4), 11: ("A8R8G8B8", 4),
    14: ("DXT1", 8), 15: ("DXT3", 16), 16: ("DXT5", 16), 17: ("P8", 1),
}
SWIZZLED_FLAG = 1 << 3
TYPE_2D = 0


class CacheFile:
    def __init__(self, path: Path):
        raw = path.read_bytes()
        self.data = raw[:HEADER_SIZE] + zlib.decompress(raw[HEADER_SIZE:])
        (self.tag_data_offset, self.tag_data_size) = struct.unpack_from("<ii", self.data, 0x10)
        self.tags = self.data[self.tag_data_offset:self.tag_data_offset + self.tag_data_size]
        instances, _, _, count = struct.unpack_from("<IiIi", self.tags, 0)
        self.instances = []
        for index in range(count):
            group, _, _, _, name, base, _, _ = struct.unpack_from("<4sIIiIIII", self.tags, self.offset(instances) + index * 0x20)
            self.instances.append((group[::-1].decode("latin-1"), self.string(name), base))

    def offset(self, address: int) -> int:
        return address - TAG_BASE

    def string(self, address: int) -> str:
        start = self.offset(address)
        return self.tags[start:self.tags.index(b"\0", start)].decode("latin-1")

    def bitmap_tags(self):
        return [(name, base) for group, name, base in self.instances if group == "bitm"]

    def bitmaps(self, base: int):
        """a bitmap tag's bitmaps: (width, height, type, format, flags, offset, size)"""
        count, address = struct.unpack_from("<iI", self.tags, self.offset(base) + 0x60)
        result = []
        for index in range(count):
            at = self.offset(address) + index * 0x30
            _, width, height, _, kind, format, flags = struct.unpack_from("<Ihhhhhh", self.tags, at)
            pixels_offset, pixels_size = struct.unpack_from("<ii", self.tags, at + 0x18)
            result.append((width, height, kind, format, flags & 0xFFFF, pixels_offset, pixels_size))
        return result


def unswizzle(pixels: bytes, width: int, height: int, size: int) -> bytes:
    """the Xbox's Morton order to rows"""
    out = bytearray(width * height * size)
    x_masks, y_masks = [], []
    bit = 1
    w, h = width, height
    while w > 1 or h > 1:
        if w > 1:
            x_masks.append(bit)
            bit <<= 1
            w >>= 1
        if h > 1:
            y_masks.append(bit)
            bit <<= 1
            h >>= 1

    def spread(value, masks):
        result = 0
        for index, mask in enumerate(masks):
            if value & (1 << index):
                result |= mask
        return result

    xs = [spread(x, x_masks) for x in range(width)]
    for y in range(height):
        row = spread(y, y_masks)
        for x in range(width):
            source = (row | xs[x]) * size
            target = (y * width + x) * size
            out[target:target + size] = pixels[source:source + size]
    return bytes(out)


def image(cache: CacheFile, bitmap) -> Image.Image:
    width, height, kind, format, flags, offset, _ = bitmap
    name, size = FORMATS[format]
    if name.startswith("DXT"):
        count = max(1, (width + 3) // 4) * max(1, (height + 3) // 4) * size
        pixels = cache.data[offset:offset + count]
        return Image.frombytes("RGBA", (width, height), pixels, "bcn", {"DXT1": 1, "DXT3": 2, "DXT5": 3}[name])
    pixels = cache.data[offset:offset + width * height * size]
    if flags & SWIZZLED_FLAG:
        pixels = unswizzle(pixels, width, height, size)
    if name in ("A8R8G8B8", "X8R8G8B8"):
        result = Image.frombytes("RGBA", (width, height), pixels, "raw", "BGRA")
        if name == "X8R8G8B8":
            result.putalpha(255)
        return result
    if name == "R5G6B5":
        return Image.frombytes("RGB", (width, height), pixels, "raw", "BGR;16")
    if name in ("A1R5G5B5", "A4R4G4B4"):
        value = numpy.frombuffer(pixels, dtype="<u2").reshape(height, width).astype(numpy.uint32)
        if name == "A1R5G5B5":
            channels = [(value >> 10) & 31, (value >> 5) & 31, value & 31]
            channels = [c * 255 // 31 for c in channels] + [((value >> 15) & 1) * 255]
        else:
            channels = [((value >> shift) & 15) * 17 for shift in (8, 4, 0, 12)]
        return Image.fromarray(numpy.dstack(channels).astype(numpy.uint8), "RGBA")
    if name == "Y8":
        return Image.frombytes("L", (width, height), pixels)
    if name == "A8":
        return Image.merge("RGBA", [Image.new("L", (width, height), 255)] * 3 + [Image.frombytes("L", (width, height), pixels)])
    if name == "AY8":
        grey = Image.frombytes("L", (width, height), pixels)
        return Image.merge("RGBA", [grey, grey, grey, grey])
    if name == "A8Y8":
        return Image.frombytes("LA", (width, height), pixels, "raw", "LA").convert("RGBA")
    raise ValueError(f"format {name} is not read")


def main():
    command, path = sys.argv[1], Path(sys.argv[2])
    cache = CacheFile(path)
    if command == "list":
        pattern = sys.argv[3] if len(sys.argv) > 3 else "*"
        for name, base in cache.bitmap_tags():
            if fnmatch.fnmatch(name, pattern):
                shapes = ", ".join(f"{b[0]}x{b[1]} {FORMATS.get(b[3], ('?',))[0]}" for b in cache.bitmaps(base))
                print(f"{name}: {shapes}")
    elif command == "extract":
        out = Path(sys.argv[3])
        patterns = sys.argv[4:] or ["*"]
        for name, base in cache.bitmap_tags():
            if not any(fnmatch.fnmatch(name, pattern) for pattern in patterns):
                continue
            for index, bitmap in enumerate(cache.bitmaps(base)):
                if bitmap[2] != TYPE_2D or bitmap[3] not in FORMATS:
                    continue
                target = out / (name.replace("\\", "/") + (f"_{index}" if index else "") + ".png")
                target.parent.mkdir(parents=True, exist_ok=True)
                image(cache, bitmap).save(target)
                print(target)


if __name__ == "__main__":
    main()
