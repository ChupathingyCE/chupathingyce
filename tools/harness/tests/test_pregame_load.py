"""A machine that joined the last game in progress gets a new lobby like the others, and one machine
whose map never loads holds the lobby's start no longer than network.pregame_load_timeout (#1049;
source/networking/network_server_manager.c)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, ROOT, build, enum_with, function, mutated, read, run  # noqa: E402

MANAGER = 'source/networking/network_server_manager.c'
CASES = ['state-reset', 'late-joiner-reports', 'late-joiner-silent', 'timeout-off', 'timeout-setting',
         'countdown-not-over', 'host-never-dropped']
FUNCTIONS = (
    'network_game_server_client_machine_is_joined_to_game', 'network_game_server_machine_has_players',
    'network_game_server_machine_player_count', 'network_game_server_late_join_wait_milliseconds',
    'network_game_server_start_late_joiner', 'network_game_server_late_joiner_loaded',
    'network_game_server_reset_to_pregame', 'network_game_server_client_machine_is_precached',
    'network_game_server_have_all_machines_have_precached', 'network_game_server_pregame_load_timeout_milliseconds',
    'network_game_server_pregame_load_timeout')
STATICS = ('late_joiner_players', 'network_game_server_late_joiner_waiting',
           'network_game_server_late_joiner_first_added', 'network_game_server_frequent_updates_until',
           'network_game_server_pregame_load_held_since')
STRUCTURES = ('message_server_begin_game', 'message_server_remove_player_ingame',
              'message_server_graceful_game_exit_pregame', 'message_server_switch_to_pregame')
CONTROLS = {
    # no drop: the lobby waits for ever, as on HCH 1
    'no-drop': ('\t\tif (!network_game_server_drop_client_machine(server, machine))\n'
                '\t\t\tnetwork_event("failed to remove client machine %x from game", machine_index);\n', '',
                'late-joiner-silent'),
    # the late joiner's report of the last lobby still counts in the next
    'report-kept': ('\t\t\t\t\t_network_client_machine_precached_bit,\n\t\t\t\t\tFALSE);', '\t\t\t\t\t31,\n\t\t\t\t\tFALSE);',
                    'state-reset'),
    # the wait goes on through a pause, so a machine is dropped early
    'wait-not-restarted': ('if (!held || !timeout ||', 'if (!timeout ||', 'countdown-not-over'),
    # the host's own machine dropped
    'host-dropped': ('\t\t\tnetwork_game_server_client_machine_is_local(server, machine) ||\n', '',
                     'host-never-dropped'),
}


def generated(control=None):
    source = read(MANAGER)
    code = '\n'.join(function(source, name) for name in FUNCTIONS)
    if control:
        before, after, _ = CONTROLS[control]
        code = mutated(code, before, after)
    statics = []
    for name in STATICS:
        statics.append(re.search(r'^static [^;(]*\b' + name + r'\b[^;]*;', source, re.M).group(0))
    statics.append(re.search(r'^static struct\n\{[^}]*\} network_game_server_ingame_additions\[[^;]*;',
                             source, re.M).group(0))
    enums = '\n'.join([
        enum_with(source, '_network_client_machine_connected_bit'),
        enum_with(source, '_network_game_server_state_pregame'),
        enum_with(read('source/networking/network_messages.h'), '_message_server_switch_to_pregame'),
        enum_with(read('source/game/game.h'), '_team_red'),
    ])
    structures = '\n'.join(re.search(r'^struct ' + name + r'\s*\{[^}]+\};', source, re.M).group(0)
                           for name in STRUCTURES)
    return (('config.inc', ''), ('enums.inc', enums), ('structures.inc', structures),
            ('under_test.inc', '\n'.join(statics) + '\n' + code))


def idle_pregame_tasks():
    return function(read(MANAGER), 'network_game_server_idle_pregame_tasks')


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('pregame_load', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('control', CONTROLS)
def test_negative_control(control):
    status, output = run(build('pregame_load', generated(control)), CONTROLS[control][2])
    assert status == CHECK_FAILED, f'{control} was not caught: {output}'


def test_lobby_runs_the_timeout():
    """the pregame's frame runs it while the countdown is on, over once it is at 0 and not paused, and
    lets it go while there is no countdown"""
    code = idle_pregame_tasks()
    held = re.search(r'network_game_server_pregame_load_timeout\(server,\s*'
                     r'countdown_timer_get_time_remaining\(&server->countdown_state\.timer\) == 0 &&\s*'
                     r'server->countdown_state\.paused == FALSE\);', code)
    assert held, 'the countdown does not run the pregame load timeout'
    start = code.index('network_game_server_start_network_game(server)')
    assert held.start() < start, 'the timeout runs after the start it should unblock'
    assert 'network_game_server_pregame_load_timeout(server, FALSE);' in code


def test_setting_registered_and_documented():
    config = (ROOT / 'port/linux/src/port_config.c').read_text(encoding='utf-8')
    assert re.search(r'\{ "network\.pregame_load_timeout", _config_integer, "90", "HALO_NET_PREGAME_LOAD_TIMEOUT", '
                     r'_environment_value,\s*_platform_all \| _platform_server', config)
    assert '`network.pregame_load_timeout` | `90` | `HALO_NET_PREGAME_LOAD_TIMEOUT` |' in \
        (ROOT / 'port/linux/README.md').read_text(encoding='utf-8')
    assert '`network.pregame_load_timeout`' in (ROOT / 'docs/settings.md').read_text(encoding='utf-8')
    assert '| `HALO_NET_PREGAME_LOAD_TIMEOUT` | `90` |' in (ROOT / 'server/docs/settings.md').read_text(encoding='utf-8')
