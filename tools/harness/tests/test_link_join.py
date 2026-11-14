"""An opened invite link held for the menus (port/linux/src/p2p.c, #994): held once, taken once, never under
p2p_lock, which the p2p thread holds while it hands a link over (a second copy's, Discord's Join, Android's
join_link.txt)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["held", "refused", "p2p_lock_held"]
CONTROLS = [
    # (the old fault: the link held under p2p_lock, which deadlocked)
    ("\tpthread_mutex_lock(&link_join_lock);\n\tsnprintf(link_join", "\tif (pthread_mutex_lock(&p2p_lock)) return 0;\n\tpthread_mutex_unlock(&p2p_lock);\n\tpthread_mutex_lock(&link_join_lock);\n\tsnprintf(link_join", "p2p_lock_held"),
    ("\t\tlink_join[0] = 0;\n", "", "held"),
    ("browser_headless() ||", "", "refused"),
]


def generated(fault=None):
    source = read("port/linux/src/p2p.c")
    state = re.search(r"^static pthread_mutex_t link_join_lock[^\n]*\n(?:static [^\n]*\n)+", source, re.M).group(0)
    code = state + function(source, "hold_link_join") + "\n" + function(source, "p2p_take_link_join") + "\n"
    if fault:
        code = mutated(code, fault[0], fault[1])
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("link_join", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("link_join", generated((before, after))), case)
    assert status == CHECK_FAILED, output
