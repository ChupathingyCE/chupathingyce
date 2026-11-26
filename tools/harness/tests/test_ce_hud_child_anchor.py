"""A Custom Edition map's weapon HUD elements honor their own anchors (#1820).

halopc-restored's sniper rifle places its angle ticks and range numbers from
the middle of the screen with the element's own anchor (MCC's field, which
Chimera reads on Halo PC). The Xbox's maps keep their parent's anchor, and
an MCC map's stays MCC's.
"""
import pytest

from tools.harness import CHECK_FAILED, build, enum_with, function, mutated, read, run

CE_HUD = "port/linux/game/ce_hud.c"
HUD_WEAPON = "source/interface/hud_weapon.c"


def code(ce_hud=None, hud_weapon=None):
    ce_hud = ce_hud or read(CE_HUD)
    hud_weapon = hud_weapon or read(HUD_WEAPON)
    return (
        ("anchors.inc", enum_with(read("source/interface/hud_definitions.h"), "_hud_anchor_center") + ";\n"),
        ("under_test.inc", "\n".join([
            function(ce_hud, "ce_hud_child_anchor"),
            function(ce_hud, "ce_hud_tags_unloaded"),
            function(hud_weapon, "weapon_hud_element_placement"),
        ])),
    )


@pytest.mark.parametrize("case", ["xbox", "custom_edition", "mcc", "unloaded"])
def test_child_anchors(case):
    status, output = run(build("ce_hud_child_anchor", code()), case)
    assert status == 0, output


@pytest.mark.parametrize("case, file, before, after", [
    # the edges' middles taken too: the Xbox's renderer has no case for them
    ("custom_edition", CE_HUD, "child_anchor > _hud_anchor_center + 1", "child_anchor > 9"),
    # on every map, the Xbox's too
    ("xbox", CE_HUD, "if (!ce_hud_child_anchors || child_anchor < 1", "if (child_anchor < 1"),
    # not let go of with the map
    ("unloaded", CE_HUD, "\tce_hud_child_anchors = FALSE;\n", ""),
    # the anchor written into the parent's placement, shared by every element
    ("custom_edition", HUD_WEAPON, "*storage = *parent;\n\t\tstorage->corner = corner;\n\t\tplacement = storage;",
     "((struct hud_absolute_placement_definition *)parent)->corner = corner;"),
])
def test_negative_controls(case, file, before, after):
    ce_hud = read(CE_HUD)
    hud_weapon = read(HUD_WEAPON)
    if file == CE_HUD:
        ce_hud = mutated(ce_hud, before, after)
    else:
        hud_weapon = mutated(hud_weapon, before, after)
    status, output = run(build("ce_hud_child_anchor", code(ce_hud, hud_weapon)), case)
    assert status == CHECK_FAILED, output
