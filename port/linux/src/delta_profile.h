/*
DELTA_PROFILE.H

Delta Profile in the game (configure.py --game-browser; docs/delta.md,
Delta Profile): the site's answers about this copy's own profile
(POST /v1/game/profile, /v1/game/unlink, /v1/game/privacy) and about the
profiles other players share in a game (POST /v1/game/players), read from
their JSON into plain structures. Only reading here: browser.c asks the
site, port/linux/game/profile_screen.c shows them. Kept in memory only.
*/

#ifndef __DELTA_PROFILE_H
#define __DELTA_PROFILE_H

#include "social.h"

/* (as many as the site sends: list_server.py's GAME_BADGES, GAME_RECENT,
GAME_COPIES and GAME_ROLES) */
#define DELTA_PROFILE_BADGES 8
#define DELTA_PROFILE_RECENT 4
#define DELTA_PROFILE_COPIES 16
#define DELTA_PROFILE_ROLES 32
/* (list_server.py's GAME_DEVICES) */
#define DELTA_PROFILE_DEVICES 16
/* (list_server.py's GAME_CLAN_MEMBERS; invites, at most clans.py's
PLAYER_INVITES) */
#define DELTA_PROFILE_CLAN_MEMBERS 24
#define DELTA_PROFILE_CLAN_INVITES 20
/* a player ID, in hexadecimal */
#define DELTA_PROFILE_ID_LENGTH 32
/* the record's medals kept, the most earned first (of the site's
"medal_counts") */
#define DELTA_PROFILE_TOP_MEDALS 3

struct delta_profile_medal
{
	char key[32];
	int count;
};

struct delta_profile_game
{
	long long time;
	/* the map's file ("bloodgulch"), the game type's name, and the result in
	the site's words ("Won 3 to 1", "2nd of 7") */
	char map[48];
	char type[32];
	char result[48];
	int kills;
	int deaths;
	/* only one player reported it (the stats model's "player" tier) */
	int observed;
};

struct delta_profile_copy
{
	/* the first 8 digits of its ID, and what it last played on (delta.h's
	platform keys: "pc_linux", "steam_deck", "pc_freebsd"; empty when not known) */
	char label[12];
	char platform[24];
	long long linked;
	long long last_seen;
	/* this copy; the copy that made the profile (it can't be unlinked) */
	int this_copy;
	int first;
};

/* a device of the account (docs/delta.md, Devices): signed in to it by
code, or one of its copies' */
struct delta_profile_device
{
	/* its device ID (32 hex digits), the name its owner gave it (else
	empty), and its platform (delta.h's platform keys) */
	char id[DELTA_PROFILE_ID_LENGTH + 1];
	char name[40];
	char platform[24];
	long long created;
	long long last_seen;
	/* enrolled with a player key; a console; signed out; this device */
	int legacy;
	int console;
	int signed_out;
	int this_device;
};

struct delta_profile_role
{
	char name[64];
	/* "owner", "admin" or "moderator"; "owner", "site" or "network" */
	char role[16];
	char source[16];
	/* the server's owner's handle */
	char owner[48];
	/* what the role allows, in the site's words (control.py's actions_words) */
	char actions[160];
	int cached;
	int online;
};

struct delta_profile_clan_member
{
	/* (the handle only in DELTA's roster: /v1/game/clan and the friends
	state's clan; empty in /v1/game/profile's) */
	char handle[24];
	char display[64];
	/* "leader", "officer" or "member" */
	char rank[12];
	/* this profile's own line */
	int you;
	/* their presence, as a friend's (the roster's): online, in a game, and
	that game when it's listed */
	int online;
	int playing;
	struct social_game game;
};

/* a clan (docs/delta.md, Clans): the profile's own, or one that invited it */
struct delta_profile_clan
{
	int id;
	char name[72];
	char tag[16];
	char motto[200];
	/* clan_emblem.h's fields: the foreground and background shapes, the
	primary, secondary and background colors */
	int emblem[5];
	long long created;
	/* the profile's rank in it */
	char rank[12];
	/* all its members; those hiding their record (left out of members);
	those left out to fit */
	int count;
	int hidden;
	int more;
	int member_count;
	struct delta_profile_clan_member members[DELTA_PROFILE_CLAN_MEMBERS];
};

struct delta_profile_invite
{
	int id;
	char name[72];
	char tag[16];
	int count;
};

struct delta_profile
{
	/* this copy counts for a profile; when not, the games its ID has */
	int linked;
	int games;
	int locked;
	/* the profile hides its service record from everyone else */
	int hidden;
	char handle[48];
	char display[64];
	long long created;
	int badge_count;
	char badges[DELTA_PROFILE_BADGES][48];
	int has_record;
	int record_games;
	int record_wins;
	int record_kills;
	int record_deaths;
	int record_assists;
	int record_medals;
	long long record_seconds;
	char record_best_type[32];
	/* (a site with the service record's details: 0 and "" from an older
	one) */
	int record_best_spree;
	char record_favorite_map[48];
	char record_favorite_weapon[48];
	int record_top_medal_count;
	struct delta_profile_medal record_top_medals[DELTA_PROFILE_TOP_MEDALS];
	int recent_count;
	struct delta_profile_game recent[DELTA_PROFILE_RECENT];
	/* (more_*: how many the site left out, its answer being cut to size) */
	int copy_count;
	int more_copies;
	struct delta_profile_copy copies[DELTA_PROFILE_COPIES];
	/* (a site with device identity: has_devices) */
	int has_devices;
	int device_count;
	int more_devices;
	struct delta_profile_device devices[DELTA_PROFILE_DEVICES];
	int role_count;
	int more_roles;
	struct delta_profile_role roles[DELTA_PROFILE_ROLES];
	/* the site's privacy settings */
	int has_privacy;
	int hide_stats;
	int hide_heatmaps;
	int show_presence;
	/* the clan tag shows before the profile's name in games (the site's
	show_clan_tag; known with has_privacy) */
	int show_clan_tag;
	/* its friends' privacy (friends.py): appearing offline, who may send
	requests ("everyone", "friends_of_friends", "nobody"), who may join
	("anyone", "friends", "invite") and who sees its status ("friends",
	"friends_and_clan"); "" while the site doesn't say */
	int appear_offline;
	char friend_requests[24];
	char who_can_join[24];
	char clan_presence[24];
	/* its clan (has_clan), and the clans that invited it */
	int has_clan;
	struct delta_profile_clan clan;
	int invite_count;
	struct delta_profile_invite invites[DELTA_PROFILE_CLAN_INVITES];
	/* a leader's or officer's: the handles their clan has invited (the
	roster's "clan_invited") */
	int clan_invited_count;
	char clan_invited[DELTA_PROFILE_CLAN_MEMBERS][24];
};

/* another player's public card (POST /v1/game/players): their profile's
handle, display name and first badge */
struct delta_profile_card
{
	char handle[48];
	char display[64];
	char badge[48];
	/* the clan tag it shows (empty: none) */
	char clan_tag[16];
	/* the handle of another profile this one's looks like, as the site
	marked it (empty: none) */
	char looks_like[48];
};

/* /v1/game/profile's answer read into profile: 1, else 0 (not JSON, or
not that answer) */
int delta_profile_parse(const char *text, struct delta_profile *profile);

/* /v1/game/privacy's answer ({"privacy": {...}}) read into profile's
privacy: 1, else 0 */
int delta_profile_parse_privacy(const char *text, struct delta_profile *profile);

/* /v1/game/clan's answer ({"clan", "clan_invites"}) read into profile's
clan and invites: 1, else 0 */
int delta_profile_parse_clan(const char *text, struct delta_profile *profile);

/* /v1/game/players's answer: found called for each ID it names, with its
card (NULL: no profile to show); the count named, -1 if not that answer */
int delta_profile_parse_cards(const char *text, void (*found)(void *context, const char *id,
	const struct delta_profile_card *card), void *context);

/* the friends' state (POST /v1/social/state, and /v1/game/friends's
answer after an action) read into state (social.h): 1 for a whole state,
2 for {"cursor", "same": true} (only state's cursor is set), 0 if not that
answer. Only the incoming requests are kept */
struct social_state;
int delta_profile_parse_social(const char *text, struct social_state *state);
/* /v1/party's answer ({"party", "party_invites"}) into state's party and
party invites (the rest left as it is): 1, or 0 if not that answer */
int delta_profile_parse_party(const char *text, struct social_state *state);
/* a string member of an answer's top ({"error", "reason"}: "reason") into
text ("" for none) */
void delta_profile_parse_text(const char *answer, const char *name, char *text, int size);
/* /v1/social/join's {"join": "halo://join/..."}: the link into link, 1 */
int delta_profile_parse_join(const char *text, char *link, int size);

#endif
