/*
SOCIAL.H

The Guide's data (port/linux/game/guide.c draws it): the rows its sections
list (a player's tag, one status line, their platform and what can be done
with them), kept in a cache the Guide reads each frame, the schedule of the
site's social polling (FRIENDS, PARTY and RECENT, coming with the site's
friends), and when this copy tells the site it is playing (presence: POST
/v1/presence, browser.c's thread sends it).

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

/* ---------- polling the site (FRIENDS, PARTY, RECENT): the schedule only,
the requests come with the site's friends (study phase 1.1) */

enum
{
	/* how often, in the menus and in a game */
	SOCIAL_POLL_MENUS = 20000,
	SOCIAL_POLL_GAME = 60000,
};

struct social_poll
{
	int started;
	unsigned long last;
};

/* whether it is time to poll: signed in to Delta, and the interval of
where the player is since the last poll (the first at once) */
int social_poll_due(struct social_poll *poll, unsigned long now, int in_game, int signed_in);

/* ---------- presence: this copy tells the site it is playing
(network.share_presence), about once a minute while it is in a Delta game.
The invite is never in it: the site gets an empty one. Never for another
player, never for a game without Delta (an OpenCE host's). */

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
};

struct social_presence
{
	/* sending: the inputs allowed it at the last look */
	int active;
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
answer comes to social_presence_answer). Inputs that no longer allow it
start the schedule again, so the next game is told at once */
int social_presence_due(struct social_presence *presence, const struct social_presence_inputs *inputs,
	unsigned long now);
/* the site's answer: its HTTP status (0: none) and its body ("ok", "off");
the next one waits by it. Returns the word (SOCIAL_PRESENCE_*) */
int social_presence_answer(struct social_presence *presence, int status, const char *body, unsigned long now);
/* the request's members besides the key, as browser.c's game_request takes
them: the empty invite */
int social_presence_fields(char *fields, int size);

#endif
