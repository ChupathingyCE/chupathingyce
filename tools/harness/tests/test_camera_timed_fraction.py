"""The camera's timed blend between ticks (port/linux/game/render_interpolation.c): a paused clock's fractions (a
whole tick) or snapshots too close in time give the fraction alone, so the camera is never drawn at a division by
nothing or a value not a number, and never stops for a tick after a pause."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["paused", "unpaused", "close-snapshots", "not-a-number", "running", "long-frame"]

# a fault in the blend and the case that must catch it
NEGATIVE_CONTROLS = {
    "paused-fractions-used": (("previous_fraction < 1.0f && latest_fraction < 1.0f && ", ""), "unpaused"),
    "paused-at-the-previous": (("previous_fraction < 1.0f && latest_fraction < 1.0f && ", ""), "paused"),
    "any-gap-divides": (("apart > least_apart", "apart > 0.0f"), "close-snapshots"),
    "nan-passes": (("previous_fraction < 1.0f && latest_fraction < 1.0f && apart > least_apart",
                    "!(previous_fraction >= 1.0f || latest_fraction >= 1.0f || apart <= least_apart)"),
                   "not-a-number"),
}


def generated(fault=None):
    interpolation = read("port/linux/game/render_interpolation.c")
    cseries = read("source/cseries/cseries.h")
    config = "".join(re.search(r"^#define " + name + r"\(.*$", cseries, re.M).group(0) + "\n"
                     for name in ("CEILING", "PIN"))
    code = "\n".join(function(interpolation, name) for name in
                     ("camera_snapshot_times_usable", "camera_timed_fraction", "camera_long_frame_along"))
    if fault:
        code = mutated(code, *fault)
    return (("config.inc", config), ("under_test.inc", code + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("camera_timed_fraction", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("camera_timed_fraction", generated(fault)), case)
    assert status == CHECK_FAILED, f"render_interpolation.c with a fault ({control}) passed '{case}': the test cannot see it"
