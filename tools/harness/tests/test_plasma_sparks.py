"""display.plasma_sparks (source/effects/particles.c): a particle of one short sequence, the plasma grenade's bolts, is
never drawn when off, as on the Xbox, at any frame rate, and drawn when on, at any frame rate."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run

CASES = ["xbox", "sparks", "frames", "looping"]
CONTROLS = [
    # (the time the frame took, as before: the bolts showed at high frame rates)
    ("return MAX(dt, 1.0f / TICKS_PER_SECOND);", "return dt;", "xbox"),
    ("\tif (particles_plasma_sparks)\n\t\treturn 0.0f;\n", "", "sparks"),
    ("\tif (definition->looping_sequence_count > 0 ||\n", "\tif (", "looping"),
    ("struct bitmap_group_sequence)->sprites.count > 1)", "struct bitmap_group_sequence)->sprites.count > 99)", "frames"),
]


def generated(fault=None):
    source = read("source/effects/particles.c")
    header = read("source/effects/particles.h")
    types = "\n".join([enum_with(source, "_particle_definition_animate_once_per_frame_bit"),
                       enum_with(source, "_particle_state_next_sequence_initial"),
                       enum_with(header, "_particle_datum_animates_backwards_bit")])
    code = "\n".join(function(source, name) for name in
                     ["particle_next_sequence", "particle_next_frame", "particle_update_frame_time",
                      "particle_first_frame_time"])
    if fault:
        code = mutated(code, fault[0], fault[1])
    return (("types.inc", types), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("plasma_sparks", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("plasma_sparks", generated((before, after))), case)
    assert status == CHECK_FAILED, output
