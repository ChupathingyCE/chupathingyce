/*
HALO_SERVER_BROWSER.H

The PC menus' Server Browser (Join Game's SERVER BROWSER mode,
port/linux/game/menu_functions.c) and the internet games it lists
(port/linux/game/server_browser.c, HALO_GAME_BROWSER): the one hook between
the two. The menus show the list's games as rows, in the order a column's
title chose, and join the one picked; where the games come from (the game
list, port/linux/src/browser.c), how one is reached (its invite) and which
can be played here (a Halo PC map in maps\ce) are the list's alone.

Joining is in two steps, as the list's own Online Games screen
(browser_screen.c) joins: server_browser_join_ready checks the game can be
joined and starts the network's search, then (the menus' player made
player 1) server_browser_join reaches its host; the host's game is joined
once it answers (server_browser_join_wait, asked each frame by the lobby the
menus open meanwhile).

A build without the game list (no HALO_GAME_BROWSER) has none of this: the
mode's list stays empty.
*/

#ifndef HALO_SERVER_BROWSER_H
#define HALO_SERVER_BROWSER_H

#define SERVER_BROWSER_MAXIMUM_GAMES 64
#define SERVER_BROWSER_NAME_LENGTH 32
#define SERVER_BROWSER_TEXT_LENGTH 128
/* a game's key: the same game's the same in each reading of the list */
#define SERVER_BROWSER_KEY_LENGTH 72

/* the orders (each the other way round with descending) */
enum
{
	/* the most players first */
	_server_browser_sort_players,
	_server_browser_sort_name,
	_server_browser_sort_map,
	_server_browser_sort_type,
	/* the shortest ping first (games whose ping is not known by name) */
	_server_browser_sort_ping,
	NUMBER_OF_SERVER_BROWSER_SORTS
};

struct server_browser_game
{
	char key[SERVER_BROWSER_KEY_LENGTH];
	wchar_t name[SERVER_BROWSER_NAME_LENGTH];
	/* its map as players name it, and its mark, short, for a map not of
	this game's own (L"PC": Halo PC's), else empty */
	wchar_t map[SERVER_BROWSER_NAME_LENGTH];
	wchar_t map_mark[SERVER_BROWSER_NAME_LENGTH];
	/* its gametype, short (L"Team Slayer") */
	wchar_t type[SERVER_BROWSER_NAME_LENGTH];
	short players;
	short maximum_players;
	/* milliseconds, NONE if not known */
	short ping;
	/* taking players (else under way, or full) */
	boolean open;
	/* its Players and Rules lines: who is in it, and how it is played */
	wchar_t players_text[SERVER_BROWSER_TEXT_LENGTH];
	wchar_t rules_text[SERVER_BROWSER_TEXT_LENGTH];
};

/* the list's games read again (the list asked for anew when it is a few
seconds old), in the order (descending: the other way round; games not
taking players last, whichever): their count, each then by its index */
short server_browser_read(short sort, boolean descending);
struct server_browser_game const *server_browser_game(short index);
/* why a game cannot be joined from here (its map missing), in message, or
FALSE if nothing stops it */
boolean server_browser_game_blocked(short index, wchar_t *message, short length);

/* a game to be joined: FALSE, and why, in message, if it cannot be (not
taking players, its map, the network); else the network searching, for
server_browser_join */
boolean server_browser_join_ready(short index, wchar_t *message, short length);
/* its host reached, its game to be joined once it answers; FALSE, and why,
if it cannot be */
boolean server_browser_join(short index, wchar_t *message, short length);
/* what became of the join begun (server_browser_join_wait) */
enum
{
	/* none begun */
	_server_browser_join_none,
	/* its host waited for (message: what is shown meanwhile) */
	_server_browser_join_waiting,
	/* the client joining the host's game */
	_server_browser_join_joined,
	/* message: why not */
	_server_browser_join_failed,
};

short server_browser_join_wait(wchar_t *message, short length);
/* the join forgotten (the menus left it) */
void server_browser_join_cancel(void);

#endif
