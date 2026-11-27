"""SPV3's menus start a level with map_name (port/linux/game/spv3_mode.c,
source/hs/hs.c's hs_map_name_set): only SPV3's own maps_spv3\\ui.yelo may,
not a ui the loader fell back to, and only for a plain name of one of
maps_spv3's .yelo levels; any other name is dropped and said once.
spv3_mode.c is compiled alone as in test_spv3_survey.py, with a file system
of the names the test gives."""
import shutil
import subprocess

import pytest

from test_ce_resource_maps import STUB_CSERIES, STUB_WINDOWS, rewrite
from test_spv3_survey import ROOT, STUB_ERRORS, STUB_PACKS

HARNESS = r"""#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "spv3_mode.h"
static char const *const files[] =
{
	"d:\\maps_spv3\\ui.yelo", "d:\\maps_spv3\\spv3a10.yelo", "d:\\maps_spv3\\bitmaps.map",
	"d:\\maps_ce\\ui.map", "d:\\maps_spv3\\..\\maps\\a10.yelo",
};
char const *map_pack_menus(void) { return "spv3"; }
void error(int priority, char const *format, ...)
{
	va_list arguments;
	(void)priority;
	va_start(arguments, format);
	printf("log: ");
	vprintf(format, arguments);
	putchar('\n');
	va_end(arguments);
}
HANDLE CreateFileA(char const *path, unsigned long access, unsigned long share, void *security, unsigned long how,
	unsigned long flags, void *template_file)
{
	size_t index;

	for (index = 0; index < sizeof(files) / sizeof(files[0]); index++)
	{
		if (!strcmp(path, files[index]))
			return (HANDLE)1;
	}
	return INVALID_HANDLE_VALUE;
}
int CloseHandle(HANDLE file) { return 1; }
/* argv: "open <map> <path>", "own <map>" or "level <name>", in turn */
int main(int argc, char **argv)
{
	int index;

	for (index = 1; index < argc; index++)
	{
		if (!strcmp(argv[index], "open") && index + 2 < argc)
		{
			spv3_mode_note_map_opened(argv[index + 1], argv[index + 2]);
			index += 2;
		}
		else if (!strcmp(argv[index], "own") && index + 1 < argc)
		{
			printf("own %s: %d\n", argv[index + 1], spv3_mode_own_ui_open(argv[index + 1]) ? 1 : 0);
			index++;
		}
		else if (!strcmp(argv[index], "level") && index + 1 < argc)
		{
			char name[64];
			int resolved = spv3_mode_level_name(argv[index + 1], name, sizeof(name)) ? 1 : 0;

			printf("level %s: %d %s\n", argv[index + 1], resolved, name);
			index++;
		}
	}
	return 0;
}
"""


@pytest.fixture(scope="module")
def run(tmp_path_factory):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    work = tmp_path_factory.mktemp("spv3-levels")
    stubs = tmp_path_factory.mktemp("spv3-levels-stubs")
    (stubs / "cseries").mkdir()
    (stubs / "cseries.h").write_text(STUB_CSERIES)
    (stubs / "cseries" / "cseries_windows.h").write_text(STUB_WINDOWS)
    (stubs / "errors.h").write_text(STUB_ERRORS)
    (stubs / "halo_map_packs.h").write_text(STUB_PACKS)
    (stubs / "harness.c").write_text(HARNESS)
    for name in ("cseries.h", "cseries/cseries_windows.h", "errors.h", "halo_map_packs.h", "harness.c"):
        rewrite(stubs / name, work / name)
    rewrite(ROOT / "port/linux/include/spv3_mode.h", work / "spv3_mode.h")
    # (OpenSauce's map log, which SPV3 mode's corrections go to: empty here)
    rewrite(ROOT / "port/linux/include/opensauce_survey.h", work / "opensauce_survey.h")
    rewrite(ROOT / "port/linux/game/spv3_mode.c", work / "spv3_mode.c")
    binary = work / "levels"
    command = [cc, "-std=gnu99", "-Wall", "-Werror", "-Wno-unused-variable", "-Wno-unused-function", "-Wno-format",
               "-DHALO_MAP_LIST", "-DHALO_64BIT", "-g", "-I", str(work), str(work / "harness.c"), str(work / "spv3_mode.c"),
               "-o", str(binary)]
    sanitized = command + ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
    if subprocess.run(sanitized, capture_output=True).returncode != 0:
        subprocess.run(command, check=True)

    def go(*arguments):
        result = subprocess.run([str(binary), *arguments], capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout
    return go


def test_own_ui_may_change_level(run):
    assert "own ui: 1" in run("open", "ui", "d:\\maps_spv3\\ui.yelo", "own", "ui")


def test_fallback_ui_may_not(run):
    out = run("open", "ui", "d:\\maps_ce\\ui.map", "own", "ui")
    assert "own ui: 0" in out
    assert "may not change the level" in out


def test_fallback_after_own_ui(run):
    assert "own ui: 0" in run("open", "ui", "d:\\maps_spv3\\ui.yelo", "open", "ui", "d:\\maps_ce\\ui.map", "own", "ui")


def test_only_the_ui(run):
    assert "own spv3a10: 0" in run("open", "ui", "d:\\maps_spv3\\ui.yelo", "own", "spv3a10")


def test_nothing_opened(run):
    assert "own ui: 0" in run("own", "ui")


def test_level_resolves(run):
    assert "level spv3a10: 1 spv3a10@ce" in run("level", "spv3a10")


@pytest.mark.parametrize("name", ["spv3_bridge_flood", "spv3_engine_covenant", "a10", "ui", "bitmaps", ""])
def test_unresolved_dropped(run, name):
    out = run("level", name)
    assert f"level {name}: 0 \n" in out
    assert "dropped" in out


@pytest.mark.parametrize("name", ["..\\maps\\a10", "../maps/a10", "spv3a10@ce", "d:spv3a10", "spv3a10.yelo",
                                  "spv3 a10", "x" * 40])
def test_odd_names_dropped(run, name):
    assert f"level {name}: 0 \n" in run("level", name)


def test_dropped_said_once(run):
    out = run("level", "spv3_bridge_flood", "level", "spv3_bridge_flood", "level", "spv3_bridge_spartan")
    assert out.count("map_name spv3_bridge_flood is not a level") == 1
    assert out.count("map_name spv3_bridge_spartan is not a level") == 1
