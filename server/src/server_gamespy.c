/*
SERVER_GAMESPY.C

The game's side of the GameSpy-style status queries and master heartbeats
(server/platform/gamespy_listing.c; server/docs/settings.md, "GameSpy
status queries"): off unless gamespy_listing is on. Each second the
server's state goes to the listing's thread: its name, its map as Halo
Custom Edition names it (an Xbox map as xbox_<map>, a Custom Edition or
Halo PC map by its own name: the naming the Custom Edition community uses,
as ugoboom passed on), its game type, its seats and its players.

The version reported (gamever) is this server's own unless
HALO_DEDICATED_GAMESPY_VERSION sets another. Custom Edition 1.0.10's server
browser shows only servers of its own version (01.00.10.0621), so by
default the server is in the masters' list but not in that browser, where
it could not be joined.

Only in the server program (HALO_SERVER): the listing's unit is the
server's own.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "game/game_engine.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_server_manager.h"
#include "dedicated.h"
#include "server_admin.h"
#include "server_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HALO_SERVER

/* (the server program's listing unit, built with the host's ABI: plain
types only) */
#include "../platform/gamespy_listing.h"

/* (network_server_manager_internal.h's) */
word network_game_server_get_state(struct network_game_server *server, short *state_data);
struct network_game *network_game_server_get_game(struct network_game_server *server);
/* (the updater's: this build's version) */
const char *updater_version(void);

enum
{
	/* the state handed over this often */
	PUBLISH_MILLISECONDS = 1000,
	/* a start that failed tried again this much later */
	RETRY_MILLISECONDS = 60000,
	PLAYER_NAME_SIZE = 32,
};

static char const default_masters[] = "s1.master.hosthpc.com:27900";

static struct
{
	boolean configured;
	boolean enabled;
	long port;
	boolean failed;
	unsigned long failed_time;
	unsigned long publish_time;
} gamespy;

/* ---------- private code */

static boolean text_true(
	char const *text)
{
	return !(!_stricmp(text, "false") || !strcmp(text, "0") || !_stricmp(text, "no") || !_stricmp(text, "off"));
}

static void configure_from_settings(
	void)
{
	static struct server_settings settings;
	char const *enabled = getenv(server_settings_table[SERVER_SETTING_GAMESPY_LISTING].environment);
	char const *port = getenv(server_settings_table[SERVER_SETTING_GAMESPY_PORT].environment);

	if (!server_admin_read_settings(&settings))
		csmemset(&settings, 0, sizeof(settings));
	gamespy.enabled = FALSE;
	if (enabled && enabled[0])
		gamespy.enabled = text_true(enabled);
	else if (settings.set[SERVER_SETTING_GAMESPY_LISTING])
		gamespy.enabled = settings.values[SERVER_SETTING_GAMESPY_LISTING] ? TRUE : FALSE;
	gamespy.port = GAMESPY_DEFAULT_PORT;
	if (port && port[0])
		gamespy.port = atol(port);
	else if (settings.set[SERVER_SETTING_GAMESPY_PORT])
		gamespy.port = settings.values[SERVER_SETTING_GAMESPY_PORT];
	gamespy.configured = TRUE;
}

static char const *game_type_name(
	long engine)
{
	switch (engine)
	{
	case SERVER_ENGINE_CTF: return "CTF";
	case SERVER_ENGINE_SLAYER: return "Slayer";
	case SERVER_ENGINE_ODDBALL: return "Oddball";
	case SERVER_ENGINE_KING: return "King";
	case SERVER_ENGINE_RACE: return "Race";
	case SERVER_ENGINE_BOMB: return "Bomb";
	}
	return "";
}

static void gather(
	struct gamespy_status *status)
{
	static char names[GAMESPY_MAXIMUM_PLAYERS * PLAYER_NAME_SIZE];
	static long scores[GAMESPY_MAXIMUM_PLAYERS], pings[GAMESPY_MAXIMUM_PLAYERS], teams[GAMESPY_MAXIMUM_PLAYERS];
	struct dedicated_status server_status;
	struct network_game_server *server = global_network_game_server_get();
	struct network_game *game = server ? network_game_server_get_game(server) : NULL;
	struct game_variant const *variant;
	long count;
	long index;

	csmemset(status, 0, sizeof(*status));
	dedicated_server_get_status(&server_status);
	snprintf(status->hostname, sizeof(status->hostname), "%s", server_status.name);
	gamespy_map_name(server_status.map, status->mapname, sizeof(status->mapname));
	snprintf(status->version, sizeof(status->version), "%s", updater_version());
	status->maxplayers = (int)server_status.maximum_players;
	status->playing = server_status.state == _dedicated_state_in_game;
	/* (the game type of the entry played, or next in the lobby; else the
	network game's) */
	{
		static struct game_variant resolved;
		static struct game_variant_options options;
		char problem[160];

		variant = NULL;
		if (server_status.variant[0] &&
			server_gametype_resolve(server_status.variant, &resolved, &options, problem, sizeof(problem)))
		{
			variant = &resolved;
		}
		else if (game)
			variant = &game->variant;
	}
	if (variant)
	{
		snprintf(status->gametype, sizeof(status->gametype), "%s", game_type_name(variant->game_engine_index));
		for (index = 0; index < (long)NUMBEROF(variant->human_readable_game_description) &&
			index < (long)sizeof(status->gamevariant) - 1 && variant->human_readable_game_description[index]; index++)
		{
			wchar_t character = variant->human_readable_game_description[index];

			status->gamevariant[index] = character >= 0x20 && character < 0x7f ? (char)character : '?';
		}
		status->gamevariant[index] = 0;
		/* (a built-in's has no title of its own: the playlist's name for it) */
		if (!status->gamevariant[0])
			snprintf(status->gamevariant, sizeof(status->gamevariant), "%s", server_status.variant);
		status->teamplay = variant->universal_variant.teams ? 1 : 0;
		status->fraglimit = (int)variant->universal_variant.score_to_win;
	}
	if (game)
	{
		status->maxplayers = game->maximum_players;
		status->numplayers = game->player_count;
	}
	count = server_commands_player_rows(names, PLAYER_NAME_SIZE, scores, pings, teams, GAMESPY_MAXIMUM_PLAYERS);
	for (index = 0; index < count; index++)
	{
		struct gamespy_player *player = &status->players[index];

		snprintf(player->name, sizeof(player->name), "%s", names + index * PLAYER_NAME_SIZE);
		player->score = (int)scores[index];
		player->ping = pings[index] == NONE ? -1 : (int)pings[index];
		player->team = (int)teams[index];
	}
	status->player_count = (int)count;
	if (status->numplayers < status->player_count)
		status->numplayers = status->player_count;
}

static void stop_at_exit(
	void)
{
	gamespy_listing_stop();
}

static void start(
	void)
{
	char const *masters = getenv("HALO_DEDICATED_GAMESPY_MASTERS");
	char const *version = getenv("HALO_DEDICATED_GAMESPY_VERSION");
	char problem[160];
	static boolean exit_registered = FALSE;

	/* (set but empty: no masters, the queries answered alone) */
	if (!masters)
		masters = default_masters;
	if (!version || !version[0])
		version = updater_version();
	if (!gamespy_listing_start((int)gamespy.port, masters, version, problem, sizeof(problem)))
	{
		error(_error_silent, "gamespy: not started: %s", problem);
		gamespy.failed = TRUE;
		gamespy.failed_time = system_milliseconds();
		return;
	}
	gamespy.failed = FALSE;
	if (!exit_registered)
	{
		exit_registered = TRUE;
		atexit(stop_at_exit);
	}
	error(_error_silent, "gamespy: answering status queries on UDP port %ld, version %s; %s", gamespy.port, version,
		masters[0] ? "heartbeats to the masters" : "no masters");
}

/* ---------- public code */

void server_gamespy_update(
	void)
{
	char notice[160];

	if (!gamespy.configured)
		configure_from_settings();
	while (gamespy_listing_next_notice(notice, sizeof(notice)))
		error(_error_silent, "%s", notice);
	if (!gamespy.enabled)
	{
		if (gamespy_listing_running())
		{
			gamespy_listing_stop();
			error(_error_silent, "gamespy: stopped");
		}
		return;
	}
	if (!gamespy_listing_running())
	{
		if (gamespy.failed && system_milliseconds() - gamespy.failed_time < RETRY_MILLISECONDS)
			return;
		start();
		if (!gamespy_listing_running())
			return;
		gamespy.publish_time = 0;
	}
	if (!gamespy.publish_time || system_milliseconds() - gamespy.publish_time >= PUBLISH_MILLISECONDS)
	{
		static struct gamespy_status status;

		gamespy.publish_time = system_milliseconds();
		gather(&status);
		gamespy_listing_publish(&status);
	}
}

void server_gamespy_configure(
	boolean enabled,
	long port)
{
	if (!gamespy.configured)
		configure_from_settings();
	if (gamespy_listing_running() && (!enabled || port != gamespy.port))
	{
		gamespy_listing_stop();
		error(_error_silent, "gamespy: stopped");
	}
	gamespy.enabled = enabled;
	gamespy.port = port;
	gamespy.failed = FALSE;
}

boolean server_gamespy_enabled(
	void)
{
	if (!gamespy.configured)
		configure_from_settings();
	return gamespy.enabled;
}

long server_gamespy_port(
	void)
{
	if (!gamespy.configured)
		configure_from_settings();
	return gamespy.port;
}

#endif
