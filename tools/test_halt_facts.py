"""The halt screen's facts (source/main/halt_facts.c): the log lines it
shows before a halt (the stack walk, the halt's own EXCEPTION line and
debug.txt's header left out, dates and the port's prefix left off, a line
repeated in a row made one, errors and warnings kept first), the assert's
place as a short path, and the map's name without its family."""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent

DRIVER = r'''
#include "halt_facts.h"
#include <stdio.h>
#include <string.h>

static char log_text[65536];

int main(int argc, char **argv)
{
	struct halt_facts facts;
	char out[256];
	size_t length;
	int index;

	if (argc > 1 && !strcmp(argv[1], "place"))
	{
		halt_facts_place(out, (int)sizeof(out), argv[2], atol(argv[3]));
		printf("%s\n", out);
		return 0;
	}
	if (argc > 1 && !strcmp(argv[1], "map"))
	{
		halt_facts_map(out, 64, argv[2]);
		printf("%s\n", out);
		return 0;
	}
	length = fread(log_text, 1, sizeof(log_text), stdin);
	halt_facts_log_lines(&facts, log_text, (int)length);
	for (index = 0; index < facts.line_count; index++)
		printf("%d|%s\n", facts.important[index], facts.lines[index]);
	return 0;
}
'''


@pytest.fixture(scope="module")
def tool(tmp_path_factory):
    clang = shutil.which("clang")
    if not clang or sys.platform == "win32":
        pytest.skip("clang required")
    work = tmp_path_factory.mktemp("halt-facts")
    (work / "driver.c").write_text(DRIVER.replace("#include <string.h>", "#include <string.h>\n#include <stdlib.h>"))
    exe = work / "test"
    result = subprocess.run([clang, "-std=gnu89", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "source/main"),
                             str(ROOT / "source/main/halt_facts.c"), str(work / "driver.c"), "-o", str(exe)],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return exe


def run(tool, *arguments, text=""):
    return subprocess.run([str(tool), *arguments], input=text.encode(), capture_output=True,
                          check=True).stdout.decode().splitlines()


def test_the_walk_and_the_halt_line_are_left_out(tool):
    log = ("\r\n\r\n11.17.26 10:00:00  halobeta xbox 01.01.14.2342(CACHE) ---\r\n"
           "11.17.26 10:00:00  reference function: _write_to_error_file\r\n"
           "11.17.26 10:00:00  one\r\n"
           "chupathingyce: two\r\n"
           "11.17.26 10:00:01  Printing stuff for Mat's edification\r\n"
           "11.17.26 10:00:01  0x1 main+44\r\n"
           "11.17.26 10:00:01  0x2 main_crash+80\r\n"
           "11.17.26 10:00:01  EXCEPTION halt in c:\\halo\\SOURCE\\main\\main.c,#3: bad\r\n")
    assert run(tool, text=log) == ["0|one", "0|two"]


def test_a_release_halt_keeps_the_lines_above_it(tool):
    log = "a\nb\nEXCEPTION assert in x,#1: y (release build)\nafter\n"
    assert run(tool, text=log) == ["0|a", "0|b"]


def test_repeats_are_one_line_and_errors_stay(tool):
    lines = ["couldn't open bitmap x"] + [f"note {n}" for n in range(15)] + ["lights: same"] * 5
    lines += ["shaders: 88 programs made, 0 failed"]
    shown = run(tool, text="".join(f"chupathingyce: {line}\n" for line in lines))
    assert shown[0] == "1|couldn't open bitmap x"
    assert "0|lights: same (x5)" in shown
    assert shown[-1] == "0|shaders: 88 programs made, 0 failed"
    assert len(shown) == 10


def test_no_log(tool):
    assert run(tool, text="") == []


@pytest.mark.parametrize("file, line, shown", [
    ("c:\\halo\\SOURCE\\cseries\\errors.c", "97", "cseries/errors.c:97"),
    ("c:\\halo\\source\\hs\\hs_library_internal_runtime.h", "273", "hs/hs_library_internal_runtime.h:273"),
    ("/home/someone/repo/port/linux/src/a.c", "5", "port/linux/src/a.c:5"),
    ("plain.c", "0", "plain.c"),
    ("", "4", ""),
])
def test_place(tool, file, line, shown):
    assert run(tool, "place", file, line) == [shown]


def test_map(tool):
    assert run(tool, "map", "bloodgulch@ce") == ["bloodgulch"]
    assert run(tool, "map", "a10") == ["a10"]
