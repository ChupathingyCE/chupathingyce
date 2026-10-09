/*
GAME_EVENTS.C

Delta Stats' recorder (docs/delta.md, "Delta Stats"; port/linux/src/
event_log.h): what happens in a game this machine hosts, when
network.report_events is on (event_upload_enabled; on by default), for
the game list's match pages, heatmaps and leaderboards. It watches the
game and changes nothing in it.

Only the host records (the dedicated server, or a player hosting): every
kill, hit and shot there is the host's own, so what it records is exact.
What it watches:

- kills (game_engine_player_killed, through game_stats.c), with the damage
  that dealt them (damage.c: game_events_damage, just before the blow's
  aftermath counts the kill): the weapon, how (bullet, melee, grenade,
  vehicle, fall, ...), a headshot, a stuck grenade, and where the killer
  and the victim were;
- every player's damage to another, and each projectile that hit one (a
  hit), against the shots fired (weapons.c: game_events_shots, each
  projectile a trigger makes), by weapon;
- each frame, each player: joining and leaving (sessions), spawning, the
  vehicle and seat they ride, weapons new in their hands (a pickup; not
  those they spawn with), grenades thrown, camouflage and overshields
  taken, and their game type's statistics moving (a flag grabbed, returned
  or scored; the ball held or dropped; the hill entered or left; a lap);
- a sample of each living player's position every network.events_positions
  seconds, and their ping every 10 seconds;
- on a dedicated server, its minute: players, frame time, CPU, memory.

event_log.c keeps it within its limits and works out the medals and
sprees; at the game's end the batch goes to event_upload.c, which sends it.
A long game is sent as it stands every network.events_part_minutes too.
Players are their names and a hash of their machine's hardware ID; no
address is ever recorded.

Called each frame from game_stats_update (main.c).
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_server_manager.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/unit_definitions.h"
#include "tag_files/tag_files.h"
#include "../src/browser.h"
#include "../src/event_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the platform layer's */
void platform_log(char const *format, ...);
long config_integer(char const *name);
const char *updater_version(void);
const char *build_identity_user_agent(void);
int browser_dedicated(void);
char *getenv(char const *name);
/* (network_server_manager_internal.h's) */
struct network_game *network_game_server_get_game(struct network_game_server *server);
/* Delta Peer's (port/linux/src/delta_peer_game.c): a player's platform
(port/linux/include/delta.h: 0 unknown, i.e. no ChupathingyCE machine) */
int delta_peer_player_platform(int player_index);
/* network_distributed.c's: a player's round trip (milliseconds), NONE */
long distributed_player_ping(short player_index);
long game_engine_report_lines(struct browser_report_player *players, long maximum);

/* ---------- constants */

enum
{
	MAXIMUM_TRACKED = HALO_PORT_MAXIMUM_NETWORK_PLAYERS,
	MAXIMUM_LINES = HALO_PORT_MAXIMUM_NETWORK_PLAYERS,
	MAXIMUM_WEAPONS = 4,
	/* a held ball, or a hill stood on, is one whose time moved this
	recently (the game counts them in whole seconds) */
	HOLD_TICKS = 45,
	PING_TICKS = 10 * TICKS_PER_SECOND,
	HEALTH_MILLISECONDS = 60000,
	/* the weapons a player has within this long of spawning are theirs to
	start with, not pickups */
	SPAWN_WEAPON_TICKS = 15,
};

/* the game's damage categories (damage.c's _damage_category_*), as the
log's kinds of damage */
static unsigned char const damage_kinds[] = {
	EVENT_LOG_DAMAGE_OTHER, EVENT_LOG_DAMAGE_FALL, EVENT_LOG_DAMAGE_BULLET, EVENT_LOG_DAMAGE_GRENADE,
	EVENT_LOG_DAMAGE_EXPLOSION, EVENT_LOG_DAMAGE_BULLET, EVENT_LOG_DAMAGE_MELEE, EVENT_LOG_DAMAGE_OTHER,
	EVENT_LOG_DAMAGE_BULLET, EVENT_LOG_DAMAGE_VEHICLE, EVENT_LOG_DAMAGE_PLASMA, EVENT_LOG_DAMAGE_PLASMA,
	EVENT_LOG_DAMAGE_BULLET,
};

/* ---------- structures */

struct tracked
{
	boolean used;
	short identifier;
	wchar_t name[12];
	/* the log's slot */
	int slot;
	boolean present;
	long unit_index;
	long spawn_tick;
	long next_position_tick;
	long next_ping_tick;
	/* the ride under way: its vehicle, seat (EVENT_LOG_SEAT_*), the
	vehicle's tag, and when it began */
	long vehicle_index;
	short seat;
	short vehicle_tag;
	long ride_tick;
	long weapons[MAXIMUM_WEAPONS];
	char grenades[2];
	short powerups[NUMBER_OF_PLAYER_POWERUPS];
	boolean overshield;
	short flag_grabs;
	short flag_returns;
	short flag_scores;
	short ball_time;
	short hill_time;
	short laps;
	long ball_tick;
	boolean holding_ball;
	long hill_tick;
	boolean on_hill;
};

/* the killing blow being dealt (damage.c, then game_stats.c) */
struct kill_context
{
	boolean set;
	long tick;
	long object_index;
	long definition_index;
	long owner_object_index;
	short category;
	boolean area_of_effect;
	boolean headshot;
};

/* ---------- globals */

static struct
{
	boolean recording;
	long last_tick;
	long started_tick;
	unsigned long part_time;
	int position_ticks;
	struct tracked players[MAXIMUM_TRACKED];
	struct kill_context kill;
	/* the dedicated server's minute */
	unsigned long health_time;
	unsigned long frame_time;
	long frames;
	double frame_sum;
	double frame_most;
} game_events;

/* ---------- private code */

static int damage_kind(short category)
{
	return category >= 0 && category < (short)NUMBEROF(damage_kinds) ? damage_kinds[category] : EVENT_LOG_DAMAGE_OTHER;
}

/* a UTF-16 name as UTF-8 (control characters kept: the log escapes them) */
static void name_utf8(wchar_t const *name, int length, char *text, int size)
{
	int used = 0, index;

	for (index = 0; index < length && name[index] && used < size - 4; index++)
	{
		unsigned int character = (unsigned short)name[index];

		/* (a lone surrogate: not a character) */
		if (character >= 0xD800 && character <= 0xDFFF)
			character = '?';
		if (character < 0x80)
		{
			text[used++] = (char)character;
		}
		else if (character < 0x800)
		{
			text[used++] = (char)(0xC0 | (character >> 6));
			text[used++] = (char)(0x80 | (character & 0x3F));
		}
		else
		{
			text[used++] = (char)(0xE0 | (character >> 12));
			text[used++] = (char)(0x80 | ((character >> 6) & 0x3F));
			text[used++] = (char)(0x80 | (character & 0x3F));
		}
	}
	text[used] = 0;
}

static int tag_of(long definition_index)
{
	char const *name = definition_index != NONE ? tag_get_name(definition_index) : NULL;

	return name ? event_log_tag(name) : EVENT_LOG_NONE;
}

/* a tag's folder (weapons\pistol\pistol: weapons\pistol), as a tag of the log */
static int folder_of(long definition_index)
{
	char const *name = definition_index != NONE ? tag_get_name(definition_index) : NULL;
	char folder[EVENT_LOG_TAG_SIZE];
	char *slash;

	if (!name)
		return EVENT_LOG_NONE;
	snprintf(folder, sizeof(folder), "%s", name);
	slash = strrchr(folder, '\\');
	if (slash && slash != folder)
		*slash = 0;
	return event_log_tag(folder);
}

static struct tracked *tracked_of(long player_index)
{
	long absolute_index;

	if (player_index == NONE)
		return NULL;
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	if (absolute_index < 0 || absolute_index >= MAXIMUM_TRACKED || !game_events.players[absolute_index].used)
		return NULL;
	return &game_events.players[absolute_index];
}

static int slot_of(long player_index)
{
	struct tracked *tracked = tracked_of(player_index);

	return tracked ? tracked->slot : EVENT_LOG_NONE;
}

/* the player whose unit an object is (a vehicle's: its driver's, or its
gunner's) */
static long player_of_object(long object_index)
{
	struct unit_datum *unit = object_index != NONE ? unit_try_and_get(object_index) : NULL;

	if (!unit)
		return NONE;
	if (unit->unit.player_index != NONE)
		return unit->unit.player_index;
	if (unit->unit.driver_object_index != NONE)
	{
		struct unit_datum *driver = unit_try_and_get(unit->unit.driver_object_index);

		if (driver && driver->unit.player_index != NONE)
			return driver->unit.player_index;
	}
	if (unit->unit.gunner_object_index != NONE)
	{
		struct unit_datum *gunner = unit_try_and_get(unit->unit.gunner_object_index);

		if (gunner && gunner->unit.player_index != NONE)
			return gunner->unit.player_index;
	}
	return NONE;
}

static boolean object_position(long object_index, float *position)
{
	real_point3d origin;

	if (object_index == NONE || !object_try_and_get(object_index))
		return FALSE;
	object_get_origin(object_index, &origin);
	position[0] = origin.x;
	position[1] = origin.y;
	position[2] = origin.z;
	/* (finite: the game's math.h has no isfinite) */
	return position[0] - position[0] == 0.0f && position[1] - position[1] == 0.0f && position[2] - position[2] == 0.0f;
}

/* the vehicle a unit rides (NONE), and its seat */
static long unit_vehicle(struct unit_datum *unit, short *seat)
{
	struct unit_datum *vehicle;
	long vehicle_index = unit->object.parent_object_index;

	*seat = EVENT_LOG_SEAT_PASSENGER;
	vehicle = vehicle_index != NONE ? unit_try_and_get(vehicle_index) : NULL;
	if (!vehicle)
		return NONE;
	{
		struct unit_definition *definition = unit_definition_get(vehicle->definition_index);
		short index = unit->unit.parent_seat_index;

		if (vehicle->unit.driver_object_index != NONE && unit_try_and_get(vehicle->unit.driver_object_index) == unit)
			*seat = EVENT_LOG_SEAT_DRIVER;
		else if (vehicle->unit.gunner_object_index != NONE &&
			unit_try_and_get(vehicle->unit.gunner_object_index) == unit)
			*seat = EVENT_LOG_SEAT_GUNNER;
		else if (definition && index >= 0 && index < definition->unit.seats.count)
		{
			struct unit_seat *seat_definition = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, index, struct unit_seat);

			if (strstr(seat_definition->label, "driver"))
				*seat = EVENT_LOG_SEAT_DRIVER;
			else if (strstr(seat_definition->label, "gunner"))
				*seat = EVENT_LOG_SEAT_GUNNER;
		}
	}
	return vehicle_index;
}

static void record_simple(int type, int slot, long tick, int tag, int value0, int value1, float const *position)
{
	struct event_log_record record;

	csmemset(&record, 0, sizeof(record));
	record.tick = (int)tick;
	record.type = (short)type;
	record.player = (short)slot;
	record.other = EVENT_LOG_NONE;
	record.tag[0] = (short)tag;
	record.tag[1] = record.tag[2] = EVENT_LOG_NONE;
	record.value[0] = value0;
	record.value[1] = value1;
	if (position)
		csmemcpy(record.position, position, sizeof(record.position));
	event_log_add(&record);
}

static void end_ride(struct tracked *tracked, long tick)
{
	if (tracked->vehicle_index == NONE)
		return;
	record_simple(EVENT_LOG_RIDE, tracked->slot, tick, tracked->vehicle_tag, tracked->seat, (int)tracked->ride_tick, NULL);
	tracked->vehicle_index = NONE;
}

/* the game's statistics of a player, as the log's totals */
static void player_totals(struct player_datum const *player, int place, int score, struct event_log_player_totals *totals)
{
	struct game_statistics const *statistics = &player->statistics;

	csmemset(totals, 0, sizeof(*totals));
	totals->score = score;
	totals->place = place;
	totals->kills = statistics->kills[0];
	totals->deaths = statistics->deaths;
	totals->assists = statistics->assists[0];
	totals->betrayals = statistics->friendly_fire_kills;
	totals->suicides = statistics->suicides;
	totals->team = (int)player->team_index;
	switch (game_engine_get_variant()->game_engine_index)
	{
	case game_engine_ctf:
		totals->flag_grabs = statistics->multiplayer_statistics.ctf_statistics.flag_grabs;
		totals->flag_returns = statistics->multiplayer_statistics.ctf_statistics.flag_returns;
		totals->flag_scores = statistics->multiplayer_statistics.ctf_statistics.flag_scores;
		break;
	case game_engine_oddball:
		totals->ball_time = statistics->multiplayer_statistics.oddball_statistics.time_with_the_ball;
		totals->ball_kills = statistics->multiplayer_statistics.oddball_statistics.ball_carrier_kills;
		break;
	case game_engine_king:
		totals->hill_time = statistics->multiplayer_statistics.king_statistics.time_on_hill;
		break;
	case game_engine_race:
		totals->laps = statistics->multiplayer_statistics.race_statistics.laps;
		break;
	}
}

static void begin_game(long tick)
{
	struct event_log_game game;
	unsigned char id[16];
	struct network_game_server *server = global_network_game_server_get();
	struct network_game *network_game = server ? network_game_server_get_game(server) : NULL;
	struct game_variant *variant = game_engine_get_variant();
	char const *agent = build_identity_user_agent();
	char const *open = strchr(agent, '(');
	long index;

	csmemset(&game, 0, sizeof(game));
	if (network_game)
	{
		snprintf(game.map, sizeof(game.map), "%s", network_game->map.name);
		name_utf8(network_game->name, 16, game.server_name, sizeof(game.server_name));
	}
	name_utf8(variant->human_readable_game_description, 12, game.gametype, sizeof(game.gametype));
	/* (a built-in variant has no name of its own: its game type's) */
	if (!game.gametype[0])
	{
		static char const *const engines[] = { "", "CTF", "Slayer", "Oddball", "King of the Hill", "Race" };
		long engine = variant->game_engine_index;

		snprintf(game.gametype, sizeof(game.gametype), "%s%s", variant->universal_variant.teams && engine == 2 ? "Team " : "",
			engine >= 0 && engine < (long)NUMBEROF(engines) ? engines[engine] : "");
	}
	game.engine = (int)variant->game_engine_index;
	game.teams = variant->universal_variant.teams ? 1 : 0;
	game.score_limit = (int)variant->universal_variant.score_to_win;
	snprintf(game.build, sizeof(game.build), "%s", updater_version());
	/* ("ChupathingyCE/0.7.0b (macOS arm64)": macos-arm64) */
	if (open)
	{
		int used = 0;

		for (open++; *open && *open != ')' && used < (int)sizeof(game.platform) - 1; open++)
		{
			char character = *open == ' ' ? '-' : *open;

			game.platform[used++] = character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
		}
		game.platform[used] = 0;
	}
	/* (a dedicated server's playlist: its file's name, playlists/slayer.txt
	as "slayer") */
	if (browser_dedicated() && getenv("HALO_DEDICATED"))
	{
		char const *name = getenv("HALO_DEDICATED");
		char const *slash = strrchr(name, '/');
		char *dot;

		if (strrchr(name, '\\') > slash)
			slash = strrchr(name, '\\');
		snprintf(game.playlist, sizeof(game.playlist), "%s", slash ? slash + 1 : name);
		dot = strrchr(game.playlist, '.');
		if (dot)
			*dot = 0;
	}
	if (browser_dedicated())
		snprintf(game.platform + strlen(game.platform), sizeof(game.platform) - strlen(game.platform), "%s",
			game.platform[0] ? "-server" : "server");
	game.start_time = event_upload_time() - (unsigned int)(tick / TICKS_PER_SECOND);
	event_upload_random(id, sizeof(id));
	event_log_set_capacity(event_upload_event_limit());
	event_log_begin(&game, id);
	csmemset(game_events.players, 0, sizeof(game_events.players));
	for (index = 0; index < MAXIMUM_TRACKED; index++)
		game_events.players[index].slot = EVENT_LOG_NONE;
	game_events.recording = TRUE;
	game_events.started_tick = tick;
	game_events.last_tick = tick;
	game_events.part_time = system_milliseconds();
	game_events.position_ticks = event_upload_position_seconds() * TICKS_PER_SECOND;
	game_events.kill.set = FALSE;
	platform_log("Delta Stats: recording game %s (%s)", event_log_game_id(), game.map);
}

/* the game's batch (its end, or a part of it so far), to the uploader */
static void send_game(boolean final, int reason)
{
	struct event_log_end end;
	char invite[160];
	char game_id[40];
	struct data_iterator iterator;
	struct player_datum *player;
	long count, index;
	size_t length;
	char *json;
	long tick = game_time_get();

	if (final)
	{
		/* (the rides under way end with the game) */
		for (index = 0; index < MAXIMUM_TRACKED; index++)
		{
			if (game_events.players[index].used)
				end_ride(&game_events.players[index], tick);
		}
	}
	/* each player's totals, and place (the postgame's order) */
	{
		static struct browser_report_player ranking[MAXIMUM_LINES];
		short place_of[MAXIMUM_TRACKED];
		int score_of[MAXIMUM_TRACKED];

		csmemset(place_of, 0, sizeof(place_of));
		csmemset(score_of, 0, sizeof(score_of));
		count = game_engine_report_lines(ranking, MAXIMUM_LINES);
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);

			for (index = 0; index < count; index++)
			{
				if (!csmemcmp(ranking[index].name, player->name, sizeof(player->name)) && absolute_index >= 0 &&
					absolute_index < MAXIMUM_TRACKED)
				{
					place_of[absolute_index] = ranking[index].place;
					score_of[absolute_index] = ranking[index].score;
					break;
				}
			}
		}
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			struct tracked *tracked = tracked_of(iterator.datum_index);
			long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
			struct event_log_player_totals totals;

			if (!tracked)
				continue;
			player_totals(player, place_of[absolute_index], score_of[absolute_index], &totals);
			event_log_player_totals(tracked->slot, &totals);
		}
	}
	csmemset(&end, 0, sizeof(end));
	end.tick = (int)tick;
	end.reason = reason;
	if (game_engine_get_variant()->universal_variant.teams)
	{
		end.team_scores[0] = (int)game_engine_get_team_score(0);
		end.team_scores[1] = (int)game_engine_get_team_score(1);
	}
	end.end_time = event_upload_time();
	event_upload_invite(invite, sizeof(invite));
	snprintf(game_id, sizeof(game_id), "%s", event_log_game_id());
	count = event_log_count();
	json = event_log_finish(&end, invite, final, &length);
	if (json)
	{
		platform_log("Delta Stats: game %s %s: %u bytes, %ld events (%d dropped)", game_id,
			final ? "over" : "so far", (unsigned int)length, count, event_log_dropped());
		event_upload_submit(json, length, game_id);
	}
}

/* a player new to the game (or back in it) */
static void track_player(struct tracked *tracked, struct player_datum *player, long player_index, long tick)
{
	struct event_log_player_identity identity;
	char const *hardware_id = network_game_server_machine_hardware_id(player->network_player_data.machine_index);
	int platform = delta_peer_player_platform((int)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index));
	static char const *const platforms[] = { "", "windows", "macos", "linux", "android", "linux", "xbox", "xbox",
		"other", "other" };
	long index;

	csmemset(tracked, 0, sizeof(*tracked));
	tracked->used = TRUE;
	tracked->identifier = player->identifier;
	csmemcpy(tracked->name, player->name, sizeof(tracked->name));
	tracked->unit_index = NONE;
	tracked->vehicle_index = NONE;
	for (index = 0; index < MAXIMUM_WEAPONS; index++)
		tracked->weapons[index] = NONE;
	csmemset(&identity, 0, sizeof(identity));
	name_utf8(player->name, 12, identity.name, sizeof(identity.name));
	snprintf(identity.hardware_id, sizeof(identity.hardware_id), "%s", hardware_id ? hardware_id : "");
	identity.client = platform > 0 ? EVENT_LOG_CLIENT_CHUPATHINGYCE : EVENT_LOG_CLIENT_OTHER;
	snprintf(identity.platform, sizeof(identity.platform), "%s",
		platform > 0 && platform < (int)NUMBEROF(platforms) ? platforms[platform] : "");
	identity.team = (int)player->team_index;
	identity.bot = FALSE;
	identity.color = player->network_player_data.primary_color_index;
	tracked->slot = event_log_player((int)tick, &identity);
	tracked->present = TRUE;
	tracked->next_position_tick = tick + (DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) % 30);
	tracked->next_ping_tick = tick + TICKS_PER_SECOND;
	/* (the game type's statistics as they stand: only what moves after counts) */
	tracked->flag_grabs = player->statistics.multiplayer_statistics.ctf_statistics.flag_grabs;
	tracked->flag_returns = player->statistics.multiplayer_statistics.ctf_statistics.flag_returns;
	tracked->flag_scores = player->statistics.multiplayer_statistics.ctf_statistics.flag_scores;
	tracked->ball_time = player->statistics.multiplayer_statistics.oddball_statistics.time_with_the_ball;
	tracked->hill_time = player->statistics.multiplayer_statistics.king_statistics.time_on_hill;
	tracked->laps = player->statistics.multiplayer_statistics.race_statistics.laps;
	csmemcpy(tracked->powerups, player->powerup_durations, sizeof(tracked->powerups));
}

/* a statistic moved: an objective's event */
static void objective(struct tracked *tracked, struct player_datum *player, long tick, int kind, float const *position)
{
	struct event_log_record record;

	csmemset(&record, 0, sizeof(record));
	record.tick = (int)tick;
	record.type = EVENT_LOG_OBJECTIVE;
	record.player = (short)tracked->slot;
	record.other = EVENT_LOG_NONE;
	record.tag[0] = record.tag[1] = record.tag[2] = EVENT_LOG_NONE;
	record.value[0] = kind;
	record.value[1] = (int)player->team_index;
	if (position)
		csmemcpy(record.position, position, sizeof(record.position));
	event_log_add(&record);
}

static void update_objectives(struct tracked *tracked, struct player_datum *player, long tick, float const *position)
{
	union multiplayer_statistics const *statistics = &player->statistics.multiplayer_statistics;
	short value;

	switch (game_engine_get_variant()->game_engine_index)
	{
	case game_engine_ctf:
		for (value = tracked->flag_grabs; value < statistics->ctf_statistics.flag_grabs; value++)
			objective(tracked, player, tick, EVENT_LOG_OBJECTIVE_FLAG_GRAB, position);
		for (value = tracked->flag_returns; value < statistics->ctf_statistics.flag_returns; value++)
			objective(tracked, player, tick, EVENT_LOG_OBJECTIVE_FLAG_RETURN, position);
		for (value = tracked->flag_scores; value < statistics->ctf_statistics.flag_scores; value++)
			objective(tracked, player, tick, EVENT_LOG_OBJECTIVE_FLAG_SCORE, position);
		tracked->flag_grabs = statistics->ctf_statistics.flag_grabs;
		tracked->flag_returns = statistics->ctf_statistics.flag_returns;
		tracked->flag_scores = statistics->ctf_statistics.flag_scores;
		break;
	case game_engine_oddball:
		if (statistics->oddball_statistics.time_with_the_ball != tracked->ball_time)
		{
			tracked->ball_time = statistics->oddball_statistics.time_with_the_ball;
			tracked->ball_tick = tick;
			if (!tracked->holding_ball)
			{
				tracked->holding_ball = TRUE;
				objective(tracked, player, tick, EVENT_LOG_OBJECTIVE_BALL_GRAB, position);
			}
		}
		else if (tracked->holding_ball && tick - tracked->ball_tick > HOLD_TICKS)
		{
			tracked->holding_ball = FALSE;
			objective(tracked, player, tick, EVENT_LOG_OBJECTIVE_BALL_DROP, position);
		}
		break;
	case game_engine_king:
		if (statistics->king_statistics.time_on_hill != tracked->hill_time)
		{
			tracked->hill_time = statistics->king_statistics.time_on_hill;
			tracked->hill_tick = tick;
			if (!tracked->on_hill)
			{
				tracked->on_hill = TRUE;
				objective(tracked, player, tick, EVENT_LOG_OBJECTIVE_HILL_ENTER, position);
			}
		}
		else if (tracked->on_hill && tick - tracked->hill_tick > HOLD_TICKS)
		{
			tracked->on_hill = FALSE;
			objective(tracked, player, tick, EVENT_LOG_OBJECTIVE_HILL_EXIT, position);
		}
		break;
	case game_engine_race:
		for (value = tracked->laps; value < statistics->race_statistics.laps; value++)
			objective(tracked, player, tick, EVENT_LOG_OBJECTIVE_RACE_LAP, position);
		tracked->laps = statistics->race_statistics.laps;
		break;
	}
}

/* what a player's unit does this frame: spawning, riding, picking up,
throwing, powering up, where it is */
static void update_unit(struct tracked *tracked, struct player_datum *player, long player_index, long tick)
{
	struct unit_datum *unit = player->unit_index != NONE ? unit_try_and_get(player->unit_index) : NULL;
	float position[3];
	boolean positioned;
	long vehicle_index;
	short seat;
	long index;

	if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
	{
		if (tracked->unit_index != NONE)
		{
			end_ride(tracked, tick);
			tracked->unit_index = NONE;
		}
		return;
	}
	positioned = object_position(player->unit_index, position);
	if (tracked->unit_index != player->unit_index)
	{
		/* a spawn (a new unit) */
		end_ride(tracked, tick);
		tracked->unit_index = player->unit_index;
		tracked->spawn_tick = tick;
		for (index = 0; index < MAXIMUM_WEAPONS; index++)
			tracked->weapons[index] = NONE;
		tracked->grenades[0] = unit->unit.grenade_counts[0];
		tracked->grenades[1] = NUMBER_OF_UNIT_GRENADE_TYPES > 1 ? unit->unit.grenade_counts[1] : 0;
		tracked->overshield = unit->object.shield_vitality > 1.0f;
		if (positioned)
			record_simple(EVENT_LOG_SPAWN, tracked->slot, tick, EVENT_LOG_NONE, 0, 0, position);
	}
	/* the vehicle and seat */
	vehicle_index = unit_vehicle(unit, &seat);
	if (vehicle_index != tracked->vehicle_index || (vehicle_index != NONE && seat != tracked->seat))
	{
		end_ride(tracked, tick);
		if (vehicle_index != NONE)
		{
			struct unit_datum *vehicle = unit_try_and_get(vehicle_index);

			tracked->vehicle_index = vehicle_index;
			tracked->seat = seat;
			tracked->vehicle_tag = (short)(vehicle ? tag_of(vehicle->definition_index) : EVENT_LOG_NONE);
			tracked->ride_tick = tick;
		}
	}
	/* weapons new in the player's hands */
	for (index = 0; index < MAXIMUM_WEAPONS; index++)
	{
		long weapon_index = unit->unit.weapon_object_indices[index];
		long known;
		boolean seen = FALSE;

		for (known = 0; known < MAXIMUM_WEAPONS; known++)
		{
			if (tracked->weapons[known] == weapon_index)
				seen = TRUE;
		}
		if (weapon_index != NONE && !seen && tick - tracked->spawn_tick > SPAWN_WEAPON_TICKS)
		{
			struct object_datum *weapon = object_try_and_get(weapon_index);

			if (weapon)
				record_simple(EVENT_LOG_PICKUP, tracked->slot, tick, tag_of(weapon->definition_index), 0, 0,
					positioned ? position : NULL);
		}
	}
	for (index = 0; index < MAXIMUM_WEAPONS; index++)
		tracked->weapons[index] = unit->unit.weapon_object_indices[index];
	/* grenades thrown (a count that went down), as shots of the grenade */
	for (index = 0; index < 2 && index < NUMBER_OF_UNIT_GRENADE_TYPES; index++)
	{
		char count = unit->unit.grenade_counts[index];

		if (count < tracked->grenades[index])
		{
			int thrown = tracked->grenades[index] - count;

			while (thrown-- > 0)
				event_log_grenade(tracked->slot, (int)index);
			event_log_shots(tracked->slot, event_log_tag(index ? "weapons\\plasma grenade" : "weapons\\frag grenade"),
				tracked->grenades[index] - count);
		}
		tracked->grenades[index] = count;
	}
	/* powerups taken */
	for (index = 0; index < NUMBER_OF_PLAYER_POWERUPS; index++)
	{
		if (player->powerup_durations[index] > tracked->powerups[index] + 1)
			record_simple(EVENT_LOG_PICKUP, tracked->slot, tick,
				event_log_tag(index == _player_powerup_active_camouflage ? "powerups\\active camouflage" :
					"powerups\\full-spectrum vision"), 0, 0, positioned ? position : NULL);
		tracked->powerups[index] = player->powerup_durations[index];
	}
	if (unit->object.shield_vitality > 1.0f && !tracked->overshield)
		record_simple(EVENT_LOG_PICKUP, tracked->slot, tick, event_log_tag("powerups\\over shield"), 0, 0,
			positioned ? position : NULL);
	tracked->overshield = unit->object.shield_vitality > 1.0f;
	update_objectives(tracked, player, tick, positioned ? position : NULL);
	/* where they are, every so often */
	if (positioned && game_events.position_ticks > 0 && tick >= tracked->next_position_tick)
	{
		record_simple(EVENT_LOG_POSITION, tracked->slot, tick, EVENT_LOG_NONE, 0, 0, position);
		tracked->next_position_tick = tick + game_events.position_ticks;
	}
	(void)player_index;
}

static void update_players(long tick)
{
	boolean seen[MAXIMUM_TRACKED];
	struct data_iterator iterator;
	struct player_datum *player;
	long index;

	csmemset(seen, 0, sizeof(seen));
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		struct tracked *tracked;

		if (absolute_index < 0 || absolute_index >= MAXIMUM_TRACKED)
			continue;
		tracked = &game_events.players[absolute_index];
		/* (another player in the same place: the one before left) */
		if (tracked->used && (tracked->identifier != player->identifier ||
			csmemcmp(tracked->name, player->name, sizeof(tracked->name))))
		{
			end_ride(tracked, tick);
			event_log_player_left(tracked->slot, (int)tick, EVENT_LOG_LEFT_QUIT);
			tracked->used = FALSE;
		}
		if (player->quit_out_of_game)
		{
			if (tracked->used && tracked->present)
			{
				end_ride(tracked, tick);
				event_log_player_left(tracked->slot, (int)tick, EVENT_LOG_LEFT_QUIT);
				tracked->present = FALSE;
			}
			if (tracked->used)
				seen[absolute_index] = TRUE;
			continue;
		}
		seen[absolute_index] = TRUE;
		if (!tracked->used)
			track_player(tracked, player, iterator.datum_index, tick);
		else if (!tracked->present)
		{
			event_log_player_rejoined(tracked->slot, (int)tick);
			tracked->present = TRUE;
		}
		update_unit(tracked, player, iterator.datum_index, tick);
		if (tick >= tracked->next_ping_tick)
		{
			long ping = distributed_player_ping((short)absolute_index);

			/* (the host's own players have none) */
			if (ping > 0)
				record_simple(EVENT_LOG_PING, tracked->slot, tick, EVENT_LOG_NONE, (int)ping, 0, NULL);
			tracked->next_ping_tick = tick + PING_TICKS;
		}
	}
	for (index = 0; index < MAXIMUM_TRACKED; index++)
	{
		struct tracked *tracked = &game_events.players[index];

		if (tracked->used && !seen[index])
		{
			end_ride(tracked, tick);
			if (tracked->present)
				event_log_player_left(tracked->slot, (int)tick, EVENT_LOG_LEFT_QUIT);
			tracked->used = FALSE;
		}
	}
}

/* the dedicated server's minute */
static void update_health(long tick)
{
	unsigned long now = system_milliseconds();

	if (!browser_dedicated())
		return;
	if (game_events.frame_time)
	{
		double frame = (double)(now - game_events.frame_time);

		game_events.frame_sum += frame;
		game_events.frames++;
		if (frame > game_events.frame_most)
			game_events.frame_most = frame;
	}
	game_events.frame_time = now;
	if (!game_events.health_time)
		game_events.health_time = now;
	if (now - game_events.health_time >= HEALTH_MILLISECONDS && game_events.frames)
	{
		struct event_log_record record;
		struct data_iterator iterator;
		int cpu, resident, players = 0;

		data_iterator_new(&iterator, player_data);
		while (data_iterator_next(&iterator))
			players++;
		event_upload_system_sample(&cpu, &resident);
		csmemset(&record, 0, sizeof(record));
		record.tick = (int)tick;
		record.type = EVENT_LOG_HEALTH;
		record.player = record.other = EVENT_LOG_NONE;
		record.tag[0] = record.tag[1] = record.tag[2] = EVENT_LOG_NONE;
		record.value[0] = players;
		record.value[1] = (int)(100.0 * game_events.frame_sum / game_events.frames + 0.5);
		record.value[2] = (int)(100.0 * game_events.frame_most + 0.5);
		record.value[3] = resident;
		record.bits = (unsigned int)cpu;
		event_log_add(&record);
		game_events.health_time = now;
		game_events.frames = 0;
		game_events.frame_sum = 0.0;
		game_events.frame_most = 0.0;
	}
}

/* how the game ended, as far as it says: everyone gone, a score at its
limit, else time */
static int end_reason(void)
{
	static struct browser_report_player ranking[MAXIMUM_LINES];
	struct game_variant *variant = game_engine_get_variant();
	long limit = variant->universal_variant.score_to_win;
	long count, index;

	{
		long present = 0;

		for (index = 0; index < MAXIMUM_TRACKED; index++)
			present += game_events.players[index].used && game_events.players[index].present;
		if (!present)
			return EVENT_LOG_END_EMPTY;
	}
	if (limit <= 0)
		return EVENT_LOG_END_TIME;
	if (variant->universal_variant.teams)
		return game_engine_get_team_score(0) >= limit || game_engine_get_team_score(1) >= limit ? EVENT_LOG_END_SCORE :
			EVENT_LOG_END_OTHER;
	count = game_engine_report_lines(ranking, MAXIMUM_LINES);
	for (index = 0; index < count; index++)
	{
		if (ranking[index].score >= limit)
			return EVENT_LOG_END_SCORE;
	}
	return EVENT_LOG_END_OTHER;
}

/* ---------- public code */

/* damage.c: a unit took damage (before its aftermath counts any kill):
from whom (damage's owner), of what (its tag, category), and whether it hit
the head and killed */
void game_events_damage(
	long object_index,
	long definition_index,
	long owner_player_index,
	long owner_object_index,
	short category,
	boolean area_of_effect,
	boolean headshot,
	real amount,
	boolean killing)
{
	struct unit_datum *victim_unit;
	long victim_player_index;
	int dealer, taker;

	if (!game_events.recording)
		return;
	if (killing)
	{
		game_events.kill.set = TRUE;
		game_events.kill.tick = game_time_get();
		game_events.kill.object_index = object_index;
		game_events.kill.definition_index = definition_index;
		game_events.kill.owner_object_index = owner_object_index;
		game_events.kill.category = category;
		game_events.kill.area_of_effect = area_of_effect;
		game_events.kill.headshot = headshot;
	}
	victim_unit = unit_try_and_get(object_index);
	victim_player_index = victim_unit ? victim_unit->unit.player_index : NONE;
	if (victim_player_index == NONE)
		return;
	if (owner_player_index == NONE)
		owner_player_index = player_of_object(owner_object_index);
	dealer = slot_of(owner_player_index);
	taker = slot_of(victim_player_index);
	if (owner_player_index == victim_player_index)
		dealer = EVENT_LOG_NONE;
	event_log_damage(dealer, taker, (float)amount);
	/* (a hit: a shot of a weapon's, not a melee blow) */
	if (dealer != EVENT_LOG_NONE && taker != EVENT_LOG_NONE && damage_kind(category) != EVENT_LOG_DAMAGE_MELEE &&
		!(definition_index != NONE && strstr(tag_get_name(definition_index), "melee")))
		event_log_hit(dealer, folder_of(definition_index), (float)amount);
}

/* weapons.c: a trigger made count projectiles of the weapon its owner (a
unit, or a vehicle's gunner) carries */
void game_events_shots(
	long owner_object_index,
	long weapon_definition_index,
	short count)
{
	int slot;

	if (!game_events.recording || count <= 0)
		return;
	slot = slot_of(player_of_object(owner_object_index));
	if (slot != EVENT_LOG_NONE)
		event_log_shots(slot, folder_of(weapon_definition_index), count);
}

/* game_stats.c (game_engine_player_killed): a player died, killed by
killing_player_index (NONE: no player's), a betrayal or not; with the
blow's damage (game_events_damage), if it came this tick */
void game_events_player_killed(
	long killing_player_index,
	long dead_player_index,
	boolean friendly_fire)
{
	struct event_log_record record;
	struct player_datum *dead;
	struct player_datum *killer;
	struct kill_context *context = &game_events.kill;
	boolean have_context;

	if (!game_events.recording || dead_player_index == NONE)
		return;
	dead = player_get(dead_player_index);
	killer = killing_player_index != NONE ? player_get(killing_player_index) : NULL;
	have_context = context->set && context->tick == game_time_get();
	csmemset(&record, 0, sizeof(record));
	record.tick = (int)game_time_get();
	record.type = EVENT_LOG_KILL;
	record.player = (short)slot_of(killing_player_index);
	record.other = (short)slot_of(dead_player_index);
	record.tag[0] = record.tag[1] = record.tag[2] = EVENT_LOG_NONE;
	if (have_context)
	{
		char const *name = context->definition_index != NONE ? tag_get_name(context->definition_index) : NULL;

		record.tag[0] = (short)tag_of(context->definition_index);
		record.value[0] = damage_kind(context->category);
		if (record.value[0] == EVENT_LOG_DAMAGE_BULLET && context->area_of_effect)
			record.value[0] = EVENT_LOG_DAMAGE_EXPLOSION;
		if (context->headshot)
			record.bits |= EVENT_LOG_KILL_HEADSHOT;
		if (name && strstr(name, "plasma grenade\\attached"))
			record.bits |= EVENT_LOG_KILL_STICK;
		if (name && strstr(name, "melee"))
			record.value[0] = EVENT_LOG_DAMAGE_MELEE;
	}
	if (killing_player_index == dead_player_index)
		record.bits |= EVENT_LOG_KILL_SUICIDE;
	else if (friendly_fire)
		record.bits |= EVENT_LOG_KILL_BETRAYAL;
	/* where they were: the victim's body as it fell, the killer's unit (or,
	dead, their body) */
	{
		long victim_unit = have_context ? context->object_index : dead->unit_index;

		if (victim_unit == NONE)
			victim_unit = dead->dead_unit_index;
		if (object_position(victim_unit, record.other_position))
			record.bits |= EVENT_LOG_KILL_VICTIM_POSITION;
		{
			struct unit_datum *unit = victim_unit != NONE ? unit_try_and_get(victim_unit) : NULL;
			short seat;

			if (unit && unit_vehicle(unit, &seat) != NONE)
				record.bits |= EVENT_LOG_KILL_VICTIM_RIDING;
		}
	}
	if (killer && killing_player_index != dead_player_index)
	{
		long killer_unit = killer->unit_index != NONE ? killer->unit_index : killer->dead_unit_index;
		struct unit_datum *unit = killer->unit_index != NONE ? unit_try_and_get(killer->unit_index) : NULL;

		if (object_position(killer_unit, record.position))
			record.bits |= EVENT_LOG_KILL_KILLER_POSITION;
		/* (dead as the blow landed: a grenade or a rocket of theirs) */
		if (!unit)
			record.bits |= EVENT_LOG_KILL_FROM_GRAVE;
		if (unit)
		{
			short seat;
			long vehicle_index = unit_vehicle(unit, &seat);
			struct unit_datum *vehicle = vehicle_index != NONE ? unit_try_and_get(vehicle_index) : NULL;

			if (vehicle)
				record.tag[1] = (short)tag_of(vehicle->definition_index);
		}
	}
	event_log_add(&record);
	context->set = FALSE;
}

/* each frame (game_stats_update) */
void game_events_update(
	void)
{
	long tick;
	boolean hosting = game_connection() == _game_connection_network_server;
	boolean running = game_engine_running() && hosting;

	if (!running)
	{
		/* (a game that stopped without its end: the server's game torn
		down, the host quitting; nothing is sent) */
		if (game_events.recording)
		{
			struct event_log_end end;
			size_t length;
			char *json;

			csmemset(&end, 0, sizeof(end));
			json = event_log_finish(&end, "", TRUE, &length);
			event_log_free(json);
			game_events.recording = FALSE;
			platform_log("Delta Stats: the game stopped before its end: not sent");
		}
		return;
	}
	tick = game_time_get();
	/* (a new game: the game's time went back) */
	if (game_events.recording && tick < game_events.last_tick)
		send_game(TRUE, EVENT_LOG_END_OTHER), game_events.recording = FALSE;
	if (!game_events.recording)
	{
		if (!event_upload_enabled() || !game_engine_can_score())
			return;
		begin_game(tick);
	}
	game_events.last_tick = tick;
	if (!game_engine_can_score())
	{
		/* the game's end (the postgame) */
		update_players(tick);
		send_game(TRUE, end_reason());
		game_events.recording = FALSE;
		return;
	}
	update_players(tick);
	update_health(tick);
	event_upload_moderation_drain();
	{
		long minutes = config_integer("network.events_part_minutes");

		if (minutes > 0 && system_milliseconds() - game_events.part_time >= (unsigned long)minutes * 60000)
		{
			send_game(FALSE, EVENT_LOG_END_OTHER);
			game_events.part_time = system_milliseconds();
		}
	}
}

#endif
