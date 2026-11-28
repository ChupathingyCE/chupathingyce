/*
DIRECT_JOIN.H

Joining a game by its address (direct_join.c): System Link's way, to a host
the broadcast doesn't reach. The menus' DIRECT IP (menu_functions.c), the
console's connect and the command line's --connect all start it here.
*/

#ifndef DIRECT_JOIN_H
#define DIRECT_JOIN_H

#ifdef HALO_GAME_BROWSER

enum
{
	/* the addresses kept (the save folder's direct_ip.txt), newest first */
	DIRECT_JOIN_SAVED_MOST = 5,
	/* a typed address's longest, with its terminator */
	DIRECT_JOIN_TEXT_SIZE = 262,
	/* a saved one's name's */
	DIRECT_JOIN_NAME_SIZE = 32,
	/* a status line's */
	DIRECT_JOIN_STATUS_SIZE = 96,
};

/* the game at text (an address or host name, with a port or not) joined:
TRUE once it is being looked for, FALSE with the reason in
direct_join_status. The address is never shown or logged as it is: only
the menus' own field had it */
boolean direct_join_start(char const *text);
/* each frame (ui_widget.c, before the menus): the host looked for, its game
joined once heard, its lobby opened */
void direct_join_update(void);
/* whether a join by address is under way (looked up, looked for) */
boolean direct_join_active(void);
/* the join given up (B on the screen) */
void direct_join_cancel(void);
/* the last word on it, for the screen's help line ("" none) */
char const *direct_join_status(void);
/* the DIRECT IP screen is up (each frame it is): a join it started is
given up once it closes before the game is found */
void direct_join_screen_alive(void);

/* the saved addresses: their count, and one's name as a list shows it
(never the address: direct_address_saved_name) */
short direct_join_saved_count(void);
boolean direct_join_saved_name(short index, char *name, int size);
/* the saved address at index joined (TRUE as direct_join_start) */
boolean direct_join_saved_start(short index);

/* the console's connect <address>[:port] [password]: the same join, its
words in the console */
void direct_join_console(char const *arguments);

#endif

#endif
