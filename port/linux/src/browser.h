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
/* (as many as the brokers' listings the Server Browser shows, menu_functions.c's
LOBBY_BROWSER_GAMES: the list sends every game it has, co-op too, and those
past the cap were dropped, the emptiest first, before the games heard only
on the brokers had any room; a console's memory takes fewer:
halo_port_limits.h, force-included) */
#ifdef HALO_PORT_CONSOLE_LIMITS
#define BROWSER_MAXIMUM_GAMES 128
#else
#define BROWSER_MAXIMUM_GAMES 256
#endif
/* a host's roster: the players it announces (as many as a game takes), and
those a listed game keeps (as many as the Online Games screen shows) */
#define BROWSER_HOSTED_ROSTER 128
#define BROWSER_LISTED_ROSTER 16
/* a listed game's machines by platform: Delta's registry
(NUMBER_OF_DELTA_PLATFORMS), then those without Delta */
#define BROWSER_MACHINE_KINDS 23

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
	/* the host takes no new players now (sv_lock: the list's field 25) */
	unsigned char joins_locked;
	/* a dedicated server's signed badge, as the list says it (fields 26 and
	27: server_badge.h's tier, 0 for none, and its label, a handle or a
	group, empty for none). Only from a build with badge keys, and dropped
	for a host that failed its proof on joining (docs/server-badges.md) */
	unsigned char badge;
	char badge_label[32];
	/* the server key the badge's certificate names (field 28), to match
	the host's proof on joining; has_badge_key 0 from a list without it */
	unsigned char has_badge_key;
	unsigned char badge_key[32];
};

/* a listed game's badge as players read it ("Official server"; NULL for none) */
const char *browser_badge_name(int badge);
/* the joined host's badge proof (delta_peer_game.c; proven NULL: none):
a host that proved none shows no badge from then on, and one that proved
a badge shows the list's only while it is the same (server_badge.h's
server_badge_joined_allows), until it joins again */
struct server_badge;
void browser_server_badge_proven(const char *invite, const struct server_badge *proven);

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
each; a console's report, a client's, has the few who left as scoreboard
lines of about 80 bytes: port/console/reports) */
#if defined(HALO_CONSOLE_GAME_REPORTS) && !defined(HALO_GAME_BROWSER)
#define BROWSER_REPORT_GAME_EXTRA_SIZE 2048
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

/* the hosted game's Steam Workshop item (an MCC map's: its id and title, ""
for none), for the announcement's workshop_id and workshop_title, and the
map's own name for players without it ("Blood Gulch" for a file named 20,
"" for none: the announcement's map_title, marked MCC); called before
browser_host_update, which takes it with the game */
#define BROWSER_WORKSHOP_ID_LENGTH 24
#define BROWSER_WORKSHOP_TITLE_LENGTH 40
#define BROWSER_MAP_TITLE_LENGTH 48
void browser_host_workshop(const char *id, const char *title, const char *map_title);

/* the hosted game's game type (its variant's name, NULL for a variant
without one; a co-op game's difficulty, -1 for none), for the
announcement's gametype as network_rules_gametype_name names it; called
before browser_host_update, which takes it with the game */
#define BROWSER_GAMETYPE_LENGTH 25
void browser_host_gametype(const char *name, short engine, int teams, int difficulty);

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
/* whether a listed game's host says its console and cheats may change it
while others play (network.host_cheats: Delta List's capability bit 18) */
int browser_game_host_cheats(const struct browser_game *game);
/* the same for a bare network version (a listing over the brokers has no
game list entry): -1 older, 1 newer, 0 joinable. Every build */
int browser_version_state(int version);
/* whether a game's map (a scenario's name or path) is a campaign level: a
co-op game */
/* (browser.c) an MCC map's kind, from the game's MCC catalog (NULL: none) */
extern int (*browser_mcc_campaign)(const char *map);
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
/* its name for a hint ("ONLY CO-OP"): ALL, MULTIPLAYER, CO-OP */
const char *browser_list_filter_name(void);
/* its words on the button that changes it: what it does with co-op games,
"CO-OP: SHOWN", "CO-OP: HIDDEN" or "CO-OP: ONLY" (never "ALL": Online Games'
views are ALL, CHUPATHINGY + PUBLIC and the others, which it doesn't change) */
const char *browser_list_filter_label(void);
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

/* whether the game this machine hosts is on the game list, and if not why:
BROWSER_HOST_*. status: the list's HTTP answer when it refused (0: it did
not answer), for BROWSER_HOST_FAILED */
enum
{
	BROWSER_HOST_LISTED,
	/* not hosting a game on the internet yet (or internet play is off) */
	BROWSER_HOST_NOT_HOSTING,
	/* hosted, but private or with a password: never listed */
	BROWSER_HOST_PRIVATE,
	/* network.list_hosted_games is off */
	BROWSER_HOST_OFF,
	/* network.browser_url is empty */
	BROWSER_HOST_NO_LIST,
	/* listable, and not offered to the list yet */
	BROWSER_HOST_WAITING,
	/* the list did not take it (offered again after a while) */
	BROWSER_HOST_FAILED,
};
int browser_host_listing(int *status);

/* a listed game with a password (locked: one heard on the brokers, whose
invite browser_get_games gives as its key hash alone) opened: its joinable
invite in code (BROWSER_INVITE_LENGTH + 1 at least) when password is its
own. 1 opened, 0 the wrong password, -1 its listing is no longer heard */
int browser_unlock_game(const char *invite, const char *password, char *code, int size);

/* whether a listed game's host is an internet play peer of this machine
(joining it, or joined): its address in the game's network then */
int browser_game_peer(const char *invite, unsigned long *address);

/* joins a listed game: its invite, as an invite link would (p2p.c). open:
the listing is a public game's without a password (not one a password
opened), whose invite a player in it shares on Discord too */
int browser_join(const char *invite, int open);

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

/* presence (study 0.1; social.h): the game says, about once a second,
whether this copy plays a Delta game now (a multiplayer game over the
network whose host speaks Delta); the browser thread tells the site which
game (its invite) about once a minute while it does and
network.share_presence is on. Not on the consoles (HALO_CONSOLE_NO_GUIDE) */
void browser_presence_playing(int playing);

/* the Guide's friends (social.h): the browser thread polls the site's
state (POST /v1/social/state) while the game says where the player is,
about every 20 seconds in the menus or with the Guide open, every minute in
a game, only for a copy linked to a Delta profile. Not on the consoles
(HALO_CONSOLE_NO_GUIDE) */
struct social_state;
enum
{
	/* not asked yet, or this copy is not linked */
	BROWSER_SOCIAL_NONE = 0,
	BROWSER_SOCIAL_LOADING,
	BROWSER_SOCIAL_LOADED,
	/* the site did not answer (a state it gave before is kept) */
	BROWSER_SOCIAL_FAILED,
	/* the site says this copy is linked to no profile */
	BROWSER_SOCIAL_UNLINKED,
};
/* the game says, about once a second, whether the player is in a game and
whether the Guide is open (opening it asks again soon); quiet longer, and
no polling */
void browser_social_where(int in_game, int guide_open);
/* the state into state when its serial is not *serial (then *serial is
its serial); returns BROWSER_SOCIAL_* */
int browser_social_get(struct social_state *state, int *serial);

/* an action on a friend (by handle), one at a time */
enum
{
	BROWSER_SOCIAL_ACCEPT,
	BROWSER_SOCIAL_DECLINE,
	BROWSER_SOCIAL_REMOVE,
	BROWSER_SOCIAL_BLOCK,
	/* a friend request (ADD FRIEND: RECENT, PLAYERS HERE) */
	BROWSER_SOCIAL_REQUEST,
	/* /v1/social/join: the halo://join link, taken by
	browser_social_take_join */
	BROWSER_SOCIAL_JOIN,
	/* /v1/social/invite, to this copy's game */
	BROWSER_SOCIAL_INVITE,
	/* a request this player sent taken back, a player unblocked
	(/v1/game/friends, as ACCEPT to REQUEST) */
	BROWSER_SOCIAL_CANCEL,
	BROWSER_SOCIAL_UNBLOCK,
	NUMBER_OF_BROWSER_SOCIAL_ACTIONS
};
enum
{
	BROWSER_SOCIAL_IDLE = 0,
	BROWSER_SOCIAL_BUSY,
	BROWSER_SOCIAL_DONE,
	BROWSER_SOCIAL_REFUSED,
};
struct browser_social_result
{
	/* counts the actions asked for */
	int serial;
	int state;
	int action;
	char handle[24];
	/* a short line for a refusal ("Their game is full.") */
	char message[96];
};
/* 0 if one is still out */
int browser_social_act(int action, const char *handle);

/* a report's clip (docs/delta.md, Reports): the file at path sent to the
site with the report's token (16 bytes), on the browser's thread: 1 if
taken (one at a time). Its state: BROWSER_CLIP_* */
enum
{
	BROWSER_CLIP_NONE,
	BROWSER_CLIP_SENDING,
	BROWSER_CLIP_SENT,
	BROWSER_CLIP_TOO_BIG,
	BROWSER_CLIP_TOO_MANY,
	BROWSER_CLIP_FAILED,
};
int browser_clip_upload(const char *path, const unsigned char *token);
int browser_clip_state(void);
/* the clips in <data root>/clips (capture's: *.mp4 no bigger than the site
takes), newest first, at most most (16): their paths and file names; the
count */
int browser_clips_list(char paths[][512], char names[][48], int most);
void browser_social_result(struct browser_social_result *result);
/* a join the site allowed: its halo://join link, once (1) */
int browser_social_take_join(char *link, int size);

/* the Guide's party (social.h): for a copy linked to a profile, the
party in the friends' state; for one with none, POST /v1/party alone,
polled while it is in one (it joins by a code). Not on the consoles
(HALO_CONSOLE_NO_GUIDE) */
struct social_party;
/* the party as the site last said (has 0: none) into party, when its
serial is not *serial (then *serial is its serial) */
void browser_party_get(struct social_party *party, int *serial);
/* an action on the party (social_party_fields's: "create", "invite" a
handle, "accept" a party ID, "join" a code, ...), with this copy's game
name for a copy with no profile; one at a time (0 if one is still out).
Its result as browser_social_result's (its action is 0) */
int browser_party_act(const char *action, const char *value, const char *name);
void browser_party_result(struct browser_social_result *result);
/* the leader's game (its invite's code) told to the party, apart from the
actions (the latest wins) */
void browser_party_go(const char *invite);
/* a halo://party/ link (opened, or on the command line): kept for the
Guide, which asks the player before joining it as a code typed would be. 1
if the text was one */
int browser_party_link(const char *text);
/* the party link waiting (its code), taken: 1, or 0 for none */
int browser_party_take_link(char *code, int size);

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
/* a friends' privacy rule changed (friend_requests, who_can_join,
clan_presence: the site's values) */
void browser_profile_friends_privacy(const char *field, const char *value);
void browser_profile_action_seen(void);
/* a clan's invite answered ("accept" or "decline", clan its ID), or the
clan left ("leave"): the site's /v1/game/clan, its result the action's */
void browser_profile_clan(const char *action, int clan);
/* a member of the profile's clan kicked, or a player invited to it
("kick" or "invite", by handle: a leader's or officer's, DELTA's CLAN);
the site's /v1/game/clan, its result the action's. 0 for a handle the site
could not have given. Not on the consoles (no DELTA there yet) */
int browser_profile_clan_member(const char *action, const char *handle);

/* the devices (docs/delta.md, Devices): this device signed out (its
certificate dropped, its key kept; nothing goes to the site as the account
until it signs in again by code), and another device of the account revoked
(by its ID, 32 hex digits); each the action's result */
void browser_profile_sign_out(void);
void browser_profile_revoke(const char *device_id);

/* Link a device: another device's Quick Connect code (as typed, ABCD-EFGH)
approved as this device's account, then that device asks its player */
enum
{
	BROWSER_APPROVE_NONE,
	/* sent, not answered yet */
	BROWSER_APPROVE_SENDING,
	/* a device's code: what it says it is (name, platform, message: the
	site's warning), for the player to approve or deny
	(browser_approve_answer) */
	BROWSER_APPROVE_SHOWN,
	/* the answer sent */
	BROWSER_APPROVE_ANSWERING,
	/* taken: the other device asks its player (handle, name, platform) */
	BROWSER_APPROVE_WAITING,
	BROWSER_APPROVE_DONE,
	BROWSER_APPROVE_DECLINED,
	BROWSER_APPROVE_EXPIRED,
	/* denied here: the code is dead */
	BROWSER_APPROVE_DENIED,
	/* refused (message) */
	BROWSER_APPROVE_FAILED,
};
struct browser_approve
{
	int state;
	char handle[48];
	char name[40];
	char platform[24];
	char message[96];
};
/* 0 if the code isn't one */
int browser_approve_start(const char *code);
/* the player's answer to a device shown (BROWSER_APPROVE_SHOWN) */
void browser_approve_answer(int approve);
void browser_approve_stop(void);
void browser_approve_get(struct browser_approve *approve);

/* another player's public profile card, by the player ID they shared in a
game (32 hex digits): 1 with it in card, else 0 (asked of the site in the
background, or they have none to show). Kept for the run */
#define BROWSER_PLAYER_CARDS 64
int browser_player_card(const char *player_id, struct delta_profile_card *card);

/* verified names in games (Delta Peer's identity capability; docs/delta.md,
"Verified names"). This copy's device certificate and its issuer's, to
prove a local player whose Halo profile is signed in to account (32 bytes
of the certificate's account; NULL: the copy's own): 1 with them (the
certificate's size, and what it says when device is not NULL), else 0
(none, not in date, not for that account, or without play) */
struct identity_device;
int browser_identity_card(const unsigned char *account, unsigned char *certificate, int *certificate_size,
	unsigned char *issuer, struct identity_device *device);
/* the device key's signature of message (64 bytes), and its public key
(may be NULL): 1, else 0 (no device key) */
int browser_identity_sign(const unsigned char *message, int size, unsigned char *signature, unsigned char *public_key);
/* a moderator's sign-in or action signed with the device key, for a device
whose certificate may moderate (the role is its account's on the server):
1, else 0 */
int browser_identity_moderator_sign(const unsigned char *message, int size, unsigned char *public_key,
	unsigned char *signature);
/* whether a device ID (16 bytes) is on the revocation list this copy last
fetched and checked (GET /v1/identity/revoked, every ten minutes, kept in
delta_revoked.bin); 0 when it has none. The dedicated server's too */
int browser_identity_revoked(const unsigned char *device_id);
/* a count of the revocation lists taken so far: when it changes, every
verified player kept is checked against the new one */
int browser_identity_revoked_changes(void);

/* a page of the site (path: "/profile"): its address as a player types it
in page, and as a QR code in modules (BROWSER_CONNECT_QR_SIZE squared at
most, nonzero for a dark one); the QR code's size, 0 for none */
int browser_page_qr(const char *path, char *page, int page_size, unsigned char *modules);
/* any short text (PARTY's halo://party/ link) as a QR code in modules, as
browser_page_qr's: its size, 0 for none */
int browser_text_qr(const char *text, unsigned char *modules);

#endif
