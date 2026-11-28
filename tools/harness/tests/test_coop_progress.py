"""Network co-op's campaign progress (port/linux/game/coop_progress.c): the host records the level it won, a joiner
only when the host's lobby comes back on the campaign's next level; a restart, another map or another game records
nothing."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, mutated, read, run  # noqa: E402

CASES = ['host', 'joiner-won', 'joiner-maw', 'joiner-restart', 'joiner-other', 'joiner-no-round-end',
         'joiner-other-game', 'joiner-left']
CONTROLS = [
    ('== (level + 1) % NUMBER_OF_SINGLE_PLAYER_LEVELS', '!= NONE', 'joiner-restart'),
    ('if (!coop_progress.round_ended)\n\t\treturn;', '', 'joiner-no-round-end'),
    ('coop_progress.client != global_network_game_client_get() || !map_name', '!map_name', 'joiner-other-game'),
]


def generated(fault=None):
    profile = read('source/saved games/player_profile.h')
    config = '#define NUMBER_OF_SINGLE_PLAYER_LEVELS %d\n' % constant(profile, 'NUMBER_OF_SINGLE_PLAYER_LEVELS')
    # (game.h's enum: the difficulties before its count)
    game = read('source/game/game.h')
    names = re.search(r'enum\s*\{([^}]*NUMBER_OF_GAME_DIFFICULTY_LEVELS)', game).group(1)
    config += '#define NUMBER_OF_GAME_DIFFICULTY_LEVELS %d\n' % len(re.findall(r'_game_difficulty_level_\w+', names))
    source = read('port/linux/game/coop_progress.c')
    code = source[source.index('static struct\n{'):]
    if fault:
        code = mutated(code, *fault[:2])
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('coop_progress', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('coop_progress', generated((before, after))), case)
    assert status == CHECK_FAILED, output
