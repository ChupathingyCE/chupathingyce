"""A client's network trouble box is for a host gone quiet, not this machine's own long frame (network_client_manager.c)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run  # noqa: E402

CASES = ['listening', 'host-quiet-then-own-frame', 'own-load', 'host-gone', 'host-gone-in-own-frame',
         'slow-machine-host-gone']
CONTROLS = {
    # the silence counted however long this machine has listened (main before the fix)
    'silence-alone': ('&&\n\t\t\tlistened > MILLISECONDS_PER_SECOND * 5', '', 'host-quiet-then-own-frame'),
    # every slow frame starts the listening over: a slow machine never sees a host gone quiet
    'every-slow-frame': ('(stalled && !network_game_client_listening.stalled)', 'stalled', 'slow-machine-host-gone'),
}


def generated(control=None):
    source = read('source/networking/network_client_manager.c')
    listening = re.search(r"^static struct\s*\{[^}]*\} network_game_client_listening;", source, re.M).group(0)
    code = '#define NETWORK_GAME_CLIENT_STALL_MILLISECONDS %d\n' % \
        constant(source, 'NETWORK_GAME_CLIENT_STALL_MILLISECONDS') + listening + '\n' + \
        function(source, 'network_game_client_listened_milliseconds') + '\n' + \
        function(source, 'network_game_client_idle_ingame')
    if control:
        before, after, _ = CONTROLS[control]
        code = mutated(code, before, after)
    config = enum_with(read('source/interface/ui_widget.h'), '_error_network_trouble_is_brewing') + '\n'
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('client_trouble_box', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('control', CONTROLS)
def test_negative_control(control):
    status, output = run(build('client_trouble_box', generated(control)), CONTROLS[control][2])
    assert status == CHECK_FAILED, f'{control} was not caught: {output}'
