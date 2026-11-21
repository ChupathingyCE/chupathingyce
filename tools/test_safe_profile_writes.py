"""Profiles written whole or not at all (#1585).

The Xbox wrote blam.sav in place: a crash, a power cut or a pulled drive in
the write left it cut short or half old, half new, its checksum failed and
the profile came back as the defaults. The desktop builds now write it
through halo_file_write_whole (port/linux/src/xbox_files.c): to blam.sav.tmp,
flushed (posix_sync) and read back, then renamed over the old file in one
step (posix_replace_file). The good file it replaces is kept as
blam.sav.bak, and a blam.sav whose checksum fails is taken back from it
(source/saved games/saved_game_files.c).

The functions are compiled from the game's source with stand-ins, against
real files. A debug build stops itself at a step when HALO_TEST_SAVE_CRASH
names it, as a crash would there: each test stops a write at each step and
checks what is left on the disk."""
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from harness import function, read  # noqa: E402

FILES = read("port/linux/src/xbox_files.c")
POSIX = read("port/linux/src/posix_files.c")
SAVED = read("source/saved games/saved_game_files.c")

BLOCK_SIZE = 512
CHECKSUM_DATA_SIZE = 48
CRASHED = 86

HARNESS = r"""
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
typedef int BOOL;
typedef int boolean;
typedef unsigned char byte;
typedef unsigned short word;
#define TRUE 1
#define FALSE 0
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#define csmemcmp memcmp
#define _snprintf snprintf
#define MAXIMUM_FILENAME_LENGTH 255
#define SAVED_GAME_FILE_BLOCK_SIZE 512
#define FLAG(bit) (1u << (bit))
enum { _permission_read_bit };
enum { _saved_game_file_type_player_profile, _saved_game_file_type_game_variant };
enum { PLAYER_PROFILE_CHECKSUM_DATA_SIZE = 48, PLAYLIST_PROFILE_CHECKSUM_DATA_SIZE = 104 };
#define _error_silent 0
static void error(int level, const char *format, ...) { (void)level; printf("log: %s\n", format); }
static unsigned long GetLastError(void) { return 0; }
static void platform_translate_path(const char *path, char *out, unsigned long size) { snprintf(out, size, "%s", path); }
static void platform_set_last_error_from_errno(int error) { (void)error; }
/* (a stand-in checksum: a byte sum, stored as four bytes) */
typedef struct { unsigned char data[4]; } XCALCSIG_SIGNATURE;
static void saved_game_file_generate_checksum(void const *buffer, word size, XCALCSIG_SIGNATURE *checksum)
{
	unsigned long sum = 7; word index;
	for (index = 0; index < size; index++) sum = sum * 31 + ((const unsigned char *)buffer)[index];
	memcpy(checksum->data, &sum, 4);
}
/* (the game's file layer, over stdio) */
struct file_reference { char path[512]; FILE *file; };
static struct file_reference *file_reference_create_from_path(struct file_reference *file, const char *path, boolean directory)
{ (void)directory; snprintf(file->path, sizeof(file->path), "%s", path); file->file = NULL; return file; }
static boolean file_exists(const struct file_reference *file) { struct stat s; return stat(file->path, &s) == 0; }
static boolean file_open(struct file_reference *file, unsigned long flags) { (void)flags; file->file = fopen(file->path, "rb"); return file->file != NULL; }
static boolean file_close(struct file_reference *file) { fclose(file->file); return TRUE; }
static unsigned long file_get_eof(const struct file_reference *file)
{ long at = ftell(file->file), end; fseek(file->file, 0, SEEK_END); end = ftell(file->file); fseek(file->file, at, SEEK_SET); return (unsigned long)end; }
static boolean file_read(const struct file_reference *file, unsigned long count, void *buffer)
{ return fread(buffer, 1, count, file->file) == count; }
int posix_sync(int descriptor);
int posix_replace_file(const char *from, const char *to);
static char const saved_game_file_backup_suffix[] = ".bak";
@FUNCTIONS@
static int load(const char *path, byte *block)
{
	FILE *file = fopen(path, "rb");
	size_t count;
	if (!file) return 0;
	count = fread(block, 1, SAVED_GAME_FILE_BLOCK_SIZE, file);
	fclose(file);
	return count == SAVED_GAME_FILE_BLOCK_SIZE;
}
/* the profile write's own steps (saved_game_file_write_block's, with the
path given): the good file there becomes the backup, then the new block */
static int write_profile(const char *path, const byte *block)
{
	byte previous[SAVED_GAME_FILE_BLOCK_SIZE];
	char backup[MAXIMUM_FILENAME_LENGTH+1];
	if (saved_game_file_backup_path(path, backup) &&
		saved_game_file_read_valid_block(path, PLAYER_PROFILE_CHECKSUM_DATA_SIZE, previous) &&
		memcmp(previous, block, SAVED_GAME_FILE_BLOCK_SIZE))
		halo_file_write_whole(backup, previous, SAVED_GAME_FILE_BLOCK_SIZE);
	return halo_file_write_whole(path, block, SAVED_GAME_FILE_BLOCK_SIZE);
}
int main(int argc, char **argv)
{
	byte block[SAVED_GAME_FILE_BLOCK_SIZE];
	if (argc == 4 && !strcmp(argv[1], "write"))
	{
		/* write <path> <block file> */
		if (!load(argv[3], block)) return 2;
		printf("written=%d\n", write_profile(argv[2], block));
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "check"))
	{
		printf("valid=%d\n", saved_game_file_read_valid_block(argv[2], PLAYER_PROFILE_CHECKSUM_DATA_SIZE, block));
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "restore"))
	{
		printf("restored=%d\n", saved_game_file_restore_backup(argv[2], PLAYER_PROFILE_CHECKSUM_DATA_SIZE));
		return 0;
	}
	return 2;
}
"""

STEPS = ("written", "verified", "replaced")


def compiler():
    return shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")


@pytest.fixture(scope="module")
def tool(tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    text = "\n".join([
        function(FILES, "write_whole_step"),
        function(FILES, "write_whole_all"),
        function(FILES, "write_whole_matches"),
        function(FILES, "halo_file_write_whole"),
        function(POSIX, "posix_sync"),
        function(POSIX, "posix_replace_file"),
        function(SAVED, "saved_game_file_read_valid_block"),
        function(SAVED, "saved_game_file_backup_path"),
        function(SAVED, "saved_game_file_restore_backup"),
    ])
    folder = tmp_path_factory.mktemp("safe-saves")
    code = folder / "safe_saves.c"
    code.write_text(HARNESS.replace("@FUNCTIONS@", text))
    binary = folder / "safe_saves"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Wno-unused-function", str(code), "-o", str(binary)], check=True)
    return binary


def checksum(data):
    total = 7
    for value in data[:CHECKSUM_DATA_SIZE]:
        total = (total * 31 + value) & 0xFFFFFFFFFFFFFFFF
    return (total & 0xFFFFFFFF).to_bytes(4, "little")


def profile(name):
    """a profile's block: its data, its checksum, padding"""
    data = name.encode().ljust(CHECKSUM_DATA_SIZE, b"\0")
    return (data + checksum(data)).ljust(BLOCK_SIZE, b"\0")


def run(tool, *arguments, crash=None, folder=None):
    environment = {"HALO_TEST_SAVE_CRASH": crash} if crash else {}
    return subprocess.run([str(tool), *map(str, arguments)], capture_output=True, text=True, env=environment,
                          cwd=folder)


def write(tool, path, block, crash=None):
    source = path.parent / "block.in"
    source.write_bytes(block)
    result = run(tool, "write", path, source, crash=crash)
    source.unlink()
    return result


def test_write_replaces_the_file_and_leaves_no_temporary(tool, tmp_path):
    path = tmp_path / "blam.sav"
    path.write_bytes(profile("old"))
    result = write(tool, path, profile("new"))
    assert "written=1" in result.stdout
    assert path.read_bytes() == profile("new")
    assert not (tmp_path / "blam.sav.tmp").exists()


def test_the_good_file_it_replaces_is_kept_as_the_backup(tool, tmp_path):
    path = tmp_path / "blam.sav"
    path.write_bytes(profile("first"))
    write(tool, path, profile("second"))
    assert (tmp_path / "blam.sav.bak").read_bytes() == profile("first")
    write(tool, path, profile("third"))
    assert (tmp_path / "blam.sav.bak").read_bytes() == profile("second")
    assert path.read_bytes() == profile("third")


def test_a_damaged_file_never_becomes_the_backup(tool, tmp_path):
    path = tmp_path / "blam.sav"
    path.write_bytes(profile("first"))
    write(tool, path, profile("second"))
    path.write_bytes(profile("second")[:100])
    write(tool, path, profile("third"))
    assert (tmp_path / "blam.sav.bak").read_bytes() == profile("first")


@pytest.mark.parametrize("step", ["written", "verified"])
def test_a_crash_before_the_rename_leaves_the_old_profile_whole(tool, tmp_path, step):
    path = tmp_path / "blam.sav"
    path.write_bytes(profile("first"))
    write(tool, path, profile("second"))
    result = write(tool, path, profile("third"), crash=step)
    assert result.returncode == CRASHED
    assert path.read_bytes() == profile("second")
    assert "valid=1" in run(tool, "check", path).stdout
    # (the next write takes over the .tmp the crash left)
    assert "written=1" in write(tool, path, profile("fourth")).stdout
    assert path.read_bytes() == profile("fourth")
    assert not (tmp_path / "blam.sav.tmp").exists()


@pytest.mark.parametrize("step", ["written", "verified"])
def test_a_crash_between_the_profiles_temporary_and_its_rename(tool, tmp_path, step):
    """the backup written, blam.sav's own write stopped before its rename"""
    path = tmp_path / "blam.sav"
    path.write_bytes(profile("first"))
    write(tool, path, profile("second"))
    result = write(tool, path, profile("third"), crash=f"{step}:blam.sav")
    assert result.returncode == CRASHED
    assert path.read_bytes() == profile("second")
    assert (tmp_path / "blam.sav.tmp").read_bytes() == profile("third")
    assert (tmp_path / "blam.sav.bak").read_bytes() == profile("second")


def test_a_crash_after_the_rename_leaves_the_new_profile_whole(tool, tmp_path):
    """a new profile (nothing to back up): the rename was its last step"""
    path = tmp_path / "blam.sav"
    result = write(tool, path, profile("new"), crash="replaced")
    assert result.returncode == CRASHED
    assert path.read_bytes() == profile("new")
    assert not (tmp_path / "blam.sav.tmp").exists()


def test_a_crash_after_the_backup_leaves_the_old_profile_and_its_backup(tool, tmp_path):
    """the backup is written whole first; a stop after it leaves blam.sav as it was"""
    path = tmp_path / "blam.sav"
    path.write_bytes(profile("first"))
    result = write(tool, path, profile("second"), crash="replaced")
    assert result.returncode == CRASHED
    assert path.read_bytes() == profile("first")
    assert (tmp_path / "blam.sav.bak").read_bytes() == profile("first")


def test_a_crash_while_the_backup_is_written_leaves_both_whole(tool, tmp_path):
    """the backup's own write stops first: blam.sav is untouched"""
    path = tmp_path / "blam.sav"
    path.write_bytes(profile("first"))
    result = write(tool, path, profile("second"), crash="written")
    assert result.returncode == CRASHED
    assert path.read_bytes() == profile("first")
    assert not (tmp_path / "blam.sav.bak").exists()


def test_a_killed_writer_leaves_the_old_profile_whole(tool, tmp_path):
    """SIGKILL, not _exit: the process stopped by the system at the step"""
    path = tmp_path / "blam.sav"
    path.write_bytes(profile("first"))
    write(tool, path, profile("second"))
    source = tmp_path / "block.in"
    source.write_bytes(profile("third"))
    # (a stand-in for the step's crash that raises SIGKILL on itself)
    code = (tool.parent / "safe_saves.c").read_text()
    killed = tool.parent / "safe_saves_killed.c"
    killed.write_text(code.replace("_exit(86);", "kill(getpid(), SIGKILL);").replace(
        "#include <unistd.h>", "#include <unistd.h>\n#include <signal.h>", 1))
    binary = tool.parent / "safe_saves_killed"
    subprocess.run([compiler(), "-std=gnu99", "-w", str(killed), "-o", str(binary)], check=True)
    for step in ("written", "verified"):
        result = run(binary, "write", path, source, crash=step)
        assert result.returncode != 0
        assert path.read_bytes() == profile("second"), step


@pytest.mark.parametrize("damage", [b"", profile("second")[:200], b"\xff" * BLOCK_SIZE,
                                    profile("second") + b"extra"],
                         ids=["empty", "cut-short", "garbage", "too-long"])
def test_a_damaged_profile_is_restored_from_its_backup(tool, tmp_path, damage):
    path = tmp_path / "blam.sav"
    path.write_bytes(profile("first"))
    write(tool, path, profile("second"))
    path.write_bytes(damage)
    assert "valid=0" in run(tool, "check", path).stdout
    assert "restored=1" in run(tool, "restore", path).stdout
    assert path.read_bytes() == profile("first")
    assert "valid=1" in run(tool, "check", path).stdout


def test_no_good_backup_leaves_it_damaged(tool, tmp_path):
    path = tmp_path / "blam.sav"
    path.write_bytes(profile("first")[:10])
    assert "restored=0" in run(tool, "restore", path).stdout
    (tmp_path / "blam.sav.bak").write_bytes(b"\0" * BLOCK_SIZE)
    assert "restored=0" in run(tool, "restore", path).stdout
    assert path.read_bytes() == profile("first")[:10]


def test_a_write_that_cannot_happen_changes_nothing(tool, tmp_path):
    path = tmp_path / "missing" / "blam.sav"
    source = tmp_path / "block.in"
    source.write_bytes(profile("x"))
    assert "written=0" in run(tool, "write", path, source).stdout
    assert not path.exists()
    assert not (tmp_path / "missing").exists()


def test_release_builds_have_no_crash_switch():
    step = function(FILES, "write_whole_step")
    assert re.search(r"#ifndef HALO_RELEASE\s+const char \*asked = getenv\(\"HALO_TEST_SAVE_CRASH\"\)", step)
    assert re.search(r"#else\s+\(void\)step;\s+\(void\)path;\s+#endif", step)


def test_consoles_keep_the_xbox_write():
    features = read("port/linux/include/halo_port_features.h")
    define = features.index("#define HALO_PORT_SAFE_SAVES")
    gate = features[features.rindex("#if", 0, define):define]
    for name in ("HALO_CONSOLE", "HALO_SWITCH_CONSOLE", "HALO_WIIU_CONSOLE", "HALO_XBOX_CONSOLE",
                 "HALO_XBOX360_CONSOLE", "HALO_CONSOLE_GL_LAYER"):
        assert f"defined({name})" in gate
    profile_source = read("source/saved games/player_profile.c")
    assert profile_source.count("#ifdef HALO_PORT_SAFE_SAVES") == 2
