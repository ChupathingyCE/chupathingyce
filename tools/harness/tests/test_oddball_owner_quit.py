"""Oddball with the ball passed by killing: an owner who quit, and whose slot was reused, marks no goal (#1325)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run  # noqa: E402

CASES = ['live', 'none', 'quit', 'reused']
CONTROLS = [
    # the owner looked up as before the fix: player_get asserts on the reused slot
    ('player_try_and_get(owner_player_index)', 'player_get(owner_player_index)', 'reused'),
    ('player_try_and_get(owner_player_index)', 'player_get(owner_player_index)', 'quit'),
]


def generated(fault=None):
    source = read('source/game/game_engine_oddball.c')
    config = '#define MAXIMUM_ODDBALLS %d\n' % constant(read('source/game/game_engine.h'), 'MAXIMUM_ODDBALLS')
    config += '#define ODDBALL_START_ANNOUNCEMENT_TICKS 60\n'
    code = function(source, 'oddball_engine_update')
    if fault:
        code = mutated(code, *fault[:2])
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('oddball_owner_quit', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('oddball_owner_quit', generated((before, after))), case)
    assert status == CHECK_FAILED, output
