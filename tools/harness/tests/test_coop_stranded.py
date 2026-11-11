"""The co-op host's test for a player stranded outside the loaded BSP (players.c's players_coop_unit_stranded): falling,
or standing where the BSP has no floor (a player a scripted BSP switch left behind on the last one's), but not riding an
elevator through its shaft or an AI-flown vehicle."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

FUNCTIONS = ("players_coop_unit_in_structure", "players_coop_unit_grounded_anywhere", "players_coop_unit_on_elevator",
             "players_coop_unit_stranded")
CASES = ["inside", "falling", "standing-in-void", "elevator-shaft", "ai-pelican", "player-vehicle"]


def generated(fault=None):
    source = read("source/game/players.c")
    code = "\n".join(function(source, name) for name in FUNCTIONS)
    if fault == "only-falling":
        code = mutated(code, " || !players_coop_unit_on_elevator(unit_index)", "")
    if fault == "elevator-ignored":
        code = mutated(code, "biped->biped.elevator_object_index != NONE", "FALSE")
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("coop_stranded", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault,case", [("only-falling", "standing-in-void"), ("elevator-ignored", "elevator-shaft")])
def test_negative_control(fault, case):
    status, output = run(build("coop_stranded", generated(fault)), case)
    assert status == CHECK_FAILED, output
