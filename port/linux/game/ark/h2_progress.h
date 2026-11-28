/*
H2_PROGRESS.H

port: Halo 2's campaign progress for the profile: which levels are done
(and so which the Select Level screen lets be played - Halo 2 lists the
levels up to the next one not yet reached), the difficulty last played,
and whether a saved game is there to Resume. The profile's directory
keeps it, in campaign_h2.txt beside the campaign save (savegame_h2.bin,
which every checkpoint of a campaign level writes: game_state_xbox.c).
*/

#ifndef __H2_PROGRESS_H
#define __H2_PROGRESS_H
#pragma once

#include "cseries.h"

/* how many of the campaign's levels (h2_campaign.h) may be played: the
first, and the one after the furthest level won; at least 1 */
long h2_progress_unlocked(
	void);

/* a campaign level won (its scenario's path, tag name or name): kept, the
next level unlocked. The last level won ends the campaign: the saved game
is let go. Returns the number of levels now unlocked. */
long h2_progress_level_won(
	char const *map_name);

/* the saved game Resume would load: the scenario's path to start and the
difficulty it was made on. FALSE with none (no save, a damaged one, one of
another build's) */
boolean h2_progress_saved_game(
	char *scenario_path,
	long size,
	short *difficulty,
	char *level_name,
	long level_name_size);

/* the difficulty last chosen for a campaign level (Normal at first) */
short h2_progress_difficulty(
	void);
void h2_progress_difficulty_set(
	short difficulty);

/* a level begun from the menu's New Campaign or Select Level: the saved
game is let go (Halo 2 warns that the saved progress is lost) */
void h2_progress_level_begun(
	char const *map_name,
	short difficulty);

#endif
