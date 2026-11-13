"""MCC script calls with more than one argument pass validation on 64-bit hosts too."""
import os
import re
import struct
import subprocess
import tempfile
from pathlib import Path
import pytest
from tools.harness import function, mutated, read, structure


def code():
    header = read("source/hs/hs.h")
    types = structure(header, "hs_function_definition")
    macro = re.search(r"#define HS_FUNCTION_PARAMETER_TYPE\(.*?\n(?:.*\\\n)*.*\n", header).group(0)
    types += "\n#ifdef HALO_64BIT\n" + macro + "#endif\n"
    types += structure(read("source/hs/hs_scenario_definitions.h"), "hs_syntax_node")
    return types, function(read("port/linux/game/mcc_scripts.c"), "mcc_script_call_valid")


def execute(types, source, case):
    work = Path(tempfile.mkdtemp(prefix="mcc-call-types-"))
    (work / "types.inc").write_text(types)
    (work / "under_test.inc").write_text(source)
    binary = work / "call_types"
    flags = ["-DHALO_64BIT"] if struct.calcsize("P") == 8 else []
    result = subprocess.run([os.environ.get("CC", "clang"), "-std=gnu99", "-O2", "-Wall", "-Werror",
        "-Wno-unused-function", *flags, "-I" + str(work),
        str(Path(__file__).with_name("mcc_script_call_types.c")), "-o", str(binary)],
        capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    run = subprocess.run([str(binary), case], capture_output=True, text=True, timeout=60)
    return run.returncode, (run.stdout + run.stderr).strip()


@pytest.mark.parametrize("case", ["right", "wrong", "short"])
def test_call_types(case):
    status, output = execute(*code(), case)
    assert status == 0, output


@pytest.mark.skipif(struct.calcsize("P") != 8, reason="the padded layout is a 64-bit host's")
def test_negative_control():
    types, source = code()
    source = mutated(source, "short type = HS_FUNCTION_PARAMETER_TYPE(function, argument);",
                     "short type = function->parameter_types[argument];")
    status, output = execute(types, source, "right")
    assert status == 1 and "check failed" in output, output
