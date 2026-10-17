/*
DISCORD_ACTIVITY.C

The activity the Discord desktop client shows while the game runs
(discord_activity.h), as JSON for p2p_discord.c to send:

- details: "<game type> on <map>" in multiplayer ("Team Slayer on Blood
  Gulch"), "Solo on Legendary" or "Co-op on Heroic" in the campaign;
- state: a team game's score ("Red 2 - 1 Blue"), else the network game's
  name (discord.show_server_name), else "In game", with its players as the
  party's size; "Campaign: <level>"; "In menus";
- the pictures, by URL (Discord fetches them through its media proxy, so the
  application needs no art of its own): the game list site's picture of a
  stock map or campaign level, else the logo; the game type's icon small,
  with the type and this machine's platform;
- the time the game began;
- buttons: while hosting a public game (one in the server list, with no
  password), "Join game" to its page on the project's site, which opens
  its invite in whichever client the viewer has (Chupathingy's or
  OpenCE's: Discord's own Ask to Join works only within one application),
  and "Get Chupathingy"; otherwise "Get Chupathingy" and "Join our
  Discord".

discord.presence "minimal" leaves only "Playing Chupathingy" and the
project's buttons; "off" clears the activity.
*/

#include "discord_activity.h"

#include <stdio.h>
#include <string.h>

#define ART_URL "https://stats.chupathingy.org/art/"
#define LOGO_URL ART_URL "icon.png"
#define SITE_URL "https://chupathingy.org"
#define DISCORD_URL "https://discord.gg/chupathingy"

/* (Discord's limit on an activity's texts, in bytes as this counts) */
enum
{
	MAXIMUM_TEXT = 127,
};

/* the stock multiplayer maps: the Xbox's, then Halo PC's own; the site has
a picture of each of the Xbox's in art/maps, and Halo PC's picture of all
of them in art/maps/ce */
static const struct
{
	const char *file;
	const char *name;
	int xbox;
} stock_maps[] =
{
	{ "beavercreek", "Battle Creek", 1 },
	{ "sidewinder", "Sidewinder", 1 },
	{ "damnation", "Damnation", 1 },
	{ "ratrace", "Rat Race", 1 },
	{ "prisoner", "Prisoner", 1 },
	{ "hangemhigh", "Hang 'Em High", 1 },
	{ "chillout", "Chill Out", 1 },
	{ "carousel", "Derelict", 1 },
	{ "boardingaction", "Boarding Action", 1 },
	{ "bloodgulch", "Blood Gulch", 1 },
	{ "wizard", "Wizard", 1 },
	{ "putput", "Chiron TL-34", 1 },
	{ "longest", "Longest", 1 },
	{ "icefields", "Ice Fields", 0 },
	{ "deathisland", "Death Island", 0 },
	{ "dangercanyon", "Danger Canyon", 0 },
	{ "infinity", "Infinity", 0 },
	{ "timberland", "Timberland", 0 },
	{ "gephyrophobia", "Gephyrophobia", 0 },
};

/* the campaign's levels (the site's art/maps/<level>.jpg) */
static const struct
{
	const char *file;
	const char *name;
} levels[] =
{
	{ "a10", "The Pillar of Autumn" },
	{ "a30", "Halo" },
	{ "a50", "The Truth and Reconciliation" },
	{ "b30", "The Silent Cartographer" },
	{ "b40", "Assault on the Control Room" },
	{ "c10", "343 Guilty Spark" },
	{ "c20", "The Library" },
	{ "c40", "Two Betrayals" },
	{ "d20", "Keyes" },
	{ "d40", "The Maw" },
};

/* the game types: their names, and the site's icons (art/types; NULL: the
icon). Bomb and Grifball show Oddball's skull, as their menus do */
static const struct
{
	const char *name;
	const char *icon;
} modes[NUMBER_OF_DISCORD_MODES] =
{
	{ "Multiplayer", NULL },
	{ "Capture the Flag", "ctf" },
	{ "Slayer", "slayer" },
	{ "Oddball", "oddball" },
	{ "King of the Hill", "king" },
	{ "Race", "race" },
	{ "Bomb", "oddball" },
	{ "Grifball", "oddball" },
};

static const char *const difficulties[] = { "Easy", "Normal", "Heroic", "Legendary" };

struct text
{
	char *buffer;
	int size;
	int used;
	int overflow;
};

static void append(struct text *text, const char *format, const char *value)
{
	int written;

	if (text->overflow)
		return;
	written = snprintf(text->buffer + text->used, (size_t)(text->size - text->used), format, value);
	if (written < 0 || written >= text->size - text->used)
	{
		text->overflow = 1;
		return;
	}
	text->used += written;
}

static void raw(struct text *text, const char *value)
{
	append(text, "%s", value);
}

/* a JSON string of value: escaped, cut to MAXIMUM_TEXT bytes (at a whole
UTF-8 character), padded to Discord's least of two */
static void string(struct text *text, const char *value)
{
	char escaped[MAXIMUM_TEXT * 6 + 8];
	int used = 0, length = 0, count = 0;

	escaped[used++] = '"';
	while (value[length] && length < MAXIMUM_TEXT)
		length++;
	/* (not in the middle of a character) */
	while (value[length] && length > 0 && ((unsigned char)value[length] & 0xC0) == 0x80)
		length--;
	for (count = 0; count < length; count++)
	{
		unsigned char character = (unsigned char)value[count];

		if (character == '"' || character == '\\')
		{
			escaped[used++] = '\\';
			escaped[used++] = (char)character;
		}
		else if (character < 0x20)
			used += snprintf(escaped + used, 7, "\\u%04x", character);
		else
			escaped[used++] = (char)character;
	}
	for (; count < 2; count++)
		escaped[used++] = ' ';
	escaped[used++] = '"';
	escaped[used] = 0;
	raw(text, escaped);
}

static int same_word(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
	{
		char x = *a >= 'A' && *a <= 'Z' ? (char)(*a - 'A' + 'a') : *a;
		char y = *b >= 'A' && *b <= 'Z' ? (char)(*b - 'A' + 'a') : *b;

		if (x != y)
			return 0;
	}
	return !*a && !*b;
}

int discord_activity_presence(const char *word)
{
	if (word && same_word(word, "off"))
		return DISCORD_PRESENCE_OFF;
	if (word && same_word(word, "minimal"))
		return DISCORD_PRESENCE_MINIMAL;
	return DISCORD_PRESENCE_FULL;
}

/* a map's file (the path's last part, without @ce, @md or @pc), and whether
it is one of the Xbox's (no suffix) */
static int map_file(const char *map, char *file, int size)
{
	const char *start = map, *at;
	int length;

	for (; *map; map++)
	{
		if (*map == '\\' || *map == '/')
			start = map + 1;
	}
	at = strchr(start, '@');
	length = at ? (int)(at - start) : (int)strlen(start);
	if (length > size - 1)
		length = size - 1;
	memcpy(file, start, (size_t)length);
	file[length] = 0;
	for (; *file; file++)
		*file = *file >= 'A' && *file <= 'Z' ? (char)(*file - 'A' + 'a') : *file;
	return !at;
}

/* the map's name to show and its picture's URL */
static void map_describe(const struct discord_activity_game *game, char *name, int name_size, char *art,
	int art_size)
{
	char file[DISCORD_ACTIVITY_TEXT_SIZE];
	int xbox = map_file(game->map, file, sizeof(file));
	size_t index;

	snprintf(name, (size_t)name_size, "%s", game->map_name[0] ? game->map_name : file);
	snprintf(art, (size_t)art_size, "%s", LOGO_URL);
	if (game->screen == DISCORD_SCREEN_CAMPAIGN)
	{
		for (index = 0; index < sizeof(levels) / sizeof(levels[0]); index++)
		{
			if (!strcmp(file, levels[index].file))
			{
				snprintf(name, (size_t)name_size, "%s", levels[index].name);
				snprintf(art, (size_t)art_size, ART_URL "maps/%s.jpg", file);
			}
		}
		return;
	}
	for (index = 0; index < sizeof(stock_maps) / sizeof(stock_maps[0]); index++)
	{
		if (!strcmp(file, stock_maps[index].file))
		{
			/* (another family's file of a stock map's name may be another
			map: its own name is kept, and Halo PC's picture only for Halo
			PC's own maps) */
			if (xbox || !game->map_name[0])
				snprintf(name, (size_t)name_size, "%s", stock_maps[index].name);
			if (xbox && stock_maps[index].xbox)
				snprintf(art, (size_t)art_size, ART_URL "maps/%s.jpg", file);
			else if (!xbox && !game->map_name[0])
				snprintf(art, (size_t)art_size, ART_URL "maps/ce/%s.jpg", file);
			else if (!xbox && !strcmp(game->map_name, stock_maps[index].name))
				snprintf(art, (size_t)art_size, ART_URL "maps/ce/%s.jpg", file);
		}
	}
	if (!name[0])
		snprintf(name, (size_t)name_size, "%s", "a custom map");
}

/* the buttons: a public game's page and the project's site while hosting
one (and discord.show_join), else the site and the Discord server */
static void buttons(struct text *text, const char *join_url)
{
	raw(text, ",\"buttons\":[");
	if (join_url && join_url[0])
	{
		raw(text, "{\"label\":\"Join game\",\"url\":");
		string(text, join_url);
		raw(text, "},{\"label\":\"Get Chupathingy\",\"url\":\"" SITE_URL "\"}]");
		return;
	}
	raw(text, "{\"label\":\"Get Chupathingy\",\"url\":\"" SITE_URL "\"},"
		"{\"label\":\"Join our Discord\",\"url\":\"" DISCORD_URL "\"}]");
}

/* a network game's players, as the party's size */
static void party_size(struct text *text, const struct discord_activity_game *game, const char *party)
{
	int count = game->player_count, maximum = game->maximum_player_count;
	char numbers[48];

	if (!party || !party[0] || maximum <= 0)
		return;
	if (count < 1)
		count = 1;
	if (count > maximum)
		maximum = count;
	raw(text, ",\"party\":{\"id\":");
	string(text, party);
	snprintf(numbers, sizeof(numbers), ",\"size\":[%d,%d]}", count, maximum);
	raw(text, numbers);
}

int discord_activity_json(char *json, int size, unsigned long nonce, unsigned long process_id,
	const struct discord_activity_game *game, const struct discord_activity_options *options, const char *party,
	const char *join_url)
{
	struct text text = { json, size, 0, 0 };
	char number[64];

	if (size <= 0)
		return -1;
	json[0] = 0;
	if (!options->show_join)
		join_url = NULL;
	snprintf(number, sizeof(number), "%lu", nonce);
	raw(&text, "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"");
	raw(&text, number);
	snprintf(number, sizeof(number), "%lu", process_id);
	raw(&text, "\",\"args\":{\"pid\":");
	raw(&text, number);
	if (options->presence == DISCORD_PRESENCE_OFF)
	{
		raw(&text, "}}");
		return text.overflow ? -1 : text.used;
	}
	raw(&text, ",\"activity\":{");
	if (options->presence == DISCORD_PRESENCE_MINIMAL)
	{
		raw(&text, "\"assets\":{\"large_image\":\"" LOGO_URL "\",\"large_text\":\"Chupathingy\"}");
		/* (nothing of the game: no join either) */
		join_url = NULL;
	}
	else
	{
		char details[DISCORD_ACTIVITY_TEXT_SIZE * 2 + 32];
		char state[DISCORD_ACTIVITY_TEXT_SIZE + 32];
		char map_name[DISCORD_ACTIVITY_TEXT_SIZE];
		char large_text[DISCORD_ACTIVITY_TEXT_SIZE * 2 + 8];
		char small_text[DISCORD_ACTIVITY_TEXT_SIZE + 32];
		char mode_name[48];
		char art[160];
		const char *icon = NULL;
		const char *server = options->show_server_name ? game->server_name : "";
		int mode = game->mode >= 0 && game->mode < NUMBER_OF_DISCORD_MODES ? game->mode : DISCORD_MODE_NONE;

		map_describe(game, map_name, sizeof(map_name), art, sizeof(art));
		snprintf(large_text, sizeof(large_text), "%s", map_name);
		small_text[0] = 0;
		switch (game->screen)
		{
		case DISCORD_SCREEN_CAMPAIGN:
			snprintf(details, sizeof(details), "%s on %s", game->coop ? "Co-op" : "Solo",
				difficulties[game->difficulty >= 0 && game->difficulty < 4 ? game->difficulty : 1]);
			snprintf(state, sizeof(state), "Campaign: %s", map_name);
			snprintf(small_text, sizeof(small_text), "%s", game->coop ? "Co-op campaign" : "Campaign");
			break;
		case DISCORD_SCREEN_MULTIPLAYER:
			snprintf(mode_name, sizeof(mode_name), "%s%s",
				game->teams && mode == DISCORD_MODE_SLAYER ? "Team " : "", modes[mode].name);
			snprintf(details, sizeof(details), "%s on %s", mode_name, map_name);
			/* (the score, else the game's name; the name then with the map) */
			if (game->score[0])
			{
				snprintf(state, sizeof(state), "%s", game->score);
				if (server[0])
					snprintf(large_text, sizeof(large_text), "%s: %s", server, map_name);
			}
			else
				snprintf(state, sizeof(state), "%s", server[0] ? server : "In game");
			icon = modes[mode].icon;
			snprintf(small_text, sizeof(small_text), "%s", mode_name);
			break;
		default:
			details[0] = 0;
			snprintf(state, sizeof(state), "In menus");
			snprintf(large_text, sizeof(large_text), "Chupathingy");
			break;
		}
		if (small_text[0] && game->platform[0])
			snprintf(small_text + strlen(small_text), sizeof(small_text) - strlen(small_text), " (%s)",
				game->platform);
		if (details[0])
		{
			raw(&text, "\"details\":");
			string(&text, details);
			raw(&text, ",");
		}
		raw(&text, "\"state\":");
		string(&text, state);
		if (game->screen != DISCORD_SCREEN_MENUS && game->start_time > 0)
		{
			snprintf(number, sizeof(number), ",\"timestamps\":{\"start\":%lld}", game->start_time);
			raw(&text, number);
		}
		raw(&text, ",\"assets\":{\"large_image\":");
		string(&text, art);
		raw(&text, ",\"large_text\":");
		string(&text, large_text);
		if (small_text[0])
		{
			char small[160];

			if (icon)
				snprintf(small, sizeof(small), ART_URL "types/%s.png", icon);
			else
				snprintf(small, sizeof(small), "%s", LOGO_URL);
			raw(&text, ",\"small_image\":");
			string(&text, small);
			raw(&text, ",\"small_text\":");
			string(&text, small_text);
		}
		raw(&text, "}");
		if (game->screen != DISCORD_SCREEN_MENUS)
			party_size(&text, game, party);
	}
	buttons(&text, join_url);
	raw(&text, ",\"instance\":false}}}");
	return text.overflow ? -1 : text.used;
}
