"""A scope's zoom and a cutscene's blur at any resolution (rasterizer_xbox_screen_effect.c): few extra passes, copies
of the screen that blend instead of ghosting, and a blur as wide as the Xbox's."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run

CASES = ['xbox-size', 'radii', 'few', 'no-ghosts', 'as-wide']
FUNCTIONS = ('rasterizer_screen_effect_extra_passes', 'rasterizer_screen_effect_pass_radius')
CONTROLS = [
    ('radius /= 3.0f', 'radius /= 2.0f', 'rasterizer_screen_effect_pass_radius', 'no-ghosts'),
    ('extra <= pass', 'extra < pass', 'rasterizer_screen_effect_pass_radius', 'radii'),
    ('passes += 2', 'passes += 0', 'rasterizer_screen_effect_extra_passes', 'no-ghosts'),
    ('screen_scale > covered', 'screen_scale >= covered', 'rasterizer_screen_effect_extra_passes', 'xbox-size'),
    ('passes < 4', 'passes < 12', 'rasterizer_screen_effect_extra_passes', 'few'),
    ('passes += 2', 'passes += 1', 'rasterizer_screen_effect_extra_passes', 'few'),
]


def generated(fault=None):
    source = read('source/rasterizer/xbox/rasterizer_xbox_screen_effect.c')
    text = ''
    for name in FUNCTIONS:
        code = function(source, name)
        if fault and name == fault[2]:
            code = mutated(code, fault[0], fault[1])
        text += code + '\n'
    return (('under_test.inc', text),)


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('screen_effect_passes', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,function_name,case', CONTROLS)
def test_negative_control(before, after, function_name, case):
    status, output = run(build('screen_effect_passes', generated((before, after, function_name))), case)
    assert status == CHECK_FAILED, output
