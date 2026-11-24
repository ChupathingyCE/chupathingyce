#include "harness.h"

/* oddball_engine_update with the ball passed by killing: the ball's owner
can quit (his unit deleted, not killed) and his slot be taken by a player
who joins (network_game_spawn_player). The goal marker then follows no one
rather than asking player_get for a player that is gone. */

#include "config.inc"
enum { _multiplayer_sound_team_oddball, _multiplayer_sound_oddball, _multiplayer_sound_play_ball };
struct object_fields { real_point3d bounding_sphere_center; };
struct unit_datum { struct object_fields object; };
struct player_datum { short identifier; long unit_index; };
struct game_variant { struct { struct { long ball_spawn_count; } oddball; } game_engine_variant; };

static struct data_array *player_data;
static struct unit_datum units[4];
static struct game_variant variant;
static struct { long ball_spawn_timer[MAXIMUM_ODDBALLS]; long current_ball_owner[MAXIMUM_ODDBALLS]; } oddball_globals;
static boolean transfer_by_killing;
static long goal_player[MAXIMUM_ODDBALLS];
static int goals_set, goals_cleared;

/* data.c's datum_get, as the game has it: a handle whose identifier is not
the datum's is "unused or changed", an assertion that ends the game */
static struct player_datum *player_get(long index)
{
	struct player_datum *player = datum_get(player_data, index);
	CHECK(index != NONE && player->identifier && player->identifier == (short)(index >> 16),
		"players index #%ld (0x%lx) is unused or changed", index & 0xFFFF, (unsigned long)index);
	return player;
}
static struct player_datum *player_try_and_get(long index)
{
	struct player_datum *player;
	if (index == NONE || DATUM_INDEX_TO_ABSOLUTE_INDEX(index) >= player_data->maximum_count) return NULL;
	player = datum_get(player_data, index);
	return player->identifier && player->identifier == (short)(index >> 16) ? player : NULL;
}
static struct unit_datum *unit_get(long index) { CHECK(index >= 0 && index < 4, "bad unit %ld", index); return &units[index]; }
static long game_time_get(void) { return 100; }
static boolean game_engine_has_teams(void) { return FALSE; }
static void game_engine_play_multiplayer_sound(long sound) { (void)sound; }
static struct game_variant *game_engine_get_variant(void) { return &variant; }
static boolean network_game_distributed_client(void) { return FALSE; }
static void create_the_ball(long ball_index) { (void)ball_index; }
static boolean oddball_ball_transfer_by_killing(void) { return transfer_by_killing; }
static void game_engine_clear_goal_position(short goal_index) { goal_player[goal_index] = NONE; goals_cleared++; }
static void game_engine_set_goal_position(short goal_index, real_point3d const *point, real radius,
	char const *marker, long a, long b, long player_index)
{
	(void)point; (void)radius; (void)marker; (void)a; (void)b;
	goal_player[goal_index] = player_index;
	goals_set++;
}
#include "under_test.inc"

static long add_player(long unit_index)
{
	long index = datum_new(player_data);
	player_get(index)->unit_index = unit_index;
	return index;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	long owner;

	player_data = game_state_data_new("players", 16, sizeof(struct player_datum));
	variant.game_engine_variant.oddball.ball_spawn_count = 1;
	transfer_by_killing = TRUE;
	goal_player[0] = 12345;
	owner = add_player(2);
	oddball_globals.current_ball_owner[0] = owner;

	CASE("live")
	{
		oddball_engine_update();
		CHECK(goals_set == 1 && goal_player[0] == owner, "the owner's unit is marked");
		return 0;
	}
	CASE("none")
	{
		oddball_globals.current_ball_owner[0] = NONE;
		oddball_engine_update();
		CHECK(goals_cleared == 1 && goal_player[0] == NONE, "no owner, no goal");
		return 0;
	}
	CASE("quit")
	{
		/* he quit and his datum went, nobody in his slot yet */
		datum_delete(player_data, owner);
		oddball_engine_update();
		CHECK(goals_cleared == 1 && goals_set == 0 && goal_player[0] == NONE, "a gone owner marks nothing");
		return 0;
	}
	CASE("reused")
	{
		/* he quit, and a player who joined took his slot */
		long joined;

		datum_delete(player_data, owner);
		joined = add_player(3);
		CHECK(DATUM_INDEX_TO_ABSOLUTE_INDEX(joined) == DATUM_INDEX_TO_ABSOLUTE_INDEX(owner) && joined != owner,
			"the joiner has the quitter's slot");
		oddball_engine_update();
		CHECK(goals_cleared == 1 && goals_set == 0 && goal_player[0] == NONE,
			"the joiner is not marked as the ball's owner");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
