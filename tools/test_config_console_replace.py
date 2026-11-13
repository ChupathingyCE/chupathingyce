"""config.toml put in place on a console (port/linux/src/port_config.c,
config_console_replace and config_console_recover, HALO_XBOX_CONSOLE): its
rename may not replace a file, so the old file is moved aside first. No
step that fails loses the settings, and a config.toml.new is taken in place
of a missing config.toml only when config.toml.old shows it was finished.

The two functions are compiled out of the file with the build machine's C
compiler, with a rename that fails on the calls a case names."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def function(source, name):
    """A function's text, from its return type to its closing brace."""
    match = re.search(r"^static (?:int|void) " + name + r"\([^;{]*\)\s*\{", source, re.M)
    assert match, name
    start = source.index("{", match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


PROGRAM = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (the calls of rename, counted from 1, that fail: FAIL="1,3") */
static int test_rename(const char *from, const char *to)
{
	static int call;
	const char *fail = getenv("FAIL");
	char token[16];

	call++;
	snprintf(token, sizeof(token), ",%d,", call);
	if (fail)
	{
		char list[256];

		snprintf(list, sizeof(list), ",%s,", fail);
		if (strstr(list, token))
			return -1;
	}
	return rename(from, to);
}
#define rename test_rename
''' + "@FUNCTIONS@\n" + r'''
int main(int argc, char **argv)
{
	char path[1100], temporary[1100];

	snprintf(path, sizeof(path), "%s/config.toml", argv[2]);
	snprintf(temporary, sizeof(temporary), "%s.new", path);
	if (!strcmp(argv[1], "replace"))
		printf("%d\n", config_console_replace(path, temporary));
	else
		config_console_recover(path);
	return 0;
}
'''


@pytest.fixture(scope="module")
def program(tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    source = (ROOT / "port/linux/src/port_config.c").read_text()
    work = tmp_path_factory.mktemp("console_replace")
    (work / "main.c").write_text(PROGRAM.replace("@FUNCTIONS@", function(source, "config_console_replace") + "\n"
                                            + function(source, "config_console_recover")))
    binary = work / "main"
    result = subprocess.run([cc, "-std=c11", "-Wall", "-Werror", "-o", str(binary), str(work / "main.c")],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return binary


def run(program, folder, *arguments, fail=None):
    environment = {"FAIL": fail} if fail else {}
    result = subprocess.run([str(program), *arguments, str(folder)], capture_output=True, text=True,
                            env=environment, check=True)
    return result.stdout.strip()


def files(folder):
    return {path.name: path.read_text() for path in folder.iterdir()}


def test_rename_replaces(program, tmp_path):
    """a rename that replaces the file: the new one in place, nothing left"""
    (tmp_path / "config.toml").write_text("old")
    (tmp_path / "config.toml.new").write_text("new")
    assert run(program, tmp_path, "replace") == "1"
    assert files(tmp_path) == {"config.toml": "new"}


def test_old_moved_aside(program, tmp_path):
    """a rename that does not replace a file: the old one aside, the new one
    in, the old one removed"""
    (tmp_path / "config.toml").write_text("old")
    (tmp_path / "config.toml.new").write_text("new")
    assert run(program, tmp_path, "replace", fail="1") == "1"
    assert files(tmp_path) == {"config.toml": "new"}


def test_second_rename_fails(program, tmp_path):
    """the new file not renamed in after the old one went aside: the old one
    is put back and the finished new one kept"""
    (tmp_path / "config.toml").write_text("old")
    (tmp_path / "config.toml.new").write_text("new")
    assert run(program, tmp_path, "replace", fail="1,3") == "-1"
    assert files(tmp_path) == {"config.toml": "old", "config.toml.new": "new"}


def test_nothing_put_back(program, tmp_path):
    """the old one not put back either: config.toml is gone, but the next
    start takes the finished new one (config.toml.old shows it finished)"""
    (tmp_path / "config.toml").write_text("old")
    (tmp_path / "config.toml.new").write_text("new")
    assert run(program, tmp_path, "replace", fail="1,3,4") == "-1"
    assert files(tmp_path) == {"config.toml.old": "old", "config.toml.new": "new"}
    run(program, tmp_path, "recover")
    assert files(tmp_path) == {"config.toml": "new"}


def test_old_put_back_without_new(program, tmp_path):
    """only config.toml.old left: it goes back"""
    (tmp_path / "config.toml.old").write_text("old")
    run(program, tmp_path, "recover")
    assert files(tmp_path) == {"config.toml": "old"}


def test_partial_first_write_left_alone(program, tmp_path):
    """the very first write cut short leaves part of a config.toml.new and
    no config.toml.old: it is not taken as the settings"""
    (tmp_path / "config.toml.new").write_text("[display]\nvsy")
    run(program, tmp_path, "recover")
    assert files(tmp_path) == {"config.toml.new": "[display]\nvsy"}
