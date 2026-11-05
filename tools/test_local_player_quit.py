"""A split screen player who quits a network game, whose slot another
machine's player then takes (network_game_spawn_player deletes the quitter's
datum, player_delete): the local players' table must forget him.

Before the fix the table kept his handle. The next input update
(player_control_update) asked the player datum of it and halted ("players
index #1 (0xec710001) is unused or changed", an original Xbox client with four
players, 2026-11-05), and a player joining again for that controller would
have too (local_player_set_player_index).

The real player_delete, local_player_get_player_index and
local_player_set_player_index (source/game/players.c) are compiled on their
own, over a player array that halts as datum_get does on a stale handle. The
negative control takes the fix out of player_delete and must fail.

Needs only a C compiler. Usage: test_local_player_quit.py
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
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
typedef unsigned char boolean;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define UNSIGNED_SHORT_MAX 0xFFFF
#define DATUM_INDEX_TO_ABSOLUTE_INDEX(datum_index) ((datum_index)&UNSIGNED_SHORT_MAX)
#define MAXIMUM_LOCAL_PLAYERS 4
#define MAXIMUM_NUMBER_OF_LOCAL_PLAYERS 4
#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(3); } } while (0)
#define match_assert(file, line, condition) CHECK(condition)

enum { SLOTS = 16 };
struct player_datum { unsigned short identifier; short local_player_index; };
struct players_globals { long local_players[MAXIMUM_LOCAL_PLAYERS]; };
struct player_control { long unit_index; };

static struct players_globals globals, *players_globals = &globals;
static struct player_datum players[SLOTS];
static unsigned short next_identifier = 0xec70;
static void *player_data = players;
static struct player_control controls[MAXIMUM_LOCAL_PLAYERS];

/* datum_get's check (data.c): a handle whose slot is unused or of another
identifier halts */
static struct player_datum *player_get(long player_index)
{
	struct player_datum *player = &players[DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index)];

	if (!player->identifier || player->identifier != (unsigned short)((unsigned long)player_index >> 16))
	{
		fprintf(stderr, "FAIL: players index #%ld (0x%08lx) is unused or changed\n",
			DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index), (unsigned long)player_index);
		exit(3);
	}
	return player;
}
static void datum_delete(void *data, long player_index)
{
	player_get(player_index)->identifier = 0;
}
static void player_control_new_unit(short local_player_index, long unit_index)
{
	controls[local_player_index].unit_index = unit_index;
}
static long player_new_at(long slot)
{
	CHECK(!players[slot].identifier);
	players[slot].identifier = ++next_identifier;
	players[slot].local_player_index = NONE;
	return ((long)players[slot].identifier << 16) | slot;
}
"""

MAIN = r"""
/* the input update's use of the table (player_control.c) */
static void input_update(void)
{
	short local_player_index;

	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
	{
		long player_index = local_player_get_player_index(local_player_index);

		if (player_index != NONE)
			CHECK(player_get(player_index)->local_player_index == local_player_index);
	}
}

int main(void)
{
	/* the controllers' players in the slots the host gave them */
	static long const slots[MAXIMUM_LOCAL_PLAYERS] = { 1, 3, 2, 4 };
	long handles[MAXIMUM_LOCAL_PLAYERS];
	long other, again;
	short local_player_index;

	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
		players_globals->local_players[local_player_index] = NONE;
	player_new_at(0);
	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
	{
		handles[local_player_index] = player_new_at(slots[local_player_index]);
		local_player_set_player_index(local_player_index, handles[local_player_index]);
		controls[local_player_index].unit_index = 100 + local_player_index;
	}
	input_update();

	/* controllers 1, 0 and 3 quit (their datums stay), and another machine's
	player takes controller 0's slot: network_game_spawn_player deletes the
	quitter's datum, then makes the new one there */
	player_delete(handles[0]);
	other = player_new_at(slots[0]);
	CHECK(DATUM_INDEX_TO_ABSOLUTE_INDEX(other) == DATUM_INDEX_TO_ABSOLUTE_INDEX(handles[0]) && other != handles[0]);
	input_update();
	CHECK(local_player_get_player_index(0) == NONE);
	CHECK(controls[0].unit_index == NONE);
	CHECK(local_player_get_player_index(1) == handles[1] && controls[1].unit_index == 101);
	CHECK(local_player_get_player_index(2) == handles[2] && controls[2].unit_index == 102);

	/* controller 0 joins again, in a new slot: the other machine's player
	stays nobody's local player */
	again = player_new_at(5);
	local_player_set_player_index(0, again);
	CHECK(player_get(other)->local_player_index == NONE);
	CHECK(player_get(again)->local_player_index == 0);
	input_update();

	/* a player not this machine's leaves the table alone */
	player_delete(other);
	CHECK(local_player_get_player_index(0) == again && controls[0].unit_index == NONE);
	input_update();

	puts("PASS");
	return 0;
}
"""

FIX = re.compile(r"\tif \(players_globals && player_index != NONE\)\n\t\{.*?\n\t\}\n", re.S)


def unit(without_fix=False):
    source = PLAYERS_C.read_text()
    delete = function(source, "void player_delete(\n")
    if without_fix:
        delete, count = FIX.subn("", delete)
        if count != 1:
            raise SystemExit("players.c: player_delete's fix not found")
    return "\n".join([
        PRELUDE,
        delete,
        function(source, "long local_player_get_player_index(\n"),
        function(source, "void local_player_set_player_index(\n"),
        MAIN,
    ])


def run(text, temp, name):
    cc = shutil.which(os.environ.get("CC", "cc")) or shutil.which("clang") or shutil.which("gcc")
    if not cc:
        raise SystemExit("no C compiler")
    source = Path(temp) / (name + ".c")
    exe = Path(temp) / name
    source.write_text(text)
    subprocess.run([cc, "-std=c99", "-Wall", "-Werror", "-Wno-unused-variable", "-Wno-unused-function", str(source), "-o", str(exe)], check=True)
    return subprocess.run([str(exe)], capture_output=True, text=True)


def main():
    with tempfile.TemporaryDirectory(prefix="halo-local-player-quit-") as temp:
        result = run(unit(), temp, "fixed")
        if result.returncode != 0 or "PASS" not in result.stdout:
            print(result.stdout + result.stderr)
            print("FAIL: a quit local player's handle is kept or reused")
            return 1
        control = run(unit(without_fix=True), temp, "control")
        if control.returncode == 0:
            print("FAIL: the negative control (no fix in player_delete) passed")
            return 1
        print("PASS: a local player's datum deleted for another machine's player leaves the table; "
              "negative control halts as the Xbox did (%s)" % control.stderr.strip().splitlines()[-1])
    return 0


if __name__ == "__main__":
    sys.exit(main())
