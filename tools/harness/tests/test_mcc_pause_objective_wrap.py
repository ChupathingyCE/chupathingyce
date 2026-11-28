"""An MCC map's campaign pause screen wraps a long objective (#1827): the
text box that shows the objective, in the map's own pause screen as in ours
(port/linux/game/mcc_pause.c, mcc_pause_text_wrap_needed), on MCC maps only,
its inputs read within bounds."""
import re

import pytest

from tools.harness import CHECK_FAILED, build, function, mutated, read, run, structure

PAUSE = "port/linux/game/mcc_pause.c"


def sources(pause=None):
    pause = pause or read(PAUSE)
    constants = re.search(r"enum \{ MCC_PAUSE_OBJECTIVE_INPUT = \d+, MCC_PAUSE_MAXIMUM_INPUTS = \d+ \};", pause)
    assert constants, "the objective input's constants"
    return (
        ("structures.inc", "\n".join((
            structure(pause, "mcc_pause_rect") + ";",
            structure(pause, "mcc_pause_widget") + ";",
            structure(pause, "mcc_pause_input") + ";",
            constants.group(0),
        )) + "\n"),
        ("under_test.inc", function(pause, "mcc_pause_text_wrap_needed")),
    )


@pytest.mark.parametrize("case", ["owned", "map_objective", "other_text", "xbox_map", "crafted"])
def test_objective_wrap(case):
    status, output = run(build("mcc_pause_objective_wrap", sources()), case)
    assert status == 0, output


@pytest.mark.parametrize("case, before, after", [
    # every map's objective wrapped, the Xbox's own too
    ("xbox_map", "if (handle == NONE || !mcc_cache_tags_loaded()) return FALSE;", "if (handle == NONE) return FALSE;"),
    # a map's count trusted
    ("crafted", "widget->inputs.count > MCC_PAUSE_MAXIMUM_INPUTS || ", ""),
    # the map's own pause screen left out, as before
    ("map_objective", "if (inputs[input].function == MCC_PAUSE_OBJECTIVE_INPUT) return TRUE;",
     "if (inputs[input].function == MCC_PAUSE_OBJECTIVE_INPUT) return FALSE;"),
])
def test_negative_controls(case, before, after):
    status, output = run(build("mcc_pause_objective_wrap", sources(mutated(read(PAUSE), before, after))), case)
    assert status == CHECK_FAILED, output
