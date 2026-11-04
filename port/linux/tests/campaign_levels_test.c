/*
CAMPAIGN_LEVELS_TEST.C

Tests of the campaign levels' places (port/linux/src/campaign_levels.h):
each level's map is its frame in the menus' level pictures, in any case,
and no other map is one. Built and run by tools/test_campaign_levels.py;
exits nonzero on a failure.
*/

#include "../src/campaign_levels.h"

#include <stdio.h>

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

int main(void)
{
	/* (the level select menus' order: ui_widget.c's scenario_paths) */
	static char const *const levels[] = { "a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40" };
	int index;

	CHECK(sizeof(levels) / sizeof(levels[0]) == CAMPAIGN_LEVEL_COUNT);
	for (index = 0; index < CAMPAIGN_LEVEL_COUNT; index++)
		CHECK(campaign_level_index(levels[index]) == index);
	CHECK(campaign_level_index("A10") == 0);
	CHECK(campaign_level_index("D40") == 9);
	CHECK(campaign_level_index("bloodgulch") == -1);
	CHECK(campaign_level_index("a1") == -1);
	CHECK(campaign_level_index("a100") == -1);
	CHECK(campaign_level_index("a20") == -1);
	CHECK(campaign_level_index("levels\\a10\\a10") == -1);
	CHECK(campaign_level_index("") == -1);
	CHECK(campaign_level_index(NULL) == -1);
	if (!failures)
		printf("campaign levels: all passed\n");
	return failures != 0;
}
