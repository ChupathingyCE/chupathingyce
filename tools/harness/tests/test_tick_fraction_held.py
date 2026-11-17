"""The frames drawn while a client's clock waits for its host stay on the last tick (source/game/game_time.c, #477)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ['local-blends', 'held-stays', 'released-blends']
CONTROLS = {
    # the fraction runs on toward a tick that never comes (the judder)
    'no-wait': ('\tif (game_time_ticks_waiting)\n\t\treturn 1.0f;\n', '', 'held-stays'),
    # the wait never ends once it started
    'stuck': ('game_time_ticks_waiting = maximum_possible_server_time <= game_time_globals->server_time;',
              'game_time_ticks_waiting |= maximum_possible_server_time <= game_time_globals->server_time;',
              'released-blends'),
}


def generated(control=None):
    source = read('source/game/game_time.c')
    connections = enum_with(read('source/game/game.h'), '_game_connection_local')
    waiting = re.search(r"^static boolean game_time_ticks_waiting = FALSE;", source, re.M).group(0)
    code = '\n'.join([
        'enum { SOME_LARGE_NUMBER_OF_TICKS = %d };' % constant(source, 'SOME_LARGE_NUMBER_OF_TICKS'),
        structure(source, 'game_time_globals_struct'),
        'static struct game_time_globals_struct *game_time_globals;',
        waiting,
    ] + [function(source, name) for name in (
        'game_time_initialize_for_new_map', 'game_time_held', 'game_time_get_speed', 'game_time_get_tick_fraction',
        'game_time_update')])
    if control:
        before, after = CONTROLS[control][:2]
        code = mutated(code, before, after)
    return (('connections.inc', connections), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('tick_fraction_held', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('control', CONTROLS)
def test_negative_control(control):
    status, output = run(build('tick_fraction_held', generated(control)), CONTROLS[control][2])
    assert status == CHECK_FAILED, f'{control} was not caught: {output}'
