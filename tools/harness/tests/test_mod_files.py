"""The real checks between a mod's files and the game: the keys a map's tag names find files by (port_mods.c) and a
tag file's bounds (mods.c)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["keys", "bounds"]
SOURCES = {
    "port/linux/src/port_mods.c": ["lower", "key_hash", "key_make", "names_equal"],
    "port/linux/game/mods.c": ["mods_in_file"],
}
# a fault in the code, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "keeps-slashes": (("key[length++] = *text == '/' ? '\\\\' : lower(*text);", "key[length++] = lower(*text);"),
                      "key_make", "keys"),
    "adds-offset-and-size": (("offset <= file_size && size <= file_size - offset", "offset + size <= file_size"),
                             "mods_in_file", "bounds"),
}


def generated(fault=None, faulty_function=None):
    text = ""
    for path, names in SOURCES.items():
        source = read(path)
        for name in names:
            code = function(source, name)
            if fault and name == faulty_function:
                code = mutated(code, *fault)
            text += code + "\n"
    return (("under_test.inc", text),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("mod_files", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("mod_files", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
