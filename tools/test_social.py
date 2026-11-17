"""Runs the C tests of the Guide's data (port/linux/src/social.c: its rows,
the polling schedule and presence's), with the build machine's C compiler.
The unit has no SDL or game dependency, so it compiles anywhere."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def compiler():
    """Finds a C compiler on the build machine.

    Returns the path of clang, cc or gcc (the first found), or None.
    """
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_social(tmp_path):
    """Builds the social unit (and the activity unit its status lines use, and
    the profile unit that reads the site's friends) with its tests, warnings as errors, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "social_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port" / "linux" / "tests" / "social_test.c"),
         str(ROOT / "port" / "linux" / "src" / "social.c"),
         str(ROOT / "port" / "linux" / "src" / "discord_activity.c"),
         str(ROOT / "port" / "linux" / "src" / "delta_profile.c"),
         "-DHALO_GAME_BROWSER",
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_guide_party_actions_are_sent():
    """Every party action DELTA asks for (guide.c's party_act calls, and
    what debug.party_test may name) is one social_party_fields builds and
    the site takes (social.c's social_party_actions): NEW PARTY CODE's
    "code" once went nowhere, refused before it was sent."""
    import re

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
