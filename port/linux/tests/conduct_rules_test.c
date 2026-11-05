/*
CONDUCT_RULES_TEST.C

Tests of a dedicated server's betrayal and AFK limits' rules (port/linux/
game/conduct_rules.c): when betrayals earn a warning or a kick, when an
automatic kick is a second within a day, what input is, and when an idle
player is warned and kicked. tools/test_conduct_rules.py builds and runs
them.
*/

#include "../game/conduct_rules.h"

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

static void test_betrayals(void)
{
	/* (off: never anything) */
	CHECK(conduct_betrayal_action(0, 0, 1) == _conduct_none);
	CHECK(conduct_betrayal_action(0, 50, 99) == _conduct_none);
	/* (3: warned at 2, kicked at 3) */
	CHECK(conduct_betrayal_action(3, 0, 1) == _conduct_none);
	CHECK(conduct_betrayal_action(3, 1, 2) == _conduct_warn);
	CHECK(conduct_betrayal_action(3, 2, 3) == _conduct_kick);
	/* (a count already acted on, or no new betrayal: nothing again) */
	CHECK(conduct_betrayal_action(3, 2, 2) == _conduct_none);
	CHECK(conduct_betrayal_action(3, 3, 4) == _conduct_none);
	CHECK(conduct_betrayal_action(3, 3, 3) == _conduct_none);
	/* (two at once, a grenade's: past the warning, straight to the kick) */
	CHECK(conduct_betrayal_action(3, 1, 3) == _conduct_kick);
	CHECK(conduct_betrayal_action(3, 0, 2) == _conduct_warn);
	/* (1: the first is a kick, with no warning; 2: warned at the first) */
	CHECK(conduct_betrayal_action(1, 0, 1) == _conduct_kick);
	CHECK(conduct_betrayal_action(2, 0, 1) == _conduct_warn);
	CHECK(conduct_betrayal_action(2, 1, 2) == _conduct_kick);
	/* (a count that went down: nothing) */
	CHECK(conduct_betrayal_action(3, 2, 1) == _conduct_none);
}

static void test_repeat_kicks(void)
{
	static struct conduct_kick_history history;
	long long day = CONDUCT_REPEAT_SECONDS;
	char key[CONDUCT_KEY_SIZE];
	int index;

	memset(&history, 0, sizeof(history));
	CHECK(conduct_note_automatic_kick(&history, "h:aaaa", 1000) == 0);
	/* (another player: a first) */
	CHECK(conduct_note_automatic_kick(&history, "h:bbbb", 1001) == 0);
	/* (the same within a day: the second, a ban) */
	CHECK(conduct_note_automatic_kick(&history, "h:aaaa", 1000 + day - 1) == 1);
	/* (a day after the last: a first again) */
	CHECK(conduct_note_automatic_kick(&history, "h:bbbb", 1001 + day) == 0);
	CHECK(conduct_note_automatic_kick(&history, "h:bbbb", 1001 + day + 60) == 1);
	/* (no key known: never a repeat, nothing remembered) */
	CHECK(conduct_note_automatic_kick(&history, "", 5000) == 0);
	CHECK(conduct_note_automatic_kick(&history, "", 5001) == 0);
	CHECK(conduct_note_automatic_kick(&history, NULL, 5002) == 0);
	CHECK(conduct_note_automatic_kick(NULL, "h:aaaa", 5003) == 0);
	/* (a clock that went back: forgotten, not a ban) */
	memset(&history, 0, sizeof(history));
	CHECK(conduct_note_automatic_kick(&history, "h:cccc", 10000) == 0);
	CHECK(conduct_note_automatic_kick(&history, "h:cccc", 9000) == 0);
	/* (full: the oldest forgotten, the newest kept) */
	memset(&history, 0, sizeof(history));
	for (index = 0; index < CONDUCT_HISTORY_SIZE; index++)
	{
		snprintf(key, sizeof(key), "h:%d", index);
		CHECK(conduct_note_automatic_kick(&history, key, 100 + index) == 0);
	}
	CHECK(conduct_note_automatic_kick(&history, "h:new", 100 + CONDUCT_HISTORY_SIZE) == 0);
	CHECK(conduct_note_automatic_kick(&history, "h:0", 101 + CONDUCT_HISTORY_SIZE) == 0);
	snprintf(key, sizeof(key), "h:%d", CONDUCT_HISTORY_SIZE - 1);
	CHECK(conduct_note_automatic_kick(&history, key, 102 + CONDUCT_HISTORY_SIZE) == 1);
	CHECK(conduct_note_automatic_kick(&history, "h:new", 103 + CONDUCT_HISTORY_SIZE) == 1);
	/* (a long key: cut to fit, still the same player) */
	memset(&history, 0, sizeof(history));
	memset(key, 'x', sizeof(key) - 1);
	key[sizeof(key) - 1] = 0;
	CHECK(conduct_note_automatic_kick(&history, key, 1) == 0);
	CHECK(conduct_note_automatic_kick(&history, key, 2) == 1);
}

static void test_input(void)
{
	struct conduct_input still;
	struct conduct_input other;

	memset(&still, 0, sizeof(still));
	still.yaw = 1.0f;
	still.weapon_index = -1;
	still.grenade_index = -1;
	other = still;
	/* (the same again: none) */
	CHECK(!conduct_input_active(&still, &other));
	/* (a little drift: none) */
	other.yaw += 0.0001f;
	other.throttle_i = 0.05f;
	CHECK(!conduct_input_active(&still, &other));
	/* (turned, a button, the trigger, the weapon, the zoom) */
	other = still;
	other.yaw += 0.01f;
	CHECK(conduct_input_active(&still, &other));
	other = still;
	other.pitch -= 0.01f;
	CHECK(conduct_input_active(&still, &other));
	other = still;
	other.control_flags = 1;
	CHECK(conduct_input_active(&still, &other));
	other = still;
	other.primary_trigger = 1.0f;
	CHECK(conduct_input_active(&still, &other));
	other = still;
	other.weapon_index = 1;
	CHECK(conduct_input_active(&still, &other));
	other = still;
	other.grenade_index = 0;
	CHECK(conduct_input_active(&still, &other));
	other = still;
	other.zoom_level = 1;
	CHECK(conduct_input_active(&still, &other));
	/* (moving: held forward is input, every time) */
	other = still;
	other.throttle_j = 1.0f;
	CHECK(conduct_input_active(&other, &other));
	CHECK(conduct_input_active(NULL, &other));
	/* (nothing before, standing still: none; nothing now: none) */
	CHECK(!conduct_input_active(NULL, &still));
	CHECK(!conduct_input_active(&still, NULL));
}

static void test_afk(void)
{
	/* (off) */
	CHECK(conduct_afk_action(0, 999999999UL) == _conduct_none);
	/* (5 minutes: warned 30 seconds before, kicked at it) */
	CHECK(conduct_afk_action(5, 0) == _conduct_none);
	CHECK(conduct_afk_action(5, 269999UL) == _conduct_none);
	CHECK(conduct_afk_action(5, 270000UL) == _conduct_warn);
	CHECK(conduct_afk_action(5, 299999UL) == _conduct_warn);
	CHECK(conduct_afk_action(5, 300000UL) == _conduct_kick);
	/* (1 minute: warned at 30 seconds) */
	CHECK(conduct_afk_action(1, 29999UL) == _conduct_none);
	CHECK(conduct_afk_action(1, 30000UL) == _conduct_warn);
	CHECK(conduct_afk_action(1, 60000UL) == _conduct_kick);
	/* (more than the most: the most) */
	CHECK(conduct_afk_action(10000, (unsigned long)CONDUCT_MAXIMUM_AFK_MINUTES * 60000UL) == _conduct_kick);
}

int main(void)
{
	test_betrayals();
	test_repeat_kicks();
	test_input();
	test_afk();
	if (failures)
	{
		printf("conduct_rules_test: %d failures\n", failures);
		return 1;
	}
	printf("conduct_rules_test: ok\n");
	return 0;
}
