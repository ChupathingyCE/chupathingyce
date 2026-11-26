"""The real mcc_meter_bleed (mcc_bitmaps.c) over small meter sheets: the texels
beside an MCC meter's shapes take their fill order, so a filtered meter doesn't
light the edges of its empty segments (#1355)."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run  # noqa: E402

CASES = ["edges-take-the-fill", "shape-and-far-texels-unchanged", "clone-keeps-coverage", "no-state-no-change"]

# a fault in mcc_meter_bleed and the case that must catch it
NEGATIVE_CONTROLS = {
    "no-bleed": (("texels[at]=(texels[at]&keep)|(texels[from]&~keep);", "(void)from;"), "edges-take-the-fill"),
    "alpha-bled": (("uint32_t keep=usage==4 ? 0xFF00FF00u : 0xFF000000u;",
                    "uint32_t keep=usage==4 ? 0x0000FF00u : 0u;"), "edges-take-the-fill"),
    "coverage-bled": (("uint32_t keep=usage==4 ? 0xFF00FF00u : 0xFF000000u;",
                       "uint32_t keep=0xFF000000u;"), "clone-keeps-coverage"),
    "one-pass-reaches-far": (("if (!state[other] || state[other]>pass) continue;",
                              "if (!state[other]) continue;"), "shape-and-far-texels-unchanged"),
}


def generated(fault=None):
    source = read("port/linux/game/mcc_bitmaps.c")
    code = "#define MCC_METER_BLEED_PASSES %d\n" % constant(source, "MCC_METER_BLEED_PASSES")
    code += function(source, "mcc_meter_bleed")
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("mcc_meter_bleed", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("mcc_meter_bleed", generated(fault)), case)
    assert status == CHECK_FAILED, f"mcc_bitmaps.c with a fault ({control}) passed '{case}': the test cannot see it"
