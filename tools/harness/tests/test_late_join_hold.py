"""A host holds late joiners until the next game, within limits (port/linux/game/late_join.c)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ['held', 'hold-ends', 'rejoined', 'told-once', 'next-game']
CONTROLS = {
    # a hold with no end: one idle player keeps a joiner out for ever
    'no-most': ('game_time_get() - late_join.held_at[absolute] >= LATE_JOIN_HOLD_MOST ||', '', 'hold-ends'),
    # a player back after dropping held as a newcomer
    'rejoin-held': ('seen = late_join_name_seen(name);', 'seen = (late_join_name_seen(name), FALSE);', 'rejoined'),
    # told every frame
    'told-always': ('\t\tlate_join.told[absolute] = TRUE;\n', '', 'told-once'),
}


def generated(control=None):
    source = read('port/linux/game/late_join.c')
    constants = re.search(r"^enum\n\{.*?^\};", source, re.M | re.S).group(0)
    globals_ = re.search(r"^static struct\n\{.*?^\} late_join;", source, re.M | re.S).group(0)
    code = '\n'.join([constants, globals_] + [function(source, name) for name in (
        'late_join_clear', 'late_join_marked', 'late_join_others_playing', 'late_join_name_seen',
        'late_join_player_added', 'late_join_holds')])
    if control:
        before, after = CONTROLS[control][:2]
        code = mutated(code, before, after)
    return (('under_test.inc', code),)


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('late_join_hold', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('control', CONTROLS)
def test_negative_control(control):
    status, output = run(build('late_join_hold', generated(control)), CONTROLS[control][2])
    assert status == CHECK_FAILED, f'{control} was not caught: {output}'
