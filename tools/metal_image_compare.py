"""Compare ChupathingyCE's debug.screenshot_every BMPs without extra packages.

Both renderers emit 32-bit uncompressed BMPs. Alpha is scratch state, so only
RGB is compared. Dimensions must match; there is no rescaling, alignment or
masking that could hide rendering regressions. A diff BMP and JSON report are
written even when tolerances fail. Used by the GL/Metal capture runner.
"""
import argparse
import json
import math
import struct
from dataclasses import dataclass
from pathlib import Path

MAX_PIXELS = 64 * 1024 * 1024


@dataclass(frozen=True)
class Image:
    width: int
    height: int
    bgra: bytes  # tightly packed rows from the top


def read_bmp(path: Path) -> Image:
    size = path.stat().st_size
    if size < 54 or size > MAX_PIXELS * 4 + 4096:
        raise ValueError(f"Invalid BMP size: {path}")
    with path.open("rb") as stream:
        header = stream.read(54)
        if header[:2] != b"BM":
            raise ValueError(f"Not a BMP: {path}")
        offset, dib = struct.unpack_from("<II", header, 10)
        width, height, planes, bits, compression = struct.unpack_from("<iiHHI", header, 18)
        if dib < 40 or offset < 14 + dib or planes != 1 or bits != 32 or compression != 0:
            raise ValueError(f"Expected uncompressed 32-bit BMP: {path}")
        if width <= 0 or height == 0 or width * abs(height) > MAX_PIXELS:
            raise ValueError(f"Invalid BMP dimensions: {path}")
        stride = width * 4
        length = stride * abs(height)
        if offset > size or length > size - offset:
            raise ValueError(f"Truncated BMP: {path}")
        stream.seek(offset)
        pixels = stream.read(length)
        if len(pixels) != length:
            raise ValueError(f"Truncated BMP pixels: {path}")
    if height > 0:
        pixels = b"".join(pixels[row:row + stride] for row in range(length - stride, -1, -stride))
    return Image(width, abs(height), pixels)


def write_bmp(path: Path, image: Image) -> None:
    if image.width <= 0 or image.height <= 0 or image.width * image.height > MAX_PIXELS:
        raise ValueError("Invalid image dimensions")
    if len(image.bgra) != image.width * image.height * 4:
        raise ValueError("Invalid image storage")
    header = bytearray(54)
    struct.pack_into("<2sI4xI", header, 0, b"BM", 54 + len(image.bgra), 54)
    struct.pack_into("<IiiHHII", header, 14, 40, image.width, -image.height, 1, 32, 0, len(image.bgra))
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as stream:
        stream.write(header)
        stream.write(image.bgra)


def compare(reference: Image, actual: Image, *, pixel_threshold: int = 16,
            mean_limit: float = 3.0, fraction_limit: float = 0.03) -> tuple[dict, Image]:
    if (reference.width, reference.height) != (actual.width, actual.height):
        raise ValueError("Image dimensions differ; refusing to resize")
    if not 0 <= pixel_threshold <= 255 or not math.isfinite(mean_limit) or not 0 <= mean_limit <= 255:
        raise ValueError("Invalid pixel/mean tolerance")
    if not math.isfinite(fraction_limit) or not 0 <= fraction_limit <= 1:
        raise ValueError("Invalid fraction tolerance")
    count = reference.width * reference.height
    if count <= 0 or count > MAX_PIXELS or len(reference.bgra) != count * 4 or len(actual.bgra) != count * 4:
        raise ValueError("Invalid image storage")
    difference = bytearray(count * 4)
    total = square = bad = 0
    for offset in range(0, count * 4, 4):
        maximum = 0
        for channel in range(3):
            error = abs(reference.bgra[offset + channel] - actual.bgra[offset + channel])
            difference[offset + channel] = error
            total += error
            square += error * error
            maximum = max(maximum, error)
        difference[offset + 3] = 255
        bad += maximum > pixel_threshold
    mean = total / (count * 3)
    fraction = bad / count
    report = {"width": reference.width, "height": reference.height,
              "mean_rgb_error_255": mean, "rms_rgb_error_255": math.sqrt(square / (count * 3)),
              "bad_pixel_fraction": fraction, "pixel_threshold_255": pixel_threshold,
              "mean_limit_255": mean_limit, "fraction_limit": fraction_limit,
              "passed": mean <= mean_limit and fraction <= fraction_limit}
    return report, Image(reference.width, reference.height, bytes(difference))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("actual", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--pixel-threshold", type=int, default=16)
    parser.add_argument("--mean-limit", type=float, default=3.0)
    parser.add_argument("--fraction-limit", type=float, default=0.03)
    args = parser.parse_args()
    report, diff = compare(read_bmp(args.reference), read_bmp(args.actual),
                           pixel_threshold=args.pixel_threshold, mean_limit=args.mean_limit,
                           fraction_limit=args.fraction_limit)
    args.output.mkdir(parents=True, exist_ok=True)
    write_bmp(args.output / "difference.bmp", diff)
    (args.output / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
