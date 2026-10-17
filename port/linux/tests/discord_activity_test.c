/*
DISCORD_ACTIVITY_TEST.C

Tests of the activity Discord shows (port/linux/src/discord_activity.c).
Built and run by tools/test_discord_activity.py; exits nonzero on a failure.
With an argument, prints the commands it builds instead.
*/

#include "../src/discord_activity.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

static char json[DISCORD_ACTIVITY_JSON_SIZE];
static int print;

static const char party[] = "0123456789abcdef0123456789abcdef";
static const char join[] = DISCORD_JOIN_URL "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

static int build(const struct discord_activity_game *game, int presence, int show_server_name, int show_join,
	const char *join_url)
{
	struct discord_activity_options options = { presence, show_server_name, show_join };
	int size = discord_activity_json(json, sizeof(json), 7, 1234, game, &options, party, join_url);

	CHECK(size == (int)strlen(json));
	if (print)
		printf("%s\n\n", json);
	return size;
}

static int has(const char *text)
{
	return strstr(json, text) != NULL;
}

/* (balanced braces and brackets outside strings, and closed strings) */
static int balanced(void)
{
	int depth = 0, in_string = 0;
	const char *character;

	for (character = json; *character; character++)
	{
		if (in_string)
		{
			if (*character == '\\')
				character++;
			else if (*character == '"')
				in_string = 0;
			continue;
		}
		if (*character == '"')
			in_string = 1;
		else if (*character == '{' || *character == '[')
			depth++;
		else if (*character == '}' || *character == ']')
		{
			if (--depth < 0)
				return 0;
		}
	}
	return !depth && !in_string;
}

static void test_menus(void)
{
	struct discord_activity_game game = { 0 };

	game.screen = DISCORD_SCREEN_MENUS;
	snprintf(game.platform, sizeof(game.platform), "macOS");
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, NULL) > 0);
	CHECK(balanced());
	CHECK(has("\"state\":\"In menus\""));
	CHECK(!has("\"details\""));
	CHECK(!has("\"party\""));
	CHECK(!has("timestamps"));
	CHECK(has("\"large_image\":\"https://stats.chupathingy.org/art/icon.png\""));
	CHECK(has("{\"label\":\"Get Chupathingy\",\"url\":\"https://chupathingy.org\"}"));
	CHECK(has("{\"label\":\"Join our Discord\",\"url\":\"https://discord.gg/chupathingy\"}"));
	CHECK(!has("secrets"));
}

static void test_team_slayer(void)
{
	struct discord_activity_game game = { 0 };

	game.screen = DISCORD_SCREEN_MULTIPLAYER;
	snprintf(game.map, sizeof(game.map), "bloodgulch");
	game.mode = DISCORD_MODE_SLAYER;
	game.teams = 1;
	snprintf(game.server_name, sizeof(game.server_name), "Milenko's \"game\"");
	snprintf(game.score, sizeof(game.score), "Red 2 - 1 Blue");
	snprintf(game.platform, sizeof(game.platform), "macOS");
	game.player_count = 3;
	game.maximum_player_count = 16;
	game.start_time = 1760000000000LL;
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, join) > 0);
	CHECK(balanced());
	CHECK(has("\"details\":\"Team Slayer on Blood Gulch\""));
	CHECK(has("\"state\":\"Red 2 - 1 Blue\""));
	CHECK(has("\"large_image\":\"https://stats.chupathingy.org/art/maps/bloodgulch.jpg\""));
	CHECK(has("\"large_text\":\"Milenko's \\\"game\\\": Blood Gulch\""));
	CHECK(has("\"small_image\":\"https://stats.chupathingy.org/art/types/slayer.png\""));
	CHECK(has("\"small_text\":\"Team Slayer (macOS)\""));
	CHECK(has("\"size\":[3,16]"));
	CHECK(has("\"timestamps\":{\"start\":1760000000000}"));
	CHECK(has("{\"label\":\"Join game\",\"url\":\"https://chupathingy.org/join/aaaa"));
	CHECK(has("{\"label\":\"Get Chupathingy\""));
	CHECK(!has("Join our Discord"));
	CHECK(!has("secrets"));

	/* the server's name hidden; no join button */
	game.score[0] = 0;
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 0, 0, join) > 0);
	CHECK(has("\"state\":\"In game\""));
	CHECK(!has("Milenko"));
	CHECK(!has("Join game"));
	CHECK(has("Join our Discord"));

	/* the server's name as the state, with no score */
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, NULL) > 0);
	CHECK(has("\"state\":\"Milenko's \\\"game\\\"\""));
	CHECK(has("\"large_text\":\"Blood Gulch\""));
}

static void test_minimal_and_off(void)
{
	struct discord_activity_game game = { 0 };

	game.screen = DISCORD_SCREEN_MULTIPLAYER;
	snprintf(game.map, sizeof(game.map), "bloodgulch");
	game.mode = DISCORD_MODE_CTF;
	snprintf(game.server_name, sizeof(game.server_name), "Secret base");
	game.maximum_player_count = 16;
	CHECK(build(&game, DISCORD_PRESENCE_MINIMAL, 1, 1, join) > 0);
	CHECK(balanced());
	CHECK(!has("details"));
	CHECK(!has("state"));
	CHECK(!has("Secret base"));
	CHECK(!has("Blood Gulch"));
	CHECK(!has("party"));
	CHECK(!has("Join game"));
	CHECK(has("Get Chupathingy"));
	CHECK(has("\"large_text\":\"Chupathingy\""));

	CHECK(build(&game, DISCORD_PRESENCE_OFF, 1, 1, join) > 0);
	CHECK(balanced());
	CHECK(!strcmp(json, "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"7\",\"args\":{\"pid\":1234}}"));

	CHECK(discord_activity_presence("off") == DISCORD_PRESENCE_OFF);
	CHECK(discord_activity_presence("Minimal") == DISCORD_PRESENCE_MINIMAL);
	CHECK(discord_activity_presence("full") == DISCORD_PRESENCE_FULL);
	CHECK(discord_activity_presence("other") == DISCORD_PRESENCE_FULL);
}

static void test_maps(void)
{
	struct discord_activity_game game = { 0 };

	game.screen = DISCORD_SCREEN_MULTIPLAYER;
	game.mode = DISCORD_MODE_KING;

	/* Halo PC's own map, by its family's suffix */
	snprintf(game.map, sizeof(game.map), "icefields@ce");
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, NULL) > 0);
	CHECK(has("\"details\":\"King of the Hill on Ice Fields\""));
	CHECK(has("art/maps/ce/icefields.jpg"));

	/* a custom map: the game's name for it, the logo */
	snprintf(game.map, sizeof(game.map), "tensity@ce");
	snprintf(game.map_name, sizeof(game.map_name), "Tensity");
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, NULL) > 0);
	CHECK(has("\"details\":\"King of the Hill on Tensity\""));
	CHECK(has("\"large_image\":\"https://stats.chupathingy.org/art/icon.png\""));

	/* a custom map named as a stock one keeps its own name, and the logo */
	snprintf(game.map, sizeof(game.map), "bloodgulch@md");
	snprintf(game.map_name, sizeof(game.map_name), "Blood Gulch Remix");
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, NULL) > 0);
	CHECK(has("on Blood Gulch Remix\""));
	CHECK(has("art/icon.png"));

	/* Grifball, named, with Oddball's skull for an icon (as in its menus) */
	game.mode = DISCORD_MODE_GRIFBALL;
	game.teams = 1;
	game.map_name[0] = 0;
	snprintf(game.map, sizeof(game.map), "carousel");
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, NULL) > 0);
	CHECK(has("\"details\":\"Grifball on Derelict\""));
	CHECK(has("\"small_image\":\"https://stats.chupathingy.org/art/types/oddball.png\""));

	/* a long, multibyte name: cut at a whole character */
	{
		int index;

		for (index = 0; index < DISCORD_ACTIVITY_TEXT_SIZE - 3; index += 2)
		{
			game.server_name[index] = (char)0xC3;
			game.server_name[index + 1] = (char)0xA9;
		}
		game.server_name[index] = 0;
		CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, NULL) > 0);
		CHECK(balanced());
	}
}

static void test_campaign(void)
{
	struct discord_activity_game game = { 0 };

	game.screen = DISCORD_SCREEN_CAMPAIGN;
	snprintf(game.map, sizeof(game.map), "levels\\b30\\b30");
	game.difficulty = 3;
	game.coop = 1;
	game.player_count = 2;
	game.maximum_player_count = 4;
	snprintf(game.platform, sizeof(game.platform), "Windows");
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, NULL) > 0);
	CHECK(balanced());
	CHECK(has("\"details\":\"Co-op on Legendary\""));
	CHECK(has("\"state\":\"Campaign: The Silent Cartographer\""));
	CHECK(has("art/maps/b30.jpg"));
	CHECK(has("\"small_text\":\"Co-op campaign (Windows)\""));
	CHECK(has("\"size\":[2,4]"));

	game.coop = 0;
	game.difficulty = 1;
	game.maximum_player_count = 0;
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, NULL) > 0);
	CHECK(has("\"details\":\"Solo on Normal\""));
	CHECK(!has("party"));
}

int main(int argc, char **argv)
{
	(void)argv;
	print = argc > 1;
	test_menus();
	test_team_slayer();
	test_minimal_and_off();
	test_maps();
	test_campaign();
	if (failures)
		printf("%d failed\n", failures);
	else if (!print)
		printf("ok\n");
	return failures ? 1 : 0;
}
