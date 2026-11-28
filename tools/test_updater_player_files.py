"""The updater never puts a player's own file in place (#2029).

v0.9.2a's 64-bit Windows and Linux zips held a config.toml (the settings
template), and the updater put it over every player's own. Now the zip's
config.toml, and its kin (config.toml.old, ...), are left out whatever the
zip holds: port/linux/src/updater.c's zip_extract, compiled here with its
own zlib unpacking and stand-ins for SDL's files, unpacks real zips made
with Python's zipfile into a scratch update.partial/.
"""
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from harness import function, read  # noqa: E402

UPDATER = read("port/linux/src/updater.c")

HARNESS = r"""
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>
typedef long long Sint64;
typedef FILE SDL_IOStream;
#define SDL_IO_SEEK_SET SEEK_SET
#define SDL_IO_SEEK_CUR SEEK_CUR
static Sint64 SDL_GetIOSize(SDL_IOStream *f)
{ long at = ftell(f), size; fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, at, SEEK_SET); return size; }
static Sint64 SDL_SeekIO(SDL_IOStream *f, Sint64 offset, int whence)
{ return fseek(f, (long)offset, whence) ? -1 : ftell(f); }
static Sint64 SDL_TellIO(SDL_IOStream *f) { return ftell(f); }
static size_t SDL_ReadIO(SDL_IOStream *f, void *data, size_t size) { return fread(data, 1, size, f); }
static size_t SDL_WriteIO(SDL_IOStream *f, const void *data, size_t size) { return fwrite(data, 1, size, f); }
static SDL_IOStream *SDL_IOFromFile(const char *path, const char *mode) { return fopen(path, mode); }
static int SDL_CloseIO(SDL_IOStream *f) { return fclose(f) == 0; }
static const char *SDL_GetError(void) { return "error"; }
#define SDL_strcasecmp strcasecmp
#define SDL_strncasecmp strncasecmp
static void platform_log(const char *format, ...)
{ va_list a; va_start(a, format); printf("LOG "); vprintf(format, a); printf("\n"); va_end(a); }
#define PATH_SEPARATOR "/"
#define UPDATE_DIRECTORY "update.partial"
#define MAXIMUM_UPDATE_FILES 32
static char updater_directory[1024];
static struct { char main_name[160]; } updater_file = { "chupathingy" };
static const char *const updater_executable_names[] = { "halo", "chupathingy" };
@FUNCTIONS@
int main(int argc, char **argv)
{
	char names[MAXIMUM_UPDATE_FILES][256], error[512] = "";
	int executable[MAXIMUM_UPDATE_FILES], count = 0, index, ok;
	SDL_IOStream *zip;

	if (argc != 3) return 2;
	snprintf(updater_directory, sizeof(updater_directory), "%s", argv[2]);
	zip = fopen(argv[1], "rb");
	if (!zip) return 2;
	ok = zip_extract(zip, names, executable, &count, error, sizeof(error));
	fclose(zip);
	printf("OK %d %s\n", ok, error);
	for (index = 0; index < count; index++)
		printf("NAME %s\n", names[index]);
	return 0;
}
"""

FUNCTIONS = ("updater_partial_path", "zip_word", "zip_long", "zip_extract_entry", "zip_name_allowed",
             "zip_name_players", "updater_executable_name", "zip_extract")


@pytest.fixture(scope="module")
def extract(tmp_path_factory):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    directory = tmp_path_factory.mktemp("updater_player_files")
    source = directory / "extract.c"
    source.write_text(HARNESS.replace("@FUNCTIONS@", "\n".join(function(UPDATER, name) for name in FUNCTIONS)))
    binary = directory / "extract"
    done = subprocess.run([cc, "-std=gnu99", "-w", str(source), "-o", str(binary), "-lz"],
                          capture_output=True, text=True)
    if done.returncode != 0:
        pytest.skip("the harness does not build here (zlib?): " + done.stderr[:600])

    def run(zip_path, folder):
        (folder / "update.partial").mkdir(parents=True, exist_ok=True)
        result = subprocess.run([str(binary), str(zip_path), str(folder)], capture_output=True, text=True,
                                check=True)
        ok = re.search(r"^OK (\d)", result.stdout, re.M)
        names = re.findall(r"^NAME (.*)$", result.stdout, re.M)
        logs = re.findall(r"^LOG (.*)$", result.stdout, re.M)
        return int(ok.group(1)), names, logs
    return run


def make_zip(path, entries, method=zipfile.ZIP_DEFLATED):
    with zipfile.ZipFile(path, "w", method) as archive:
        for name, data in entries.items():
            archive.writestr(name, data)
    return path


RELEASE = {
    "chupathingy": b"\x7fELF game",
    "libSDL3.so.0": b"sdl",
    "brokers.txt": b"broker.example:1883\n",
    "LICENSE.txt": b"GPL-3.0\n",
}

TEMPLATE = b"# Chupathingy settings\n\nconfig_version = 2\n\n[display]\n# renderer = \"gl\"\n"


@pytest.mark.parametrize("method", [zipfile.ZIP_DEFLATED, zipfile.ZIP_STORED])
def test_config_in_the_zip_is_left_out(extract, tmp_path, method):
    zip_path = make_zip(tmp_path / "release.zip", {**RELEASE, "config.toml": TEMPLATE}, method)
    ok, names, logs = extract(zip_path, tmp_path / "game")
    assert ok == 1
    assert sorted(names) == sorted(RELEASE)
    assert not (tmp_path / "game/update.partial/config.toml").exists()
    assert (tmp_path / "game/update.partial/brokers.txt").read_bytes() == RELEASE["brokers.txt"]
    assert any("config.toml" in line and "player's own" in line for line in logs)


def test_its_kin_and_other_cases_too(extract, tmp_path):
    players = ["config.toml.old", "config.toml.new", "Config.TOML", "CONFIG.toml.template-0.9.2a"]
    zip_path = make_zip(tmp_path / "release.zip", {**RELEASE, **{name: TEMPLATE for name in players}})
    ok, names, _ = extract(zip_path, tmp_path / "game")
    assert ok == 1
    assert sorted(names) == sorted(RELEASE)
    for name in players:
        assert not (tmp_path / "game/update.partial" / name).exists()


def test_names_like_it_are_still_a_release_s(extract, tmp_path):
    # (not config.toml: a release may ship files that merely start alike)
    alike = {"config.tomlx": b"x", "config.txt": b"y", "my-config.toml": b"z"}
    zip_path = make_zip(tmp_path / "release.zip", {**RELEASE, **alike})
    ok, names, _ = extract(zip_path, tmp_path / "game")
    assert ok == 1
    assert sorted(names) == sorted({**RELEASE, **alike})


def test_v092a_zip_shape_keeps_the_player_s_settings(extract, tmp_path):
    """the v0.9.2a 64-bit Linux zip's six files: five unpacked, config.toml not"""
    zip_path = make_zip(tmp_path / "chupathingyce-linux64-release.zip",
                        {"brokers.txt": b"b", "chupathingy": b"g", "config.toml": TEMPLATE, "libSDL3.so.0": b"s",
                         "LICENSE.txt": b"l", "THIRD-PARTY-NOTICES.txt": b"n"})
    ok, names, _ = extract(zip_path, tmp_path / "game")
    assert ok == 1
    assert "config.toml" not in names and len(names) == 5
