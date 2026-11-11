"""Checks that every console's build gets the console switches meant for it.

The shared headers leave the desktop's extras out of a console's build with
HALO_CONSOLE_NO_* switches (port/linux/include/halo_port_features.h,
halo_console_features.h). The 360 and the Wii U define HALO_CONSOLE, but the
original Xbox doesn't: it sets HALO_XBOX_CONSOLE and picks its own switches
(warthog-xbox's halo_xbox_features.h). A switch added only under
HALO_CONSOLE missed it: the six-stage HUD meter, the particle streams and the
smoothed shake ran on the Xbox (warthog-xbox#129: the meter's stages made its
models chrome).

Each console is preprocessed here with only the defines its build gives the
shared headers, as its prefix header and build.py set them, and the switches
it gets are compared."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "port" / "linux" / "include"

# what each console's build defines before the shared headers (its prefix
# header and its build.py), as far as the switches go
CONSOLES = {
    "xbox": ["HALO_XBOX_CONSOLE", "HALO_CONSOLE_DELTA", "HALO_CONSOLE_PLAYERS=32", "HALO_MAP_LIST",
             "HALO_CUSTOM_EDITION", "HALO_CONSOLE_GAME_REPORTS"],
    "xbox360": ["HALO_CONSOLE", "HALO_XBOX360_CONSOLE", "HALO_BIG_ENDIAN", "HALO_64BIT",
                "HALO_CONSOLE_DELTA", "HALO_CONSOLE_PLAYERS=32", "HALO_MAP_LIST"],
    "wiiu": ["HALO_CONSOLE", "HALO_WIIU_CONSOLE", "HALO_BIG_ENDIAN", "HALO_CONSOLE_DELTA",
             "HALO_CONSOLE_PLAYERS=32", "HALO_MAP_LIST"],
    "switch": ["HALO_SWITCH_CONSOLE", "HALO_GAME_BROWSER", "HALO_CUSTOM_EDITION", "HALO_MAP_LIST"],
}

DESKTOP = ["HALO_GAME_BROWSER", "HALO_CUSTOM_EDITION", "HALO_MAP_LIST"]

# what a console's system lacks, not an extra it leaves out: one console's
# own (halo_console_features.h)
PLATFORM_GATES = {"HALO_CONSOLE_NO_WRITE_WATCH", "HALO_CONSOLE_NO_THREAD_LOCAL"}

# what the original Xbox must leave out: the ones that reached it before
# (#1068's meter stages, the particle streams, the smoothed shake) and the
# ones its own header sets
XBOX_EXPECTED = {
    "HALO_CONSOLE_NO_SMOOTH_METER_EDGES",
    "HALO_CONSOLE_NO_PARTICLE_STREAMS",
    "HALO_CONSOLE_NO_SMOOTH_SHAKE",
    "HALO_CONSOLE_NO_SMOOTH_FUNCTIONS",
    "HALO_CONSOLE_NO_TEST_GAMEPADS",
    "HALO_CONSOLE_NO_VOICE",
    "HALO_CONSOLE_NO_HIRES",
    "HALO_CONSOLE_NO_TELNET",
    "HALO_CONSOLE_NO_UPDATER",
    "HALO_CONSOLE_NO_DISCORD",
    "HALO_CONSOLE_NO_LIGHT_EFFECTS",
    "HALO_CONSOLE_NO_TITLE_CHOICE",
    "HALO_CONSOLE_NO_GUIDE",
    "HALO_CONSOLE_NO_SHADER_PREP",
}


def gates(defines):
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    # (in halo_port_limits.h's order: the consoles' own switches first)
    source = '#include "{}"\n#include "{}"\n'.format(
        (INCLUDE / "halo_console_features.h").as_posix(), (INCLUDE / "halo_port_features.h").as_posix())
    command = [compiler, "-E", "-dM", "-x", "c", "-"] + ["-D" + define for define in defines]
    result = subprocess.run(command, input=source, capture_output=True, text=True, check=True)
    names = {line.split()[1] for line in result.stdout.splitlines() if line.startswith("#define ")}
    return {name for name in names if name.startswith("HALO_CONSOLE_NO_")} - PLATFORM_GATES


def test_the_xbox_leaves_out_what_halo_console_leaves_out():
    """the Xbox doesn't define HALO_CONSOLE, so every switch HALO_CONSOLE
    sets has to reach it by HALO_XBOX_CONSOLE too"""
    missing = gates(["HALO_CONSOLE"]) - gates(CONSOLES["xbox"])
    assert not missing, "the original Xbox's build misses {}".format(sorted(missing))


def test_the_xbox_gets_its_switches():
    missing = XBOX_EXPECTED - gates(CONSOLES["xbox"])
    assert not missing, "the original Xbox's build misses {}".format(sorted(missing))


@pytest.mark.parametrize("console", sorted(CONSOLES))
def test_every_console_gets_the_every_console_switches(console):
    """what the Switch alone gets is what the headers give every console
    (their lists of every console's switch): each console gets it too"""
    missing = gates(CONSOLES["switch"]) - gates(CONSOLES[console])
    assert not missing, "{} misses {}".format(console, sorted(missing))


@pytest.mark.parametrize("console", ["xbox", "xbox360", "wiiu"])
def test_the_consoles_on_halo_console_agree(console):
    """the 360, the Wii U and the Xbox leave out the same desktop extras
    (a console's own extras, the Xbox's EXIT and medal icons, aside)"""
    own = {"HALO_CONSOLE_NO_EXIT", "HALO_CONSOLE_NO_MEDAL_ICONS"}
    expected = gates(CONSOLES["wiiu"]) | gates(CONSOLES["xbox360"])
    missing = expected - gates(CONSOLES[console]) - own
    assert not missing, "{} misses {}".format(console, sorted(missing))


def test_the_desktop_leaves_nothing_out():
    assert not gates(DESKTOP)
    assert not gates([])
