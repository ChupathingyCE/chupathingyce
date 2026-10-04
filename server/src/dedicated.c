/*
DEDICATED.C

The dedicated server (server/README.md): with HALO_DEDICATED naming a
playlist file in the data folder (playlists/slayer.txt, beside maps), the
game hosts system link games by itself, one playlist
entry after another, with no player of its own. Built into the game browser's
builds (configure.py --game-browser); without HALO_DEDICATED it does
nothing.

It runs without a window: nothing drawn (d3d8_gl.c), no sound, no movies,
no display needed (SDL's dummy drivers), so it runs on a server with no
screen. Each frame (main.c, beside the user interface) the director:
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
  - ends a game nobody has scored in for a while, or once everyone has
    left it (game_engine_end_game, as the score limit does);
  - after each game, once the carnage report has shown a while, goes back
    to the pregame lobby (the host's A = pick game, game_engine.c) and sets
    the next entry;
  - leaves a team entry for the next one without teams while a single
    player waits (a team game needs players on both teams; joining players
    are put on the smaller team, network_server_manager.c).
The game list (port/linux/src/browser.c) lists the game as it does any
hosted game, and a finished game's carnage report goes out as usual. Unless
HALO_DEDICATED_PUBLIC is false, the game is public too: its signed listing
is published on internet play's brokers (port/linux/src/p2p_lobby.c), so it
shows in every OpenCE and ChupathingyCE server browser (Join Game > Server
Browser), and is withdrawn when the server stops.

The playlist: one entry a line, a map (its name, "bloodgulch", its path, a
Custom Edition map in maps\ce as <name>@ce, "timberland@ce", or a HaloMD map
in md_maps as <name>@md, "phoenix3_15@md": halo_map_families.h) and a game
type (game_engine_get_variant_by_name's names: slayer, team_slayer, ctf,
king, oddball, race, ...); # starts a comment.
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
	/* the carnage report shown this long before the next game's lobby */
	POSTGAME_MILLISECONDS = 20000,
	/* a game with no players left ended this long after the last left */
	EMPTY_GAME_MILLISECONDS = 30000,
	/* a frame at most this often: with nothing drawn, no display's refresh
	paces the main loop (60 a second, twice the game's ticks) */
	FRAME_MILLISECONDS = 16,
};

/* (network_server_manager_internal.h's, and the menus') */
word network_game_server_get_state(struct network_game_server *server, short *state_data);
struct network_game *network_game_server_get_game(struct network_game_server *server);
void network_game_server_change_map_name(struct network_game_server *server, char const *map_name);
void network_game_server_change_game_variant(struct network_game_server *server, struct game_variant *variant);
void network_game_server_pause_countdown(struct network_game_server *server, boolean pause_countdown);
void network_game_server_dedicated_start_countdown(struct network_game_server *server);
/* (internet play's: port/linux/src/p2p.c, p2p_lobby.c) */
void p2p_set_hosting_allowed(int allowed);
void p2p_set_hosting_public(int public);
void network_game_accept_remote_connections(boolean accept);
void game_engine_playlist_initialize(void);
void game_engine_playlist_begin(void);
void game_connection_set(short connection);
void main_set_multiplayer_map_name(char const *map_name);
void game_engine_override_map_name(char const *map_name);
long game_engine_total_score(void);
boolean main_menu_is_active(void);
boolean bink_playback_active(void);

enum
{
	/* network_server_manager.c's server states */
	DEDICATED_SERVER_STATE_PREGAME = 0,
	DEDICATED_SERVER_STATE_INGAME = 1,
	DEDICATED_SERVER_STATE_POSTGAME = 2,
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
	/* (minutes without a score; 0: none) */
	long idle_limit;
	wchar_t name[16];
	/* listed in the server browser (HALO_DEDICATED_PUBLIC) */
	boolean public_game;

	boolean hosting;
	boolean entry_set;
	boolean entry_teams;
	word last_state;
	unsigned long retry_time;
	unsigned long postgame_time;
	unsigned long frame_time;
	unsigned long score_time;
	long score;
	unsigned long empty_time;
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
		/* (a bare name is a multiplayer level's: levels\test\<name>\<name>;
		a Custom Edition or HaloMD map's, <name>@ce or <name>@md, stays bare,
		as the menus' map list plays it: cache_files_windows.c) */
		if (!strchr(map, '\\') && !strchr(map, '@'))
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
	char const *idle_limit = getenv("HALO_DEDICATED_IDLE_LIMIT");
	char const *public_game = getenv("HALO_DEDICATED_PUBLIC");
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
	/* (a game nobody scores in ends: 5 minutes unless set) */
	dedicated.idle_limit = idle_limit ? atol(idle_limit) : 5;
	if (dedicated.idle_limit < 0)
		dedicated.idle_limit = 0;
	if (!name || !name[0])
		name = "Dedicated";
	for (index = 0; index < 15 && name[index]; index++)
		dedicated.name[index] = (wchar_t)(unsigned char)name[index];
	dedicated.name[index] = 0;
	/* (public unless false, 0, no or off) */
	dedicated.public_game = !public_game || !public_game[0] ||
		!(!_stricmp(public_game, "false") || !strcmp(public_game, "0") || !_stricmp(public_game, "no") ||
			!_stricmp(public_game, "off"));
	error(_error_silent, "dedicated: %s game (HALO_DEDICATED_PUBLIC)", dedicated.public_game ? "a public" :
		"not a public");
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
	/* (an internet game, reached through its invite; listed in the server
	browser if public) */
	p2p_set_hosting_allowed(TRUE);
	p2p_set_hosting_public(dedicated.public_game);
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

	/* (the rest of the frame waits: the server spins otherwise) */
	{
		unsigned long now = system_milliseconds();
		unsigned long elapsed = now - dedicated.frame_time;

		if (dedicated.frame_time && elapsed < FRAME_MILLISECONDS)
			Sleep(FRAME_MILLISECONDS - elapsed);
		dedicated.frame_time = system_milliseconds();
	}

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
	else if (state == DEDICATED_SERVER_STATE_INGAME)
	{
		struct network_game *game = network_game_server_get_game(server);
		unsigned long now = system_milliseconds();

		if (dedicated.last_state != DEDICATED_SERVER_STATE_INGAME)
		{
			dedicated.score_time = now;
			dedicated.score = 0;
			dedicated.empty_time = 0;
		}
		/* everyone left: the game ends (as the score limit ends it) and the
		next entry's lobby opens */
		if (game && game->player_count == 0)
		{
			if (!dedicated.empty_time)
				dedicated.empty_time = now;
			else if (now - dedicated.empty_time >= EMPTY_GAME_MILLISECONDS && game_engine_running() && game_engine_can_score())
			{
				error(_error_silent, "dedicated: everyone left; ending the game");
				game_engine_end_game();
			}
		}
		else
			dedicated.empty_time = 0;
		/* nobody has scored for a while (idle players, or no one fighting):
		the game ends rather than sitting there */
		if (game_engine_running())
		{
			long score = game_engine_total_score();

			if (score != dedicated.score)
			{
				dedicated.score = score;
				dedicated.score_time = now;
			}
			else if (dedicated.idle_limit && game_engine_can_score() &&
				now - dedicated.score_time >= (unsigned long)dedicated.idle_limit * 60000)
			{
				error(_error_silent, "dedicated: no score in %ld minutes; ending the game", dedicated.idle_limit);
				game_engine_end_game();
			}
		}
	}
	else if (state == DEDICATED_SERVER_STATE_POSTGAME)
	{
		/* (the host's A on the carnage report: the server has no one to press it) */
		if (dedicated.last_state != DEDICATED_SERVER_STATE_POSTGAME)
			dedicated.postgame_time = system_milliseconds() + POSTGAME_MILLISECONDS;
		else if ((long)(system_milliseconds() - dedicated.postgame_time) >= 0)
		{
			error(_error_silent, "dedicated: back to the lobby");
			network_game_server_reset_to_pregame(server);
		}
	}
	dedicated.last_state = state;
}

#endif
