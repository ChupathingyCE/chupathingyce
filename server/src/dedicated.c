/*
DEDICATED.C

The dedicated server (server/README.md): with HALO_DEDICATED naming a
playlist file, the game hosts system link games by itself, one playlist
entry after another, with no player of its own. Built into the game browser's
builds (configure.py --game-browser); without HALO_DEDICATED it does
nothing.

Each frame (main.c, beside the user interface) the director:
  - waits for the main menu to be up;
  - hosts as the Multiplayer menu's Create Game does
    (ui_widget_event_handler_functions.c, network_game_start_new_server):
    the game server, and its client on this machine with no player;
  - sets the entry's map and game type (the menu's automation:
    game_engine_get_variant_by_name);
  - lets the pregame countdown run once enough players have joined (the
    server counts down by itself when it may: server_ok_to_countdown, which
    lets the host's machine go without a player: dedicated_server_active);
  - after each game, back in the pregame, sets the next entry.
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
void network_game_accept_remote_connections(boolean accept);
void game_engine_playlist_initialize(void);
void game_engine_playlist_begin(void);
void game_connection_set(short connection);
void main_set_multiplayer_map_name(char const *map_name);
void game_engine_override_map_name(char const *map_name);
boolean main_menu_is_active(void);

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
	word last_state;
	unsigned long retry_time;
} dedicated;

/* ---------- private code */

static void load_playlist(
	char const *path)
{
	FILE *file = fopen(path, "r");
	char line[256];

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
	error(_error_silent, "dedicated: next %s on %s", variant_name, map);
	return TRUE;
}

/* hosting as the menu's Create Game does, without a player */
static boolean host(
	void)
{
	boolean result = TRUE;

	dispose_global_network_game_client();
	player_ui_clear_multiplayer_variant();
	network_game_accept_remote_connections(TRUE);
	if (!global_network_game_server_get())
	{
		game_engine_playlist_initialize();
		result = create_global_network_game_server();
		if (result)
		{
			network_game_server_pause_countdown(global_network_game_server_get(), TRUE);
			game_engine_playlist_begin();
			game_connection_set(2);
		}
	}
	if (result && !global_network_game_client_get())
		result = create_global_network_game_client();
	if (!result)
	{
		dispose_global_network_game_server();
		dispose_global_network_game_client();
		network_game_accept_remote_connections(FALSE);
		player_ui_clear_multiplayer_variant();
		error(_error_silent, "dedicated: could not host; trying again");
	}
	return result;
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
		if (!main_menu_is_active() || system_milliseconds() < dedicated.retry_time)
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
		{
			struct network_game *game = network_game_server_get_game(server);

			/* its name on the lists, and its seats */
			if (game)
			{
				ustrncpy(game->name, dedicated.name, NUMBEROF(game->name) - 1);
				game->name[NUMBEROF(game->name) - 1] = 0;
				if (game->maximum_players != dedicated.maximum_players)
					game->maximum_players = (byte)dedicated.maximum_players;
			}
			/* the countdown runs by itself once it may (enough players, teams) */
			network_game_server_pause_countdown(server,
				!game || game->player_count < dedicated.minimum_players);
		}
	}
	dedicated.last_state = state;
}

#endif
