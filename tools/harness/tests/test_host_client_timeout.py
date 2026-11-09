"""The host's own client does not time out its own server after a long frame (network_client_manager.c)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ['host-hitch', 'host-loading', 'client-hitch', 'client-loading', 'client-host-gone', 'client-heard']
CONTROLS = {
    # the host's client timed out as another machine's is: a long frame leaves the host's own game
    'host-times-out': ('return global_network_game_server_get() ? 0 : timeout;', 'return timeout;', 'host-hitch'),
    # no client ever timed out: a client whose host is gone stays for ever
    'nobody-times-out': ('return global_network_game_server_get() ? 0 : timeout;', 'return 0;', 'client-hitch'),
}


def generated(control=None):
    source = read('source/networking/network_client_manager.c')
    code = function(source, 'network_game_client_connection_timeout') + '\n' + \
        function(source, 'network_game_client_idle_ingame')
    if control:
        before, after, _ = CONTROLS[control]
        code = mutated(code, before, after)
    config = enum_with(read('source/interface/ui_widget.h'), '_error_network_connection_lost') + '\n'
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('host_client_timeout', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('control', CONTROLS)
def test_negative_control(control):
    status, output = run(build('host_client_timeout', generated(control)), CONTROLS[control][2])
    assert status == CHECK_FAILED, f'{control} was not caught: {output}'
