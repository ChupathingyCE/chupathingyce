/*
PLAYER_PROFILE_LIST.H

port: the profile a multiplayer profile list starts on. No game or SDL
dependency, so tools/test_player_profile_list.py builds it alone.
*/

#ifndef __PLAYER_PROFILE_LIST_H
#define __PLAYER_PROFILE_LIST_H

/* (saved_game_files.h's _saved_game_file_index_read_only_bit and
_saved_game_file_index_valid_bit) */
#define PLAYER_PROFILE_LIST_READ_ONLY_BIT 30
#define PLAYER_PROFILE_LIST_VALID_BIT 31

/* whether the list's entry is one of the built-in profiles (Default,
Inverted): read only, and with no name of their own */
static int player_profile_list_built_in(
	long profile_index)
{
	unsigned long bits = (unsigned long)profile_index;

	return profile_index != -1 &&
		(bits & (1UL << PLAYER_PROFILE_LIST_VALID_BIT)) != 0 &&
		(bits & (1UL << PLAYER_PROFILE_LIST_READ_ONLY_BIT)) != 0;
}

/* the entry the list starts on: the profile player 1 last used, as the
Xbox's did; with none (a profile made or renamed in Settings is not one
until a game is played with it), the first of the player's own instead of
a built-in one, whose empty name multiplayer refuses (player_name_valid).
-1 leaves the list where it is */
static short player_profile_list_start(
	long const *profile_indices,
	short count,
	long last_used_profile_index)
{
	short index;

	if (last_used_profile_index != -1)
	{
		for (index = 0; index < count; index++)
		{
			if (profile_indices[index] == last_used_profile_index)
				return index;
		}
	}
	for (index = 0; index < count; index++)
	{
		long profile_index = profile_indices[index];

		if (profile_index != -1 &&
			((unsigned long)profile_index & (1UL << PLAYER_PROFILE_LIST_VALID_BIT)) != 0 &&
			!player_profile_list_built_in(profile_index))
		{
			return index;
		}
	}
	return -1;
}

#endif
