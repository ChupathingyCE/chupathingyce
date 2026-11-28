"""MCC maps' unit HUD sheets come whole (#1826): the panels' backgrounds and
their meters are drawn over each other, so if one of them is the map's own
(an MCC campaign mod's bitmaps.map), the high-res HUD stands for none of
them, and a vehicle's health panel never draws MCC's wrench beside ours
(port/linux/game/hud_hires_tags.c)."""
import re

import pytest

from tools.harness import CHECK_FAILED, build, function, mutated, read, run, structure

TAGS = "port/linux/game/hud_hires_tags.c"


def sources(tags=None):
    tags = tags or read(TAGS)
    table = re.search(r"static char const \*const mcc_unit_sheet_tags\[\] =\n\{.*?\n\};", tags, re.S)
    assert table, "the unit sheets' table"
    return (
        ("sheets.inc", structure(read("port/linux/src/hud_hires.h"), "hud_hires_embedded") + "\n" + table.group(0) + "\n"),
        ("under_test.inc", "\n".join((
            function(tags, "hud_hires_mcc_match"),
            function(tags, "hud_hires_mcc_unit_sheet"),
            function(tags, "hud_hires_mcc_tags_loaded"),
        ))),
    )


@pytest.mark.parametrize("case", ["stock", "own_background", "own_meter", "no_meters", "own_numbers"])
def test_unit_sheets(case):
    status, output = run(build("hud_hires_mcc_units", sources()), case)
    assert status == 0, output


@pytest.mark.parametrize("case, before, after", [
    # each sheet on its own, as before: MCC's background with our meter
    ("own_background", "\t\t\t\tunit_sheet_own = 1;\n", ""),
    # a sheet the map has not got taken for one of its own
    ("no_meters", "if (bitmap && hud_hires_mcc_unit_sheet(embedded->tag))", "if (hud_hires_mcc_unit_sheet(embedded->tag))"),
])
def test_negative_controls(case, before, after):
    status, output = run(build("hud_hires_mcc_units", sources(mutated(read(TAGS), before, after))), case)
    assert status == CHECK_FAILED, output
