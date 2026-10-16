"""The settings file of an older version (port/linux/src/port_config.c): a
value that was an earlier version's default and was never a choice
(discord.application_id, OpenCE's application) is taken out and written
again commented out at the current default, so that it follows, with this
version's help text and no second entry of the key.

port_config.c is built with the build machine's C compiler and SDL3 beside
a stand-in for the platform layer, and run on files in a scratch folder
(HALO_SAVE_ROOT, as the automated runs of the game have it)."""
import os
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src"
TOML = ROOT / "port" / "third_party" / "tomlc17"

OLD = "1553978809840050229"
NEW = "1560397272166957136"

MAIN = r"""
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "port_config.h"

void platform_log(const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	vprintf(format, arguments);
	printf("\n");
	va_end(arguments);
}

int main(int count, char **arguments)
{
	(void)count; (void)arguments;
	printf("VALUE %s\n", config_string("discord.application_id"));
	return 0;
}
"""

# the owner's [discord] section: the commented line and the active one, each
# under the help text of the day
OLD_HELP = (
    "# The Discord application internet play invites go through (its name is\n"
    "# what Discord says is being played).\n"
    "# (for one run: HALO_DISCORD_APPLICATION=<value>)\n"
)


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
def binary(tmp_path_factory):
    cc, flags = compiler(), sdl_flags()
    if not cc or flags is None:
        pytest.skip("needs a C compiler and SDL3")
    work = tmp_path_factory.mktemp("config_upgrade")
    (work / "main.c").write_text(MAIN)
    (work / "platform.h").write_text(
        "void platform_log(const char *format, ...);\n"
        "int platform_app_folder(char *path, unsigned long size);\n")
    # (copied, so that the stand-in platform.h is the one it includes)
    for name in ("port_config.c", "port_config.h"):
        shutil.copy(SOURCE / name, work / name)
    out = work / "config_test"
    done = subprocess.run(
        [cc, "-std=gnu11", "-w", "-I", str(work), "-I", str(TOML), "-o", str(out),
         str(work / "main.c"), str(work / "port_config.c"), str(TOML / "tomlc17.c"), *flags, "-lpthread"],
        capture_output=True, text=True)
    if done.returncode != 0:
        pytest.skip("port_config.c does not build alone here: " + done.stderr[:600])
    return out


def run(binary, tmp_path, text):
    folder = tmp_path / "root"
    folder.mkdir()
    config = folder / "config.toml"
    config.write_text(text)
    # (beside the executable on Linux, the save root on macOS)
    beside = tmp_path / "bin"
    beside.mkdir()
    shutil.copy(binary, beside / "config_test")
    shutil.copy(config, beside / "config.toml")
    env = dict(os.environ, HALO_SAVE_ROOT=str(folder))
    env.pop("HALO_DISCORD_APPLICATION", None)
    done = subprocess.run([str(beside / "config_test")], capture_output=True, text=True, env=env, cwd=beside)
    assert done.returncode == 0, done.stderr
    written = config if config.read_text() != text else beside / "config.toml"
    for candidate in (config, beside / "config.toml"):
        if candidate.read_text() != text:
            written = candidate
    return done.stdout, written.read_text()


def owners_file():
    return (
        "# Halo settings\n\nconfig_version = 2\n\n[display]\n\n# Menus.\n# menus = \"xbox\"\n\n"
        "[discord]\n\n" + OLD_HELP + f'# application_id = "{OLD}"\n\n' + OLD_HELP +
        f'application_id = "{OLD}"\n' +
        "\n# Presence.\n# presence = \"full\"\n")


def entries(text):
    return re.findall(r"^\s*#?\s*application_id\s*=", text, re.M)


def test_old_default_follows(binary, tmp_path):
    said, text = run(binary, tmp_path, owners_file())
    assert f"VALUE {NEW}" in said
    assert OLD not in text
    assert len(entries(text)) == 1
    assert re.search(rf'^# application_id = "{NEW}"$', text, re.M)
    assert "internet play invites" not in text
    assert "Chupathingy" in text or "Discord application the game shows as" in text
    assert "# presence" in text and "[discord]" in text


def test_chosen_value_is_kept(binary, tmp_path):
    chosen = owners_file().replace(f'\napplication_id = "{OLD}"', '\napplication_id = "42"')
    said, text = run(binary, tmp_path, chosen)
    assert "VALUE 42" in said
    assert 'application_id = "42"' in text
