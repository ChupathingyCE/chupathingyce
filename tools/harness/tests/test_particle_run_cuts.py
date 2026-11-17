"""A run of particles cut at the transparent queue's depths (render_particles_cut_run), so that each particle sorts
among the queue's groups as it would alone: the production depth list, cut, room, sprites' sort depth and queue sort.
From OpenH1's test of the cut (erin.cena, OpenH1 80351761), with the keys and the room of our version."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run
from harness.render_types import transparent_types

CASES = ["mist", "depths", "whole", "budget", "invalid", "bias", "random", "room"]
CONTROLS = {
    # the run whole, as the Xbox built it: the pickup over all of the mist
    "never-cut": (("if (depths[middle] < particle_depth)", "if (FALSE)"), "mist"),
    "water-decal-a-depth": (("\t\t\tshader_is_water_decal(group->shader) ||\n", ""), "depths"),
    "first-person-a-depth": (("\t\t\tTEST_FLAG(group->geometry_flags, _rasterizer_geometry_first_person_bit) ||\n", ""), "depths"),
    "depths-unsorted": (("qsort(depths, depth_count, sizeof(*depths), sort_depths_cmpfn);", "(void)0;"), "depths"),
    "bias-ignored": (("\t\tz_sort += SHADER_EFFECT_SORT_BIAS;", "\t\tz_sort += 0.0f;"), "bias"),
    "budget-ignored": (("if (part_count > maximum_part_count)", "if (FALSE)"), "budget"),
    "invalid-counted": (("\t\tif (keys[particle_index] == NONE)\n\t\t{\n\t\t\tcontinue;\n\t\t}\n\t\tif (!counts[keys[particle_index]]++)",
                         "\t\tif (!counts[MAX(keys[particle_index], 0)]++)"), "invalid"),
    "last-depth-ignored": (("\tlong high = depth_count;", "\tlong high = depth_count - 1;"), "random"),
    "vertex-buffers-ignored": (("MIN(free_group_count, free_vertex_buffer_count)", "free_group_count"), "room"),
    "pages-ignored": (("room / part_group_bound + 1", "room + 1"), "room"),
    "budget-unbounded": (("\troom = MIN(room, cut_budget);\n", ""), "room"),
}


def generated(control=None):
    core = read("source/rasterizer/rasterizer_transparent_geometry.c")
    dynamic = read("source/rasterizer/xbox/rasterizer_xbox_dynavobgeom.c")
    types = transparent_types()
    for name in ["_rasterizer_geometry_first_person_bit"]:
        types += f"\n#define {name} {constant(core, name)}\n"
    types += f"\n#define _shader_effect_sort_bias_bit {constant(dynamic, '_shader_effect_sort_bias_bit')}\n"
    bias = re.search(r"#define\s+SHADER_EFFECT_SORT_BIAS\s+([0-9.]+)f", dynamic).group(1)
    types += f"\n#define SHADER_EFFECT_SORT_BIAS {bias}f\n"
    code = function(read("source/shaders/shaders.c"), "shader_is_water_decal")
    code += "\n" + function(dynamic, "rasterizer_dynamic_unlit_geometry_z_sort")
    for name in ["sort_depths_cmpfn", "rasterizer_transparent_geometry_sort_depths", "group_sorted_indices_cmpfn"]:
        code += "\n" + function(core, name)
    particles = read("source/render/render_particles.c")
    for name in ["render_particles_run_key", "render_particles_cut_run", "render_particles_maximum_part_count"]:
        code += "\n" + function(particles, name)
    if control:
        before, after = CONTROLS[control][0]
        code = mutated(code, before, after)
    return (("types.inc", types), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("particle_run_cuts", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    status, output = run(build("particle_run_cuts", generated(control)), CONTROLS[control][1])
    assert status == CHECK_FAILED, f"{control} escaped its check: {output}"
