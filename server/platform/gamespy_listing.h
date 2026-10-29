/*
GAMESPY_LISTING.H

GameSpy-style status queries and master heartbeats (gamespy_listing.c), off
unless the server's settings turn them on (gamespy_listing, server/docs/
settings.md). One UDP port answers the two status query forms Halo Custom
Edition 1.0.10's own server uses (GameSpy's "\basic\" and QR2's FE FD 00),
and, toward each master in a list, sends QR2 heartbeats and answers the
master's challenge.

Built with the host's ABI (server_control.h's rule): only int and char
cross here. The game's thread (server/src/server_gamespy.c) hands the
server's state over once a second; the socket has its own thread.
*/

#ifndef GAMESPY_LISTING_H
#define GAMESPY_LISTING_H

enum
{
	GAMESPY_TEXT_SIZE = 64,
	GAMESPY_SHORT_TEXT_SIZE = 32,
	/* the player rows a reply carries, at most */
	GAMESPY_MAXIMUM_PLAYERS = 32,
	/* a reply, at most */
	GAMESPY_PACKET_SIZE = 1400,
	/* the default port: Custom Edition's own */
	GAMESPY_DEFAULT_PORT = 2302
};

struct gamespy_player
{
	char name[GAMESPY_SHORT_TEXT_SIZE];
	int score;
	/* milliseconds, or -1 when not known */
	int ping;
	/* 0 red, 1 blue */
	int team;
};

struct gamespy_status
{
	char hostname[GAMESPY_TEXT_SIZE];
	/* as Custom Edition names the map (gamespy_map_name) */
	char mapname[GAMESPY_TEXT_SIZE];
	/* Slayer, CTF, King, Oddball, Race, ... */
	char gametype[GAMESPY_SHORT_TEXT_SIZE];
	char gamevariant[GAMESPY_SHORT_TEXT_SIZE];
	/* this server's own version (the bridge_version key) */
	char version[GAMESPY_SHORT_TEXT_SIZE];
	int maxplayers;
	int teamplay;
	int fraglimit;
	int password;
	/* a game is being played (else the lobby or the carnage report) */
	int playing;
	/* every player, though at most GAMESPY_MAXIMUM_PLAYERS have rows */
	int numplayers;
	int player_count;
	struct gamespy_player players[GAMESPY_MAXIMUM_PLAYERS];
};

/* the socket on port (every address), heartbeats to masters ("host:port"
entries, comma separated; empty: none), and gamever the version reported:
1, else 0 and why in problem */
int gamespy_listing_start(int port, const char *masters, const char *gamever, char *problem, int problem_size);
/* the server's state now (copied) */
void gamespy_listing_publish(const struct gamespy_status *status);
/* the masters told the server is gone, and the socket closed */
void gamespy_listing_stop(void);
int gamespy_listing_running(void);
/* the next line for the server's log, if any: 1, else 0 */
int gamespy_listing_next_notice(char *text, int size);

/* ---------- the parts the tests use */

/* a playlist's map ("levels\test\bloodgulch\bloodgulch", "timberland@ce",
"icefields@pc") as Custom Edition names it: an Xbox map as xbox_<name>, a
Custom Edition or Halo PC map by its own name, any other by its name */
void gamespy_map_name(const char *map, char *out, int size);
/* the reply to a query (its length bytes): its length, or 0 for none.
gamever is the version reported */
int gamespy_reply(const struct gamespy_status *status, const char *gamever, const unsigned char *query, int length,
	unsigned char *out, int size);
/* a heartbeat: statechanged 0 (none), 1 (changed), 2 (gone) or 3 (first),
localip as text and localport. Its length, or 0 */
int gamespy_heartbeat(const struct gamespy_status *status, const char *gamever, const unsigned char instance[4],
	int statechanged, const char *localip, int localport, unsigned char *out, int size);
/* the answer to a master's challenge (length bytes), its text into out */
void gamespy_challenge_response(const unsigned char *challenge, int length, char *out, int size);

#endif
