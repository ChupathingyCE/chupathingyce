/*
GAME_STATS.C

The game list's statistics recorder (configure.py --game-browser,
HALO_GAME_BROWSER; port/linux/src/browser.c). It watches the game and
changes nothing in it, on the host and on every client alike:

- Every kill as this machine's game has it (game_engine_player_killed:
  who killed whom, betrayal or not), with the damage that dealt it
  (damage.c, where the kill is counted). From these it works out each
  player's multikills (kills each within 4 seconds of the last, the game's
  own window: game_statistics_record_kill), sprees (kills without dying),
  kills by weapon (the damage's tag) and the medals they make.
- Every frame, each player's statistics as they stand, so that a player who
  leaves before the end is still in the report (with what they had when
  they left), and when each player was first and last in the game.

On the host every kill is the host's own, so all of this is exact. A client
is sent the host's statistics and scores (network_distributed.c), which it
reports as they are; it sees each death when its copy of the player dies,
with the host's killer, and usually the host's killing blow (network_damage.c
replays it), so its multikills, sprees and weapons are worked out from what
it saw: a player's are sent only when the kills this machine saw add up to
the host's count of that player's kills, and the report says how each part
was known ("sources").

The host's report goes with the game's listing (game_engine_report_game). A
client that joined through an invite sends its own report a few seconds
after the game ends (browser_client_report), whoever hosted it, so that a
game whose host does not report is recorded too.

Called each frame from the main loop (main.c).
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
#include "networking/network_client_manager.h"
#include "tag_files/tag_files.h"
#include "../src/browser.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* game_engine.c's */
long game_engine_report_lines(struct browser_report_player *players, long maximum);
struct game_variant *game_engine_get_variant(void);
/* this file's, game_engine.c's too */
void game_stats_game_extra(boolean host, char *text, long size);
/* the platform layer's */
void platform_log(char const *format, ...);
const char *updater_version(void);

/* ---------- constants */

enum
{
	MAXIMUM_STATS_PLAYERS = HALO_PORT_MAXIMUM_NETWORK_PLAYERS,
	/* players who left before the end, kept */
	MAXIMUM_DEPARTED_PLAYERS = 32,
	/* the weapons (damage tags) a player's kills are counted by */
	MAXIMUM_PLAYER_WEAPONS = 12,
	/* the game's multikill window (game_statistics_record_kill's) */
	MULTIKILL_TICKS = 120,
	/* a client reports this long after the game ends: the host sends its
	statistics twice a second, and every one of them once a second */
	CLIENT_REPORT_DELAY_MILLISECONDS = 3000,
	/* a client that was in the game from within this many seconds of its
	start saw all of it */
	JOINED_AT_START_SECONDS = 15,
	/* the longest weapon key kept (a damage effect's tag path) */
	WEAPON_KEY_LENGTH = 64,
};

/* the medals, by the site's keys (halo.milenko.org's site.js, MEDALS) */
enum
{
	_medal_double_kill,
	_medal_triple_kill,
	_medal_killtacular,
	_medal_killtrocity,
	_medal_killing_spree,
	_medal_killing_frenzy,
	_medal_running_riot,
	_medal_rampage,
	_medal_beat_down,
	_medal_sniper_kill,
	_medal_grenade_stick,
	/* (no splatter or assassin: they need their damage tags' names, or a
	way to tell a kill from behind, confirmed in a real game first) */
	NUMBER_OF_MEDALS
};

static char const *const medal_keys[NUMBER_OF_MEDALS] = {
	"double_kill", "triple_kill", "killtacular", "killtrocity", "killing_spree", "killing_frenzy",
	"running_riot", "rampage", "beat_down", "sniper_kill", "grenade_stick",
};

/* ---------- structures */

struct stats_weapon
{
	long definition_index;
	short kills;
};

struct stats_player
{
	boolean used;
	boolean departed;
	/* quit, but kept in the game (the scoreboard keeps players who quit:
	display.show_quit_players); last_tick is when */
	boolean quit;
	short identifier;
	wchar_t name[12];
	long team_index;
	short color;
	long first_tick;
	long last_tick;
	/* the statistics as they stood last (a departed player's: when they left) */
	struct game_statistics statistics;
	/* from the kills this machine saw */
	short feed_kills;
	short spree;
	short best_spree;
	short chain;
	long last_kill_tick;
	short multikills;
	short medals[NUMBER_OF_MEDALS];
	short weapon_count;
	struct stats_weapon weapons[MAXIMUM_PLAYER_WEAPONS];
	/* kills by damage of no tag this machine knew (a client's copy that
	died before the killing blow came) */
	short unknown_weapon_kills;
};

/* ---------- globals */

static struct
{
	boolean recording;
	long last_tick;
	/* the tick this machine's recording started at (a client that joined
	late saw only what came after) */
	long first_tick;
	struct stats_player players[MAXIMUM_STATS_PLAYERS];
	short departed_count;
	struct stats_player departed[MAXIMUM_DEPARTED_PLAYERS];
	/* the damage of the kill being counted (damage.c) */
	long kill_damage;
	boolean over;
	unsigned long over_time;
	boolean client_reported;
} game_stats = { FALSE, 0, 0 };

/* ---------- private code */

static boolean stats_network_game(
	void)
{
	return global_network_game_server_get() != NULL || global_network_game_client_get() != NULL;
}

static void stats_reset(
	long tick)
{
	csmemset(game_stats.players, 0, sizeof(game_stats.players));
	csmemset(game_stats.departed, 0, sizeof(game_stats.departed));
	game_stats.departed_count = 0;
	game_stats.recording = TRUE;
	game_stats.first_tick = tick;
	game_stats.last_tick = tick;
	game_stats.over = FALSE;
	game_stats.client_reported = FALSE;
}

/* a new game (the game's time went back: a new map, or the next game) */
static void stats_check_game(
	void)
{
	long tick = game_time_get();

	if (!game_stats.recording || tick < game_stats.last_tick)
		stats_reset(tick);
	game_stats.last_tick = tick;
}

static void stats_depart(
	struct stats_player *player)
{
	if (!player->used)
		return;
	/* (one who played at all: kept with what they had) */
	if (game_stats.departed_count < MAXIMUM_DEPARTED_PLAYERS &&
		(player->statistics.kills[0] || player->statistics.deaths || player->statistics.assists[0] ||
			player->last_tick - player->first_tick >= 10 * TICKS_PER_SECOND))
	{
		game_stats.departed[game_stats.departed_count] = *player;
		game_stats.departed[game_stats.departed_count].departed = TRUE;
		game_stats.departed_count++;
	}
	csmemset(player, 0, sizeof(*player));
}

/* the player of an index, as tracked (NULL for none) */
static struct stats_player *stats_player(
	long player_index)
{
	long absolute_index;

	if (player_index == NONE)
		return NULL;
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	if (absolute_index < 0 || absolute_index >= MAXIMUM_STATS_PLAYERS)
		return NULL;
	return &game_stats.players[absolute_index];
}

/* the players as they are now: new ones taken, those gone kept as departed */
static void stats_track_players(
	void)
{
	boolean seen[MAXIMUM_STATS_PLAYERS];
	struct data_iterator iterator;
	struct player_datum *player;
	long tick = game_time_get();
	long index;

	csmemset(seen, 0, sizeof(seen));
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct stats_player *tracked = stats_player(iterator.datum_index);

		if (!tracked)
			continue;
		/* (another player in the same place: the one before left) */
		if (tracked->used && (tracked->identifier != player->identifier ||
			csmemcmp(tracked->name, player->name, sizeof(tracked->name))))
		{
			stats_depart(tracked);
		}
		/* (one who quit and is kept in the game is still a line of the
		report, with when they left: not a departed player too) */
		if (player->quit_out_of_game)
		{
			if (tracked->used)
			{
				tracked->quit = TRUE;
				seen[DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index)] = TRUE;
			}
			continue;
		}
		if (!tracked->used)
		{
			tracked->used = TRUE;
			tracked->identifier = player->identifier;
			csmemcpy(tracked->name, player->name, sizeof(tracked->name));
			tracked->first_tick = tick;
		}
		seen[DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index)] = TRUE;
		tracked->team_index = player->team_index;
		tracked->color = player->network_player_data.primary_color_index;
		tracked->statistics = player->statistics;
		tracked->last_tick = tick;
	}
	for (index = 0; index < MAXIMUM_STATS_PLAYERS; index++)
	{
		if (game_stats.players[index].used && !seen[index])
			stats_depart(&game_stats.players[index]);
	}
}

static char const *stats_weapon_name(
	long definition_index)
{
	char const *name = definition_index != NONE ? tag_get_name(definition_index) : NULL;

	return name ? name : "";
}

/* whether a damage tag's path begins with prefix (any case, either slash) */
static boolean stats_weapon_is(
	char const *name,
	char const *prefix)
{
	for (; *prefix; name++, prefix++)
	{
		char a = *name == '/' ? '\\' : *name;
		char b = *prefix == '/' ? '\\' : *prefix;

		if (a >= 'A' && a <= 'Z')
			a = (char)(a - 'A' + 'a');
		if (!a || a != b)
			return FALSE;
	}
	return TRUE;
}

static boolean stats_weapon_has(
	char const *name,
	char const *part)
{
	return strstr(name, part) != NULL;
}

static void stats_medal(
	struct stats_player *player,
	short medal)
{
	if (player->medals[medal] < SHRT_MAX)
		player->medals[medal]++;
}

/* a kill as the killer's counts have it */
static void stats_credit_kill(
	struct stats_player *killer,
	long definition_index,
	long tick)
{
	char const *weapon = stats_weapon_name(definition_index);
	short index;

	killer->feed_kills++;
	/* a spree: kills without dying */
	killer->spree++;
	if (killer->spree > killer->best_spree)
		killer->best_spree = killer->spree;
	switch (killer->spree)
	{
	case 5: stats_medal(killer, _medal_killing_spree); break;
	case 10: stats_medal(killer, _medal_killing_frenzy); break;
	case 15: stats_medal(killer, _medal_running_riot); break;
	case 20: stats_medal(killer, _medal_rampage); break;
	}
	/* a multikill: each kill within the window of the one before */
	if (killer->chain > 0 && tick - killer->last_kill_tick <= MULTIKILL_TICKS)
		killer->chain++;
	else
		killer->chain = 1;
	killer->last_kill_tick = tick;
	if (killer->chain >= 2)
	{
		stats_medal(killer, (short)(_medal_double_kill + MIN(killer->chain, 5) - 2));
		killer->multikills++;
	}
	/* the weapon (the damage's tag) */
	if (!weapon[0])
	{
		killer->unknown_weapon_kills++;
		return;
	}
	if (stats_weapon_has(weapon, "melee"))
		stats_medal(killer, _medal_beat_down);
	if (stats_weapon_is(weapon, "weapons\\sniper rifle\\"))
		stats_medal(killer, _medal_sniper_kill);
	if (stats_weapon_is(weapon, "weapons\\plasma grenade\\attached"))
		stats_medal(killer, _medal_grenade_stick);
	for (index = 0; index < killer->weapon_count; index++)
	{
		if (killer->weapons[index].definition_index == definition_index)
			break;
	}
	if (index == killer->weapon_count)
	{
		if (killer->weapon_count == MAXIMUM_PLAYER_WEAPONS)
		{
			killer->unknown_weapon_kills++;
			return;
		}
		killer->weapons[index].definition_index = definition_index;
		killer->weapons[index].kills = 0;
		killer->weapon_count++;
	}
	killer->weapons[index].kills++;
}

/* text appended to a buffer, as far as it fits */
static long stats_append(
	char *text,
	long size,
	long used,
	char const *format,
	...)
{
	va_list arguments;
	long written;

	if (used >= size - 1)
		return used;
	va_start(arguments, format);
	written = vsnprintf(text + used, (size_t)(size - used), format, arguments);
	va_end(arguments);
	if (written < 0)
		return used;
	return MIN(used + written, size - 1);
}

/* a tag's path as a JSON string's body (backslashes escaped) */
static long stats_append_path(
	char *text,
	long size,
	long used,
	char const *path)
{
	long length = 0;

	for (; *path && length < WEAPON_KEY_LENGTH && used < size - 3; path++, length++)
	{
		char character = *path;

		if (character == '\\' || character == '"')
			text[used++] = '\\';
		if ((unsigned char)character < 0x20)
			character = ' ';
		text[used++] = character;
	}
	text[used] = 0;
	return used;
}

/* whether the kills this machine saw of a player add up to the game's own
count (the host's statistics): on the host always, on a client if it saw
every one */
static boolean stats_feed_complete(
	struct stats_player const *player)
{
	return player->feed_kills == player->statistics.kills[0];
}

/* a player's recorded parts, JSON members (into text) */
static long stats_player_members(
	struct stats_player const *player,
	char *text,
	long size,
	long used)
{
	short index;
	boolean any;

	used = stats_append(text, size, used, "\"joined\": %ld", player->first_tick / TICKS_PER_SECOND);
	if (player->quit && !player->departed)
		used = stats_append(text, size, used, ", \"left\": %ld", player->last_tick / TICKS_PER_SECOND);
	if (!stats_feed_complete(player))
		return stats_append(text, size, used, ", \"feed_incomplete\": true");
	used = stats_append(text, size, used, ", \"best_spree\": %d, \"medals\": {", player->best_spree);
	for (index = 0, any = FALSE; index < NUMBER_OF_MEDALS; index++)
	{
		if (!player->medals[index])
			continue;
		used = stats_append(text, size, used, "%s\"%s\": %d", any ? ", " : "", medal_keys[index], player->medals[index]);
		any = TRUE;
	}
	used = stats_append(text, size, used, "}, \"weapons\": {");
	for (index = 0, any = FALSE; index < player->weapon_count; index++)
	{
		used = stats_append(text, size, used, "%s\"", any ? ", " : "");
		used = stats_append_path(text, size, used, stats_weapon_name(player->weapons[index].definition_index));
		used = stats_append(text, size, used, "\": %d", player->weapons[index].kills);
		any = TRUE;
	}
	if (player->unknown_weapon_kills)
		used = stats_append(text, size, used, "%s\"\": %d", any ? ", " : "", player->unknown_weapon_kills);
	return stats_append(text, size, used, "}");
}

/* a departed player, as a JSON object */
static long stats_departed_json(
	struct stats_player const *player,
	char *text,
	long size,
	long used)
{
	char name[96];

	browser_json_name(name, sizeof(name), (unsigned short const *)player->name, 12);
	used = stats_append(text, size, used,
		"{\"name\": %s, \"team\": %ld, \"color\": %d, \"kills\": %d, \"assists\": %d, \"deaths\": %d, "
		"\"betrayals\": %d, \"suicides\": %d, \"shots_fired\": %ld, \"shots_hit\": %ld, \"multikills\": %d, "
		"\"left\": %ld, ",
		name, player->team_index, player->color, player->statistics.kills[0], player->statistics.assists[0],
		player->statistics.deaths, player->statistics.friendly_fire_kills, player->statistics.suicides,
		player->statistics.shots_fired, player->statistics.shots_hit,
		stats_feed_complete(player) ? player->multikills : player->statistics.multiple_kills,
		player->last_tick / TICKS_PER_SECOND);
	used = stats_player_members(player, text, size, used);
	return stats_append(text, size, used, "}");
}

/* a client's report of the game it joined, sent once it has its last
statistics from the host */
static void stats_client_report(
	void)
{
	static struct browser_report_player players[MAXIMUM_STATS_PLAYERS];
	static char extra[BROWSER_REPORT_GAME_EXTRA_SIZE];
	struct network_game_client *client = global_network_game_client_get();
	struct network_game *game = client ? network_game_client_get_game(client) : NULL;
	struct game_variant *variant = game_engine_get_variant();
	boolean teams = variant->universal_variant.teams;
	char name[96], host[96];
	long count;
	long used = 0;
	struct player_datum const *reporter = NULL;
	struct data_iterator iterator;
	struct player_datum *player;

	if (!game)
		return;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (player->local_player_index != NONE)
		{
			reporter = player;
			break;
		}
	}
	/* (a machine whose players all left: nothing of its own to say) */
	if (!reporter)
		return;
	count = game_engine_report_lines(players, MAXIMUM_STATS_PLAYERS);
	if (count <= 0)
		return;
	browser_json_name(name, sizeof(name), (unsigned short const *)reporter->name, 12);
	browser_json_name(host, sizeof(host), (unsigned short const *)game->name, 16);
	used = stats_append(extra, sizeof(extra), used,
		"\"map\": \"");
	used = stats_append_path(extra, sizeof(extra), used, game->map.name);
	used = stats_append(extra, sizeof(extra), used,
		"\", \"engine\": %d, \"score_limit\": %d, \"host\": %s, \"reporter\": %s, \"client_joined\": %ld, "
		"\"build\": \"ChupathingyCE %s\", \"network_version\": %d, ",
		(int)variant->game_engine_index, (int)variant->universal_variant.score_to_win, host, name,
		game_stats.first_tick / TICKS_PER_SECOND, updater_version(), (int)HALO_PORT_NETWORK_VERSION);
	game_stats_game_extra(FALSE, extra + used, (long)sizeof(extra) - used);
	browser_client_report(
		teams,
		teams ? game_engine_get_team_score(0) : 0,
		teams ? game_engine_get_team_score(1) : 0,
		game_time_get() / TICKS_PER_SECOND,
		players,
		(int)count,
		extra);
	platform_log("Game list: the joined game's report is on its way (%ld players)", count);
}

/* ---------- public code */

/* damage.c: the damage of the kill about to be counted (NONE once it is) */
void game_stats_kill_damage(
	long definition_index)
{
	game_stats.kill_damage = definition_index;
}

/* game_engine_player_killed: a player died, killed by killing_player_index
(NONE for no player's kill), a betrayal or not, as this machine's game has
it (a client's: the host's killer) */
void game_stats_player_killed(
	long killing_player_index,
	long dead_player_index,
	boolean friendly_fire)
{
	struct stats_player *dead;
	struct stats_player *killer;
	long tick;

	if (!stats_network_game() || !game_engine_can_score())
		return;
	stats_check_game();
	stats_track_players();
	tick = game_time_get();
	dead = stats_player(dead_player_index);
	killer = killing_player_index != dead_player_index ? stats_player(killing_player_index) : NULL;
	if (dead && dead->used)
	{
		dead->spree = 0;
		dead->chain = 0;
	}
	if (killer && killer->used && !friendly_fire)
		stats_credit_kill(killer, game_stats.kill_damage, tick);
	game_stats.kill_damage = NONE;
}

/* a report line's recorded parts (JSON members, into text): when the player
joined, and, if every kill of theirs was seen, their medals, weapons and best
spree; multikills, the count of multikills where every kill was seen (the
game's own count otherwise) */
void game_stats_player_extra(
	long player_index,
	char *text,
	long size,
	short *multikills)
{
	struct stats_player *player = stats_player(player_index);

	text[0] = 0;
	if (!game_stats.recording || !player || !player->used)
		return;
	if (stats_feed_complete(player))
		*multikills = player->multikills;
	stats_player_members(player, text, size, 0);
}

/* a report's recorded parts (JSON members, into text): who left before the
end, and how each part was known (host: the host's own; synced: the host's,
as every client is sent them; feed: worked out from the kills this machine
saw) */
void game_stats_game_extra(
	boolean host,
	char *text,
	long size)
{
	long used = 0;
	short index;

	text[0] = 0;
	if (!game_stats.recording)
		return;
	used = stats_append(text, size, used,
		"\"stats\": 1, \"sources\": {\"statistics\": \"%s\", \"team_scores\": \"%s\", \"medals\": \"%s\", "
		"\"weapons\": \"%s\", \"departed\": \"%s\"}, \"departed\": [",
		host ? "host" : "synced", host ? "host" : "synced", host ? "host" : "feed", host ? "host" : "feed",
		host ? "host" : "synced");
	for (index = 0; index < game_stats.departed_count; index++)
	{
		if (index)
			used = stats_append(text, size, used, ", ");
		used = stats_departed_json(&game_stats.departed[index], text, size, used);
	}
	stats_append(text, size, used, "]");
}

void game_stats_update(
	void)
{
	boolean over;

	if (!game_engine_running() || !stats_network_game())
	{
		game_stats.recording = FALSE;
		return;
	}
	stats_check_game();
	over = !game_engine_can_score();
	if (!over)
	{
		stats_track_players();
		game_stats.over = FALSE;
		return;
	}
	if (!game_stats.over)
	{
		game_stats.over = TRUE;
		game_stats.over_time = system_milliseconds();
	}
	/* a client's report, once the host's last statistics are in */
	if (!game_stats.client_reported && game_connection() == _game_connection_network_client &&
		system_milliseconds() - game_stats.over_time >= CLIENT_REPORT_DELAY_MILLISECONDS)
	{
		game_stats.client_reported = TRUE;
		stats_client_report();
	}
}

#endif
