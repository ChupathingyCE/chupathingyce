/*
DEDICATED.C

The dedicated server (server/README.md): with HALO_DEDICATED naming a
playlist file in the data folder (playlists/slayer.txt, beside maps), the
game hosts system link games by itself, one playlist
entry after another, with no player of its own. Built into the game browser's
builds (configure.py --game-browser); without HALO_DEDICATED it does
nothing.

Each frame (main.c, beside the user interface) the director:
  - waits for the main menu to be up, and no movie playing (the intro's
    end loads the main menu anew, which ends any network game);
  - hosts as the game's fast set-up does (player_ui.c,
    player_ui_fast_setup_network_server): the game server, its client on
    this machine with no player, and the pregame lobby's screen;
  - sets the entry's map and game type (the menu's automation:
    game_engine_get_variant_by_name);
  - starts the pregame countdown once enough players have joined (a
    lobby's players start it, and the server has none; it may run when
    server_ok_to_countdown, which lets the host's machine go without a
    player: dedicated_server_active);
  - after each game, back in the pregame, sets the next entry;
  - leaves a team entry for the next one without teams while a single
    player waits (a team game needs players on both teams; joining players
    are put on the smaller team, network_server_manager.c).
The game list (port/linux/src/browser.c) lists the game as it does any
hosted game, and a finished game's carnage report goes out as usual.

The playlist: one entry a line, a map (its name, "bloodgulch", or its path)
and a game type (game_engine_get_variant_by_name's names: slayer,
team_slayer, ctf, king, oddball, race, ...); # starts a comment.
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "game/game_engine.h"
#include "interface/player_ui.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "text/unicode.h"
#include "networking/network_server_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	MAXIMUM_ENTRIES = 64,
	/* a failed start tried again this much later */
	RETRY_MILLISECONDS = 5000,
};

/* (network_server_manager_internal.h's, and the menus') */
word network_game_server_get_state(struct network_game_server *server, short *state_data);
struct network_game *network_game_server_get_game(struct network_game_server *server);
void network_game_server_change_map_name(struct network_game_server *server, char const *map_name);
void network_game_server_change_game_variant(struct network_game_server *server, struct game_variant *variant);
void network_game_server_pause_countdown(struct network_game_server *server, boolean pause_countdown);
void network_game_server_dedicated_start_countdown(struct network_game_server *server);
void network_game_accept_remote_connections(boolean accept);
void game_engine_playlist_initialize(void);
void game_engine_playlist_begin(void);
void game_connection_set(short connection);
void main_set_multiplayer_map_name(char const *map_name);
void game_engine_override_map_name(char const *map_name);
boolean main_menu_is_active(void);
boolean bink_playback_active(void);

enum
{
	/* network_server_manager.c's server states */
	DEDICATED_SERVER_STATE_PREGAME = 0,
};

/* ---------- globals */

static struct
{
	boolean initialized;
	boolean active;
	long entry_count;
	char maps[MAXIMUM_ENTRIES][128];
	char variants[MAXIMUM_ENTRIES][32];
	long entry;
	long minimum_players;
	long maximum_players;
	wchar_t name[16];

	boolean hosting;
	boolean entry_set;
	boolean entry_teams;
	word last_state;
	unsigned long retry_time;
} dedicated;

/* ---------- private code */

/* the playlist, in the data folder (the game's d:, beside maps) */
static void load_playlist(
	char const *name)
{
	char path[256];
	FILE *file;
	char line[256];
	char *cursor;

	snprintf(path, sizeof(path), "d:\\%s", name);
	for (cursor = path; *cursor; cursor++)
	{
		if (*cursor == '/')
			*cursor = '\\';
	}
	file = fopen(path, "r");

	if (!file)
	{
		error(_error_silent, "dedicated: cannot read the playlist %s", path);
		return;
	}
	while (fgets(line, sizeof(line), file) && dedicated.entry_count < MAXIMUM_ENTRIES)
	{
		char map[128], variant[32];
		char *comment = strchr(line, '#');

		if (comment)
			*comment = 0;
		if (sscanf(line, "%127s %31s", map, variant) != 2)
			continue;
		/* (a bare name is a multiplayer level's: levels\test\<name>\<name>) */
		if (!strchr(map, '\\'))
			snprintf(dedicated.maps[dedicated.entry_count], sizeof(dedicated.maps[0]), "levels\\test\\%s\\%s", map, map);
		else
			snprintf(dedicated.maps[dedicated.entry_count], sizeof(dedicated.maps[0]), "%s", map);
		snprintf(dedicated.variants[dedicated.entry_count], sizeof(dedicated.variants[0]), "%s", variant);
		dedicated.entry_count++;
	}
	fclose(file);
	error(_error_silent, "dedicated: %ld playlist entries from %s", dedicated.entry_count, path);
}

static void initialize(
	void)
{
	char const *playlist = getenv("HALO_DEDICATED");
	char const *minimum = getenv("HALO_DEDICATED_MINIMUM_PLAYERS");
	char const *maximum = getenv("HALO_DEDICATED_MAXIMUM_PLAYERS");
	char const *name = getenv("HALO_DEDICATED_NAME");
	long index;

	dedicated.initialized = TRUE;
	if (!playlist || !playlist[0])
		return;
	load_playlist(playlist);
	dedicated.minimum_players = minimum ? atol(minimum) : 1;
	if (dedicated.minimum_players < 1)
		dedicated.minimum_players = 1;
	/* (12 while the server is tested) */
	dedicated.maximum_players = maximum ? atol(maximum) : 12;
	if (dedicated.maximum_players < dedicated.minimum_players)
		dedicated.maximum_players = dedicated.minimum_players;
	if (!name || !name[0])
		name = "Dedicated";
	for (index = 0; index < 15 && name[index]; index++)
		dedicated.name[index] = (wchar_t)(unsigned char)name[index];
	dedicated.name[index] = 0;
	dedicated.active = dedicated.entry_count > 0;
}

/* whether an entry's game type is played in teams */
static boolean entry_has_teams(
	long entry)
{
	struct game_variant variant;

	game_engine_get_variant_by_name(&variant, dedicated.variants[entry]);
	return variant.universal_variant.teams ? TRUE : FALSE;
}

/* a team game cannot start with one player (it needs a player on each of two
teams: server_needs_more_teams): the next entry played alone instead */
static void skip_team_entry_for_one_player(
	struct network_game *game)
{
	long offset;

	if (!dedicated.entry_teams || !game || game->player_count != 1)
		return;
	for (offset = 1; offset < dedicated.entry_count; offset++)
	{
		long entry = (dedicated.entry + offset) % dedicated.entry_count;

		if (!entry_has_teams(entry))
		{
			error(_error_silent, "dedicated: one player: %s instead of %s", dedicated.variants[entry],
				dedicated.variants[dedicated.entry]);
			dedicated.entry = entry;
			dedicated.entry_set = FALSE;
			return;
		}
	}
}

/* the playlist's entry, set on the server (in its pregame) */
static boolean set_entry(
	struct network_game_server *server)
{
	struct game_variant variant;
	struct game_variant empty;
	char const *map = dedicated.maps[dedicated.entry];
	char const *variant_name = dedicated.variants[dedicated.entry];

	csmemset(&empty, 0, sizeof(empty));
	game_engine_get_variant_by_name(&variant, variant_name);
	if (!csmemcmp(&variant, &empty, sizeof(variant)))
	{
		error(_error_silent, "dedicated: no game type %s; skipping the entry", variant_name);
		dedicated.entry = (dedicated.entry + 1) % dedicated.entry_count;
		return FALSE;
	}
	main_set_multiplayer_map_name(map);
	game_engine_override_map_name(map);
	network_game_server_change_map_name(server, map);
	player_ui_set_game_variant(&variant);
	network_game_server_change_game_variant(server, &variant);
	dedicated.entry_teams = variant.universal_variant.teams ? TRUE : FALSE;
	error(_error_silent, "dedicated: next %s on %s", variant_name, map);
	return TRUE;
}

/* hosting as the game's fast set-up does (player_ui.c): the server, its
client on this machine (without a player) and the pregame lobby's screen,
as a host that chose Create Game ends up */
static boolean host(
	void)
{
	player_ui_fast_setup_network_server();
	if (!global_network_game_server_get() || !global_network_game_client_get())
	{
		error(_error_silent, "dedicated: could not host; trying again");
		return FALSE;
	}
	return TRUE;
}

/* ---------- public code */

boolean dedicated_server_active(
	void)
{
	if (!dedicated.initialized)
		initialize();
	return dedicated.active;
}

void dedicated_server_update(
	void)
{
	struct network_game_server *server;
	word state;

	if (!dedicated_server_active())
		return;
	server = global_network_game_server_get();
	if (!server)
	{
		dedicated.hosting = FALSE;
		dedicated.entry_set = FALSE;
		/* (not while a movie plays: the intro's end loads the main menu,
		which ends any network game) */
		if (!main_menu_is_active() || bink_playback_active() || system_milliseconds() < dedicated.retry_time)
			return;
		dedicated.retry_time = system_milliseconds() + RETRY_MILLISECONDS;
		dedicated.hosting = host();
		return;
	}

	state = network_game_server_get_state(server, NULL);
	if (state == DEDICATED_SERVER_STATE_PREGAME)
	{
		/* (back from a game: the next entry) */
		if (dedicated.last_state != DEDICATED_SERVER_STATE_PREGAME && dedicated.entry_set)
		{
			dedicated.entry = (dedicated.entry + 1) % dedicated.entry_count;
			dedicated.entry_set = FALSE;
		}
		if (!dedicated.entry_set)
			dedicated.entry_set = set_entry(server);
		if (dedicated.entry_set)
			skip_team_entry_for_one_player(network_game_server_get_game(server));
		if (!dedicated.entry_set)
			dedicated.entry_set = set_entry(server);
		if (dedicated.entry_set)
		{
			struct network_game *game = network_game_server_get_game(server);

			/* its name on the lists, and its seats */
			if (game)
			{
				ustrncpy(game->name, dedicated.name, NUMBEROF(game->name) - 1);
				game->name[NUMBEROF(game->name) - 1] = 0;
				if (game->maximum_players != dedicated.maximum_players)
					game->maximum_players = (byte)dedicated.maximum_players;
				/* (a game's lobby wants two players, network_game_manager.c:
				the server's own would have been one) */
				if (game->minimum_players != dedicated.minimum_players)
					game->minimum_players = (byte)dedicated.minimum_players;
			}
			/* the countdown, started once it may (enough players, teams): a
			lobby's players start it, and the server has none of its own */
			network_game_server_pause_countdown(server,
				!game || game->player_count < dedicated.minimum_players);
			network_game_server_dedicated_start_countdown(server);
		}
	}
	dedicated.last_state = state;
}

#endif
