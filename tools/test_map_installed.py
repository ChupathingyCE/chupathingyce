"""Checks whether a host has a map to set a game on (cache_files_map_installed,
source/cache/cache_files.c, #1106) over a maps tree written here: an Xbox map in
maps, a Custom Edition map in maps_ce with and without Custom Edition's resource
maps beside it, a HaloMD map in maps_md (and a console that doesn't play them),
a pack's map (<name>@<pack>) in its maps_<pack>, and an MCC-format map among
Custom Edition's, which counts as missing; an MCC level
(mcc_maps\\<package>\\<level>) is installed if its map is. The function is taken from the game's source and compiled with the
map family code (port/linux/game/map_families.c) and the stand-ins of
tools/test_ce_resource_maps.py."""
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from harness import function, read  # noqa: E402
from test_ce_resource_maps import (CUSTOM_EDITION, HARNESS, STUB_CSERIES, STUB_WINDOWS,  # noqa: E402
                                   rewrite)

# (the stand-ins' harness, but its own main: each map named is asked about)
STAND_INS = HARNESS[:HARNESS.index("int main(")]
MAIN = r"""
#include "halo_map_packs.h"
/* (the packs' folders: maps_<pack>, as a pack named for its folder has it) */
boolean map_pack_file_path(char const *pack, char const *file, char *path, long size)
{
	return snprintf(path, (size_t)size, "d:\\maps_%s\\%s.map", pack, file) < size;
}
char const *map_pack_menus(void) { return NULL; }
/* (an MCC level: mcc_cache.c's, here one installed level, aerowalk) */
boolean mcc_level_name(char const *map_name) { return !strncmp(map_name, "mcc_maps\\", 9); }
boolean mcc_cache_available(char const *map_name) { return strstr(map_name, "\\aerowalk") != NULL; }
/* (no map precached: the file is what counts) */
boolean cache_files_precache_map_loaded(char const *map_name) { (void)map_name; return FALSE; }
#include "installed.inc"
int main(int argc, char **argv)
{
	int index;
	snprintf(root, sizeof(root), "%s", argv[1]);
	for (index = 2; index < argc; index++)
		printf("%s %d\n", argv[index], cache_files_map_installed(argv[index]));
	return 0;
}
"""

# a cache file's header: 'head', its version, and at 0x60 its type
# (multiplayer); Xbox 5, Custom Edition 609, Halo PC 7 (HaloMD's too)
XBOX, CUSTOM_EDITION_VERSION, HALO_PC = 5, 609, 7


def cache_file(version):
    header = bytearray(2048)
    header[0:8] = b"daeh" + struct.pack("<I", version)
    header[0x60] = 1
    return bytes(header)


def mcc_file():
    # (an MCC map: Custom Edition's version 609 is not what its header says,
    # nor any other version the loaders read)
    return cache_file(13)


@pytest.fixture(scope="module")
def binaries(tmp_path_factory):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    installed = function(read("source/cache/cache_files.c"), "cache_files_map_installed")
    built = {}
    for name, defines in (("desktop", []), ("console", ['-DHALO_CONSOLE_NO_HALOMD_MAPS="the Xbox"'])):
        work = tmp_path_factory.mktemp(name)
        stubs = tmp_path_factory.mktemp(name + "-stubs")
        (stubs / "cseries").mkdir()
        (stubs / "cseries.h").write_text(STUB_CSERIES)
        (stubs / "cseries" / "cseries_windows.h").write_text(STUB_WINDOWS)
        (stubs / "harness.c").write_text(STAND_INS + MAIN)
        (stubs / "installed.inc").write_text(installed)
        for file in ("cseries.h", "cseries/cseries_windows.h", "harness.c", "installed.inc"):
            rewrite(stubs / file, work / file)
        for file in ("halo_map_families.h", "halo_join_reasons.h", "halo_map_packs.h"):
            rewrite(ROOT / "port/linux/include" / file, work / file)
        rewrite(ROOT / "port/linux/game/map_families.c", work / "game/map_families.c")
        rewrite(ROOT / "port/linux/game/map_stock_pairs.h", work / "game/map_stock_pairs.h")
        for file in ("map_archive.h", "map_file_hash.h"):
            rewrite(ROOT / "port/linux/src" / file, work / "src" / file)
        binary = work / "installed"
        subprocess.run([cc, "-std=gnu99", "-Wall", "-Werror", "-Wno-unused-variable", "-Wno-unused-function",
                        "-Wno-format", "-DHALO_CUSTOM_EDITION", "-DHALO_MAP_LIST", *defines, "-I", str(work),
                        str(work / "harness.c"), str(work / "game/map_families.c"), "-o", str(binary)], check=True)
        built[name] = binary

    def run(root, maps, build="desktop"):
        lines = subprocess.run([str(built[build]), str(root), *maps], check=True, capture_output=True,
                               text=True).stdout.splitlines()
        answers = dict(line.rsplit(" ", 1) for line in lines if not line.startswith("log: "))
        return {name: answer == "1" for name, answer in answers.items()}
    return run


def write(root, relative, contents):
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(contents)


def tree(root, resources=True):
    write(root, "maps/bloodgulch.map", cache_file(XBOX))
    write(root, "maps_ce/deathisland.map", cache_file(CUSTOM_EDITION_VERSION))
    write(root, "maps_ce/hugeass.map", mcc_file())
    write(root, "maps_md/hydrolysis.map", cache_file(HALO_PC))
    write(root, "maps_cex/wizard.map", cache_file(XBOX))
    if resources:
        for name, contents in CUSTOM_EDITION.items():
            write(root, f"maps_ce/{name}.map", contents)
    return root


MAPS = ["levels\\test\\bloodgulch\\bloodgulch", "levels\\test\\dangercanyon\\dangercanyon", "deathisland@ce",
        "putput@ce", "hydrolysis@md", "wizard@cex", "gothra@cex", "hugeass@ce",
        "mcc_maps\\2906110850\\aerowalk", "mcc_maps\\2906110850\\hearth"]


def test_with_resource_maps(binaries, tmp_path):
    answers = binaries(tree(tmp_path), MAPS)
    assert answers == {
        "levels\\test\\bloodgulch\\bloodgulch": True,
        "levels\\test\\dangercanyon\\dangercanyon": False,
        "deathisland@ce": True,
        "putput@ce": False,
        "hydrolysis@md": True,
        "wizard@cex": True,
        "gothra@cex": False,
        # (an MCC-format map among Custom Edition's: the loaders don't play one)
        "hugeass@ce": False,
        # (an MCC level: installed if its map is, mcc_cache_available)
        "mcc_maps\\2906110850\\aerowalk": True,
        "mcc_maps\\2906110850\\hearth": False,
    }


def test_without_resource_maps(binaries, tmp_path):
    # (a map past the Xbox's reads Custom Edition's resource maps as it loads:
    # without them it isn't installed, though its file is there)
    answers = binaries(tree(tmp_path, resources=False), MAPS)
    assert answers["levels\\test\\bloodgulch\\bloodgulch"]
    assert answers["wizard@cex"]
    assert not answers["deathisland@ce"]
    assert not answers["hydrolysis@md"]


def test_console_without_halomd(binaries, tmp_path):
    answers = binaries(tree(tmp_path), ["hydrolysis@md", "deathisland@ce"], build="console")
    assert answers == {"hydrolysis@md": False, "deathisland@ce": True}

