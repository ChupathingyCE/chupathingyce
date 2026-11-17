"""A host's join lock ends when hosting ends or nobody it was for is in (port/linux/game/host_rules.c)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ['players-stay', 'hosting-ends', 'empties', 'own-players-alone', 'short-gap', 'locked-again']
CONTROLS = {
    # the old behavior: a lock that never ends by itself
    'never-ends': ('\thost_rules_set_joins_locked(FALSE);\n\terror(', '\terror(', 'hosting-ends', None),
    # a player's host counting its own machine as someone to keep the lock for
    'own-machine-counts': ('return game->machine_count > 1;', 'return game->machine_count > 0;',
                           'own-players-alone', False),
    # a gap that ends the wait doesn't start it over
    'gap-not-reset': ('\t\thost_rules.lock_empty_since = 0;\n\t\treturn;', '\t\treturn;', 'short-gap', None),
}


def generated(server, control=None):
    source = read('port/linux/game/host_rules.c')
    globals_ = re.search(r"^static struct\n\{.*?^\} host_rules;", source, re.M | re.S).group(0)
    constants = re.search(r"^enum\n\{\n\t/\* a lock outlives.*?^\};", source, re.M | re.S).group(0)
    code = '\n'.join([constants, globals_] + [function(source, name) for name in (
        'host_rules_set_joins_locked', 'host_rules_joins_locked', 'host_rules_lock_has_players',
        'host_rules_lock_update')])
    if control:
        before, after = CONTROLS[control][:2]
        code = mutated(code, before, after)
    config = '#define HALO_SERVER\n' if server else ''
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('server', [False, True], ids=['player-host', 'dedicated'])
@pytest.mark.parametrize('case', CASES)
def test_case(case, server):
    status, output = run(build('join_lock', generated(server)), case)
    assert status == 0, output


@pytest.mark.parametrize('control', CONTROLS)
def test_negative_control(control):
    server = CONTROLS[control][3]
    status, output = run(build('join_lock', generated(bool(server), control)), CONTROLS[control][2])
    assert status == CHECK_FAILED, f'{control} was not caught: {output}'
