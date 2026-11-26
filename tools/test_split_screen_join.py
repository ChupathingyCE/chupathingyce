"""Split screen players joining an online game already under way (#1788).

The preview of a game in progress (port/linux/game/lobby_screen.c) lets
more players of this machine join with player 1 (ADD PLAYERS). Checked here:

- the room check: the players joining from here are compared with the spots
  the game has left; when they don't fit, JOIN THE GAME is off and the
  preview says how many spots are left ("Only 2 spots left in this game"),
  and SELECT PROFILE's last A waits with the same words;
- the main menu under Online Games (an invite, Discord's Join or a party
  opens it there) leaves the joins and the game being joined alone while
  SELECT PROFILE is up over it, as the Multiplayer menu does
  (source/interface/ui_widget_event_handler_functions.c's
  main_menu_initialize), and clears them as before otherwise.

The functions are taken from the game's source and compiled with stand-ins.
"""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
LOBBY = (ROOT / "port" / "linux" / "game" / "lobby_screen.c").read_text(encoding="utf-8")
HANDLERS = (ROOT / "source" / "interface" / "ui_widget_event_handler_functions.c").read_text(encoding="utf-8")


def function(source, name):
    match = re.search(r"^(?:static )?[a-z][^;\n]*\b" + name + r"\((?:[^;{]*?)\)\s*\n\{", source, re.M | re.S)
    assert match, name
    return source[match.start():source.index("\n}\n", match.end()) + 3]


ROOM = r"""
#include <stdio.h>
#include <string.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define LOCAL_PLAYERS 4
static struct
{
	struct { short players; short maximum_players; } preview_game;
} lobby;
static short joining;
static short joining_count(void) { return joining; }
@FUNCTIONS@
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
	char text[64];

	/* 14 of 16: two spots, room for two */
	lobby.preview_game.players = 14;
	lobby.preview_game.maximum_players = 16;
	joining = 2;
	CHECK(preview_spots() == 2 && preview_has_room());
	/* three won't fit */
	joining = 3;
	CHECK(!preview_has_room());
	room_text(text, sizeof(text), " ");
	CHECK(!strcmp(text, "Only 2 spots left in this game"));
	room_text(text, sizeof(text), "\r\n");
	CHECK(!strcmp(text, "Only 2 spots left\r\nin this game"));
	/* one spot */
	lobby.preview_game.players = 15;
	room_text(text, sizeof(text), " ");
	CHECK(!strcmp(text, "Only 1 spot left in this game"));
	joining = 1;
	CHECK(preview_has_room());
	/* full, or more players than it takes (a host that shrank its game) */
	lobby.preview_game.players = 17;
	CHECK(preview_spots() == 0 && !preview_has_room());
	room_text(text, sizeof(text), " ");
	CHECK(!strcmp(text, "No spots left in this game"));
	/* a game that doesn't say how many it takes: no check */
	lobby.preview_game.maximum_players = 0;
	joining = 4;
	CHECK(preview_has_room());
	printf("ok\n");
	return 0;
}
"""

MAIN_MENU = r"""
#include <stdio.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define HALO_GAME_BROWSER
struct widget_instance;
struct event_record;
static boolean adding;
static int joins_cleared, variant_cleared, client_disposed, server_disposed, remote_off, controllers_reset;
static short player_spawn_count;
static boolean lobby_screen_adding_players(void) { return adding; }
static void player_ui_clear_multiplayer_joins(void) { joins_cleared++; }
static void player_ui_clear_multiplayer_variant(void) { variant_cleared++; }
static void dispose_global_network_game_client(void) { client_disposed++; }
static void dispose_global_network_game_server(void) { server_disposed++; }
static void network_game_accept_remote_connections(boolean accept) { (void)accept; remote_off++; }
static void player_ui_reset_single_player_local_player_controllers(void) { controllers_reset++; }
static void player_ui_end_editing_profile(void) {}
static boolean ui_main_menu_music_active(void) { return TRUE; }
static void ui_start_main_menu_music(void) {}
@FUNCTIONS@
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
	boolean deleted = FALSE;

	/* back from SELECT PROFILE over the preview: the joins and the client stay */
	adding = TRUE;
	player_spawn_count = 2;
	main_menu_initialize(NULL, NULL, &deleted);
	CHECK(!joins_cleared && !variant_cleared && !client_disposed && !server_disposed && !remote_off);
	CHECK(player_spawn_count == 1 && controllers_reset == 1);
	/* the main menu any other time: everything cleared, as before */
	adding = FALSE;
	main_menu_initialize(NULL, NULL, &deleted);
	CHECK(joins_cleared == 1 && variant_cleared == 1 && client_disposed == 1 && server_disposed == 1 && remote_off == 1);
	printf("ok\n");
	return 0;
}
"""


def compile_and_run(tmp_path, name, source):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    path = tmp_path / (name + ".c")
    path.write_text(source)
    binary = tmp_path / name
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Wno-unused-function", "-Wno-unused-variable", str(path),
                    "-o", str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0 and "ok" in result.stdout, result.stdout + result.stderr


def test_room_check(tmp_path):
    functions = "\n".join(function(LOBBY, name) for name in ("preview_spots", "preview_has_room", "room_text"))
    compile_and_run(tmp_path, "room", ROOM.replace("@FUNCTIONS@", functions))


def test_room_check_used():
    menu = function(LOBBY, "read_menu")
    assert "lobby.preview_game.open && preview_has_room()" in menu
    status = function(LOBBY, "read_status")
    assert 'room_text(lobby.status, sizeof(lobby.status), " ");' in status
    finish = function(LOBBY, "lobby_screen_add_players_finish")
    assert "if (!preview_has_room())" in finish
    assert "display_error_text_deferred(wide, controller);" in finish
    # (the dialog keeps the pointer: the text must outlive the call)
    assert "static wchar_t wide[64];" in finish


def test_main_menu_keeps_the_join(tmp_path):
    compile_and_run(tmp_path, "main_menu", MAIN_MENU.replace("@FUNCTIONS@", function(HANDLERS, "main_menu_initialize")))


def test_player_one_port_given_back():
    update = function(LOBBY, "lobby_screen_update")
    assert "first_port_reset();" in update
    assert "first_port_reset();" in function(LOBBY, "leave")
    assert "platform_gamepad_first_reset();" in function(LOBBY, "first_port_reset")


WITHIN = r"""
#include <stdio.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
struct widget_instance { struct widget_instance *parent; };
@FUNCTIONS@
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
	struct widget_instance screen = { NULL }, quarter = { &screen }, list = { &quarter }, item = { &list },
		other = { &screen };

	CHECK(widget_within(&quarter, &quarter));
	CHECK(widget_within(&list, &quarter) && widget_within(&item, &quarter));
	CHECK(!widget_within(&screen, &quarter) && !widget_within(&other, &quarter));
	CHECK(!widget_within(NULL, &quarter));
	printf("ok\n");
	return 0;
}
"""


def test_b_in_a_quarter_never_uses_the_deleted_widget(tmp_path):
    # B empties a quarter by replacing it: the widget the B came to is gone
    # with it when it is the quarter or inside it (its list), and the event
    # code must not touch it again
    compile_and_run(tmp_path, "within", WITHIN.replace("@FUNCTIONS@", function(LOBBY, "widget_within")))
    back = function(LOBBY, "lobby_screen_add_players_back")
    assert back.index("*widget_gone = widget_within(widget, quarter);") < back.index(
        "ui_widget_port_replace(quarter, empty)")
    widget_source = (ROOT / "source" / "interface" / "ui_widget.c").read_text(encoding="utf-8")
    hook = widget_source[widget_source.index("event->controller_index, widget, &widget_gone))"):]
    hook = hook[:hook.index("#endif")]
    assert "if (widget_gone)\n\t\t\t\t\t\twidget_deleted = TRUE;" in hook
