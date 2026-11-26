"""A joiner keeps the names of its host's settings record (the game's, every machine's and every player's) to text that
draws, as the host keeps the names joiners send it."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ['dirty', 'unterminated', 'every-name', 'clean-stays']
CONTROLS = [
    ('player_name_clean(game->name, NUMBEROF(game->name));', ';', 'dirty'),
    ('player_name_clean(game->machines[index].name', 'if (0) player_name_clean(game->machines[index].name', 'every-name'),
    ('player_name_clean(game->players[index].name', 'if (0) player_name_clean(game->players[index].name', 'every-name'),
]


def generated(fault=None):
    # (the desktop builds' tables, halo_port_limits.h)
    config = '#define MACHINES 16\n#define PLAYERS 16\n'
    players = read('source/game/players.c')
    code = '\n'.join(function(players, name) for name in ('player_name_character_ascii', 'player_name_clean'))
    code += '\n' + function(read('source/networking/network_client_manager.c'), 'network_game_client_clean_record_names')
    if fault:
        code = mutated(code, *fault[:2])
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('joiner_record_names', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('joiner_record_names', generated((before, after))), case)
    assert status == CHECK_FAILED, output
