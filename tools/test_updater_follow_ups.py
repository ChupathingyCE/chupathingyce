"""Two follow-ups of the Game Mode update work (#1017).

An update that was downloaded and checked but not put in place (the player
quit while it waited for the game to end) is reused by the next start when
its file is still the release list's: port/linux/src/updater.c's
updater_reusable, compiled here with stand-ins against real files; the
listed zip is kept after its unpack and deleted by the swap.

The setup screen's file picker (port/linux/src/launcher.c): a picker given
up on with B can still answer later, on another thread; each picker has a
slot of its own now, so that answer never passes for the next picker's.
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from harness import function, read  # noqa: E402

UPDATER = read("port/linux/src/updater.c")
LAUNCHER = read("port/linux/src/launcher.c")

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define UPDATE_SHA256_TEXT_SIZE 65
#define MAXIMUM_UPDATE_SIZE (1u << 20)
static struct { int listed; size_t size; char sha256[UPDATE_SHA256_TEXT_SIZE]; } updater_file;
static unsigned char *SDL_LoadFile(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb"); unsigned char *data; long length;
	if (!file) return NULL;
	fseek(file, 0, SEEK_END); length = ftell(file); fseek(file, 0, SEEK_SET);
	data = malloc((size_t)length + 1);
	if (fread(data, 1, (size_t)length, file) != (size_t)length) { fclose(file); free(data); return NULL; }
	fclose(file); *size = (size_t)length; return data;
}
static void SDL_free(void *data) { free(data); }
/* (a stand-in digest: the size and the first byte) */
static void update_sha256(const unsigned char *data, size_t size, char hex[UPDATE_SHA256_TEXT_SIZE])
{ snprintf(hex, UPDATE_SHA256_TEXT_SIZE, "size%zu-first%02x", size, size ? data[0] : 0); }
static int logged;
static void platform_log(const char *format, ...) { (void)format; logged++; }
static void update_delete_file(const char *path) { unlink(path); }
@FUNCTIONS@
int main(int argc, char **argv)
{
	if (argc != 5) return 2;
	updater_file.listed = atoi(argv[2]);
	updater_file.size = (size_t)atol(argv[3]);
	snprintf(updater_file.sha256, sizeof(updater_file.sha256), "%s", argv[4]);
	printf("reusable=%d logged=%d\n", updater_reusable(argv[1]), logged);
	return 0;
}
"""


@pytest.fixture(scope="module")
def reusable(tmp_path_factory):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    directory = tmp_path_factory.mktemp("reuse")
    source = directory / "reuse.c"
    source.write_text(HARNESS.replace("@FUNCTIONS@", function(UPDATER, "updater_reusable")))
    binary = directory / "reuse"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Wno-unused-function", str(source), "-o", str(binary)], check=True)

    def run(path, listed, size, sha256):
        result = subprocess.run([str(binary), str(path), str(int(listed)), str(size), sha256],
                                capture_output=True, text=True, check=True)
        match = re.match(r"reusable=(\d) logged=(\d)", result.stdout)
        return int(match.group(1)), int(match.group(2))
    return run


def test_the_staged_file_is_reused(reusable, tmp_path):
    zip_path = tmp_path / "game.zip"
    zip_path.write_bytes(b"Pk" + b"\0" * 98)
    assert reusable(zip_path, True, 100, "size100-first50") == (1, 1)
    assert zip_path.exists()


def test_a_changed_or_wrong_file_is_not(reusable, tmp_path):
    zip_path = tmp_path / "game.zip"
    zip_path.write_bytes(b"Pk" + b"\0" * 98)
    # (another size: the list moved on)
    assert reusable(zip_path, True, 101, "size101-first50") == (0, 0)
    assert not zip_path.exists()
    zip_path.write_bytes(b"Pk" + b"\0" * 98)
    # (the size but not the digest: changed on disk)
    assert reusable(zip_path, True, 100, "size100-first00") == (0, 0)
    assert not zip_path.exists()


def test_nothing_staged_or_unlisted(reusable, tmp_path):
    assert reusable(tmp_path / "none.zip", True, 100, "size100-first50") == (0, 0)
    zip_path = tmp_path / "game.zip"
    zip_path.write_bytes(b"Pk" + b"\0" * 98)
    # (an unlisted download is never reused, and left alone here)
    assert reusable(zip_path, False, 100, "size100-first50") == (0, 0)
    assert zip_path.exists()


def test_fetch_reuses_before_downloading():
    fetch = function(UPDATER, "updater_fetch")
    assert "(updater_reusable(updater_staged.download) || updater_download_zip(updater_staged.download" in fetch


def test_listed_zip_kept_until_the_swap():
    unpack = function(UPDATER, "updater_unpack")
    assert "if (!updater_file.listed)\n\t\tupdate_delete_file(zip_path);" in unpack
    assert "if (!succeeded)\n\t\tupdate_delete_file(zip_path);" in unpack
    swap = function(UPDATER, "updater_swap")
    assert swap.index("update_delete_file(updater_staged.download);", swap.index("updater_replace_files")) < \
        swap.rindex("update_delete_file(partial);")


def test_each_picker_has_its_own_slot():
    choose = function(LAUNCHER, "choose")
    assert "launcher.chooser_generation++;" in choose
    assert "memset(current_chooser(), 0, sizeof(*current_chooser()));" in choose
    assert "SDL_ShowOpenFolderDialog(chosen, current_chooser()," in choose
    assert "SDL_ShowOpenFileDialog(chosen, current_chooser()," in choose
    poll = function(LAUNCHER, "poll_work")
    assert "SDL_GetAtomicInt(&current_chooser()->done)" in poll
    # (no code reads one shared chooser any more)
    assert "launcher.chooser." not in LAUNCHER and "&launcher.chooser," not in LAUNCHER
    current = function(LAUNCHER, "current_chooser")
    assert "launcher.choosers[launcher.chooser_generation % NUMBER_OF_CHOOSERS]" in current
