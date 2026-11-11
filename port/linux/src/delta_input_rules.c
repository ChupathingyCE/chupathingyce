/*
DELTA_INPUT_RULES.C

DELTA's keys and Back held in the menus (delta_input_rules.h). No SDL, no
game.
*/

#include "delta_input_rules.h"

#include <stdio.h>
#include <string.h>

int delta_keys_split(const char *list, char names[][DELTA_KEY_NAME_SIZE], int most)
{
	int count = 0;

	while (list && *list && count < most)
	{
		size_t length = strcspn(list, ",");
		const char *start = list, *end = list + length;

		while (start < end && (*start == ' ' || *start == '\t'))
			start++;
		while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
			end--;
		if (end > start && (size_t)(end - start) < DELTA_KEY_NAME_SIZE)
		{
			memcpy(names[count], start, (size_t)(end - start));
			names[count][end - start] = 0;
			count++;
		}
		list += length;
		if (*list == ',')
			list++;
	}
	return count;
}

/* an F key's name: F and a number */
static int f_key(const char *name)
{
	return (name[0] == 'F' || name[0] == 'f') && name[1] >= '1' && name[1] <= '9' &&
		(!name[2] || (name[2] >= '0' && name[2] <= '9' && !name[3]));
}

void delta_key_shown(const char *list, int apple, char *shown, int size)
{
	char names[DELTA_KEYS_MOST][DELTA_KEY_NAME_SIZE];
	int count = delta_keys_split(list, names, DELTA_KEYS_MOST), index, chosen = 0;

	if (size <= 0)
		return;
	for (index = 0; apple && index < count; index++)
	{
		if (!f_key(names[index]))
		{
			chosen = index;
			break;
		}
	}
	snprintf(shown, (size_t)size, "%s", count ? names[chosen] : "F3");
}

int delta_back_hold_step(struct delta_back_hold *hold, int back_down, int armed, unsigned long now)
{
	int result = 0;

	if (back_down && !hold->held)
	{
		hold->held = 1;
		hold->since = now;
		hold->opened = 0;
		/* (not where a hold opens DELTA: Back is the menu's or the game's,
		as it always was) */
		if (!armed)
			hold->opened = -1;
	}
	if (back_down)
	{
		/* (a press that began where a hold opens nothing goes through) */
		if (hold->opened < 0)
			return DELTA_BACK_PASS;
		if (!hold->opened && armed && now - hold->since >= DELTA_BACK_HOLD)
		{
			hold->opened = 1;
			result = DELTA_BACK_OPEN;
		}
		/* (no longer where a hold opens DELTA, the menus gone under it:
		the press is theirs again) */
		else if (!hold->opened && !armed)
			hold->opened = -1;
		return result;
	}
	/* let go: a tap (one that didn't open DELTA) is the menu's now */
	if (hold->held)
	{
		hold->held = 0;
		if (!hold->opened && armed)
		{
			hold->tapping = 1;
			hold->tap_since = now;
			hold->tap_reads = 0;
		}
		hold->opened = 0;
	}
	if (hold->tapping)
	{
		if (now - hold->tap_since < DELTA_BACK_TAP || hold->tap_reads < DELTA_BACK_TAP_READS)
		{
			hold->tap_reads++;
			return DELTA_BACK_PASS;
		}
		hold->tapping = 0;
	}
	return 0;
}

void delta_back_hold_cancel(struct delta_back_hold *hold)
{
	hold->tapping = 0;
	/* (the press is someone else's: no DELTA, no tap when it ends) */
	if (hold->held && !hold->opened)
		hold->opened = 2;
}

int delta_hint_due(int shown, int menu_item)
{
	return !menu_item && shown >= 0 && shown < DELTA_HINT_TIMES;
}

void delta_hint_text(char *text, int size, int pad, const char *key, int guide_button, int hold_back)
{
	if (size <= 0)
		return;
	if (pad && hold_back)
		snprintf(text, (size_t)size, "Hold Back for the Delta Guide");
	else if (pad && guide_button)
		snprintf(text, (size_t)size, "Press the Guide button");
	else
		snprintf(text, (size_t)size, "Press %s for the Delta Guide", key && key[0] ? key : "F3");
}

void delta_hint_text_for(char *text, int size, int pad, const char *key, int guide_button, int hold_back, int apple)
{
	delta_hint_text(text, size, pad, key, guide_button && !apple, hold_back);
}
