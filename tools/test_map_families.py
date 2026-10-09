"""Checks the names the map families give a map in the game's protocol
(port/linux/game/map_families.c, halo_map_families.h): a Halo PC map's
<file>@ce as OpenCE's build-145 names a Custom Edition map, custom_maps\\<file>,
HaloMD's and Halo PC retail's as names no OpenCE client has, and each read
back as this port names it. The family code is compiled alone with the build
machine's C compiler, with stand-ins for the game's headers."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent

STUB_CSERIES = """
#include <string.h>
#include <strings.h>
typedef unsigned char boolean;
#define TRUE 1
#define FALSE 0
#define NUMBEROF(a) (sizeof(a) / sizeof((a)[0]))
"""
STUB_WINDOWS = """
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
"""
HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "halo_map_families.h"
int main(int argc, char **argv)
{
	char out[256];
	int index;
	for (index = 2; index < argc; index++)
	{
		short family;
		if (!strcmp(argv[1], "wire"))
		{
			map_family_wire_name(argv[index], out, sizeof(out));
			printf("%s\n", out);
		}
		else
		{
			family = map_family_from_wire_name(argv[index], out, sizeof(out));
			printf("%d %s\n", family, out);
		}
	}
	return 0;
}
"""


@pytest.fixture(scope="module")
def families(tmp_path_factory):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    work = tmp_path_factory.mktemp("families")
    (work / "cseries").mkdir()
    (work / "cseries.h").write_text(STUB_CSERIES)
    (work / "cseries" / "cseries_windows.h").write_text(STUB_WINDOWS)
    (work / "harness.c").write_text(HARNESS)
    # (the header alone: port/linux/include's stdio.h is the game's)
    shutil.copy(ROOT / "port/linux/include/halo_map_families.h", work / "halo_map_families.h")
    binary = work / "families"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Werror", "-I", str(work),
                    str(work / "harness.c"), str(ROOT / "port/linux/game/map_families.c"), "-o", str(binary)],
                   check=True)

    def run(mode, *names):
        output = subprocess.run([str(binary), mode, *names], check=True, capture_output=True, text=True).stdout
        return output.splitlines()
    return run


def test_wire_names(families):
    assert families("wire", "levels\\test\\bloodgulch\\bloodgulch", "infinity@ce", "phoenix3_15@md",
                    "bloodgulch@pc") == [
        "levels\\test\\bloodgulch\\bloodgulch", "custom_maps\\infinity", "maps_md\\phoenix3_15.md",
        "maps_pc\\bloodgulch.pc"]


def test_wire_names_read_back(families):
    assert families("read", "custom_maps\\infinity", "CUSTOM_MAPS\\Infinity", "maps_md\\phoenix3_15.md",
                    "maps_pc\\bloodgulch.pc", "levels\\test\\bloodgulch\\bloodgulch", "infinity@ce",
                    "phoenix3_15@md") == [
        "1 infinity@ce", "1 Infinity@ce", "2 phoenix3_15@md", "3 bloodgulch@pc",
        "0 levels\\test\\bloodgulch\\bloodgulch", "1 infinity@ce", "2 phoenix3_15@md"]


def test_wire_names_not_files(families):
    # (a folder, a family's suffix or nothing in the file's place is no map
    # of a family: the name stays as it came)
    assert families("read", "custom_maps\\", "custom_maps\\a\\b", "custom_maps\\x@md", "maps_md\\name",
                    "maps_pc\\.pc") == [
        "0 custom_maps\\", "0 custom_maps\\a\\b", "2 custom_maps\\x@md", "0 maps_md\\name", "0 maps_pc\\.pc"]
