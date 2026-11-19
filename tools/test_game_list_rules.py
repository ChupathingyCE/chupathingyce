"""Runs the C tests of which listed games the game lists show (port/linux/
src/game_list_rules.h): only games this build can join. The header has no
SDL or game dependency, so it compiles anywhere. Also checks that both lists
(Online Games and the Server Browser) go through it, and that the switch to
show every game is there, off by default."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
BROWSER_C = (ROOT / "port" / "linux" / "src" / "browser.c").read_text(encoding="utf-8")
MENUS = (ROOT / "port" / "linux" / "game" / "menu_functions.c").read_text(encoding="utf-8")
CONFIG = (ROOT / "port" / "linux" / "src" / "port_config.c").read_text(encoding="utf-8")
README = (ROOT / "port" / "linux" / "README.md").read_text(encoding="utf-8")
RULES_H = (ROOT / "port" / "linux" / "src" / "game_list_rules.h").read_text(encoding="utf-8")


def compiler():
    """Finds a C compiler on the build machine.

    Returns the path of clang, cc or gcc (the first found), or None.
    """
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_game_list_rules(tmp_path):
    """Builds the rules' tests, warnings as errors, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "game_list_rules_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port" / "linux" / "tests" / "game_list_rules_test.c"),
         str(ROOT / "port" / "linux" / "src" / "server_names.c"),
         str(ROOT / "port" / "linux" / "src" / "delta_chat_words.c"),
         "-iquote", str(ROOT / "port" / "linux" / "include"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_online_games_filters_its_games():
    # (browser_get_games keeps only the games shown, before their pings)
    body = BROWSER_C[BROWSER_C.index("int browser_get_games("):]
    body = body[:body.index("\n}\n")]
    assert body.count("count = keep_shown(games, count);") == 2
    assert body.rindex("keep_shown") < body.index("add_known(games, count)")
    # (then without the games of a blocked name, whatever the debug switch says)
    assert body.index("count = keep_named(games, count);") < body.index("add_known(games, count)")
    named = BROWSER_C[BROWSER_C.index("static int keep_named("):]
    named = named[:named.index("\n}\n")]
    assert "browser_name_blocked(" in named and "show_all" not in named
    # (a broker listing by the same rules as the list's games, old builds'
    # too: no network floor; a blocked name still left out)
    direct = BROWSER_C[BROWSER_C.index("static int add_direct("):]
    direct = direct[:direct.index("\n}\n")]
    assert "browser_version_shown(listing->version)" in direct
    assert "browser_name_blocked_text(listing->name_utf8)" in direct
    for source in (BROWSER_C, MENUS, RULES_H):
        assert "BROKER_MINIMUM" not in source and "broker_listed" not in source
        assert "browser_listing_shown" not in source
    keep = BROWSER_C[BROWSER_C.index("static int keep_shown("):]
    assert "game_list_joinable(" in keep[:keep.index("\n}\n")]


def test_server_browser_filters_its_games():
    update = MENUS[MENUS.index("static void lobby_browser_update("):]
    assert re.search(r"lobby_browser_add_listed\(\);\n\tlobby_browser_joinable_games\(\);", update)
    assert "browser_version_shown(lobby_browser.games[index].version)" in MENUS
    assert "browser_name_blocked_text(lobby_browser.games[index].name)" in MENUS


def test_blocked_names_from_the_site():
    # (the site's list read on the browser thread, the built-in copy until then)
    assert 'server_url("/v1/blocked-names"' in BROWSER_C
    assert "update_names();" in BROWSER_C
    assert "delta_chat_blocked_words" in BROWSER_C


def test_show_all_switch_off_by_default():
    assert '{ "debug.show_all_games", _config_boolean, "false", "HALO_SHOW_ALL_GAMES"' in CONFIG
    assert "`debug.show_all_games`" in README
    assert 'config_boolean("debug.show_all_games")' in BROWSER_C
