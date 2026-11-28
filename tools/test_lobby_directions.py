"""The Xbox menus' lobby line under a team game (multiplayer_game_directions in
source/interface/ui_widget_game_data_input_functions.c): a client alone in a
dedicated server's game, whose host has no players of its own, is told it waits
for another player, not that it must form 2 teams. With a second player the
team lines are as on the Xbox. The function is taken from the game's source and
compiled with stand-ins."""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from harness import enum_with, function, read  # noqa: E402

SOURCE = read("source/interface/ui_widget_game_data_input_functions.c")

HARNESS = r"""
#include <stdio.h>
#include <string.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define NUMBEROF(array) (sizeof(array) / sizeof(array[0]))
#define match_vassert(file, line, condition, text) ((void)0)
enum { _team_red = 0, _team_blue };
enum { _ui_widget_type_text_box = 1 };
@ENUM@
struct network_player { boolean valid; short team_index; };
struct network_game
{
	struct { struct { boolean teams; } universal_variant; } variant;
	short machine_count;
	short player_count;
	struct network_player players[16];
};
struct widget_instance
{
	short type;
	boolean visible;
	struct { struct { short string_list_index; } text_box; } parameters;
};
struct network_game_server { int unused; };
static struct network_game the_game;
static struct network_game_server the_server;
static boolean hosting;
static short seconds_to_start = -1;
static struct network_game *network_game_get_game(void) { return &the_game; }
static struct network_game_server *global_network_game_server_get(void) { return hosting ? &the_server : NULL; }
static void *global_network_game_client_get(void) { return NULL; }
static short network_game_client_get_seconds_to_game_start(void *client) { (void)client; return seconds_to_start; }
static boolean network_game_solo_game(void) { return FALSE; }
static boolean network_game_is_splitscreen_local(void) { return FALSE; }
static boolean network_player_is_valid(struct network_player *player) { return player->valid; }
@FUNCTION@
static void join(int index, short team)
{
	the_game.players[index].valid = TRUE;
	the_game.players[index].team_index = team;
	the_game.player_count++;
}
static short directions(struct widget_instance *widget)
{
	memset(widget, 0, sizeof(*widget));
	widget->type = _ui_widget_type_text_box;
	widget->parameters.text_box.string_list_index = -1;
	multiplayer_game_directions(widget);
	return widget->visible ? widget->parameters.text_box.string_list_index : -1;
}
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
	struct widget_instance widget;

	/* a client alone in a dedicated server's team game */
	the_game.variant.universal_variant.teams = TRUE;
	the_game.machine_count = 2;
	join(0, _team_red);
	CHECK(directions(&widget) == _multiplayer_game_text_string_waiting_for_player);
	/* a second player on the same team: the teams line */
	join(1, _team_red);
	CHECK(directions(&widget) == _multiplayer_game_text_string_waiting_for_teams);
	/* one on each team */
	the_game.players[1].team_index = _team_blue;
	CHECK(directions(&widget) == _multiplayer_game_text_string_teams_ready);
	/* the countdown running: nothing */
	seconds_to_start = 5;
	CHECK(directions(&widget) == -1);
	seconds_to_start = -1;
	/* not a team game: nothing, as on the Xbox */
	memset(&the_game, 0, sizeof(the_game));
	the_game.machine_count = 2;
	join(0, _team_red);
	CHECK(directions(&widget) == -1);
	/* a host alone: waiting for another Xbox, as on the Xbox */
	hosting = TRUE;
	the_game.variant.universal_variant.teams = TRUE;
	the_game.machine_count = 1;
	CHECK(directions(&widget) == _multiplayer_game_text_string_waiting_for_machine);
	/* a host with one client, both players on red: the teams line, as on the Xbox */
	the_game.machine_count = 2;
	join(1, _team_red);
	CHECK(directions(&widget) == _multiplayer_game_text_string_waiting_for_teams);
	printf("ok\n");
	return 0;
}
"""


def test_lobby_directions(tmp_path):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    body = function(SOURCE, "multiplayer_game_directions")
    text = HARNESS.replace("@ENUM@", enum_with(SOURCE, "_multiplayer_game_text_string_waiting_for_player"))
    source = tmp_path / "directions.c"
    source.write_text(text.replace("@FUNCTION@", body))
    binary = tmp_path / "directions"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Wno-unused-function", "-Wno-unused-variable", str(source),
                    "-o", str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0 and "ok" in result.stdout, result.stdout + result.stderr
