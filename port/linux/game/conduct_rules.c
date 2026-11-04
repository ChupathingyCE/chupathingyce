/*
CONDUCT_RULES.C

A dedicated server's betrayal and AFK limits' rules (conduct_rules.h).
*/

#include "conduct_rules.h"

#include <math.h>
#include <string.h>

/* ---------- constants */

/* how far a player must turn (radians), or hold the stick out, for it to
be input: less is a controller's drift, or nothing */
#define CONDUCT_TURN_EPSILON 0.0005f
#define CONDUCT_STICK_EPSILON 0.1f
#define CONDUCT_TRIGGER_EPSILON 0.01f

/* ---------- public code */

int conduct_betrayal_action(
	int limit,
	int previous,
	int now)
{
	if (limit <= 0 || now <= previous)
		return _conduct_none;
	if (now >= limit && previous < limit)
		return _conduct_kick;
	if (limit > 1 && now >= limit - 1 && previous < limit - 1)
		return _conduct_warn;
	return _conduct_none;
}

int conduct_note_automatic_kick(
	struct conduct_kick_history *history,
	char const *key,
	long long now)
{
	int repeat = 0;
	int free_index = -1;
	int oldest_index = 0;
	int index;

	if (!history)
		return 0;
	for (index = 0; index < CONDUCT_HISTORY_SIZE; index++)
	{
		long long time = history->entries[index].time;

		/* (a day old, or from a clock that went back: forgotten) */
		if (time && (now - time >= CONDUCT_REPEAT_SECONDS || time > now))
		{
			history->entries[index].time = 0;
			history->entries[index].key[0] = 0;
			time = 0;
		}
		if (!time)
		{
			if (free_index < 0)
				free_index = index;
			continue;
		}
		if (key && key[0] && !strcmp(history->entries[index].key, key))
			repeat = 1;
		if (time < history->entries[oldest_index].time || !history->entries[oldest_index].time)
			oldest_index = index;
	}
	if (!key || !key[0])
		return 0;
	index = free_index >= 0 ? free_index : oldest_index;
	strncpy(history->entries[index].key, key, CONDUCT_KEY_SIZE - 1);
	history->entries[index].key[CONDUCT_KEY_SIZE - 1] = 0;
	history->entries[index].time = now;
	return repeat;
}

int conduct_input_active(
	struct conduct_input const *previous,
	struct conduct_input const *now)
{
	if (!now)
		return 0;
	/* (held out: moving, wherever it was before) */
	if (fabsf(now->throttle_i) > CONDUCT_STICK_EPSILON || fabsf(now->throttle_j) > CONDUCT_STICK_EPSILON)
		return 1;
	if (!previous)
		return 0;
	if (now->control_flags != previous->control_flags || now->weapon_index != previous->weapon_index ||
		now->grenade_index != previous->grenade_index || now->zoom_level != previous->zoom_level)
	{
		return 1;
	}
	if (fabsf(now->primary_trigger - previous->primary_trigger) > CONDUCT_TRIGGER_EPSILON)
		return 1;
	/* (so written that a value not a number is no turn) */
	if (fabsf(now->yaw - previous->yaw) > CONDUCT_TURN_EPSILON ||
		fabsf(now->pitch - previous->pitch) > CONDUCT_TURN_EPSILON)
	{
		return 1;
	}
	return 0;
}

int conduct_afk_action(
	int minutes,
	unsigned long idle_milliseconds)
{
	unsigned long limit;

	if (minutes <= 0)
		return _conduct_none;
	if (minutes > CONDUCT_MAXIMUM_AFK_MINUTES)
		minutes = CONDUCT_MAXIMUM_AFK_MINUTES;
	limit = (unsigned long)minutes * 60UL * 1000UL;
	if (idle_milliseconds >= limit)
		return _conduct_kick;
	if (limit > CONDUCT_AFK_WARNING_MILLISECONDS && idle_milliseconds >= limit - CONDUCT_AFK_WARNING_MILLISECONDS)
		return _conduct_warn;
	return _conduct_none;
}
