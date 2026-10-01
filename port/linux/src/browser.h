/*
BROWSER.H

The game list (configure.py --game-browser,
HALO_GAME_BROWSER): the system link games hosted by copies of the game
anywhere, listed on network.browser_url (halo.milenko.org). A host's
game is listed with its invite (p2p.c); a player picks a listed game, which
joins its invite, and the host's game then shows in System Link as any
game reached through an invite. See browser.c.
*/

#ifndef __BROWSER_H
#define __BROWSER_H

/* an invite's code: the host's key hash and the token, in hexadecimal
(p2p_internal.h's P2P_LINK_SIZE, without "halo://join/") */
#define BROWSER_INVITE_LENGTH 64
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
	/* the player's armor (the profile's color, 0 to 17) */
	short color;
	/* the game type's own: flags grabbed, returned and scored (CTF), seconds
	with the ball and ball carriers killed (Oddball), seconds on the hill
	(King), laps (Race) */
	short flag_grabs;
	short flag_returns;
	short flag_scores;
	short ball_time;
	short ball_carrier_kills;
	short hill_time;
	short laps;
	/* the IPv4 address the host's game has the player's machine at (an
	internet player's virtual one), 0 for the host's own: from it the host
	tags the player's line, so that only their machine may confirm it
	(browser.c); the address itself is not sent */
	unsigned long address;
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

/* whether this copy is the dedicated server (server/src/dedicated.c:
HALO_DEDICATED names its playlist): it joins no invite, leaves the clipboard
alone and plays no sound */
int browser_dedicated(void);

/* the local players of a game that just ended, by name: their lines in its
carnage report confirmed with this copy's player key (browser.c); and the
public player ID it confirms them as */
void browser_claim_game(const unsigned short (*names)[12], int count);
int browser_player_id(char *text, int size);

/* the profile page (halo.milenko.org/profile), signed in as this copy's
player, opened in the web browser (MY PROFILE) */
void browser_open_profile(void);
/* a restored key (halo://key/...): kept, then put in place of this copy's
once the player says yes (sdl_platform.c asks) */
int browser_key_link(const char *text);
int browser_take_key_link(char *new_id, char *old_id, int size);
void browser_answer_key_link(int install);

#endif
