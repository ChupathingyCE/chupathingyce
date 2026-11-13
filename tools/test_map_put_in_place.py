"""A downloaded Custom Edition map put in place beside another version of
it (port/linux/src/map_download.c's put_in_place), and the Linux and macOS
MoveFileA (port/linux/src/xbox_files.c) it goes through.

Build 6 on Linux: joining a host whose version of floodgulch5.5 differed
from the player's failed every time with "maps_ce/floodgulch5.5.map could
not be put in place (is it there already?)". put_in_place took every failed
move for a file in the way, and a move onto another file system (a maps_ce
that is a link to another drive) failed twice over. Now: a move across file
systems copies, as Windows's MoveFileA does; a move that fails for another
reason says its error and touches nothing; a file of the hashed name there
already is kept when its hash says it is this map; and the file that landed
is hashed once more where its hash is known. The functions are compiled
from the game's source with stand-ins, against real files."""
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from harness import function, read  # noqa: E402

DOWNLOAD = read("port/linux/src/map_download.c")
FILES = read("port/linux/src/xbox_files.c")

PLACE_HARNESS = r"""
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
typedef unsigned long DWORD;
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_GEN_FAILURE 31L
#define MAP_DOWNLOAD_PATH_SIZE 512
static struct { unsigned int version; char installed[48]; } map_download;
static DWORD last_error;
static DWORD GetLastError(void) { return last_error; }
static void SetLastError(DWORD error) { last_error = error; }
static char root[512];
static int fail_moves;
static void translate(const char *path, char *out, size_t size)
{
	size_t index;
	if (!strncmp(path, "d:\\", 3)) { snprintf(out, size, "%s/%s", root, path + 3); }
	else snprintf(out, size, "%s", path);
	for (index = 0; out[index]; index++) if (out[index] == '\\') out[index] = '/';
}
static int MoveFileA(const char *from_path, const char *to_path)
{
	char from[1024], to[1024];
	struct stat information;
	translate(from_path, from, sizeof(from));
	translate(to_path, to, sizeof(to));
	if (stat(to, &information) == 0) { SetLastError(ERROR_ALREADY_EXISTS); return 0; }
	if (fail_moves) { SetLastError(ERROR_GEN_FAILURE); return 0; }
	if (rename(from, to) != 0) { SetLastError(errno == EEXIST ? ERROR_ALREADY_EXISTS : ERROR_GEN_FAILURE); return 0; }
	return 1;
}
static int DeleteFileA(const char *path) { char local[1024]; translate(path, local, sizeof(local)); return unlink(local) == 0; }
/* (a stand-in hash: the file's first four bytes, big-endian) */
static int map_file_hash_prefix(const char *path, unsigned int *prefix)
{
	char local[1024]; unsigned char head[4]; FILE *file;
	translate(path, local, sizeof(local));
	file = fopen(local, "rb");
	if (!file || fread(head, 1, 4, file) != 4) { if (file) fclose(file); return 0; }
	fclose(file);
	*prefix = ((unsigned int)head[0] << 24) | ((unsigned int)head[1] << 16) | ((unsigned int)head[2] << 8) | head[3];
	return 1;
}
static int map_archive_hashed_name(const char *name, char *base, int base_size, unsigned int *prefix)
{
	size_t length = strlen(name);
	if (length < 10 || name[length - 9] != '_') return 0;
	if (sscanf(name + length - 8, "%8x", prefix) != 1) return 0;
	snprintf(base, (size_t)base_size, "%.*s", (int)(length - 9), name);
	return 1;
}
static void platform_log(const char *format, ...) { (void)format; }
@FUNCTIONS@
int main(int argc, char **argv)
{
	char beside[48] = "", error[160] = "";
	const char *installed;
	if (argc != 7) return 2;
	snprintf(root, sizeof(root), "%s", argv[1]);
	map_download.version = (unsigned int)strtoul(argv[4], NULL, 16);
	fail_moves = atoi(argv[5]);
	installed = put_in_place(argv[2], argv[3], !strcmp(argv[2], argv[3]) ? 0 : 1, argv[6], beside, error, sizeof(error));
	printf("installed=%s\nerror=%s\n", installed ? installed : "", error);
	return 0;
}
"""

MOVE_HARNESS = r"""
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
typedef int BOOL;
typedef unsigned long DWORD;
typedef const char *LPCSTR;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_SUCCESS 0L
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
struct posix_file_information { int size; };
static int posix_stat(const char *path, struct posix_file_information *information)
{ struct stat s; (void)information; return stat(path, &s); }
static void platform_translate_path(const char *path, char *out, size_t size) { snprintf(out, size, "%s", path); }
static DWORD last_error;
static DWORD GetLastError(void) { return last_error; }
static void SetLastError(DWORD error) { last_error = error; }
static DWORD platform_set_last_error_from_errno(int error) { last_error = error == EEXIST ? ERROR_ALREADY_EXISTS : 1000 + (DWORD)error; return last_error; }
static int across_devices;
/* (2: another file system, and a file lands at the target just before the copy) */
static int test_rename(const char *from, const char *to)
{
	if (across_devices == 2) { FILE *f = fopen(to, "wb"); if (f) { fputs("theirs", f); fclose(f); } }
	if (across_devices) { errno = EXDEV; return -1; }
	return rename(from, to);
}
#define rename test_rename
static BOOL WINAPI CopyFileA(LPCSTR existing_file_name, LPCSTR new_file_name, BOOL fail_if_exists);
@FUNCTIONS@
int main(int argc, char **argv)
{
	if (argc != 4) return 2;
	across_devices = atoi(argv[1]);
	printf("moved=%d error=%lu\n", MoveFileA(argv[2], argv[3]) ? 1 : 0, GetLastError());
	return 0;
}
"""


def compiler():
    return shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")


def build(tmp_path, name, harness, source, functions):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    text = "\n".join(function(source, function_name) for function_name in functions)
    text = re.sub(r"^BOOL WINAPI ", "static BOOL WINAPI ", text, flags=re.M)
    code = tmp_path / f"{name}.c"
    code.write_text(harness.replace("@FUNCTIONS@", text))
    binary = tmp_path / name
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Wno-unused-function", str(code), "-o", str(binary)], check=True)
    return binary


@pytest.fixture
def placer(tmp_path_factory):
    return build(tmp_path_factory.mktemp("place"), "place", PLACE_HARNESS, DOWNLOAD,
                 ("final_path", "prefix_of", "placed", "put_in_place"))


@pytest.fixture
def mover(tmp_path_factory):
    return build(tmp_path_factory.mktemp("move"), "move", MOVE_HARNESS, FILES, ("CopyFileA", "MoveFileA"))


def map_bytes(prefix, filler=b"x"):
    """a stand-in map whose 'hash' (its first four bytes) is prefix"""
    return struct.pack(">I", prefix) + filler * 64


def place(placer, root, name, asked, version, fail_moves=0):
    root.joinpath("maps_ce").mkdir(exist_ok=True)
    result = subprocess.run([str(placer), str(root), name, asked, f"{version:08x}", str(fail_moves),
                             f"d:\\maps_quarantine\\{asked}.map.download"], capture_output=True, text=True, check=True)
    lines = dict(line.split("=", 1) for line in result.stdout.splitlines())
    return lines["installed"], lines["error"]


def downloaded(root, asked, prefix):
    root.joinpath("maps_quarantine").mkdir(exist_ok=True)
    partial = root / "maps_quarantine" / f"{asked}.map.download"
    partial.write_bytes(map_bytes(prefix))
    return partial


def test_placed_by_name_when_nothing_is_there(placer, tmp_path):
    partial = downloaded(tmp_path, "floodgulch5.5", 0x9FC1B040)
    installed, error = place(placer, tmp_path, "floodgulch5.5", "floodgulch5.5", 0x9FC1B040)
    assert (installed, error) == ("floodgulch5.5", "")
    assert not partial.exists()
    assert (tmp_path / "maps_ce" / "floodgulch5.5.map").read_bytes() == map_bytes(0x9FC1B040)


def test_placed_beside_another_version(placer, tmp_path):
    """ugoboom's case: the player's floodgulch5.5.map is another version"""
    mine = tmp_path / "maps_ce"
    mine.mkdir()
    (mine / "floodgulch5.5.map").write_bytes(map_bytes(0x11111111, b"m"))
    partial = downloaded(tmp_path, "floodgulch5.5", 0x9FC1B040)
    installed, error = place(placer, tmp_path, "floodgulch5.5", "floodgulch5.5", 0x9FC1B040)
    assert (installed, error) == ("floodgulch5.5_9fc1b040", "")
    assert not partial.exists()
    # (the player's file untouched, the host's version beside it)
    assert (mine / "floodgulch5.5.map").read_bytes() == map_bytes(0x11111111, b"m")
    assert (mine / "floodgulch5.5_9fc1b040.map").read_bytes() == map_bytes(0x9FC1B040)


def test_the_hashed_file_there_already_is_kept(placer, tmp_path):
    """a second download of the same version: the file from the first is this map"""
    mine = tmp_path / "maps_ce"
    mine.mkdir()
    (mine / "floodgulch5.5.map").write_bytes(map_bytes(0x11111111, b"m"))
    (mine / "floodgulch5.5_9fc1b040.map").write_bytes(map_bytes(0x9FC1B040, b"o"))
    partial = downloaded(tmp_path, "floodgulch5.5", 0x9FC1B040)
    installed, error = place(placer, tmp_path, "floodgulch5.5", "floodgulch5.5", 0x9FC1B040)
    assert (installed, error) == ("floodgulch5.5_9fc1b040", "")
    assert not partial.exists()
    assert (mine / "floodgulch5.5_9fc1b040.map").read_bytes() == map_bytes(0x9FC1B040, b"o")


def test_another_file_of_the_hashed_name_is_in_the_way(placer, tmp_path):
    mine = tmp_path / "maps_ce"
    mine.mkdir()
    (mine / "floodgulch5.5.map").write_bytes(map_bytes(0x11111111, b"m"))
    (mine / "floodgulch5.5_9fc1b040.map").write_bytes(map_bytes(0x22222222, b"o"))
    downloaded(tmp_path, "floodgulch5.5", 0x9FC1B040)
    installed, error = place(placer, tmp_path, "floodgulch5.5", "floodgulch5.5", 0x9FC1B040)
    assert installed == "" and "in the way" in error
    assert (mine / "floodgulch5.5_9fc1b040.map").read_bytes() == map_bytes(0x22222222, b"o")


def test_a_move_that_fails_otherwise_says_its_error(placer, tmp_path):
    """not 'is it there already?': the error, and nothing of the player's touched"""
    mine = tmp_path / "maps_ce"
    mine.mkdir()
    partial = downloaded(tmp_path, "area54", 0x7E86C0D9)
    installed, error = place(placer, tmp_path, "area54", "area54", 0x7E86C0D9, fail_moves=1)
    assert installed == "" and "error 31" in error and "there already" not in error
    assert partial.exists() and not (mine / "area54.map").exists() and not (mine / "area54_7e86c0d9.map").exists()


def test_a_hashed_name_from_the_host(placer, tmp_path):
    """a host that names the map Sneak_af352483: that file, or beside sneak.map"""
    mine = tmp_path / "maps_ce"
    mine.mkdir()
    downloaded(tmp_path, "sneak_af352483", 0xAF352483)
    installed, error = place(placer, tmp_path, "sneak", "sneak_af352483", 0)
    assert (installed, error) == ("sneak_af352483", "")
    assert (mine / "sneak_af352483.map").exists()


def test_a_hashed_name_there_already_is_kept(placer, tmp_path):
    """the host's Sneak_af352483.map from an earlier download: kept, this copy let go"""
    mine = tmp_path / "maps_ce"
    mine.mkdir()
    (mine / "sneak_af352483.map").write_bytes(map_bytes(0xAF352483, b"o"))
    partial = downloaded(tmp_path, "sneak_af352483", 0xAF352483)
    installed, error = place(placer, tmp_path, "sneak", "sneak_af352483", 0)
    assert (installed, error) == ("sneak_af352483", "")
    assert not partial.exists() and not (mine / "sneak.map").exists()
    assert (mine / "sneak_af352483.map").read_bytes() == map_bytes(0xAF352483, b"o")


def test_the_landed_file_is_checked(placer, tmp_path):
    """a file that isn't what was downloaded once it landed: removed, refused"""
    mine = tmp_path / "maps_ce"
    mine.mkdir()
    downloaded(tmp_path, "sneak_af352483", 0x00000001)
    installed, error = place(placer, tmp_path, "sneak", "sneak_af352483", 0)
    assert installed == "" and "not the same after" in error
    assert not (mine / "sneak_af352483.map").exists()


def move(mover, across, source, target):
    result = subprocess.run([str(mover), str(across), str(source), str(target)], capture_output=True, text=True,
                            check=True)
    match = re.match(r"moved=(\d) error=(\d+)", result.stdout)
    return int(match.group(1)), int(match.group(2))


def test_move_on_one_file_system(mover, tmp_path):
    source = tmp_path / "a.map.download"
    source.write_bytes(b"map")
    assert move(mover, 0, source, tmp_path / "a.map") == (1, 0)
    assert not source.exists() and (tmp_path / "a.map").read_bytes() == b"map"


def test_move_across_file_systems_copies(mover, tmp_path):
    """rename refused with EXDEV (a maps_ce on another drive): copied, the original removed"""
    source = tmp_path / "a.map.download"
    source.write_bytes(b"m" * 100000)
    assert move(mover, 1, source, tmp_path / "a.map") == (1, 0)
    assert not source.exists() and (tmp_path / "a.map").read_bytes() == b"m" * 100000


def test_move_across_file_systems_keeps_a_file_that_appeared(mover, tmp_path):
    """a file that lands at the target between the check and the copy: the
    copy fails, and that file is left as it is"""
    source = tmp_path / "a.map.download"
    source.write_bytes(b"new")
    assert move(mover, 2, source, tmp_path / "a.map") == (0, 183)
    assert source.read_bytes() == b"new" and (tmp_path / "a.map").read_bytes() == b"theirs"


def test_move_never_replaces_a_file(mover, tmp_path):
    source = tmp_path / "a.map.download"
    source.write_bytes(b"new")
    (tmp_path / "a.map").write_bytes(b"old")
    for across in (0, 1):
        assert move(mover, across, source, tmp_path / "a.map") == (0, 183)
        assert source.read_bytes() == b"new" and (tmp_path / "a.map").read_bytes() == b"old"
