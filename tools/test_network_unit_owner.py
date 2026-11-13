"""A unit the host's word gives one player that another player here still
has (network_distributed.c, distributed_handle_unit_state): the other
player must let it go.

The attach (network_player_attach_unit) points the unit at its new player
and leaves the old one holding it; when the unit was deleted, only the new
player was detached from it, and the old one's handle went stale (a client's
HUD then read the unit and faulted). A host decides which unit is whose, so
any host could do this to its clients.

The real distributed_unit_release_others (port/linux/game/network_distributed.c)
and the real network_player_attach_unit and network_player_detach_unit
(source/game/players.c) are compiled on their own, over fake players and
units. Needs only a C compiler.
"""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
DISTRIBUTED_C = ROOT / "port/linux/game/network_distributed.c"
PLAYERS_C = ROOT / "source/game/players.c"


def function(source, signature):
    """The definition that starts with signature, to its closing brace."""
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end]


PRELUDE = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define NONE (-1L)
#define csmemset memset
enum { _object_mask_unit = 3, _player_action_result_reload = 1 };
struct object_data { short owner_team_index; long owner_player_index; };
struct unit_part { long player_index; };
struct unit_datum { struct object_data object; struct unit_part unit; int exists; };
struct player_datum { long unit_index; long team_index; short local_player_index; short powerup_durations[4];
	short action_result; long action_object_index; };
struct data_iterator { int next; long datum_index; };
static struct player_datum players[3];
static struct unit_datum units[4];
static void *player_data = players;
static struct player_datum *player_get(long index) { return &players[index]; }
static struct unit_datum *unit_get(long index)
{
	if (index < 0 || index >= 4 || !units[index].exists)
	{
		printf("unit #%ld read after it was gone\n", index);
		exit(1);
	}
	return &units[index];
}
static void *object_try_and_get_and_verify_type(long index, int mask)
{
	(void)mask;
	return index >= 0 && index < 4 && units[index].exists ? &units[index] : NULL;
}
static void data_iterator_new(struct data_iterator *iterator, void *data) { (void)data; iterator->next = 0; }
static void *data_iterator_next(struct data_iterator *iterator)
{
	if (iterator->next >= 3)
		return NULL;
	iterator->datum_index = iterator->next;
	return &players[iterator->next++];
}
static boolean game_engine_has_teams(void) { return FALSE; }
static void unit_set_actively_controlled(long unit, boolean on) { (void)unit; (void)on; }
static void player_control_new_unit(short local, long unit) { (void)local; (void)unit; }
static void observer_obsolete_position(short local) { (void)local; }
static void network_player_detach_unit(long player_index);
"""

MAIN = r"""
int main(void)
{
	long index;

	for (index = 0; index < 3; index++)
	{
		players[index].unit_index = NONE;
		players[index].local_player_index = NONE;
	}
	players[0].local_player_index = 0;
	units[1].exists = 1;
	units[1].unit.player_index = NONE;
	/* this machine's player (0) has unit 1 */
	network_player_attach_unit(0, 1);
	/* the host says unit 1 is player 2's (handle_unit_state's order) */
	distributed_unit_release_others(2, 1);
	network_player_attach_unit(2, 1);
	/* the unit is deleted: its player, 2, detached (distributed_client_delete) */
	network_player_detach_unit(units[1].unit.player_index);
	units[1].exists = 0;
	/* every player's unit read as the HUD reads it */
	for (index = 0; index < 3; index++)
	{
		if (players[index].unit_index != NONE)
			unit_get(players[index].unit_index);
	}
	printf("unit_owner_test: ok\n");
	return 0;
}
"""


def test_a_unit_given_to_another_player_is_let_go(tmp_path):
    cc = shutil.which("clang") or shutil.which("cc")
    if not cc:
        pytest.skip("no C compiler")
    distributed = DISTRIBUTED_C.read_text(encoding="utf-8")
    players = PLAYERS_C.read_text(encoding="utf-8")
    source = (PRELUDE +
              function(distributed, "static void distributed_unit_release_others(\n") +
              function(players, "void network_player_attach_unit(\n") +
              function(players, "void network_player_detach_unit(\n").replace("void network_player_detach_unit(",
                                                                          "static void network_player_detach_unit(") +
              MAIN)
    path = tmp_path / "unit_owner_test.c"
    path.write_text(source, encoding="utf-8")
    binary = tmp_path / "unit_owner_test"
    build = subprocess.run([cc, "-std=c11", "-w", str(path), "-o", str(binary)], capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run.returncode == 0 and "unit_owner_test: ok" in run.stdout, run.stdout + run.stderr
