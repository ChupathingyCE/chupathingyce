/* Halo 2's per-tick AI scheduling, on a Halo 2 level.

Halo 2's actor update loop (the Xbox decompilation, retail 0x1e2f50 in
src/unknown_1e3920.cpp) gives its PERCEPTION SLICE to 5 actors a tick: each
tick every active actor's slice flag is cleared, then the next 5 actors past
a rotating cursor get it, wrapping round the list (5 or fewer actors: all of
them, every tick). The prop loop (0x262ba0, src/unknown_264330.cpp) does the
full status update of one prop - the line-of-sight test, the AI's largest
cost in a big fight - only on the actor's slice tick; the prop wait timers
keep growing meanwhile, and the prop the actor is focused on has its
position sampled every tick regardless.

Its highest-waiting service slot (0x1e3790) is Halo CE's rule twice over:
two counters per actor, each up by 1 a tick (no +3 for a charging or
leaping actor, as CE has), the slot given to the actor whose counter beats
the last tick's highest losing counter and 0.5 s (CE's > 15 ticks). The
first slot gates the movement and firing-position searches (CE's
timeslice); the second gates re-picking the actor's tracked props
(0x263210), which CE does on the same single slice.

This engine (CE's lineage) refreshes one prop's status for EVERY actor EVERY
tick: 37 line-of-sight rays a tick in Cairo Station's fights where Halo 2
casts about 5. Kept here, outside the game state: the slice flag and the
second counter per actor slot, stamped with the slot's identifier.
HALO_H2_AI_BUDGET=0: the old behaviour. */

#include "cseries.h"
#include "ai/actors.h"
#include "game/game.h"
#include "h2_rules.h"
#include "h2_ai_budget.h"

#include <stdlib.h>
#include <string.h>

enum
{
	/* (0x1e2f50: `short remaining = 5`) */
	AI_BUDGET_PERCEPTION_SLICE_ACTORS = 5,
	/* (0x1e3790: ticks * rate > 0.5 s) */
	AI_BUDGET_SLOT_TICKS = 15,
};

struct ai_budget_actor
{
	short identifier;
	byte slice;
	byte slot_given;
	short slot_counter;
};

static struct ai_budget_actor budget_actors[HALO_PORT_MAXIMUM_ACTORS];
static long budget_cursor = NONE;
/* (the second slot's g_4f55d0+0xc/+0xe/+0x10) */
static short budget_slot_last_highest, budget_slot_current_highest;
static boolean budget_slot_given_this_tick;

boolean h2_ai_budget_active(
	void)
{
	extern volatile unsigned long halo_settings_generation;
	static unsigned long settings_seen;
	static int enabled = -1;

	if (enabled < 0 || settings_seen != halo_settings_generation)
	{
		const char *setting = getenv("HALO_H2_AI_BUDGET");

		settings_seen = halo_settings_generation;
		enabled = setting ? atoi(setting) != 0 : 1;
	}
	return enabled && h2_rules_level_fast();
}

void h2_ai_budget_reset(
	void)
{
	memset(budget_actors, 0, sizeof(budget_actors));
	budget_cursor = NONE;
	budget_slot_last_highest = budget_slot_current_highest = 0;
	budget_slot_given_this_tick = FALSE;
}

static struct ai_budget_actor *budget_actor_get(
	long actor_index)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index);
	short identifier = DATUM_INDEX_TO_IDENTIFIER(actor_index);
	struct ai_budget_actor *entry;

	if (absolute_index < 0 || absolute_index >= HALO_PORT_MAXIMUM_ACTORS)
		return NULL;
	entry = &budget_actors[absolute_index];
	if (entry->identifier != identifier)
	{
		memset(entry, 0, sizeof(*entry));
		entry->identifier = identifier;
	}
	return entry;
}

static long budget_indices[HALO_PORT_MAXIMUM_ACTORS];
static long budget_count;

void h2_ai_budget_tick_begin(
	void)
{
	/* (0x1e2f50: the counters' thresholds move on, the slot is free) */
	budget_slot_last_highest = budget_slot_current_highest;
	budget_slot_current_highest = 0;
	budget_slot_given_this_tick = FALSE;
	budget_count = 0;
}

void h2_ai_budget_tick_note_actor(
	long actor_index)
{
	struct ai_budget_actor *entry = budget_actor_get(actor_index);

	if (!entry)
		return;
	entry->slice = FALSE;
	entry->slot_given = FALSE;
	if (budget_count < HALO_PORT_MAXIMUM_ACTORS)
		budget_indices[budget_count++] = actor_index;
}

void h2_ai_budget_tick_assign(
	void)
{
	long count = budget_count, i, start = 0, remaining;

	if (count == 0)
		return;
	/* (Halo 2 walks its actor array in index order; this engine's actor
	iterator walks encounter by encounter, each one's own list - the cursor
	below counts on index order: without it the same 5 actors past the
	cursor took every slice and the rest never refreshed a prop status
	again - 05a ledge Grunts' player props frozen 30 s, audit5 pt-2) */
	for (i = 1; i < count; i++)
	{
		long key = budget_indices[i], j = i - 1;

		while (j >= 0 && DATUM_INDEX_TO_ABSOLUTE_INDEX(budget_indices[j]) > DATUM_INDEX_TO_ABSOLUTE_INDEX(key))
		{
			budget_indices[j + 1] = budget_indices[j];
			j--;
		}
		budget_indices[j + 1] = key;
	}
	if (count <= AI_BUDGET_PERCEPTION_SLICE_ACTORS)
	{
		for (i = 0; i < count; i++)
			budget_actor_get(budget_indices[i])->slice = TRUE;
		budget_cursor = NONE;
		return;
	}
	/* the next 5 past the cursor (the last actor given a slice), wrapping */
	if (budget_cursor != NONE)
	{
		for (i = 0; i < count; i++)
		{
			if (DATUM_INDEX_TO_ABSOLUTE_INDEX(budget_indices[i]) > DATUM_INDEX_TO_ABSOLUTE_INDEX(budget_cursor))
				break;
		}
		start = i;
	}
	for (remaining = AI_BUDGET_PERCEPTION_SLICE_ACTORS, i = start; remaining > 0; remaining--, i++)
	{
		if (i >= count)
			i = 0;
		budget_actor_get(budget_indices[i])->slice = TRUE;
		budget_cursor = budget_indices[i];
	}
}

boolean h2_ai_budget_perception_slice(
	long actor_index)
{
	struct ai_budget_actor *entry = budget_actor_get(actor_index);

	return !entry || entry->slice;
}

boolean h2_ai_budget_prop_slot(
	long actor_index)
{
	struct ai_budget_actor *entry = budget_actor_get(actor_index);
	boolean result = FALSE;

	if (!entry)
		return TRUE;
	/* (0x1e3790's second counter: +1 a tick; the highest past the last
	tick's highest and 0.5 s takes the slot) */
	if (entry->slot_counter < SHORT_MAX)
		entry->slot_counter++;
	if (!budget_slot_given_this_tick &&
		entry->slot_counter > budget_slot_last_highest &&
		entry->slot_counter > AI_BUDGET_SLOT_TICKS)
	{
		entry->slot_counter = 0;
		budget_slot_given_this_tick = TRUE;
		result = TRUE;
	}
	else if (entry->slot_counter > budget_slot_current_highest)
	{
		budget_slot_current_highest = entry->slot_counter;
	}
	entry->slot_given = result;
	return result;
}

boolean h2_ai_budget_prop_slot_given(
	long actor_index)
{
	struct ai_budget_actor *entry = budget_actor_get(actor_index);

	return entry && entry->slot_given;
}
