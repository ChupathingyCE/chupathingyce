"""A cutscene camera's cuts between ticks (port/linux/game/render_interpolation.c, observer_timed_cut): a fast
camera's snapshots a tick or a little more apart are no cut, nor is its first move after a cut; a jump to another
point, a cut from a moving camera, a turn and a position not a number are."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run  # noqa: E402

CASES = ["fast-camera", "cut", "first-move-after-cut", "not-a-number"]

# a fault in the cut and the case that must catch it
NEGATIVE_CONTROLS = {
    # judged by distance alone, as the player's views are
    "distance-alone": (("\tif (!earlier || !(a_time > earlier_time))", "\treturn TRUE;\n\tif (!earlier)"),
                       "fast-camera"),
    # no last speed: a fast camera's first move after a cut taken for a cut
    "no-last-speed": (("reach += speed * ticks;", "(void)speed;"), "first-move-after-cut"),
    # never a cut once it moved as it was moving
    "cut-never": (("return !(distance_squared(&expected, &b->position)", "return 0 && !(distance_squared(&expected, &b->position)"),
                  "cut"),
}


def generated(fault=None):
    interpolation = read("port/linux/game/render_interpolation.c")
    config = "".join(f"#define {name} {value}\n" for name, value in (
        ("CAMERA_CUT_DISTANCE", "3.0f"), ("CAMERA_CUT_COSINE", "0.5f")))
    assert f"#define CAMERA_CUT_DISTANCE 3.0f" in interpolation and "#define CAMERA_CUT_COSINE 0.5f" in interpolation
    code = function(interpolation, "observer_timed_cut")
    if fault:
        code = mutated(code, *fault)
    return (("config.inc", config), ("under_test.inc", code + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("camera_timed_cut", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("camera_timed_cut", generated(fault)), case)
    assert status == CHECK_FAILED, f"render_interpolation.c with a fault ({control}) passed '{case}': the test cannot see it"
