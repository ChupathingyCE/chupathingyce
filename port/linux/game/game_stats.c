/*
GAME_STATS.C

The game list's statistics recorder (configure.py --game-browser,
HALO_GAME_BROWSER; port/linux/src/browser.c; on a console,
HALO_CONSOLE_GAME_REPORTS: port/console/reports). It watches the game and
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
game whose host does not report is recorded too. A console sends only that
one (console_reports_joined_game): it keeps fewer players who left
(MAXIMUM_DEPARTED_PLAYERS), whose lines in a client's report are the
scoreboard's numbers anyway.

Called each frame from the main loop (main.c).
*/

#ifdef HALO_PORT_GAME_REPORTS

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "interface/hud_messaging.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_client_manager.h"
#include "tag_files/tag_files.h"
#include "../src/browser.h"
#include "../src/game_report.h"
#include "../src/medal_rules.h"
#ifdef HALO_GAME_BROWSER
#include "interface/player_ui.h"
#include "saved games/player_profile.h"
#include "../src/service_record.h"
#endif

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* game_engine.c's */
long game_engine_report_lines(struct browser_report_player *players, long maximum);
struct game_variant *game_engine_get_variant(void);
#ifdef HALO_GAME_BROWSER
/* Delta Stats' recorder (game_events.c) */
void game_events_update(void);
void game_events_player_killed(long killing_player_index, long dead_player_index, boolean friendly_fire);
#define stats_send_client_report browser_client_report
#else
/* a console's report of the game (port/console/reports/console_reports.h),
as browser_client_report, and its build's name ("Warthog 0.8.1d") */
int console_reports_joined_game(int teams, int red_score, int blue_score, int duration_seconds,
	const struct browser_report_player *players, int count, const char *extra);
const char *console_reports_build(void);
#define stats_send_client_report console_reports_joined_game
#endif
#if !defined(HALO_GAME_BROWSER) && defined(HALO_CONSOLE_GAME_CLAIMS)
/* a console's claim of its own line, hosted or joined
(port/console/reports/console_claims.h) */
int console_claims_game_ended(int hosted, const char *invite, const char *map, const unsigned short *host_name,
	int host_length, const unsigned short *player_name, int name_length, int team, int score, int kills, int deaths,
	int duration_seconds);
/* internet play's (port/linux/src/p2p.c) */
int p2p_joined_invite(char *text, int size);
#endif
#if defined(HALO_GAME_BROWSER) || defined(HALO_CONSOLE_GAME_CLAIMS)
/* the hosted game (network_server_manager_internal.h) */
struct network_game *network_game_server_get_game(struct network_game_server *server);
#endif
/* Delta Peer's (delta_peer_game.c): a player's platform, 0 if their machine
is not on Chupathingy */
int delta_peer_player_platform(int player_index);
/* this file's, game_engine.c's too */
void game_stats_game_extra(boolean host, char *text, long size);
/* the platform layer's */
void platform_log(char const *format, ...);
int config_boolean(char const *name);
#ifdef HALO_GAME_BROWSER
const char *updater_version(void);
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);
#endif

/* ---------- constants */

enum
{
	MAXIMUM_STATS_PLAYERS = HALO_PORT_MAXIMUM_NETWORK_PLAYERS,
	/* players who left before the end, kept (a console's fewer: its
memory, and a client's report has them as scoreboard lines) */
#ifdef HALO_GAME_BROWSER
	MAXIMUM_DEPARTED_PLAYERS = 32,
#else
	MAXIMUM_DEPARTED_PLAYERS = 8,
#endif
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

/* (the medals: medal_rules.h's, by the site's keys. No Splatter or
Assassin here: they need the damage's category, or a way to tell a kill
from behind, which only the host's Delta Stats has: event_log.c) */

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
	/* on Chupathingy (this machine's own, or Delta Peer told the platform):
	only these players' lines go in full in the host's report */
	boolean delta;
	wchar_t name[12];
	long team_index;
	short color;
	long first_tick;
	long last_tick;
	/* the statistics as they stood last (a departed player's: when they left) */
	struct game_statistics statistics;
	/* from the kills this machine saw: sprees and multikills
	(medal_rules.h), and the medals they made */
	short feed_kills;
	struct medal_player medal;
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
	/* the damage of the kill being counted (damage.c; NONE: unknown, as for
	a client's first kill, which damage.c did not see) */
	long kill_damage;
	boolean over;
	unsigned long over_time;
	boolean client_reported;
	/* a console's claim of its line (console_claims_game_ended) */
	boolean console_claimed;
	/* the game's first kill (First Strike) */
	struct medal_game medal_game;
#ifdef HALO_GAME_BROWSER
	/* this machine's players' games added to their service records */
	boolean service_recorded;
#endif
} game_stats = { .recording = FALSE, .kill_damage = NONE };

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
	game_stats.console_claimed = FALSE;
	medal_game_reset(&game_stats.medal_game);
#ifdef HALO_GAME_BROWSER
	game_stats.service_recorded = FALSE;
#endif
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
			medal_player_reset(&tracked->medal);
			tracked->identifier = player->identifier;
			csmemcpy(tracked->name, player->name, sizeof(tracked->name));
			tracked->first_tick = tick;
		}
		seen[DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index)] = TRUE;
		tracked->team_index = player->team_index;
		tracked->delta = tracked->delta || player->local_player_index != NONE ||
			delta_peer_player_platform((int)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index)) > 0;
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

/* a medal of this machine's own player, on their HUD (display.medals, off
by default: retail Halo shows none) */
static void stats_medal_popup(
	long player_index,
	int medal)
{
	struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;
	char const *name = medal_rules_name(medal);
	wchar_t text[48];
	short index;

	if (!player || player->local_player_index == NONE || !config_boolean("display.medals"))
		return;
	for (index = 0; name[index] && index < (short)(NUMBEROF(text) - 1); index++)
		text[index] = (wchar_t)(unsigned char)name[index];
	text[index] = 0;
	hud_print_message(player->local_player_index, text);
}

/* what a kill's damage was, for its medals (MEDAL_KILL_*): its tag's path */
static unsigned int stats_kill_kinds(
	char const *weapon)
{
	unsigned int kinds = 0;

	if (!weapon[0])
		return 0;
	if (stats_weapon_has(weapon, "melee"))
		kinds |= MEDAL_KILL_MELEE;
	if (stats_weapon_is(weapon, "weapons\\sniper rifle\\"))
		kinds |= MEDAL_KILL_SNIPER;
	if (stats_weapon_is(weapon, "weapons\\shotgun\\"))
		kinds |= MEDAL_KILL_SHOTGUN;
	if (stats_weapon_is(weapon, "weapons\\plasma grenade\\attached"))
		kinds |= MEDAL_KILL_STICK;
	return kinds;
}

/* a death as the players' counts have it: the dead player's sprees end,
and a kill that counts (not a betrayal or a suicide) is the killer's, with
its medals (medal_rules.h) and its weapon */
static void stats_count_kill(
	struct stats_player *killer,
	long killer_index,
	struct stats_player *dead,
	long dead_index,
	boolean counts,
	long definition_index,
	long tick)
{
	char const *weapon = stats_weapon_name(definition_index);
	struct player_datum *killer_player = killer_index != NONE ? player_get(killer_index) : NULL;
	unsigned int kinds = stats_kill_kinds(weapon);
	int medals[MEDAL_RULES_MAXIMUM_PER_KILL];
	int count, medal;
	short index;

	/* (dead as the blow landed: a grenade or a rocket of theirs) */
	if (killer_player && killer_player->unit_index == NONE)
		kinds |= MEDAL_KILL_FROM_GRAVE;
	count = medal_rules_kill(&game_stats.medal_game, killer ? &killer->medal : NULL,
		(int)DATUM_INDEX_TO_ABSOLUTE_INDEX(killer_index), dead ? &dead->medal : NULL,
		dead_index != NONE ? (int)DATUM_INDEX_TO_ABSOLUTE_INDEX(dead_index) : -1, counts && killer, tick, kinds, medals);
	if (!counts || !killer)
		return;
	killer->feed_kills++;
	for (medal = 0; medal < count; medal++)
	{
		if (killer->medals[medals[medal]] < SHRT_MAX)
			killer->medals[medals[medal]]++;
		if (medals[medal] >= MEDAL_DOUBLE_KILL && medals[medal] <= MEDAL_KILLIONAIRE)
			killer->multikills++;
		stats_medal_popup(killer_index, medals[medal]);
	}
	/* the weapon (the damage's tag) */
	if (!weapon[0])
	{
		killer->unknown_weapon_kills++;
		return;
	}
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
		/* (and ASCII only: a map's tag paths may be any bytes, which would
		not be the report's UTF-8) */
		if ((unsigned char)character >= 0x7F)
			character = '?';
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
	used = stats_append(text, size, used, ", \"best_spree\": %d, \"medals\": {", player->medal.best_spree);
	for (index = 0, any = FALSE; index < NUMBER_OF_MEDALS; index++)
	{
		if (!player->medals[index])
			continue;
		used = stats_append(text, size, used, "%s\"%s\": %d", any ? ", " : "", medal_rules_key(index), player->medals[index]);
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

/* a departed player, as a JSON object: in full only a Chupathingy player's,
on the host's report (full); anyone else's is their name, team, kills and
deaths */
static long stats_departed_json(
	struct stats_player const *player,
	boolean full,
	char *text,
	long size,
	long used)
{
	char name[96];

	game_report_json_name(name, sizeof(name), (unsigned short const *)player->name, BROWSER_PLAYER_NAME_LENGTH);
	if (!full)
	{
		return stats_append(text, size, used, "{\"name\": %s, \"team\": %ld, \"kills\": %d, \"deaths\": %d}",
			name, player->team_index, player->statistics.kills[0], player->statistics.deaths);
	}
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

/* the game's report lines, as game_engine_report_lines gives them (a
client's report, a console's claim: one after the other, on the main thread) */
static struct browser_report_player stats_lines[MAXIMUM_STATS_PLAYERS];

#if defined(HALO_GAME_BROWSER) && !defined(HALO_CONSOLE_NO_DELTA_PROFILE)
/* port/linux/game/profile_names.c's */
boolean profile_names_local_guest(long local_player);
#endif

/* whether a local player's Halo profile plays as a guest (not connected to
Delta: profile_names_local_guest) */
static boolean stats_local_guest(
	short local_player_index)
{
#if defined(HALO_GAME_BROWSER) && !defined(HALO_CONSOLE_NO_DELTA_PROFILE)
	return profile_names_local_guest(local_player_index);
#else
	(void)local_player_index;
	return FALSE;
#endif
}

/* a client's report of the game it joined, sent once it has its last
statistics from the host */
static void stats_client_report(
	void)
{
	struct browser_report_player *players = stats_lines;
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
		if (player->local_player_index != NONE && !stats_local_guest(player->local_player_index))
		{
			reporter = player;
			break;
		}
	}
	/* (a machine whose players all left, or whose players are all guests,
	not connected to Delta: nothing of its own to say, nor anything to put
	on a profile) */
	if (!reporter)
		return;
	count = game_engine_report_lines(players, MAXIMUM_STATS_PLAYERS);
	if (count <= 0)
		return;
	game_report_json_name(name, sizeof(name), (unsigned short const *)reporter->name, BROWSER_PLAYER_NAME_LENGTH);
	game_report_json_name(host, sizeof(host), (unsigned short const *)game->name, 16);
	used = stats_append(extra, sizeof(extra), used,
		"\"map\": \"");
	used = stats_append_path(extra, sizeof(extra), used, game->map.name);
	used = stats_append(extra, sizeof(extra), used,
		"\", \"engine\": %d, \"score_limit\": %d, \"host\": %s, \"reporter\": %s, \"client_joined\": %ld, "
		"\"build\": \"%s%s\", \"network_version\": %d, ",
		(int)variant->game_engine_index, (int)variant->universal_variant.score_to_win, host, name,
		game_stats.first_tick / TICKS_PER_SECOND,
#ifdef HALO_GAME_BROWSER
		"ChupathingyCE ", updater_version(),
#else
		"", console_reports_build(),
#endif
		delta_legacy_announce());
	game_stats_game_extra(FALSE, extra + used, (long)sizeof(extra) - used);
	/* (told only when one is sent: none without a list server, with
	reports of joined games off, or for a game not joined by its invite) */
	if (stats_send_client_report(
		teams,
		teams ? game_engine_get_team_score(0) : 0,
		teams ? game_engine_get_team_score(1) : 0,
		game_time_get() / TICKS_PER_SECOND,
		players,
		(int)count,
		extra))
	{
		platform_log("Game list: the joined game's report is on its way (%ld players)", count);
	}
}

#if !defined(HALO_GAME_BROWSER) && defined(HALO_CONSOLE_GAME_CLAIMS)
/* a console's claim of its own line in the game that ended, hosted or
joined: its first local player's line, as the game's report lines have it
(the scoreboard's team, score, kills and deaths, which the site matches) */
static void stats_console_claim(
	void)
{
	struct browser_report_player *lines = stats_lines;
	boolean hosted = game_connection() == _game_connection_network_server;
	struct network_game_server *server = hosted ? global_network_game_server_get() : NULL;
	struct network_game_client *client = hosted ? NULL : global_network_game_client_get();
	struct network_game *game = server ? network_game_server_get_game(server) :
		client ? network_game_client_get_game(client) : NULL;
	struct player_datum const *claimant = NULL;
	struct data_iterator iterator;
	struct player_datum *player;
	char invite[BROWSER_INVITE_LENGTH + 1];
	long count, index;

	if (!game)
		return;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		/* (the console's first player: the one its profile is) */
		if (player->local_player_index == 0)
		{
			claimant = player;
			break;
		}
	}
	if (!claimant)
		return;
	count = game_engine_report_lines(lines, MAXIMUM_STATS_PLAYERS);
	/* (its line: game_engine_report_lines copies each player's name whole) */
	for (index = 0; index < count; index++)
	{
		if (!csmemcmp(lines[index].name, claimant->name, sizeof(lines[index].name)))
			break;
	}
	if (index >= count)
		return;
	if (hosted || !p2p_joined_invite(invite, sizeof(invite)))
		invite[0] = 0;
	console_claims_game_ended(hosted, invite, game->map.name, (unsigned short const *)game->name, 16,
		lines[index].name, BROWSER_PLAYER_NAME_LENGTH, lines[index].team, lines[index].score, lines[index].kills,
		lines[index].deaths, (int)(game_time_get() / TICKS_PER_SECOND));
}
#endif

#ifdef HALO_GAME_BROWSER
/* the game that ended, added to the service record of each of this
machine's players (service_record.h, in their Halo profile's folder): their
line as the report has it (the host's statistics, which every client is
sent), whether they won as the site counts a win (the best team, else first
place), and the medals and weapons of the kills this machine saw */
static void stats_service_record(
	void)
{
	struct browser_report_player *lines = stats_lines;
	struct network_game_server *server = global_network_game_server_get();
	struct network_game_client *client = server ? NULL : global_network_game_client_get();
	struct network_game *network = server ? network_game_server_get_game(server) :
		client ? network_game_client_get_game(client) : NULL;
	boolean teams = game_engine_get_variant()->universal_variant.teams;
	long red = teams ? game_engine_get_team_score(0) : 0;
	long blue = teams ? game_engine_get_team_score(1) : 0;
	struct data_iterator iterator;
	struct player_datum *player;
	long count;

	if (!network)
		return;
	count = game_engine_report_lines(lines, MAXIMUM_STATS_PLAYERS);
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		static struct service_record record;
		struct stats_player *tracked = stats_player(iterator.datum_index);
		struct service_record_weapon weapons[MAXIMUM_PLAYER_WEAPONS];
		struct service_record_game game;
		char folder[512], path[1024];
		long profile, index;
		short medal;

		if (player->local_player_index == NONE || !tracked || !tracked->used)
			continue;
		for (index = 0; index < count; index++)
		{
			if (!csmemcmp(lines[index].name, player->name, sizeof(lines[index].name)))
				break;
		}
		profile = player_ui_get_active_player_profile_index(player->local_player_index);
		/* (the first player's: the profile used last, as DELTA shows it) */
		if (profile == NONE && player->local_player_index == 0)
			profile = player_ui_get_player1_last_used_profile_index();
		if (index >= count || profile == NONE || !player_profile_get_enclosing_directory_path(profile, folder))
			continue;
		platform_translate_path(folder, path, sizeof(path));
		if (strlen(path) + 1 + strlen(SERVICE_RECORD_FILE_NAME) >= sizeof(path))
			continue;
		if (path[0] && path[strlen(path) - 1] != '/' && path[strlen(path) - 1] != '\\')
			strcat(path, "/");
		strcat(path, SERVICE_RECORD_FILE_NAME);
		csmemset(&game, 0, sizeof(game));
		game.map = network->map.name;
		if (teams)
			game.won = red != blue && lines[index].team == (red > blue ? 0 : 1);
		else
			game.won = lines[index].place == 1;
		game.kills = lines[index].kills;
		game.deaths = lines[index].deaths;
		game.assists = lines[index].assists;
		game.betrayals = lines[index].betrayals;
		game.suicides = lines[index].suicides;
		game.seconds = (int)((tracked->last_tick - tracked->first_tick) / TICKS_PER_SECOND);
		game.best_spree = tracked->medal.best_spree;
		for (medal = 0; medal < NUMBER_OF_MEDALS; medal++)
			game.medals[medal] = tracked->medals[medal];
		for (medal = 0; medal < tracked->weapon_count; medal++)
		{
			weapons[medal].path = stats_weapon_name(tracked->weapons[medal].definition_index);
			weapons[medal].kills = tracked->weapons[medal].kills;
		}
		game.weapons = weapons;
		game.weapon_count = tracked->weapon_count;
		/* (a record that would not read is left alone, not written over) */
		if (!service_record_load(path, &record))
		{
			platform_log("Service record: a profile's record could not be read; this game is not added");
			continue;
		}
		service_record_add_game(&record, &game);
		if (service_record_save(path, &record))
			platform_log("Service record: game %ld added", record.games);
		else
			platform_log("Service record: could not be saved");
	}
}
#endif

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
#ifdef HALO_GAME_BROWSER
	/* (Delta Stats' recorder, when this machine hosts: game_events.c) */
	game_events_player_killed(killing_player_index, dead_player_index, friendly_fire);
#endif
	stats_check_game();
	stats_track_players();
	tick = game_time_get();
	dead = stats_player(dead_player_index);
	killer = killing_player_index != dead_player_index ? stats_player(killing_player_index) : NULL;
	stats_count_kill(killer && killer->used ? killer : NULL, killing_player_index, dead && dead->used ? dead : NULL,
		dead_player_index, !friendly_fire, game_stats.kill_damage, tick);
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

/* whether a report line goes in full (else only the scoreboard's numbers:
browser_report_player's minimal): this machine's own players' always; on
the host's report (host), a Chupathingy player's too; on a client's, no one
else's */
boolean game_stats_line_full(
	long player_index,
	boolean host)
{
	struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;
	struct stats_player *tracked = stats_player(player_index);

	if (!player)
		return FALSE;
	if (player->local_player_index != NONE)
		return TRUE;
	return host && ((tracked && tracked->used && tracked->delta) ||
		delta_peer_player_platform((int)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index)) > 0);
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
	/* the game type: the host's engine (a client's report has it already),
	the gametype's name, and a Bomb game's own members (docs/bomb.md,
	Reports) */
	{
		char name[96];
#ifdef HALO_GAME_BROWSER
		char bomb[256];
#endif

		game_report_json_name(name, sizeof(name), (unsigned short const *)game_engine_get_variant()->human_readable_game_description,
			12);
		if (host)
			used = stats_append(text, size, used, "\"engine\": %d, ", (int)game_engine_get_variant()->game_engine_index);
		used = stats_append(text, size, used, "\"gametype\": %s, ", name);
#ifdef HALO_GAME_BROWSER
		if (game_engine_bomb_report_game(bomb, sizeof(bomb)))
			used = stats_append(text, size, used, "%s, ", bomb);
#endif
	}
	used = stats_append(text, size, used,
		"\"stats\": 1, \"sources\": {\"statistics\": \"%s\", \"team_scores\": \"%s\", \"medals\": \"%s\", "
		"\"weapons\": \"%s\", \"departed\": \"%s\"}, \"departed\": [",
		host ? "host" : "synced", host ? "host" : "synced", host ? "host" : "feed", host ? "host" : "feed",
		host ? "host" : "synced");
	for (index = 0; index < game_stats.departed_count; index++)
	{
		long before = used;

		if (index)
			used = stats_append(text, size, used, ", ");
		used = stats_departed_json(&game_stats.departed[index], host && game_stats.departed[index].delta, text, size, used);
		/* (a player who doesn't fit is left out whole, with room kept for
		the closing bracket: a report cut off mid-player isn't JSON, and the
		site turns it away) */
		if (used >= size - 2)
		{
			text[before] = 0;
			used = before;
			break;
		}
	}
	stats_append(text, size, used, "]");
}

void game_stats_update(
	void)
{
	boolean over;

#ifdef HALO_GAME_BROWSER
	game_events_update();
#endif

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
#ifdef HALO_GAME_BROWSER
	/* this machine's players' service records, once the host's last
	statistics are in */
	if (!game_stats.service_recorded &&
		(game_connection() == _game_connection_network_client || game_connection() == _game_connection_network_server) &&
		system_milliseconds() - game_stats.over_time >= CLIENT_REPORT_DELAY_MILLISECONDS)
	{
		game_stats.service_recorded = TRUE;
		stats_service_record();
	}
#endif
#if !defined(HALO_GAME_BROWSER) && defined(HALO_CONSOLE_GAME_CLAIMS)
	/* a console's claim of its own line, hosted or joined, once its report
	(a joined game's) is on its way */
	if (!game_stats.console_claimed &&
		(game_connection() == _game_connection_network_client || game_connection() == _game_connection_network_server) &&
		system_milliseconds() - game_stats.over_time >= CLIENT_REPORT_DELAY_MILLISECONDS)
	{
		game_stats.console_claimed = TRUE;
		stats_console_claim();
	}
#endif
}

#endif
