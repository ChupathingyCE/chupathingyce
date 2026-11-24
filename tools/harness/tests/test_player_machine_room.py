"""A machine with no room for another player is refused one, rather than the game's process ending."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run  # noqa: E402

CASES = ['four', 'fifth', 'fifth-at-slot', 'machine-past-table', 'room-again']
CONTROLS = [
    ('if (!machine_add_player(machine_index, player_index))', 'machine_add_player(machine_index, player_index);\n\tif (0)',
     'fifth'),
    ('if (player_index != NONE)\n\t\t\tplayer_delete(player_index);', ';', 'fifth'),
    ('DATUM_INDEX_TO_ABSOLUTE_INDEX(machine_index) >= MAXIMUM_NETWORK_MACHINE_COUNT', 'FALSE',
     'machine-past-table'),
]


def generated(fault=None):
    source = read('source/game/players.c')
    config = '#define MAXIMUM_LOCAL_PLAYERS %d\n' % constant(read('source/game/players.h'), 'MAXIMUM_LOCAL_PLAYERS')
    config += '#define MAXIMUM_NUMBER_OF_LOCAL_PLAYERS %d\n' % constant(
        read('source/networking/network_connection.h'), 'MAXIMUM_NUMBER_OF_LOCAL_PLAYERS')
    # (the consoles' table, halo_port_limits.h: the cases need only a few machines)
    config += '#define MAXIMUM_NETWORK_MACHINE_COUNT 16\n'
    code = '\n'.join(function(source, name) for name in
                     ('machine_get_player_list', 'machine_add_player', 'machine_remove_player', 'player_new'))
    if fault:
        code = mutated(code, *fault[:2])
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('player_machine_room', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('player_machine_room', generated((before, after))), case)
    assert status == CHECK_FAILED, output
