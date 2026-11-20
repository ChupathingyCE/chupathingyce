"""MCC maps' stock HUD sheets: the high-res HUD stands for a sheet only where
its tag, bitmap, size and stored pixels are the stock sheet's
(port/linux/game/hud_hires_tags.c), and reads the map's own texels for the
cells it keeps."""
import pytest

from tools.harness import CHECK_FAILED, build, function, mutated, read, run, structure

TAGS = "port/linux/game/hud_hires_tags.c"


def sources(match=None):
    tags = read(TAGS)
    return (("under_test.inc", "\n".join((
        structure(read("port/linux/src/hud_hires.h"), "hud_hires_embedded"),
        match if match is not None else function(tags, "hud_hires_mcc_match"),
        function(tags, "mcc_texel"),
    ))),)


@pytest.mark.parametrize("case", ["match", "texels"])
def test_stock_sheets(case):
    status, output = run(build("hud_hires_mcc", sources()), case)
    assert status == 0, output


def test_custom_art_under_a_stock_name_is_told_apart():
    # Negative control: matched by name and size alone, a map maker's own
    # art would be drawn over.
    match = mutated(function(read(TAGS), "hud_hires_mcc_match"),
                    "(crc & 0xffffffffUL) == (embedded->crc & 0xffffffffUL)", "1")
    status, output = run(build("hud_hires_mcc", sources(match)), "match")
    assert status == CHECK_FAILED, output
