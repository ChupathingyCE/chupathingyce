/*
SERVER_BROWSER.C

The PC menus' Server Browser (halo_server_browser.h; configure.py
--game-browser): the games of the game list (port/linux/src/browser.c), as
the Online Games screen (browser_screen.c) has them, for Join Game's SERVER
BROWSER mode (menu_functions.c) to show and join.

A game is named, its map named and marked, and joined as Online Games does
it, with its words: a game on a Custom Edition map (Halo PC's, announced as
<file>@ce) named as the menus' map list names it (ui_map_list.c) and marked
HALO PC; joined only with the map in maps\ce, on a build with Halo PC map
support (HALO_CUSTOM_EDITION); through its invite (browser_join, as an
invite link would), the host's game then joined once it is advertised
through the tunnel (network_game_client_join_invite_host).
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "game/game.h"
#include "main/main.h"
#include "networking/network_game_globals.h"
#include "text/unicode.h"
#include "../src/browser.h"
#include "halo_server_browser.h"
#include "halo_ui_map_list.h"

#include <stdio.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* a picked game's host answers this soon, or it is given up on
	(browser_screen.c's) */
	CONNECT_TIMEOUT = 15000,
	/* whether a game's Custom Edition map is in maps\ce, asked again this
	often */
	CE_MAP_CHECK_INTERVAL = 1000,
};

/* whether a game's map can be played here (browser_screen.c's) */
enum
{
	_ce_map_none,
	_ce_map_present,
	_ce_map_missing,
	_ce_map_unsupported,
};

/* the game's engines, short (as players say them) to fit the column
(browser_screen.c's) */
static wchar_t const *const engine_names[] =
{
	L"Game", L"CTF", L"Slayer", L"Oddball", L"King", L"Race",
};

/* the multiplayer maps' names in the menus (browser_screen.c's) */
static char const *const map_names[][2] =
{
	{ "beavercreek", "Battle Creek" }, { "bloodgulch", "Blood Gulch" }, { "boardingaction", "Boarding Action" },
	{ "carousel", "Derelict" }, { "chillout", "Chill Out" }, { "damnation", "Damnation" },
	{ "hangemhigh", "Hang 'Em High" }, { "longest", "Longest" }, { "prisoner", "Prisoner" },
	{ "putput", "Chiron TL-34" }, { "ratrace", "Rat Race" }, { "sidewinder", "Sidewinder" }, { "wizard", "Wizard" },
};

/* ---------- prototypes */

/* (network_client_manager.c: the game whose host's identifier the invite
starts with, joined once it is advertised) */
long network_game_client_join_invite_host(char const *invite);

/* ---------- globals */

static struct
{
	/* the list as last read, and its games as shown */
	struct browser_game listed[SERVER_BROWSER_MAXIMUM_GAMES];
	struct server_browser_game games[SERVER_BROWSER_MAXIMUM_GAMES];
	short order[SERVER_BROWSER_MAXIMUM_GAMES];
	short count;
	short sort;
	boolean descending;
	/* the last game's map asked of maps\ce, and the answer */
	char ce_map[BROWSER_MAP_LENGTH];
	short ce_map_answer;
	unsigned long ce_map_time;
	/* a game being joined: its invite, while its host is waited for; or
	why it failed */
	boolean joining;
	boolean join_failed;
	char join_invite[BROWSER_INVITE_LENGTH + 1];
	wchar_t join_name[SERVER_BROWSER_NAME_LENGTH];
	wchar_t join_message[SERVER_BROWSER_TEXT_LENGTH];
	unsigned long join_time;
} server_browser;

/* ---------- private code */

static void text_from_ascii(char const *ascii, wchar_t *text, short length)
{
	short index;

	for (index = 0; ascii[index] && index < length - 1; index++)
		text[index] = (wchar_t)(unsigned char)ascii[index];
	text[index] = 0;
}

static void text_from_name(unsigned short const *name, short name_length, wchar_t *text, short length)
{
	short index;

	for (index = 0; index < name_length && name[index] && index < length - 1; index++)
		text[index] = (wchar_t)name[index];
	text[index] = 0;
}

/* a game's map: its file's name (the path's last part, without a Custom
Edition map's @ce), and whether it is a Custom Edition map */
static boolean map_file(char const *path, char *file, long size)
{
	char const *base = path;
	char const *cursor;
	long length;
	boolean ce;

	for (cursor = path; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/')
			base = cursor + 1;
	}
	length = (long)strlen(base);
	ce = length >= 3 && base[length - 3] == '@' && (base[length - 2] | 0x20) == 'c' && (base[length - 1] | 0x20) == 'e';
	if (ce)
		length -= 3;
	snprintf(file, (size_t)size, "%.*s", (int)length, base);
	return ce;
}

/* a game's map as players name it: an Xbox map's name in the menus, or a
Custom Edition map's as the menus' map list names it */
static void map_display_name(char const *path, wchar_t *text, short length)
{
	char file[BROWSER_MAP_LENGTH];
	short index;

	if (map_file(path, file, sizeof(file)))
	{
		ui_map_list_ce_name(file, text, length);
		if (!text[0])
			text_from_ascii("Unknown map", text, length);
		return;
	}
	for (index = 0; index < NUMBEROF(map_names); index++)
	{
		if (!strcmp(file, map_names[index][0]))
		{
			text_from_ascii(map_names[index][1], text, length);
			return;
		}
	}
	text_from_ascii(file, text, length);
}

/* whether a game's map can be played here: an Xbox map, or a Custom Edition
map in maps\ce, missing, or on a build without them (maps\ce asked again
for another map, or now and then) */
static short ce_map_state(struct browser_game const *game)
{
	char file[BROWSER_MAP_LENGTH];

	if (!map_file(game->map, file, sizeof(file)))
		return _ce_map_none;
#ifdef HALO_CUSTOM_EDITION
	if (strcmp(server_browser.ce_map, game->map) ||
		system_milliseconds() - server_browser.ce_map_time > CE_MAP_CHECK_INTERVAL)
	{
		csstrncpy(server_browser.ce_map, game->map, sizeof(server_browser.ce_map) - 1);
		server_browser.ce_map[sizeof(server_browser.ce_map) - 1] = 0;
		server_browser.ce_map_answer = ui_map_list_ce_present(file) ? _ce_map_present : _ce_map_missing;
		server_browser.ce_map_time = system_milliseconds();
	}
	return server_browser.ce_map_answer;
#else
	return _ce_map_unsupported;
#endif
}

/* what this machine lacks to join a game on a Custom Edition map (the
Online Games screen's words), or FALSE */
static boolean ce_map_blocker(struct browser_game const *game, wchar_t *message, short length)
{
	char file[BROWSER_MAP_LENGTH], text[BROWSER_MAP_LENGTH + 32];

	switch (ce_map_state(game))
	{
	case _ce_map_missing:
		map_file(game->map, file, sizeof(file));
		snprintf(text, sizeof(text), "Needs maps/ce/%s.map to join", file);
		break;
	case _ce_map_unsupported:
		snprintf(text, sizeof(text), "Halo PC maps need ChupathingyCE with Halo PC map support.");
		break;
	default:
		return FALSE;
	}
	text_from_ascii(text, message, length);
	return TRUE;
}

static void type_name(struct browser_game const *game, wchar_t *text, short length)
{
	short engine = game->engine >= 1 && game->engine < NUMBEROF(engine_names) ? game->engine : 0;

	/* (Capture the Flag is played in teams alone) */
	usnprintf(text, length - 1, L"%s%s", game->teams && engine != 1 ? L"Team " : L"", engine_names[engine]);
	text[length - 1] = 0;
}

/* the Players line: how many, and who (the host's roster, when it sends
one) */
static void players_text(struct browser_game const *game, wchar_t *text, short length)
{
	short used, index, kept;

	usnprintf(text, length - 1, L"%d of %d", game->players, game->maximum_players);
	text[length - 1] = 0;
	if (!game->players && !game->roster_count)
		return;
	used = (short)ustrlen(text);
	if (!game->roster_count)
	{
		usnprintf(text + used, length - 1 - used, L": this host doesn't share names");
		text[length - 1] = 0;
		return;
	}
	kept = (short)MIN(game->roster_count, BROWSER_LISTED_ROSTER);
	for (index = 0; index < kept; index++)
	{
		wchar_t name[NUMBEROF(game->roster[index].name) + 1];

		text_from_name(game->roster[index].name, NUMBEROF(game->roster[index].name), name, NUMBEROF(name));
		used = (short)ustrlen(text);
		/* (room kept for what is left unsaid) */
		if (used + (short)ustrlen(name) + 16 >= length)
			break;
		usnprintf(text + used, length - 1 - used, L"%s%s", index ? L", " : L": ", name);
		text[length - 1] = 0;
	}
	if (index < game->roster_count)
	{
		used = (short)ustrlen(text);
		usnprintf(text + used, length - 1 - used, L" +%d more", game->roster_count - index);
		text[length - 1] = 0;
	}
}

/* the Rules line: the gametype, its score, the map, whether it is under
way */
static void rules_text(struct server_browser_game const *shown, struct browser_game const *game, wchar_t *text,
	short length)
{
	short used;

	usnprintf(text, length - 1, L"%s", shown->type);
	text[length - 1] = 0;
	if (game->score_limit)
	{
		used = (short)ustrlen(text);
		usnprintf(text + used, length - 1 - used, L" to %d", game->score_limit);
	}
	used = (short)ustrlen(text);
	usnprintf(text + used, length - 1 - used, L" on %s", shown->map);
	/* (Halo PC's map: marked as Online Games marks it) */
	if (shown->map_mark[0])
	{
		used = (short)ustrlen(text);
		usnprintf(text + used, length - 1 - used, L" (HALO %s)", shown->map_mark);
	}
	used = (short)ustrlen(text);
	usnprintf(text + used, length - 1 - used, L"%s", game->open ? L"" : L": under way");
	text[length - 1] = 0;
}

static void game_show(struct browser_game const *game, struct server_browser_game *shown)
{
	char file[BROWSER_MAP_LENGTH];

	csmemset(shown, 0, sizeof(*shown));
	csstrncpy(shown->key, game->invite, sizeof(shown->key) - 1);
	text_from_name(game->name, BROWSER_NAME_LENGTH, shown->name, NUMBEROF(shown->name));
	map_display_name(game->map, shown->map, NUMBEROF(shown->map));
	if (map_file(game->map, file, sizeof(file)))
		text_from_ascii("PC", shown->map_mark, NUMBEROF(shown->map_mark));
	type_name(game, shown->type, NUMBEROF(shown->type));
	shown->players = game->players;
	shown->maximum_players = game->maximum_players;
	/* (the game list has no pings yet) */
	shown->ping = NONE;
	shown->open = game->open != 0;
	players_text(game, shown->players_text, NUMBEROF(shown->players_text));
	rules_text(shown, game, shown->rules_text, NUMBEROF(shown->rules_text));
}

static long compare_text(wchar_t const *a, wchar_t const *b)
{
	for (;; a++, b++)
	{
		wchar_t x = *a >= 'a' && *a <= 'z' ? (wchar_t)(*a - 32) : *a;
		wchar_t y = *b >= 'a' && *b <= 'z' ? (wchar_t)(*b - 32) : *b;

		if (x != y || !x)
			return (long)x - (long)y;
	}
}

/* the order of two games (by their places in the list read) */
static long compare_games(short a_index, short b_index)
{
	struct server_browser_game const *a = &server_browser.games[a_index];
	struct server_browser_game const *b = &server_browser.games[b_index];
	struct browser_game const *a_game = &server_browser.listed[a_index];
	struct browser_game const *b_game = &server_browser.listed[b_index];
	long order;

	/* (games not taking players last, whatever the order) */
	if (a->open != b->open)
		return a->open ? -1 : 1;
	switch (server_browser.sort)
	{
	case _server_browser_sort_name: order = compare_text(a->name, b->name); break;
	case _server_browser_sort_map:
		order = compare_text(a->map, b->map);
		/* (an Xbox map before Halo PC's of the same name) */
		if (!order)
			order = (long)(a->map_mark[0] != 0) - (long)(b->map_mark[0] != 0);
		break;
	case _server_browser_sort_type:
		order = (long)a_game->engine * 2 + a_game->teams - ((long)b_game->engine * 2 + b_game->teams);
		break;
	case _server_browser_sort_ping:
		order = a->ping == b->ping ? 0 : a->ping == NONE ? 1 : b->ping == NONE ? -1 : (long)a->ping - (long)b->ping;
		break;
	default: order = (long)b->players - (long)a->players; break;
	}
	if (server_browser.descending)
		order = -order;
	return order ? order : compare_text(a->name, b->name);
}

static struct browser_game const *listed_game(short index)
{
	return index >= 0 && index < server_browser.count ? &server_browser.listed[server_browser.order[index]] : NULL;
}

/* ---------- public code */

short server_browser_read(short sort, boolean descending)
{
	short index, other;

	server_browser.sort = sort;
	server_browser.descending = descending;
	server_browser.count = (short)browser_get_games(server_browser.listed, SERVER_BROWSER_MAXIMUM_GAMES);
	for (index = 0; index < server_browser.count; index++)
		game_show(&server_browser.listed[index], &server_browser.games[index]);
	/* (insertion: a few dozen games) */
	for (index = 0; index < server_browser.count; index++)
	{
		for (other = index; other > 0 && compare_games(server_browser.order[other - 1], index) > 0; other--)
			server_browser.order[other] = server_browser.order[other - 1];
		server_browser.order[other] = index;
	}
	return server_browser.count;
}

struct server_browser_game const *server_browser_game(short index)
{
	return index >= 0 && index < server_browser.count ? &server_browser.games[server_browser.order[index]] : NULL;
}

boolean server_browser_game_blocked(short index, wchar_t *message, short length)
{
	struct browser_game const *game = listed_game(index);

	return game && ce_map_blocker(game, message, length);
}

boolean server_browser_join_ready(short index, wchar_t *message, short length)
{
	struct browser_game const *game = listed_game(index);

	if (!game)
		return FALSE;
	if (!game->open)
	{
		text_from_ascii("That game is not accepting players.", message, length);
		return FALSE;
	}
	/* (a Custom Edition map's game, only with the map: a join without it
	would fail at its loading) */
	if (ce_map_blocker(game, message, length))
		return FALSE;
	/* a network client searching, as System Link's (the advertisement comes
	to it) */
	if (!global_network_game_client_get())
	{
		if (!create_global_network_game_client())
		{
			text_from_ascii("Could not start the network.", message, length);
			return FALSE;
		}
		game_connection_set(_game_connection_network_client);
	}
	return TRUE;
}

boolean server_browser_join(short index, wchar_t *message, short length)
{
	struct browser_game const *game = listed_game(index);

	server_browser_join_cancel();
	if (!game)
		return FALSE;
	if (!browser_join(game->invite))
	{
		text_from_ascii("Internet play is off (network.online in config.toml).", message, length);
		return FALSE;
	}
	server_browser.joining = TRUE;
	csstrncpy(server_browser.join_invite, game->invite, sizeof(server_browser.join_invite) - 1);
	server_browser.join_invite[sizeof(server_browser.join_invite) - 1] = 0;
	text_from_name(game->name, BROWSER_NAME_LENGTH, server_browser.join_name, NUMBEROF(server_browser.join_name));
	server_browser.join_time = system_milliseconds();
	return TRUE;
}

short server_browser_join_wait(wchar_t *message, short length)
{
	long joined;

	if (server_browser.join_failed)
	{
		ustrncpy(message, server_browser.join_message, length - 1);
		message[length - 1] = 0;
		return _server_browser_join_failed;
	}
	if (!server_browser.joining)
		return _server_browser_join_none;
	joined = network_game_client_join_invite_host(server_browser.join_invite);
	if (joined > 0)
	{
		server_browser.joining = FALSE;
		return _server_browser_join_joined;
	}
	if (joined < 0)
		text_from_ascii("That game can't be joined from this version.", server_browser.join_message,
			NUMBEROF(server_browser.join_message));
	else if (system_milliseconds() - server_browser.join_time > CONNECT_TIMEOUT)
		text_from_ascii("The host did not answer.", server_browser.join_message, NUMBEROF(server_browser.join_message));
	else
	{
		long dots = (long)((system_milliseconds() - server_browser.join_time) / 400 % 4);

		usnprintf(message, length - 1, L"Connecting to %s%s", server_browser.join_name, L"..." + 3 - dots);
		message[length - 1] = 0;
		return _server_browser_join_waiting;
	}
	server_browser.joining = FALSE;
	server_browser.join_failed = TRUE;
	ustrncpy(message, server_browser.join_message, length - 1);
	message[length - 1] = 0;
	return _server_browser_join_failed;
}

void server_browser_join_cancel(void)
{
	server_browser.joining = FALSE;
	server_browser.join_failed = FALSE;
	server_browser.join_message[0] = 0;
}

#endif
