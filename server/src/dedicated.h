/*
DEDICATED.H

The dedicated server's director (dedicated.c), as its commands
(server_commands.c) see it.
*/

#ifndef DEDICATED_H
#define DEDICATED_H

enum
{
	/* a playlist's entries, at most */
	DEDICATED_MAXIMUM_ENTRIES = 64,
	/* a map's name or path, and a game type's name, with their ends */
	DEDICATED_MAP_SIZE = 128,
	DEDICATED_VARIANT_SIZE = 32,
};

enum
{
	/* where the server is */
	_dedicated_state_starting,
	_dedicated_state_lobby,
	_dedicated_state_loading,
	_dedicated_state_in_game,
	_dedicated_state_postgame,
};

struct dedicated_status
{
	short state;
	/* what is played (or next, in the lobby): the playlist's entry, or a
	map and game type a command chose (sv_map) */
	char map[DEDICATED_MAP_SIZE];
	char variant[DEDICATED_VARIANT_SIZE];
	boolean chosen;
	/* a map and game type a command chose, to follow the game in progress
	(empty if none) */
	char next_map[DEDICATED_MAP_SIZE];
	char next_variant[DEDICATED_VARIANT_SIZE];
	long entry;
	long entry_count;
	char playlist[DEDICATED_MAP_SIZE];
	long minimum_players;
	long maximum_players;
	long idle_limit;
	boolean public_game;
	char name[16];
	unsigned long uptime_seconds;
};

boolean dedicated_server_active(void);
void dedicated_server_get_status(struct dedicated_status *status);

/* the playlist's entries */
long dedicated_playlist_count(void);
char const *dedicated_playlist_map(long entry);
char const *dedicated_playlist_variant(long entry);

/* a map (as a playlist names it) and game type played next, at once: the
game in progress ends; then the playlist goes on where it was */
void dedicated_server_play(char const *map, char const *variant);
/* the next of the playlist's entries, at once (the game in progress ends) */
void dedicated_server_skip(void);
/* the game in progress ends, as its score limit would end it; FALSE if none
is */
boolean dedicated_server_end_game(void);

void dedicated_server_set_maximum_players(long maximum_players);
void dedicated_server_set_name(char const *name);

#endif
