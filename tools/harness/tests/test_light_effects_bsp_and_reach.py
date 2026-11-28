# Dynamic Lighting, Dynamic Shadows and shared renderer optimisations by
# chestahh (https://github.com/chestahh), originally developed for GulchCE
# (https://github.com/chestahh/GulchCE). Original OpenCE port:
# https://github.com/chestahh/GulchCE/tree/035f47bb8fc682675456323dbb3095b506502ec6
# From GulchCE by chestahh (granted 2026-11-26). Changed for Chupathingy (see the git history).
#
# SPDX-FileCopyrightText: 2026 chestahh (GulchCE)
# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
"""A structure bsp switched to sees its lens flares anew, and the Light effects' own lights reach the clusters
they light.

    python -m pytest -q tools/harness/tests/test_light_effects_bsp_and_reach.py
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import function, read  # noqa: E402


def lf(text):
    return text.replace("\r\n", "\n")


def test_new_structure_bsp_sees_its_flares_anew():
    """The switch clears the bsp markers' visibilities and their slots, which the old bsp's markers at the same
    indices left, once the new bsp is current; a console's build keeps what it had."""
    lights = lf(read("source/rasterizer/rasterizer_lights.c"))
    reset = function(lights, "rasterizer_lights_reset_for_new_structure_bsp")
    results = "memset(local_lens_flare_occlusion_test_results2, 0, sizeof(local_lens_flare_occlusion_test_results2));"
    slots = "memset(lens_flare_test_slots, 0, sizeof(lens_flare_test_slots));"
    buckets = "memset(lens_flare_test_buckets, 0, sizeof(lens_flare_test_buckets));"
    for line in (results, slots, buckets, "lens_flare_test_cursor = 0;"):
        assert line in reset, line
    # the same slots a new map clears
    new_map = function(lights, "rasterizer_lights_reset_for_new_map")
    for line in (results, slots, buckets):
        assert line in new_map, line
    # never on a console (the Xbox's waited tests read their own index's slot)
    console = reset.index("defined(HALO_XBOX_CONSOLE)")
    assert reset.index("#if !(") < console < reset.index(results)
    switch = function(lf(read("source/scenario/scenario.c")), "scenario_switch_structure_bsp")
    assert switch.index("global_structure_bsp_index = structure_bsp_index;") < \
        switch.index("rasterizer_lights_reset_for_new_structure_bsp();")
    assert "void rasterizer_lights_reset_for_new_structure_bsp(" in read("source/rasterizer/rasterizer.h")


def test_owned_lights_reach_their_clusters():
    """A light of the Light effects' own (a muzzle flash, an emitter, a Covenant bolt, a rocket) is connected to
    the clusters as far as it lights, before the plasma lights' own reach; consoles build none of them."""
    sphere = function(lf(read("source/objects/object_lights.c")), "light_compute_bounding_sphere")
    block = sphere[sphere.index("if (maximum)"):]
    block = block[:block.index("\n\t}\n")]
    calls = ["muzzle_lighting_radius(light_index, light_radius)",
             "covenant_emitter_lighting_radius(light_index, light_radius)",
             "covenant_lighting_radius(light_index, light->object_index, light_radius)",
             "rocket_lighting_radius(light_index, light->object_index, light_radius)",
             "plasma_lighting_radius(light_index, light->object_index, light_radius)"]
    places = [block.index(call) for call in calls]
    assert places == sorted(places)
    guard = block.index("#ifndef HALO_CONSOLE_NO_LIGHT_EFFECTS")
    assert guard < places[0] and places[3] < block.index("#endif") < places[4]


def test_radii_only_grow_and_only_when_on():
    """Each reach keeps the map light's radius when its effect is off or the light is not its own, and never
    shortens it (the retail clusters stay connected)."""
    for path, name, setting in (("port/linux/game/rocket_lighting.c", "rocket_lighting_radius", "plasma_lighting"),
                                ("port/linux/game/covenant_lighting.c", "covenant_lighting_radius",
                                 "plasma_lighting"),
                                ("port/linux/game/muzzle_lighting.c", "muzzle_lighting_radius", "muzzle_lighting")):
        body = function(lf(read(path)), name)
        assert f'if (!lighting_config_boolean("display.{setting}"))\n\t\treturn radius;' in body, name
        assert "return MAX(radius, " in body, name
    body = function(lf(read("port/linux/game/covenant_emitter_lighting.c")), "covenant_emitter_lighting_radius")
    assert "emitter_kind_enabled(emitters[i].kind)" in body and "return MAX(radius, emitters[i].radius);" in body
