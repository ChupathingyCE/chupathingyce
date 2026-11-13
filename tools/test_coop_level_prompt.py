"""A campaign level picked for co-op in a lobby whose move to or from an MCC
map asks the host first (network_server_manager.c's
network_game_server_port_change_cooperative_level, #1124): nothing changes
until the host says yes. A no leaves the lobby on its map and gametype, with
its difficulty; a yes moves it and makes it co-op. Without a question (no
one would leave), the level and its co-op settings go on at once, unless the
MCC map waits for its package check: then they wait with it
(mcc_ui_network_hold_cooperative, tools/test_mcc_check_failed.py). The
functions are taken from the game's source and compiled with stand-ins."""
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from harness import function, mutated, read  # noqa: E402

SOURCE = read("source/networking/network_server_manager.c")
STATE = SOURCE[SOURCE.index("/* (the host's answer: yes, the map it picked) */"):
               SOURCE.index("static short network_game_server_mcc_choosing_difficulty = NONE;") +
               len("static short network_game_server_mcc_choosing_difficulty = NONE;")]
FUNCTIONS = ("network_game_server_mcc_confirm_answer", "network_game_server_mcc_confirm",
             "network_game_server_port_cooperative_apply", "network_game_server_port_change_cooperative_level")

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include <wchar.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define NUMBEROF(x) (sizeof(x) / sizeof((x)[0]))
#define NETWORK_GAME_MAP_NAME_LENGTH 32
#define NUMBER_OF_GAME_DIFFICULTY_LEVELS 4
#define csstrncpy strncpy
#define csmemset memset
#define ustrncpy wcsncpy
enum { _network_game_server_state_pregame = 1, _network_game_server_state_ingame };
struct game_variant { wchar_t human_readable_game_description[24]; };
struct network_game_server { int state; struct { struct { char name[NETWORK_GAME_MAP_NAME_LENGTH]; } map; } game; };
static struct network_game_server the_server;
static struct network_game_server *global_network_game_server_get(void) { return &the_server; }
static void network_event(char const *format, ...) { (void)format; }
static long would_drop;
static long network_game_server_mcc_would_drop(struct network_game_server *server, char const *map_name)
{ (void)server; (void)map_name; return would_drop; }
static int mcc_level_name(char const *name) { return !strncmp(name, "mcc_maps\\", 9); }
static char menu_map[64];
static void main_set_multiplayer_map_name(char const *name) { snprintf(menu_map, sizeof(menu_map), "%s", name); }
static short menu_difficulty = NONE, cooperative_difficulty = NONE, variants;
static void main_set_difficulty(short difficulty) { menu_difficulty = difficulty; }
static void network_game_server_port_set_cooperative(struct network_game_server *server, short difficulty)
{ (void)server; cooperative_difficulty = difficulty; }
static boolean network_game_server_change_game_variant(struct network_game_server *server, struct game_variant const *variant)
{ (void)server; variants += variant->human_readable_game_description[0] == L'C'; return TRUE; }
static void (*asked)(boolean yes);
static int asks;
static void invite_prompt_ask(char const *title, char const *question, char const *detail, void (*answer_function)(boolean yes))
{ (void)title; (void)question; (void)detail; asked = answer_function; asks++; }
static void invite_prompt_ask_cancel(void (*answer_function)(boolean yes)) { if (asked == answer_function) asked = NULL; }
/* (the MCC package check: a pick that waits for it keeps its co-op
settings back, mcc_ui_network.inl's) */
static int check_waits;
static struct network_game_server *waiting;
static short held = NONE;
static boolean mcc_ui_network_hold_cooperative(struct network_game_server *server, short difficulty)
{ if (!server || server != waiting) return FALSE; held = difficulty; return TRUE; }
@STATE@
static boolean network_game_server_mcc_confirm(struct network_game_server *server, char const *map_name);
static void network_game_server_port_cooperative_apply(struct network_game_server *server, short difficulty);
/* (the map change as the game's: asks, and keeps the map until yes) */
static boolean network_game_server_change_map_name(struct network_game_server *server, char const *map_name)
{
	if (network_game_server_mcc_confirm(server, map_name))
	{
		main_set_multiplayer_map_name(server->game.map.name);
		return TRUE;
	}
	if (check_waits)
	{
		waiting = server;
		return TRUE;
	}
	snprintf(server->game.map.name, sizeof(server->game.map.name), "%s", map_name);
	return TRUE;
}
@FUNCTIONS@
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static void reset(void)
{
	memset(&the_server, 0, sizeof(the_server));
	the_server.state = _network_game_server_state_pregame;
	strcpy(the_server.game.map.name, "bloodgulch");
	strcpy(menu_map, "bloodgulch");
	menu_difficulty = cooperative_difficulty = NONE;
	variants = asks = 0;
	asked = NULL;
	would_drop = 0;
	check_waits = 0;
	waiting = NULL;
	held = NONE;
}
int main(void)
{
	/* nobody would leave: the level and co-op at once */
	reset();
	CHECK(network_game_server_port_change_cooperative_level(&the_server, "mcc_maps\\123\\a10", 2));
	CHECK(!asks && !strcmp(the_server.game.map.name, "mcc_maps\\123\\a10") && !strcmp(menu_map, "mcc_maps\\123\\a10"));
	CHECK(menu_difficulty == 2 && cooperative_difficulty == 2 && variants == 1);
	/* a player would leave: asked; nothing changes */
	reset();
	would_drop = 1;
	CHECK(network_game_server_port_change_cooperative_level(&the_server, "mcc_maps\\123\\a10", 1));
	CHECK(asks == 1 && asked && !strcmp(the_server.game.map.name, "bloodgulch") && !strcmp(menu_map, "bloodgulch"));
	CHECK(menu_difficulty == NONE && cooperative_difficulty == NONE && variants == 0);
	/* no: the lobby stays as it was */
	asked(FALSE);
	CHECK(!strcmp(the_server.game.map.name, "bloodgulch") && menu_difficulty == NONE && cooperative_difficulty == NONE && variants == 0);
	/* asked again, yes: the level and co-op together */
	CHECK(network_game_server_port_change_cooperative_level(&the_server, "mcc_maps\\123\\a10", 1));
	CHECK(asks == 2 && asked);
	asked(TRUE);
	CHECK(!strcmp(the_server.game.map.name, "mcc_maps\\123\\a10") && !strcmp(menu_map, "mcc_maps\\123\\a10"));
	CHECK(menu_difficulty == 1 && cooperative_difficulty == 1 && variants == 1);
	/* a multiplayer map asked about (the question carries no difficulty): yes moves the map alone */
	reset();
	would_drop = 2;
	CHECK(network_game_server_change_map_name(&the_server, "mcc_maps\\123\\mp"));
	CHECK(asks == 1 && asked);
	asked(TRUE);
	CHECK(!strcmp(the_server.game.map.name, "mcc_maps\\123\\mp") && menu_difficulty == NONE && cooperative_difficulty == NONE && variants == 0);
	/* the question taken back (the host left its lobby): a late yes changes nothing */
	reset();
	would_drop = 1;
	CHECK(network_game_server_port_change_cooperative_level(&the_server, "mcc_maps\\123\\a10", 3));
	{
		void (*late)(boolean yes) = asked;

		invite_prompt_ask_cancel(network_game_server_mcc_confirm_answer);
		network_game_server_mcc_session++;
		network_game_server_mcc_confirm_map[0] = 0;
		network_game_server_mcc_asking = FALSE;
		network_game_server_mcc_confirm_difficulty = NONE;
		late(TRUE);
	}
	CHECK(!strcmp(the_server.game.map.name, "bloodgulch") && menu_difficulty == NONE && cooperative_difficulty == NONE && variants == 0);
	/* the MCC map waits for its package check: the co-op settings wait
	with it, so a failed check leaves the gametype as it was (#1124) */
	reset();
	check_waits = 1;
	CHECK(network_game_server_port_change_cooperative_level(&the_server, "mcc_maps\\123\\a10", 2));
	CHECK(!strcmp(the_server.game.map.name, "bloodgulch") && held == 2);
	CHECK(cooperative_difficulty == NONE && variants == 0);
	/* (and after the host's yes, the same) */
	reset();
	would_drop = 1;
	check_waits = 1;
	CHECK(network_game_server_port_change_cooperative_level(&the_server, "mcc_maps\\123\\a10", 3));
	asked(TRUE);
	CHECK(held == 3 && cooperative_difficulty == NONE && variants == 0);
	/* a bad difficulty or no server: refused */
	reset();
	CHECK(!network_game_server_port_change_cooperative_level(&the_server, "mcc_maps\\123\\a10", NUMBER_OF_GAME_DIFFICULTY_LEVELS));
	CHECK(!network_game_server_port_change_cooperative_level(NULL, "mcc_maps\\123\\a10", 0));
	printf("ok\n");
	return 0;
}
"""


def compile_and_run(tmp_path, functions):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    functions = re.sub(r"^(void|boolean) ", r"static \1 ", functions, flags=re.M)
    source = tmp_path / "coop_prompt.c"
    source.write_text(HARNESS.replace("@STATE@", STATE).replace("@FUNCTIONS@", functions))
    binary = tmp_path / "coop_prompt"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Wno-unused-function", "-Wno-unused-variable", str(source),
                    "-o", str(binary)], check=True)
    return subprocess.run([str(binary)], capture_output=True, text=True)


def test_coop_level_waits_for_the_answer(tmp_path):
    result = compile_and_run(tmp_path, "\n".join(function(SOURCE, name) for name in FUNCTIONS))
    assert result.returncode == 0 and "ok" in result.stdout, result.stdout + result.stderr


def test_negative_control(tmp_path):
    """the co-op settings applied while the question is out (the bug): caught"""
    functions = "\n".join(function(SOURCE, name) for name in FUNCTIONS)
    functions = mutated(functions, "\tif (network_game_server_mcc_asking)\n\t\treturn TRUE;\n", "")
    result = compile_and_run(tmp_path, functions)
    assert result.returncode != 0


def test_negative_control_package_check(tmp_path):
    """the co-op settings applied while the package check runs (the bug): caught"""
    functions = "\n".join(function(SOURCE, name) for name in FUNCTIONS)
    functions = mutated(functions, "\tif (mcc_ui_network_hold_cooperative(server, difficulty))\n\t\treturn TRUE;\n", "")
    result = compile_and_run(tmp_path, functions)
    assert result.returncode != 0
