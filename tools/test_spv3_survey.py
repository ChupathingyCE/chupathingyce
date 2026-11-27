"""The survey of an OpenSauce map (port/linux/game/opensauce_survey.c,
opensauce_survey_map), which SPV3 mode also runs on every map, on tag data
no map tool would write: a tag count whose size wraps in 32 bits, a tag
index, scenario, structure BSPs or script nodes outside the tag data, and a
BSP whose end wraps. The survey reads only inside the tag data and says
where it stopped. Outside SPV3 mode it logs a map with OpenSauce's header
("opensauce:") and not one without. opensauce_survey.c and spv3_mode.c are
compiled alone through the 64-bit builds' rewrite of long (tools/lp64_rewrite.py: an unsigned long is 32 bits there, as
on Windows and the 32-bit builds), with the sanitizers where the compiler has
them, and stand-ins for the game's headers."""
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

from test_ce_resource_maps import STUB_CSERIES, STUB_WINDOWS, rewrite

ROOT = Path(__file__).resolve().parent.parent
BASE = 0x40440000
SIZE = 0x4000

STUB_ERRORS = r"""
enum { _error_silent };
void error(int priority, char const *format, ...);
"""
STUB_PACKS = r"""
char const *map_pack_menus(void);
"""
HARNESS = r"""#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "opensauce_maps.h"
#include "opensauce_survey.h"
#include "spv3_mode.h"
/* (argv[2]: the pack whose menus are played, "spv3" for SPV3 mode) */
static char const *pack_menus;
char const *map_pack_menus(void) { return pack_menus; }
boolean opensauce_map_active(void) { return FALSE; }
/* (OpenSauce's larger window, opensauce_maps.c: not mapped here) */
unsigned long opensauce_maps_header_tag_cache_size(void const *cache_header, unsigned long custom_edition_size)
{
	(void)cache_header;
	return custom_edition_size;
}
void error(int priority, char const *format, ...)
{
	va_list arguments;
	(void)priority;
	va_start(arguments, format);
	vprintf(format, arguments);
	putchar('\n');
	va_end(arguments);
}
HANDLE CreateFileA(char const *path, unsigned long access, unsigned long share, void *security, unsigned long how,
	unsigned long flags, void *template_file)
{
	return INVALID_HANDLE_VALUE;
}
int CloseHandle(HANDLE file) { return 1; }
/* argv[1]: the tag data, as written by the test; argv[2]: the pack; argv[3]:
"yelo" for OpenSauce's header */
int main(int argc, char **argv)
{
	static unsigned char header[0x800];
	FILE *file = fopen(argv[1], "rb");

	pack_menus = argc > 2 && argv[2][0] ? argv[2] : NULL;
	unsigned char *tags;
	long size;

	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	tags = malloc((size_t)size);
	if (fread(tags, 1, (size_t)size, file) != (size_t)size)
		return 2;
	fclose(file);
	memcpy(header, "daeh", 4);
	header[4] = 0x61; /* 609 */
	header[5] = 0x02;
	if (argc <= 3 || !strcmp(argv[3], "yelo"))
	{
		memcpy(header + 0x70, "oley", 4);
		header[0x76] = 1;
	}
	opensauce_survey_map("hostile", header, tags, (unsigned long)size, 0x10000);
	opensauce_survey_checked("hostile", NULL);
	opensauce_survey_note_correction("after the checks");
	free(tags);
	puts("survey done");
	return 0;
}
"""


@pytest.fixture(scope="module")
def survey(tmp_path_factory):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    work = tmp_path_factory.mktemp("spv3")
    stubs = tmp_path_factory.mktemp("spv3-stubs")
    (stubs / "cseries").mkdir()
    (stubs / "cseries.h").write_text(STUB_CSERIES)
    (stubs / "cseries" / "cseries_windows.h").write_text(STUB_WINDOWS)
    (stubs / "errors.h").write_text(STUB_ERRORS)
    (stubs / "halo_map_packs.h").write_text(STUB_PACKS)
    (stubs / "harness.c").write_text(HARNESS)
    for name in ("cseries.h", "cseries/cseries_windows.h", "errors.h", "halo_map_packs.h", "harness.c"):
        rewrite(stubs / name, work / name)
    for name in ("spv3_mode.h", "opensauce.h", "opensauce_maps.h", "opensauce_survey.h"):
        rewrite(ROOT / "port/linux/include" / name, work / name)
    for name in ("spv3_mode.c", "opensauce_survey.c"):
        rewrite(ROOT / "port/linux/game" / name, work / name)
    binary = work / "survey"
    command = [cc, "-std=gnu99", "-Wall", "-Werror", "-Wno-unused-variable", "-Wno-unused-function", "-Wno-format",
               "-DHALO_MAP_LIST", "-DHALO_64BIT", "-DHALO_CUSTOM_EDITION", "-g", "-I", str(work),
               str(work / "harness.c"), str(work / "spv3_mode.c"), str(work / "opensauce_survey.c"), "-o", str(binary)]
    sanitized = command + ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
    if subprocess.run(sanitized, capture_output=True).returncode != 0:
        subprocess.run(command, check=True)

    def run(tags, pack="spv3", header="yelo"):
        path = work / "tags.bin"
        path.write_bytes(bytes(tags))
        result = subprocess.run([str(binary), str(path), pack, header], capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        assert "survey done" in result.stdout
        return result.stdout
    return run


def tag_data(tag_count=2, instances=BASE + 0x28, scenario=BASE + 0x100, bsps=1, bsp_address=BASE + 0x800,
             bsp_base=BASE + 0x3000, bsp_size=0x800, syntax=BASE + 0x900):
    tags = bytearray(SIZE)
    struct.pack_into("<4I", tags, 0, instances, 0, 0, tag_count)
    struct.pack_into("<4s", tags, 0x24, b"sgat")
    # (two tags: the scenario, and OpenSauce's yelo)
    struct.pack_into("<4s5I", tags, 0x28, b"rncs", 0, 0, 0, 0, scenario)
    struct.pack_into("<4s", tags, 0x48, b"oley")
    struct.pack_into("<iI", tags, 0x100 + 0x5A4, bsps, bsp_address)
    struct.pack_into("<3I", tags, 0x800, 0x800, bsp_size, bsp_base)
    struct.pack_into("<4I", tags, 0x100 + 0x474, 0x38, 0, 0, syntax)
    struct.pack_into("<h", tags, 0x900 + 0x20, 4)
    return tags


def test_sound_tags(survey):
    out = survey(tag_data())
    assert "hostile: 2 tags" in out
    assert "tag group yelo (project_yellow): 1 tags" in out
    assert "inside the tag window" in out


def test_tag_count_that_wraps(survey):
    # (0x08000001 * 32 is 0x20 in 32 bits: it would pass a range check made
    # after the multiply)
    out = survey(tag_data(tag_count=0x08000001))
    assert "the tag index is outside the tag data" in out


@pytest.mark.parametrize("count", [0xFFFFFFFF, SIZE // 32, SIZE // 32 + 1])
def test_tag_count_past_the_data(survey, count):
    assert "the tag index is outside the tag data" in survey(tag_data(tag_count=count))


@pytest.mark.parametrize("instances", [0, BASE - 0x20, BASE + SIZE - 0x10, 0xFFFFFFF0])
def test_tag_index_outside(survey, instances):
    assert "the tag index is outside the tag data" in survey(tag_data(instances=instances))


@pytest.mark.parametrize("scenario", [0, BASE + SIZE - 0x10, 0xFFFFFFFF])
def test_scenario_outside(survey, scenario):
    assert "the scenario is outside the tag data" in survey(tag_data(scenario=scenario))


@pytest.mark.parametrize("bsps,address", [(0x7FFFFFFF, BASE + 0x800), (64, BASE + SIZE - 0x40), (3, 0xFFFFFFF0),
                                          (-5, BASE + 0x800)])
def test_bsps_outside(survey, bsps, address):
    survey(tag_data(bsps=bsps, bsp_address=address))


def test_bsp_end_that_wraps(survey):
    out = survey(tag_data(bsp_base=0xFFFFF000, bsp_size=0x2000))
    assert "OUTSIDE the tag window" in out


@pytest.mark.parametrize("syntax", [0, BASE + SIZE - 4, 0xFFFFFFFF])
def test_script_nodes_outside(survey, syntax):
    assert "0 nodes" in survey(tag_data(syntax=syntax))


def test_spv3_mode_lines(survey):
    out = survey(tag_data())
    assert "spv3: ---- hostile: map survey ----" in out
    assert "opensauce:" not in out
    # (SPV3 mode surveys a map without the header too, as it always did)
    assert "spv3: hostile: no OpenSauce header" in survey(tag_data(), header="none")


def test_opensauce_map_outside_spv3_mode(survey):
    out = survey(tag_data(), pack="")
    assert "opensauce: ---- hostile: map survey ----" in out
    assert "opensauce: hostile: tag group yelo (project_yellow): 1 tags" in out
    assert "spv3:" not in out
    # (once its checks are done, the map's log follows the module, which is
    # off in this harness)
    assert "after the checks" not in out


def test_custom_edition_map_outside_spv3_mode(survey):
    out = survey(tag_data(), pack="", header="none")
    assert "survey" not in out.replace("survey done", "")
    assert "spv3:" not in out and "opensauce:" not in out


if __name__ == "__main__":
    sys.exit(pytest.main([__file__]))
