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

/* (as many as the site sends: list_server.py's GAME_BADGES, GAME_RECENT,
GAME_COPIES and GAME_ROLES) */
#define DELTA_PROFILE_BADGES 8
#define DELTA_PROFILE_RECENT 4
#define DELTA_PROFILE_COPIES 16
#define DELTA_PROFILE_ROLES 32
/* a player ID, in hexadecimal */
#define DELTA_PROFILE_ID_LENGTH 32

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
	platform keys: "pc_linux", "steam_deck"; empty when not known) */
	char label[12];
	char platform[24];
	long long linked;
	long long last_seen;
	/* this copy; the copy that made the profile (it can't be unlinked) */
	int this_copy;
	int first;
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
	int recent_count;
	struct delta_profile_game recent[DELTA_PROFILE_RECENT];
	/* (more_*: how many the site left out, its answer being cut to size) */
	int copy_count;
	int more_copies;
	struct delta_profile_copy copies[DELTA_PROFILE_COPIES];
	int role_count;
	int more_roles;
	struct delta_profile_role roles[DELTA_PROFILE_ROLES];
	/* the site's privacy settings */
	int has_privacy;
	int hide_stats;
	int hide_heatmaps;
	int show_presence;
};

/* another player's public card (POST /v1/game/players): their profile's
handle, display name and first badge */
struct delta_profile_card
{
	char handle[48];
	char display[64];
	char badge[48];
};

/* /v1/game/profile's answer read into profile: 1, else 0 (not JSON, or
not that answer) */
int delta_profile_parse(const char *text, struct delta_profile *profile);

/* /v1/game/privacy's answer ({"privacy": {...}}) read into profile's
privacy: 1, else 0 */
int delta_profile_parse_privacy(const char *text, struct delta_profile *profile);

/* /v1/game/players's answer: found called for each ID it names, with its
card (NULL: no profile to show); the count named, -1 if not that answer */
int delta_profile_parse_cards(const char *text, void (*found)(void *context, const char *id,
	const struct delta_profile_card *card), void *context);

#endif
