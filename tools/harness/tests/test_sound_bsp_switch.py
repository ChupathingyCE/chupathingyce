"""Across a structure BSP switch a looping sound's location is the new structure's, and a sound whose cluster the
structure does not have is not looked up in its cluster tables (sound_reconnect_to_structure_bsp,
compute_sound_obstruction)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run
from harness.audio import manager_types

CASES = ["kept-loop", "loop-in-both", "sound", "stale-cluster"]
NEGATIVE_CONTROLS = {
    # the looping sounds' locations left as they were: a10's switch into its two-cluster structure
    "loops-not-made-again": (("looping_sound->source.spatialization_mode ==", "FALSE &&"), "kept-loop"),
    # the source's cluster not checked: read past the cluster tables
    "source-cluster-unchecked": (("boolean valid = source_cluster_index < cluster_count &&",
                                  "boolean valid = TRUE &&"), "stale-cluster"),
}


def generated(fault=None):
    manager = read("source/sound/sound_manager.c")
    code = "\n".join([function(manager, "track_loop_track_sound"),
                      function(manager, "sound_reconnect_to_structure_bsp"),
                      function(read("source/sound/game_sound.c"), "sound_obstruction_clusters_valid")])
    if fault:
        code = mutated(code, *fault)
    return (("types.inc", manager_types()), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("sound_bsp_switch", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("sound_bsp_switch", generated(fault)), case)
    assert status == CHECK_FAILED, f"{control} escaped {case}: {output}"
