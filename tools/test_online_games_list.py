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


def test_filter_words_never_say_all():
    """(#1150) Online Games opened on CHUPATHINGY + PUBLIC while its footer
    said "FILTER: ALL": the filter is co-op's, not the views', so its button
    says what it does with co-op games, in Online Games and in Join Game"""
    labels = re.search(r"labels\[BROWSER_LIST_FILTERS\] = \{([^}]*)\}", function(BROWSER_C, "browser_list_filter_label"))
    words = re.findall(r'"([^"]*)"', labels.group(1))
    assert words == ["CO-OP: SHOWN", "CO-OP: HIDDEN", "CO-OP: ONLY"]
    assert not any("ALL" in word for word in words)
    assert 'snprintf(filter_word, sizeof(filter_word), "=%s", browser_list_filter_label());' in SCREEN
    assert "browser_list_filter_label()" in function(MENUS, "browser_update")
    assert "FILTER: %s" not in SCREEN and "FILTER: %s" not in MENUS


def test_tabs_fit_and_read_over_the_background():
    """(#1150) the OPENCE tab's name was lost over the menus' moving
    background: each tab not shown has a dark backing, and the tabs fit
    between LB and RB"""
    tabs = function(SCREEN, "draw_tabs")
    assert "if (total > right - left && total > 0.0f)" in tabs
    shown = tabs.index("if (view == browser_screen.view)")
    other = tabs.index("ui_overlay_rect(x, TABS_Y, width, TABS_HEIGHT, 6,", shown)
    assert other < tabs.index("ui_overlay_text(UI_FONT_MENU, size, x + width * 0.5f", other)


def test_selection_stays_on_its_game():
    """(#1150) a refresh keeps the selection on its game as the order
    changes: by its invite, or by its host's key hash (the invite's first
    half) when its token is new"""
    fetch = function(SCREEN, "fetch_games")
    assert "!strcmp(browser_screen.games[index].invite, invite)" in fetch
    assert "same_key_hash(browser_screen.games[index].invite, invite)" in fetch
    # (the exact invite before the same host's other listing)
    assert fetch.index("if (index < browser_screen.count)") < fetch.index("else if (same_host != NONE)")
    same = function(SCREEN, "same_key_hash")
    assert "BROWSER_INVITE_LENGTH / 2" in same
