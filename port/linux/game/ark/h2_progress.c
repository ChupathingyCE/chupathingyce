/*
H2_PROGRESS.C

port: Halo 2's campaign progress for the profile (h2_progress.h).
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "main/main.h"
#include "saved games/game_state.h"
#include "h2_campaign.h"
#include "h2_progress.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

boolean game_state_port_profile_directory(char *path);
void game_state_port_discard_h2_save(void);
boolean game_state_set_persistent_storage_custom(boolean custom);

static struct
{
	boolean loaded;
	char directory[256];
	unsigned long completed;    /* (bit k: campaign level k won) */
	short difficulty;
	long last;                  /* (the level begun last) */
} h2_progress = { FALSE, "", 0, _game_difficulty_level_normal, NONE };

static boolean h2_progress_path(
	char *path,
	long size)
{
	char directory[256];

	if (!game_state_port_profile_directory(directory))
		return FALSE;
	snprintf(path, size, "%scampaign_h2.txt", directory);
	return TRUE;
}

static void h2_progress_save(
	void);

/* the profile's file read (once for each profile directory) */
static void h2_progress_load(
	void)
{
	char directory[256], path[300], line[160];
	FILE *file;
	unsigned long carried = 0;

	if (!game_state_port_profile_directory(directory))
		directory[0] = 0;
	if (h2_progress.loaded && !strcmp(directory, h2_progress.directory))
		return;
	/* (levels won with no profile - the hub's way in, "kept for this run" -
	go to the profile taken after them, as Halo 2 keeps a campaign played
	before signing in: the menu takes one (h2_menu.c, the current or a new
	profile) when it next opens, and reading that profile's file dropped
	them - 5ackwood won The Heretic that way and found The Armory still
	locked in the level select) */
	if (h2_progress.loaded && !h2_progress.directory[0] && directory[0])
		carried = h2_progress.completed;
	h2_progress.loaded = TRUE;
	snprintf(h2_progress.directory, sizeof(h2_progress.directory), "%s", directory);
	h2_progress.completed = 0;
	h2_progress.difficulty = _game_difficulty_level_normal;
	h2_progress.last = NONE;
	if (!directory[0] || !h2_progress_path(path, sizeof(path)))
		return;
	if ((file = fopen(path, "rb")) != NULL)
	{
		while (fgets(line, sizeof(line), file))
		{
			unsigned long value;

			if (sscanf(line, "completed %lx", &value) == 1)
				h2_progress.completed = value & ((1UL << h2_campaign_level_count()) - 1);
			else if (sscanf(line, "difficulty %lu", &value) == 1 && value < NUMBER_OF_GAME_DIFFICULTY_LEVELS)
				h2_progress.difficulty = (short)value;
			else if (sscanf(line, "last %lu", &value) == 1 && (long)value < h2_campaign_level_count())
				h2_progress.last = (long)value;
		}
		fclose(file);
		platform_log("h2 progress: %s: levels won %lx, difficulty %d", path, h2_progress.completed, (int)h2_progress.difficulty);
	}
	if (carried & ~h2_progress.completed)
	{
		h2_progress.completed |= carried;
		h2_progress_save();
		platform_log("h2 progress: the levels won with no profile (%lx) kept in %s: levels won %lx", carried, path,
			h2_progress.completed);
	}
}

static void h2_progress_save(
	void)
{
	char path[300];
	FILE *file;

	if (!h2_progress_path(path, sizeof(path)) || !(file = fopen(path, "wb")))
	{
		platform_log("h2 progress: no profile to keep the campaign's progress in (kept for this run)");
		return;
	}
	fprintf(file, "completed %lx\ndifficulty %d\nlast %ld\n", h2_progress.completed, (int)h2_progress.difficulty, h2_progress.last);
	fclose(file);
}

long h2_progress_unlocked(
	void)
{
	long k, unlocked = 1;

	h2_progress_load();
	for (k = 0; k < h2_campaign_level_count(); k++)
	{
		if (h2_progress.completed & (1UL << k))
			unlocked = k + 2;
	}
	return unlocked > h2_campaign_level_count() ? h2_campaign_level_count() : unlocked;
}

long h2_progress_level_won(
	char const *map_name)
{
	long level = h2_campaign_level_index(map_name);

	h2_progress_load();
	if (level != NONE)
	{
		h2_progress.completed |= 1UL << level;
		h2_progress.difficulty = main_get_difficulty();
		h2_progress_save();
		platform_log("h2 progress: %s won on difficulty %d: %ld of %ld levels unlocked", h2_campaign_level_name(level),
			(int)h2_progress.difficulty, h2_progress_unlocked(), h2_campaign_level_count());
		/* (the end of the campaign: nothing left to resume) */
		if (level + 1 >= h2_campaign_level_count())
		{
			game_state_port_discard_h2_save();
			platform_log("h2 progress: the campaign is finished: the saved game is let go");
		}
	}
	return h2_progress_unlocked();
}

boolean h2_progress_saved_game(
	char *scenario_path,
	long size,
	short *difficulty,
	char *level_name,
	long level_name_size)
{
	char map[256];
	boolean corrupted = FALSE, found;
	boolean previous = game_state_set_persistent_storage_custom(2);
	long level;

	map[0] = 0;
	found = game_state_persistent_storage_exists() && game_state_test_persistent_storage(map, difficulty, &corrupted) && !corrupted;
	game_state_set_persistent_storage_custom(previous);
	level = found ? h2_campaign_level_index(map) : NONE;
	if (level == NONE)
		return FALSE;
	snprintf(scenario_path, size, "scenarios\\solo\\%s\\%s", h2_campaign_level_name(level), h2_campaign_level_name(level));
	if (level_name)
		snprintf(level_name, level_name_size, "%s", h2_campaign_level_name(level));
	return TRUE;
}

short h2_progress_difficulty(
	void)
{
	h2_progress_load();
	return h2_progress.difficulty;
}

void h2_progress_difficulty_set(
	short difficulty)
{
	h2_progress_load();
	if (difficulty >= 0 && difficulty < NUMBER_OF_GAME_DIFFICULTY_LEVELS && difficulty != h2_progress.difficulty)
	{
		h2_progress.difficulty = difficulty;
		h2_progress_save();
	}
}

void h2_progress_level_begun(
	char const *map_name,
	short difficulty)
{
	long level = h2_campaign_level_index(map_name);

	h2_progress_load();
	h2_progress.last = level;
	h2_progress.difficulty = difficulty;
	h2_progress_save();
	game_state_port_discard_h2_save();
	platform_log("h2 progress: %s begun on difficulty %d: the saved game is let go", map_name ? map_name : "?", (int)difficulty);
}
