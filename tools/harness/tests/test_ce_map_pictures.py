"""Where the menus' map list finds Halo PC's map pictures in a bitmaps.map (ui_map_list.c): Custom Edition's keeps
them in one resource at the offsets its ui.map gives, Halo PC retail's and MCC's each in its own resource elsewhere
(read at Custom Edition's offsets they were black boxes with a strip of noise, #1817); another file, or a table that
points outside its file, gives none. Crafted tables never give a picture outside the file."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run, structure  # noqa: E402

SOURCE = "port/linux/game/ui_map_list.c"
CONSTANTS = ["CE_BITMAPS_MAP_TYPE", "CE_BITMAPS_MAXIMUM_RESOURCES", "CE_BITMAPS_MAXIMUM_PATHS_SIZE"]
FUNCTIONS = ["ce_bitmaps_header_valid", "ce_bitmaps_resource", "ce_picture_pixels"]
CASES = ["custom_edition", "halo_pc", "bounds", "headers", "crafted"]
# a fault in the code, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "any-size-by-path": (("resource != NONE && table->resources[resource * 3 + 1] == size", "resource != NONE"),
                         "ce_picture_pixels", "halo_pc"),
    "range-that-wraps": (("offset - pixels[2] <= pixels[1] - size", "offset + size <= pixels[2] + pixels[1]"),
                         "ce_picture_pixels", "bounds"),
    "resource-past-file": (("resource[2] <= table->file_size && resource[1] <= table->file_size - resource[2]",
                            "TRUE"), "ce_bitmaps_resource", "bounds"),
    "table-past-file": (("header[2] <= file_size &&", ""), "ce_bitmaps_header_valid", "headers"),
    "no-pixels-resource": (("offset >= pixels[2] && size <= pixels[1]", "FALSE && size <= pixels[1]"),
                           "ce_picture_pixels", "custom_edition"),
}


def generated(fault=None, faulty_function=None):
    source = read(SOURCE)
    text = "".join(f"#define {name} {constant(source, name)}\n" for name in CONSTANTS)
    text += structure(source, "ce_bitmaps_table") + "\n"
    for name in FUNCTIONS:
        code = function(source, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    return (("under_test.inc", text),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("ce_map_pictures", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("ce_map_pictures", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
