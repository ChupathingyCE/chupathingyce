/*
BROWSER.H

The game list (configure.py --game-browser,
HALO_GAME_BROWSER): the system link games hosted by copies of the game
anywhere, listed on network.browser_url (tools/list_server.py). A host's
game is listed with its invite (p2p.c); a player picks a listed game, which
joins its invite, and the host's game then shows in System Link as any
game reached through an invite. See browser.c.
*/

#ifndef __BROWSER_H
#define __BROWSER_H

#define BROWSER_INVITE_LENGTH 44
#define BROWSER_NAME_LENGTH 16
#define BROWSER_MAP_LENGTH 64
#define BROWSER_MAXIMUM_GAMES 64

struct browser_game
{
	char invite[BROWSER_INVITE_LENGTH + 1];
	/* (UTF-16, as the game's names) */
	unsigned short name[BROWSER_NAME_LENGTH];
	char map[BROWSER_MAP_LENGTH];
	short engine;
	short players;
	short maximum_players;
	unsigned char open;
	unsigned char teams;
	unsigned short version;
	short score_limit;
};

/* one player's line of a finished game's carnage report */
struct browser_report_player
{
	/* (UTF-16, as the game's names) */
	unsigned short name[12];
	short team;
	short place;
	int score;
	short kills;
	short assists;
	short deaths;
	short betrayals;
	short suicides;
	short multikills;
	int shots_fired;
	int shots_hit;
};

/* a hosted game that ended (reached the postgame): its carnage report, sent
to the list server if the game is listed there (game_engine.c) */
void browser_report_game(int teams, int red_score, int blue_score, int duration_seconds,
	const struct browser_report_player *players, int count);

/* the hosted game, as the game's server has it; called each frame while
this machine hosts (network_server_manager.c). The listing follows (and is
withdrawn a few seconds after the calls stop). */
void browser_host_update(const unsigned short *name, const char *map, short engine, short players,
	short maximum_players, int open, short score_limit, int teams);

/* the listed games, asking the server for the list again if the last one
is more than a few seconds old: those of this machine's network version,
without this machine's own. Returns their count. */
int browser_get_games(struct browser_game *games, int maximum_count);

/* whether a listed game's host is an internet play peer of this machine
(joining it, or joined): its address in the game's network then */
int browser_game_peer(const char *invite, unsigned long *address);

/* joins a listed game: its invite, as an invite link would (p2p.c) */
int browser_join(const char *invite);

#endif
