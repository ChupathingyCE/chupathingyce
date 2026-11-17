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
static const char join[] = DISCORD_JOIN_PREFIX "0123456789abcdef0123456789abcdef0123456789ABCDEF0123456789abcdef";
/* a password game's invite (its door's code after a p) */
static const char locked[] = DISCORD_JOIN_PREFIX "p0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef01234567";

static int build(const struct discord_activity_game *game, int presence, int show_server_name, int show_join,
	const char *join_secret)
{
	struct discord_activity_options options = { presence, show_server_name, show_join };
	int size = discord_activity_json(json, sizeof(json), 7, 1234, game, &options, party, join_secret);

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
	/* (with the secret, no buttons: the hover text says where the game is) */
	CHECK(has("\"large_text\":\"Free at chupathingy.org\""));
	CHECK(!has("Milenko's \\\"game\\\": Blood Gulch"));
	CHECK(has("\"small_image\":\"https://stats.chupathingy.org/art/types/slayer.png\""));
	CHECK(has("\"small_text\":\"Team Slayer (macOS)\""));
	CHECK(has("\"size\":[3,16]"));
	CHECK(has("\"timestamps\":{\"start\":1760000000000}"));
	/* hosting a public game: its invite as the join secret, no buttons */
	CHECK(has("\"party\":{\"id\":\"0123456789abcdef0123456789abcdef\",\"size\":[3,16]}"));
	CHECK(has(",\"secrets\":{\"join\":\"halo://join/0123456789abcdef"));
	CHECK(!has("buttons"));
	CHECK(!has("Get Chupathingy"));
	CHECK(!has("chupathingy.org/join"));
	CHECK(has("\"instance\":false"));

	/* the server's name hidden; discord.show_join off: no secret, buttons */
	game.score[0] = 0;
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 0, 0, join) > 0);
	CHECK(balanced());
	CHECK(has("\"state\":\"In game\""));
	CHECK(!has("Milenko"));
	CHECK(!has("secrets"));
	CHECK(!has("halo://"));
	CHECK(has("Get Chupathingy"));
	CHECK(has("Join our Discord"));
	CHECK(has("\"large_text\":\"Blood Gulch\""));
	CHECK(!has("Free at chupathingy.org"));

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
	CHECK(!has("secrets"));
	CHECK(!has("halo://"));
	CHECK(has("Get Chupathingy"));
	CHECK(has("\"large_text\":\"Chupathingy\""));

	CHECK(build(&game, DISCORD_PRESENCE_OFF, 1, 1, join) > 0);
	CHECK(balanced());
	CHECK(!strcmp(json, "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"7\",\"args\":{\"pid\":1234}}"));
	CHECK(!has("halo://"));

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

static void test_join(void)
{
	struct discord_activity_game game = { 0 };
	static const char *const not_joinable[] =
	{
		locked,
		/* an address, in any guise */
		"halo://join/192.168.1.20:2302",
		"halo://join/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde.",
		"10.0.0.1",
		/* too short, too long, another scheme, empty */
		"halo://join/0123456789abcdef",
		"halo://join/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0",
		"https://chupathingy.org/join/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
		"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
		"",
	};
	size_t index;

	CHECK(discord_activity_joinable(join));
	CHECK(!discord_activity_joinable(NULL));
	for (index = 0; index < sizeof(not_joinable) / sizeof(not_joinable[0]); index++)
		CHECK(!discord_activity_joinable(not_joinable[index]));

	/* hosting in the menus (the lobby): the party and the secret still */
	game.screen = DISCORD_SCREEN_MENUS;
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, join) > 0);
	CHECK(balanced());
	CHECK(has("\"state\":\"In menus\""));
	CHECK(has("\"party\":{\"id\":"));
	CHECK(has("\"size\":[1,2]"));
	CHECK(has("\"secrets\":{\"join\":"));
	CHECK(!has("buttons"));

	/* a password game: no secret, the project's buttons */
	game.screen = DISCORD_SCREEN_MULTIPLAYER;
	snprintf(game.map, sizeof(game.map), "bloodgulch");
	game.player_count = 2;
	game.maximum_player_count = 8;
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, locked) > 0);
	CHECK(balanced());
	CHECK(!has("secrets"));
	CHECK(!has("halo://"));
	CHECK(has("Get Chupathingy"));
	CHECK(has("\"size\":[2,8]"));

	/* an address never goes */
	for (index = 0; index < sizeof(not_joinable) / sizeof(not_joinable[0]); index++)
	{
		CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, not_joinable[index]) > 0);
		CHECK(!has("secrets"));
		CHECK(!has("192.168"));
		CHECK(!has("10.0.0.1"));
		CHECK(!has(":2302"));
	}

	/* not hosting: buttons, no secret */
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, NULL) > 0);
	CHECK(!has("secrets"));
	CHECK(has("buttons"));

	/* no party id: no secret either (Discord needs both) */
	{
		struct discord_activity_options options = { DISCORD_PRESENCE_FULL, 1, 1 };

		CHECK(discord_activity_json(json, sizeof(json), 7, 1234, &game, &options, "", join) > 0);
		CHECK(!has("secrets"));
		CHECK(has("buttons"));
	}

	/* minimal and off: no secret */
	CHECK(build(&game, DISCORD_PRESENCE_MINIMAL, 1, 1, join) > 0);
	CHECK(!has("secrets"));
	CHECK(!has("party"));
	CHECK(build(&game, DISCORD_PRESENCE_OFF, 1, 1, join) > 0);
	CHECK(!has("secrets"));
}

/* the large picture's hover text: the site's word with a join secret, the
existing text otherwise (unless there is none) */
static void test_hover_text(void)
{
	struct discord_activity_game game = { 0 };

	/* hosting in the menus: the site's word in place of "Chupathingy" */
	game.screen = DISCORD_SCREEN_MENUS;
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, join) > 0);
	CHECK(balanced());
	CHECK(has("\"large_text\":\"Free at chupathingy.org\""));
	CHECK(!has("buttons"));
	/* not hosting: as it was */
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, NULL) > 0);
	CHECK(has("\"large_text\":\"Chupathingy\""));
	CHECK(!has("Free at"));

	/* the campaign (no secret there): its level's name */
	game.screen = DISCORD_SCREEN_CAMPAIGN;
	snprintf(game.map, sizeof(game.map), "levels\\a10\\a10");
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, NULL) > 0);
	CHECK(!has("Free at"));

	/* a multiplayer game: the map's name without the secret, the site's
	with it; a password game's (no secret) keeps the map's */
	game.screen = DISCORD_SCREEN_MULTIPLAYER;
	snprintf(game.map, sizeof(game.map), "bloodgulch");
	game.player_count = 2;
	game.maximum_player_count = 8;
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, NULL) > 0);
	CHECK(has("\"large_text\":\"Blood Gulch\""));
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, join) > 0);
	CHECK(has("\"large_text\":\"Free at chupathingy.org\""));
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 1, locked) > 0);
	CHECK(has("\"large_text\":\"Blood Gulch\""));
	/* discord.show_join off: no secret, the map's */
	CHECK(build(&game, DISCORD_PRESENCE_FULL, 1, 0, join) > 0);
	CHECK(has("\"large_text\":\"Blood Gulch\""));
	CHECK(!has("Free at"));

	/* minimal: its own text, never the secret's */
	CHECK(build(&game, DISCORD_PRESENCE_MINIMAL, 1, 1, join) > 0);
	CHECK(has("\"large_text\":\"Chupathingy\""));
}

/* a player in a public game it joined shares its invite: only while in the
network game, under the host's party, as the host does */
static void test_player_share(void)
{
	struct discord_activity_game game = { 0 };
	struct discord_activity_options options = { DISCORD_PRESENCE_FULL, 1, 1 };
	static const char host_party[] = "a1b2c3d4e5f6";

	CHECK(!discord_activity_player_shares(NULL));
	/* the menus, or a lobby: not yet */
	game.screen = DISCORD_SCREEN_MENUS;
	game.player_count = 3;
	game.maximum_player_count = 16;
	CHECK(!discord_activity_player_shares(&game));
	/* the campaign (co-op): never */
	game.screen = DISCORD_SCREEN_CAMPAIGN;
	CHECK(!discord_activity_player_shares(&game));
	/* a multiplayer game not over the network: no */
	game.screen = DISCORD_SCREEN_MULTIPLAYER;
	game.maximum_player_count = 0;
	game.player_count = 0;
	CHECK(!discord_activity_player_shares(&game));
	/* in the network game: yes */
	game.player_count = 3;
	game.maximum_player_count = 16;
	CHECK(discord_activity_player_shares(&game));

	/* its activity: the host's party and invite, the game's players */
	snprintf(game.map, sizeof(game.map), "bloodgulch");
	game.mode = DISCORD_MODE_SLAYER;
	CHECK(discord_activity_json(json, sizeof(json), 7, 1234, &game, &options, host_party, join) > 0);
	CHECK(balanced());
	CHECK(has("\"party\":{\"id\":\"a1b2c3d4e5f6\",\"size\":[3,16]}"));
	CHECK(has("\"secrets\":{\"join\":\"halo://join/0123456789abcdef"));
	CHECK(has("\"large_text\":\"Free at chupathingy.org\""));
	CHECK(!has("buttons"));
	/* a password game's (locked) invite never goes, nor with show_join off */
	CHECK(discord_activity_json(json, sizeof(json), 7, 1234, &game, &options, host_party, locked) > 0);
	CHECK(!has("secrets"));
	options.show_join = 0;
	CHECK(discord_activity_json(json, sizeof(json), 7, 1234, &game, &options, host_party, join) > 0);
	CHECK(!has("secrets"));
	CHECK(has("buttons"));
}

static void test_commands(void)
{
	CHECK(discord_activity_subscribe_json(json, sizeof(json), 9, "ACTIVITY_JOIN") > 0);
	CHECK(!strcmp(json, "{\"cmd\":\"SUBSCRIBE\",\"evt\":\"ACTIVITY_JOIN\",\"nonce\":\"9\"}"));
	CHECK(discord_activity_subscribe_json(json, 8, 9, "ACTIVITY_JOIN") < 0);

	CHECK(discord_activity_accept_json(json, sizeof(json), 10, "123456789012345678") > 0);
	CHECK(!strcmp(json, "{\"cmd\":\"SEND_ACTIVITY_JOIN_INVITE\",\"args\":{\"user_id\":\"123456789012345678\"},"
		"\"nonce\":\"10\"}"));
	CHECK(discord_activity_accept_json(json, sizeof(json), 10, "12\"},\"x\":\"") < 0);
	CHECK(discord_activity_accept_json(json, sizeof(json), 10, "") < 0);
}

static void test_events(void)
{
	char value[160];
	char frame[512];

	/* a Join: the invite */
	snprintf(frame, sizeof(frame), "{\"cmd\":\"DISPATCH\",\"data\":{\"secret\":\"%s\"},\"evt\":\"ACTIVITY_JOIN\","
		"\"nonce\":null}", join);
	CHECK(discord_activity_event(frame, value, sizeof(value)) == DISCORD_EVENT_JOIN);
	CHECK(!strcmp(value, join));

	/* the fields in another order, with spaces and an escaped slash */
	CHECK(discord_activity_event("{\"evt\": \"ACTIVITY_JOIN\", \"cmd\": \"DISPATCH\", \"data\": {\"secret\": "
		"\"halo:\\/\\/join\\/0123456789abcdef0123456789abcdef0123456789ABCDEF0123456789abcdef\"}}", value,
		sizeof(value)) == DISCORD_EVENT_JOIN);
	CHECK(!strcmp(value, join));

	/* the answer to the SUBSCRIBE names the event too: not a Join */
	CHECK(discord_activity_event("{\"cmd\":\"SUBSCRIBE\",\"data\":{\"evt\":\"ACTIVITY_JOIN\"},\"evt\":null,"
		"\"nonce\":\"3\"}", value, sizeof(value)) == DISCORD_EVENT_NONE);

	/* a secret that is not an open game's invite is not joined */
	snprintf(frame, sizeof(frame), "{\"cmd\":\"DISPATCH\",\"data\":{\"secret\":\"%s\"},\"evt\":\"ACTIVITY_JOIN\"}",
		locked);
	CHECK(discord_activity_event(frame, value, sizeof(value)) == DISCORD_EVENT_NONE);
	CHECK(!value[0]);
	CHECK(discord_activity_event("{\"cmd\":\"DISPATCH\",\"data\":{\"secret\":\"halo://join/1.2.3.4:2302\"},"
		"\"evt\":\"ACTIVITY_JOIN\"}", value, sizeof(value)) == DISCORD_EVENT_NONE);
	CHECK(discord_activity_event("{\"cmd\":\"DISPATCH\",\"data\":{},\"evt\":\"ACTIVITY_JOIN\"}", value,
		sizeof(value)) == DISCORD_EVENT_NONE);
	/* (too long for the value: not cut into something else) */
	snprintf(frame, sizeof(frame), "{\"cmd\":\"DISPATCH\",\"data\":{\"secret\":\"%s\"},\"evt\":\"ACTIVITY_JOIN\"}",
		join);
	CHECK(discord_activity_event(frame, value, 20) == DISCORD_EVENT_NONE);

	/* an Ask to Join: the user's id */
	CHECK(discord_activity_event("{\"cmd\":\"DISPATCH\",\"data\":{\"user\":{\"id\":\"123456789012345678\","
		"\"username\":\"someone\"}},\"evt\":\"ACTIVITY_JOIN_REQUEST\"}", value, sizeof(value)) ==
		DISCORD_EVENT_JOIN_REQUEST);
	CHECK(!strcmp(value, "123456789012345678"));
	CHECK(discord_activity_event("{\"cmd\":\"DISPATCH\",\"data\":{\"user\":{\"id\":\"12a\"}},"
		"\"evt\":\"ACTIVITY_JOIN_REQUEST\"}", value, sizeof(value)) == DISCORD_EVENT_NONE);

	/* others */
	CHECK(discord_activity_event("{\"cmd\":\"DISPATCH\",\"data\":{\"v\":1},\"evt\":\"READY\"}", value,
		sizeof(value)) == DISCORD_EVENT_NONE);
	CHECK(discord_activity_event("not json", value, sizeof(value)) == DISCORD_EVENT_NONE);
	CHECK(discord_activity_event("", value, sizeof(value)) == DISCORD_EVENT_NONE);
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
	test_join();
	test_hover_text();
	test_player_share();
	test_commands();
	test_events();
	if (failures)
		printf("%d failed\n", failures);
	else if (!print)
		printf("ok\n");
	return failures ? 1 : 0;
}
