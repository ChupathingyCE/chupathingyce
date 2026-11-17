"""A rotation naming a map the host hasn't got skips it, and never takes the host down (#1106).

The host's map change (source/networking/network_server_manager.c) keeps the game's map when this machine lacks the
new one; the test host's map list (port/linux/game/network_test.c) skips such a map to the next installed one; a
dedicated server (server/src/dedicated.c) whose playlist has no installed map hosts nothing and says so once."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run  # noqa: E402

CASES = ['middle-skipped', 'last-missing-holds', 'none-installed', 'host-keeps-its-map', 'playlist-none-playable',
         'playlist-playable']
CONTROLS = {
    # the old behavior: the host sets a game on any map it is given
    'host-sets-missing-map': ('if (!cache_files_map_installed(map_name))\n\t{',
                              'if (0)\n\t{', 'host-keeps-its-map'),
    # the test host's list not skipping a missing map
    'list-not-skipped': ('entry < count; entry++)\n\t{\n\t\tnetwork_test_map_path(entry, path, size);\n'
                         '\t\tif (cache_files_map_installed(path))',
                         'entry < count; entry++)\n\t{\n\t\tnetwork_test_map_path(entry, path, size);\n\t\tif (1)',
                         'middle-skipped'),
    # the list wrapping to its first map past its end
    'list-wraps': ('for (entry = network_test.map_entry - 1; entry >= 0; entry--)',
                   'for (entry = 0; entry < 0; entry++)', 'last-missing-holds'),
    # the refusal not told to the caller
    'refusal-hidden': ('\t\treturn FALSE;\n\t}\n\t/* port: a lobby moving to or from an MCC map',
                       '\t\treturn TRUE;\n\t}\n\t/* port: a lobby moving to or from an MCC map', 'host-keeps-its-map'),
    # a dedicated server starting a playlist with nothing to play
    'playlist-started-anyway': ('\tdedicated.none_playable = TRUE;\n\treturn FALSE;\n}',
                                '\tdedicated.none_playable = TRUE;\n\treturn TRUE;\n}', 'playlist-none-playable'),
    # the refusal said on every look
    'refusal-repeated': ('\tif (!dedicated.none_playable)\n\t{\n\t\terror(', '\tif (1)\n\t{\n\t\terror(',
                         'playlist-none-playable'),
}


def generated(control=None):
    server = read('source/networking/network_server_manager.c')
    test = read('port/linux/game/network_test.c')
    dedicated = read('server/src/dedicated.c')
    code = '\n'.join(
        [f"#define NONE_PLAYABLE_MILLISECONDS {constant(dedicated, 'NONE_PLAYABLE_MILLISECONDS')}"] +
        [function(test, name) for name in ('network_test_map', 'network_test_map_count', 'network_test_map_path',
                                           'network_test_playable_map')] +
        [function(server, 'network_game_server_change_map_name'), function(dedicated, 'playlist_playable')])
    if control:
        before, after = CONTROLS[control][:2]
        code = mutated(code, before, after)
    return (('config.inc', ''), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('rotation_missing_maps', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('control', CONTROLS)
def test_negative_control(control):
    status, output = run(build('rotation_missing_maps', generated(control)), CONTROLS[control][2])
    assert status == CHECK_FAILED, f'{control} was not caught: {output}'
