"""Checks the names the map families give a map in the game's protocol
(port/linux/game/map_families.c, halo_map_families.h): a Halo PC map's
<file>@ce as OpenCE's build-145 names a Custom Edition map, custom_maps\\<file>,
HaloMD's, Halo PC retail's and a map pack's (<file>@<pack>,
maps_<pack>\\<file>.<pack>) as names no OpenCE client has, and each read
back as this port names it; and the file an Xbox-format map is read from. The family code is compiled alone with the build
machine's C compiler, with stand-ins for the game's headers."""
import json
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
#define FILE_SHARE_READ 1
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
#include <stdlib.h>
#include <string.h>
#include "cseries.h"
#include "halo_map_families.h"
char const *config_string(char const *name) { (void)name; return ""; }
char const *cache_files_map_directory(void) { return "d:\\maps\\"; }
static char const *level_at(long index, void *context)
{
	return ((char const *const *)context)[index];
}
int main(int argc, char **argv)
{
	char out[256];
	int index;
	if (!strcmp(argv[1], "unknown"))
	{
		/* (the map, version, has_platform, ce_present, mcc_found) */
		printf("%d\n", map_family_opence_unknown(argv[2], atol(argv[3]), (boolean)atoi(argv[4]),
			(boolean)atoi(argv[5]), (boolean)atoi(argv[6])));
		return 0;
	}
	if (!strcmp(argv[1], "missing"))
	{
		/* (stock, downloadable, opence_unknown) */
		printf("%d\n", map_family_missing_kind((boolean)atoi(argv[2]), (boolean)atoi(argv[3]),
			(boolean)atoi(argv[4])));
		return 0;
	}
	if (!strcmp(argv[1], "gives_up"))
	{
		/* (asked_host, advertised family, present, stock, downloadable) */
		printf("%d\n", map_family_asked_host_gives_up((boolean)atoi(argv[2]), (short)atoi(argv[3]),
			(boolean)atoi(argv[4]), (boolean)atoi(argv[5]), (boolean)atoi(argv[6])));
		return 0;
	}
	if (!strcmp(argv[1], "mcc_listed"))
	{
		/* (version, Custom Edition map here 0/1, the listed map, then this
		machine's MCC levels) */
		if (map_family_listed_mcc(argv[4], atol(argv[2]), (boolean)atoi(argv[3]), level_at, argc - 5, argv + 5,
			out, sizeof(out)))
			printf("%s\n", out);
		else
			printf("-\n");
		return 0;
	}
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
		else if (!strcmp(argv[1], "mcc"))
		{
			/* (argv[index]: the header's version, in decimal) */
			unsigned long header[2] = { 'head', strtoul(argv[index], NULL, 10) };
			printf("%d\n", map_family_header_is_mcc(header));
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
    shutil.copy(ROOT / "port/linux/include/halo_join_reasons.h", work / "halo_join_reasons.h")
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


def test_mcc_header_detected(families):
    # (cache version 13 is MCC's; Custom Edition's 609, Halo PC's 7 and the
    # Xbox's 5 are not)
    assert families("mcc", "13", "609", "7", "5", "0") == ["1", "0", "0", "0", "0"]


LEVELS = ["mcc_maps\\2900197929\\desert", "mcc_maps\\2900306344\\portent", "mcc_maps\\my_pack\\Portent",
          "mcc_maps\\3002260635\\crashsite"]


def test_opence_mcc_listing(families):
    # (OpenCE build-344's listing of a game on mcc_maps\2900306344\portent
    # names its map "portent", at network version 43:
    # tools/tests/opence_mcc/listing-344.json; the first local level with
    # that file name, the Workshop item's here)
    assert families("mcc_listed", "43", "0", "portent", *LEVELS) == ["mcc_maps\\2900306344\\portent"]
    assert families("mcc_listed", "43", "0", "PORTENT", *LEVELS) == ["mcc_maps\\2900306344\\portent"]
    assert families("mcc_listed", "42", "0", "crashsite", *LEVELS) == ["mcc_maps\\3002260635\\crashsite"]


def test_opence_mcc_listing_unchanged(families):
    # (what stays as it was: a host before MCC maps (41), a Custom Edition
    # map this machine has by the name, a map no MCC level here has, the
    # Xbox's maps, our own hosts' full names and other families' names)
    assert families("mcc_listed", "41", "0", "portent", *LEVELS) == ["-"]
    assert families("mcc_listed", "43", "1", "portent", *LEVELS) == ["-"]
    assert families("mcc_listed", "43", "0", "cairo", *LEVELS) == ["-"]
    assert families("mcc_listed", "43", "0", "bloodgulch", "mcc_maps\\1\\bloodgulch") == ["-"]
    assert families("mcc_listed", "43", "0", "mcc_maps\\2900306344\\portent", *LEVELS) == ["-"]
    assert families("mcc_listed", "43", "0", "portent@ce", *LEVELS) == ["-"]
    assert families("mcc_listed", "43", "0", "", *LEVELS) == ["-"]
    # (a level not under mcc_maps, or with no file part, is never taken)
    assert families("mcc_listed", "43", "0", "portent", "maps\\portent", "mcc_maps\\portent\\") == ["-"]


def read_listing(raw):
    # (p2p_lobby.c's listing_read, the fields up to the gametype)
    at = 5
    version = raw[3] << 8 | raw[4]
    at += 1 + 8 + 32 + 16 + 3
    texts = []
    for _ in range(3):
        length = raw[at]
        texts.append(raw[at + 1:at + 1 + length].decode())
        at += 1 + length
    return version, texts


def test_opence_mcc_listing_recorded(families):
    # (what OpenCE build-344 listed, recorded: the MCC game's map by its file
    # alone, at 43, and that read as this machine's level of it)
    fixture = json.loads((ROOT / "tools/tests/opence_mcc/listing-344.json").read_text())
    listing = next(entry for entry in fixture["listings"] if entry["fields"]["engine_type"] == 2)
    version, (name, listed_map, gametype) = read_listing(bytes.fromhex(listing["listing_hex"]))
    assert (version, name, listed_map) == (43, "Host", "portent")
    assert families("mcc_listed", str(version), "0", listed_map, *LEVELS) == [fixture["settings"]["map_name"]]
    # (the same host's lobby on an Xbox map stays the Xbox's)
    lobby = next(entry for entry in fixture["listings"] if entry["fields"]["engine_type"] == 0)
    version, (_, listed_map, _) = read_listing(bytes.fromhex(lobby["listing_hex"]))
    assert families("mcc_listed", str(version), "0", listed_map, *LEVELS) == ["-"]


# (map_family_missing_kind's answers, halo_map_families.h's order)
STOCK, DOWNLOAD, ASK_HOST, BLOCKED = "0", "1", "2", "3"


def test_opence_unknown_names(families):
    # (OpenCE's bare name, 42 on, of a map this machine has neither as an MCC
    # nor as a Custom Edition map: unknown; anything else is not)
    assert families("unknown", "cairo", "43", "0", "0", "0") == ["1"]
    assert families("unknown", "cairo", "42", "0", "0", "0") == ["1"]
    assert families("unknown", "cairo", "41", "0", "0", "0") == ["0"]
    assert families("unknown", "cairo", "43", "1", "0", "0") == ["0"]
    assert families("unknown", "cairo", "43", "0", "1", "0") == ["0"]
    assert families("unknown", "cairo", "43", "0", "0", "1") == ["0"]
    assert families("unknown", "bloodgulch", "43", "0", "0", "0") == ["0"]
    assert families("unknown", "mcc_maps\\1\\cairo", "43", "0", "0", "0") == ["0"]
    assert families("unknown", "cairo@ce", "43", "0", "0", "0") == ["0"]


def test_plain_custom_edition_games_on_opence_43(families):
    # (a Custom Edition game on an OpenCE 42+ host, its map missing here:
    # Halo PC's own map and a map that may be downloaded are said and done
    # as they always were, before the unknown name's "join to find out")
    assert families("missing", "1", "0", "1") == [STOCK]
    assert families("missing", "1", "1", "1") == [STOCK]
    assert families("missing", "0", "1", "1") == [DOWNLOAD]
    # (one that can't be downloaded: only then joined to ask the host)
    assert families("missing", "0", "0", "1") == [ASK_HOST]
    # (any other game, as before: an older host, our own, a map not bare)
    assert families("missing", "0", "0", "0") == [BLOCKED]
    assert families("missing", "0", "1", "0") == [DOWNLOAD]
    assert families("missing", "1", "0", "0") == [STOCK]


def test_asked_host_gives_up_only_that_join(families):
    # (the host names a Custom Edition map (1) that can't be had: the join let
    # through only to ask is given up; a stock or downloadable one is offered)
    assert families("gives_up", "1", "1", "0", "0", "0") == ["1"]
    assert families("gives_up", "1", "1", "0", "1", "0") == ["0"]
    assert families("gives_up", "1", "1", "0", "0", "1") == ["0"]
    assert families("gives_up", "1", "1", "1", "0", "0") == ["0"]
    # (an MCC map, mcc_maps\..., is an Xbox-format name: joined, the content
    # fingerprint decides)
    assert families("gives_up", "1", "0", "0", "0", "0") == ["0"]
    # (every other join, invites and system link included, goes on as it did)
    for family in ("0", "1", "2", "3", "4"):
        for present in ("0", "1"):
            assert families("gives_up", "0", family, present, "0", "0") == ["0"]
