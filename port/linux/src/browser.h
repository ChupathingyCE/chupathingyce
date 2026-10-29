/*
BROWSER.H

The game list (configure.py --game-browser,
HALO_GAME_BROWSER): the system link games hosted by copies of the game
anywhere, listed on network.browser_url (stats.chupathingy.org). A host's
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
#define BROWSER_MACHINE_KINDS 20

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
	/* a co-op game's difficulty, 1 Easy to 4 Legendary, from its gametype
	("Co-op Legend"); 0 when the list does not say */
	unsigned char difficulty;
	unsigned char machines[BROWSER_MACHINE_KINDS];
	/* (browser_get_games's, from what this machine knows itself) the round
	trip to its host in milliseconds: the tunnel's while it reaches it (a game
	being joined or played), else a ping before joining (p2p_probe.c) or an
	estimate (ping_estimated), else -1; and whether it has a
	password, as its host's public listing says (p2p_lobby.c, while the
	public listings are browsed: p2p_lobby_browse) */
	short ping;
	unsigned char locked;
	/* ping is an estimate (the list's region and this machine's round trips
	to the list's anchors: browser_ping.c), shown "~85"; else measured: the
	tunnel's, or a probe's before joining (p2p_probe.c) */
	unsigned char ping_estimated;
	/* the host's region as the list says ("EU-DE"; empty: not known), and
	its announced capabilities (Delta's registry: the ping bit says it
	answers pings) */
	char region[8];
	unsigned long capabilities;
	/* an Xbox map pack's map's name and its pack's, as its host announces
	them (the list's fields 20 and 21: "Gothra", "Halo CE+"; empty for
	another map, or a host that doesn't) */
	char map_title[48];
	char map_pack[48];
	/* the playlist its host plays, as the list says (field 23: a verified
	playlist's id, whose map set the join offers, docs/playlist-maps.md;
	empty for none) */
	char playlist[33];
	/* the kind its public listing says (p2p_listing's; 0 when it has none
	or isn't heard): OpenCE's game types and Hardcore, by name in the list */
	unsigned char kind;
	/* heard directly, not on the list: a public listing from the brokers
	(p2p_lobby.c) that the list lacks (browser_get_games) */
	unsigned char direct;
	/* an OpenCE dedicated server's game, by its public listing's flags
	(p2p_listing's opence_server): the lists' neutral label
	(HOST_LABEL_OPENCE_SERVER), nothing more */
	unsigned char opence_server;
};

/* the note a screen of listed games shows while the list is down and its
games are those found directly (browser_list_status: BROWSER_LIST_FAILED) */
#define BROWSER_LIST_OFFLINE_NOTE "The game list is offline; showing games found directly."

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
	/* nonzero: only the scoreboard's numbers go in the line (name, team,
	place, score, kills, deaths, and the host's tag): a player not on
	Chupathingy, on the host's report; anyone but this machine's own
	players, on a client's (game_stats_line_full) */
	int minimal;
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
/* (room for every player who left a full game early, at about 500 bytes
each; the original Xbox keeps the smaller one for its memory) */
#ifdef HALO_XBOX_CONSOLE
#define BROWSER_REPORT_GAME_EXTRA_SIZE 8192
#else
#define BROWSER_REPORT_GAME_EXTRA_SIZE 32768
#endif

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
map, the game type, the reporting player, ...). 1 if it is to be sent, 0
if not (no list server, reports of joined games off, or a game not joined
through an invite). */
int browser_client_report(int teams, int red_score, int blue_score, int duration_seconds,
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

/* the game list's address as a player types it (stats.chupathingy.org: no
scheme), empty for none */
void browser_server_name(char *text, int size);

/* the listed games, asking the server for the list again if the last one
is more than a few seconds old: with games of other network versions
(browser_game_version_state), without this machine's own. Returns their
count. The list's games come first; then, while the public listings are
followed (browser_follow_listings), each game heard on the brokers that the
list lacks (direct): an OpenCE (legacy) game while the list answers, and
any host's while it is down, so a list outage costs no game the brokers
carry. */
int browser_get_games(struct browser_game *games, int maximum_count);
/* whether this build can join a game by its network version: 0 if it can
(or may try), -1 if the game runs an older version, 1 a newer one */
int browser_game_version_state(const struct browser_game *game);
/* the same for a bare network version (a listing over the brokers has no
game list entry): -1 older, 1 newer, 0 joinable. Every build */
int browser_version_state(int version);
/* whether a game's map (a scenario's name or path) is a campaign level: a
co-op game */
int browser_map_is_campaign(const char *map);
/* the game lists' filter, kept for the session: all games, multiplayer
games, or co-op games (a campaign level). Every build */
enum
{
	BROWSER_LIST_FILTER_ALL,
	BROWSER_LIST_FILTER_MULTIPLAYER,
	BROWSER_LIST_FILTER_COOP,
	BROWSER_LIST_FILTERS
};
int browser_list_filter(void);
void browser_list_filter_cycle(void);
/* its name for a button or a hint: ALL, MULTIPLAYER, CO-OP */
const char *browser_list_filter_name(void);
/* whether a game on this map (a scenario's name or path) passes it */
int browser_list_filter_allows(const char *map);
/* the public listings (OpenCE's, p2p_lobby.c) followed while a screen of
listed games is up, for which of them have a password; and the games'
pings measured (p2p_probe.c) and estimated (browser_ping.c) meanwhile,
while internet play is on */
void browser_follow_listings(int on);
/* how the last asking for the list went, for a screen with no games to say
why: BROWSER_LIST_* */
enum
{
	BROWSER_LIST_ANSWERED,
	/* not answered yet */
	BROWSER_LIST_WAITING,
	/* the last asking failed (asked again after a while) */
	BROWSER_LIST_FAILED,
	/* no list (network.browser_url empty) */
	BROWSER_LIST_NONE,
};
int browser_list_status(void);

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

/* whether this copy's games count for a profile, as far as it knows: Link
Profile done on this copy (kept in the save root's game_list_profile.txt,
with the player ID it was done as), or the site's word that the player's
own ID has a profile (asked once a run, from the first call on); the
profile's handle in handle (empty when not linked). Never waits. */
int browser_profile_linked(char *handle, int size);

/* the profile page (stats.chupathingy.org/profile), signed in as this copy's
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
	/* where to type it, as a player types it (stats.chupathingy.org/connect) */
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

/* Delta Profile in the game (port/linux/game/profile_screen.c;
delta_profile.h): this copy's own profile, as the site has it, kept in
memory only. browser_profile_load asks the site for it (in the background:
at once the first time, else when it is a minute old or force; never more
often than every 10 seconds), browser_profile_get gives the last answer. */
enum
{
	BROWSER_PROFILE_NOT_ASKED,
	BROWSER_PROFILE_LOADING,
	BROWSER_PROFILE_LOADED,
	/* the site did not answer (a profile it gave before is kept) */
	BROWSER_PROFILE_FAILED,
	/* no HTTPS game list, or no player key */
	BROWSER_PROFILE_NO_KEY,
	/* no game list (network.browser_url empty) */
	BROWSER_PROFILE_NO_LIST,
};

/* what the last action (unlinking this copy, a privacy setting) came to */
enum
{
	BROWSER_PROFILE_ACTION_NONE,
	BROWSER_PROFILE_ACTION_WORKING,
	BROWSER_PROFILE_ACTION_DONE,
	BROWSER_PROFILE_ACTION_FAILED,
	/* this is the copy that made the profile: it can't be unlinked */
	BROWSER_PROFILE_ACTION_FIRST_COPY,
};

struct browser_profile_status
{
	int state;
	/* an answer is kept (profile), how old it is, and a count of the
	answers kept (a new one: it changed) */
	int has_profile;
	unsigned long age_seconds;
	int serial;
	/* asked for, not answered yet */
	int loading;
	int action;
	char message[128];
};

struct delta_profile;
struct delta_profile_card;
void browser_profile_load(int force);
void browser_profile_get(struct browser_profile_status *status, struct delta_profile *profile);
/* this copy unlinked from its profile (the site's /v1/game/unlink); a
privacy setting of the site's changed ("hide_stats", "hide_heatmaps",
"show_presence", "show_clan_tag"); the action's result shown (no longer kept) */
void browser_profile_unlink(void);
void browser_profile_privacy(const char *field, int value);
void browser_profile_action_seen(void);
/* a clan's invite answered ("accept" or "decline", clan its ID), or the
clan left ("leave"): the site's /v1/game/clan, its result the action's */
void browser_profile_clan(const char *action, int clan);

/* another player's public profile card, by the player ID they shared in a
game (32 hex digits): 1 with it in card, else 0 (asked of the site in the
background, or they have none to show). Kept for the run */
#define BROWSER_PLAYER_CARDS 64
int browser_player_card(const char *player_id, struct delta_profile_card *card);

/* a page of the site (path: "/profile"): its address as a player types it
in page, and as a QR code in modules (BROWSER_CONNECT_QR_SIZE squared at
most, nonzero for a dark one); the QR code's size, 0 for none */
int browser_page_qr(const char *path, char *page, int page_size, unsigned char *modules);

#endif
