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
	SOCIAL_SECTION_CLAN,
	SOCIAL_SECTION_PLAYERS_HERE,
	SOCIAL_SECTION_RECENT,
	/* the player's medals and totals (port/linux/game/guide_record.c) */
	SOCIAL_SECTION_SERVICE_RECORD,
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
	/* the doorbell (p2p_doorbell) rang: polled at once, at least this long
	after the last poll, while fewer than SOCIAL_POLL_RING_ROOM polls went out
	for a ring in the last SOCIAL_POLL_RING_WINDOW and fewer than
	SOCIAL_POLL_ROOM went out at all (the menus' 3 a minute and a ring's 2
	stay under the site's 6, with one left for a join or an invite) */
	SOCIAL_POLL_RING_GAP = 2000,
	SOCIAL_POLL_RING_ROOM = 2,
	SOCIAL_POLL_ROOM = 5,
	SOCIAL_POLL_RING_WINDOW = 61000,
	SOCIAL_POLL_TIMES = 8,
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
	/* the doorbell rang since the last poll (social_poll_ring) */
	int rung;
	/* when the latest polls went out (the ring's room), newest at
	times[(next + SOCIAL_POLL_TIMES - 1) % SOCIAL_POLL_TIMES], and which of
	them only went out early for a ring */
	unsigned long times[SOCIAL_POLL_TIMES];
	int rang[SOCIAL_POLL_TIMES];
	int time_count;
	int next_time;
	/* in a party: the menus' interval is SOCIAL_POLL_PARTY */
	int party;
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
/* the doorbell rang (something changed for this profile): polled at once
when there is room (SOCIAL_POLL_RING_*), else as soon as there is, unless
the site asked for a wait */
void social_poll_ring(struct social_poll *poll);

/* ---------- the site's friends: what the state answer holds (the parts
the Guide shows). Only Delta profiles, by handle: nothing of an OpenCE
player, no address, no player ID */

enum
{
	/* a handle: 3 to 20 of a-z, 0-9, _ and - */
	SOCIAL_HANDLE_SIZE = 24,
	SOCIAL_GAME_TEXT_SIZE = 64,
	SOCIAL_CURSOR_SIZE = 40,
	/* this copy's doorbell (p2p_doorbell): 32 of 0-9 and a-f */
	SOCIAL_DOORBELL_SIZE = 33,
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

/* ---------- parties (the site's /v1/party, and the party in the state):
up to SOCIAL_PARTY_LARGEST players who follow their leader into games */

enum
{
	SOCIAL_PARTY_LARGEST = 8,
	SOCIAL_PARTY_ID_SIZE = 20,
	/* a code's 6 letters and digits (no 0, O, 1, I or L) */
	SOCIAL_PARTY_CODE_LENGTH = 6,
	SOCIAL_PARTY_CODE_SIZE = 8,
	SOCIAL_PARTY_MEMBER_ID_SIZE = 12,
	/* 16 hexadecimal digits: the 8 bytes the members tell their game's host
	(Delta Peer's party_teams) */
	SOCIAL_PARTY_TAG_SIZE = 20,
	SOCIAL_PARTY_PLATFORM_SIZE = 16,
	SOCIAL_PARTY_RULE_SIZE = 12,
	SOCIAL_PARTY_LINK_SIZE = 192,
	SOCIAL_MAXIMUM_PARTY_INVITES = 8,
	/* the leader's game offered this long before the member's copy follows */
	SOCIAL_FOLLOW_TIME = 5000,
	/* polled this often in the menus while in a party (the leader's go comes
	sooner) */
	SOCIAL_POLL_PARTY = 15000,
};

struct social_party_member
{
	char id[SOCIAL_PARTY_MEMBER_ID_SIZE];
	/* their profile's name, or the game name of a copy with none */
	char name[SOCIAL_TAG_SIZE];
	/* "" for a copy with no profile */
	char handle[SOCIAL_HANDLE_SIZE];
	/* Delta's registry name ("pc_macos"; "unknown") */
	char platform[SOCIAL_PARTY_PLATFORM_SIZE];
	int leader;
	int you;
};

/* the leader's game, for a member (never the leader's own) */
struct social_party_go
{
	int has;
	long long serial;
	long long time;
	struct social_game game;
	/* its halo://join link, or why the member can't follow ("full") and the
	site's words for it */
	char join[SOCIAL_PARTY_LINK_SIZE];
	char reason[16];
	char error[96];
};

struct social_party
{
	int has;
	char id[SOCIAL_PARTY_ID_SIZE];
	char code[SOCIAL_PARTY_CODE_SIZE];
	/* this player leads it */
	int leader;
	/* who the code lets in: "anyone", "friends" or "invite" */
	char who_can_join[SOCIAL_PARTY_RULE_SIZE];
	int largest;
	int member_count;
	struct social_party_member members[SOCIAL_PARTY_LARGEST];
	char tag[SOCIAL_PARTY_TAG_SIZE];
	struct social_party_go go;
};

/* a friend's invite to their party */
struct social_party_invite
{
	char party[SOCIAL_PARTY_ID_SIZE];
	char from[SOCIAL_HANDLE_SIZE];
	char display[SOCIAL_TAG_SIZE];
	int size;
	long long time;
	long long expires;
};

/* ---------- RECENT: the Delta players this player shared a game with
(the site's, from its confirmed report lines, and those verified in games
this run: the identity capability's roster). Only Delta profiles, by handle:
never an OpenCE player, a name alone or an address */

enum
{
	/* the site sends 24 at most; this run's own, as many */
	SOCIAL_MAXIMUM_RECENT = 24,
};

struct social_recent
{
	char handle[SOCIAL_HANDLE_SIZE];
	char display[SOCIAL_TAG_SIZE];
	/* the last game shared (Unix time) and its map ("bloodgulch"; "" not
	known) */
	long long time;
	char map[SOCIAL_GAME_TEXT_SIZE];
	/* already a friend; this player's request to them waiting */
	int is_friend;
	int requested;
	/* verified in a game this run (not from the site) */
	int here;
};

/* this run's verified players, newest first */
struct social_recent_seen
{
	int count;
	struct social_recent players[SOCIAL_MAXIMUM_RECENT];
};

struct social_state
{
	char cursor[SOCIAL_CURSOR_SIZE];
	/* this copy's doorbell (empty: the site has none for it) */
	char doorbell[SOCIAL_DOORBELL_SIZE];
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
	/* the party this player is in (has 0: none), and invites to others */
	struct social_party party;
	int party_invite_count;
	struct social_party_invite party_invites[SOCIAL_MAXIMUM_PARTY_INVITES];
	/* RECENT as the site says, newest first */
	int recent_count;
	struct social_recent recent[SOCIAL_MAXIMUM_RECENT];
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

/* a player verified in this run's game noted (a valid handle; display may
be ""), at the front of seen: once each, the oldest let go when full */
void social_recent_note(struct social_recent_seen *seen, const char *handle, const char *display, const char *map,
	long long time);
/* a handle let go of seen (blocked) */
void social_recent_forget(struct social_recent_seen *seen, const char *handle);
/* RECENT's rows into out (most of them): this run's first, then the
site's, each handle once, never this player's own (own_handle); "friend"
and "requested" from the state where it knows them. Returns the count */
int social_recent_merge(const struct social_state *state, const struct social_recent_seen *seen,
	const char *own_handle, struct social_recent *out, int most);
/* a recent player's line ("Blood Gulch · 2 hours ago", "In this game"),
now in Unix time */
void social_recent_status(const struct social_recent *player, long long now, char *text, int size);

/* ---------- DELTA's CLAN: a clan's ranks as the site has them ("leader",
"officer", "member") */

/* a rank's order: 3 the leader, 2 an officer, 1 a member, 0 anything else */
int social_clan_rank(const char *rank);
/* whether a rank may invite (the leader and officers), and kick a member of
another rank (the leader anyone but themself, an officer members) */
int social_clan_can_invite(const char *rank);
int social_clan_can_kick(const char *rank, const char *their_rank);
/* a member's line: their rank, then their game's line, "In a game",
"Online" or "Offline" ("Officer \xC2\xB7 Online"); "You \xC2\xB7 Leader"
for the player's own */
void social_clan_status(const char *rank, int you, int online, int playing, const struct social_game *game, char *text,
	int size);

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
when they do not fit, or for a handle the site could not have given. The
state asks for the profile's clan too ("clan": DELTA's CLAN, its roster
with presence; a site from before it leaves it out) */
int social_state_fields(char *fields, int size, const char *cursor, int limit);
/* action: "accept", "decline", "remove", "block" or "request"
(/v1/game/friends) */
int social_action_fields(char *fields, int size, const char *action, const char *handle);
int social_join_fields(char *fields, int size, const char *handle);
/* invite: the game's invite (social_presence_invite_valid), or "" for the
one presence told the site */
int social_invite_fields(char *fields, int size, const char *handle, const char *invite);

/* the line shown when the site says no: a join's (its "reason"), an
invite's (its "reason", else by status) */
const char *social_join_refusal(const char *reason);
const char *social_invite_refusal(const char *reason, int status);

/* ---------- parties' helpers */

/* the site's party page, before the code: the link COPY PARTY LINK copies
and the party's QR code holds (the page opens halo://party/CODE in the game) */
#define SOCIAL_PARTY_PAGE "https://stats.chupathingy.org/party/"

/* a code as typed or linked ("abc-def", "halo://party/ABCDEF", the party
page's https://stats.chupathingy.org/party/ABCDEF) in its 6 capitals into
code (SOCIAL_PARTY_CODE_SIZE): 1, or 0 if it can't be one */
int social_party_code_clean(const char *text, char *code);
/* a code as shown ("ABC-DEF"); its link to share, the party page
(SOCIAL_PARTY_PAGE "ABCDEF"); and the game's own link ("halo://party/ABCDEF"),
which the page opens */
void social_party_code_shown(const char *code, char *text, int size);
int social_party_link(const char *code, char *link, int size);
int social_party_app_link(const char *code, char *link, int size);
/* whether a 16-digit tag (Delta Peer's party_teams) reads as 8 bytes */
int social_party_tag_bytes(const char *tag, unsigned char *bytes);
/* the party's leader (NULL: none) */
const struct social_party_member *social_party_leader(const struct social_party *party);

/* a list's chosen item, kept on the same one when the list changes under it
(PARTY's: a member joins, VOICE shows): its kind and number, and for an item
that moves around (a member, an invite) its ID */
enum { SOCIAL_LIST_KEY_ID_SIZE = 20 };
struct social_list_key
{
	int kind;
	int index;
	char id[SOCIAL_LIST_KEY_ID_SIZE];
};
/* the row to choose now: kept and kept_row, the item chosen and its row as
last remembered; keys and count, the list now; row, the row chosen now. With
the row as remembered, the item there or, if the list moved it, wherever it
went; with the row moved since (by a press, or the code), that row. *changed
is 1 when the item at the row returned isn't the one remembered (anything
armed on it no longer applies), else 0 */
int social_list_keep(const struct social_list_key *kept, int kept_row, const struct social_list_key *keys, int count,
	int row, int *changed);
/* whether the item at index now (keys, count) is the one drawn there
(drawn, drawn_count: the list as last drawn), so a tap on that row, aimed at
what the player saw, still lands on it (0: the list changed under the tap) */
int social_list_same_item(const struct social_list_key *drawn, int drawn_count, const struct social_list_key *keys,
	int count, int index);
/* "WHO CAN JOIN"'s value as shown (ANYONE, FRIENDS, INVITE ONLY), and the
next one (A or right; back with a direction below 0) */
const char *social_party_rule_shown(const char *rule);
const char *social_party_rule_next(const char *rule, int direction);

/* /v1/party's request members besides the key: an action ("create",
"invite" a handle, "accept" or "decline" a party ID, "join" a code,
"leave", "kick" or "lead" a member ID, "rule" (who can join), "go" an
invite), with this copy's name and platform; -1 when something is not
what the site could take, or does not fit */
int social_party_fields(char *fields, int size, const char *action, const char *value, const char *name,
	const char *platform);
/* the line a refusal shows (/v1/party's "reason", else by status) */
const char *social_party_refusal(const char *reason, int status);

/* what a member's copy does with the leader's game (social_follow_decide) */
enum
{
	/* nothing: no go, the leader's own, one seen already */
	SOCIAL_FOLLOW_NONE = 0,
	/* offer it: "Following Sarge to Blood Gulch", B to stay */
	SOCIAL_FOLLOW_OFFER,
	/* can't: a line why (social_follow_reason) */
	SOCIAL_FOLLOW_CANT,
	/* in that game already: seen, nothing shown */
	SOCIAL_FOLLOW_THERE,
};

struct social_follow_inputs
{
	/* the go's serial last acted on (0: none) */
	long long seen_serial;
	/* this copy hosts a game others are in (leaving would end it) */
	int hosting_others;
	/* the game this copy is in now (its invite's code; "" for none) */
	const char *current_invite;
	/* delta.follow_leader (social_follow_mode) */
	int mode;
};

/* delta.follow_leader: when a member's copy follows the leader. ASK (the
default) offers it with a countdown that joins when it ends in the menus,
and stays when it ends in a game (A joins); ALWAYS joins when it ends
either way; NEVER offers nothing */
enum
{
	SOCIAL_FOLLOW_ASK = 0,
	SOCIAL_FOLLOW_ALWAYS,
	SOCIAL_FOLLOW_NEVER,
};

/* the setting's text ("ask", "always", "never"; anything else: ASK) */
int social_follow_mode(const char *text);
/* the next setting's text after this one's (a row cycling them) */
const char *social_follow_mode_next(const char *text);
/* whether the offer's countdown ending joins the leader's game (else the
member stays): never while the member is in a game unless ALWAYS */
int social_follow_timeout_joins(int mode, int in_game);

int social_follow_decide(const struct social_party *party, const struct social_follow_inputs *inputs);
/* the one line shown when the member can't follow */
void social_follow_reason(const struct social_party *party, const struct social_follow_inputs *inputs, char *text,
	int size);
/* the line of the offer ("Following Sarge to Blood Gulch"), the leader's
name and the game's map as the status lines show it */
void social_follow_line(const struct social_party *party, char *text, int size);

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
