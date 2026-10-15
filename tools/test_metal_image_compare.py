"""Image comparison contracts: format validation and non-vacuous thresholds."""
import struct
import tempfile
from pathlib import Path
from unittest import TestCase

from tools.metal_image_compare import Image, compare, read_bmp, write_bmp


class MetalImageCompareTests(TestCase):
    def test_round_trip_and_bottom_up(self):
        image = Image(1, 2, bytes([1, 2, 3, 4, 5, 6, 7, 8]))
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "frame.bmp"
            write_bmp(path, image)
            self.assertEqual(read_bmp(path), image)
            data = bytearray(path.read_bytes())
            struct.pack_into("<i", data, 22, 2)
            data[54:] = image.bgra[4:] + image.bgra[:4]
            path.write_bytes(data)
            self.assertEqual(read_bmp(path), image)

    def test_alpha_ignored_and_differences_visible(self):
        reference = Image(2, 1, bytes([10, 20, 30, 0, 40, 50, 60, 1]))
        actual = Image(2, 1, bytes([10, 20, 30, 255, 40, 50, 60, 255]))
        report, diff = compare(reference, actual)
        self.assertTrue(report["passed"])
        self.assertEqual(report["mean_rgb_error_255"], 0)
        self.assertEqual(diff.bgra, bytes([0, 0, 0, 255] * 2))
        report, diff = compare(reference, Image(2, 1, bytes([255] * 8)))
        self.assertFalse(report["passed"])
        self.assertEqual(report["bad_pixel_fraction"], 1)
        self.assertEqual(diff.bgra[:3], bytes([245, 235, 225]))

    def test_tolerances_are_independent(self):
        dark = Image(10, 1, bytes([0, 0, 0, 255] * 10))
        slight = Image(10, 1, bytes([4, 4, 4, 255] * 10))
        self.assertFalse(compare(dark, slight)[0]["passed"])  # mean fails
        spot = Image(10, 1, bytes([20, 0, 0, 255] + [0, 0, 0, 255] * 9))
        self.assertFalse(compare(dark, spot)[0]["passed"])  # fraction fails
        self.assertTrue(compare(dark, spot, fraction_limit=0.1)[0]["passed"])

    def test_dimensions_storage_and_nan_rejected(self):
        with self.assertRaisesRegex(ValueError, "dimensions"):
            compare(Image(1, 1, b"1234"), Image(1, 2, b"12345678"))
        with self.assertRaisesRegex(ValueError, "storage"):
            compare(Image(1, 1, b"1"), Image(1, 1, b"1234"))
        with self.assertRaisesRegex(ValueError, "tolerance"):
            compare(Image(1, 1, b"1234"), Image(1, 1, b"1234"), mean_limit=float("nan"))

    def test_bad_bmp_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "bad.bmp"
            path.write_bytes(b"BM")
            with self.assertRaises(ValueError):
                read_bmp(path)
            write_bmp(path, Image(1, 1, bytes([1, 2, 3, 4])))
            good = path.read_bytes()
            for offset, value in ((0, b"NO"), (28, struct.pack("<H", 24)),
                                  (30, struct.pack("<I", 3)), (18, struct.pack("<i", -1)),
                                  (10, struct.pack("<I", 0xFFFFFFFF))):
                bad = bytearray(good)
                bad[offset:offset + len(value)] = value
                path.write_bytes(bad)
                with self.assertRaises(ValueError):
                    read_bmp(path)
            path.write_bytes(good[:-1])
            with self.assertRaises(ValueError):
                read_bmp(path)
