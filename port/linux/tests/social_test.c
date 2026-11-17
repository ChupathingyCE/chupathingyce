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

#include <limits.h>
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
	CHECK(!strcmp(social_section_name(SOCIAL_SECTION_CLAN), "CLAN"));
	CHECK(!strcmp(social_section_name(SOCIAL_SECTION_PLAYERS_HERE), "PLAYERS HERE"));
	CHECK(!strcmp(social_section_name(SOCIAL_SECTION_SETTINGS), "SETTINGS"));
	CHECK(!strcmp(social_section_name(SOCIAL_SECTION_SERVICE_RECORD), "SERVICE RECORD"));
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

	/* the doorbell: at once (after the gap), while there is room */
	{
		unsigned long now = 100000;
		int polls;

		memset(&poll, 0, sizeof(poll));
		CHECK(social_poll_due(&poll, now, 0, 1));
		social_poll_ring(&poll);
		CHECK(!social_poll_due(&poll, now + SOCIAL_POLL_RING_GAP - 1, 0, 1));
		CHECK(social_poll_due(&poll, now + SOCIAL_POLL_RING_GAP, 0, 1));
		CHECK(!poll.rung);
		/* (a third: the room is full, so the ring waits for the schedule) */
		now += SOCIAL_POLL_RING_GAP;
		social_poll_ring(&poll);
		CHECK(social_poll_due(&poll, now + SOCIAL_POLL_RING_GAP, 0, 1));
		now += SOCIAL_POLL_RING_GAP;
		social_poll_ring(&poll);
		CHECK(!social_poll_due(&poll, now + SOCIAL_POLL_RING_GAP, 0, 1));
		CHECK(poll.rung);
		CHECK(social_poll_due(&poll, now + SOCIAL_POLL_MENUS, 0, 1));
		CHECK(!poll.rung);
		/* (a ring never goes through a wait the site asked for) */
		social_poll_answer(&poll, 429, 0);
		social_poll_ring(&poll);
		CHECK(!poll.rung);
		CHECK(!social_poll_due(&poll, now + SOCIAL_POLL_MENUS + 70000, 0, 1));
		social_poll_answer(&poll, 200, 1);

		/* (in the menus, after a minute of the schedule's own polls, a ring
		still polls early: the schedule never uses up the ring's room) */
		memset(&poll, 0, sizeof(poll));
		now = 500000;
		CHECK(social_poll_due(&poll, now, 0, 1));
		CHECK(social_poll_due(&poll, now + SOCIAL_POLL_MENUS, 0, 1));
		CHECK(social_poll_due(&poll, now + 2 * SOCIAL_POLL_MENUS, 0, 1));
		now += 2 * SOCIAL_POLL_MENUS + 5000;
		CHECK(!social_poll_due(&poll, now, 0, 1));
		social_poll_ring(&poll);
		CHECK(social_poll_due(&poll, now, 0, 1));
		CHECK(!poll.rung);
		/* (and a second, then the room is full) */
		social_poll_ring(&poll);
		CHECK(social_poll_due(&poll, now + SOCIAL_POLL_RING_GAP, 0, 1));
		social_poll_ring(&poll);
		CHECK(!social_poll_due(&poll, now + 2 * SOCIAL_POLL_RING_GAP, 0, 1));
		CHECK(poll.rung);

		/* (rings all the time, in the menus, for five minutes: never more
		than 5 polls in any minute (the site takes 6), and at least the
		schedule's) */
		memset(&poll, 0, sizeof(poll));
		{
			unsigned long times[128];
			int count = 0, index, later;

			for (now = 1000000; now < 1000000 + 5 * 60000; now += 250)
			{
				social_poll_ring(&poll);
				if (social_poll_due(&poll, now, 0, 1) && count < 128)
					times[count++] = now;
			}
			CHECK(count >= 20);
			for (index = 0; index < count; index++)
			{
				polls = 0;
				for (later = index; later < count && times[later] - times[index] < 60000; later++)
					polls++;
				CHECK(polls <= 5);
			}
		}
	}
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
	/* (the requests sent, CANCEL's, and the blocked, UNBLOCK's) */
	CHECK(state.outgoing_count == 1 && !strcmp(state.outgoing[0].handle, "lopez") &&
		!strcmp(state.outgoing[0].display, "Lopez"));
	CHECK(state.blocked_count == 1 && !strcmp(state.blocked[0], "tex"));
	CHECK(social_action_fields(text, sizeof(text), "cancel", "lopez") > 0);
	CHECK(social_action_fields(text, sizeof(text), "unblock", "tex") > 0);
	CHECK(social_action_fields(text, sizeof(text), "dance", "tex") == -1);
	/* (the friends' privacy, from MY PROFILE's PRIVACY) */
	{
		char privacy[160];

		CHECK(social_friends_privacy_fields(privacy, sizeof(privacy), "", "invite", NULL) > 0);
		CHECK(!strcmp(privacy, "\"action\": \"privacy\", \"join\": \"invite\""));
		CHECK(social_friends_privacy_fields(privacy, sizeof(privacy), "friends_of_friends", "", "friends_and_clan") > 0);
		CHECK(!strcmp(privacy, "\"action\": \"privacy\", \"requests\": \"friends_of_friends\", "
			"\"clan_presence\": \"friends_and_clan\""));
		CHECK(social_friends_privacy_fields(privacy, sizeof(privacy), "", "everybody", "") == -1);
		CHECK(social_friends_privacy_fields(privacy, sizeof(privacy), "", "", "") == -1);
	}
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
	/* the doorbell: 32 of 0-9 and a-f, in a whole state or the cursor alone */
	CHECK(delta_profile_parse_social("{\"cursor\": \"1\", \"same\": true,"
		" \"doorbell\": \"0123456789abcdef0123456789abcdef\"}", &other) == 2);
	CHECK(!strcmp(other.doorbell, "0123456789abcdef0123456789abcdef"));
	CHECK(delta_profile_parse_social("{\"cursor\": \"1\", \"same\": true, \"doorbell\": \"doorbell/#\"}", &other) == 2);
	CHECK(!other.doorbell[0]);
	CHECK(delta_profile_parse_social("{\"cursor\": \"1\", \"same\": true,"
		" \"doorbell\": \"0123456789ABCDEF0123456789abcdef\"}", &other) == 2);
	CHECK(!other.doorbell[0]);
	CHECK(!state.doorbell[0]);
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
	CHECK(!strcmp(text, "\"since\": \"89ab01cd23ef4567\", \"limit\": 200, \"clan\": true"));
	CHECK(social_state_fields(text, sizeof(text), "", 200) > 0 && !strcmp(text, "\"limit\": 200, \"clan\": true"));
	CHECK(social_state_fields(text, sizeof(text), "\"}", 200) == -1);
	CHECK(social_action_fields(text, sizeof(text), "accept", "donut") > 0);
	CHECK(!strcmp(text, "\"action\": \"accept\", \"handle\": \"donut\""));
	CHECK(social_action_fields(text, sizeof(text), "request", "donut") > 0);
	CHECK(!strcmp(text, "\"action\": \"request\", \"handle\": \"donut\""));
	CHECK(social_action_fields(text, sizeof(text), "unknown", "donut") == -1);
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
	/* an answer that ends inside an escape is read no further than its end
	(the bytes after it here are another answer's, left in the buffer) */
	{
		static const char cut_pair[] = "{\"reason\": \"\\uD800\\u\0ABC\"}";
		static const char cut_escape[] = "{\"reason\": \"x\\\0\"}";

		delta_profile_parse_text(cut_pair, "reason", reason, sizeof(reason));
		CHECK(!reason[0]);
		delta_profile_parse_text(cut_escape, "reason", reason, sizeof(reason));
		CHECK(!reason[0]);
	}
}

static void test_clan(void)
{
	static const char roster[] = "{\"clan\": {\"id\": 7, \"name\": \"Gray Matter\", \"tag\": \"GM\", \"motto\": \"\", "
		"\"emblem\": {\"fg\": 1, \"bg\": 2, \"c1\": 3, \"c2\": 4, \"c3\": 5}, \"created\": 1, \"rank\": \"officer\", "
		"\"count\": 3, \"hidden\": 0, \"more\": 0, \"members\": [{\"handle\": \"walter\", \"display\": \"Walter\", "
		"\"rank\": \"leader\", \"online\": true, \"playing\": true, \"game\": {\"map\": \"bloodgulch\", \"engine\": 2, "
		"\"teams\": false, \"players\": 5, \"maximum_players\": 16}}, {\"handle\": \"jesse\", \"display\": \"Jesse\", "
		"\"rank\": \"officer\", \"you\": true}, {\"handle\": \"skinny\", \"display\": \"\", \"rank\": \"member\", "
		"\"online\": false, \"playing\": false, \"game\": null}]}, \"clan_invites\": [], "
		"\"clan_invited\": [{\"handle\": \"badger\", \"time\": 5}], \"friends\": [], \"cursor\": \"ab\"}";
	static struct delta_profile profile;
	char line[SOCIAL_STATUS_SIZE];
	struct social_game none;

	memset(&none, 0, sizeof(none));
	memset(&profile, 0, sizeof(profile));
	/* (the roster, from /v1/game/clan or the friends poll) */
	CHECK(delta_profile_parse_clan(roster, &profile) == 1);
	CHECK(profile.has_clan && profile.clan.member_count == 3 && !strcmp(profile.clan.tag, "GM"));
	CHECK(!strcmp(profile.clan.members[0].handle, "walter") && profile.clan.members[0].online &&
		profile.clan.members[0].playing && profile.clan.members[0].game.listed &&
		!strcmp(profile.clan.members[0].game.map, "bloodgulch"));
	CHECK(profile.clan.members[1].you && !profile.clan.members[2].game.listed);
	CHECK(profile.clan_invited_count == 1 && !strcmp(profile.clan_invited[0], "badger"));
	/* (an answer with no clan part leaves the profile's as it was) */
	CHECK(delta_profile_parse_clan("{\"friends\": [], \"cursor\": \"ab\"}", &profile) == 0);
	CHECK(profile.has_clan);
	/* (a member's answer: none out) */
	CHECK(delta_profile_parse_clan("{\"clan\": null, \"clan_invites\": []}", &profile) == 1);
	CHECK(!profile.has_clan && !profile.clan_invited_count);
	/* ranks */
	CHECK(social_clan_rank("leader") == 3 && social_clan_rank("officer") == 2 && social_clan_rank("member") == 1);
	CHECK(social_clan_rank("admin") == 0 && social_clan_rank(NULL) == 0);
	CHECK(social_clan_can_invite("leader") && social_clan_can_invite("officer") && !social_clan_can_invite("member"));
	CHECK(social_clan_can_kick("leader", "officer") && social_clan_can_kick("leader", "member"));
	CHECK(!social_clan_can_kick("leader", "leader") && social_clan_can_kick("officer", "member"));
	CHECK(!social_clan_can_kick("officer", "officer") && !social_clan_can_kick("member", "member"));
	/* lines */
	social_clan_status("officer", 0, 1, 0, &none, line, sizeof(line));
	CHECK(!strcmp(line, "Officer \xC2\xB7 Online"));
	social_clan_status("member", 0, 0, 0, &none, line, sizeof(line));
	CHECK(!strcmp(line, "Member \xC2\xB7 Offline"));
	social_clan_status("leader", 0, 1, 1, &none, line, sizeof(line));
	CHECK(!strcmp(line, "Leader \xC2\xB7 In a game"));
	social_clan_status("leader", 1, 1, 1, &none, line, sizeof(line));
	CHECK(!strcmp(line, "You \xC2\xB7 Leader"));
	CHECK(delta_profile_parse_clan(roster, &profile) == 1);
	social_clan_status("leader", 0, 1, 1, &profile.clan.members[0].game, line, sizeof(line));
	CHECK(!strncmp(line, "Leader \xC2\xB7 Slayer \xC2\xB7 ", 20) && strstr(line, "5/16") != NULL);
	social_clan_status("leader", 0, 1, 1, &profile.clan.members[0].game, line, 12);
	CHECK(strlen(line) < 12);
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

/* the service record's details from the site (an older site sends none) */
static void test_record(void)
{
	static struct delta_profile profile;

	CHECK(delta_profile_parse("{\"linked\": true, \"handle\": \"grif\", \"record\": {\"games\": 4, \"kills\": 30, "
		"\"best_spree\": 7, \"favorite_map\": \"bloodgulch\", \"favorite_weapon\": \"sniper rifle\", "
		"\"medal_counts\": {\"victory\": 2, \"double_kill\": 9, \"revenge\": 0, \"killjoy\": 3, \"rampage\": 1, "
		"\"first_strike\": 5}}}", &profile));
	CHECK(profile.has_record && profile.record_games == 4 && profile.record_best_spree == 7);
	CHECK(!strcmp(profile.record_favorite_map, "bloodgulch") && !strcmp(profile.record_favorite_weapon, "sniper rifle"));
	CHECK(profile.record_top_medal_count == 3);
	CHECK(!strcmp(profile.record_top_medals[0].key, "double_kill") && profile.record_top_medals[0].count == 9);
	CHECK(!strcmp(profile.record_top_medals[1].key, "first_strike") && profile.record_top_medals[2].count == 3);
	CHECK(delta_profile_parse("{\"linked\": true, \"handle\": \"grif\", \"record\": {\"games\": 1, \"medals\": 2}}", &profile));
	CHECK(profile.record_games == 1 && profile.record_top_medal_count == 0 && !profile.record_favorite_map[0]);
	/* (numbers past an int's range, or a double's, are held at the end,
	never cast as they are) */
	CHECK(delta_profile_parse("{\"linked\": true, \"handle\": \"grif\", \"record\": {\"best_spree\": 1e300, "
		"\"medal_counts\": {\"victory\": 4294967297, \"killjoy\": -1e300, \"rampage\": 3}}}", &profile));
	CHECK(profile.record_best_spree == INT_MAX);
	CHECK(profile.record_top_medal_count == 2 && profile.record_top_medals[0].count == INT_MAX &&
		!strcmp(profile.record_top_medals[1].key, "rampage") && profile.record_top_medals[1].count == 3);
}

static const char party_text[] =
	"{\"cursor\": \"0011223344556677\", \"same\": false, \"you\": {\"handle\": \"grif\"}, \"friends\": [],"
	" \"party\": {\"id\": \"0123456789abcdef\", \"code\": \"GQ7RTX\", \"link\": \"halo://party/GQ7RTX\","
	" \"leader\": false, \"who_can_join\": \"friends\", \"largest\": 8, \"tag\": \"00112233aabbccdd\","
	" \"members\": [{\"id\": \"aa11bb22\", \"name\": \"Sarge\", \"handle\": \"sarge\", \"platform\": \"pc_macos\","
	" \"leader\": true, \"you\": false}, {\"id\": \"cc33dd44\", \"name\": \"Lopez\", \"handle\": null,"
	" \"platform\": \"xbox\", \"leader\": false, \"you\": true}],"
	" \"go\": {\"serial\": 3, \"time\": 1700000000, \"game\": {\"name\": \"Red Base\", \"map\": \"bloodgulch\","
	" \"engine\": 1, \"players\": 4, \"maximum_players\": 16, \"teams\": true},"
	" \"join\": \"halo://join/0123456789abcdef0123456789abcdef0123456789ab\"}},"
	" \"party_invites\": [{\"party\": \"fedcba9876543210\", \"from\": \"simmons\", \"display\": \"Simmons\","
	" \"size\": 3, \"time\": 1, \"expires\": 301}]}";

static void test_party(void)
{
	static struct social_state read;
	struct social_follow_inputs inputs;
	char text[192], code[SOCIAL_PARTY_CODE_SIZE];
	unsigned char tag[8];

	CHECK(delta_profile_parse_social(party_text, &read) == 1);
	CHECK(read.party.has && !read.party.leader && read.party.member_count == 2 && read.party.largest == 8);
	CHECK(!strcmp(read.party.code, "GQ7RTX") && !strcmp(read.party.who_can_join, "friends"));
	CHECK(!strcmp(read.party.members[0].handle, "sarge") && read.party.members[0].leader);
	CHECK(!read.party.members[1].handle[0] && read.party.members[1].you && !strcmp(read.party.members[1].platform, "xbox"));
	CHECK(read.party.go.has && read.party.go.serial == 3 && read.party.go.game.listed);
	CHECK(read.party_invite_count == 1 && read.party_invites[0].size == 3);
	CHECK(!strcmp(social_party_leader(&read.party)->name, "Sarge"));
	CHECK(social_party_tag_bytes(read.party.tag, tag) && tag[0] == 0x00 && tag[7] == 0xdd);
	CHECK(!social_party_tag_bytes("00112233aabbccdX", tag) && !social_party_tag_bytes("0011", tag));

	/* /v1/party's own answer, and no party */
	CHECK(delta_profile_parse_party("{\"party\": null, \"party_invites\": []}", &read) == 1);
	CHECK(!read.party.has && !read.party_invite_count);
	CHECK(delta_profile_parse_party("{\"error\": \"x\", \"reason\": \"full\"}", &read) == 0);
	CHECK(delta_profile_parse_social(party_text, &read) == 1);
	/* (a go whose join isn't a game link is none) */
	{
		static struct social_state odd;

		CHECK(delta_profile_parse_party("{\"party\": {\"id\": \"0123456789abcdef\", \"members\": [{\"id\": "
			"\"aa11bb22\", \"name\": \"x\", \"leader\": true}], \"go\": {\"serial\": 1, \"join\": \"http://x\"}}}",
			&odd) == 1);
		CHECK(odd.party.has && odd.party.go.has && !odd.party.go.join[0]);
	}

	/* codes */
	CHECK(social_party_code_clean("gq7-rtx", code) && !strcmp(code, "GQ7RTX"));
	CHECK(social_party_code_clean("halo://party/GQ7RTX/", code) && !strcmp(code, "GQ7RTX"));
	CHECK(social_party_code_clean("HALO://PARTY/gq7rtx", code) && !strcmp(code, "GQ7RTX"));
	CHECK(!social_party_code_clean("GQ7RT", code) && !code[0]);
	CHECK(!social_party_code_clean("GQ7RTXX", code));
	CHECK(!social_party_code_clean("GQ0RTX", code));
	CHECK(!social_party_code_clean("halo://party/GQ7RTX/more", code));
	social_party_code_shown("GQ7RTX", text, sizeof(text));
	CHECK(!strcmp(text, "GQ7-RTX"));
	/* the link shared is the site's party page (chat programs make a link of
	it, never of halo://); the page opens the game's own */
	CHECK(social_party_link("gq7rtx", text, sizeof(text)) &&
		!strcmp(text, "https://stats.chupathingy.org/party/GQ7RTX"));
	CHECK(social_party_link("GQ7-RTX", text, sizeof(text)) && !strcmp(text, SOCIAL_PARTY_PAGE "GQ7RTX"));
	CHECK(!social_party_link("GQ0RTX", text, sizeof(text)) && !text[0]);
	CHECK(!social_party_link("gq7rtx", text, 20) && !text[0]);
	CHECK(social_party_app_link("gq7rtx", text, sizeof(text)) && !strcmp(text, "halo://party/GQ7RTX"));
	/* both read back as the code, as do other sites' party pages */
	CHECK(social_party_code_clean("https://stats.chupathingy.org/party/GQ7RTX", code) && !strcmp(code, "GQ7RTX"));
	CHECK(social_party_code_clean("HTTPS://STATS.CHUPATHINGY.ORG/PARTY/gq7rtx/", code) && !strcmp(code, "GQ7RTX"));
	CHECK(social_party_code_clean("http://localhost/party/GQ7-RTX", code) && !strcmp(code, "GQ7RTX"));
	CHECK(!social_party_code_clean("https://evil.example?.chupathingy.org/party/GQ7RTX", code));
	CHECK(!social_party_code_clean("https://stats.chupathingy.org@evil.example/party/GQ7RTX", code));
	CHECK(!social_party_code_clean("https://stats.chupathingy.org/party/GQ7RTX/more", code));
	CHECK(!social_party_code_clean("https://stats.chupathingy.org/players/GQ7RTX", code));
	CHECK(!social_party_code_clean("https:///party/GQ7RTX", code));
	CHECK(!social_party_code_clean("https://a b/party/GQ7RTX", code));
	CHECK(!social_party_code_clean("https://stats.chupathingy.org/party/GQ7RTX?x=1", code));
	CHECK(!strcmp(social_party_rule_shown("invite"), "INVITE ONLY"));
	CHECK(!strcmp(social_party_rule_next("friends", 1), "invite"));
	CHECK(!strcmp(social_party_rule_next("anyone", -1), "invite"));

	/* the requests' fields */
	CHECK(social_party_fields(text, sizeof(text), "create", "", "Lopez", "xbox") > 0);
	CHECK(!strcmp(text, "\"action\": \"create\", \"name\": \"Lopez\", \"platform\": \"xbox\""));
	CHECK(social_party_fields(text, sizeof(text), "join", "gq7-rtx", "", "") > 0);
	CHECK(!strcmp(text, "\"action\": \"join\", \"code\": \"GQ7RTX\""));
	CHECK(social_party_fields(text, sizeof(text), "invite", "grif", "", "") > 0);
	CHECK(social_party_fields(text, sizeof(text), "invite", "Gr\"if", "", "") == -1);
	CHECK(social_party_fields(text, sizeof(text), "kick", "aa11bb22", "", "") > 0);
	CHECK(social_party_fields(text, sizeof(text), "kick", "nope", "", "") == -1);
	CHECK(social_party_fields(text, sizeof(text), "rule", "anyone", "", "") > 0);
	CHECK(social_party_fields(text, sizeof(text), "rule", "all", "", "") == -1);
	CHECK(social_party_fields(text, sizeof(text), "go", "0123456789abcdef0123456789abcdef0123456789ab", "", "") > 0);
	CHECK(social_party_fields(text, sizeof(text), "go", "192.168.1.1", "", "") == -1);
	CHECK(social_party_fields(text, sizeof(text), "dance", "", "", "") == -1);
	/* (a quote in a game name is left out, not a reason to send nothing) */
	CHECK(social_party_fields(text, sizeof(text), "create", "", "Lo\"pe\\z", "") > 0);
	CHECK(!strcmp(text, "\"action\": \"create\", \"name\": \"Lopez\""));
	/* (NEW PARTY CODE's) */
	CHECK(social_party_fields(text, sizeof(text), "code", "", "", "") > 0);
	CHECK(!strcmp(text, "\"action\": \"code\""));
	CHECK(!strcmp(social_party_action_refusal("invite", "no_party", 404), "Start a party first."));
	CHECK(!strcmp(social_party_action_refusal("invite", "member", 409), "They're in your party already."));
	CHECK(!strcmp(social_party_action_refusal("invite", "", 429), "Too many invites. Wait a minute."));
	CHECK(!strcmp(social_party_action_refusal("join", "friends_only", 403), "Only the leader's friends can join."));
	CHECK(!strcmp(social_party_refusal("friends_only", 403), "Only the leader's friends can join."));
	CHECK(!strcmp(social_party_refusal("", 500), "Can't do that right now."));

	/* following */
	memset(&inputs, 0, sizeof(inputs));
	inputs.current_invite = "";
	CHECK(social_follow_decide(&read.party, &inputs) == SOCIAL_FOLLOW_OFFER);
	social_follow_line(&read.party, text, sizeof(text));
	CHECK(!strcmp(text, "Following Sarge to Blood Gulch"));
	inputs.seen_serial = 3;
	CHECK(social_follow_decide(&read.party, &inputs) == SOCIAL_FOLLOW_NONE);
	inputs.seen_serial = 2;
	inputs.current_invite = "halo://join/0123456789abcdef0123456789abcdef0123456789ab";
	CHECK(social_follow_decide(&read.party, &inputs) == SOCIAL_FOLLOW_THERE);
	inputs.current_invite = "";
	inputs.hosting_others = 1;
	CHECK(social_follow_decide(&read.party, &inputs) == SOCIAL_FOLLOW_CANT);
	social_follow_reason(&read.party, &inputs, text, sizeof(text));
	CHECK(!strcmp(text, "Can't follow: you're hosting a game."));
	inputs.hosting_others = 0;
	inputs.mode = social_follow_mode("never");
	CHECK(social_follow_decide(&read.party, &inputs) == SOCIAL_FOLLOW_NONE);
	inputs.mode = social_follow_mode("always");
	CHECK(social_follow_decide(&read.party, &inputs) == SOCIAL_FOLLOW_OFFER);
	inputs.mode = social_follow_mode("");
	CHECK(inputs.mode == SOCIAL_FOLLOW_ASK && social_follow_mode("ask") == SOCIAL_FOLLOW_ASK &&
		social_follow_mode("Always") == SOCIAL_FOLLOW_ASK);
	/* (the countdown ending: joins in the menus, stays in a game unless
	"always", never with "never") */
	CHECK(social_follow_timeout_joins(SOCIAL_FOLLOW_ASK, 0) && !social_follow_timeout_joins(SOCIAL_FOLLOW_ASK, 1));
	CHECK(social_follow_timeout_joins(SOCIAL_FOLLOW_ALWAYS, 0) && social_follow_timeout_joins(SOCIAL_FOLLOW_ALWAYS, 1));
	CHECK(!social_follow_timeout_joins(SOCIAL_FOLLOW_NEVER, 0) && !social_follow_timeout_joins(SOCIAL_FOLLOW_NEVER, 1));
	CHECK(!strcmp(social_follow_mode_next("ask"), "always") && !strcmp(social_follow_mode_next("always"), "never") &&
		!strcmp(social_follow_mode_next("never"), "ask") && !strcmp(social_follow_mode_next("junk"), "always"));
	read.party.go.join[0] = 0;
	social_copy_text(read.party.go.error, sizeof(read.party.go.error), "Can't follow: the game is full.");
	CHECK(social_follow_decide(&read.party, &inputs) == SOCIAL_FOLLOW_CANT);
	social_follow_reason(&read.party, &inputs, text, sizeof(text));
	CHECK(!strcmp(text, "Can't follow: the game is full."));
	/* (the leader never follows) */
	read.party.leader = 1;
	CHECK(social_follow_decide(&read.party, &inputs) == SOCIAL_FOLLOW_NONE);
}

static struct social_list_key list_key(int kind, int index, const char *id)
{
	struct social_list_key key;

	memset(&key, 0, sizeof(key));
	key.kind = kind;
	key.index = index;
	social_copy_text(key.id, sizeof(key.id), id);
	return key;
}

/* PARTY's chosen row kept on its item as the list changes (guide.c): 0 a
member, 1 VOICE, 2 a button (2 MAKE LEADER, 3 KICK) */
static void test_list_keep(void)
{
	struct social_list_key before[4], after[5], kick = list_key(2, 3, "");
	int changed;

	/* (the leader on a member: two members, then MAKE LEADER and KICK) */
	before[0] = list_key(0, 0, "m1");
	before[1] = list_key(0, 1, "m2");
	before[2] = list_key(2, 2, "");
	before[3] = list_key(2, 3, "");
	CHECK(social_list_keep(&kick, 3, before, 4, 3, &changed) == 3 && !changed);
	/* (a member joins above: KICK moves down a row and stays chosen, still
	armed) */
	after[0] = before[0];
	after[1] = before[1];
	after[2] = list_key(0, 2, "m3");
	after[3] = before[2];
	after[4] = before[3];
	CHECK(social_list_keep(&kick, 3, after, 5, 3, &changed) == 4 && !changed);
	/* (one member out and one in from the four as remembered, m1 gone and m3
	there: KICK still on row 3, where it was) */
	CHECK(social_list_keep(&kick, 3, after + 1, 4, 3, &changed) == 3 && !changed);
	/* (from the five, m1 leaves: KICK up a row, from 4 to 3, never MAKE
	LEADER under the cursor) */
	CHECK(social_list_keep(&kick, 4, after + 1, 4, 4, &changed) == 3 && !changed);
	after[1] = before[2];
	after[2] = before[3];
	CHECK(social_list_keep(&kick, 3, after, 3, 3, &changed) == 2 && !changed);
	/* (KICK gone: the row stays, clamped, and what was armed is dropped) */
	after[0] = list_key(0, 0, "m1");
	after[1] = list_key(1, 0, "");
	after[2] = list_key(2, 0, "");
	CHECK(social_list_keep(&kick, 3, after, 3, 3, &changed) == 2 && changed);
	/* (a member found by ID, not by number) */
	{
		struct social_list_key member = list_key(0, 1, "m2"), moved = list_key(0, 0, "m2");

		after[0] = list_key(0, 0, "m1");
		after[1] = list_key(0, 1, "m3");
		CHECK(social_list_keep(&member, 1, after, 2, 1, &changed) == 1 && changed);
		after[0] = moved;
		CHECK(social_list_keep(&moved, 0, after, 2, 0, &changed) == 0 && !changed);
	}
	/* (as guide.c keys them, a member's number zeroed and only its ID kept:
	m1 leaves, m2 moves from row 1 to row 0, and the cursor on m2 follows it,
	nothing changed under it) */
	{
		struct social_list_key m2 = list_key(0, 0, "m2"), now[3];

		now[0] = list_key(0, 0, "m2");
		now[1] = list_key(2, 2, "");
		now[2] = list_key(2, 3, "");
		CHECK(social_list_keep(&m2, 1, now, 3, 1, &changed) == 0 && !changed);
		/* (and m2 gone too: the row clamped onto MAKE LEADER, changed) */
		CHECK(social_list_keep(&m2, 1, now + 1, 2, 1, &changed) == 1 && changed);
	}
	/* (the row moved since: that row, and changed when its item differs) */
	CHECK(social_list_keep(&kick, 3, before, 4, 2, &changed) == 2 && changed);
	CHECK(social_list_keep(&kick, 2, before, 4, 3, &changed) == 3 && !changed);
	/* (nothing remembered yet, and an empty list) */
	CHECK(social_list_keep(NULL, 0, before, 4, 9, &changed) == 3 && !changed);
	CHECK(social_list_keep(&kick, 3, before, 0, 3, &changed) == 0 && changed);
}

/* a tap on PARTY's row as last drawn (guide.c): it lands only on the item
drawn there. 0 a member, 1 VOICE, 2 a button (2 COPY PARTY LINK, 3 LEAVE) */
static void test_list_tap(void)
{
	struct social_list_key drawn[4], now[4];

	drawn[0] = list_key(0, 0, "m1");
	drawn[1] = list_key(2, 2, "");
	drawn[2] = list_key(2, 3, "");
	CHECK(social_list_same_item(drawn, 3, drawn, 3, 1));
	/* (VOICE shows above the buttons since: the tap on COPY PARTY LINK's
	row would land on VOICE, and LEAVE's on COPY PARTY LINK) */
	now[0] = drawn[0];
	now[1] = list_key(1, 0, "");
	now[2] = drawn[1];
	now[3] = drawn[2];
	CHECK(!social_list_same_item(drawn, 3, now, 4, 1));
	CHECK(!social_list_same_item(drawn, 3, now, 4, 2));
	CHECK(social_list_same_item(drawn, 3, now, 4, 0));
	/* (and gone again: the tap drawn on VOICE lands on a button) */
	CHECK(!social_list_same_item(now, 4, drawn, 3, 1));
	/* (a row past the list now, or past the one drawn) */
	CHECK(!social_list_same_item(now, 4, drawn, 3, 3));
	CHECK(!social_list_same_item(drawn, 3, now, 4, 3));
	CHECK(!social_list_same_item(drawn, 3, drawn, 3, -1));
}

/* RECENT: the site's list, this run's verified players first */
static void test_recent(void)
{
	static struct social_state state;
	struct social_recent_seen seen;
	struct social_recent out[SOCIAL_MAXIMUM_RECENT];
	char text[SOCIAL_STATUS_SIZE];
	int count, index;
	static const char recent_text[] =
		"{\"cursor\": \"aa\", \"you\": {\"handle\": \"sarge\", \"display\": \"Sarge\"}, "
		"\"friends\": [{\"handle\": \"grif\", \"display\": \"Grif\", \"online\": true}], "
		"\"recent\": [{\"handle\": \"caboose\", \"display\": \"Caboose\", \"time\": 1000, \"map\": \"bloodgulch\", "
		"\"friend\": false, \"requested\": true}, "
		"{\"handle\": \"Not A Handle\", \"display\": \"x\", \"time\": 900}, "
		"{\"handle\": \"grif\", \"display\": \"Grif\", \"time\": 800, \"map\": \"hangemhigh\", \"friend\": true, "
		"\"requested\": false, \"later\": [1, {\"a\": 2}]}]}";

	CHECK(delta_profile_parse_social(recent_text, &state) == 1);
	CHECK(state.recent_count == 2);
	CHECK(!strcmp(state.recent[0].handle, "caboose") && state.recent[0].requested && !state.recent[0].is_friend);
	CHECK(state.recent[0].time == 1000 && !strcmp(state.recent[0].map, "bloodgulch"));
	CHECK(!strcmp(state.recent[1].handle, "grif") && state.recent[1].is_friend);

	/* this run's: newest first, once each, never this player */
	memset(&seen, 0, sizeof(seen));
	social_recent_note(&seen, "donut", "", "dangercanyon", 5000);
	social_recent_note(&seen, "caboose", "Caboose", "dangercanyon", 5000);
	social_recent_note(&seen, "sarge", "Sarge", "dangercanyon", 5000);
	social_recent_note(&seen, "donut", "Donut", "dangercanyon", 5100);
	social_recent_note(&seen, "Bad Name", "", "", 5100);
	CHECK(seen.count == 3 && !strcmp(seen.players[0].handle, "donut") && !strcmp(seen.players[0].display, "Donut"));
	count = social_recent_merge(&state, &seen, "sarge", out, SOCIAL_MAXIMUM_RECENT);
	CHECK(count == 3);
	CHECK(!strcmp(out[0].handle, "donut") && out[0].here && !out[0].is_friend);
	/* (caboose: this run's, with the site's request waiting) */
	CHECK(!strcmp(out[1].handle, "caboose") && out[1].here && out[1].requested && out[1].time == 5000);
	CHECK(!strcmp(out[2].handle, "grif") && !out[2].here && out[2].is_friend);
	CHECK(social_recent_merge(&state, &seen, "sarge", out, 1) == 1);

	/* a block lets them go; the list keeps the newest */
	social_recent_forget(&seen, "donut");
	CHECK(seen.count == 2 && !strcmp(seen.players[0].handle, "sarge"));
	for (index = 0; index < SOCIAL_MAXIMUM_RECENT + 4; index++)
	{
		char handle[16];

		snprintf(handle, sizeof(handle), "player%d", index);
		social_recent_note(&seen, handle, "", "", index);
	}
	CHECK(seen.count == SOCIAL_MAXIMUM_RECENT && !strcmp(seen.players[0].handle, "player27"));

	/* the line */
	social_recent_status(&state.recent[0], 1000 + 2 * 3600, text, sizeof(text));
	CHECK(!strcmp(text, "Blood Gulch \xC2\xB7 2 hours ago"));
	social_recent_status(&state.recent[0], 1000 + 86400, text, sizeof(text));
	CHECK(!strcmp(text, "Blood Gulch \xC2\xB7 1 day ago"));
	social_recent_status(&state.recent[0], 1060, text, sizeof(text));
	CHECK(!strcmp(text, "Blood Gulch \xC2\xB7 Just now"));
	social_recent_status(&state.recent[0], 1000 + 25 * 60, text, sizeof(text));
	CHECK(!strcmp(text, "Blood Gulch \xC2\xB7 25 minutes ago"));
	state.recent[0].map[0] = 0;
	social_recent_status(&state.recent[0], 1000 + 40 * 86400LL, text, sizeof(text));
	CHECK(!strcmp(text, "A while ago"));
}

/* every action /v1/party takes, with a value of its kind, makes fields (a
table, so DELTA's actions and the request builder can't drift apart:
tools/test_social.py checks guide.c's against social_party_actions) */
static void test_party_actions(void)
{
	static const struct
	{
		const char *action;
		const char *value;
	} samples[] =
	{
		{ "create", "" }, { "invite", "grif" }, { "accept", "0123456789abcdef" }, { "decline", "0123456789abcdef" },
		{ "join", "GQ7RTX" }, { "leave", "" }, { "kick", "aa11bb22" }, { "lead", "aa11bb22" }, { "rule", "friends" },
		{ "code", "" }, { "go", "0123456789abcdef0123456789abcdef0123456789ab" },
	};
	char text[320];
	int index, found;
	size_t sample;

	for (index = 0; social_party_actions[index]; index++)
	{
		found = 0;
		for (sample = 0; sample < sizeof(samples) / sizeof(samples[0]); sample++)
		{
			if (strcmp(samples[sample].action, social_party_actions[index]))
				continue;
			found = 1;
			if (social_party_fields(text, sizeof(text), samples[sample].action, samples[sample].value, "Sarge",
				"pc_macos") <= 0)
			{
				printf("party action %s: no fields\n", samples[sample].action);
				failures++;
			}
		}
		if (!found)
		{
			printf("party action %s: no sample\n", social_party_actions[index]);
			failures++;
		}
	}
	CHECK(index == (int)(sizeof(samples) / sizeof(samples[0])));
}

/* the invites sent, and the lines said for them */
static void test_sent(void)
{
	static struct social_sent_list list;
	char text[128];
	int index;

	memset(&list, 0, sizeof(list));
	social_sent_add(&list, "grif", SOCIAL_SENT_PARTY, 1000);
	CHECK(social_sent_has(&list, "grif", SOCIAL_SENT_PARTY, 2000));
	CHECK(!social_sent_has(&list, "grif", SOCIAL_SENT_GAME, 2000));
	CHECK(!social_sent_has(&list, "simmons", SOCIAL_SENT_PARTY, 2000));
	/* (as long as the site keeps it) */
	CHECK(!social_sent_has(&list, "grif", SOCIAL_SENT_PARTY, 1000 + SOCIAL_SENT_LIFETIME));
	/* (again: one entry, its time new) */
	social_sent_add(&list, "grif", SOCIAL_SENT_PARTY, 1000 + SOCIAL_SENT_LIFETIME);
	CHECK(list.count == 1 && social_sent_has(&list, "grif", SOCIAL_SENT_PARTY, 2000 + SOCIAL_SENT_LIFETIME));
	social_sent_add(&list, "grif", SOCIAL_SENT_GAME, 5);
	social_sent_forget(&list, "grif", SOCIAL_SENT_PARTY);
	CHECK(list.count == 1 && social_sent_has(&list, "grif", SOCIAL_SENT_GAME, 6));
	for (index = 0; index < SOCIAL_MAXIMUM_SENT + 3; index++)
	{
		snprintf(text, sizeof(text), "player%d", index);
		social_sent_add(&list, text, SOCIAL_SENT_PARTY, 10);
	}
	CHECK(list.count == SOCIAL_MAXIMUM_SENT && social_sent_has(&list, "player18", SOCIAL_SENT_PARTY, 11));
	social_sent_forget(&list, NULL, SOCIAL_SENT_PARTY);
	CHECK(list.count == 0);

	social_sent_line(text, sizeof(text), SOCIAL_SENT_PARTY, "Grif", 1, "");
	CHECK(!strcmp(text, "Invite sent to Grif"));
	social_sent_line(text, sizeof(text), SOCIAL_SENT_PARTY, "Grif", 0, NULL);
	CHECK(!strcmp(text, "Invite sent to Grif. It waits 5 minutes for them."));
	social_sent_line(text, sizeof(text), SOCIAL_SENT_REQUEST, "Grif", 1, "");
	CHECK(!strcmp(text, "Request sent to Grif"));
	social_sent_line(text, sizeof(text), SOCIAL_SENT_PARTY, "Grif", 1, "Your party is full.");
	CHECK(!strcmp(text, "Your party is full."));
}

/* news in one poll: each shows, one after another */
static void test_toast_queue(void)
{
	static struct social_toast_queue queue;
	struct social_toast toast;
	int index;

	memset(&queue, 0, sizeof(queue));
	CHECK(!social_toast_pop(&queue, &toast));
	social_toast_push(&queue, 1, "inv1", "Sarge invited you", "Blood Gulch", "Sarge");
	social_toast_push(&queue, 3, "grif", "Grif", "Friend request", "Grif");
	/* (the same one again: still one) */
	social_toast_push(&queue, 3, "grif", "Grif", "Friend request", "Grif");
	CHECK(queue.count == 2);
	CHECK(social_toast_pop(&queue, &toast) && toast.kind == 1 && !strcmp(toast.key, "inv1"));
	CHECK(social_toast_pop(&queue, &toast) && toast.kind == 3 && !strcmp(toast.title, "Grif"));
	CHECK(!social_toast_pop(&queue, &toast));
	/* (answered before it showed) */
	social_toast_push(&queue, 3, "simmons", "Simmons", "Friend request", "Simmons");
	social_toast_forget(&queue, 3, "simmons");
	CHECK(queue.count == 0);
	/* (full: the oldest goes) */
	for (index = 0; index < SOCIAL_TOAST_QUEUE_MOST + 2; index++)
	{
		char key[8];

		snprintf(key, sizeof(key), "k%d", index);
		social_toast_push(&queue, 2, key, "t", "l", "");
	}
	CHECK(queue.count == SOCIAL_TOAST_QUEUE_MOST);
	CHECK(social_toast_pop(&queue, &toast) && !strcmp(toast.key, "k2"));
}

int main(void)
{
	test_party_actions();
	test_toast_queue();
	test_sent();
	test_sections();
	test_copy_text();
	test_list();
	test_status();
	test_poll();
	test_presence();
	test_state();
	test_party();
	test_list_keep();
	test_list_tap();
	test_recent();
	test_clan();
	test_record();
	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("ok\n");
	return 0;
}
