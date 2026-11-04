"""Runs the C tests of the campaign levels' places (port/linux/src/
campaign_levels.h), the frames of the menus' level pictures that Online
Games shows for co-op games, with the build machine's C compiler. The header
has no SDL or game dependency, so it compiles anywhere."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SCREEN = (ROOT / "port" / "linux" / "game" / "browser_screen.c").read_text(encoding="utf-8")
MAP_ART = (ROOT / "port" / "linux" / "src" / "map_art.c").read_text(encoding="utf-8")


def compiler():
    """Finds a C compiler on the build machine.

    Returns the path of clang, cc or gcc (the first found), or None.
    """
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_campaign_levels(tmp_path):
    """Builds the levels' tests, warnings as errors, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "campaign_levels_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port" / "linux" / "tests" / "campaign_levels_test.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_online_games_pictures_co_op_levels():
    # (the menus' level pictures, else the site's: its maps/<level>.jpg)
    assert 'bitmaps\\\\sp_levels", (short)campaign_level_index(file)' in SCREEN
    assert 'snprintf(path, (size_t)size, "maps/%s", campaign_level_files[level]);' in MAP_ART
