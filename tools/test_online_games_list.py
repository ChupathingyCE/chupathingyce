"""Online Games (port/linux/game/browser_screen.c) lists the same games as
the Server Browser (menu_functions.c): browser_get_games (port/linux/src/
browser.c) has room for every game the list sends, co-op ones too, and
then for the games heard only on the brokers; and a game with a password,
which only the brokers have, is listed with its lock and asked for its
password, not dropped."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BROWSER_H = (ROOT / "port" / "linux" / "src" / "browser.h").read_text(encoding="utf-8")
BROWSER_C = (ROOT / "port" / "linux" / "src" / "browser.c").read_text(encoding="utf-8")
MENUS = (ROOT / "port" / "linux" / "game" / "menu_functions.c").read_text(encoding="utf-8")
SCREEN = (ROOT / "port" / "linux" / "game" / "browser_screen.c").read_text(encoding="utf-8")


def function(source, name):
    # (its definition, not a declaration ending in ";")
    start = re.search(r"^(?:static )?[a-z][^;\n]*\b" + name + r"\((?![^\n]*;)", source, re.M).start()
    body = source.index("\n{", start)
    end = source.index("\n}\n", body)
    return source[body:end]


def desktop_maximum():
    # (the native builds' cap: the #else of the console's limits)
    match = re.search(r"#ifdef HALO_PORT_CONSOLE_LIMITS\n#define BROWSER_MAXIMUM_GAMES (\d+)\n#else\n"
                      r"#define BROWSER_MAXIMUM_GAMES (\d+)\n#endif", BROWSER_H)
    assert match, "BROWSER_MAXIMUM_GAMES: a console's cap and the native builds'"
    return int(match.group(2))


def test_room_for_the_server_browsers_games():
    lobby = int(re.search(r"#define LOBBY_BROWSER_GAMES (\d+)", MENUS).group(1))
    assert desktop_maximum() >= lobby


def test_answer_holds_every_game():
    assert re.search(r"RESPONSE_SIZE = BROWSER_MAXIMUM_GAMES \* (\d+)", BROWSER_C)
    per_game = int(re.search(r"RESPONSE_SIZE = BROWSER_MAXIMUM_GAMES \* (\d+)", BROWSER_C).group(1))
    # (a line is about 150 bytes; a full roster makes it longer)
    assert per_game >= 512


def test_password_listings_kept():
    body = function(BROWSER_C, "add_direct")
    assert "listing->locked ||" not in body, "a listing with a password is dropped"
    assert "game->locked = listing->locked;" in body


def test_listings_lock_survives_add_known():
    body = function(BROWSER_C, "add_known")
    direct = body.index("if (games[index].direct)")
    assert body.index("games[index].locked = 0;") > direct


def test_online_games_asks_for_the_password():
    body = function(SCREEN, "join_selected")
    assert "if (game->locked)" in body and "password_open(game);" in body
    assert "browser_unlock_game(" in function(SCREEN, "password_join")


def test_no_cutouts_under_another_screen():
    # (#1048: the pictures from the game's bitmaps are drawn under the
    # overlay, in a hole cut in it, and the hole is the whole overlay's: with
    # the Delta Guide, or another screen drawn after this one, open over the browser
    # they showed through it. Neither the hole nor the picture then)
    covered = function(SCREEN, "browser_screen_covered")
    for screen in ("guide_active()", "profile_screen_active()", "controls_screen_active()",
                   "moderation_screen_active()", "invite_prompt_active()"):
        assert screen in covered
    # (the definition, after a forward declaration over two lines)
    start = SCREEN.index("static void draw_bitmap_picture(\n")
    picture = SCREEN[SCREEN.index("\n{", start):SCREEN.index("\n}\n", start)]
    assert picture.index("if (browser_screen_covered())\n\t\treturn;") < picture.index("ui_overlay_cutout(")
    assert picture.index("if (browser_screen_covered())") < picture.index("draw_bitmap_in_rect(")
    # (the browser's only cut-out)
    assert SCREEN.count("ui_overlay_cutout(") == 1
    # (the profile screen's Spartan, the other cut-out, the same way)
    profile = (ROOT / "port" / "linux" / "game" / "profile_screen.c").read_text(encoding="utf-8")
    assert "guide_active()" in function(profile, "profile_screen_covered")
    spartan = function(profile, "draw_spartan")
    assert spartan.index("profile_screen_covered()") < spartan.index("ui_overlay_cutout(")
    assert profile.count("ui_overlay_cutout(") == 1
