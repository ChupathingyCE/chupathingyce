/*
MEDAL_RULES_TEST.C

Tests of the medals' rules (port/linux/src/medal_rules.h) and of the local
service record's math and file (port/linux/src/service_record.c). Built and
run by tools/test_medal_rules.py; exits nonzero on a failure. With a path,
it also saves a record there and loads it back.
*/

#include "../src/medal_rules.h"
#include "../src/service_record.h"

#include <stdio.h>
#include <stdlib.h>
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

enum { PLAYERS = 4 };

static struct medal_game game;
static struct medal_player players[PLAYERS];
static int earned[PLAYERS][NUMBER_OF_MEDALS];

static void start(void)
{
	int index;

	medal_game_reset(&game);
	for (index = 0; index < PLAYERS; index++)
		medal_player_reset(&players[index]);
	memset(earned, 0, sizeof(earned));
}

/* killer kills victim at second (killer -1: no player's kill; counts 0: a
betrayal or a suicide); the medals it made */
static int kill_at(int killer, int victim, double second, unsigned int kinds, int counts, int *medals)
{
	int list[MEDAL_RULES_MAXIMUM_PER_KILL], count, index;
	int *out = medals ? medals : list;

	count = medal_rules_kill(&game, killer >= 0 ? &players[killer] : NULL, killer, &players[victim], victim, counts,
		(long)(second * MEDAL_RULES_TICKS_PER_SECOND + 0.5), kinds, out);
	for (index = 0; index < count && killer >= 0; index++)
		earned[killer][out[index]]++;
	return count;
}

static int has(int const *medals, int count, int medal)
{
	int index;

	for (index = 0; index < count; index++)
	{
		if (medals[index] == medal)
			return 1;
	}
	return 0;
}

static void test_names(void)
{
	int medal;

	for (medal = 0; medal < NUMBER_OF_MEDALS; medal++)
	{
		CHECK(medal_rules_key(medal)[0] && medal_rules_name(medal)[0]);
		CHECK(medal_rules_find(medal_rules_key(medal)) == medal);
	}
	CHECK(!strcmp(medal_rules_key(MEDAL_GRENADE_STICK), "grenade_stick"));
	CHECK(!strcmp(medal_rules_name(MEDAL_GRENADE_STICK), "Stuck"));
	CHECK(medal_rules_find("victory") == -1 && medal_rules_find(NULL) == -1);
	CHECK(!strcmp(medal_rules_key(NUMBER_OF_MEDALS), "") && !strcmp(medal_rules_key(-1), ""));
}

static void test_multikills(void)
{
	int medals[MEDAL_RULES_MAXIMUM_PER_KILL], index, count;
	/* (Halo: Combat Evolved's four, then Halo 3's, Killionaire from the
	ninth kill on) */
	static int const expected[] = { -1, MEDAL_DOUBLE_KILL, MEDAL_TRIPLE_KILL, MEDAL_KILLTACULAR, MEDAL_KILLTROCITY,
		MEDAL_KILLIMANJARO, MEDAL_KILLTASTROPHE, MEDAL_KILLPOCALYPSE, MEDAL_KILLIONAIRE, MEDAL_KILLIONAIRE };

	start();
	for (index = 0; index < 10; index++)
	{
		count = kill_at(0, 1 + index % 3, 10.0 + index * 3.9, 0, 1, medals);
		if (expected[index] >= 0)
			CHECK(has(medals, count, expected[index]));
	}
	CHECK(earned[0][MEDAL_KILLIONAIRE] == 2 && earned[0][MEDAL_DOUBLE_KILL] == 1);
	/* (more than 4 seconds after the last: a new chain) */
	count = kill_at(0, 1, 10.0 + 9 * 3.9 + 4.1, 0, 1, medals);
	CHECK(!has(medals, count, MEDAL_DOUBLE_KILL) && !has(medals, count, MEDAL_KILLIONAIRE));
	/* (exactly 4 seconds: still the game's window) */
	count = kill_at(0, 2, 10.0 + 9 * 3.9 + 8.1, 0, 1, medals);
	CHECK(has(medals, count, MEDAL_DOUBLE_KILL));
}

static void test_sprees(void)
{
	int medals[MEDAL_RULES_MAXIMUM_PER_KILL], index, count;

	start();
	for (index = 1; index <= 45; index++)
	{
		count = kill_at(0, 1 + index % 3, index * 10.0, 0, 1, medals);
		if (index % 5 == 0 && index <= 40)
			CHECK(has(medals, count, MEDAL_KILLING_SPREE + index / 5 - 1));
	}
	CHECK(earned[0][MEDAL_UNFRIGGENBELIEVABLE] == 1 && earned[0][MEDAL_RAMPAGE] == 1);
	CHECK(players[0].best_spree == 45 && players[0].spree == 45);
	/* (no multikills: ten seconds apart) */
	CHECK(earned[0][MEDAL_DOUBLE_KILL] == 0);
	/* (dying ends it; the best stays) */
	kill_at(1, 0, 1000.0, 0, 1, NULL);
	CHECK(players[0].spree == 0 && players[0].best_spree == 45);
	/* (ending a spree of 5 or more: a Killjoy) */
	CHECK(earned[1][MEDAL_KILLJOY] == 1);
}

static void test_weapon_sprees(void)
{
	int index;

	start();
	for (index = 0; index < 10; index++)
		kill_at(0, 1 + index % 3, index * 10.0, MEDAL_KILL_SNIPER, 1, NULL);
	CHECK(earned[0][MEDAL_SNIPER_KILL] == 10);
	CHECK(earned[0][MEDAL_SNIPER_SPREE] == 1 && earned[0][MEDAL_SHARPSHOOTER] == 1);
	/* (a death starts the weapon's spree again) */
	kill_at(1, 0, 200.0, 0, 1, NULL);
	for (index = 0; index < 4; index++)
		kill_at(0, 2, 300.0 + index * 10.0, MEDAL_KILL_SHOTGUN, 1, NULL);
	CHECK(earned[0][MEDAL_SHOTGUN_SPREE] == 0);
	kill_at(0, 2, 400.0, MEDAL_KILL_SHOTGUN, 1, NULL);
	CHECK(earned[0][MEDAL_SHOTGUN_SPREE] == 1);
	for (index = 0; index < 5; index++)
		kill_at(3, 2, 500.0 + index * 10.0, MEDAL_KILL_STICK | MEDAL_KILL_SPLATTER, 1, NULL);
	CHECK(earned[3][MEDAL_GRENADE_STICK] == 5 && earned[3][MEDAL_STICK_SPREE] == 1);
	CHECK(earned[3][MEDAL_SPLATTER] == 5 && earned[3][MEDAL_SPLATTER_SPREE] == 1);
	CHECK(earned[3][MEDAL_VEHICULAR_MANSLAUGHTER] == 0);
}

static void test_kinds_first_strike_revenge(void)
{
	int medals[MEDAL_RULES_MAXIMUM_PER_KILL], count;

	start();
	/* (a betrayal or a suicide first: nothing, and no First Strike yet) */
	CHECK(kill_at(2, 3, 1.0, 0, 0, NULL) == 0);
	CHECK(kill_at(1, 1, 2.0, 0, 1, NULL) == 0);
	count = kill_at(0, 1, 5.0, MEDAL_KILL_MELEE, 1, medals);
	CHECK(has(medals, count, MEDAL_FIRST_STRIKE) && has(medals, count, MEDAL_BEAT_DOWN));
	CHECK(medals[0] == MEDAL_FIRST_STRIKE);
	/* (once a game) */
	count = kill_at(1, 0, 20.0, MEDAL_KILL_FROM_GRAVE, 1, medals);
	CHECK(!has(medals, count, MEDAL_FIRST_STRIKE));
	/* (player 1 kills the one who killed them, from the grave) */
	CHECK(has(medals, count, MEDAL_REVENGE) && has(medals, count, MEDAL_FROM_THE_GRAVE));
	/* (and once: the next kill of the same player is no revenge) */
	kill_at(1, 0, 40.0, 0, 1, NULL);
	CHECK(earned[1][MEDAL_REVENGE] == 1);
	/* (a betrayal leaves no one to take revenge on) */
	kill_at(2, 3, 60.0, 0, 0, NULL);
	count = kill_at(3, 2, 70.0, 0, 1, medals);
	CHECK(!has(medals, count, MEDAL_REVENGE));
	/* (no player's kill: a fall) */
	CHECK(kill_at(-1, 2, 80.0, 0, 1, NULL) == 0);
	medal_game_reset(&game);
	count = kill_at(2, 3, 90.0, 0, 1, medals);
	CHECK(has(medals, count, MEDAL_FIRST_STRIKE));
}

static void test_record_math(void)
{
	static struct service_record record;
	struct service_record_weapon weapons[] = {
		{ "weapons\\sniper rifle\\sniper bullet", 7 }, { "weapons\\pistol\\bullet", 3 },
		{ "vehicles\\warthog\\bullet", 1 }, { "globals\\falling", 4 },
	};
	struct service_record_game played;
	int medals[4];
	long counts[4];
	char text[64];

	service_record_clear(&record);
	memset(&played, 0, sizeof(played));
	played.map = "levels\\test\\bloodgulch\\bloodgulch";
	played.won = 1;
	played.kills = 11;
	played.deaths = 4;
	played.assists = 2;
	played.seconds = 600;
	played.best_spree = 6;
	played.medals[MEDAL_DOUBLE_KILL] = 2;
	played.medals[MEDAL_KILLING_SPREE] = 1;
	played.medals[MEDAL_SNIPER_KILL] = 7;
	played.weapons = weapons;
	played.weapon_count = 4;
	service_record_add_game(&record, &played);
	played.map = "prisoner";
	played.won = 0;
	played.best_spree = 3;
	played.weapon_count = 2;
	service_record_add_game(&record, &played);
	played.map = "BloodGulch";
	service_record_add_game(&record, &played);
	CHECK(record.games == 3 && record.wins == 1 && record.kills == 33 && record.deaths == 12 && record.assists == 6);
	CHECK(record.seconds == 1800 && record.best_spree == 6);
	CHECK(record.medals[MEDAL_SNIPER_KILL] == 21 && service_record_medal_total(&record) == 30);
	CHECK(!strcmp(service_record_favorite_map(&record), "bloodgulch"));
	CHECK(!strcmp(service_record_favorite_weapon(&record), "sniper rifle"));
	/* (the game's own effects are no weapon) */
	CHECK(record.weapon_count == 3);
	CHECK(service_record_top_medals(&record, medals, counts, 4) == 3);
	CHECK(medals[0] == MEDAL_SNIPER_KILL && counts[0] == 21);
	CHECK(medals[1] == MEDAL_DOUBLE_KILL && counts[1] == 6 && medals[2] == MEDAL_KILLING_SPREE);
	CHECK(service_record_top_medals(&record, medals, counts, 1) == 1);
	service_record_map_title("levels\\test\\carousel\\carousel", text, sizeof(text));
	CHECK(!strcmp(text, "Derelict"));
	service_record_map_title("my_custom_map", text, sizeof(text));
	CHECK(!strcmp(text, "My Custom Map"));
	service_record_weapon_family("weapons/frag grenade/explosion", text, sizeof(text));
	CHECK(!strcmp(text, "frag grenade"));
	service_record_title(text, text, sizeof(text));
	CHECK(!strcmp(text, "Frag Grenade"));
	/* (an empty record has no favorites) */
	service_record_clear(&record);
	CHECK(!service_record_favorite_map(&record)[0] && !service_record_favorite_weapon(&record)[0]);
	CHECK(service_record_top_medals(&record, medals, counts, 4) == 0);
	/* (totals stop at their largest) */
	record.kills = SERVICE_RECORD_MAXIMUM_COUNT - 1;
	played.kills = 50;
	service_record_add_game(&record, &played);
	CHECK(record.kills == SERVICE_RECORD_MAXIMUM_COUNT);
	/* (a full table: a new map counted in the totals only) */
	{
		int index;
		char name[16];

		service_record_clear(&record);
		for (index = 0; index < SERVICE_RECORD_MAPS + 3; index++)
		{
			snprintf(name, sizeof(name), "map%d", index);
			played.map = name;
			service_record_add_game(&record, &played);
		}
		CHECK(record.map_count == SERVICE_RECORD_MAPS && record.games == SERVICE_RECORD_MAPS + 3);
	}
}

static void test_record_text(char const *path)
{
	static struct service_record record, back;
	static char text[SERVICE_RECORD_MAXIMUM_FILE];
	struct service_record_weapon weapons[] = { { "weapons\\plasma rifle\\bolt", 5 } };
	struct service_record_game played;
	int length;

	service_record_clear(&record);
	memset(&played, 0, sizeof(played));
	played.map = "hangemhigh";
	played.won = 1;
	played.kills = 9;
	played.deaths = 1;
	played.seconds = 300;
	played.best_spree = 8;
	played.medals[MEDAL_KILLJOY] = 1;
	played.medals[MEDAL_UNFRIGGENBELIEVABLE] = 2;
	played.weapons = weapons;
	played.weapon_count = 1;
	service_record_add_game(&record, &played);
	length = service_record_format(&record, text, sizeof(text));
	CHECK(length > 0 && !strncmp(text, "service_record 1\n", 17));
	CHECK(strstr(text, "\nweapon 5 plasma rifle\n") != NULL);
	CHECK(service_record_parse(&back, text) && !memcmp(&record, &back, sizeof(record)));
	/* (a later build's lines are skipped; a value out of range clamped) */
	CHECK(service_record_parse(&back,
		"service_record 2\r\ngames 3\r\nfuture_thing 9 x\r\nkills -5\r\nmedal 4 not_a_medal\r\nmedal 2 killjoy\r\nwins\n"));
	CHECK(back.games == 3 && back.kills == 0 && back.medals[MEDAL_KILLJOY] == 2 && back.wins == 0);
	/* (anything else is no record) */
	CHECK(!service_record_parse(&back, "games 3\n") && back.games == 0);
	CHECK(!service_record_parse(&back, ""));
	/* (too small: -1) */
	CHECK(service_record_format(&record, text, 20) == -1);
	if (path)
	{
		CHECK(service_record_save(path, &record));
		CHECK(service_record_load(path, &back) && !memcmp(&record, &back, sizeof(record)));
	}
	/* (no file yet: an empty record) */
	CHECK(service_record_load("no such folder/service_record.txt", &back) && back.games == 0);
}

int main(int argc, char **argv)
{
	test_names();
	test_multikills();
	test_sprees();
	test_weapon_sprees();
	test_kinds_first_strike_revenge();
	test_record_math();
	test_record_text(argc > 1 ? argv[1] : NULL);
	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("medal rules and service record: ok\n");
	return 0;
}
