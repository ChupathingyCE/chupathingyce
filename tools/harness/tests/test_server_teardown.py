"""Real menu and server teardown, including native/legacy platform guards."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run  # noqa: E402

# (the platform's defines; the sleep it waits before its transport closes: only a console on the XDK's XNet keeps
# the original second, HALO_CONSOLE_SERVER_CLOSE_GRACE in halo_console_features.h)
PLATFORMS = {
    'windows': ('#define HALO_WINDOWS 1', 0),
    'linux': ('#define __linux__ 1', 0),
    'android': ('#define HALO_ANDROID 1', 0),
    'xbox': ('#define HALO_XBOX_CONSOLE 1', 1000),
    'other-port': ('#define HALO_UNEXAMINED_PORT 1', 0),
}
CASES = [(platform, case) for platform in PLATFORMS for case in ('back', 'reopen')]
CASES += [('windows', case) for case in ('postgame', 'ingame', 'message-failure',
                                       'send-failure', 'machine-failure', 'no-server')]
CASES += [('xbox', case) for case in ('postgame', 'ingame')]
GUARD = '#ifdef HALO_CONSOLE_SERVER_CLOSE_GRACE'
CONTROLS = {
    'native-sleeps': (GUARD, '#if 1', 'windows', 'back'),
    'xbox-loses-grace': ('SleepEx(MILLISECONDS_PER_SECOND, FALSE);', ';', 'xbox', 'back'),
    'transport-not-terminated': ('transport_server_terminate();', ';', 'windows', 'back'),
    'connections-not-closed': ('network_connection_delete(server->connection);', ';', 'windows', 'reopen'),
    'server-state-not-cleared': ('csmemset(server, 0, sizeof(*server));', ';', 'windows', 'back'),
    'number-still-held': ('delta_legacy_hold(FALSE);', ';', 'windows', 'back'),
    'damage-tables-kept': ('network_damage_host_tables_free();', ';', 'windows', 'back'),
    'late-joiners-kept': ('network_game_server_late_joiners_free();', ';', 'windows', 'reopen'),
}


def generated(platform, control=None):
    server = read('source/networking/network_server_manager.c')
    code = function(server, 'network_game_server_dispose')
    if control:
        before, after, _, _ = CONTROLS[control]
        code = mutated(code, before, after)
    globals_source = read('source/networking/network_game_globals.c')
    for name in ('dispose_global_network_game_server', 'dispose_global_network_game_client'):
        code += '\n' + function(globals_source, name)
    widgets = read('source/interface/ui_widget_event_handler_functions.c')
    for name in ('clear_multiplayer_player_joins', 'ui_widget_port_host'):
        code += '\n' + function(widgets, name)
    defines, sleep = PLATFORMS[platform]
    # The harness runs on Linux; each compiled variant selects its own target.
    config = '#undef __linux__\n#define xbox 1\n' + defines + f'\n#define EXPECTED_SLEEP {sleep}\n'
    if platform == 'xbox':
        # (the gate as the Xbox's features have it)
        features = read('port/linux/include/halo_console_features.h')
        block = features[features.index('#ifdef HALO_XBOX_CONSOLE\n#define'):]
        block = block[:block.index('#endif')]
        assert '#define HALO_CONSOLE_SERVER_CLOSE_GRACE 1' in block, 'the Xbox no longer keeps the close grace'
        config += '#define HALO_CONSOLE_SERVER_CLOSE_GRACE 1\n'
    config += f'#define MILLISECONDS_PER_SECOND {constant(read("source/cseries/cseries.h"), "MILLISECONDS_PER_SECOND")}\n'
    for member in ('_network_game_server_state_pregame', '_network_game_server_game_open_bit'):
        config += enum_with(server, member) + '\n'
    config += enum_with(read('source/networking/network_messages.h'), '_message_server_graceful_game_exit_pregame') + '\n'
    for name in ('message_server_graceful_game_exit_pregame', 'message_server_graceful_game_exit_postgame'):
        config += re.search(r'struct ' + name + r'\s*\{[^}]+\};', server).group() + '\n'
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('platform,case', CASES)
def test_case(platform, case):
    status, output = run(build('server_teardown', generated(platform)), case)
    assert status == 0, output


@pytest.mark.parametrize('control', CONTROLS)
def test_negative_control(control):
    _, _, platform, case = CONTROLS[control]
    status, output = run(build('server_teardown', generated(platform, control)), case)
    assert status == CHECK_FAILED, f'{control} was not caught: {output}'
