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
/* a player's name (UTF-16, as the game's: 11 characters and a 0) */
#define BROWSER_PLAYER_NAME_LENGTH 12
#define BROWSER_MAP_LENGTH 64
#define BROWSER_MAXIMUM_GAMES 64
/* a host's roster: the players it announces (as many as a game takes), and
those a listed game keeps (as many as the Online Games screen shows) */
#define BROWSER_HOSTED_ROSTER 128
#define BROWSER_LISTED_ROSTER 16
/* a listed game's machines by platform: Delta's registry
(NUMBER_OF_DELTA_PLATFORMS), then those without Delta */
#define BROWSER_MACHINE_KINDS 11

/* how a listed game is hosted, and on what protocol, as the list says
(docs/delta.md, Delta List): not known (an older host, or one listed by its
invite) is 0 */
enum
{
	BROWSER_HOSTING_UNKNOWN,
	BROWSER_HOSTING_PLAYER,
	BROWSER_HOSTING_DEDICATED,
	/* one of ChupathingyCE's own dedicated servers */
	BROWSER_HOSTING_OFFICIAL,
};
enum
{
	BROWSER_PROTOCOL_UNKNOWN,
	BROWSER_PROTOCOL_DELTA,
	/* OpenCE's protocol alone: an OpenCE host, or one with Delta off */
	BROWSER_PROTOCOL_OPENCE,
};

/* a player of a game's roster */
struct browser_roster_player
{
	/* (UTF-16, as the game's names) */
	unsigned short name[BROWSER_PLAYER_NAME_LENGTH];
	/* its team, -1 in a game without teams */
	short team;
};

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
	/* who is in it, as its host announces it (none from hosts that do not:
	OpenCE's, and links added on the site); roster_count may be more than
	the players kept */
	short roster_count;
	struct browser_roster_player roster[BROWSER_LISTED_ROSTER];
	/* its host, as the list says (BROWSER_HOSTING_*, BROWSER_PROTOCOL_*):
	its platform (delta.h's enum delta_platform; has_platform 0 when not
	said), and its machines by platform (all 0 when not said) */
	unsigned char has_platform;
	unsigned char host_platform;
	unsigned char hosting;
	unsigned char protocol;
	unsigned char machines[BROWSER_MACHINE_KINDS];
};

/* one player's line of a finished game's carnage report */
struct browser_report_player
{
	/* (UTF-16, as the game's names) */
	unsigned short name[BROWSER_PLAYER_NAME_LENGTH];
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
	/* what the game's statistics recorder adds to the line
	(port/linux/game/game_stats.c): JSON members, without braces ("medals":
	{...}, "weapons": {...}, ...), or NULL */
	const char *extra;
};

/* (the most a line's extra, and a report's, may be) */
#define BROWSER_REPORT_EXTRA_SIZE 1024
#define BROWSER_REPORT_GAME_EXTRA_SIZE 8192

/* a hosted game that ended (reached the postgame): its carnage report, sent
to the list server if the game is listed there (game_engine.c); extra, JSON
members the report adds (the recorder's: who left early, how each part was
known), or NULL */
void browser_report_game(int teams, int red_score, int blue_score, int duration_seconds,
	const struct browser_report_player *players, int count, const char *extra);

/* a game this machine joined through an invite that ended: its report as
this machine saw it (the host's statistics and scores, which every client
is sent, and what this machine's own game saw of the kills), sent to the
list server with this copy's player key, whoever hosted the game (a host of
the game list's own builds reports it too; one of other builds does not),
so that the server can record the games of hosts that do not report
(port/linux/game/game_stats.c). extra: the report's other JSON members (the
map, the game type, the reporting player, ...). */
void browser_client_report(int teams, int red_score, int blue_score, int duration_seconds,
	const struct browser_report_player *players, int count, const char *extra);

/* a player's name as a JSON string, appended to out (UTF-8, quoted and
escaped); returns its length */
int browser_json_name(char *out, int size, const unsigned short *name, int length);

/* the hosted game, as the game's server has it; called each frame while
this machine hosts (network_server_manager.c). The listing follows (and is
withdrawn a few seconds after the calls stop). */
void browser_host_update(const unsigned short *name, const char *map, short engine, short players,
	short maximum_players, int open, short score_limit, int teams,
	const struct browser_roster_player *roster, int roster_count);

/* the game list's address as a player types it (halo.milenko.org: no
scheme), empty for none */
void browser_server_name(char *text, int size);

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

/* the invite this copy probes (server/src/probe.c: HALO_PROBE names it,
its digits), NULL if it is not a probe: it reads the game the invite leads
to, prints it, and quits */
const char *browser_probe(void);

/* whether this copy runs without a window, sound or a player: the
dedicated server or a probe */
int browser_headless(void);

/* the local players of a game that just ended, by name: their lines in its
carnage report confirmed with this copy's player key (browser.c); and the
public player ID it confirms them as */
void browser_claim_game(const unsigned short (*names)[BROWSER_PLAYER_NAME_LENGTH], int count);
int browser_player_id(char *text, int size);
/* the moderator key (Delta Peer's moderation, docs/delta.md): an Ed25519
key pair made from this copy's player key, whose public half a dedicated
server's moderators file names. Its signature of a message and its public
key: 1, else 0 (no player key). The public key alone as 64 hex digits */
int browser_moderator_sign(const unsigned char *message, int size, unsigned char *public_key,
	unsigned char *signature);
int browser_moderator_key(char *text, int size);

/* the profile page (halo.milenko.org/profile), signed in as this copy's
player, opened in the web browser (MY PROFILE) */
void browser_open_profile(void);
/* a restored key (halo://key/...): kept, then put in place of this copy's
once the player says yes (sdl_platform.c asks) */
int browser_key_link(const char *text);
int browser_take_key_link(char *new_id, char *old_id, int size);
void browser_answer_key_link(int install);

/* Link Profile (Online Games' RB): the game linked to the player's profile
from another device, where no web browser opens (Steam's Game Mode, a
console). The list server gives a short code for this copy's player key;
the player types it at <server>/connect on a phone or any computer, signed
in, and the game hears that they did (browser.c asks every few seconds) and
asks the player to confirm the profile it is to be linked to. */
enum
{
	BROWSER_CONNECT_OFF,
	/* a code asked for */
	BROWSER_CONNECT_STARTING,
	/* a code shown, waiting for it to be typed */
	BROWSER_CONNECT_WAITING,
	/* the code typed: the player asked whether to link to that profile */
	BROWSER_CONNECT_CONFIRM,
	BROWSER_CONNECT_CONNECTED,
	/* the player said no */
	BROWSER_CONNECT_DECLINED,
	BROWSER_CONNECT_EXPIRED,
	BROWSER_CONNECT_FAILED,
};

/* (a QR code of version 10 at most: 57 modules square) */
#define BROWSER_CONNECT_QR_SIZE 57

struct browser_connect
{
	int state;
	/* the code to type (K7Q-4MD), and how many seconds it (or the confirm
	question) is good for yet */
	char code[16];
	int seconds;
	/* the question answered, the answer on its way */
	int answered;
	/* where to type it, as a player types it (halo.milenko.org/connect) */
	char page[128];
	/* who typed the code (confirm) and who the game was linked to before
	(empty if none), then who it is linked to (connected); what went wrong
	(failed) */
	char handle[64];
	char previous[64];
	char message[96];
	/* the page with the code in it as a QR code: qr_size modules square,
	nonzero for a dark one (qr_size 0: none) */
	int qr_size;
	unsigned char qr[BROWSER_CONNECT_QR_SIZE * BROWSER_CONNECT_QR_SIZE];
};

/* a new code asked for (the one before forgotten), with the name of the
profile it links (UTF-16, as the game's names; NULL for none), and the
asking stopped (the panel closed) */
void browser_connect_start(const unsigned short *name);
void browser_connect_stop(void);
/* the player's answer to the confirm question */
void browser_connect_answer(int accept);
void browser_connect_get(struct browser_connect *connect);

#endif
