/*
SOCIAL.H

The Guide's data (port/linux/game/guide.c draws it): the rows its sections
list (a player's tag, one status line, their platform and what can be done
with them), kept in a cache the Guide reads each frame, the schedule of the
site's friends polling and what its answer holds (browser.c's thread asks,
delta_profile.c reads it), and when this copy tells the site it is playing
(presence: POST /v1/presence, browser.c's thread sends it).

No SDL, no drawing and no game here, so its tests build anywhere
(port/linux/tests/social_test.c, tools/test_social.py). Times are a
millisecond clock's (p2p_now, system_milliseconds), compared by difference
so that they may wrap.
*/

#ifndef SOCIAL_H
#define SOCIAL_H

struct discord_activity_game;

/* the Guide's sections, in the order the panel has them */
enum
{
	SOCIAL_SECTION_FRIENDS = 0,
	SOCIAL_SECTION_PARTY,
	SOCIAL_SECTION_PLAYERS_HERE,
	SOCIAL_SECTION_RECENT,
	SOCIAL_SECTION_SETTINGS,
	NUMBER_OF_SOCIAL_SECTIONS
};

/* a section's name on the panel ("PLAYERS HERE"); "" past the last */
const char *social_section_name(int section);

/* a row's player: their dot on the panel */
enum
{
	SOCIAL_STATE_OFFLINE = 0,
	SOCIAL_STATE_ONLINE,
	SOCIAL_STATE_IN_GAME,
	SOCIAL_STATE_AWAY,
};

/* what can be done with a row's player (bits of social_row.actions) */
enum
{
	SOCIAL_ACTION_MUTE = 1 << 0,
	SOCIAL_ACTION_JOIN = 1 << 1,
	SOCIAL_ACTION_INVITE = 1 << 2,
	SOCIAL_ACTION_ADD_FRIEND = 1 << 3,
	SOCIAL_ACTION_VIEW_PROFILE = 1 << 4,
};

enum
{
	/* a Delta Tag of 24 characters, any of them up to 4 bytes of UTF-8 */
	SOCIAL_TAG_SIZE = 100,
	SOCIAL_STATUS_SIZE = 96,
	/* the rows a list keeps (the largest game's players) */
	SOCIAL_MAXIMUM_ROWS = 32,
};

struct social_row
{
	/* the name shown (UTF-8): a Delta Tag, else the game name */
	char tag[SOCIAL_TAG_SIZE];
	/* one line of what they're doing ("Slayer · Blood Gulch · 11/16"); ""
	shows none */
	char status[SOCIAL_STATUS_SIZE];
	/* delta.h's enum delta_platform (0: not known, no logo) */
	int platform;
	int state;
	unsigned int actions;
	/* muted on this copy (the chat log's mute) */
	int muted;
	/* shares a Delta profile in this game (verified by the site) */
	int verified;
	/* the list's own key for the row: Players here's machine index */
	int key;
};

struct social_list
{
	int count;
	/* raised each time the rows change (social_list_commit) */
	unsigned int serial;
	struct social_row rows[SOCIAL_MAXIMUM_ROWS];
};

void social_list_clear(struct social_list *list);
/* a row added at the end: its index, or -1 when the list is full. The
row's texts are cut to fit, never past a character */
int social_list_add(struct social_list *list, const struct social_row *row);
/* the row with a key, or -1 */
int social_list_find(const struct social_list *list, int key);
/* the rows sorted: in a game first, then online, then the rest, each by
tag (ASCII letters' case ignored) */
void social_list_sort(struct social_list *list);
/* fresh's rows into the cache, its serial raised if they differ: 1 if they
did */
int social_list_commit(struct social_list *cache, const struct social_list *fresh);

/* text copied into a field of size bytes, cut at a whole UTF-8 character */
void social_copy_text(char *destination, int size, const char *text);

/* this player's own line on the card: "Online · " and what they're doing
(discord_activity_status_line), or "Online" alone; its length, -1 if it
does not fit */
int social_own_status(const struct discord_activity_game *game, char *text, int size);

/* ---------- polling the site's friends (POST /v1/social/state {key,
since}: browser.c's thread asks, on this schedule) */

enum
{
	/* how often: in the menus or with the Guide open, and in a game */
	SOCIAL_POLL_MENUS = 20000,
	SOCIAL_POLL_GAME = 60000,
	/* after an action or the Guide opening, sooner, but never sooner than
	this after the last (the site takes 6 a minute from a key) */
	SOCIAL_POLL_SOONEST = 10000,
	/* too many (429), and a copy the site says is linked to no profile */
	SOCIAL_POLL_BUSY_WAIT = 5 * 60000,
	SOCIAL_POLL_UNLINKED_WAIT = 10 * 60000,
	/* no answer, or an answer not understood: the wait doubles from the
	interval up to this */
	SOCIAL_POLL_FAILED_WAIT = 5 * 60000,
};

struct social_poll
{
	int started;
	unsigned long last;
	/* the wait the site's last answer asked for (0: the interval) */
	unsigned long wait;
	int failures;
	/* asked for sooner (social_poll_soon) */
	int soon;
};

/* whether it is time to poll: signed in to Delta, and the interval of
where the player is since the last poll (the first at once), or the
longer wait the last answer asked for */
int social_poll_due(struct social_poll *poll, unsigned long now, int in_game, int signed_in);
/* the poll's answer: its HTTP status (0: none) and whether it was read */
void social_poll_answer(struct social_poll *poll, int status, int understood);
/* polled again soon (an action, the Guide opening), unless the site asked
for a wait */
void social_poll_soon(struct social_poll *poll);

/* ---------- the site's friends: what the state answer holds (the parts
the Guide shows). Only Delta profiles, by handle: nothing of an OpenCE
player, no address, no player ID */

enum
{
	/* a handle: 3 to 20 of a-z, 0-9, _ and - */
	SOCIAL_HANDLE_SIZE = 24,
	SOCIAL_GAME_TEXT_SIZE = 64,
	SOCIAL_CURSOR_SIZE = 40,
	SOCIAL_INVITE_ID_SIZE = 40,
	/* the site keeps 200 friends at most, 50 requests waiting */
	SOCIAL_MAXIMUM_FRIENDS = 200,
	SOCIAL_MAXIMUM_REQUESTS = 50,
	SOCIAL_MAXIMUM_INVITES = 20,
};

/* a friend's game, as the game list has it (none when not listed) */
struct social_game
{
	int listed;
	/* the game's name and map, as listed ("bloodgulch") */
	char name[SOCIAL_GAME_TEXT_SIZE];
	char map[SOCIAL_GAME_TEXT_SIZE];
	/* the game type (1 CTF, 2 Slayer, 3 Oddball, 4 King, 5 Race, 6 Bomb),
	a campaign level played together, teams */
	int engine;
	int coop;
	int teams;
	int players;
	int maximum_players;
};

struct social_friend
{
	char handle[SOCIAL_HANDLE_SIZE];
	char display[SOCIAL_TAG_SIZE];
	/* friends since (Unix time) */
	long long since;
	int online;
	int playing;
	/* the site would let this player join now */
	int joinable;
	struct social_game game;
};

/* a friend request to this player */
struct social_request
{
	char handle[SOCIAL_HANDLE_SIZE];
	char display[SOCIAL_TAG_SIZE];
	long long time;
};

/* a friend's invite to their game */
struct social_invite
{
	char id[SOCIAL_INVITE_ID_SIZE];
	char from[SOCIAL_HANDLE_SIZE];
	char display[SOCIAL_TAG_SIZE];
	long long time;
	long long expires;
	struct social_game game;
};

struct social_state
{
	char cursor[SOCIAL_CURSOR_SIZE];
	/* this player: their handle and name, and whether they appear offline */
	char handle[SOCIAL_HANDLE_SIZE];
	char display[SOCIAL_TAG_SIZE];
	int appear_offline;
	/* online first, then in a game, then by handle (the site's order) */
	int friend_count;
	struct social_friend friends[SOCIAL_MAXIMUM_FRIENDS];
	int request_count;
	struct social_request requests[SOCIAL_MAXIMUM_REQUESTS];
	int invite_count;
	struct social_invite invites[SOCIAL_MAXIMUM_INVITES];
};

/* the name a profile shows: its display name, else its handle */
const char *social_name(const char *display, const char *handle);
/* whether text is a handle the site could have given (sent back in a
request as it is) */
int social_handle_valid(const char *handle);
/* a game's line ("Slayer · Blood Gulch · 11/16"): its
length, -1 for none (not listed, or it does not fit) */
int social_game_line(const struct social_game *game, char *text, int size);
/* a friend's status line: their game's line, "In a game" (one not
listed), "Online" or "Offline" */
void social_friend_status(const struct social_friend *buddy, char *text, int size);
/* how many friends are online */
int social_online_count(const struct social_state *state);

/* what changed between two states, for the toasts: the first invite and
the first request that are new (-1: none), and the first friend who came
online (-1: none), as indexes in fresh */
struct social_news
{
	int invite;
	int request;
	int online;
};
void social_news(const struct social_state *old, const struct social_state *fresh, struct social_news *news);

/* the requests' members besides the key (browser.c's game_request): -1
when they do not fit, or for a handle the site could not have given */
int social_state_fields(char *fields, int size, const char *cursor, int limit);
/* action: "accept", "decline", "remove" or "block" (/v1/game/friends) */
int social_action_fields(char *fields, int size, const char *action, const char *handle);
int social_join_fields(char *fields, int size, const char *handle);
/* invite: the game's invite (social_presence_invite_valid), or "" for the
one presence told the site */
int social_invite_fields(char *fields, int size, const char *handle, const char *invite);

/* the line shown when the site says no: a join's (its "reason"), an
invite's (its "reason", else by status) */
const char *social_join_refusal(const char *reason);
const char *social_invite_refusal(const char *reason, int status);

/* ---------- presence: this copy tells the site it is playing
(network.share_presence), about once a minute while it is in a Delta game
that has an invite: the game's own invite (the one the game list and
halo://join/ links carry), so that the site can show which game it is.
The site shows the game only to those the player allows, and the invite
itself only to those allowed to join. Nothing in the menus (the site's
social state covers "online"), never for another player, never for a game
without Delta (an OpenCE host's). */

enum
{
	SOCIAL_PRESENCE_INTERVAL = 60000,
	/* the site said "off" (no profile, "Show when I'm playing" off, or a
	hidden service record), or it does not take the request: asked again
	in half an hour */
	SOCIAL_PRESENCE_OFF_WAIT = 30 * 60000,
	/* too many from this address (429) */
	SOCIAL_PRESENCE_BUSY_WAIT = 10 * 60000,
};

/* what presence decides by */
struct social_presence_inputs
{
	/* network.share_presence */
	int share;
	/* in a multiplayer game over the network */
	int in_game;
	/* the game is a Delta one (delta_peer_game_on_delta: not an OpenCE
	host's) */
	int on_delta;
	/* this copy counts for a Delta profile */
	int linked;
	/* the site's privacy, when known: "Show when I'm playing" off */
	int hidden;
	/* the game's invite is known (social_presence_invite_valid) */
	int has_invite;
};

struct social_presence
{
	/* sending: the inputs allowed it at the last look */
	int active;
	/* the player's own choices at the last look (share, linked, hidden as
	bits; -1 before the first): a change starts the schedule over */
	int choices;
	/* a request is out, not answered yet */
	int waiting;
	unsigned long last;
	/* nothing sent until this long after last */
	unsigned long wait;
	/* how many were sent, and the site's last word (enum below) */
	int sent;
	int answer;
};

/* the site's last word */
enum
{
	SOCIAL_PRESENCE_NONE = 0,
	SOCIAL_PRESENCE_OK,
	SOCIAL_PRESENCE_OFF,
	SOCIAL_PRESENCE_BUSY,
	SOCIAL_PRESENCE_REFUSED,
	SOCIAL_PRESENCE_NO_ANSWER,
};

void social_presence_reset(struct social_presence *presence);
/* whether one is sent now; with 1, the request is counted as out (the
answer comes to social_presence_answer). The wait the site's last answer
asked for holds across gaps in the game (a map loading, the next game), so
"off" and too many are not asked again each round; only a change of the
player's own choices (share, linked, hidden) starts it over */
int social_presence_due(struct social_presence *presence, const struct social_presence_inputs *inputs,
	unsigned long now);
/* the site's answer: its HTTP status (0: none) and its body ("ok", "off");
the next one waits by it. Returns the word (SOCIAL_PRESENCE_*) */
int social_presence_answer(struct social_presence *presence, int status, const char *body, unsigned long now);
/* whether text is an invite the site takes: 64 or 44 lowercase
hexadecimal digits (a code, without halo://join/) */
int social_presence_invite_valid(const char *invite);
/* the request's members besides the key, as browser.c's game_request takes
them: the game's invite; -1 for an invite the site would not take, or when
it does not fit */
int social_presence_fields(char *fields, int size, const char *invite);

#endif
