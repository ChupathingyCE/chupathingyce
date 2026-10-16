/*
DISCORD_ACTIVITY.H

What the Discord desktop client shows of the game (port/linux/src/
discord_activity.c): the SET_ACTIVITY command p2p_discord.c sends, built
from what the game is doing (port/linux/game/discord_game.c) and the
player's settings (discord.presence, discord.show_server_name). Its pictures
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

/* a public game's page, by its invite's code (halo://join/<code>): it
opens the invite in whichever client the viewer has */
#define DISCORD_JOIN_URL "https://chupathingy.org/join/"

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
	/* a public game's Join game button (discord.show_join) */
	int show_join;
};

/* the SET_ACTIVITY command into json (size bytes): its length, or -1 if it
does not fit. party is this machine's party id (hex); join_url the web page
of the public game this machine hosts (its invite: DISCORD_JOIN_URL), or
NULL for none. DISCORD_PRESENCE_OFF clears the activity. */
int discord_activity_json(char *json, int size, unsigned long nonce, unsigned long process_id,
	const struct discord_activity_game *game, const struct discord_activity_options *options, const char *party,
	const char *join_url);

/* discord.presence's value, by its word ("full", "minimal", "off"; any other:
full) */
int discord_activity_presence(const char *word);

#endif
