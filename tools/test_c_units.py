"""The C tests of small units that need no SDL and no game, each built with
the build machine's C compiler, warnings as errors, and run: one entry of
UNITS per unit (port/linux/tests/<name>_test.c and the sources it tests).
Add a unit with one line here.

- ping: Online Games' pings before joining (port/linux/src/p2p_probe.c) and
  their estimates (browser_ping.c)
- social: the Guide's data (social.c: its rows, the polling schedule and
  presence's), with the activity unit its status lines use and the profile
  unit that reads the site's friends
- party_teams: the host's party teams plan (party_teams_plan.c: keeping a
  party on one team), as C89 (the consoles' compilers read it too)
- ui_point: the mapping between the window's points and the menus'
- touch_menu: the menus' touch gestures
- link_register: the handler of halo:// links (link_register.c, with
  discord_register.c, whose quoting and marks it shares), on a scratch
  folder (as ~/.local/share/applications): another program's handler is
  never replaced
- discord_activity: the activity Discord shows
- discord_register: the launch command registered for Discord, on a scratch
  folder
- p2p_copies: one public game listed under every network version, shown once
- p2p_host_port: the port a host's tunnel takes (2302, the next free one, or
  a chosen one: p2p_host_port.h)
- port_folders: the game's own folders beside the maps and the saves
  (data/, logs/, cache/, private/)
- console_shell: the drop-down console's typing (port/linux/game/
  console_shell.c): words to the scripts' expression, quotes, numbers,
  nested calls and the console's own form
- delta_input_rules: DELTA's input rules (controls.delta's keys, Back held
  in the menus)
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SRC = "port/linux/src"
GAME = "port/linux/game"

C11 = ["-std=c11"]
# name: (the compiler's standard flags, the units tested (after the test's own
# port/linux/tests/<name>_test.c), flags after them, whether the test is given
# a scratch folder)
UNITS = {
    "console_shell": (C11, [f"{GAME}/console_shell.c"], [], False),
    "delta_input_rules": (C11, [f"{SRC}/delta_input_rules.c"], [], False),
    "discord_activity": (C11, [f"{SRC}/discord_activity.c"], [], False),
    "discord_register": (C11, [f"{SRC}/discord_register.c"], [], True),
    "link_register": (C11, [f"{SRC}/link_register.c", f"{SRC}/discord_register.c"], [], True),
    "p2p_copies": (C11, [f"{SRC}/p2p_copies.c"], [], False),
    "p2p_host_port": (C11, [], [], False),
    "party_teams": (["-std=c89", "-pedantic"], [f"{SRC}/party_teams_plan.c"], [], False),
    "ping": (C11, [f"{SRC}/p2p_probe.c", f"{SRC}/browser_ping.c"], [], False),
    "port_folders": (["-std=gnu11"], [f"{SRC}/port_folders.c"], [], False),
    "social": (C11, [f"{SRC}/social.c", f"{SRC}/discord_activity.c", f"{SRC}/delta_profile.c"],
               ["-DHALO_GAME_BROWSER"], False),
    "touch_menu": (C11, [f"{SRC}/touch_menu.c"], ["-lm"], False),
    "ui_point": (C11, [f"{SRC}/ui_point.c"], ["-lm"], False),
}


def compiler():
    """Finds a C compiler on the build machine.

    Returns the path of clang, cc or gcc (the first found), or None.
    """
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


@pytest.mark.parametrize("name", sorted(UNITS))
def test_c_unit(name, tmp_path):
    """Builds the unit with its tests, warnings as errors, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    standard, sources, after, folder = UNITS[name]
    binary = tmp_path / (f"{name}_test" + (".exe" if sys.platform == "win32" else ""))
    build = subprocess.run(
        [cc, *standard, "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port" / "linux" / "tests" / f"{name}_test.c"),
         *(str(ROOT / source) for source in sources), *after,
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    arguments = []
    if folder:
        applications = tmp_path / "applications"
        applications.mkdir()
        arguments = [str(applications)]
    result = subprocess.run([str(binary), *arguments], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_guide_party_actions_are_sent():
    """Every party action DELTA asks for (guide.c's party_act calls, and
    what debug.party_test may name) is one social_party_fields builds and
    the site takes (social.c's social_party_actions): NEW PARTY CODE's
    "code" once went nowhere, refused before it was sent."""
    social = (ROOT / "port" / "linux" / "src" / "social.c").read_text()
    table = re.search(r"social_party_actions\[\] =\s*\{(.*?)\};", social, re.S)
    assert table, "social.c has no social_party_actions table"
    taken = set(re.findall(r'"([a-z]+)"', table.group(1)))
    guide = (ROOT / "port" / "linux" / "game" / "guide.c").read_text()
    sent = set(re.findall(r'party_act\("([a-z]*)"', guide))
    # (and a choice of two: party_act(kick ? "kick" : "lead", ...))
    for pair in re.findall(r'party_act\([^;]*\?\s*"([a-z]+)"\s*:\s*"([a-z]+)"', guide):
        sent |= set(pair)
    assert sent, "no party_act calls found in guide.c"
    assert sent <= taken, f"guide.c sends party actions social.c refuses: {sorted(sent - taken)}"
