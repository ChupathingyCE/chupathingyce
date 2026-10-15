"""The real merge of the game list's listings (port/linux/src/browser.c): a host that lists one game once for each
network version is shown once, at its newest version, and its players are counted once; other games stay apart."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, function, mutated, read, run  # noqa: E402

CASES = ["one-game-many-versions", "different-games-kept", "same-version-kept"]

# a fault in browser.c, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "never-merged": (("for (other = 0; other < kept && ", "for (other = kept; other < kept && "),
                     "merge_listings", "one-game-many-versions"),
    "oldest-kept": (("games[index].version > games[other].version", "games[index].version < games[other].version"),
                    "merge_listings", "one-game-many-versions"),
    "same-version-merged": (("a->version != b->version &&", ""), "same_game", "same-version-kept"),
    "players-ignored": (("a->players == b->players &&", ""), "same_game", "different-games-kept"),
    "map-ignored": (("!strcmp(a->map, b->map) &&", ""), "same_game", "different-games-kept"),
    "roster-ignored": (("!memcmp(a->roster, b->roster, sizeof(a->roster))", "1"), "same_game", "different-games-kept"),
}


def generated(fault=None, faulty_function=None):
    source = read("port/linux/src/browser.c")
    text = ""
    for name in ("same_game", "merge_listings"):
        code = function(source, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    return (("browser.h", read("port/linux/src/browser.h")), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("browser_listings", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("browser_listings", generated(fault, faulty_function)), case)
    assert status != 0, f"{control}: {case} did not catch it"
