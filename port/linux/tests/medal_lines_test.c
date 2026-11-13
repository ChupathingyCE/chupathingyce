/*
MEDAL_LINES_TEST.C

The kill feed's medals (port/linux/src/medal_lines.h): which line each goes
with. Built and run by tools/test_medal_rules.py.
*/

#include "medal_lines.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do { if (!(condition)) { printf("%s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)

int main(void)
{
	struct medal_lines lines;
	int slot;

	/* two kills in one tick (one grenade): both kills' medals wait, the
	first line printed that tick takes them all, the second none */
	memset(&lines, 0, sizeof(lines));
	medal_lines_earn(&lines, 3, 100);
	medal_lines_earn(&lines, 7, 100);
	medal_lines_earn(&lines, 9, 100);
	CHECK(!medal_lines_leftover(&lines, 100));
	CHECK(medal_lines_printed(&lines, 100, 1, 0));
	CHECK(!medal_lines_printed(&lines, 100, 2, 0));
	slot = medal_lines_find(&lines, 100, 1);
	CHECK(slot >= 0 && lines.lines[slot].count == 3 && lines.lines[slot].icons[0] == 3 &&
		lines.lines[slot].icons[2] == 9);
	CHECK(medal_lines_find(&lines, 100, 2) < 0);

	/* a line of another tick takes none */
	medal_lines_earn(&lines, 4, 200);
	CHECK(!medal_lines_printed(&lines, 201, 3, 0));
	CHECK(lines.pending_count == 1);

	/* leftovers: no line in their tick, two ticks before a frame. They are
	seen as such, and their own line takes them whatever its time */
	CHECK(medal_lines_leftover(&lines, 202));
	CHECK(medal_lines_printed(&lines, 202, 4, 1));
	slot = medal_lines_find(&lines, 202, 4);
	CHECK(slot >= 0 && lines.lines[slot].count == 1 && lines.lines[slot].icons[0] == 4);
	CHECK(!medal_lines_leftover(&lines, 202));
	/* (and with nothing waiting, a leftovers' line takes nothing: no blank
	line is printed for none) */
	CHECK(!medal_lines_printed(&lines, 203, 5, 1));

	/* a new tick's medals start their own wait */
	medal_lines_earn(&lines, 1, 300);
	medal_lines_earn(&lines, 2, 301);
	CHECK(lines.pending_count == 1 && lines.pending[0] == 2);

	/* a line holds at most MEDAL_LINES_PER_LINE */
	memset(&lines, 0, sizeof(lines));
	for (slot = 0; slot < MEDAL_LINES_PER_LINE + 3; slot++)
		medal_lines_earn(&lines, slot, 400);
	CHECK(lines.pending_count == MEDAL_LINES_PER_LINE);

	/* the oldest line's medals give way after MEDAL_LINES_KEPT lines */
	memset(&lines, 0, sizeof(lines));
	for (slot = 0; slot <= MEDAL_LINES_KEPT; slot++)
	{
		medal_lines_earn(&lines, 1, 500 + slot);
		CHECK(medal_lines_printed(&lines, 500 + slot, (short)slot, 0));
	}
	CHECK(medal_lines_find(&lines, 500, 0) < 0);
	CHECK(medal_lines_find(&lines, 500 + MEDAL_LINES_KEPT, (short)MEDAL_LINES_KEPT) >= 0);

	if (failures)
		printf("%d failed\n", failures);
	return failures != 0;
}
