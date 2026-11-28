"""The one-time repair of a config.toml an update replaced (#2029).

v0.9.2a's 64-bit Windows and Linux zips held the settings template, and the
updater put it in place of the player's config.toml, keeping theirs as
config.toml.old. At the start the game puts it back (port_config.c's
config_repair_template, config_template_repair.h), only when config.toml is
the template (no live setting but config_version) and config.toml.old has
live settings; anything else is left as it is.

port_config.c is built with the build machine's C compiler and SDL3 beside
a stand-in for the platform layer (as tools/test_config_upgrade.py does) and
run on files in a scratch folder: beside the executable on Linux, the save
root (HALO_SAVE_ROOT) on macOS. The header's checks are also built alone.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src"
TOML = ROOT / "port" / "third_party" / "tomlc17"

KEPT = "config.toml.template-0.9.2a"

# the shape of the v0.9.2a template: help text, sections, every setting
# commented out, config_version live (CRLF as a Windows editor may leave it)
TEMPLATE = (
    "# Chupathingy settings. A line starting with # is a comment.\n\n"
    "config_version = 2\n\n"
    "[display]\n\n# The renderer.\n# renderer = \"gl\"\n# fullscreen = false\n\n"
    "[audio]\n\n# master_volume = 1.0\n\n"
    "[update]\n\n# auto = true\n"
)
PLAYERS = (
    "config_version = 2\n\n[display]\nmenus = \"xbox\"\n\n"
    "[audio]\nvolume = 0.4\n"
)

MAIN = r"""
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "port_config.h"

void platform_log(const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	printf("LOG ");
	vprintf(format, arguments);
	printf("\n");
	va_end(arguments);
}

int main(void)
{
	printf("MENUS %s\n", config_string("display.menus"));
	return 0;
}
"""

# config_repair_template itself, out of port_config.c, with stand-ins for
# its file calls: runs wherever there is a C compiler (no SDL3)
REPAIR = r"""
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "config_template_repair.h"
static int SDL_GetPathInfo(const char *path, void *info) { (void)info; return access(path, F_OK) == 0; }
static int SDL_RenamePath(const char *from, const char *to) { return rename(from, to) == 0; }
static void platform_log(const char *format, ...)
{ va_list a; va_start(a, format); printf("LOG "); vprintf(format, a); printf("\n"); va_end(a); }
@READ@
@REPAIR@
int main(int argc, char **argv)
{
	if (argc != 2) return 2;
	config_repair_template(argv[1]);
	return 0;
}
"""

UNIT = r"""
#include <stdio.h>
#include <stdlib.h>
#include "config_template_repair.h"

static char *slurp(const char *path)
{
	FILE *file = fopen(path, "rb");
	long length;
	char *text;

	if (!file) return NULL;
	fseek(file, 0, SEEK_END); length = ftell(file); fseek(file, 0, SEEK_SET);
	text = calloc(1, (size_t)length + 1);
	if (fread(text, 1, (size_t)length, file) != (size_t)length) { fclose(file); return NULL; }
	fclose(file);
	return text;
}

int main(int argc, char **argv)
{
	char *current = slurp(argv[1]), *old = argc > 2 ? slurp(argv[2]) : NULL;

	printf("template=%d repair=%d\n", config_template_is_template(current),
		config_template_should_repair(current, old));
	return 0;
}
"""


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def sdl_flags():
    pkg = shutil.which("pkg-config")
    if pkg:
        done = subprocess.run([pkg, "--cflags", "--libs", "sdl3"], capture_output=True, text=True)
        if done.returncode == 0:
            return done.stdout.split()
    brew = Path("/opt/homebrew")
    if (brew / "include" / "SDL3").is_dir():
        return [f"-I{brew}/include", f"-L{brew}/lib", "-lSDL3"]
    return None


@pytest.fixture(scope="module")
def unit(tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    work = tmp_path_factory.mktemp("template_unit")
    (work / "unit.c").write_text(UNIT)
    out = work / "unit"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Werror", "-I", str(SOURCE), "-o", str(out), str(work / "unit.c")],
                   check=True)

    def run(current, old=None):
        (work / "current").write_bytes(current.encode())
        arguments = [str(out), str(work / "current")]
        if old is not None:
            (work / "old").write_bytes(old.encode())
            arguments.append(str(work / "old"))
        done = subprocess.run(arguments, capture_output=True, text=True, check=True)
        return done.stdout.strip()
    return run


@pytest.fixture(scope="module")
def game(tmp_path_factory):
    if sys.platform not in ("linux", "darwin"):
        pytest.skip("builds port_config.c for Linux or macOS")
    cc, flags = compiler(), sdl_flags()
    if not cc or flags is None:
        pytest.skip("needs a C compiler and SDL3")
    work = tmp_path_factory.mktemp("template_repair")
    (work / "main.c").write_text(MAIN)
    (work / "platform.h").write_text(
        "void platform_log(const char *format, ...);\n"
        "int platform_app_folder(char *path, unsigned long size);\n")
    # (copied, so that the stand-in platform.h is the one it includes; the
    # port's halo_*.h alone, not port/linux/include's libc stand-ins)
    for name in ("port_config.c", "port_config.h"):
        shutil.copy(SOURCE / name, work / name)
    for header in (ROOT / "port/linux/include").glob("halo_*.h"):
        shutil.copy(header, work / header.name)
    out = work / "config_test"
    done = subprocess.run(
        [cc, "-std=gnu11", "-w", "-I", str(work), "-I", str(SOURCE), "-I", str(TOML), "-o", str(out),
         str(work / "main.c"), str(work / "port_config.c"), str(SOURCE / "posix_files.c"), str(TOML / "tomlc17.c"),
         *flags, "-lpthread"],
        capture_output=True, text=True)
    if done.returncode != 0:
        pytest.skip("port_config.c does not build alone here: " + done.stderr[:600])
    return out


@pytest.fixture(scope="module")
def repair(tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    sys.path.insert(0, str(ROOT / "tools"))
    from harness import function, read
    source = read("port/linux/src/port_config.c")
    # (the desktop reader: SDL_LoadFile's stand-in is fopen's)
    reader = (
        "static char *config_read_file(const char *path, size_t *size)\n"
        "{ FILE *f = fopen(path, \"rb\"); long n; char *t; if (!f) return NULL;\n"
        "  fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET); t = malloc((size_t)n + 1);\n"
        "  if (fread(t, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(t); return NULL; }\n"
        "  fclose(f); t[n] = 0; *size = (size_t)n; return t; }\n")
    work = tmp_path_factory.mktemp("template_harness")
    (work / "repair.c").write_text(REPAIR.replace("@READ@", reader)
                                   .replace("@REPAIR@", function(source, "config_repair_template")))
    out = work / "repair"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Wno-unused-function", "-I", str(SOURCE), "-o", str(out),
                    str(work / "repair.c")], check=True)
    return out


@pytest.fixture(params=["harness", "game"])
def starter(request):
    if request.param == "harness":
        binary = request.getfixturevalue("repair")
        return lambda tmp_path, config=None, old=None: start_harness(binary, tmp_path, config, old)
    binary = request.getfixturevalue("game")
    return lambda tmp_path, config=None, old=None: start(binary, tmp_path, config, old)


def start_harness(binary, tmp_path, config=None, old=None):
    """config_repair_template alone on config.toml in a folder"""
    folder = tmp_path / "game"
    folder.mkdir()
    if config is not None:
        (folder / "config.toml").write_bytes(config.encode())
    if old is not None:
        (folder / "config.toml.old").write_bytes(old.encode())
    done = subprocess.run([str(binary), str(folder / "config.toml")], capture_output=True, text=True)
    assert done.returncode == 0, done.stderr
    return folder, done.stdout + "MENUS xbox\n"


def start(binary, tmp_path, config=None, old=None):
    """the game started with config.toml (and config.toml.old) where it reads
    them: their folder, its output"""
    saves = tmp_path / "saves"
    saves.mkdir()
    beside = tmp_path / "bin"
    beside.mkdir()
    shutil.copy(binary, beside / "config_test")
    folder = saves if sys.platform == "darwin" else beside
    if config is not None:
        (folder / "config.toml").write_bytes(config.encode())
    if old is not None:
        (folder / "config.toml.old").write_bytes(old.encode())
    env = dict(os.environ, HALO_SAVE_ROOT=str(saves), HOME=str(tmp_path / "home"))
    for name in list(env):
        if name in ("HALO_MENUS", "HALO_VOLUME"):
            env.pop(name)
    done = subprocess.run([str(beside / "config_test")], capture_output=True, text=True, env=env, cwd=beside)
    assert done.returncode == 0, done.stderr
    return folder, done.stdout


def live(path):
    """a settings file's live lines (the game completes a file with the
    commented help of the settings it lacks)"""
    return [line.strip() for line in path.read_text().splitlines()
            if line.strip() and not line.strip().startswith(("#", "["))]


def live_text(text):
    return [line.strip() for line in text.splitlines() if line.strip() and not line.strip().startswith(("#", "["))]


def repaired_lines(said):
    return [line for line in said.splitlines() if line.startswith("LOG ") and KEPT in line]


# ---------- the four cases, through the game's own start

def test_template_and_old_restores(starter, tmp_path):
    folder, said = starter(tmp_path, TEMPLATE, PLAYERS)
    assert live(folder / "config.toml") == live_text(PLAYERS)
    assert (folder / KEPT).read_text() == TEMPLATE
    assert not (folder / "config.toml.old").exists()
    assert "MENUS xbox" in said
    assert len(repaired_lines(said)) == 1


def test_real_config_and_old_is_left_alone(starter, tmp_path):
    mine = PLAYERS.replace("0.4", "0.7")
    folder, said = starter(tmp_path, mine, PLAYERS)
    assert live(folder / "config.toml") == live_text(mine)
    assert (folder / "config.toml.old").read_text() == PLAYERS
    assert not (folder / KEPT).exists()
    assert not repaired_lines(said)


def test_template_without_old_is_left_alone(starter, tmp_path):
    folder, said = starter(tmp_path, TEMPLATE)
    assert live(folder / "config.toml") == ["config_version = 2"]
    assert not (folder / KEPT).exists()
    assert not (folder / "config.toml.old").exists()
    assert not repaired_lines(said)


def test_template_the_player_edited_is_left_alone(starter, tmp_path):
    edited = TEMPLATE.replace('# renderer = "gl"', 'menus = "pc"')
    folder, said = starter(tmp_path, edited, PLAYERS)
    assert 'menus = "pc"' in live(folder / "config.toml")
    assert (folder / "config.toml.old").read_text() == PLAYERS
    assert not (folder / KEPT).exists()
    assert not repaired_lines(said)


def test_once_only(game, tmp_path):
    """a kept template already there: a second template is not swapped"""
    folder, _ = start(game, tmp_path, TEMPLATE, PLAYERS)
    (folder / "config.toml").rename(folder / "config.toml.old")
    (folder / "config.toml").write_text(TEMPLATE)
    runs = tmp_path / "again"
    runs.mkdir()
    # (the same folder, started again)
    env = dict(os.environ, HALO_SAVE_ROOT=str(tmp_path / "saves"), HOME=str(tmp_path / "home"))
    done = subprocess.run([str(tmp_path / "bin/config_test")], capture_output=True, text=True, env=env,
                          cwd=tmp_path / "bin")
    assert done.returncode == 0
    assert not repaired_lines(done.stdout)
    assert (folder / "config.toml.old").exists()


# ---------- the header's checks alone

def test_the_template_is_recognized(unit):
    assert unit(TEMPLATE, PLAYERS) == "template=1 repair=1"
    assert unit(TEMPLATE.replace("\n", "\r\n"), PLAYERS) == "template=1 repair=1"
    assert unit("﻿" + TEMPLATE, PLAYERS) == "template=1 repair=1"
    # (indented comments and a section with its own comment)
    assert unit("config_version=2\n   # x = 1\n[audio] # sound\n", PLAYERS) == "template=1 repair=1"


def test_anything_else_is_not(unit):
    assert unit(PLAYERS, PLAYERS) == "template=0 repair=0"
    assert unit(TEMPLATE.replace("# auto = true", "auto = false"), PLAYERS) == "template=0 repair=0"
    # (no config_version: not the shipped template)
    assert unit("# all comments\n[display]\n", PLAYERS) == "template=0 repair=0"
    # (config_version_x is a setting, not the version)
    assert unit("config_version = 2\nconfig_versions = 3\n", PLAYERS) == "template=0 repair=0"
    # (an old file that is a template too, or empty, has nothing to restore)
    assert unit(TEMPLATE, TEMPLATE) == "template=1 repair=0"
    assert unit(TEMPLATE, "") == "template=1 repair=0"
    assert unit(TEMPLATE) == "template=1 repair=0"


def test_the_shipped_092a_template_if_present(unit):
    """the real v0.9.2a template, when a copy is at HALO_092A_TEMPLATE"""
    path = os.environ.get("HALO_092A_TEMPLATE")
    if not path or not Path(path).is_file():
        pytest.skip("HALO_092A_TEMPLATE not set")
    assert unit(Path(path).read_text(encoding="utf-8"), PLAYERS) == "template=1 repair=1"
