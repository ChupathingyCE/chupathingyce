"""The real ui_map_read (menu_tags.c) over made-up cache files: ui.map's copy in
the cache is read for the settings' pictures in a game map only when its header
names ui.map and has the checksum the main menu found, and only for bytes past
the header and within the length the header gives (#1357)."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run  # noqa: E402

CASES = ["reads-ui-map", "another-map-or-checksum", "range-outside-the-map", "header-longer-than-file"]
CONSTANTS = ["UI_MAP_HEADER_SIZE", "UI_MAP_HEADER_LENGTH_OFFSET", "UI_MAP_HEADER_NAME_OFFSET",
             "UI_MAP_HEADER_NAME_SIZE", "UI_MAP_HEADER_CHECKSUM_OFFSET"]

# a fault in ui_map_read and the case that must catch it
NEGATIVE_CONTROLS = {
    "any-name": (('!strcmp((char const *)header + UI_MAP_HEADER_NAME_OFFSET, "ui") && ', ""),
                 "another-map-or-checksum"),
    "any-checksum": (("header_checksum == checksum &&", ""), "another-map-or-checksum"),
    "into-the-header": (("offset >= UI_MAP_HEADER_SIZE &&", "offset >= 0 &&"), "range-outside-the-map"),
    "past-the-length": (("offset <= length - size &&", ""), "range-outside-the-map"),
    "short-read-taken": (("bytes_read == (unsigned long)size)", "bytes_read >= 0)"), "header-longer-than-file"),
}


def generated(fault=None):
    source = read("port/linux/game/menu_tags.c")
    code = "".join("#define %s %d\n" % (name, constant(source, name)) for name in CONSTANTS)
    code += '#define UI_MAP_CACHE_FILE "z:\\\\cache002.map"\n'
    code += function(source, "ui_map_read")
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("ui_map_read", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("ui_map_read", generated(fault)), case)
    assert status == CHECK_FAILED, f"menu_tags.c with a fault ({control}) passed '{case}': the test cannot see it"
