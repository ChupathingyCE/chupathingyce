"""A screen effect bitmap's scale to its texture's coordinates (rasterizer_xbox_screen_effect.c): a Custom Edition
map's linear zoom mask sampled from 0 to 1 as Halo PC samples it, the screen's own copy and an Xbox map's linear
bitmaps in texels."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run

CASES = ['ce-linear-mask', 'ce-linear-mask-scaled', 'swizzled', 'screen-copy', 'xbox-linear']
FUNCTION = 'rasterizer_screen_effect_bitmap_scale'
CONTROLS = [
    # every linear bitmap in texels, as before Custom Edition maps' were sampled from 0 to 1
    ('linear && (screen_copy || !custom_edition_map)', 'linear', 'ce-linear-mask'),
    ('linear && (screen_copy || !custom_edition_map)', 'linear', 'ce-linear-mask-scaled'),
    # the screen's copy sampled from 0 to 1 on a Custom Edition map
    ('(screen_copy || !custom_edition_map)', '(!custom_edition_map)', 'screen-copy'),
    # the mask's size left unscaled
    ('*size_i *= mask_scale;', '', 'ce-linear-mask-scaled'),
]


def generated(fault=None):
    code = function(read('source/rasterizer/xbox/rasterizer_xbox_screen_effect.c'), FUNCTION)
    if fault:
        code = mutated(code, fault[0], fault[1])
    return (('under_test.inc', code + '\n'),)


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('screen_effect_bitmap_scale', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('screen_effect_bitmap_scale', generated((before, after))), case)
    assert status == CHECK_FAILED, output
