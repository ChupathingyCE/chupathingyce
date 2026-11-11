/*
DELTA_INPUT_RULES_TEST.C

Tests of DELTA's keys and Back held in the menus
(port/linux/src/delta_input_rules.c). Built and run by
tools/test_delta_input_rules.py; exits nonzero on a failure.
*/

#include "../src/delta_input_rules.h"

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

static void test_keys(void)
{
	char names[DELTA_KEYS_MOST][DELTA_KEY_NAME_SIZE], shown[DELTA_KEY_NAME_SIZE];

	CHECK(delta_keys_split("F3", names, DELTA_KEYS_MOST) == 1 && !strcmp(names[0], "F3"));
	CHECK(delta_keys_split(" F3 ,  \\ ", names, DELTA_KEYS_MOST) == 2 && !strcmp(names[0], "F3") &&
		!strcmp(names[1], "\\"));
	CHECK(delta_keys_split("", names, DELTA_KEYS_MOST) == 0);
	CHECK(delta_keys_split(",,F4,", names, DELTA_KEYS_MOST) == 1 && !strcmp(names[0], "F4"));
	CHECK(delta_keys_split("F3, F4, F5, F6, F7", names, DELTA_KEYS_MOST) == DELTA_KEYS_MOST);
	CHECK(delta_keys_split(NULL, names, DELTA_KEYS_MOST) == 0);

	/* (a prompt: the first key; a Mac's, the first that isn't an F key) */
	delta_key_shown("F3, Backslash", 0, shown, sizeof(shown));
	CHECK(!strcmp(shown, "F3"));
	delta_key_shown("F3, Backslash", 1, shown, sizeof(shown));
	CHECK(!strcmp(shown, "Backslash"));
	delta_key_shown("F5", 1, shown, sizeof(shown));
	CHECK(!strcmp(shown, "F5"));
	delta_key_shown("F12, F1", 1, shown, sizeof(shown));
	CHECK(!strcmp(shown, "F12"));
	delta_key_shown("", 1, shown, sizeof(shown));
	CHECK(!strcmp(shown, "F3"));
}

/* Back held in the menus: what the menus get, poll by poll */
static void test_back_hold(void)
{
	struct delta_back_hold hold;
	unsigned long now = 1000, held;
	int opens = 0, passes = 0;

	/* a hold: DELTA opens once, the menus never see Back */
	memset(&hold, 0, sizeof(hold));
	for (held = 0; held <= 2000; held += 16)
	{
		int step = delta_back_hold_step(&hold, 1, held < DELTA_BACK_HOLD + 32, now + held);

		opens += (step & DELTA_BACK_OPEN) != 0;
		passes += (step & DELTA_BACK_PASS) != 0;
	}
	/* (and let go, DELTA open now: nothing left for the menu) */
	for (held = 2016; held < 2400; held += 16)
		passes += (delta_back_hold_step(&hold, 0, 1, now + held) & DELTA_BACK_PASS) != 0;
	CHECK(opens == 1);
	CHECK(passes == 0);

	/* a tap: nothing while down, a short press once let go */
	memset(&hold, 0, sizeof(hold));
	opens = passes = 0;
	for (held = 0; held < 200; held += 16)
		passes += (delta_back_hold_step(&hold, 1, 1, now + held) & DELTA_BACK_PASS) != 0;
	CHECK(passes == 0);
	for (held = 208; held < 1000; held += 16)
	{
		int step = delta_back_hold_step(&hold, 0, 1, now + held);

		opens += (step & DELTA_BACK_OPEN) != 0;
		passes += (step & DELTA_BACK_PASS) != 0;
	}
	CHECK(opens == 0);
	CHECK(passes > 0 && passes <= DELTA_BACK_TAP / 16 + 1);

	/* not armed (off, or in a game): Back goes through as it is */
	memset(&hold, 0, sizeof(hold));
	CHECK(delta_back_hold_step(&hold, 1, 0, now) == DELTA_BACK_PASS);
	CHECK(delta_back_hold_step(&hold, 1, 0, now + 5000) == DELTA_BACK_PASS);
	CHECK(delta_back_hold_step(&hold, 0, 0, now + 5016) == 0);
	/* (a press that began unarmed stays the game's when the menus come up
	under it: no DELTA, no tap later) */
	CHECK(delta_back_hold_step(&hold, 1, 0, now + 6000) == DELTA_BACK_PASS);
	CHECK(delta_back_hold_step(&hold, 1, 1, now + 7000) == DELTA_BACK_PASS);
	CHECK(delta_back_hold_step(&hold, 0, 1, now + 7016) == 0);

	/* a tap's press lasts at least DELTA_BACK_TAP_READS reads, however long
	a read takes */
	memset(&hold, 0, sizeof(hold));
	CHECK(delta_back_hold_step(&hold, 1, 1, now) == 0);
	CHECK(delta_back_hold_step(&hold, 0, 1, now + 50) == DELTA_BACK_PASS);
	CHECK(delta_back_hold_step(&hold, 0, 1, now + 500) == DELTA_BACK_PASS);
	CHECK(delta_back_hold_step(&hold, 0, 1, now + 1000) == 0);

	/* the capture buttons' Back and a stick (the capture clears Back from the
	pad and cancels the hold, each read): no DELTA, no tap, while it lasts
	and once it is let go */
	memset(&hold, 0, sizeof(hold));
	opens = passes = 0;
	for (held = 0; held < 200; held += 16)
		passes += (delta_back_hold_step(&hold, 1, 1, now + held) & DELTA_BACK_PASS) != 0;
	for (held = 200; held < 2000; held += 16)
	{
		int step;

		delta_back_hold_cancel(&hold);
		step = delta_back_hold_step(&hold, 0, 1, now + held);
		opens += (step & DELTA_BACK_OPEN) != 0;
		passes += (step & DELTA_BACK_PASS) != 0;
	}
	for (held = 2000; held < 2500; held += 16)
		passes += (delta_back_hold_step(&hold, 0, 1, now + held) & DELTA_BACK_PASS) != 0;
	CHECK(opens == 0);
	CHECK(passes == 0);
	/* (and the next tap is a tap again) */
	CHECK(delta_back_hold_step(&hold, 1, 1, now + 3000) == 0);
	CHECK(delta_back_hold_step(&hold, 0, 1, now + 3050) == DELTA_BACK_PASS);

	/* the clock wrapping during a hold */
	memset(&hold, 0, sizeof(hold));
	CHECK(delta_back_hold_step(&hold, 1, 1, (unsigned long)-100) == 0);
	CHECK(delta_back_hold_step(&hold, 1, 1, (unsigned long)DELTA_BACK_HOLD) == DELTA_BACK_OPEN);
}

/* the main menu's note */
static void test_hint(void)
{
	char text[64];

	CHECK(delta_hint_due(0, 0) && delta_hint_due(DELTA_HINT_TIMES - 1, 0));
	CHECK(!delta_hint_due(DELTA_HINT_TIMES, 0));
	/* (no note with DELTA's rows in the menus) */
	CHECK(!delta_hint_due(0, 1));
	CHECK(!delta_hint_due(-1, 0));
	delta_hint_text(text, sizeof(text), 0, "F3", 1, 1);
	CHECK(!strcmp(text, "Press F3 for the Delta Guide"));
	/* (a Mac's key, as its prompts show it) */
	delta_hint_text(text, sizeof(text), 0, "\\", 1, 0);
	CHECK(!strcmp(text, "Press \\ for the Delta Guide"));
	delta_hint_text(text, sizeof(text), 1, "F3", 1, 0);
	CHECK(!strcmp(text, "Press the Guide button"));
	delta_hint_text(text, sizeof(text), 1, "F3", 1, 1);
	CHECK(!strcmp(text, "Hold Back for the Delta Guide"));
	/* (a pad with neither: the keyboard's way) */
	delta_hint_text(text, sizeof(text), 1, "F4", 0, 0);
	CHECK(!strcmp(text, "Press F4 for the Delta Guide"));
	delta_hint_text(text, sizeof(text), 0, "", 0, 0);
	CHECK(!strcmp(text, "Press F3 for the Delta Guide"));
	/* (a Mac: never Guide, which macOS keeps) */
	delta_hint_text_for(text, sizeof(text), 1, "\\", 1, 0, 1);
	CHECK(!strcmp(text, "Press \\ for the Delta Guide"));
	delta_hint_text_for(text, sizeof(text), 1, "\\", 1, 1, 1);
	CHECK(!strcmp(text, "Hold Back for the Delta Guide"));
	delta_hint_text_for(text, sizeof(text), 1, "F3", 1, 0, 0);
	CHECK(!strcmp(text, "Press the Guide button"));
}

int main(void)
{
	test_hint();
	test_keys();
	test_back_hold();
	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("ok\n");
	return 0;
}
