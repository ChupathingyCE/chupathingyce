"""Checks how a 64 MB console starts a Custom Edition map's large bitmaps at
their next level (port/linux/game/ce_bitmap_reduce.h, ce_repairs.c). The
header is compiled alone with the build machine's C compiler and run on
sample bitmaps: the DXT, P8 and 32-bit formats lose their top level (a
2048x2048 one of more than 2 MB a level two), and one-level, cube, 3D and
linear bitmaps, sprites, interface bitmaps and the ones a HUD or menu tag
refers to keep every level."""
import re
import shutil
import struct
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "port" / "linux" / "game" / "ce_bitmap_reduce.h"

# one bitmap per line on stdin: group type, interface drawn, then the 48
# bytes as hex; out: the levels dropped and the 48 bytes after
HARNESS = r"""
#include <stdio.h>
#include "ce_bitmap_reduce.h"
int main(void)
{
	int group_type, interface_drawn;
	char hex[2 * CE_BITMAP_SIZE + 1];
	while (scanf("%d %d %96s", &group_type, &interface_drawn, hex) == 3)
	{
		unsigned char bitmap[CE_BITMAP_SIZE];
		int index, drops;
		for (index = 0; index < CE_BITMAP_SIZE; index++)
		{
			unsigned int byte;
			sscanf(hex + 2 * index, "%2x", &byte);
			bitmap[index] = (unsigned char)byte;
		}
		drops = ce_bitmap_reduce(bitmap, (short)group_type, interface_drawn);
		printf("%d ", drops);
		for (index = 0; index < CE_BITMAP_SIZE; index++)
			printf("%02x", bitmap[index]);
		printf("\n");
	}
	return 0;
}
"""

# bitmaps.c's formats and types, bitmap_extract.c's group types
A8, R5G6B5, A8R8G8B8, DXT1, DXT3, DXT5, P8 = 0, 6, 11, 14, 15, 16, 17
TYPE_2D, TYPE_3D, TYPE_CUBE = 0, 1, 2
TEXTURES_2D, TEXTURES_3D, CUBE_MAPS, SPRITES, INTERFACE_BITMAPS = 0, 1, 2, 3, 4
COMPRESSED, LINEAR = 0x02, 0x10
BITS = {A8: 8, R5G6B5: 16, A8R8G8B8: 32, DXT1: 4, DXT3: 8, DXT5: 8, P8: 8}
LAYOUT = "<4shhhhhHhhhhiiii8x"


def level_bytes(width, height, fmt, flags, depth=1, faces=1):
    if flags & COMPRESSED:
        width += -width & 3
        height += -height & 3
    return width * height * depth * faces * BITS[fmt] // 8


def chain_bytes(width, height, fmt, flags, mipmaps, depth=1, faces=1):
    return sum(level_bytes(max(1, width >> level), max(1, height >> level), fmt, flags, max(1, depth >> level),
                           faces) for level in range(mipmaps + 1))


def bitmap(width, height, fmt, mipmaps, flags=0, kind=TYPE_2D, depth=1, registration=(0, 0), offset=4096):
    faces = 6 if kind == TYPE_CUBE else 1
    size = chain_bytes(width, height, fmt, flags, mipmaps, depth, faces)
    return dict(width=width, height=height, depth=depth, type=kind, format=fmt, flags=flags,
                registration=registration, mipmaps=mipmaps, offset=offset, size=size)


def pack(b):
    return struct.pack(LAYOUT, b"mtib", b["width"], b["height"], b["depth"], b["type"], b["format"], b["flags"],
                       b["registration"][0], b["registration"][1], b["mipmaps"], 0, b["offset"], b["size"], 0, 0)


def unpack(data):
    fields = struct.unpack(LAYOUT, data)
    return dict(width=fields[1], height=fields[2], depth=fields[3], type=fields[4], format=fields[5],
                flags=fields[6], registration=(fields[7], fields[8]), mipmaps=fields[9], offset=fields[11],
                size=fields[12])


@pytest.fixture(scope="module")
def reduce(tmp_path_factory):
    cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    directory = tmp_path_factory.mktemp("ce_bitmap_reduce")
    source = directory / "harness.c"
    source.write_text(HARNESS)
    binary = directory / "harness"
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(HEADER.parent), str(source), "-o",
                    str(binary)], check=True)

    def run(b, group_type=TEXTURES_2D, interface_drawn=False):
        line = "%d %d %s\n" % (group_type, int(interface_drawn), pack(b).hex())
        out = subprocess.run([str(binary)], input=line, capture_output=True, text=True, check=True).stdout.split()
        return int(out[0]), unpack(bytes.fromhex(out[1]))
    return run


def assert_dropped(before, after, levels):
    dropped = sum(level_bytes(max(1, before["width"] >> level), max(1, before["height"] >> level),
                              before["format"], before["flags"]) for level in range(levels))
    assert after["width"] == max(1, before["width"] >> levels)
    assert after["height"] == max(1, before["height"] >> levels)
    assert after["mipmaps"] == before["mipmaps"] - levels
    assert after["offset"] == before["offset"] + dropped
    assert after["size"] == before["size"] - dropped
    # what is left is the smaller bitmap's whole chain, as the file has it
    assert after["size"] == chain_bytes(after["width"], after["height"], after["format"], after["flags"],
                                        after["mipmaps"])
    assert after["format"] == before["format"] and after["flags"] == before["flags"]


@pytest.mark.parametrize("fmt", [DXT1, DXT3, DXT5])
def test_dxt_loses_its_top_level(reduce, fmt):
    before = bitmap(1024, 1024, fmt, 10, COMPRESSED)
    drops, after = reduce(before)
    assert drops == 1
    assert_dropped(before, after, 1)


def test_p8_loses_its_top_level(reduce):
    before = bitmap(512, 512, P8, 9)
    drops, after = reduce(before)
    assert drops == 1
    assert_dropped(before, after, 1)


def test_wide_bitmap_of_as_many_pixels(reduce):
    # 1024x256 has 512x512's pixels
    before = bitmap(1024, 256, DXT1, 10, COMPRESSED)
    drops, after = reduce(before)
    assert drops == 1
    assert (after["width"], after["height"]) == (512, 128)


def test_large_32_bit_loses_two(reduce):
    # its top level is 16 MB, its next 4 MB: still over 2 MB
    before = bitmap(2048, 2048, A8R8G8B8, 11)
    drops, after = reduce(before)
    assert drops == 2
    assert_dropped(before, after, 2)


def test_lightmap_loses_one(reduce):
    # Coagulation's 2048x2048 lightmap: 8 MB, then 2 MB, which is not over
    before = bitmap(2048, 2048, R5G6B5, 11)
    drops, after = reduce(before)
    assert drops == 1
    assert after["size"] < 3 * 1024 * 1024


def test_registration_point_halved(reduce):
    drops, after = reduce(bitmap(512, 512, A8R8G8B8, 9, registration=(256, 100)))
    assert drops == 1
    assert after["registration"] == (128, 50)


def test_small_bitmap_kept(reduce):
    drops, after = reduce(bitmap(256, 256, A8R8G8B8, 8))
    assert drops == 0


def test_one_level_kept(reduce):
    before = bitmap(1024, 1024, DXT1, 0, COMPRESSED)
    drops, after = reduce(before)
    assert drops == 0
    assert after == before


def test_cube_map_kept(reduce):
    before = bitmap(512, 512, DXT1, 9, COMPRESSED, kind=TYPE_CUBE)
    drops, after = reduce(before, CUBE_MAPS)
    assert drops == 0
    assert after == before


def test_3d_kept(reduce):
    before = bitmap(512, 512, A8, 9, kind=TYPE_3D, depth=4)
    drops, after = reduce(before, TEXTURES_3D)
    assert drops == 0
    assert after == before


def test_linear_kept(reduce):
    before = bitmap(1024, 512, A8R8G8B8, 0, LINEAR)
    before["mipmaps"] = 1
    drops, after = reduce(before)
    assert drops == 0
    assert after == before


@pytest.mark.parametrize("group_type", [SPRITES, INTERFACE_BITMAPS])
def test_sprites_and_interface_bitmaps_kept(reduce, group_type):
    # coldsnap's player\hud\bitmaps\meters: a 512x512 sprite sheet of 2 levels
    before = bitmap(512, 512, A8R8G8B8, 2)
    drops, after = reduce(before, group_type)
    assert drops == 0
    assert after == before


def test_hud_referenced_kept(reduce):
    before = bitmap(1024, 1024, A8R8G8B8, 10)
    drops, after = reduce(before, TEXTURES_2D, interface_drawn=True)
    assert drops == 0
    assert after == before


def test_format_of_none_kept(reduce):
    # (format 4 has no pixels in bitmaps.c's table; 30 is past the formats)
    for fmt in (4, 30):
        before = bitmap(1024, 1024, A8R8G8B8, 10)
        before["format"] = fmt
        drops, after = reduce(before)
        assert drops == 0
        assert after == before


def test_pixels_short_of_the_top_level_kept(reduce):
    before = bitmap(1024, 1024, DXT1, 10, COMPRESSED)
    before["size"] = level_bytes(1024, 1024, DXT1, COMPRESSED)
    drops, after = reduce(before)
    assert drops == 0
    assert after == before


def test_bits_match_bitmaps_c():
    text = (ROOT / "source" / "bitmaps" / "bitmaps.c").read_text()
    table = re.search(r"bitmap_format_bits_per_pixel_table\[[^\]]*\]\s*=\s*\{(.*?)\};", text, re.S).group(1)
    game = [int(value) for value in re.findall(r"-?\d+", table)]
    header = HEADER.read_text()
    ours = [int(value) for value in re.search(r"ce_bitmap_format_bits\[[^\]]*\]\s*=\s*\{(.*?)\};", header,
                                                re.S).group(1).split(",") if value.strip()]
    assert ours == game[:len(ours)] and len(ours) == 18


def test_repairs_log_reduced_only_when_any():
    text = (ROOT / "port" / "linux" / "game" / "ce_repairs.c").read_text()
    log = re.search(r"static void ce_repairs_log\(.*?\n\}", text, re.S).group(0)
    assert re.search(r"if \(counts->bitmaps_reduced > 0\)\s*snprintf\(reduced", log)
    assert "without their top levels" not in log.split("error(_error_silent")[1]


def test_setting_is_the_xbox_s():
    text = (ROOT / "port" / "linux" / "src" / "port_config.c").read_text()
    assert re.search(r'\{ "console\.ce_reduce_bitmaps", _config_boolean, "true", NULL, _environment_value, '
                     r'_platform_xbox,', text)
    repairs = (ROOT / "port" / "linux" / "game" / "ce_repairs.c").read_text()
    assert 'platform_ce_alias && config_boolean("console.ce_reduce_bitmaps")' in repairs
