/*
SERVER_COMMANDS.C

The dedicated server's commands (server/docs/admin.md), named after Halo
PC's dedicated server's where it had one: sv_status, sv_players, sv_kick,
sv_ban, sv_unban, sv_banlist, sv_map, sv_maps, sv_mapcycle,
sv_mapcycle_next, sv_end_game, sv_maxplayers, sv_name, and help (and the
console's own sv_admin_* commands, which server_control.c runs: they are
listed here, and refused anywhere else).

They come from three places, and run here, on the game's main thread, each
frame (dedicated.c), whichever thread they came in on:
  - the startup commands (HALO_DEDICATED_COMMANDS: a file in the data
    folder, a command a line), run once the server first hosts, their
    output in the log;
  - the server's console (its standard input) and its control API (HTTP,
    off unless HALO_DEDICATED_CONTROL is set), which the server program's
    control unit reads on a thread of its own and queues
    (server/platform/server_control.c): the server program's alone
    (HALO_SERVER), not the game's own dedicated mode.
Every command from the console or the control API is logged, with where it
came from (which of the API's credentials), before it runs.

They do what the game's own protocol does: a player kicked or banned is
refused (_message_server_machine_rejected) and dropped, as the game drops a
cheater; a ban is a line in bans.txt, which every join is checked against
(network_distributed.c), by the machine's hardware id and its address; a
map is changed by ending the game and setting the next, as the playlist
does. No message of the protocol is new or changed. Nothing here shows a
player's address.

sv_password is not here: a join request (message_client_join_game_request)
has no password a player could type in, its join token is the game's own,
and no build asks a player for one, so a join password would be a protocol
change.
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_game_protocol.h"
#include "networking/network_server_manager.h"
#include "halo_map_families.h"
#include "command_line.h"
#include "dedicated.h"
#include "delta.h"
#ifdef HALO_SERVER
/* (the server program's control unit, built with the host's ABI: plain
types only) */
#include "../platform/server_control.h"
#endif
#include "server_admin.h"
#include "server_moderation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef HALO_SERVER
/* (the game's own dedicated mode, HALO_DEDICATED in the game: no roles,
audit, link or Delta Control, only the startup commands, the owner's) */
#define server_command_permission(line, changes) ((void)(line), *(changes) = 0, 0u)
#define server_audit(via, actor, role, action, target, reason, ok, detail) ((void)0)
#define server_roles_actor_allowed(actor, retry_after) ((void)(actor), (void)(retry_after), 1)
#define server_role_name(role) ((void)(role), "owner")
#define server_roles_start() ((void)0)
#define server_moderation_update() ((void)0)
#define server_moderation_notice(machine, warning, title, text) ((void)(machine), FALSE)
#define server_moderation_machine_key(machine, key) ((void)(machine), (void)(key), FALSE)
#define server_roles_key_role(key, permissions, who, who_size) 0
#endif

/* ---------- constants */

enum
{
	/* a command's output, at most (a full server's sv_players fits) */
	OUTPUT_SIZE = 64 * 1024,
	/* the startup commands, at most */
	MAXIMUM_STARTUP_COMMANDS = 64,
	/* the bans sv_banlist lists, at most */
	MAXIMUM_LISTED_BANS = 500,
	/* the maps sv_maps lists, at most */
	MAXIMUM_LISTED_MAPS = 512,
};

/* (network_server_manager_internal.h's) */
word network_game_server_get_state(struct network_game_server *server, short *state_data);
struct network_game *network_game_server_get_game(struct network_game_server *server);
/* (network_distributed.c's: the host's bans, bans.txt) */
long distributed_player_ping(short player_index);
void network_distributed_ban_until(long machine_index, unsigned long address, char const *names, char const *reason,
	unsigned long until);
boolean network_distributed_ban_entry(long index, char *when, long when_size, char *hardware_id, long hardware_id_size,
	char *players, long players_size, char *reason, long reason_size, unsigned long *until);
boolean network_distributed_unban(long index);
/* (the version: server_platform.c's, updater.c's in the game) */
const char *updater_version(void);
char const *cache_files_map_directory(void);

/* ---------- structures */

struct server_command
{
	char const *name;
	char const *usage;
	char const *help;
	/* its words, with its name */
	short minimum_words;
	short maximum_words;
	/* only while the server hosts a game */
	boolean needs_game;
	boolean (*execute)(struct command_line const *line, boolean json, struct command_output *output);
};

/* a player of the server's game, as the commands show it */
struct server_player
{
	/* its number in sv_players (from 1: its slot in the game) */
	long number;
	char name[NETWORK_GAME_SERVER_NAME_TEXT_SIZE];
	long machine_index;
	long team_index;
	boolean scored;
	long score;
	long ping;
};

/* ---------- prototypes */

static boolean command_help(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_status(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_players(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_kick(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_ban(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_unban(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_banlist(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_map(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_mapcycle(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_mapcycle_next(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_end_game(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_maxplayers(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_name(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_maps(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_console_only(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_warn(struct command_line const *line, boolean json, struct command_output *output);
static void note_target(char const *target, char const *reason);
static void log_output(char const *output);
static char const *actor_display(void);
#ifdef HALO_SERVER
static boolean command_mod_list(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_mod_add(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_mod_remove(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_link(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_unlink(struct command_line const *line, boolean json, struct command_output *output);
static boolean command_link_status(struct command_line const *line, boolean json, struct command_output *output);
#endif

/* ---------- globals */

static struct server_command const server_commands[] =
{
	{ "help", "help [command]", "Lists the commands, or tells what one does.", 1, 2, FALSE, command_help },
	{ "sv_status", "sv_status", "The server: its name, version, map and game type, players and uptime.", 1, 1, FALSE,
		command_status },
	{ "sv_players", "sv_players", "The players: number, name, team, score, ping and hardware id.", 1, 1, FALSE,
		command_players },
	{ "sv_warn", "sv_warn <player> <reason>", "Warns a player: their game shows the reason, if it is a "
		"ChupathingyCE game (Delta Peer); the warning is in the audit file either way.", 3, 3, TRUE, command_warn },
	{ "sv_kick", "sv_kick <player> [reason]", "Drops a player (a number from sv_players, or a name) and every "
		"player of their machine. They may join again.", 2, 3, TRUE, command_kick },
	{ "sv_ban", "sv_ban <player> [duration] [reason]", "Drops a player and keeps their machine out (by its "
		"hardware id and address, in bans.txt), for ever or for a while: 30m, 2h, 7d, 1d12h.", 2, 4, TRUE,
		command_ban },
	{ "sv_unban", "sv_unban <ban>", "Takes a ban (its number in sv_banlist) out of bans.txt.", 2, 2, FALSE,
		command_unban },
	{ "sv_banlist", "sv_banlist", "The bans in bans.txt: number, when, hardware id, players, how long.", 1, 1, FALSE,
		command_banlist },
	{ "sv_map", "sv_map <map> <game type>", "Plays a map (bloodgulch, a Custom Edition map as name@ce, a HaloMD map as "
		"name@md, a Halo PC map as name@pc) and game type (slayer, ctf, ...) now; then the playlist goes on.", 3, 3, FALSE, command_map },
	{ "sv_maps", "sv_maps", "The maps this server can play (its multiplayer maps: Xbox, name@ce, name@md, name@pc) and "
		"the game types sv_map takes.", 1, 1, FALSE, command_maps },
	{ "sv_mapcycle", "sv_mapcycle", "The playlist, and which entry is played.", 1, 1, FALSE, command_mapcycle },
	{ "sv_mapcycle_next", "sv_mapcycle_next", "Skips to the playlist's next entry now.", 1, 1, TRUE,
		command_mapcycle_next },
	{ "sv_end_game", "sv_end_game", "Ends the game in progress (its carnage report shows, then the next entry).",
		1, 1, TRUE, command_end_game },
	{ "sv_maxplayers", "sv_maxplayers [count]", "Shows or sets the most players a game takes (from the next "
		"lobby, or the lobby now).", 1, 2, FALSE, command_maxplayers },
	{ "sv_name", "sv_name [name]", "Shows or sets the server's name on the lists (15 characters at most).", 1, 2, FALSE,
		command_name },
	/* (the playlists, game types and settings: server_admin.c) */
	{ "sv_playlists", "sv_playlists", "The playlists: the server's own (playlists/) and those made here "
		"(admin/playlists/), and which is played.", 1, 1, FALSE, server_admin_playlists },
	{ "sv_playlist", "sv_playlist <name>", "A playlist's games, and any that cannot be played.", 2, 2, FALSE,
		server_admin_playlist },
	{ "sv_playlist_new", "sv_playlist_new <name> [from]", "Makes a playlist, empty or a copy of another.", 2, 3, FALSE,
		server_admin_playlist_new },
	{ "sv_playlist_add", "sv_playlist_add <name> <map> <game type> [position]", "Adds a game to a playlist (at the "
		"end, or at a position).", 4, 5, FALSE, server_admin_playlist_add },
	{ "sv_playlist_remove", "sv_playlist_remove <name> <number>", "Takes a game out of a playlist.", 3, 3, FALSE,
		server_admin_playlist_remove },
	{ "sv_playlist_move", "sv_playlist_move <name> <from> <to>", "Moves a playlist's game to another position.",
		4, 4, FALSE, server_admin_playlist_move },
	{ "sv_playlist_delete", "sv_playlist_delete <name>", "Deletes a playlist made here (never the server's own).",
		2, 2, FALSE, server_admin_playlist_delete },
	{ "sv_playlist_use", "sv_playlist_use <name>", "Plays a playlist from the next game (in the lobby, now), and "
		"keeps it across restarts.", 2, 2, FALSE, server_admin_playlist_use },
	{ "sv_playlist_save", "sv_playlist_save <name>", "Saves a playlist's whole file (the control API's: POST "
		"/v1/file).", 2, 2, FALSE, server_admin_playlist_save },
	{ "sv_mapcycle_add", "sv_mapcycle_add <map> <game type>", "Adds a game to the end of the playlist played (saved "
		"in admin/playlists).", 3, 3, FALSE, server_admin_mapcycle_add },
	{ "sv_mapcycle_del", "sv_mapcycle_del <number>", "Takes a game out of the playlist played (saved in "
		"admin/playlists).", 2, 2, FALSE, server_admin_mapcycle_del },
	{ "sv_gametypes", "sv_gametypes", "The game types: the built-ins and the files in admin/gametypes.", 1, 1, FALSE,
		server_admin_gametypes },
	{ "sv_gametype", "sv_gametype <name>", "A game type's settings.", 2, 2, FALSE, server_admin_gametype },
	{ "sv_gametype_new", "sv_gametype_new <name> <base>", "Makes a game type file from a built-in or another.",
		3, 3, FALSE, server_admin_gametype_new },
	{ "sv_gametype_set", "sv_gametype_set <name> <setting> <value>", "Changes a game type file's setting "
		"(sv_gametype lists them).", 4, 4, FALSE, server_admin_gametype_set },
	{ "sv_gametype_delete", "sv_gametype_delete <name>", "Deletes a game type file no playlist plays.", 2, 2, FALSE,
		server_admin_gametype_delete },
	{ "sv_gametype_save", "sv_gametype_save <name>", "Saves a game type's whole file (the control API's: POST "
		"/v1/file).", 2, 2, FALSE, server_admin_gametype_save },
	{ "sv_settings", "sv_settings", "The settings: each one's value, and where it comes from.", 1, 1, FALSE,
		server_admin_settings },
	{ "sv_set", "sv_set <setting> <value>", "Changes a setting and saves it (admin/settings.toml).", 3, 3, FALSE,
		server_admin_set },
#ifdef HALO_SERVER
	/* (Delta Control's roles: server/docs/moderation.md) */
	{ "sv_mod_list", "sv_mod_list", "The moderators: the moderators file's, the control panel's accounts bound to "
		"a game, and the site's.", 1, 1, FALSE, command_mod_list },
	{ "sv_mod_add", "sv_mod_add <player|key> <role> [name]", "Gives a player (once their game has proved its "
		"moderator key: sv_players) or a moderator key a role: moderator, admin or owner (moderators.txt).", 3, 4,
		FALSE, command_mod_add },
	{ "sv_mod_remove", "sv_mod_remove <key|name>", "Takes a moderator out of moderators.txt.", 2, 2, FALSE,
		command_mod_remove },
	{ "sv_link", "sv_link", "Links the server to an account on halo.milenko.org (Delta Control): prints a code "
		"to enter there.", 1, 1, FALSE, command_link },
	{ "sv_unlink", "sv_unlink", "Unlinks the server from halo.milenko.org: its credential stops working.", 1, 1,
		FALSE, command_unlink },
	{ "sv_link_status", "sv_link_status", "Whether the server is linked to halo.milenko.org, and to whom.", 1, 1,
		FALSE, command_link_status },
#endif
	/* (the console's own: server_control.c runs them before they get here) */
	{ "sv_admin_list", "sv_admin_list", "The control API's credentials (console only).", 1, 1, FALSE,
		command_console_only },
	{ "sv_admin_add", "sv_admin_add <name>", "Makes another credential, a token of its own for one admin, and "
		"prints its token once (console only).", 2, 2, FALSE, command_console_only },
	{ "sv_admin_rotate", "sv_admin_rotate <name>", "Gives a credential a new token, printed once; the old one and its "
		"web sessions stop working (console only).", 2, 2, FALSE, command_console_only },
	{ "sv_admin_remove", "sv_admin_remove <name>", "Takes a credential out; its token and web sessions stop "
		"working (console only).", 2, 2, FALSE, command_console_only },
	{ "sv_account_list", "sv_account_list", "The control panel's accounts (console only).", 1, 1, FALSE,
		command_console_only },
	{ "sv_account_invite", "sv_account_invite <role>", "An invitation for a new account of a role, printed once "
		"(console only).", 2, 2, FALSE, command_console_only },
	{ "sv_account_role", "sv_account_role <name> <role>", "Changes an account's role (console only).", 3, 3, FALSE,
		command_console_only },
	{ "sv_account_remove", "sv_account_remove <name>", "Takes an account out; its sessions end (console only).",
		2, 2, FALSE, command_console_only },
	{ "sv_account_reset", "sv_account_reset <name>", "A code to set an account's password again (and turn off its "
		"second factor), printed once (console only).", 2, 2, FALSE, command_console_only },
	{ "sv_account_setup", "sv_account_setup", "A new setup code for the first owner's account, while there is none "
		"(console only).", 1, 1, FALSE, command_console_only },
};

/* the game types sv_map takes (game_engine_get_variant_by_name's) */
static char const *const server_game_types[] =
{
	"slayer", "team_slayer", "ctf", "ironctf", "king", "team_king", "oddball", "team_oddball", "race",
	"team_race", "rally", "elimination", "stalker", "accumulation",
};

static char server_command_output[OUTPUT_SIZE];

/* who runs a command (its source), and the permissions they have */
struct command_actor
{
	/* console, startup, api, web, game, site */
	char const *via;
	/* who: the source's rest ("alice 1a2b3c4d", "Odb718 1a2b3c4d") */
	char const *name;
	int role;
	unsigned int permissions;
};

static struct
{
	struct command_actor const *actor;
	/* the command running's payload (a file's text the API brought) */
	char const *payload;
	/* what it was done to, and why, for the audit (the commands set them) */
	char target[96];
	char reason[96];
} server_command_running;

static struct
{
	boolean started;
	boolean startup_run;
	long startup_count;
	unsigned long link_status_time;
	char startup[MAXIMUM_STARTUP_COMMANDS][COMMAND_LINE_MAXIMUM_LENGTH + 1];
} server_commands_globals;

/* ---------- private code */

/* a map as the commands show it: a playlist's path's last part
(levels\test\bloodgulch\bloodgulch: bloodgulch; timberland@ce as it is) */
static char const *map_display_name(
	char const *map)
{
	char const *base = map;
	char const *cursor;

	for (cursor = map; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/')
			base = cursor + 1;
	}
	return base;
}

static char const *state_name(
	short state,
	boolean json)
{
	switch (state)
	{
	case _dedicated_state_lobby: return "lobby";
	case _dedicated_state_loading: return "loading";
	case _dedicated_state_in_game: return json ? "in_game" : "in game";
	case _dedicated_state_postgame: return json ? "postgame" : "carnage report";
	default: return "starting";
	}
}

static boolean hosting(
	void)
{
	return global_network_game_server_get() != NULL;
}

/* whether the server's game is played in teams */
static boolean game_has_teams(
	struct network_game const *game)
{
	return game && game->variant.universal_variant.teams ? TRUE : FALSE;
}

/* the players of the server's game (other machines' only: the server has
none of its own), at most maximum_count; how many */
static long get_players(
	struct server_player *players,
	long maximum_count)
{
	struct network_game_server *server = global_network_game_server_get();
	struct network_game *game = server ? network_game_server_get_game(server) : NULL;
	boolean in_game;
	long count = 0;
	long index;

	if (!game)
		return 0;
	in_game = network_game_server_get_state(server, NULL) != 0 && game_engine_running() && player_data;
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS && count < maximum_count; index++)
	{
		struct network_player *network_player = &game->players[index];
		struct server_player *player = &players[count];
		long character;

		if (!network_player_is_valid(network_player))
			continue;
		csmemset(player, 0, sizeof(*player));
		player->number = index + 1;
		for (character = 0; character < (long)NUMBEROF(network_player->name) && network_player->name[character] &&
			character < (long)sizeof(player->name) - 1; character++)
		{
			player->name[character] = player_name_character_ascii(network_player->name[character]);
		}
		player->name[character] = 0;
		player->machine_index = network_player->machine_index;
		player->team_index = network_player->team_index;
		player->ping = NONE;
		/* (in a game: the score and ping of the player it made of them) */
		if (in_game)
		{
			struct data_iterator iterator;
			struct player_datum *datum;

			data_iterator_new(&iterator, player_data);
			while ((datum = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
			{
				if (datum->quit_out_of_game ||
					datum->network_player_data.machine_index != network_player->machine_index ||
					datum->network_player_data.controller_index != network_player->controller_index)
				{
					continue;
				}
				if (game_engine && game_engine->get_player_score)
				{
					player->scored = TRUE;
					player->score = game_engine->get_player_score(iterator.datum_index, _get_score_individual);
				}
				player->team_index = datum->team_index;
				player->ping = distributed_player_ping((short)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index));
				break;
			}
		}
		count++;
	}
	return count;
}

/* the player a command names: a number from sv_players, else a name (the
whole name, in either case; else the one name beginning with it); FALSE,
with why in the output, if none or more than one */
static boolean find_player(
	char const *text,
	struct server_player *found,
	struct command_output *output)
{
	static struct server_player players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	long count = get_players(players, NUMBEROF(players));
	long number;
	long index;
	long whole_count = 0, whole_index = NONE;
	long begins_count = 0, begins_index = NONE;

	if (command_line_integer(text, 1, HALO_PORT_MAXIMUM_NETWORK_PLAYERS, &number))
	{
		for (index = 0; index < count; index++)
		{
			if (players[index].number == number)
			{
				*found = players[index];
				return TRUE;
			}
		}
		command_output_printf(output, "no player is number %ld (sv_players)\n", number);
		return FALSE;
	}
	for (index = 0; index < count; index++)
	{
		int match = command_line_name_match(players[index].name, text);

		if (match == 2)
		{
			whole_count++;
			whole_index = index;
		}
		if (match)
		{
			begins_count++;
			begins_index = index;
		}
	}
	if (whole_count == 1)
	{
		*found = players[whole_index];
		return TRUE;
	}
	if (whole_count > 1)
	{
		command_output_printf(output, "%ld players are named \"%s\": give their number (sv_players)\n", whole_count, text);
		return FALSE;
	}
	if (begins_count == 1)
	{
		*found = players[begins_index];
		return TRUE;
	}
	if (begins_count > 1)
	{
		command_output_printf(output, "%ld players' names begin with \"%s\": give more of it, or their number\n",
			begins_count, text);
		return FALSE;
	}
	command_output_printf(output, "no player's name begins with \"%s\"\n", text);
	return FALSE;
}

/* a machine's first player: their number in sv_players (and their name,
name may be NULL), or 0 if it has none */
long server_commands_machine_player(
	long machine_index,
	char *name,
	long name_size)
{
	static struct server_player players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	long count = get_players(players, NUMBEROF(players));
	long index;

	for (index = 0; index < count; index++)
	{
		if (players[index].machine_index != machine_index)
			continue;
		if (name && name_size > 0)
			snprintf(name, (size_t)name_size, "%s", players[index].name);
		return players[index].number;
	}
	return 0;
}

/* the machine of the player numbered so in sv_players, or -1 */
long server_commands_player_machine(
	long number)
{
	static struct server_player players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	long count = get_players(players, NUMBEROF(players));
	long index;

	for (index = 0; index < count; index++)
	{
		if (players[index].number == number)
			return players[index].machine_index;
	}
	return -1;
}

/* the names of every player of a machine, "a, b" */
static void machine_names(
	long machine_index,
	char *names,
	long size)
{
	static struct server_player players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	long count = get_players(players, NUMBEROF(players));
	long index;
	long length = 0;

	if (size > 0)
		names[0] = 0;
	for (index = 0; index < count; index++)
	{
		if (players[index].machine_index != machine_index || length >= size - 1)
			continue;
		length += snprintf(names + length, (size_t)(size - length), "%s%s", length ? ", " : "", players[index].name);
		if (length > size - 1)
			length = size - 1;
	}
}

/* whether a file is a multiplayer map's (a cache file whose header says so) */
static boolean file_is_multiplayer_map(
	char const *path)
{
	unsigned char header[0x64];
	FILE *file = fopen(path, "rb");
	boolean multiplayer = FALSE;

	if (!file)
		return FALSE;
	if (fread(header, 1, sizeof(header), file) == sizeof(header))
	{
		/* ('head', and the type at 0x60: 1, multiplayer) */
		multiplayer = header[0] == 'd' && header[1] == 'a' && header[2] == 'e' && header[3] == 'h' &&
			header[0x60] == 1 && header[0x61] == 0;
	}
	fclose(file);
	return multiplayer;
}

/* whether the server has a map (as a command names it) to play: a
multiplayer map's file, in its family's folders */
boolean server_map_playable(
	char const *map,
	struct command_output *output)
{
	char file[64];
	char path[256];
	short family = map_family_parse(map, file, sizeof(file));

	if (family == _map_family_xbox)
	{
		snprintf(path, sizeof(path), "%s%s.map", cache_files_map_directory(), file);
		if (!file_is_multiplayer_map(path))
		{
			command_output_printf(output, "no multiplayer map %s in the maps folder\n", file);
			return FALSE;
		}
		return TRUE;
	}
#ifdef HALO_CUSTOM_EDITION
	if (!map_family_find(family, file, path, sizeof(path)) || !file_is_multiplayer_map(path))
	{
		command_output_printf(output, "no multiplayer map %s in %s\n", map, map_family_folder(family));
		return FALSE;
	}
	return TRUE;
#else
	command_output_printf(output, "this server does not play %s maps\n", map_family_badge(family));
	return FALSE;
#endif
}

static void print_status(
	struct command_output *output,
	boolean json)
{
	struct dedicated_status status;
	struct network_game_server *server = global_network_game_server_get();
	struct network_game *game = server ? network_game_server_get_game(server) : NULL;
	long players = game ? game->player_count : 0;
	char uptime[32], following[64];

	dedicated_server_get_status(&status);
	if (json)
	{
		command_output_printf(output, "{\"name\": ");
		command_output_json_string(output, status.name);
		command_output_printf(output, ", \"version\": ");
		command_output_json_string(output, updater_version());
		delta_legacy_following(following, (int)sizeof(following));
		command_output_printf(output, ", \"network_version\": %d, \"following\": ", delta_legacy_announce());
		command_output_json_string(output, following);
		command_output_printf(output, ", \"state\": \"%s\", \"map\": ", state_name(status.state, TRUE));
		command_output_json_string(output, map_display_name(status.map));
		command_output_printf(output, ", \"game_type\": ");
		command_output_json_string(output, status.variant);
		command_output_printf(output, ", \"chosen\": %s, \"next_map\": ", status.chosen ? "true" : "false");
		if (status.next_map[0])
		{
			command_output_json_string(output, map_display_name(status.next_map));
			command_output_printf(output, ", \"next_game_type\": ");
			command_output_json_string(output, status.next_variant);
		}
		else
			command_output_printf(output, "null, \"next_game_type\": null");
		command_output_printf(output, ", \"playlist\": ");
		command_output_json_string(output, status.playlist);
		command_output_printf(output, ", \"entry\": %ld, \"entries\": %ld, \"players\": %ld, \"maximum_players\": %ld, "
			"\"minimum_players\": %ld, \"public\": %s, \"idle_limit_minutes\": %ld, \"uptime_seconds\": %lu}",
			status.entry + 1, status.entry_count, players, status.maximum_players, status.minimum_players,
			status.public_game ? "true" : "false", status.idle_limit, status.uptime_seconds);
		return;
	}
	command_line_duration_text((long)status.uptime_seconds, uptime, sizeof(uptime));
	command_output_printf(output, "name: %s\n", status.name);
	delta_legacy_following(following, (int)sizeof(following));
	command_output_printf(output, "version: %s (network version %d; %s)\n", updater_version(), delta_legacy_announce(),
		following);
	command_output_printf(output, "state: %s\n", state_name(status.state, FALSE));
	command_output_printf(output, "map: %s, game type: %s (%s)\n", map_display_name(status.map), status.variant,
		status.chosen ? "chosen by a command" : "the playlist's");
	if (status.next_map[0])
		command_output_printf(output, "next: %s, %s (chosen by a command)\n", map_display_name(status.next_map),
			status.next_variant);
	command_output_printf(output, "playlist: %s, entry %ld of %ld\n", status.playlist, status.entry + 1,
		status.entry_count);
	command_output_printf(output, "players: %ld of %ld (a game starts with %ld)\n", players, status.maximum_players,
		status.minimum_players);
	command_output_printf(output, "public: %s\n", status.public_game ? "yes (in the Server Browser)" : "no");
	command_output_printf(output, "uptime: %s\n", status.uptime_seconds < 60 ? "under a minute" : uptime);
}

/* ---------- the commands */

static boolean command_help(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	long index;

	(void)json;
	for (index = 0; index < (long)NUMBEROF(server_commands); index++)
	{
		if (line->count == 2 && strcmp(server_commands[index].name, line->words[1]))
			continue;
		command_output_printf(output, "%-28s %s\n", server_commands[index].usage, server_commands[index].help);
		if (line->count == 2)
			return TRUE;
	}
	if (line->count == 2)
	{
		command_output_printf(output, "no command %s (help lists them)\n", line->words[1]);
		return FALSE;
	}
	return TRUE;
}

static boolean command_status(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	(void)line;
	print_status(output, json);
	return TRUE;
}

static boolean command_players(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	static struct server_player players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	struct network_game_server *server = global_network_game_server_get();
	struct network_game *game = server ? network_game_server_get_game(server) : NULL;
	boolean teams = game_has_teams(game);
	long count = get_players(players, NUMBEROF(players));
	long index;

	(void)line;
	if (json)
		command_output_printf(output, "{\"players\": [");
	else if (!count)
		command_output_printf(output, "no players\n");
	else
	{
		command_output_printf(output, "%3s  %-11s  %-4s  %5s  %5s  %-32s  %s\n", "#", "name", "team", "score", "ping",
			"id", "moderator key");
	}
	for (index = 0; index < count; index++)
	{
		struct server_player const *player = &players[index];
		char const *hardware_id = network_game_server_machine_hardware_id(player->machine_index);
		char const *team = !teams ? NULL : player->team_index == 0 ? "red" : player->team_index == 1 ? "blue" : NULL;
		unsigned char key[32];
		char key_text[72] = "";
		int role = 0;

		/* (the moderator key the player's game proved, and its role here) */
		if (server_moderation_machine_key(player->machine_index, key))
		{
			unsigned char const *byte;

			for (byte = key; byte < key + 32; byte++)
				snprintf(key_text + 2 * (byte - key), 3, "%02x", *byte);
			role = server_roles_key_role(key, NULL, NULL, 0);
		}

		if (json)
		{
			command_output_printf(output, "%s{\"number\": %ld, \"name\": ", index ? ", " : "", player->number);
			command_output_json_string(output, player->name);
			command_output_printf(output, ", \"machine\": %ld, \"team\": ", player->machine_index);
			if (team)
				command_output_json_string(output, team);
			else
				command_output_printf(output, "null");
			if (player->scored)
				command_output_printf(output, ", \"score\": %ld", player->score);
			else
				command_output_printf(output, ", \"score\": null");
			if (player->ping != NONE)
				command_output_printf(output, ", \"ping\": %ld", player->ping);
			else
				command_output_printf(output, ", \"ping\": null");
			command_output_printf(output, ", \"id\": ");
			if (hardware_id && hardware_id[0])
				command_output_json_string(output, hardware_id);
			else
				command_output_printf(output, "null");
			command_output_printf(output, ", \"moderator_key\": ");
			if (key_text[0])
				command_output_printf(output, "\"%s\", \"role\": \"%s\"", key_text, server_role_name(role));
			else
				command_output_printf(output, "null, \"role\": null");
			command_output_printf(output, "}");
		}
		else
		{
			char score[16], ping[16];

			if (player->scored)
				snprintf(score, sizeof(score), "%ld", player->score);
			else
				snprintf(score, sizeof(score), "-");
			if (player->ping != NONE)
				snprintf(ping, sizeof(ping), "%ld", player->ping);
			else
				snprintf(ping, sizeof(ping), "-");
			command_output_printf(output, "%3ld  %-11s  %-4s  %5s  %5s  %-32s  %s%s%s%s\n", player->number, player->name,
				team ? team : "-", score, ping, hardware_id && hardware_id[0] ? hardware_id : "none",
				key_text[0] ? key_text : "-", role ? " (" : "", role ? server_role_name(role) : "", role ? ")" : "");
		}
	}
	if (json)
	{
		command_output_printf(output, "], \"count\": %ld, \"maximum_players\": %ld}", count,
			game ? (long)game->maximum_players : 0);
	}
	return TRUE;
}

static boolean command_kick(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	struct server_player player;
	char names[96];

	(void)json;
	if (!find_player(line->words[1], &player, output))
		return FALSE;
	machine_names(player.machine_index, names, sizeof(names));
	note_target(names, line->count == 3 ? line->words[2] : NULL);
	/* (a ChupathingyCE game is told why before it goes) */
	if (line->count == 3)
		server_moderation_notice(player.machine_index, TRUE, "Kicked", line->words[2]);
	if (!network_game_server_drop_machine(player.machine_index, _rejection_code_game_is_closed))
	{
		command_output_printf(output, "%s cannot be kicked: their machine has not joined\n", player.name);
		return FALSE;
	}
	error(_error_silent, "dedicated: kicked %s", names);
	command_output_printf(output, "kicked %s\n", names);
	return TRUE;
}

static boolean command_ban(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	struct server_player player;
	char names[96];
	char reason[96];
	char duration_text[32];
	long duration = 0;
	unsigned long until = 0;
	char const *hardware_id;

	(void)json;
	if (line->count >= 3 && !command_line_duration(line->words[2], &duration))
	{
		command_output_printf(output, "%s is not a duration: 30m, 2h, 7d, 1d12h, or forever\n", line->words[2]);
		return FALSE;
	}
	if (!find_player(line->words[1], &player, output))
		return FALSE;
	machine_names(player.machine_index, names, sizeof(names));
	note_target(names, line->count == 4 ? line->words[3] : NULL);
	command_line_duration_text(duration, duration_text, sizeof(duration_text));
	/* (bans.txt's reason: by whom, how long, and why) */
	snprintf(reason, sizeof(reason), "banned by %s%s%s%s%s", actor_display(), duration ? " for " : "",
		duration ? duration_text : "", line->count == 4 ? ": " : "", line->count == 4 ? line->words[3] : "");
	if (duration)
		until = (unsigned long)time(NULL) + (unsigned long)duration;
	server_moderation_notice(player.machine_index, TRUE, "Banned", line->count == 4 ? line->words[3] : duration_text);
	hardware_id = network_game_server_machine_hardware_id(player.machine_index);
	if (!network_game_server_drop_machine(player.machine_index, _rejection_code_blacklisted_machine))
	{
		command_output_printf(output, "%s cannot be banned: their machine has not joined\n", player.name);
		return FALSE;
	}
	network_distributed_ban_until(player.machine_index, network_game_server_machine_address(player.machine_index),
		names, reason, until);
	error(_error_silent, "dedicated: banned %s (%s)", names, duration_text);
	command_output_printf(output, "banned %s %s%s\n", names, duration ? "for " : "", duration_text);
	if (!hardware_id || !hardware_id[0])
	{
		command_output_printf(output, "(their machine told no hardware id: the ban is by its address alone)\n");
	}
	return TRUE;
}

static boolean command_unban(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	long number;

	(void)json;
	if (!command_line_integer(line->words[1], 1, 1000000, &number))
	{
		command_output_printf(output, "%s is not a ban's number (sv_banlist)\n", line->words[1]);
		return FALSE;
	}
	if (!network_distributed_unban(number - 1))
	{
		command_output_printf(output, "no ban %ld to take out (sv_banlist)\n", number);
		return FALSE;
	}
	error(_error_silent, "dedicated: ban %ld taken out of bans.txt", number);
	command_output_printf(output, "ban %ld taken out (the others' numbers have moved up)\n", number);
	return TRUE;
}

static boolean command_banlist(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	long index;
	unsigned long now = (unsigned long)time(NULL);

	(void)line;
	if (json)
		command_output_printf(output, "{\"bans\": [");
	for (index = 0; index < MAXIMUM_LISTED_BANS; index++)
	{
		char when[32], hardware_id[40], players[96], reason[96], left[32];
		unsigned long until;

		if (!network_distributed_ban_entry(index, when, sizeof(when), hardware_id, sizeof(hardware_id), players,
			sizeof(players), reason, sizeof(reason), &until))
		{
			break;
		}
		if (json)
		{
			command_output_printf(output, "%s{\"number\": %ld, \"when\": ", index ? ", " : "", index + 1);
			command_output_json_string(output, when);
			command_output_printf(output, ", \"id\": ");
			if (hardware_id[0])
				command_output_json_string(output, hardware_id);
			else
				command_output_printf(output, "null");
			command_output_printf(output, ", \"players\": ");
			command_output_json_string(output, players);
			command_output_printf(output, ", \"reason\": ");
			command_output_json_string(output, reason);
			if (!until)
				command_output_printf(output, ", \"until\": null, \"seconds_left\": null}");
			else
				command_output_printf(output, ", \"until\": %lu, \"seconds_left\": %lu}", until,
					until > now ? until - now : 0UL);
			continue;
		}
		if (!until)
			snprintf(left, sizeof(left), "forever");
		else if (until <= now)
			snprintf(left, sizeof(left), "over");
		else
		{
			char duration[24];

			command_line_duration_text((long)(until - now), duration, sizeof(duration));
			snprintf(left, sizeof(left), "%s left", duration);
		}
		command_output_printf(output, "%3ld  %-19s  %-32s  %-14s  %s  (%s)\n", index + 1, when[0] ? when : "-",
			hardware_id, left, players[0] ? players : "-", reason[0] ? reason : "-");
	}
	if (json)
		command_output_printf(output, "], \"count\": %ld}", index);
	else if (!index)
		command_output_printf(output, "no bans\n");
	return TRUE;
}

static boolean command_map(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	struct game_variant variant;
	struct game_variant empty;

	(void)json;
	if (!command_line_map_name_valid(line->words[1]))
	{
		command_output_printf(output, "%s is not a map's name (bloodgulch, name@ce, name@md, name@pc)\n", line->words[1]);
		return FALSE;
	}
	csmemset(&empty, 0, sizeof(empty));
	game_engine_get_variant_by_name(&variant, line->words[2]);
	if (!csmemcmp(&variant, &empty, sizeof(variant)))
	{
		command_output_printf(output, "no game type %s (slayer, team_slayer, ctf, king, oddball, race, ...)\n",
			line->words[2]);
		return FALSE;
	}
	if (!server_map_playable(line->words[1], output))
		return FALSE;
	dedicated_server_play(line->words[1], line->words[2]);
	error(_error_silent, "dedicated: a command plays %s on %s", line->words[2], line->words[1]);
	command_output_printf(output, "%s on %s %s\n", line->words[2], line->words[1],
		hosting() ? "now" : "once the server hosts");
	return TRUE;
}

static boolean command_mapcycle(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	struct dedicated_status status;
	long index;

	(void)line;
	dedicated_server_get_status(&status);
	if (json)
	{
		command_output_printf(output, "{\"playlist\": ");
		command_output_json_string(output, status.playlist);
		command_output_printf(output, ", \"entry\": %ld, \"state\": \"%s\", \"entries\": [", status.entry + 1,
			state_name(status.state, TRUE));
		for (index = 0; index < dedicated_playlist_count(); index++)
		{
			command_output_printf(output, "%s{\"number\": %ld, \"map\": ", index ? ", " : "", index + 1);
			command_output_json_string(output, map_display_name(dedicated_playlist_map(index)));
			command_output_printf(output, ", \"game_type\": ");
			command_output_json_string(output, dedicated_playlist_variant(index));
			command_output_printf(output, "}");
		}
		command_output_printf(output, "], \"chosen\": ");
		if (status.chosen)
		{
			command_output_printf(output, "{\"map\": ");
			command_output_json_string(output, map_display_name(status.map));
			command_output_printf(output, ", \"game_type\": ");
			command_output_json_string(output, status.variant);
			command_output_printf(output, "}");
		}
		else
			command_output_printf(output, "null");
		command_output_printf(output, ", \"next\": ");
		if (status.next_map[0])
		{
			command_output_printf(output, "{\"map\": ");
			command_output_json_string(output, map_display_name(status.next_map));
			command_output_printf(output, ", \"game_type\": ");
			command_output_json_string(output, status.next_variant);
			command_output_printf(output, "}");
		}
		else
			command_output_printf(output, "null");
		command_output_printf(output, "}");
		return TRUE;
	}
	command_output_printf(output, "%s:\n", status.playlist);
	for (index = 0; index < dedicated_playlist_count(); index++)
	{
		char const *marker = "";

		if (index == status.entry)
			marker = status.chosen ? "  (next)" : status.state == _dedicated_state_in_game ||
				status.state == _dedicated_state_loading || status.state == _dedicated_state_postgame ? "  (playing)" :
				"  (in the lobby)";
		command_output_printf(output, "%3ld  %-20s  %s%s\n", index + 1, map_display_name(dedicated_playlist_map(index)),
			dedicated_playlist_variant(index), marker);
	}
	if (status.chosen)
		command_output_printf(output, "now: %s on %s (chosen by a command)\n", status.variant,
			map_display_name(status.map));
	if (status.next_map[0])
		command_output_printf(output, "next: %s on %s (chosen by a command), then the playlist\n", status.next_variant,
			map_display_name(status.next_map));
	return TRUE;
}

static boolean command_mapcycle_next(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	struct dedicated_status status;

	(void)line;
	(void)json;
	dedicated_server_get_status(&status);
	dedicated_server_skip();
	error(_error_silent, "dedicated: a command skips to the next entry");
	if (status.state == _dedicated_state_lobby)
	{
		dedicated_server_get_status(&status);
		command_output_printf(output, "next: %s on %s\n", status.variant, map_display_name(status.map));
	}
	else
		command_output_printf(output, "the game ends; the next entry's lobby opens\n");
	return TRUE;
}

static boolean command_end_game(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	(void)line;
	(void)json;
	if (!dedicated_server_end_game())
	{
		command_output_printf(output, "no game in progress\n");
		return FALSE;
	}
	command_output_printf(output, "the game ends\n");
	return TRUE;
}

static boolean command_maxplayers(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	struct dedicated_status status;
	long maximum;

	(void)json;
	dedicated_server_get_status(&status);
	if (line->count == 1)
	{
		command_output_printf(output, "%ld\n", status.maximum_players);
		return TRUE;
	}
	if (!command_line_integer(line->words[1], 1, HALO_PORT_MAXIMUM_NETWORK_PLAYERS, &maximum))
	{
		command_output_printf(output, "%s is not a number of players from 1 to %d\n", line->words[1],
			(int)HALO_PORT_MAXIMUM_NETWORK_PLAYERS);
		return FALSE;
	}
	if (maximum < status.minimum_players)
	{
		command_output_printf(output, "a game waits for %ld players (HALO_DEDICATED_MINIMUM_PLAYERS): no fewer\n",
			status.minimum_players);
		return FALSE;
	}
	dedicated_server_set_maximum_players(maximum);
	error(_error_silent, "dedicated: a command sets the most players to %ld", maximum);
	command_output_printf(output, "a game takes %ld players %s\n", maximum,
		status.state == _dedicated_state_lobby ? "(now)" : "(from the next lobby)");
	return TRUE;
}

static boolean command_name(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	struct dedicated_status status;

	(void)json;
	dedicated_server_get_status(&status);
	if (line->count == 1)
	{
		command_output_printf(output, "%s\n", status.name);
		return TRUE;
	}
	if (!command_line_server_name_valid(line->words[1]))
	{
		command_output_printf(output, "a name is 1 to 15 printable ASCII characters (in quotes if it has spaces)\n");
		return FALSE;
	}
	dedicated_server_set_name(line->words[1]);
	error(_error_silent, "dedicated: a command names the server %s", line->words[1]);
	command_output_printf(output, "the server is %s %s\n", line->words[1],
		status.state == _dedicated_state_lobby ? "(now)" : "(from the next lobby)");
	return TRUE;
}

/* the maps sv_maps lists, as it finds them */
struct map_listing
{
	char names[MAXIMUM_LISTED_MAPS][80];
	long count;
};

static void map_listing_add(
	struct map_listing *listing,
	char const *name)
{
	long index;

	for (index = 0; index < listing->count; index++)
	{
		if (!_stricmp(listing->names[index], name))
			return;
	}
	if (listing->count < MAXIMUM_LISTED_MAPS)
		snprintf(listing->names[listing->count++], sizeof(listing->names[0]), "%s", name);
}

#ifdef HALO_CUSTOM_EDITION
struct map_listing_family
{
	struct map_listing *listing;
	short family;
};

static void map_listing_found(
	char const *file,
	void *context)
{
	struct map_listing_family *family = (struct map_listing_family *)context;
	char name[80];

	snprintf(name, sizeof(name), "%s%s", file, map_family_suffix(family->family));
	if (command_line_map_name_valid(name))
		map_listing_add(family->listing, name);
}
#endif

static int map_listing_compare(
	void const *a,
	void const *b)
{
	return _stricmp((char const *)a, (char const *)b);
}

static boolean command_maps(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	static struct map_listing listing;
	char pattern[288];
	WIN32_FIND_DATAA data;
	HANDLE find;
	long index;

	(void)line;
	listing.count = 0;
	/* the Xbox's: the maps folder's multiplayer maps */
	snprintf(pattern, sizeof(pattern), "%s*.map", cache_files_map_directory());
	find = FindFirstFileA(pattern, &data);
	if (find != INVALID_HANDLE_VALUE)
	{
		do
		{
			char name[80], path[384];
			size_t length = strlen(data.cFileName);

			if (length <= 4 || length - 4 >= sizeof(name) || _stricmp(data.cFileName + length - 4, ".map"))
				continue;
			snprintf(name, sizeof(name), "%.*s", (int)(length - 4), data.cFileName);
			if (map_family_parse(name, NULL, 0) != _map_family_xbox || !command_line_map_name_valid(name))
				continue;
			snprintf(path, sizeof(path), "%s%s", cache_files_map_directory(), data.cFileName);
			if (file_is_multiplayer_map(path))
				map_listing_add(&listing, name);
		}
		while (FindNextFileA(find, &data));
		CloseHandle(find);
	}
#ifdef HALO_CUSTOM_EDITION
	{
		short family;

		for (family = _map_family_xbox + 1; family < NUMBER_OF_MAP_FAMILIES; family++)
		{
			struct map_listing_family context;

			context.listing = &listing;
			context.family = family;
			map_family_list(family, map_listing_found, &context);
		}
	}
#endif
	qsort(listing.names, (size_t)listing.count, sizeof(listing.names[0]), map_listing_compare);
	if (json)
	{
		command_output_printf(output, "{\"maps\": [");
		for (index = 0; index < listing.count; index++)
		{
			command_output_printf(output, "%s", index ? ", " : "");
			command_output_json_string(output, listing.names[index]);
		}
		command_output_printf(output, "], \"game_types\": [");
		for (index = 0; index < (long)NUMBEROF(server_game_types); index++)
		{
			command_output_printf(output, "%s", index ? ", " : "");
			command_output_json_string(output, server_game_types[index]);
		}
		command_output_printf(output, "]}");
		return TRUE;
	}
	command_output_printf(output, "maps:");
	for (index = 0; index < listing.count; index++)
		command_output_printf(output, " %s", listing.names[index]);
	command_output_printf(output, "%s\ngame types:", listing.count ? "" : " none");
	for (index = 0; index < (long)NUMBEROF(server_game_types); index++)
		command_output_printf(output, " %s", server_game_types[index]);
	command_output_printf(output, "\n");
	return TRUE;
}

static boolean command_warn(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	struct server_player player;
	char names[96];
	boolean shown;

	(void)json;
	if (!find_player(line->words[1], &player, output))
		return FALSE;
	machine_names(player.machine_index, names, sizeof(names));
	note_target(names, line->words[2]);
	shown = server_moderation_notice(player.machine_index, TRUE, "Warning", line->words[2]);
	error(_error_silent, "dedicated: warned %s", names);
	command_output_printf(output, "warned %s%s\n", names, shown ? "" :
		" (their game cannot show it: not a ChupathingyCE game; the warning is in the audit file)");
	return TRUE;
}

#ifdef HALO_SERVER
static boolean command_mod_list(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	long count = server_roles_moderator_count();
	long index;

	(void)line;
	if (json)
		command_output_printf(output, "{\"moderators\": [");
	else if (!count)
		command_output_printf(output, "no moderators in moderators.txt\n");
	for (index = 0; index < count; index++)
	{
		char key[72], name[40];
		int role;

		if (!server_roles_moderator_get((int)index, &role, key, sizeof(key), name, sizeof(name)))
			break;
		if (json)
		{
			command_output_printf(output, "%s{\"role\": \"%s\", \"key\": \"%s\", \"name\": ", index ? ", " : "",
				server_role_name(role), key);
			command_output_json_string(output, name);
			command_output_printf(output, "}");
		}
		else
			command_output_printf(output, "%-9s  %s  %s\n", server_role_name(role), key, name);
	}
	if (json)
		command_output_printf(output, "], \"count\": %ld}", count);
	else
		command_output_printf(output, "(and the control panel's accounts bound to a game, and the site's, if the "
			"server is linked)\n");
	return TRUE;
}

static boolean command_mod_add(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	unsigned char key[32];
	char problem[160];
	char name[40];
	int role = server_role_parse(line->words[2]);

	(void)json;
	if (role <= 0)
	{
		command_output_printf(output, "%s is not a role: moderator, admin or owner\n", line->words[2]);
		return FALSE;
	}
	name[0] = 0;
	if (line->count == 4)
		snprintf(name, sizeof(name), "%s", line->words[3]);
	/* a key given whole, else a player of the game whose game proved theirs */
	if (!server_key_parse(line->words[1], key))
	{
		struct server_player player;

		if (!find_player(line->words[1], &player, output))
			return FALSE;
		if (!server_moderation_machine_key(player.machine_index, key))
		{
			command_output_printf(output, "%s's game has not proved a moderator key: they sign in from the game's "
				"Moderation screen (a ChupathingyCE game; Delta Peer), or give the key itself\n", player.name);
			return FALSE;
		}
		if (!name[0])
			snprintf(name, sizeof(name), "%s", player.name);
	}
	note_target(name[0] ? name : line->words[1], NULL);
	if (!server_roles_moderator_set(role, key, name, problem, sizeof(problem)))
	{
		command_output_printf(output, "%s\n", problem);
		return FALSE;
	}
	server_moderation_roles_changed();
	command_output_printf(output, "%s is %s (moderators.txt)\n", name[0] ? name : line->words[1],
		server_role_name(role));
	return TRUE;
}

static boolean command_mod_remove(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	char problem[160];
	char found[64];

	(void)json;
	note_target(line->words[1], NULL);
	if (!server_roles_moderator_remove(line->words[1], found, sizeof(found), problem, sizeof(problem)))
	{
		command_output_printf(output, "%s\n", problem);
		return FALSE;
	}
	server_moderation_roles_changed();
	command_output_printf(output, "%s taken out of moderators.txt\n", found);
	return TRUE;
}

static boolean command_link(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	struct dedicated_status status;
	char text[512];
	int ok;

	(void)line;
	(void)json;
	dedicated_server_get_status(&status);
	ok = server_link_start(status.name, updater_version(), text, sizeof(text));
	command_output_printf(output, "%s\n", text);
	return ok ? TRUE : FALSE;
}

static boolean command_unlink(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	char text[256];
	int ok;

	(void)line;
	(void)json;
	ok = server_link_stop(text, sizeof(text));
	command_output_printf(output, "%s\n", text);
	return ok ? TRUE : FALSE;
}

static boolean command_link_status(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	char text[512];

	(void)line;
	server_link_status(json ? 1 : 0, text, sizeof(text));
	command_output_printf(output, "%s%s", text, json ? "" : "\n");
	return TRUE;
}

#endif

static boolean command_console_only(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	(void)json;
	command_output_printf(output, "%s is the server's console's only (its token is printed there)\n", line->words[0]);
	return FALSE;
}

/* the permission a command needs, by its name (for refusals) */
static char const *permission_name(
	unsigned int permission)
{
	switch (permission)
	{
	case SERVER_PERMISSION_VIEW: return "view (any role)";
	case SERVER_PERMISSION_WARN: return "warn (moderator)";
	case SERVER_PERMISSION_KICK: return "kick (moderator)";
	case SERVER_PERMISSION_BAN_TIMED: return "ban for a while (moderator)";
	case SERVER_PERMISSION_BAN: return "ban for longer, or for ever (admin)";
	case SERVER_PERMISSION_UNBAN: return "unban (admin)";
	case SERVER_PERMISSION_MAP: return "map and playlist (admin)";
	case SERVER_PERMISSION_SETTINGS: return "settings (admin)";
	case SERVER_PERMISSION_ROLES: return "roles (owner)";
	default: return "console";
	}
}

/* a command line run for someone: its output (text, or JSON for the reads
when json) in output; whether it did what it was asked. Refused if they
lack its permission (server/docs/moderation.md), or have made too many
changes lately; a change is written to the audit file whether it was done
or not */
static boolean execute(
	char const *text,
	boolean json,
	struct command_output *output,
	struct command_actor const *actor)
{
	struct command_line line;
	char problem[96];
	long index;
	int changes = 0;
	unsigned int needed;
	boolean ok;

	if (!command_line_parse(text, &line, problem, sizeof(problem)))
	{
		command_output_printf(output, "%s\n", problem);
		return FALSE;
	}
	if (!line.count)
		return TRUE;
	for (index = 0; index < (long)NUMBEROF(server_commands); index++)
	{
		struct server_command const *command = &server_commands[index];

		if (strcmp(command->name, line.words[0]))
			continue;
		needed = server_command_permission(text, &changes);
		if (needed && !(actor->permissions & needed))
		{
			command_output_printf(output, "%s needs the %s permission, which %s does not have\n", line.words[0],
				permission_name(needed), actor->name[0] ? actor->name : actor->via);
			if (changes)
			{
				server_audit(actor->via, actor->name, actor->role, line.words[0], NULL, "refused: no permission",
					FALSE, output->text);
			}
			return FALSE;
		}
		if (line.count < command->minimum_words || line.count > command->maximum_words)
		{
			command_output_printf(output, "usage: %s\n", command->usage);
			return FALSE;
		}
		if (command->needs_game && !hosting())
		{
			command_output_printf(output, "the server is not hosting a game yet\n");
			return FALSE;
		}
		/* (the console's and the startup commands' are the owner's own, and
		not limited) */
		if (changes && strcmp(actor->via, "console") && strcmp(actor->via, "startup"))
		{
			char who[COMMAND_LINE_MAXIMUM_LENGTH];
			int retry_after = 0;

			snprintf(who, sizeof(who), "%s %s", actor->via, actor->name);
			if (!server_roles_actor_allowed(who, &retry_after))
			{
				command_output_printf(output, "too many changes in a short while: try again in %d seconds\n",
					retry_after);
				server_audit(actor->via, actor->name, actor->role, line.words[0], NULL, "refused: rate limit",
					FALSE, NULL);
				return FALSE;
			}
		}
		server_command_running.actor = actor;
		server_command_running.target[0] = 0;
		server_command_running.reason[0] = 0;
		ok = command->execute(&line, json, output);
		if (changes)
		{
			server_audit(actor->via, actor->name, actor->role, line.words[0], server_command_running.target[0] ?
				server_command_running.target : NULL, server_command_running.reason[0] ?
				server_command_running.reason : NULL, ok, output->text);
		}
		server_command_running.actor = NULL;
		return ok;
	}
	command_output_printf(output, "no command %s (help lists them)\n", line.words[0]);
	return FALSE;
}

/* ---------- the running command's */

/* whom it is done to, and why (the audit file's) */
static void note_target(
	char const *target,
	char const *reason)
{
	snprintf(server_command_running.target, sizeof(server_command_running.target), "%s", target ? target : "");
	snprintf(server_command_running.reason, sizeof(server_command_running.reason), "%s", reason ? reason : "");
}

/* who runs it, as a player reads it: "the server", or "Odb718
(moderator)" (a source's name is "<name> <id>": the id is the log's) */
static char const *actor_display(
	void)
{
	static char text[96];
	struct command_actor const *actor = server_command_running.actor;
	char name[48];
	char *space;

	if (!actor || !strcmp(actor->via, "console") || !strcmp(actor->via, "startup") || !strcmp(actor->via, "api"))
		return "the server";
	snprintf(name, sizeof(name), "%s", actor->name);
	space = strchr(name, ' ');
	if (space)
		*space = 0;
	snprintf(text, sizeof(text), "%s (%s)", name, server_role_name(actor->role));
	return text;
}

char const *server_commands_payload(
	void)
{
	return server_command_running.payload;
}

/* the game's own (server_moderation.c, Delta Peer): an action a moderator's
game asked for, run as a command of theirs */
boolean server_commands_run_as(
	char const *text,
	char const *via,
	char const *name,
	int role,
	unsigned int permissions,
	char *result,
	long result_size)
{
	struct command_actor actor;
	struct command_output output;
	boolean ok;

	actor.via = via;
	actor.name = name;
	actor.role = role;
	actor.permissions = permissions;
	error(_error_silent, "control: %s %s: %s", via, name, text);
	command_output_begin(&output, server_command_output, sizeof(server_command_output));
	ok = execute(text, FALSE, &output, &actor);
	log_output(server_command_output);
	snprintf(result, (size_t)result_size, "%s", server_command_output);
	return ok;
}


/* the startup commands (HALO_DEDICATED_COMMANDS, a file in the data folder,
as the playlist is), read as the server starts */
static void load_startup_commands(
	void)
{
	char const *name = getenv("HALO_DEDICATED_COMMANDS");
	char path[256];
	char line[COMMAND_LINE_MAXIMUM_LENGTH + 2];
	char *cursor;
	FILE *file;

	if (!name || !name[0])
		return;
	snprintf(path, sizeof(path), "d:\\%s", name);
	for (cursor = path; *cursor; cursor++)
	{
		if (*cursor == '/')
			*cursor = '\\';
	}
	file = fopen(path, "r");
	if (!file)
	{
		error(_error_silent, "dedicated: cannot read the startup commands %s", path);
		return;
	}
	while (fgets(line, sizeof(line), file))
	{
		long length = (long)strlen(line);
		boolean whole = length > 0 && line[length - 1] == '\n';

		while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
			line[--length] = 0;
		/* (a line too long for a command: the rest of it skipped too) */
		if (!whole && !feof(file))
		{
			int character;

			error(_error_silent, "dedicated: a startup command longer than %d characters, skipped",
				(int)COMMAND_LINE_MAXIMUM_LENGTH);
			while ((character = fgetc(file)) != EOF && character != '\n')
				;
			continue;
		}
		if (!length || line[strspn(line, " \t")] == '#' || !line[strspn(line, " \t")])
			continue;
		if (server_commands_globals.startup_count >= MAXIMUM_STARTUP_COMMANDS)
		{
			error(_error_silent, "dedicated: more than %d startup commands; the rest skipped",
				(int)MAXIMUM_STARTUP_COMMANDS);
			break;
		}
		snprintf(server_commands_globals.startup[server_commands_globals.startup_count++],
			sizeof(server_commands_globals.startup[0]), "%s", line);
	}
	fclose(file);
	error(_error_silent, "dedicated: %ld startup commands from %s", server_commands_globals.startup_count, path);
}

/* a command's output in the log, a line at a time */
static void log_output(
	char const *output)
{
	while (*output)
	{
		char const *end = strchr(output, '\n');
		long length = end ? (long)(end - output) : (long)strlen(output);

		error(_error_silent, "dedicated:   %.*s", (int)length, output);
		output += length + (end ? 1 : 0);
	}
}

#ifdef HALO_SERVER
/* the server's state for the site (Delta Control's link): sv_status's, and
the players' numbers, names, teams and scores, and whether each has proved
a moderator key; never an address or a hardware id */
static void link_status(
	void)
{
	static char text[16 * 1024];
	static struct server_player players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	struct command_output output;
	struct network_game_server *server = global_network_game_server_get();
	struct network_game *game = server ? network_game_server_get_game(server) : NULL;
	boolean teams = game_has_teams(game);
	long count = get_players(players, NUMBEROF(players));
	long index;

	struct dedicated_status status;

	dedicated_server_get_status(&status);
	command_output_begin(&output, text, sizeof(text));
	command_output_printf(&output, "{\"name\": ");
	command_output_json_string(&output, status.name);
	command_output_printf(&output, ", \"version\": ");
	command_output_json_string(&output, updater_version());
	command_output_printf(&output, ", \"state\": \"%s\", \"map\": ", state_name(status.state, TRUE));
	command_output_json_string(&output, map_display_name(status.map));
	command_output_printf(&output, ", \"game_type\": ");
	command_output_json_string(&output, status.variant);
	command_output_printf(&output, ", \"playlist\": ");
	command_output_json_string(&output, status.playlist);
	command_output_printf(&output, ", \"player_count\": %ld, \"maximum_players\": %ld, \"uptime_seconds\": %lu, "
		"\"players\": [", count, status.maximum_players, status.uptime_seconds);
	for (index = 0; index < count; index++)
	{
		struct server_player const *player = &players[index];
		char const *team = !teams ? NULL : player->team_index == 0 ? "red" : player->team_index == 1 ? "blue" : NULL;
		unsigned char key[32];

		command_output_printf(&output, "%s{\"number\": %ld, \"name\": ", index ? ", " : "", player->number);
		command_output_json_string(&output, player->name);
		command_output_printf(&output, ", \"team\": %s%s%s, \"score\": ", team ? "\"" : "", team ? team : "null",
			team ? "\"" : "");
		if (player->scored)
			command_output_printf(&output, "%ld", player->score);
		else
			command_output_printf(&output, "null");
		command_output_printf(&output, ", \"moderator_key_verified\": %s}",
			server_moderation_machine_key(player->machine_index, key) ? "true" : "false");
	}
	command_output_printf(&output, "]}");
	if (!output.truncated)
		server_link_set_status(text);
	(void)game;
}
#endif

/* ---------- public code */

void server_commands_update(
	void)
{
	static struct command_actor const startup_actor = { "startup", "", SERVER_ROLE_OWNER, SERVER_PERMISSION_ALL };
	struct command_output output;

	if (!server_commands_globals.started)
	{
		server_commands_globals.started = TRUE;
		server_roles_start();
		load_startup_commands();
#ifdef HALO_SERVER
		server_control_start();
		server_link_begin();
#endif
	}
	server_moderation_update();
#ifdef HALO_SERVER
	/* the console's and the control API's, as they came */
	for (;;)
	{
		char line[COMMAND_LINE_MAXIMUM_LENGTH + 1];
		char source[96];
		int flags = 0;
		unsigned int permissions = 0;
		int role = 0;
		int ticket = server_control_next(line, sizeof(line), source, sizeof(source), &flags, &permissions, &role);
		struct command_actor actor;
		char *space;
		boolean ok;

		if (!ticket)
			break;
		if (flags & CONTROL_NOTICE)
		{
			error(_error_silent, "control: %s", line);
			continue;
		}
		if (!(flags & CONTROL_QUIET))
			error(_error_silent, "control: %s: %s", source, line);
		/* (the source: "console", "api <name> <id>", "web <name> <id>",
		"site <handle>") */
		space = strchr(source, ' ');
		if (space)
			*space = 0;
		actor.via = source;
		actor.name = space ? space + 1 : "";
		actor.role = role;
		actor.permissions = permissions;
		server_command_running.payload = server_control_payload(ticket);
		command_output_begin(&output, server_command_output, sizeof(server_command_output));
		ok = execute(line, (flags & CONTROL_JSON) ? TRUE : FALSE, &output, &actor);
		server_command_running.payload = NULL;
		/* (JSON cut short is no JSON) */
		if ((flags & CONTROL_JSON) && output.truncated)
		{
			command_output_begin(&output, server_command_output, sizeof(server_command_output));
			command_output_printf(&output, "{\"error\": \"the output is too long\"}");
			ok = FALSE;
		}
		else if (output.truncated)
			csstrcpy(output.text + output.size - 5, "...\n");
		server_control_finish(ticket, ok, server_command_output);
	}
#endif
#ifdef HALO_SERVER
	/* the site's (Delta Control's link), one a frame: run as the site's
	account, with the role this server's copy of the site's list gives it */
	{
		char line[COMMAND_LINE_MAXIMUM_LENGTH + 1];
		char handle[48];
		unsigned int id;

		if (server_link_next_command(line, sizeof(line), handle, sizeof(handle), &id))
		{
			struct command_actor actor;
			boolean ok;

			actor.via = "site";
			actor.name = handle;
			actor.role = server_roles_site_handle_role(handle);
			actor.permissions = server_role_permissions(actor.role);
			error(_error_silent, "control: site %s: %s", handle, line);
			command_output_begin(&output, server_command_output, sizeof(server_command_output));
			if (!actor.role)
			{
				command_output_printf(&output, "%s has no role on this server\n", handle);
				server_audit("site", handle, 0, line, NULL, "refused: no role", FALSE, NULL);
				ok = FALSE;
			}
			else
				ok = execute(line, FALSE, &output, &actor);
			log_output(server_command_output);
			server_link_finish_command(id, ok ? 1 : 0, server_command_output);
		}
		/* the server's state, for the site, every few seconds */
		if (system_milliseconds() - server_commands_globals.link_status_time >= 5000 ||
			!server_commands_globals.link_status_time)
		{
			server_commands_globals.link_status_time = system_milliseconds();
			link_status();
		}
	}
#endif
	/* the startup commands, once the server first hosts */
	if (!server_commands_globals.startup_run && hosting())
	{
		long index;

		server_commands_globals.startup_run = TRUE;
		for (index = 0; index < server_commands_globals.startup_count; index++)
		{
			char const *line = server_commands_globals.startup[index];

			error(_error_silent, "dedicated: startup: %s", line);
			command_output_begin(&output, server_command_output, sizeof(server_command_output));
			execute(line, FALSE, &output, &startup_actor);
			log_output(server_command_output);
		}
	}
}

#endif
