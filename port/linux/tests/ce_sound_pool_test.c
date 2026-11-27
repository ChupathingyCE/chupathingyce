/*
CE_SOUND_POOL_TEST.C
The decoded sounds' file (port/linux/game/ce_sound_pool.h): filled to its
budget, a new sound still goes in, moving out the sound read longest ago
that is not playing, never one that is playing or was just read; and when
every sound in it is busy, a new one does not go in. Run by
tools/test_ce_sound_pool.py.
*/
#include <stdio.h>

#include "../game/ce_sound_pool.h"

#define MEGABYTE 0x100000UL
#define BUDGET (512 * MEGABYTE)

static int failures;

#define CHECK(condition) \
	do { if (!(condition)) { printf("%s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)

/* (the entries playing: a flag each) */
static int playing[16];

static int busy(long entry, void *context)
{
	(void)context;
	return playing[entry];
}

static int overlaps(struct ce_sound_pool_entry const *entries, long count)
{
	long a, b;

	for (a = 0; a < count; a++)
	{
		for (b = a + 1; b < count; b++)
		{
			if (entries[a].resident && entries[b].resident &&
				entries[a].base < entries[b].base + entries[b].length &&
				entries[b].base < entries[a].base + entries[a].length)
			{
				return 1;
			}
		}
	}
	return 0;
}

int main(void)
{
	struct ce_sound_pool_entry entries[8];
	long index, evicted = 0, base;

	/* the file filled: four sounds of 128 MB, read at times 40, 10, 30, 20 */
	for (index = 0; index < 4; index++)
	{
		entries[index].base = (unsigned long)index * 128 * MEGABYTE;
		entries[index].length = 128 * MEGABYTE;
		entries[index].resident = 1;
	}
	entries[0].last_used = 40;
	entries[1].last_used = 10;
	entries[2].last_used = 30;
	entries[3].last_used = 20;
	/* a new sound of 100 MB, not in it */
	entries[4].length = 100 * MEGABYTE;
	entries[4].resident = 0;
	entries[4].last_used = 0;
	/* the one read longest ago (1) is playing: the next oldest (3) goes */
	playing[1] = 1;
	base = ce_sound_pool_place(entries, 5, 4, BUDGET, busy, NULL, &evicted);
	CHECK(base == (long)(384 * MEGABYTE));
	CHECK(entries[4].resident);
	CHECK(!entries[3].resident);
	CHECK(entries[0].resident && entries[1].resident && entries[2].resident);
	CHECK(evicted == 1);
	CHECK(!overlaps(entries, 5));

	/* a sound too big for any gap: sounds go, oldest not busy first, until
	one is big enough (2, 0, then 4; 1 stays, playing) */
	entries[5].length = 250 * MEGABYTE;
	entries[5].resident = 0;
	entries[4].last_used = 50;
	evicted = 0;
	base = ce_sound_pool_place(entries, 6, 5, BUDGET, busy, NULL, &evicted);
	CHECK(base == (long)(256 * MEGABYTE));
	CHECK(entries[5].resident);
	CHECK(entries[1].resident);
	CHECK(!entries[0].resident && !entries[2].resident && !entries[4].resident);
	CHECK(evicted == 3);
	CHECK(!overlaps(entries, 6));

	/* every sound in the file busy: a new one does not go in, and none moves */
	for (index = 0; index < 6; index++)
		playing[index] = entries[index].resident;
	entries[6].length = 200 * MEGABYTE;
	entries[6].resident = 0;
	evicted = 0;
	base = ce_sound_pool_place(entries, 7, 6, BUDGET, busy, NULL, &evicted);
	CHECK(base == -1);
	CHECK(evicted == 0 && !entries[6].resident);
	CHECK(entries[1].resident && entries[5].resident);

	/* bigger than the whole file: never */
	entries[7].length = BUDGET + 1;
	entries[7].resident = 0;
	CHECK(ce_sound_pool_place(entries, 8, 7, BUDGET, NULL, NULL, NULL) == -1);

	if (failures)
		printf("%d failed\n", failures);
	return failures ? 1 : 0;
}
