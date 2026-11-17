"""A host's MCC map pick whose package check fails (#1132):
port/linux/game/mcc_ui_network.inl's mcc_ui_network_update_map lets the pick
go instead of keeping the lobby waiting for good. The lobby stays on its map
(the menus show it again), the error box is shown once, the refusal is
handed out once (mcc_ui_network_map_refused: a dedicated server skips the
entry), and a check still running keeps the pick pending as before. A
campaign level picked for co-op while its check runs keeps its co-op
settings back (mcc_ui_network_hold_cooperative): they go on with the map
when the check passes, and a failed check leaves the lobby on its map and
gametype (#1124). The functions are compiled from the game's source with
stand-ins."""
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from harness import function, mutated, read  # noqa: E402

SOURCE = read("port/linux/game/mcc_ui_network.inl")
STATE = SOURCE[SOURCE.index("static struct network_game_server *mcc_restart_server;"):
               SOURCE.index("boolean mcc_ui_network_defer_map(")]
FUNCTIONS = ("mcc_ui_network_defer_map", "mcc_ui_network_hold_cooperative", "mcc_ui_network_map_pending", "mcc_ui_network_update_map",
             "mcc_ui_network_map_refused", "mcc_ui_network_server_dispose")

HARNESS = r"""
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
enum { _network_game_server_state_pregame = 1, _network_game_server_state_ingame };
struct network_game { struct { char name[64]; long version; } map; };
struct network_game_server { int state; int sent_start_game_message; struct network_game game; };
static struct network_game_server the_server, *current;
static struct network_game_server *global_network_game_server_get(void) { return current; }
static void *global_network_game_client_get(void) { return NULL; }
static long network_game_client_get_local_machine_index(void) { return 0; }
static int mcc_level_name(char const *name) { return name && !strncmp(name, "mcc_maps\\", 9); }
/* the check: its answer for the map asked about (1 fine, 0 running, -1 failed) */
static int check_status = 0, cancels, updates, identity_prepared, host_cleared;
static int mcc_validation_request(char const *name, uint32_t *checksum, unsigned char digest[32])
{ (void)name; (void)digest; if (checksum) *checksum = 0x1234; return check_status; }
static int mcc_validation_lookup(char const *name, uint32_t *checksum, unsigned char digest[32])
{ (void)name; (void)digest; if (checksum) *checksum = 0x1234; return check_status == 1; }
static void mcc_validation_update(void) { updates++; }
static void mcc_validation_cancel(void) { cancels++; }
static char const *mcc_validation_error(void) { return "The package is damaged."; }
static int mcc_identity_prepare_host(char const *name) { (void)name; identity_prepared++; return 1; }
static void mcc_identity_host_clear(void) { host_cleared++; }
static int errors_shown;
static void display_error_text_when_main_menu_loaded(wchar_t const *text) { (void)text; errors_shown++; }
static int events;
static void network_event(char const *format, ...) { (void)format; events++; }
static char menu_map[64], override_map[64];
static void main_set_multiplayer_map_name(char const *name) { snprintf(menu_map, sizeof(menu_map), "%s", name); }
static void game_engine_override_map_name(char const *name) { snprintf(override_map, sizeof(override_map), "%s", name); }
static int map_changes;
static boolean network_game_server_change_map_name(struct network_game_server *server, char const *name)
{ map_changes++; snprintf(server->game.map.name, sizeof(server->game.map.name), "%s", name); server->game.map.version = 0x1234; return TRUE; }
/* (network_server_manager.c's: the co-op settings, and the MCC question) */
static boolean network_game_server_mcc_asking;
static short network_game_server_mcc_choosing_difficulty = NONE;
static int cooperative_applied, cooperative_difficulty = NONE;
static void network_game_server_port_cooperative_apply(struct network_game_server *server, short difficulty)
{ (void)server; cooperative_applied++; cooperative_difficulty = difficulty; }
/* (the rest of the file's statics and functions: restart, unused here) */
struct network_player { int x; };
@STATE@
@FUNCTIONS@
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static void reset(void)
{
	memset(&the_server, 0, sizeof(the_server));
	the_server.state = _network_game_server_state_pregame;
	strcpy(the_server.game.map.name, "bloodgulch");
	the_server.game.map.version = 7;
	current = &the_server;
	strcpy(menu_map, "mcc_maps\\123\\a10");
	override_map[0] = 0;
	check_status = 0;
	errors_shown = events = map_changes = cancels = updates = identity_prepared = cooperative_applied = 0;
	cooperative_difficulty = NONE;
	mcc_ui_network_server_dispose(&the_server);
}
int main(void)
{
	char map[64], reason[320];

	/* the pick waits while the check runs */
	reset();
	CHECK(mcc_ui_network_defer_map(&the_server, "mcc_maps\\123\\a10"));
	CHECK(mcc_ui_network_map_pending(&the_server));
	mcc_ui_network_update_map(&the_server);
	CHECK(mcc_ui_network_map_pending(&the_server) && updates == 1 && !map_changes);
	CHECK(!mcc_ui_network_map_refused(map, sizeof(map), reason, sizeof(reason)));
	/* the check fails: the pick let go, the lobby on its map, said once */
	check_status = -1;
	mcc_ui_network_update_map(&the_server);
	CHECK(!mcc_ui_network_map_pending(&the_server));
	CHECK(errors_shown == 1 && events == 2 && !map_changes);  /* (defer_map said the wait, then the failure) */
	CHECK(!strcmp(the_server.game.map.name, "bloodgulch") && !strcmp(menu_map, "bloodgulch") && !strcmp(override_map, "bloodgulch"));
	CHECK(mcc_ui_network_map_refused(map, sizeof(map), reason, sizeof(reason)));
	CHECK(!strcmp(map, "mcc_maps\\123\\a10") && !strcmp(reason, "The package is damaged."));
	/* (once) */
	CHECK(!mcc_ui_network_map_refused(map, sizeof(map), reason, sizeof(reason)));
	mcc_ui_network_update_map(&the_server);
	CHECK(errors_shown == 1 && events == 2 && !mcc_ui_network_map_refused(map, sizeof(map), reason, sizeof(reason)));
	/* a check that passes: the map changes, nothing refused */
	reset();
	check_status = 1;
	CHECK(!mcc_ui_network_defer_map(&the_server, "mcc_maps\\123\\a10"));
	the_server.game.map.version = 0;
	strcpy(the_server.game.map.name, "mcc_maps\\123\\a10");
	CHECK(mcc_ui_network_map_pending(&the_server));
	mcc_ui_network_update_map(&the_server);
	CHECK(map_changes == 1 && !errors_shown && identity_prepared == 1);
	CHECK(!mcc_ui_network_map_refused(map, sizeof(map), reason, sizeof(reason)));
	/* a co-op pick whose check fails: the settings never go on, so the lobby
	keeps its map and its gametype (#1124) */
	reset();
	CHECK(mcc_ui_network_defer_map(&the_server, "mcc_maps\\123\\a10"));
	CHECK(mcc_ui_network_hold_cooperative(&the_server, 2));
	CHECK(!cooperative_applied);
	check_status = -1;
	mcc_ui_network_update_map(&the_server);
	CHECK(!cooperative_applied && !map_changes && !strcmp(the_server.game.map.name, "bloodgulch"));
	/* (a later pick of the same map passes without co-op) */
	CHECK(!mcc_ui_network_hold_cooperative(&the_server, 2));
	/* a co-op pick whose check passes: the settings go on with the map */
	reset();
	CHECK(mcc_ui_network_defer_map(&the_server, "mcc_maps\\123\\a10"));
	CHECK(mcc_ui_network_hold_cooperative(&the_server, 3));
	check_status = 1;
	mcc_ui_network_update_map(&the_server);
	CHECK(map_changes == 1 && cooperative_applied == 1 && cooperative_difficulty == 3);
	CHECK(network_game_server_mcc_choosing_difficulty == NONE);
	/* a new pick drops a held co-op pick */
	reset();
	check_status = 0;
	CHECK(mcc_ui_network_defer_map(&the_server, "mcc_maps\\123\\a10"));
	CHECK(mcc_ui_network_hold_cooperative(&the_server, 3));
	CHECK(mcc_ui_network_defer_map(&the_server, "mcc_maps\\123\\a30"));
	check_status = 1;
	mcc_ui_network_update_map(&the_server);
	CHECK(map_changes == 1 && !cooperative_applied);
	/* no pick waiting: nothing held, the caller applies at once */
	reset();
	CHECK(!mcc_ui_network_hold_cooperative(&the_server, 1));
	/* a refusal not asked for is gone with the server */
	reset();
	CHECK(mcc_ui_network_defer_map(&the_server, "mcc_maps\\123\\a10"));
	check_status = -1;
	mcc_ui_network_update_map(&the_server);
	mcc_ui_network_server_dispose(&the_server);
	CHECK(!mcc_ui_network_map_refused(map, sizeof(map), reason, sizeof(reason)));
	printf("ok\n");
	return 0;
}
"""


def compile_and_run(tmp_path, functions):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    functions = re.sub(r"^(void|boolean) ", r"static \1 ", functions, flags=re.M)
    source = tmp_path / "mcc_check.c"
    source.write_text(HARNESS.replace("@STATE@", STATE).replace("@FUNCTIONS@", functions))
    binary = tmp_path / "mcc_check"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Wno-unused-function", "-Wno-unused-variable", str(source),
                    "-o", str(binary)], check=True)
    return subprocess.run([str(binary)], capture_output=True, text=True)


def test_failed_check_lets_the_pick_go(tmp_path):
    result = compile_and_run(tmp_path, "\n".join(function(SOURCE, name) for name in FUNCTIONS))
    assert result.returncode == 0 and "ok" in result.stdout, result.stdout + result.stderr


def test_negative_control(tmp_path):
    """the pick kept pending after a failed check (the bug): caught"""
    functions = "\n".join(function(SOURCE, name) for name in FUNCTIONS)
    functions = mutated(functions, "        mcc_pending_map_server=NULL;\n        mcc_validation_server=NULL;\n"
                        "        /* (a co-op pick's settings never went on: the gametype stays too) */",
                        "        /* (a co-op pick's settings never went on: the gametype stays too) */")
    result = compile_and_run(tmp_path, functions)
    assert result.returncode != 0


def test_negative_control_cooperative(tmp_path):
    """co-op settings put on while the pick waits (the bug): caught"""
    functions = "\n".join(function(SOURCE, name) for name in FUNCTIONS)
    functions = mutated(functions, "    if (!server || server != mcc_pending_map_server) return FALSE;",
                        "    return FALSE;")
    result = compile_and_run(tmp_path, functions)
    assert result.returncode != 0


def test_cooperative_level_holds_while_the_pick_waits():
    """network_server_manager.c asks before it applies, on a co-op pick and
    on the host's yes to the MCC question"""
    manager = read("source/networking/network_server_manager.c")
    level = function(manager, "network_game_server_port_change_cooperative_level")
    assert level.index("if (network_game_server_mcc_asking)") < \
        level.index("if (mcc_ui_network_hold_cooperative(server, difficulty))") < \
        level.index("network_game_server_port_cooperative_apply(server, difficulty);")
    answer = function(manager, "network_game_server_mcc_confirm_answer")
    assert answer.index("!mcc_ui_network_hold_cooperative(server, difficulty)") < \
        answer.index("network_game_server_port_cooperative_apply(server, difficulty);")
