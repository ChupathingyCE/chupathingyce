/*
PLAYER_PROFILE_LIST_TEST.C

Tests of the profile a multiplayer profile list starts on (source/saved
games/player_profile_list.h): the one player 1 last used, else the first of
the player's own, never a built-in one (Default, Inverted) while the player
has one. Built and run by tools/test_player_profile_list.py; exits nonzero on
a failure.
*/

#include "../../../source/saved games/player_profile_list.h"

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

#define VALID ((long)(1UL << PLAYER_PROFILE_LIST_VALID_BIT))
#define READ_ONLY ((long)(1UL << PLAYER_PROFILE_LIST_READ_ONLY_BIT))

int main(void)
{
	/* (as saved_game_files.c lists them: Default and Inverted first, read
	only, then the player's) */
	long const built_in_default = VALID | READ_ONLY | 0x1F0000;
	long const built_in_inverted = VALID | READ_ONLY | 0x1F0001;
	long const new001 = VALID | 0x1F0002;
	long const tester = VALID | 0x1F0003;
	long const fresh[] = { built_in_default, built_in_inverted };
	long const one[] = { built_in_default, built_in_inverted, new001 };
	long const two[] = { built_in_default, built_in_inverted, new001, tester };
	long const padded[] = { new001, -1, -1 };

	CHECK(player_profile_list_built_in(built_in_default));
	CHECK(player_profile_list_built_in(built_in_inverted));
	CHECK(!player_profile_list_built_in(new001));
	CHECK(!player_profile_list_built_in(-1));

	/* none of the player's own: the list stays where it is */
	CHECK(player_profile_list_start(fresh, 2, -1) == -1);
	/* a profile made in Settings, no game played with it yet: it, not Default */
	CHECK(player_profile_list_start(one, 3, -1) == 2);
	/* the one last used, as the Xbox's did */
	CHECK(player_profile_list_start(two, 4, tester) == 3);
	CHECK(player_profile_list_start(two, 4, new001) == 2);
	/* last used a built-in one: still it */
	CHECK(player_profile_list_start(two, 4, built_in_inverted) == 1);
	/* last used one no longer listed (deleted): the first of the player's own */
	CHECK(player_profile_list_start(two, 4, VALID | 0x1F0009) == 2);
	/* the lists padded with NONE (3-wide ones) */
	CHECK(player_profile_list_start(padded, 3, -1) == 0);
	CHECK(player_profile_list_start(padded + 1, 2, -1) == -1);

	if (failures)
		return 1;
	printf("player profile list: all passed\n");
	return 0;
}
