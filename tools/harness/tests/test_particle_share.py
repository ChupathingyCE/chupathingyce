"""An effect's split screen particle share with no local player divides nothing by 0."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run  # noqa: E402

CASES = ['one', 'two', 'none', 'few']


def block(source, start):
    """The statement at start, with its braces matched."""
    at = source.index(start)
    end = source.index('{', at) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[at:end]


def generated(fault=None):
    source = read('source/effects/effects.c')
    config = '#define MAXIMUM_SPLIT_SCREEN_PARTICLE_COUNT %d\n' % constant(source, 'MAXIMUM_SPLIT_SCREEN_PARTICLE_COUNT')
    code = block(function(source, 'effect_update'), 'if (count > MAXIMUM_SPLIT_SCREEN_PARTICLE_COUNT)')
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('particle_share', generated()), case)
    assert status == 0, output


def test_negative_control():
    # the share computed with no local player too, as before the fix
    status, output = run(build('particle_share', generated(('if (local_player_count() <= 0)', 'if (0)'))), 'none')
    assert status == CHECK_FAILED, output
