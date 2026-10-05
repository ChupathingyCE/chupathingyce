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

The server's commands (server_commands.c: its console, its startup
commands and its control API, server/docs/admin.md) change what the
director does through dedicated.h: a map and game type played at once
(after which the playlist goes on where it was), the next entry at once,
the game ending, and the seats and the name, which the lobby takes.
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
#include "dedicated.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	MAXIMUM_ENTRIES = DEDICATED_MAXIMUM_ENTRIES,
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
boolean network_game_server_lobby_is_open(struct network_game_server *server);
boolean network_game_server_game_is_loading(struct network_game_server *server);
/* (server_commands.c's) */
void server_commands_update(void);

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
	char maps[MAXIMUM_ENTRIES][DEDICATED_MAP_SIZE];
	char variants[MAXIMUM_ENTRIES][DEDICATED_VARIANT_SIZE];
	char playlist[DEDICATED_MAP_SIZE];
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
	unsigned long start_time;

	/* a map and game type a command chose (sv_map): to be played next, and
	being played (the playlist's entry is then the one after it) */
	boolean chosen_pending;
	boolean chosen_playing;
	char chosen_map[DEDICATED_MAP_SIZE];
	char chosen_variant[DEDICATED_VARIANT_SIZE];
	/* the game in progress to end (a command's), and its carnage report not
	waited out when the command wanted the next game at once */
	boolean end_requested;
	boolean postgame_skipped;
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
	dedicated.start_time = system_milliseconds();
	if (!playlist || !playlist[0])
		return;
	snprintf(dedicated.playlist, sizeof(dedicated.playlist), "%s", playlist);
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
	/* (a team game a command chose too: the playlist's goes on, from its
	entry, if that one is played alone) */
	if (dedicated.chosen_playing)
	{
		error(_error_silent, "dedicated: one player: not %s on %s", dedicated.chosen_variant, dedicated.chosen_map);
		dedicated.chosen_playing = FALSE;
		dedicated.entry_set = FALSE;
		if (!entry_has_teams(dedicated.entry))
			return;
	}
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

/* the playlist's entry (or the map and game type a command chose), set on
the server (in its pregame) */
static boolean set_entry(
	struct network_game_server *server)
{
	struct game_variant variant;
	struct game_variant empty;
	char const *map = dedicated.maps[dedicated.entry];
	char const *variant_name = dedicated.variants[dedicated.entry];

	if (dedicated.chosen_pending)
	{
		dedicated.chosen_pending = FALSE;
		dedicated.chosen_playing = TRUE;
	}
	if (dedicated.chosen_playing)
	{
		map = dedicated.chosen_map;
		variant_name = dedicated.chosen_variant;
	}
	csmemset(&empty, 0, sizeof(empty));
	game_engine_get_variant_by_name(&variant, variant_name);
	if (!csmemcmp(&variant, &empty, sizeof(variant)))
	{
		error(_error_silent, "dedicated: no game type %s; skipping the entry", variant_name);
		if (dedicated.chosen_playing)
			dedicated.chosen_playing = FALSE;
		else
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
	/* the commands that came in (the console's, the control API's), here on
	the main thread */
	server_commands_update();

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
		/* (back from a game: the next entry; after a game a command chose,
		the entry that was next before it) */
		if (dedicated.last_state != DEDICATED_SERVER_STATE_PREGAME && dedicated.entry_set)
		{
			if (dedicated.chosen_playing)
				dedicated.chosen_playing = FALSE;
			else
				dedicated.entry = (dedicated.entry + 1) % dedicated.entry_count;
			dedicated.entry_set = FALSE;
		}
		if (dedicated.last_state != DEDICATED_SERVER_STATE_PREGAME)
			dedicated.postgame_skipped = FALSE;
		/* (a command's game, once the lobby takes it) */
		if (dedicated.chosen_pending && dedicated.entry_set && network_game_server_lobby_is_open(server))
			dedicated.entry_set = FALSE;
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
		/* a command ended it (sv_end_game, sv_map, sv_mapcycle_next), once it
		can end */
		if (dedicated.end_requested && game_engine_running() && game_engine_can_score())
		{
			dedicated.end_requested = FALSE;
			error(_error_silent, "dedicated: a command ends the game");
			game_engine_end_game();
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
		/* (the host's A on the carnage report: the server has no one to press
		it; at once when a command wanted the next game) */
		dedicated.end_requested = FALSE;
		if (dedicated.postgame_skipped)
		{
			dedicated.postgame_skipped = FALSE;
			error(_error_silent, "dedicated: back to the lobby at once");
			network_game_server_reset_to_pregame(server);
		}
		else if (dedicated.last_state != DEDICATED_SERVER_STATE_POSTGAME)
			dedicated.postgame_time = system_milliseconds() + POSTGAME_MILLISECONDS;
		else if ((long)(system_milliseconds() - dedicated.postgame_time) >= 0)
		{
			error(_error_silent, "dedicated: back to the lobby");
			network_game_server_reset_to_pregame(server);
		}
	}
	dedicated.last_state = state;
}

/* ---------- the commands' (dedicated.h) */

void dedicated_server_get_status(
	struct dedicated_status *status)
{
	struct network_game_server *server = global_network_game_server_get();
	long index;

	csmemset(status, 0, sizeof(*status));
	status->state = _dedicated_state_starting;
	if (server)
	{
		word state = network_game_server_get_state(server, NULL);

		if (state == DEDICATED_SERVER_STATE_INGAME)
			status->state = _dedicated_state_in_game;
		else if (state == DEDICATED_SERVER_STATE_POSTGAME)
			status->state = _dedicated_state_postgame;
		else if (network_game_server_game_is_loading(server))
			status->state = _dedicated_state_loading;
		else
			status->state = _dedicated_state_lobby;
	}
	status->entry = dedicated.entry;
	status->entry_count = dedicated.entry_count;
	if (dedicated.chosen_playing || (dedicated.chosen_pending && status->state != _dedicated_state_in_game &&
		status->state != _dedicated_state_loading && status->state != _dedicated_state_postgame))
	{
		status->chosen = TRUE;
		snprintf(status->map, sizeof(status->map), "%s", dedicated.chosen_map);
		snprintf(status->variant, sizeof(status->variant), "%s", dedicated.chosen_variant);
	}
	else if (dedicated.entry_count)
	{
		snprintf(status->map, sizeof(status->map), "%s", dedicated.maps[dedicated.entry]);
		snprintf(status->variant, sizeof(status->variant), "%s", dedicated.variants[dedicated.entry]);
	}
	/* (and a command's game to follow the one in progress) */
	if (dedicated.chosen_pending && !status->chosen)
	{
		snprintf(status->next_map, sizeof(status->next_map), "%s", dedicated.chosen_map);
		snprintf(status->next_variant, sizeof(status->next_variant), "%s", dedicated.chosen_variant);
	}
	snprintf(status->playlist, sizeof(status->playlist), "%s", dedicated.playlist);
	status->minimum_players = dedicated.minimum_players;
	status->maximum_players = dedicated.maximum_players;
	status->idle_limit = dedicated.idle_limit;
	status->public_game = dedicated.public_game;
	for (index = 0; index < (long)NUMBEROF(status->name) - 1 && dedicated.name[index]; index++)
		status->name[index] = (char)dedicated.name[index];
	status->name[index] = 0;
	status->uptime_seconds = (system_milliseconds() - dedicated.start_time) / 1000;
}

long dedicated_playlist_count(
	void)
{
	return dedicated.entry_count;
}

char const *dedicated_playlist_map(
	long entry)
{
	return entry >= 0 && entry < dedicated.entry_count ? dedicated.maps[entry] : "";
}

char const *dedicated_playlist_variant(
	long entry)
{
	return entry >= 0 && entry < dedicated.entry_count ? dedicated.variants[entry] : "";
}

/* the game in progress (or loading) to end at once, and the next lobby to
open without waiting out the carnage report; in the lobby, the next entry
(or the command's game) set now */
static void next_game_now(
	void)
{
	struct network_game_server *server = global_network_game_server_get();
	word state;

	if (!server)
		return;
	state = network_game_server_get_state(server, NULL);
	if (state == DEDICATED_SERVER_STATE_PREGAME && network_game_server_lobby_is_open(server))
	{
		dedicated.entry_set = FALSE;
		return;
	}
	dedicated.postgame_skipped = TRUE;
	if (state != DEDICATED_SERVER_STATE_POSTGAME)
		dedicated.end_requested = TRUE;
}

void dedicated_server_play(
	char const *map,
	char const *variant)
{
	/* (a bare name is a multiplayer level's, as the playlist's) */
	if (!strchr(map, '\\') && !strchr(map, '@'))
		snprintf(dedicated.chosen_map, sizeof(dedicated.chosen_map), "levels\\test\\%s\\%s", map, map);
	else
		snprintf(dedicated.chosen_map, sizeof(dedicated.chosen_map), "%s", map);
	snprintf(dedicated.chosen_variant, sizeof(dedicated.chosen_variant), "%s", variant);
	dedicated.chosen_pending = TRUE;
	/* (a game a command chose before, set in the lobby, is replaced; one being
	played ends, and as it ends the playlist's entry stays the one after the
	last it played) */
	{
		struct network_game_server *server = global_network_game_server_get();

		if (server && network_game_server_get_state(server, NULL) == DEDICATED_SERVER_STATE_PREGAME &&
			network_game_server_lobby_is_open(server))
		{
			dedicated.chosen_playing = FALSE;
		}
	}
	next_game_now();
}

void dedicated_server_skip(
	void)
{
	struct network_game_server *server = global_network_game_server_get();

	/* (a command's game waiting or in the lobby: dropped, and the playlist's
	entry it came before played instead) */
	dedicated.chosen_pending = FALSE;
	if (server && network_game_server_get_state(server, NULL) == DEDICATED_SERVER_STATE_PREGAME &&
		network_game_server_lobby_is_open(server))
	{
		if (dedicated.chosen_playing)
			dedicated.chosen_playing = FALSE;
		else if (dedicated.entry_count)
			dedicated.entry = (dedicated.entry + 1) % dedicated.entry_count;
	}
	next_game_now();
}

boolean dedicated_server_end_game(
	void)
{
	struct network_game_server *server = global_network_game_server_get();
	word state;

	if (!server)
		return FALSE;
	state = network_game_server_get_state(server, NULL);
	if (state == DEDICATED_SERVER_STATE_INGAME || network_game_server_game_is_loading(server))
	{
		dedicated.end_requested = TRUE;
		return TRUE;
	}
	return FALSE;
}

void dedicated_server_set_maximum_players(
	long maximum_players)
{
	dedicated.maximum_players = maximum_players;
}

void dedicated_server_set_name(
	char const *name)
{
	long index;

	for (index = 0; index < (long)NUMBEROF(dedicated.name) - 1 && name[index]; index++)
		dedicated.name[index] = (wchar_t)(unsigned char)name[index];
	dedicated.name[index] = 0;
}

#endif
