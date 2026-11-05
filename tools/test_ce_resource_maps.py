"""Checks Custom Edition's resource maps as every map past the Xbox's needs
them (map_family_resources_ready, port/linux/game/map_families.c): none, one
missing, Halo PC retail's own in their place (named for their pixels' and
permutations' numbers, not Custom Edition's <path>__pixels and
<path>__permutations), a file that is no resource map, and Custom Edition's
in maps_ce or in the older maps/ce. Each answer that is no comes with what the
player is told, and every place looked at is logged. The resource maps are
written here, as small as their headers and paths allow; the family code is
compiled alone with the build machine's C compiler, through the 64-bit
builds' rewrite of long (tools/lp64_rewrite.py), with stand-ins for the
game's headers and its file calls."""
import shutil
import struct
import subprocess
import sys
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
STUB_WINDOWS = r"""
#include <stdio.h>
#include <stdlib.h>
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
typedef void *HANDLE;
typedef struct { char cFileName[260]; } WIN32_FIND_DATAA;
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define INVALID_FILE_SIZE 0xffffffffUL
#define INVALID_SET_FILE_POINTER 0xffffffffUL
#define GENERIC_READ 1
#define FILE_SHARE_READ 1
#define GENERIC_WRITE 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define FILE_ATTRIBUTE_NORMAL 0
#define FILE_BEGIN 0
#define FILE_END 2
HANDLE CreateFileA(char const *path, unsigned long access, unsigned long share, void *security, unsigned long how,
	unsigned long flags, void *template_file);
int ReadFile(HANDLE file, void *buffer, unsigned long size, unsigned long *count, void *overlapped);
int WriteFile(HANDLE file, void const *buffer, unsigned long size, unsigned long *count, void *overlapped);
int CloseHandle(HANDLE file);
unsigned long SetFilePointer(HANDLE file, long offset, long *high, unsigned long how);
unsigned long GetFileSize(HANDLE file, unsigned long *high);
#define FindFirstFileA(pattern, data) INVALID_HANDLE_VALUE
#define FindNextFileA(find, data) 0
#define FindClose(find) 0
char const *config_string(char const *name);
"""
HARNESS = r"""#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "halo_map_families.h"
static char root[512];
static void host(char const *path, char *out, size_t size)
{
	char const *rest = (path[0] && path[1] == ':' && path[2] == '\\') ? path + 3 : path;
	size_t at;
	snprintf(out, size, "%s/%s", root, rest);
	for (at = strlen(root); out[at]; at++)
		if (out[at] == '\\')
			out[at] = '/';
}
HANDLE CreateFileA(char const *path, unsigned long access, unsigned long share, void *security, unsigned long how,
	unsigned long flags, void *template_file)
{
	char real[1024];
	FILE *file;
	host(path, real, sizeof(real));
	file = fopen(real, "rb");
	return file ? (HANDLE)file : INVALID_HANDLE_VALUE;
}
int ReadFile(HANDLE file, void *buffer, unsigned long size, unsigned long *count, void *overlapped)
{
	*count = (unsigned long)fread(buffer, 1, size, (FILE *)file);
	return 1;
}
int WriteFile(HANDLE file, void const *buffer, unsigned long size, unsigned long *count, void *overlapped) { *count = 0; return 0; }
int CloseHandle(HANDLE file) { return fclose((FILE *)file) == 0; }
unsigned long SetFilePointer(HANDLE file, long offset, long *high, unsigned long how)
{
	return fseek((FILE *)file, offset, how == 2 ? SEEK_END : SEEK_SET) ? INVALID_SET_FILE_POINTER : (unsigned long)offset;
}
unsigned long GetFileSize(HANDLE file, unsigned long *high)
{
	long at = ftell((FILE *)file), size;
	fseek((FILE *)file, 0, SEEK_END);
	size = ftell((FILE *)file);
	fseek((FILE *)file, at, SEEK_SET);
	if (high)
		*high = 0;
	return (unsigned long)size;
}
char const *config_string(char const *name) { (void)name; return ""; }
char const *cache_files_map_directory(void) { return "d:\\maps\\"; }
#include <stdarg.h>
void platform_log(char const *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	fputs("log: ", stdout);
	vprintf(format, arguments);
	fputc('\n', stdout);
	va_end(arguments);
}
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size)
{
	host(xbox_path, host_path, host_path_size);
}
unsigned int map_archive_header_checksum(const unsigned char *header) { (void)header; return 0; }
int map_archive_stock_name(const char *name) { (void)name; return 0; }
int map_archive_hashed_name(const char *name, char *base, int base_size, unsigned int *prefix) { return 0; }
void map_archive_display_name(const char *name, char *display, int size) { snprintf(display, (size_t)size, "%s", name); }
int map_archive_version_matches(unsigned int version, unsigned int checksum, unsigned int hash_version) { return 1; }
int map_file_hash_prefix(const char *path, unsigned int *prefix) { return 0; }
int map_file_hash_version(const char *path, unsigned int *version) { return 0; }
int main(int argc, char **argv)
{
	char message[512];
	int ready;
	snprintf(root, sizeof(root), "%s", argv[1]);
	ready = map_family_resources_ready(message, sizeof(message));
	printf("%d %s\n", ready, ready ? "" : message);
	/* (asked again, unchanged: the same answer) */
	printf("%d\n", map_family_resources_ready(NULL, 0));
	return 0;
}
"""

MESSAGE = ("This map needs Halo Custom Edition's bitmaps.map, sounds.map and loc.map in maps_ce. "
           "Use the setup screen to import the Custom Edition installer.")


def rewrite(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(ROOT / "tools/lp64_rewrite.py"), "--output", str(destination), str(source)],
                   check=True)


@pytest.fixture(scope="module")
def ready(tmp_path_factory):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    work = tmp_path_factory.mktemp("resources")
    stubs = tmp_path_factory.mktemp("stubs")
    (stubs / "cseries").mkdir()
    (stubs / "cseries.h").write_text(STUB_CSERIES)
    (stubs / "cseries" / "cseries_windows.h").write_text(STUB_WINDOWS)
    (stubs / "harness.c").write_text(HARNESS)
    for name in ("cseries.h", "cseries/cseries_windows.h", "harness.c"):
        rewrite(stubs / name, work / name)
    for name in ("halo_map_families.h", "halo_join_reasons.h"):
        rewrite(ROOT / "port/linux/include" / name, work / name)
    # (map_families.c's own includes are relative to it: ../src)
    rewrite(ROOT / "port/linux/game/map_families.c", work / "game/map_families.c")
    rewrite(ROOT / "port/linux/game/map_stock_pairs.h", work / "game/map_stock_pairs.h")
    for name in ("map_archive.h", "map_file_hash.h"):
        rewrite(ROOT / "port/linux/src" / name, work / "src" / name)
    binary = work / "ready"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Werror", "-Wno-unused-variable", "-Wno-format",
                    "-DHALO_CUSTOM_EDITION", "-I", str(work), str(work / "harness.c"),
                    str(work / "game/map_families.c"), "-o", str(binary)], check=True)

    def run(root):
        lines = subprocess.run([str(binary), str(root)], check=True, capture_output=True,
                               text=True).stdout.splitlines()
        logs = [line[5:] for line in lines if line.startswith("log: ")]
        answers = [line for line in lines if not line.startswith("log: ")]
        return answers, logs
    return run


def resource_map(kind, names):
    """A resource map: its type (1 bitmaps, 2 sounds, 3 strings and fonts),
    its paths' offset, its table's offset and its count; the paths; a table
    of (path offset, size, offset) with each resource's bytes after it."""
    paths = b"".join(name.encode() + b"\0" for name in names)
    offsets, at = [], 0
    for name in names:
        offsets.append(at)
        at += len(name) + 1
    table_offset = 16 + len(paths)
    data_offset = table_offset + 12 * len(names)
    table = b"".join(struct.pack("<3I", offset, 4, data_offset + 4 * index) for index, offset in enumerate(offsets))
    return struct.pack("<4I", kind, 16, table_offset, len(names)) + paths + table + b"\0" * 4 * len(names)


CUSTOM_EDITION = {
    "bitmaps": resource_map(1, ["sky\\space\\bitmaps\\stars__pixels", "sky\\space\\bitmaps\\stars"]),
    "sounds": resource_map(2, ["sound\\sfx\\impulse\\coolant\\enter_water__permutations",
                               "sound\\sfx\\impulse\\coolant\\enter_water"]),
    "loc": resource_map(3, ["ui\\shell\\strings\\default_map_names"]),
}
HALO_PC = {
    "bitmaps": resource_map(1, ["sky\\space\\bitmaps\\stars_0", "sky\\space\\bitmaps\\star mask_0"]),
    "sounds": resource_map(2, ["sound\\sfx\\impulse\\coolant\\enter_water__0__0"]),
}


def data_root(tmp_path, folder, files):
    (tmp_path / folder).mkdir(parents=True, exist_ok=True)
    for name, contents in files.items():
        (tmp_path / folder / f"{name}.map").write_bytes(contents)
    return tmp_path


def test_none(ready, tmp_path):
    answers, logs = ready(tmp_path)
    assert answers == [f"0 {MESSAGE}", "0"]
    for name in ("bitmaps", "sounds", "loc"):
        line = next(line for line in logs if line.startswith(f"Custom Edition resource maps: {name}.map is missing"))
        for place in ("maps_ce", "maps/ce", "custom_maps"):
            assert f"{tmp_path}/{place}/{name}.map" in line


def test_custom_editions(ready, tmp_path):
    answers, logs = ready(data_root(tmp_path, "maps_ce", CUSTOM_EDITION))
    assert answers == ["1 ", "1"]
    assert f"Custom Edition resource maps: bitmaps.map is Custom Edition's: {tmp_path}/maps_ce/bitmaps.map" in logs


def test_custom_editions_in_the_older_place(ready, tmp_path):
    answers, _ = ready(data_root(tmp_path, "maps/ce", CUSTOM_EDITION))
    assert answers == ["1 ", "1"]


def test_one_missing(ready, tmp_path):
    files = dict(CUSTOM_EDITION)
    del files["loc"]
    answers, logs = ready(data_root(tmp_path, "maps_ce", files))
    assert answers == [f"0 {MESSAGE}", "0"]
    assert any(line.startswith("Custom Edition resource maps: loc.map is missing") for line in logs)


def test_halo_pc_retails(ready, tmp_path):
    files = dict(CUSTOM_EDITION, **HALO_PC)
    answers, logs = ready(data_root(tmp_path, "maps_ce", files))
    assert answers == [f"0 {MESSAGE}", "0"]
    assert ("Custom Edition resource maps: bitmaps.map is Halo PC's own, not Custom Edition's: "
            f"{tmp_path}/maps_ce/bitmaps.map") in logs
    assert any(line.startswith("Custom Edition resource maps: sounds.map is Halo PC's own") for line in logs)
    assert any(line.startswith("Custom Edition resource maps: loc.map is Custom Edition's") for line in logs)


def test_not_a_resource_map(ready, tmp_path):
    # (a cache file, an Xbox map named bitmaps.map, and a sounds.map of the
    # wrong type)
    files = dict(CUSTOM_EDITION, bitmaps=b"daeh" + struct.pack("<I", 5) + b"\0" * 2040,
                 sounds=CUSTOM_EDITION["loc"])
    answers, logs = ready(data_root(tmp_path, "maps_ce", files))
    assert answers == [f"0 {MESSAGE}", "0"]
    assert any(line.startswith("Custom Edition resource maps: bitmaps.map is not a resource map of its kind")
               for line in logs)
    assert any(line.startswith("Custom Edition resource maps: sounds.map is not a resource map of its kind")
               for line in logs)


def test_cut_short(ready, tmp_path):
    files = dict(CUSTOM_EDITION, bitmaps=CUSTOM_EDITION["bitmaps"][:40])
    answers, _ = ready(data_root(tmp_path, "maps_ce", files))
    assert answers == [f"0 {MESSAGE}", "0"]
