"""display.match_timer (port/linux/src/port_config.c): once true or false,
now "off", "remaining" or "elapsed". An older file's true reads as
"remaining" (what it showed: the time left) and false as "off", so nobody's
timer changes; display.match_timer_position is "top" unless set.

Built and run as tools/test_config_upgrade.py does: port_config.c alone with
a stand-in for the platform layer, on files in a scratch folder."""
import os
import shutil
import subprocess

import pytest

from test_config_upgrade import SOURCE, TOML, compiler, sdl_flags

MAIN = r"""
#include <stdarg.h>
#include <stdio.h>
#include "port_config.h"
#include "posix.h"

/* (no file is followed for changes here) */
int posix_stat(const char *path, struct posix_file_information *information)
{
	(void)path; (void)information;
	return -1;
}

void platform_log(const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	vprintf(format, arguments);
	printf("\n");
	va_end(arguments);
}

int main(void)
{
	printf("TIMER %s\n", config_string("display.match_timer"));
	printf("POSITION %s\n", config_string("display.match_timer_position"));
	return 0;
}
"""


@pytest.fixture(scope="module")
def binary(tmp_path_factory):
    cc, flags = compiler(), sdl_flags()
    if not cc or flags is None:
        pytest.skip("needs a C compiler and SDL3")
    work = tmp_path_factory.mktemp("match_timer")
    (work / "main.c").write_text(MAIN)
    (work / "platform.h").write_text(
        "void platform_log(const char *format, ...);\n"
        "int platform_app_folder(char *path, unsigned long size);\n")
    for name in ("port_config.c", "port_config.h"):
        shutil.copy(SOURCE / name, work / name)
    # (port/linux/include's other headers stand in for the C library's: only the halo_ ones)
    for header in (SOURCE.parent / "include").glob("halo_*.h"):
        shutil.copy(header, work)
    out = work / "config_test"
    done = subprocess.run(
        [cc, "-std=gnu11", "-w", "-I", str(work), "-I", str(SOURCE), "-I", str(TOML), "-o", str(out),
         str(work / "main.c"), str(work / "port_config.c"), str(TOML / "tomlc17.c"), *flags, "-lpthread"],
        capture_output=True, text=True)
    if done.returncode != 0:
        pytest.skip("port_config.c does not build alone here: " + done.stderr[:600])
    return out


def read(binary, tmp_path, display, environment=None):
    folder = tmp_path / "root"
    folder.mkdir()
    beside = tmp_path / "bin"
    beside.mkdir()
    text = "# Halo settings\n\nconfig_version = 2\n\n[display]\n\n" + display
    (folder / "config.toml").write_text(text)
    (beside / "config.toml").write_text(text)
    shutil.copy(binary, beside / "config_test")
    env = {k: v for k, v in os.environ.items() if not k.startswith("HALO_MATCH_TIMER")}
    env.update(environment or {}, HALO_SAVE_ROOT=str(folder))
    done = subprocess.run([str(beside / "config_test")], capture_output=True, text=True, env=env, cwd=beside)
    assert done.returncode == 0, done.stderr
    lines = dict(line.split(" ", 1) for line in done.stdout.splitlines() if line.startswith(("TIMER ", "POSITION ")))
    return lines["TIMER"], lines["POSITION"]


@pytest.mark.parametrize("line, timer", [
    ("", "off"),
    ("match_timer = true\n", "remaining"),
    ("match_timer = false\n", "off"),
    ('match_timer = "remaining"\n', "remaining"),
    ('match_timer = "Elapsed"\n', "elapsed"),
])
def test_old_and_new_values(binary, tmp_path, line, timer):
    assert read(binary, tmp_path, line) == (timer, "top")


def test_position(binary, tmp_path):
    assert read(binary, tmp_path, 'match_timer = true\nmatch_timer_position = "bottom_right"\n') == \
        ("remaining", "bottom_right")


@pytest.mark.parametrize("value, timer", [("1", "remaining"), ("true", "remaining"), ("0", "off"), ("elapsed", "elapsed")])
def test_environment(binary, tmp_path, value, timer):
    assert read(binary, tmp_path, "", {"HALO_MATCH_TIMER": value})[0] == timer
