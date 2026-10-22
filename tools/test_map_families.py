"""Checks the names the map families give a map in the game's protocol
(port/linux/game/map_families.c, halo_map_families.h): a Halo PC map's
<file>@ce as OpenCE's build-145 names a Custom Edition map, custom_maps\\<file>,
HaloMD's, Halo PC retail's and a map pack's (<file>@<pack>,
maps_<pack>\\<file>.<pack>) as names no OpenCE client has, and each read
back as this port names it; and the file an Xbox-format map is read from. The family code is compiled alone with the build
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
typedef void *HANDLE;
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define GENERIC_READ 1
#define GENERIC_WRITE 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define FILE_ATTRIBUTE_NORMAL 0
#define FILE_END 2
#define CreateFileA(path, access, share, security, how, flags, template) INVALID_HANDLE_VALUE
#define ReadFile(file, buffer, size, count, overlapped) (*(count) = 0)
#define WriteFile(file, buffer, size, count, overlapped) (*(count) = 0)
#define CloseHandle(file) 0
#define SetFilePointer(file, a, b, c) 0
char const *config_string(char const *name);
"""
HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "cseries.h"
#include "halo_map_families.h"
char const *config_string(char const *name) { (void)name; return ""; }
char const *cache_files_map_directory(void) { return "d:\\maps\\"; }
int main(int argc, char **argv)
{
	char out[256];
	int index;
	for (index = 2; index < argc; index++)
	{
		short family;
		if (!strcmp(argv[1], "listed"))
		{
			family = map_family_parse_listed(argv[index], out, sizeof(out));
			printf("%d %s\n", family, out);
		}
		else if (!strcmp(argv[1], "path"))
		{
			char pack[16];
			int ok = map_family_xbox_path(argv[index], out, sizeof(out));
			family = map_family_parse(argv[index], NULL, 0);
			map_family_pack_id(argv[index], pack, sizeof(pack));
			printf("%d %d %s %s\n", family, ok, out, pack[0] ? pack : "-");
		}
		else if (!strcmp(argv[1], "wire"))
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


def test_listed_games_by_bare_name(families):
    # (OpenCE's hosts announce every map by its file name alone: the Xbox's
    # maps stay the Xbox's, any other is a Halo PC map, as custom_maps\<name>
    # is in the game's protocol)
    assert families("listed", "bloodgulch", "deathisland", "Icefields", "hugeass", "a30",
                    "levels\\test\\hangemhigh\\hangemhigh", "deathisland@ce", "x@md", "bloodgulch@pc", "") == [
        "0 bloodgulch", "1 deathisland", "1 Icefields", "1 hugeass", "0 a30",
        "0 hangemhigh", "1 deathisland", "2 x", "3 bloodgulch", "0 "]


def test_pack_names(families):
    # (a map pack's map: <file>@<pack>, its pack 1 to 8 letters and digits,
    # in its own folder; the other families' suffixes and the Xbox's language
    # folders are no pack's)
    assert families("path", "bloodgulch@cex", "Bloodgulch@CEX", "bloodgulch@xyz", "bloodgulch", "ui",
                    "levels\\test\\bloodgulch\\bloodgulch", "x@md", "x@de", "x@toolongname", "@cex", "a@b@cex") == [
        "4 1 d:\\maps_cex\\bloodgulch.map cex", "4 1 d:\\maps_cex\\Bloodgulch.map cex",
        "4 1 d:\\maps_xyz\\bloodgulch.map xyz", "0 1 d:\\maps\\bloodgulch.map -", "0 1 d:\\maps\\ui.map -",
        "0 1 d:\\maps\\levels\\test\\bloodgulch\\bloodgulch.map -", "2 1 d:\\maps\\x@md.map -",
        "0 1 d:\\maps\\x@de.map -", "0 1 d:\\maps\\x@toolongname.map -", "0 1 d:\\maps\\@cex.map -",
        "4 1 d:\\maps_cex\\a@b.map cex"]


def test_pack_wire_names(families):
    # (never the retail name: a build before packs reads maps_cex\\<file>.cex
    # as a map it doesn't have)
    assert families("wire", "bloodgulch@cex", "putput@xyz") == ["maps_cex\\bloodgulch.cex", "maps_xyz\\putput.xyz"]
    assert families("read", "maps_cex\\bloodgulch.cex", "MAPS_CEX\\Wizard.CEX", "maps_xyz\\putput.xyz",
                    "maps_cex\\bloodgulch.xyz", "maps_cex\\bloodgulch", "maps_de\\x.de", "maps_cex\\a\\b.cex",
                    "maps_cex\\.cex") == [
        "4 bloodgulch@cex", "4 Wizard@cex", "4 putput@xyz", "0 maps_cex\\bloodgulch.xyz", "0 maps_cex\\bloodgulch",
        "0 maps_de\\x.de", "0 maps_cex\\a\\b.cex", "0 maps_cex\\.cex"]


def test_pack_listed(families):
    assert families("listed", "bloodgulch@cex", "longest@xyz") == ["4 bloodgulch", "4 longest"]
