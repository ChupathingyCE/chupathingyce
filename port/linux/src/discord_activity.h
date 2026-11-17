/*
DISCORD_ACTIVITY.H

What the Discord desktop client shows of the game (port/linux/src/
discord_activity.c): the SET_ACTIVITY command p2p_discord.c sends, built
from what the game is doing (port/linux/game/discord_game.c) and the
player's settings (discord.presence, discord.show_server_name,
discord.show_join), and what Discord's events tell. Its pictures
are the game list site's, by URL. No SDL or game dependency, so its tests
build anywhere (port/linux/tests/discord_activity_test.c).
*/

#ifndef DISCORD_ACTIVITY_H
#define DISCORD_ACTIVITY_H

/* discord.presence */
enum
{
	DISCORD_PRESENCE_OFF = 0,
	/* "Playing Chupathingy" and nothing more */
	DISCORD_PRESENCE_MINIMAL,
	DISCORD_PRESENCE_FULL,
};

/* where the player is */
enum
{
	DISCORD_SCREEN_MENUS = 0,
	DISCORD_SCREEN_CAMPAIGN,
	DISCORD_SCREEN_MULTIPLAYER,
};

/* the game type (the game's engine index for the first five) */
enum
{
	DISCORD_MODE_NONE = 0,
	DISCORD_MODE_CTF,
	DISCORD_MODE_SLAYER,
	DISCORD_MODE_ODDBALL,
	DISCORD_MODE_KING,
	DISCORD_MODE_RACE,
	DISCORD_MODE_BOMB,
	DISCORD_MODE_GRIFBALL,
	NUMBER_OF_DISCORD_MODES
};

/* the invite a public game's join secret carries, before its code */
#define DISCORD_JOIN_PREFIX "halo://join/"

enum
{
	DISCORD_ACTIVITY_TEXT_SIZE = 64,
	/* the largest command built */
	DISCORD_ACTIVITY_JSON_SIZE = 2048,
};

struct discord_activity_game
{
	int screen;
	/* the map as the game names it: "bloodgulch", "levels\\b30\\b30",
	"icefields@ce", "foo@md" */
	char map[DISCORD_ACTIVITY_TEXT_SIZE];
	/* its name to show (UTF-8), where the game knows one this does not (a
	Custom Edition or HaloMD map's); "" for this file's own */
	char map_name[DISCORD_ACTIVITY_TEXT_SIZE];
	int mode;
	int teams;
	/* campaign: played with others, and its difficulty (0 easy to 3
	legendary) */
	int coop;
	int difficulty;
	/* a network game's name (UTF-8; "" for none) and its players (0 for a
	game not over the network) */
	char server_name[DISCORD_ACTIVITY_TEXT_SIZE];
	int player_count;
	int maximum_player_count;
	/* a team game's score as it stands ("Red 2 - 1 Blue"; "" for none) */
	char score[DISCORD_ACTIVITY_TEXT_SIZE];
	/* this machine's platform as players know it ("macOS", "Windows") */
	char platform[24];
	/* when this game began (Unix time, milliseconds; 0 unknown) */
	long long start_time;
};

struct discord_activity_options
{
	int presence;
	int show_server_name;
	/* a public game's join secret, for Discord's Join and Ask to Join
	(discord.show_join) */
	int show_join;
};

/* what discord_activity_event finds in a frame from Discord */
enum
{
	DISCORD_EVENT_NONE = 0,
	/* the player chose Join (or a host accepted their Ask to Join): the
	value is the invite */
	DISCORD_EVENT_JOIN,
	/* someone asks to join this machine's game: the value is their user id */
	DISCORD_EVENT_JOIN_REQUEST,
};

/* the SET_ACTIVITY command into json (size bytes): its length, or -1 if it
does not fit. party is this machine's party id (hex); join_secret the
invite of the public game this machine hosts (DISCORD_JOIN_PREFIX and its
code), or NULL for none. With a joinable invite (discord_activity_joinable),
discord.show_join and presence "full", the activity carries it as its join
secret, with the party's size, no buttons (Discord takes one or the
other) and "Free at chupathingy.org" as the large picture's hover text;
otherwise the project's buttons (and that hover text only where there
would be none). DISCORD_PRESENCE_OFF clears the
activity. */
int discord_activity_json(char *json, int size, unsigned long nonce, unsigned long process_id,
	const struct discord_activity_game *game, const struct discord_activity_options *options, const char *party,
	const char *join_secret);

/* whether a player in a public game it joined (not its host) shares the
game's invite now, by what the game tells: only while in the network game
(a multiplayer game with players), not in the menus or a lobby. p2p.c
says whether the game joined is a public one and its host still reached */
int discord_activity_player_shares(const struct discord_activity_game *game);

/* whether text is an invite Discord may carry: DISCORD_JOIN_PREFIX and the
code of an open game (hex digits only: not a password game's, never an
address) */
int discord_activity_joinable(const char *text);

/* the SUBSCRIBE command for one of Discord's events ("ACTIVITY_JOIN") into
json: its length, or -1 */
int discord_activity_subscribe_json(char *json, int size, unsigned long nonce, const char *event);

/* the SEND_ACTIVITY_JOIN_INVITE command, accepting the Ask to Join of the
user (digits) into json: its length, or -1 */
int discord_activity_accept_json(char *json, int size, unsigned long nonce, const char *user_id);

/* what a frame from Discord (its JSON) tells: DISCORD_EVENT_JOIN with a
joinable invite, DISCORD_EVENT_JOIN_REQUEST with the user's id, into value
(size bytes); else DISCORD_EVENT_NONE */
int discord_activity_event(const char *json, char *value, int size);

/* what the player is doing in a few words, for the Guide's card (social.c,
port/linux/game/guide.c), with a middle dot between them: "Team Slayer ·
Blood Gulch · 11/16", "Campaign · Halo", "In the menus"; its length, or -1
if it does not fit */
int discord_activity_status_line(const struct discord_activity_game *game, char *text, int size);

/* a map's name as the status lines show it ("Blood Gulch"; a campaign
level's with campaign set; "a custom map" for one with no name) */
void discord_activity_map_name(const char *map, int campaign, char *text, int size);

/* discord.presence's value, by its word ("full", "minimal", "off"; any other:
full) */
int discord_activity_presence(const char *word);

#endif
