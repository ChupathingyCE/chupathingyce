/*
SOCIAL_TEST.C

Tests of the Guide's data (port/linux/src/social.c): its rows and their
cache, the polling schedule and presence's, and the site's friends (read
by delta_profile.c). Built and run by tools/test_social.py with
discord_activity.c and delta_profile.c; exits nonzero on a failure.
*/

#include "../src/social.h"
#include "../src/discord_activity.h"
#include "../src/delta_profile.h"

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

static struct social_row make_row(const char *tag, int state, int key)
{
	struct social_row row;

	memset(&row, 0, sizeof(row));
	social_copy_text(row.tag, sizeof(row.tag), tag);
	row.state = state;
	row.key = key;
	return row;
}

static void test_sections(void)
{
	CHECK(!strcmp(social_section_name(SOCIAL_SECTION_FRIENDS), "FRIENDS"));
	CHECK(!strcmp(social_section_name(SOCIAL_SECTION_PLAYERS_HERE), "PLAYERS HERE"));
	CHECK(!strcmp(social_section_name(SOCIAL_SECTION_SETTINGS), "SETTINGS"));
	CHECK(!strcmp(social_section_name(-1), ""));
	CHECK(!strcmp(social_section_name(NUMBER_OF_SOCIAL_SECTIONS), ""));
}

static void test_copy_text(void)
{
	char small[6];

	social_copy_text(small, sizeof(small), "Grif");
	CHECK(!strcmp(small, "Grif"));
	social_copy_text(small, sizeof(small), "Simmons");
	CHECK(!strcmp(small, "Simmo"));
	/* (a cut never splits a character: "Caboé" is 6 bytes) */
	social_copy_text(small, sizeof(small), "Cabo\xC3\xA9");
	CHECK(!strcmp(small, "Cabo"));
	social_copy_text(small, sizeof(small), NULL);
	CHECK(!strcmp(small, ""));
}

static void test_list(void)
{
	static struct social_list list, cache;
	struct social_row row;
	int index;

	memset(&cache, 0, sizeof(cache));
	social_list_clear(&list);
	row = make_row("tucker", SOCIAL_STATE_AWAY, 4);
	CHECK(social_list_add(&list, &row) == 0);
	row = make_row("Grif", SOCIAL_STATE_IN_GAME, 2);
	CHECK(social_list_add(&list, &row) == 1);
	row = make_row("Church", SOCIAL_STATE_OFFLINE, 1);
	social_list_add(&list, &row);
	row = make_row("Donut", SOCIAL_STATE_ONLINE, 3);
	social_list_add(&list, &row);
	row = make_row("caboose", SOCIAL_STATE_ONLINE, 5);
	social_list_add(&list, &row);
	social_list_sort(&list);
	CHECK(!strcmp(list.rows[0].tag, "Grif"));
	CHECK(!strcmp(list.rows[1].tag, "caboose"));
	CHECK(!strcmp(list.rows[2].tag, "Donut"));
	CHECK(!strcmp(list.rows[3].tag, "tucker"));
	CHECK(!strcmp(list.rows[4].tag, "Church"));
	CHECK(social_list_find(&list, 3) == 2);
	CHECK(social_list_find(&list, 9) == -1);

	CHECK(social_list_commit(&cache, &list) == 1);
	CHECK(cache.serial == 1);
	/* (the same rows again: no change, even from a row with leftovers past
	its texts' ends) */
	social_list_clear(&list);
	for (index = 0; index < cache.count; index++)
	{
		row = cache.rows[index];
		memset(row.status + 1, 'x', sizeof(row.status) - 1);
		row.status[0] = 0;
		social_list_add(&list, &row);
	}
	CHECK(social_list_commit(&cache, &list) == 0);
	CHECK(cache.serial == 1);
	list.rows[0].muted = 1;
	CHECK(social_list_commit(&cache, &list) == 1);
	CHECK(cache.serial == 2 && cache.rows[0].muted);

	/* full */
	social_list_clear(&list);
	for (index = 0; index < SOCIAL_MAXIMUM_ROWS; index++)
	{
		row = make_row("Lopez", SOCIAL_STATE_ONLINE, index);
		CHECK(social_list_add(&list, &row) == index);
	}
	CHECK(social_list_add(&list, &row) == -1);
	CHECK(list.count == SOCIAL_MAXIMUM_ROWS);
}

static void test_status(void)
{
	struct discord_activity_game game;
	char text[SOCIAL_STATUS_SIZE];

	memset(&game, 0, sizeof(game));
	game.screen = DISCORD_SCREEN_MENUS;
	CHECK(social_own_status(&game, text, sizeof(text)) > 0);
	CHECK(!strcmp(text, "Online \xC2\xB7 In the menus"));

	game.screen = DISCORD_SCREEN_MULTIPLAYER;
	snprintf(game.map, sizeof(game.map), "bloodgulch");
	game.mode = DISCORD_MODE_SLAYER;
	game.player_count = 11;
	game.maximum_player_count = 16;
	CHECK(discord_activity_status_line(&game, text, sizeof(text)) > 0);
	CHECK(!strcmp(text, "Slayer \xC2\xB7 Blood Gulch \xC2\xB7 11/16"));
	game.teams = 1;
	discord_activity_status_line(&game, text, sizeof(text));
	CHECK(!strcmp(text, "Team Slayer \xC2\xB7 Blood Gulch \xC2\xB7 11/16"));
	/* (no network game: no count) */
	game.player_count = 0;
	discord_activity_status_line(&game, text, sizeof(text));
	CHECK(!strcmp(text, "Team Slayer \xC2\xB7 Blood Gulch"));

	memset(&game, 0, sizeof(game));
	game.screen = DISCORD_SCREEN_CAMPAIGN;
	snprintf(game.map, sizeof(game.map), "levels\\a30\\a30");
	discord_activity_status_line(&game, text, sizeof(text));
	CHECK(!strcmp(text, "Campaign \xC2\xB7 Halo"));

	/* too small: "Online" alone, never a cut line */
	CHECK(social_own_status(&game, text, 12) == -1);
	CHECK(!strcmp(text, "Online"));
	CHECK(social_own_status(NULL, text, sizeof(text)) == 6);
}

static void test_poll(void)
{
	struct social_poll poll;

	memset(&poll, 0, sizeof(poll));
	CHECK(!social_poll_due(&poll, 1000, 0, 0));
	CHECK(social_poll_due(&poll, 1000, 0, 1));
	CHECK(!social_poll_due(&poll, 1000 + SOCIAL_POLL_MENUS - 1, 0, 1));
	CHECK(social_poll_due(&poll, 1000 + SOCIAL_POLL_MENUS, 0, 1));
	CHECK(!social_poll_due(&poll, 1000 + SOCIAL_POLL_MENUS * 2, 1, 1));
	CHECK(social_poll_due(&poll, 1000 + SOCIAL_POLL_MENUS + SOCIAL_POLL_GAME, 1, 1));
	/* (the clock wraps) */
	memset(&poll, 0, sizeof(poll));
	CHECK(social_poll_due(&poll, (unsigned long)-5000, 0, 1));
	CHECK(!social_poll_due(&poll, 1000, 0, 1));
	CHECK(social_poll_due(&poll, SOCIAL_POLL_MENUS, 0, 1));

	/* too many: five minutes; then back to the interval once answered */
	memset(&poll, 0, sizeof(poll));
	CHECK(social_poll_due(&poll, 1000, 0, 1));
	social_poll_answer(&poll, 429, 0);
	CHECK(!social_poll_due(&poll, 1000 + SOCIAL_POLL_MENUS, 0, 1));
	CHECK(!social_poll_due(&poll, 1000 + SOCIAL_POLL_BUSY_WAIT - 1, 0, 1));
	CHECK(social_poll_due(&poll, 1000 + SOCIAL_POLL_BUSY_WAIT, 0, 1));
	social_poll_answer(&poll, 200, 1);
	CHECK(social_poll_due(&poll, 1000 + SOCIAL_POLL_BUSY_WAIT + SOCIAL_POLL_MENUS, 0, 1));
	/* no answer: the wait doubles, at most five minutes */
	memset(&poll, 0, sizeof(poll));
	CHECK(social_poll_due(&poll, 0, 0, 1));
	social_poll_answer(&poll, 0, 0);
	CHECK(poll.wait == 2 * SOCIAL_POLL_MENUS);
	social_poll_answer(&poll, 503, 0);
	CHECK(poll.wait == 4 * SOCIAL_POLL_MENUS);
	social_poll_answer(&poll, 200, 0);
	social_poll_answer(&poll, 0, 0);
	social_poll_answer(&poll, 0, 0);
	CHECK(poll.wait == SOCIAL_POLL_FAILED_WAIT);
	/* sooner after an action, but not while the site asked to wait */
	memset(&poll, 0, sizeof(poll));
	CHECK(social_poll_due(&poll, 1000, 0, 1));
	social_poll_soon(&poll);
	CHECK(!social_poll_due(&poll, 1000 + SOCIAL_POLL_SOONEST - 1, 0, 1));
	CHECK(social_poll_due(&poll, 1000 + SOCIAL_POLL_SOONEST, 0, 1));
	CHECK(!social_poll_due(&poll, 1000 + 2 * SOCIAL_POLL_SOONEST, 0, 1));
	social_poll_answer(&poll, 429, 0);
	social_poll_soon(&poll);
	CHECK(!social_poll_due(&poll, 1000 + 3 * SOCIAL_POLL_SOONEST, 0, 1));
	/* signed out: all forgotten */
	CHECK(!social_poll_due(&poll, 2000, 0, 0));
	CHECK(poll.wait == 0 && social_poll_due(&poll, 2001, 0, 1));
}

static const char state_text[] =
	"{\"you\": {\"handle\": \"sarge\", \"display\": \"Sarge\", \"online\": true, \"appear_offline\": false,"
	" \"show_presence\": true, \"friend_requests\": \"everyone\", \"who_can_join\": \"friends\"},"
	" \"friends\": [{\"handle\": \"grif\", \"display\": \"Grif\", \"since\": 1761000000, \"online\": true,"
	" \"playing\": true, \"joinable\": true, \"game\": {\"name\": \"[C] Red vs Blue\", \"map\": \"bloodgulch\","
	" \"engine\": 2, \"mode\": \"\", \"players\": 11, \"maximum_players\": 16, \"teams\": 0, \"platform\": 1,"
	" \"protocol\": 2, \"version\": 41, \"build\": \"0.9.0\"}},"
	" {\"handle\": \"simmons\", \"display\": \"\", \"since\": 1761000001, \"online\": true, \"playing\": true,"
	" \"joinable\": false, \"game\": null},"
	" {\"handle\": \"church\", \"display\": \"Church\\u00e9\", \"since\": 1, \"online\": false, \"playing\": false,"
	" \"joinable\": false, \"game\": null}],"
	" \"more\": 0, \"requests\": {\"incoming\": [{\"handle\": \"donut\", \"display\": \"Donut\", \"time\": 5}],"
	" \"outgoing\": [{\"handle\": \"lopez\", \"display\": \"Lopez\", \"time\": 6}]},"
	" \"invites\": [{\"id\": \"0a1b2c3d4e5f6a7b\", \"from\": \"grif\", \"display\": \"Grif\", \"time\": 7,"
	" \"expires\": 307, \"game\": {\"name\": \"x\", \"map\": \"a10\", \"engine\": 0, \"mode\": \"coop\","
	" \"players\": 2, \"maximum_players\": 4, \"teams\": 0}}],"
	" \"blocked\": [\"tex\"], \"counts\": {\"friends\": 3, \"online\": 2, \"requests\": 1, \"invites\": 1},"
	" \"cursor\": \"89ab01cd23ef4567\", \"same\": false}";

static struct social_state state, other;

static void test_state(void)
{
	char text[SOCIAL_STATUS_SIZE], reason[32], link[160];
	struct social_news news;

	CHECK(delta_profile_parse_social(state_text, &state) == 1);
	CHECK(!strcmp(state.cursor, "89ab01cd23ef4567"));
	CHECK(!strcmp(state.handle, "sarge") && !strcmp(state.display, "Sarge") && !state.appear_offline);
	CHECK(state.friend_count == 3);
	CHECK(!strcmp(state.friends[0].handle, "grif") && state.friends[0].joinable && state.friends[0].game.listed);
	CHECK(state.friends[0].game.engine == 2 && state.friends[0].game.players == 11);
	CHECK(!strcmp(state.friends[2].display, "Church\xC3\xA9"));
	CHECK(!state.friends[1].game.listed && state.friends[1].playing);
	CHECK(state.request_count == 1 && !strcmp(state.requests[0].handle, "donut"));
	CHECK(state.invite_count == 1 && !strcmp(state.invites[0].id, "0a1b2c3d4e5f6a7b") && state.invites[0].game.coop);
	CHECK(social_online_count(&state) == 2);
	CHECK(!strcmp(social_name(state.friends[1].display, state.friends[1].handle), "simmons"));

	/* the status lines */
	social_friend_status(&state.friends[0], text, sizeof(text));
	CHECK(!strcmp(text, "Slayer \xC2\xB7 Blood Gulch \xC2\xB7 11/16"));
	social_friend_status(&state.friends[1], text, sizeof(text));
	CHECK(!strcmp(text, "In a game"));
	social_friend_status(&state.friends[2], text, sizeof(text));
	CHECK(!strcmp(text, "Offline"));
	CHECK(social_game_line(&state.invites[0].game, text, sizeof(text)) > 0);
	CHECK(!strcmp(text, "Co-op \xC2\xB7 The Pillar of Autumn"));

	/* nothing changed: the cursor alone */
	CHECK(delta_profile_parse_social("{\"cursor\": \"89ab01cd23ef4567\", \"same\": true}", &other) == 2);
	CHECK(!strcmp(other.cursor, "89ab01cd23ef4567"));
	CHECK(delta_profile_parse_social("{\"error\": \"too many requests\"}", &other) == 0);
	CHECK(delta_profile_parse_social("not json", &other) == 0);
	CHECK(delta_profile_parse_social("{\"cursor\": \"1\", \"friends\": [{\"handle\": ", &other) == 0);

	/* what's new: an invite, a request, a friend online */
	memset(&other, 0, sizeof(other));
	social_news(&other, &state, &news);
	CHECK(news.invite == 0 && news.request == 0 && news.online == -1);
	social_news(&state, &state, &news);
	CHECK(news.invite == -1 && news.request == -1 && news.online == -1);
	other = state;
	other.friends[0].online = 0;
	social_news(&other, &state, &news);
	CHECK(news.online == 0);

	/* the requests' fields */
	CHECK(social_state_fields(text, sizeof(text), "89ab01cd23ef4567", 200) > 0);
	CHECK(!strcmp(text, "\"since\": \"89ab01cd23ef4567\", \"limit\": 200"));
	CHECK(social_state_fields(text, sizeof(text), "", 200) > 0 && !strcmp(text, "\"limit\": 200"));
	CHECK(social_state_fields(text, sizeof(text), "\"}", 200) == -1);
	CHECK(social_action_fields(text, sizeof(text), "accept", "donut") > 0);
	CHECK(!strcmp(text, "\"action\": \"accept\", \"handle\": \"donut\""));
	CHECK(social_action_fields(text, sizeof(text), "request", "donut") == -1);
	CHECK(social_action_fields(text, sizeof(text), "block", "Donut") == -1);
	CHECK(social_action_fields(text, sizeof(text), "block", "do\"nut") == -1);
	CHECK(social_join_fields(text, sizeof(text), "grif") > 0 && !strcmp(text, "\"friend\": \"grif\""));
	CHECK(social_invite_fields(text, sizeof(text), "grif", "") > 0 && !strcmp(text, "\"to\": \"grif\""));
	CHECK(social_invite_fields(text, sizeof(text), "grif", "0123456789abcdef0123456789abcdef0123456789ab") > 0);
	CHECK(social_invite_fields(text, sizeof(text), "grif", "192.168.1.1:2302") == -1);

	/* the site's no */
	delta_profile_parse_text("{\"error\": \"Their game is full.\", \"reason\": \"full\"}", "reason", reason,
		sizeof(reason));
	CHECK(!strcmp(reason, "full"));
	CHECK(!strcmp(social_join_refusal(reason), "Their game is full."));
	CHECK(!strcmp(social_join_refusal("something new"), "Can't join right now."));
	CHECK(!strcmp(social_invite_refusal("", 403), "Friends only."));
	CHECK(delta_profile_parse_join("{\"join\": \"halo://join/0123abcd\", \"friend\": \"grif\"}", link, sizeof(link)));
	CHECK(!strcmp(link, "halo://join/0123abcd"));
	CHECK(!delta_profile_parse_join("{\"join\": \"http://example.com\"}", link, sizeof(link)) && !link[0]);
}

static void test_presence(void)
{
	struct social_presence presence;
	struct social_presence_inputs inputs = { 1, 1, 1, 1, 0, 1 };
	struct social_presence_inputs off;
	char fields[96];
	unsigned long now = 5000;

	social_presence_reset(&presence);
	/* each input on its own stops it */
	off = inputs; off.share = 0;
	CHECK(!social_presence_due(&presence, &off, now));
	off = inputs; off.in_game = 0;
	CHECK(!social_presence_due(&presence, &off, now));
	off = inputs; off.on_delta = 0;
	CHECK(!social_presence_due(&presence, &off, now));
	off = inputs; off.linked = 0;
	CHECK(!social_presence_due(&presence, &off, now));
	off = inputs; off.hidden = 1;
	CHECK(!social_presence_due(&presence, &off, now));
	/* (no invite: the menus, or a game the site can't name) */
	off = inputs; off.has_invite = 0;
	CHECK(!social_presence_due(&presence, &off, now));
	CHECK(presence.sent == 0);

	/* the first at once, then once a minute */
	CHECK(social_presence_due(&presence, &inputs, now));
	CHECK(!social_presence_due(&presence, &inputs, now + 1));
	CHECK(social_presence_answer(&presence, 200, "ok\n", now + 100) == SOCIAL_PRESENCE_OK);
	CHECK(!social_presence_due(&presence, &inputs, now + 100 + SOCIAL_PRESENCE_INTERVAL - 1));
	CHECK(social_presence_due(&presence, &inputs, now + 100 + SOCIAL_PRESENCE_INTERVAL));
	now += 100 + SOCIAL_PRESENCE_INTERVAL;

	/* "off": half an hour */
	CHECK(social_presence_answer(&presence, 200, "off\n", now) == SOCIAL_PRESENCE_OFF);
	CHECK(!social_presence_due(&presence, &inputs, now + SOCIAL_PRESENCE_OFF_WAIT - 1));
	CHECK(social_presence_due(&presence, &inputs, now + SOCIAL_PRESENCE_OFF_WAIT));
	now += SOCIAL_PRESENCE_OFF_WAIT;

	/* too many: ten minutes; refused: half an hour; no answer: a minute */
	CHECK(social_presence_answer(&presence, 429, "too many requests\n", now) == SOCIAL_PRESENCE_BUSY);
	CHECK(!social_presence_due(&presence, &inputs, now + SOCIAL_PRESENCE_BUSY_WAIT - 1));
	CHECK(social_presence_due(&presence, &inputs, now + SOCIAL_PRESENCE_BUSY_WAIT));
	now += SOCIAL_PRESENCE_BUSY_WAIT;
	CHECK(social_presence_answer(&presence, 400, "bad request\n", now) == SOCIAL_PRESENCE_REFUSED);
	CHECK(!social_presence_due(&presence, &inputs, now + SOCIAL_PRESENCE_INTERVAL));
	CHECK(social_presence_due(&presence, &inputs, now + SOCIAL_PRESENCE_OFF_WAIT));
	now += SOCIAL_PRESENCE_OFF_WAIT;
	CHECK(social_presence_answer(&presence, 0, "", now) == SOCIAL_PRESENCE_NO_ANSWER);
	CHECK(social_presence_due(&presence, &inputs, now + SOCIAL_PRESENCE_INTERVAL));
	now += SOCIAL_PRESENCE_INTERVAL;

	/* a gap in the game (a map loading) keeps the site's wait: "off" is not
	asked again each round */
	CHECK(social_presence_answer(&presence, 200, "off", now) == SOCIAL_PRESENCE_OFF);
	off = inputs; off.in_game = 0;
	CHECK(!social_presence_due(&presence, &off, now + 1));
	CHECK(!social_presence_due(&presence, &inputs, now + SOCIAL_PRESENCE_INTERVAL * 2));
	CHECK(social_presence_due(&presence, &inputs, now + SOCIAL_PRESENCE_OFF_WAIT));
	now += SOCIAL_PRESENCE_OFF_WAIT;
	/* (an answer that comes during a gap still counts) */
	off = inputs; off.has_invite = 0;
	CHECK(!social_presence_due(&presence, &off, now + 1));
	CHECK(social_presence_answer(&presence, 429, "", now + 2) == SOCIAL_PRESENCE_BUSY);
	CHECK(!social_presence_due(&presence, &inputs, now + 3));
	/* the player's own choices changing starts it over, at once */
	off = inputs; off.share = 0;
	CHECK(!social_presence_due(&presence, &off, now + 4));
	CHECK(social_presence_due(&presence, &inputs, now + 5));

	/* the game's invite, as the site takes it: 64 or 44 lowercase hex */
	CHECK(social_presence_invite_valid("0123456789abcdef0123456789abcdef0123456789ab"));
	CHECK(social_presence_invite_valid("0123456789abcdef0123456789abcdef0123456789ab0123456789abcdef0123"));
	CHECK(!social_presence_invite_valid(""));
	CHECK(!social_presence_invite_valid(NULL));
	CHECK(!social_presence_invite_valid("0123456789ABCDEF0123456789ABCDEF0123456789AB"));
	CHECK(!social_presence_invite_valid("0123456789abcdef0123456789abcdef0123456789ab0"));
	CHECK(!social_presence_invite_valid("halo://join/0123456789abcdef0123456789abcdef0123456789ab"));
	CHECK(social_presence_fields(fields, sizeof(fields), "0123456789abcdef0123456789abcdef0123456789ab") > 0);
	CHECK(!strcmp(fields, "\"invite\": \"0123456789abcdef0123456789abcdef0123456789ab\""));
	CHECK(social_presence_fields(fields, sizeof(fields), "") == -1 && !fields[0]);
	CHECK(social_presence_fields(fields, 20, "0123456789abcdef0123456789abcdef0123456789ab") == -1);
}

int main(void)
{
	test_sections();
	test_copy_text();
	test_list();
	test_status();
	test_poll();
	test_presence();
	test_state();
	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("ok\n");
	return 0;
}
