/*
STUBBS_CAMPAIGN.C

Stubbs mode's campaign (stubbs_campaign.h): its levels, in its own order,
and how one leads to the next. Called only in Stubbs mode.

The levels and their order are read from the maps themselves: every level's
scripts list the twelve (the static scripts a10 ... c50, each a map_name),
and each level ends with game_won (a10's trigger_to_greenhouse_script says
"have fun at the greenhouse!!!", a30 next). Stubbs's scripts also ask
(map_completed <difficulty>) as a level starts, for its unlockables; the
answer is kept here, per profile, as Halo's profile has room for only ten
levels. The maps' script nodes were read with the layout in Sigmmma's
reclaimer (GPL-3.0, halo_script/hsc.py), thanks!

Stubbs mode keeps its profiles and saved games apart from Halo's: the Xbox
drives (u:\, z:\ ...) are <save root>/stubbs/ while it is on
(platform_save_drive_folder), so neither game's menus show the other's.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "errors.h"
#include "cache/cache_files.h"
#include "game/game.h"
#include "interface/player_ui.h"
#include "saved games/player_profile.h"
#include "interface/ui_widget_instance.h"
#include "tag_files/tag_files.h"
#include "stubbs_mode.h"
#include "stubbs_campaign.h"

#include <stdio.h>
#include <string.h>

#ifndef HALO_CONSOLE_NO_STUBBS /* (the header's empty ones on a console) */

void platform_save_drive_folder(const char *folder);

/* the campaign's levels, in order */
static char const *const stubbs_levels[STUBBS_CAMPAIGN_LEVEL_COUNT] =
{
	"levels\\a10_plaza\\a10_plaza",
	"levels\\a30_greenhouse\\a30_greenhouse",
	"levels\\a40_police_station\\a40_police_station",
	"levels\\a45_dance\\a45_dance",
	"levels\\a50_maul\\a50_maul",
	"levels\\a60_maulfight\\a60_maulfight",
	"levels\\b10_farm_house\\b10_farm_house",
	"levels\\b30_dam\\b30_dam",
	"levels\\c10_offender\\c10_offender",
	"levels\\c30_lab\\c30_lab",
	"levels\\c40_cityhall\\c40_cityhall",
	"levels\\c50_end\\c50_end",
};

/* the profiles' finished levels (on Stubbs's own drives): a line
"<profile> <level> <difficulty flags>" for each, the profile by its name (in
hexadecimal, four digits a character) */
#define STUBBS_PROGRESS_FILE "u:\\stubbs_progress.txt"
#define STUBBS_PROGRESS_PROFILES 64
#define STUBBS_PROGRESS_KEY_SIZE (4 * MAXIMUM_PLAYER_PROFILE_NAME_LENGTH + 1)

/* a map's own name: the last part of its path, without a pack's @<pack> */
static void level_base_name(
	char const *name,
	char *base,
	unsigned long size)
{
	char const *last = name;
	char const *cursor;
	unsigned long length;

	for (cursor = name; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/')
			last = cursor + 1;
	}
	length = (unsigned long)strcspn(last, "@");
	if (length >= size)
		length = size - 1;
	memcpy(base, last, length);
	base[length] = 0;
}

void stubbs_campaign_initialize(
	void)
{
	platform_save_drive_folder("stubbs");
	stubbs_mode_log("its profiles and saved games are in the save root's stubbs folder");
}

char const *stubbs_campaign_level_name(
	short level,
	char const *halo_name)
{
	(void)halo_name;
	return level >= 0 && level < STUBBS_CAMPAIGN_LEVEL_COUNT ? stubbs_levels[level] : NULL;
}

short stubbs_campaign_level_from_name(
	char const *name,
	short halo_level)
{
	char base[64];
	char level_base[64];
	short level;

	(void)halo_level;
	if (!name)
		return NONE;
	level_base_name(name, base, sizeof(base));
	for (level = 0; level < STUBBS_CAMPAIGN_LEVEL_COUNT; level++)
	{
		level_base_name(stubbs_levels[level], level_base, sizeof(level_base));
		if (!_stricmp(base, level_base))
			return level;
	}
	return NONE;
}

boolean stubbs_campaign_map_name_allowed(
	void)
{
	/* (a Stubbs level's own scripts, which name only Stubbs's levels) */
	return stubbs_campaign_level_from_name(cache_file_loaded_map_name(), NONE) != NONE;
}

/* ---------- the profiles' finished levels */

static struct
{
	char profile[STUBBS_PROGRESS_KEY_SIZE];
	unsigned long flags[STUBBS_CAMPAIGN_LEVEL_COUNT];
} stubbs_progress[STUBBS_PROGRESS_PROFILES];
static long stubbs_progress_count;

/* a local player's profile, as the file names it: FALSE for none */
static boolean progress_key(
	short local_player_index,
	char *key)
{
	struct player_profile profile;
	long index;

	key[0] = 0;
	if (player_ui_get_active_player_profile_index(local_player_index) == NONE)
		return FALSE;
	player_ui_get_active_player_profile(local_player_index, &profile);
	for (index = 0; index < MAXIMUM_PLAYER_PROFILE_NAME_LENGTH && profile.player_name[index]; index++)
		sprintf(key + 4 * index, "%04x", (unsigned)(profile.player_name[index] & 0xFFFF));
	return key[0] != 0;
}

/* a profile's entry, made if need be (NONE if there is no room) */
static long progress_entry(
	char const *key,
	boolean make)
{
	long index;

	for (index = 0; index < stubbs_progress_count; index++)
	{
		if (!strcmp(stubbs_progress[index].profile, key))
			return index;
	}
	if (!make || stubbs_progress_count >= STUBBS_PROGRESS_PROFILES)
		return NONE;
	memset(&stubbs_progress[index], 0, sizeof(stubbs_progress[index]));
	csstrncpy(stubbs_progress[index].profile, key, STUBBS_PROGRESS_KEY_SIZE - 1);
	stubbs_progress_count++;
	return index;
}

static void progress_read(
	void)
{
	FILE *file = fopen(STUBBS_PROGRESS_FILE, "r");
	char line[STUBBS_PROGRESS_KEY_SIZE + 64];

	stubbs_progress_count = 0;
	if (!file)
		return;
	while (fgets(line, sizeof(line), file))
	{
		char key[STUBBS_PROGRESS_KEY_SIZE];
		long level, index;
		unsigned long flags;

		if (sscanf(line, "%48s %ld %lu", key, &level, &flags) != 3 ||
			level < 0 || level >= STUBBS_CAMPAIGN_LEVEL_COUNT)
		{
			continue;
		}
		index = progress_entry(key, TRUE);
		if (index != NONE)
			stubbs_progress[index].flags[level] |= flags & 0xF;
	}
	fclose(file);
}

static void progress_write(
	void)
{
	FILE *file = fopen(STUBBS_PROGRESS_FILE, "w");
	long index, level;

	if (!file)
	{
		error(_error_silent, "stubbs mode: couldn't write the finished levels");
		return;
	}
	for (index = 0; index < stubbs_progress_count; index++)
	{
		for (level = 0; level < STUBBS_CAMPAIGN_LEVEL_COUNT; level++)
		{
			if (stubbs_progress[index].flags[level])
				fprintf(file, "%s %ld %lu\n", stubbs_progress[index].profile, level, stubbs_progress[index].flags[level]);
		}
	}
	fclose(file);
}

boolean stubbs_campaign_map_completed(
	short difficulty)
{
	short level = stubbs_campaign_level_from_name(cache_file_loaded_map_name(), NONE);
	char key[STUBBS_PROGRESS_KEY_SIZE];
	long index;

	if (level == NONE || !progress_key(0, key) || difficulty < 0 || difficulty >= 4)
		return FALSE;
	progress_read();
	index = progress_entry(key, FALSE);
	/* (finished on that difficulty or a harder one) */
	return index != NONE && (stubbs_progress[index].flags[level] >> difficulty) != 0;
}

unsigned long stubbs_campaign_level_flags(
	short local_player_index,
	short level)
{
	char key[STUBBS_PROGRESS_KEY_SIZE];
	long index;

	if (level < 0 || level >= STUBBS_CAMPAIGN_LEVEL_COUNT || !progress_key(local_player_index, key))
		return 0;
	progress_read();
	index = progress_entry(key, FALSE);
	return index != NONE ? stubbs_progress[index].flags[level] : 0;
}

boolean stubbs_campaign_level_available(
	short local_player_index,
	short level)
{
	if (level < 0 || level >= STUBBS_CAMPAIGN_LEVEL_COUNT)
		return FALSE;
	return level == 0 || stubbs_campaign_level_flags(local_player_index, level) ||
		stubbs_campaign_level_flags(local_player_index, level - 1);
}

char const *stubbs_campaign_level_won(
	char const *map_name,
	short local_player_count)
{
	short level = stubbs_campaign_level_from_name(map_name, NONE);
	short difficulty = game_difficulty_level_get();
	short local_player_index;
	char const *next;

	if (level == NONE)
		return NULL;
	if (difficulty >= 0 && difficulty < 4)
	{
		progress_read();
		for (local_player_index = 0; local_player_index < local_player_count; local_player_index++)
		{
			char key[STUBBS_PROGRESS_KEY_SIZE];
			long index = progress_key(local_player_index, key) ? progress_entry(key, TRUE) : NONE;

			if (index != NONE)
				stubbs_progress[index].flags[level] |= 1UL << difficulty;
		}
		progress_write();
	}
	next = level + 1 < STUBBS_CAMPAIGN_LEVEL_COUNT ? stubbs_levels[level + 1] : NULL;
	stubbs_mode_log("%s won; next: %s", stubbs_levels[level], next ? next : "the main menu");
	return next;
}

/* Stubbs's ui.map has one widget function past Halo's 102 (0 to 101): its
new campaign's "creating profile" screen runs function 102 as it opens
(event created), beside Halo's own new campaign decision (97) as it closes.
What it did in Stubbs isn't known; the new campaign is the same without it
(profile, difficulty, a10_plaza), so it is run as one that does nothing,
rather than failing as an invalid function */
#define STUBBS_WIDGET_FUNCTION_CREATING_PROFILE 102

boolean stubbs_campaign_event_function(
	struct widget_instance *widget,
	struct event_record *event,
	word function_index,
	boolean *widget_deleted,
	boolean *result)
{
	(void)event;
	(void)widget_deleted;
	if (function_index == STUBBS_WIDGET_FUNCTION_CREATING_PROFILE)
	{
		*result = TRUE;
		return TRUE;
	}
	stubbs_mode_log("menu widget function %u, which Halo lacks (%s)", (unsigned)function_index,
		widget && widget->definition_tag_index != NONE ? tag_get_name(widget->definition_tag_index) : "?");
	return FALSE;
}

#endif
