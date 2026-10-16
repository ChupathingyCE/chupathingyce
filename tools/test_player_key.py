"""The game list's player key (port/linux/src/posix_browser.c,
posix_browser_private_key) and the Android launchers' save folder (their
share_entry): on Android a key file the launcher left readable to adb is
still read, elsewhere only one of the user's no one else can read is; and
the launchers leave the key's own mode alone.

The functions are compiled out of their files with stubs for the rest (the
build machine's C compiler; no game data, SDL or mbed TLS needed)."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
KEY_SIZE = 32


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def function(source, name):
    """A function's text (from its return type to its closing brace)."""
    match = re.search(r"^(?:static )?int " + name + r"\([^;{]*\)\s*\{", source, re.M)
    assert match, name
    start = source.index("{", match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def private_key_source(android):
    browser = (ROOT / "port/linux/src/posix_browser.c").read_text()
    # (the POSIX one, after #else, and its mode test before it)
    posix = browser[browser.index("#else", browser.index("posix_browser_private_key")):]
    macro = re.search(r"#ifdef __ANDROID__\n#define PRIVATE_KEY_PRIVATE.*?#endif\n", posix, re.S).group(0)
    return ("#define _GNU_SOURCE\n" + ("#define __ANDROID__ 1\n" if android else "") + r'''
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define PSA_SUCCESS 0
static int crypto_ready = 1;
static void posix_browser_lock(void) {}
static void posix_browser_unlock(void) {}
static void set_up(void) {}
static int psa_generate_random(unsigned char *key, size_t size)
{
	memset(key, 0x5a, size);
	return 0;
}
''' + macro + function(posix, "posix_browser_private_key") + r'''
int main(int argc, char **argv)
{
	unsigned char key[32];

	(void)argc;
	return posix_browser_private_key(argv[1], key, (int)sizeof(key)) ? 0 : 1;
}
''')


def build(tmp_path, name, source):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    (tmp_path / (name + ".c")).write_text(source)
    binary = tmp_path / name
    result = subprocess.run([cc, "-std=c11", "-Wall", "-Werror", "-o", str(binary), str(tmp_path / (name + ".c"))],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return binary


@pytest.mark.parametrize("android", [False, True])
def test_player_key_made_then_read(tmp_path, android):
    """The first call makes the key (0600), the next reads the same one."""
    binary = build(tmp_path, "key", private_key_source(android))
    key = tmp_path / "game_list_player.key"
    assert subprocess.run([str(binary), str(key)]).returncode == 0
    assert key.stat().st_size == KEY_SIZE and key.stat().st_mode & 0o777 == 0o600
    assert subprocess.run([str(binary), str(key)]).returncode == 0


@pytest.mark.parametrize("android", [False, True])
def test_player_key_readable_by_others(tmp_path, android):
    """A key others may read (0664, as Android's launcher made it) is read
    on Android, where the app's storage is its own, and refused elsewhere."""
    binary = build(tmp_path, "key", private_key_source(android))
    key = tmp_path / "game_list_player.key"
    key.write_bytes(bytes(KEY_SIZE))
    key.chmod(0o664)
    assert (subprocess.run([str(binary), str(key)]).returncode == 0) == android


@pytest.mark.parametrize("android", [False, True])
def test_player_key_link_refused(tmp_path, android):
    """A link in the key's place is never followed."""
    binary = build(tmp_path, "key", private_key_source(android))
    target = tmp_path / "elsewhere"
    target.write_bytes(bytes(KEY_SIZE))
    target.chmod(0o600)
    (tmp_path / "game_list_player.key").symlink_to(target)
    assert subprocess.run([str(binary), str(tmp_path / "game_list_player.key")]).returncode == 1


@pytest.mark.parametrize("launcher", ["port/android/host/host_main.c", "port/android/native64/android64_main.c"])
def test_launcher_leaves_the_key_private(tmp_path, launcher):
    """The launcher opens the save folder's files to adb (0664), but not
    the player key."""
    text = (ROOT / launcher).read_text()
    share = function(text, "share_entry")
    binary = build(tmp_path, "share", "#define _XOPEN_SOURCE 700\n" + r'''
#include <ftw.h>
#include <string.h>
#include <sys/stat.h>
''' + share + r'''
int main(int argc, char **argv)
{
	(void)argc;
	return nftw(argv[1], share_entry, 16, FTW_PHYS);
}
''')
    save = tmp_path / "save"
    save.mkdir()
    for name in ("game_list_player.key", "config.toml"):
        (save / name).write_bytes(b"x")
        (save / name).chmod(0o600)
    assert subprocess.run([str(binary), str(save)]).returncode == 0
    assert (save / "game_list_player.key").stat().st_mode & 0o777 == 0o600
    assert (save / "config.toml").stat().st_mode & 0o777 == 0o664


def test_failed_key_load_is_tried_again():
    """A player key that could not be read is not given up on for the run
    (browser.c, player_key): only a loaded key is kept."""
    browser = (ROOT / "port/linux/src/browser.c").read_text()
    body = re.search(r"^static int player_key\(.*?^\}", browser, re.M | re.S).group(0)
    assert re.search(r"else if \(loaded > 0\)\s*\{[^}]*player_key_loaded = loaded;", body)
    assert body.count("player_key_loaded = ") == 1
