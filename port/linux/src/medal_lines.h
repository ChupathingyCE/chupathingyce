/*
MEDAL_LINES.H

Which kill feed line a local player's medals go with (display.medals,
port/linux/game/medal_hud.c). A kill's medals are worked out just before
the game prints the kill's line, in the same tick: they wait here for the
next line printed that tick, and are kept with it by its time and number.
Medals no line took (none was printed in their tick) are leftovers: the
HUD prints them a line of their own, which takes them whatever its time.

Header only, plain C, nothing of the game's: medal_hud.c and the unit tests
(port/linux/tests/medal_lines_test.c) take the same code.
*/

#ifndef MEDAL_LINES_H
#define MEDAL_LINES_H

enum
{
	/* the medals one line holds at most */
	MEDAL_LINES_PER_LINE = 6,
	/* the lines with medals kept (more than a HUD shows) */
	MEDAL_LINES_KEPT = 8,
};

struct medal_lines
{
	/* the medals waiting for their line, and the tick they came in */
	short pending[MEDAL_LINES_PER_LINE];
	short pending_count;
	long pending_time;
	/* the lines that have medals: a message's time and number, its medals */
	struct
	{
		long time;
		short number;
		short count;
		short icons[MEDAL_LINES_PER_LINE];
	} lines[MEDAL_LINES_KEPT];
	short next_line;
};

/* medals waiting that no line of their tick took, by now */
static inline int medal_lines_leftover(struct medal_lines const *lines, long now)
{
	return lines->pending_count > 0 && lines->pending_time != now;
}

/* a medal (its icon) earned at the tick now. Leftovers of an earlier tick
are given their line first (medal_lines_leftover) */
static inline void medal_lines_earn(struct medal_lines *lines, int icon, long now)
{
	if (lines->pending_time != now)
		lines->pending_count = 0;
	lines->pending_time = now;
	if (lines->pending_count < MEDAL_LINES_PER_LINE)
		lines->pending[lines->pending_count++] = (short)icon;
}

/* a line printed (its time and number): the medals waiting go with it if
they came in its tick, or whatever their tick for a leftovers' line
(any_time); whether it took them */
static inline int medal_lines_printed(struct medal_lines *lines, long time, short number, int any_time)
{
	short slot, index;

	if (!lines->pending_count || (!any_time && lines->pending_time != time))
		return 0;
	slot = lines->next_line;
	lines->next_line = (short)((slot + 1) % MEDAL_LINES_KEPT);
	lines->lines[slot].time = time;
	lines->lines[slot].number = number;
	lines->lines[slot].count = lines->pending_count;
	for (index = 0; index < lines->pending_count; index++)
		lines->lines[slot].icons[index] = lines->pending[index];
	lines->pending_count = 0;
	return 1;
}

/* a line's slot (its time and number), -1 for a line with no medals */
static inline int medal_lines_find(struct medal_lines const *lines, long time, short number)
{
	int slot;

	for (slot = 0; slot < MEDAL_LINES_KEPT; slot++)
	{
		if (lines->lines[slot].count && lines->lines[slot].time == time && lines->lines[slot].number == number)
			return slot;
	}
	return -1;
}

#endif
