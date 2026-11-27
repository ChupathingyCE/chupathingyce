"""How the game reads an OpenSauce map (port/linux/game/opensauce_maps.c),
outside SPV3 mode as in it: which of its header's flags are accepted (memory
upgrades only, and only with the larger tag window mapped), and the tag
cache's size, which is OpenSauce's only for the map that asks for the
memory upgrades (being checked, or loaded) and for every map in SPV3 mode,
as that mode always read them. opensauce_maps.c is compiled alone through
the 64-bit builds' rewrite of long, with stand-ins for the OpenSauce module
and SPV3 mode."""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from test_ce_resource_maps import STUB_CSERIES, STUB_WINDOWS, rewrite

ROOT = Path(__file__).resolve().parent.parent
STOCK = 0x01700000
OPENSAUCE = 0x02280000
UPGRADES, MOD_DATA, PROTECTED = 1, 2, 4

STUB_ERRORS = r"""
enum { _error_silent };
void error(int priority, char const *format, ...);
"""
HARNESS = r"""#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "opensauce.h"
#include "opensauce_maps.h"
#include "opensauce_survey.h"
#include "spv3_mode.h"

static int spv3;
static boolean loaded;
static struct opensauce_header loaded_header;

boolean spv3_mode_active(void) { return spv3 ? TRUE : FALSE; }
struct opensauce_header const *opensauce_map_header(void) { return loaded ? &loaded_header : NULL; }
boolean opensauce_header_read(void const *cache_header, struct opensauce_header *header)
{
	unsigned char const *bytes = (unsigned char const *)cache_header + OPENSAUCE_HEADER_OFFSET;

	if (memcmp(bytes, "oley", 4))
		return FALSE;
	memset(header, 0, sizeof(*header));
	header->flags = (unsigned short)(bytes[6] | (bytes[7] << 8));
	return TRUE;
}
char const *opensauce_header_flag_name(short bit) { return bit == 2 ? "is_protected" : "another"; }
void opensauce_survey_log(char const *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	printf("log: ");
	vprintf(format, arguments);
	putchar('\n');
	va_end(arguments);
}

/* each argument a step: window N, spv3 N, loaded FLAGS|none, start, accept
FLAGS, done, size, header FLAGS|none (the size for that header), upgrades */
int main(int argc, char **argv)
{
	int index;

	for (index = 1; index < argc; index++)
	{
		char const *step = argv[index];
		char const *value = strchr(step, ' ') ? strchr(step, ' ') + 1 : "";
		unsigned short flags = (unsigned short)strtoul(value, NULL, 0);

		if (!strncmp(step, "window", 6))
			opensauce_tag_window = atoi(value);
		else if (!strncmp(step, "spv3", 4))
			spv3 = atoi(value);
		else if (!strncmp(step, "loaded", 6))
		{
			loaded = strcmp(value, "none") != 0;
			loaded_header.flags = flags;
		}
		else if (!strcmp(step, "start"))
			opensauce_maps_check_started();
		else if (!strcmp(step, "done"))
			opensauce_maps_check_done();
		else if (!strncmp(step, "accept", 6))
			printf("accept %d\n", opensauce_maps_accepts_header("test", flags) ? 1 : 0);
		else if (!strcmp(step, "size"))
			printf("size 0x%08lX\n", opensauce_maps_tag_cache_size(0x01700000UL));
		else if (!strcmp(step, "upgrades"))
			printf("upgrades %d\n", opensauce_maps_memory_upgrades() ? 1 : 0);
		else if (!strncmp(step, "header", 6))
		{
			static unsigned char header[0x800];

			memset(header, 0, sizeof(header));
			if (strcmp(value, "none"))
			{
				memcpy(header + OPENSAUCE_HEADER_OFFSET, "oley", 4);
				header[OPENSAUCE_HEADER_OFFSET + 6] = (unsigned char)flags;
			}
			printf("header size 0x%08lX\n", opensauce_maps_header_tag_cache_size(header, 0x01700000UL));
		}
	}
	return 0;
}
"""


def build(tmp_path_factory, name, defines):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    work = tmp_path_factory.mktemp(name)
    stubs = tmp_path_factory.mktemp(name + "-stubs")
    (stubs / "cseries").mkdir()
    (stubs / "cseries.h").write_text(STUB_CSERIES)
    (stubs / "cseries" / "cseries_windows.h").write_text(STUB_WINDOWS)
    (stubs / "errors.h").write_text(STUB_ERRORS)
    (stubs / "harness.c").write_text(HARNESS)
    for stub in ("cseries.h", "cseries/cseries_windows.h", "errors.h", "harness.c"):
        rewrite(stubs / stub, work / stub)
    for header in ("opensauce.h", "opensauce_maps.h", "opensauce_survey.h", "spv3_mode.h"):
        rewrite(ROOT / "port/linux/include" / header, work / header)
    rewrite(ROOT / "port/linux/game/opensauce_maps.c", work / "opensauce_maps.c")
    binary = work / "maps"
    command = [cc, "-std=gnu99", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-format", "-DHALO_CUSTOM_EDITION",
               *defines, "-g", "-I", str(work), str(work / "harness.c"), str(work / "opensauce_maps.c"),
               "-o", str(binary)]
    sanitized = command + ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
    if subprocess.run(sanitized, capture_output=True).returncode != 0:
        subprocess.run(command, check=True)

    def run(*steps):
        result = subprocess.run([str(binary), *steps], capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout
    return run


@pytest.fixture(scope="module")
def maps(tmp_path_factory):
    return build(tmp_path_factory, "opensauce-maps", ["-DHALO_MAP_LIST", "-DHALO_64BIT"])


@pytest.fixture(scope="module")
def maps_32(tmp_path_factory):
    return build(tmp_path_factory, "opensauce-maps-32", ["-DHALO_MAP_LIST"])


def test_memory_upgrades_need_the_window(maps):
    out = maps("start", f"accept {UPGRADES}", "size", "done")
    assert "accept 0" in out
    assert "which is not mapped" in out
    assert f"size 0x{STOCK:08X}" in out


def test_memory_upgrades_with_the_window(maps):
    out = maps("window 1", "start", f"accept {UPGRADES}", "size", "upgrades", "done", "size")
    assert "accept 1" in out
    assert f"size 0x{OPENSAUCE:08X}" in out.splitlines()[2]
    assert "upgrades 1" in out
    # (once the checks end, the size is the loaded map's: none here)
    assert out.splitlines()[-1] == f"size 0x{STOCK:08X}"


@pytest.mark.parametrize("flags", [MOD_DATA, PROTECTED, UPGRADES | PROTECTED, 0x10, 0x8000])
def test_other_flags_refused(maps, flags):
    out = maps("window 1", "start", f"accept {flags}", "size")
    assert "accept 0" in out
    assert "not supported, refused" in out
    assert f"size 0x{STOCK:08X}" in out


def test_loaded_map_size(maps):
    out = maps("window 1", f"loaded {UPGRADES}", "size", "loaded 0", "size", "loaded none", "size")
    assert out.split() == ["size", f"0x{OPENSAUCE:08X}", "size", f"0x{STOCK:08X}", "size", f"0x{STOCK:08X}"]


def test_a_check_keeps_to_its_own_map(maps):
    # (a Custom Edition map checked while an OpenSauce map with the upgrades
    # is loaded: the check sees Custom Edition's size, the loaded map its own)
    out = maps("window 1", f"loaded {UPGRADES}", "start", "size", "done", "size")
    assert out.split() == ["size", f"0x{STOCK:08X}", "size", f"0x{OPENSAUCE:08X}"]


def test_spv3_mode_reads_every_map_so(maps):
    out = maps("window 1", "spv3 1", "loaded none", "size", "start", "size", "done", "header none")
    assert out.split() == ["size", f"0x{OPENSAUCE:08X}", "size", f"0x{OPENSAUCE:08X}", "header", "size",
                           f"0x{OPENSAUCE:08X}"]
    # (and without the window, Custom Edition's, as before)
    assert maps("spv3 1", "size").split() == ["size", f"0x{STOCK:08X}"]


def test_header_size(maps):
    out = maps("window 1", f"header {UPGRADES}", "header 0", "header none", "window 0", f"header {UPGRADES}")
    assert [line.split()[-1] for line in out.splitlines()] == [f"0x{OPENSAUCE:08X}", f"0x{STOCK:08X}",
                                                               f"0x{STOCK:08X}", f"0x{STOCK:08X}"]


def test_32_bit_build_never_reads_the_upgrades(maps_32):
    out = maps_32("window 1", "start", f"accept {UPGRADES}", "size", "done", f"header {UPGRADES}")
    assert "accept 0" in out
    assert f"0x{OPENSAUCE:08X}" not in out


if __name__ == "__main__":
    sys.exit(pytest.main([__file__]))
