/*
CE_SOUND_POOL.H
Where a Custom Edition map's decoded sounds lie in the file they are decoded
into (ce_resources.c, z:\ce_sounds.pcm), which holds no more than its
budget: each sound one piece of the file, in it or not. A sound to be put
in it goes in the lowest gap it fits; while none is big enough, the sound
read longest ago that is not busy (playing, or read a moment ago) is moved
out, and its piece is free again. Whole in this header, so that its test
(port/linux/tests/ce_sound_pool_test.c) builds it alone.
*/
#ifndef CE_SOUND_POOL_H
#define CE_SOUND_POOL_H

#include <stdlib.h>

struct ce_sound_pool_entry
{
	/* its piece of the file, while it is in it */
	unsigned long base;
	unsigned long length;
	/* when it was last read (any clock that only goes up) */
	unsigned long last_used;
	int resident;
};

/* whether an entry may not be moved out now */
typedef int (*ce_sound_pool_busy_proc)(long entry, void *context);

static struct ce_sound_pool_entry const *ce_sound_pool_sorting;

static int ce_sound_pool_by_base(void const *a, void const *b)
{
	unsigned long base_a = ce_sound_pool_sorting[*(long const *)a].base;
	unsigned long base_b = ce_sound_pool_sorting[*(long const *)b].base;

	return base_a < base_b ? -1 : base_a > base_b ? 1 : 0;
}

/* the lowest gap of length bytes in [0, capacity) between the resident
entries but `placing`, or -1 (order: scratch room for count entries) */
static long ce_sound_pool_gap(struct ce_sound_pool_entry const *entries, long count, long placing,
	unsigned long capacity, unsigned long length, long *order)
{
	long resident = 0, index;
	unsigned long at = 0;

	if (length > capacity)
		return -1;
	for (index = 0; index < count; index++)
	{
		if (index != placing && entries[index].resident)
			order[resident++] = index;
	}
	ce_sound_pool_sorting = entries;
	qsort(order, (size_t)resident, sizeof(*order), ce_sound_pool_by_base);
	for (index = 0; index < resident; index++)
	{
		struct ce_sound_pool_entry const *entry = &entries[order[index]];

		if (entry->base >= at && entry->base - at >= length)
			return (long)at;
		if (entry->base + entry->length > at)
			at = entry->base + entry->length;
	}
	return at <= capacity && capacity - at >= length ? (long)at : -1;
}

/* entry `placing` put in the file: its base, or -1 if no room can be made.
*evicted counts the entries moved out for it */
static long ce_sound_pool_place(struct ce_sound_pool_entry *entries, long count, long placing,
	unsigned long capacity, ce_sound_pool_busy_proc busy, void *context, long *evicted)
{
	long *order = malloc((size_t)(count ? count : 1) * sizeof(*order));
	long base = -1;

	if (!order)
		return -1;
	for (;;)
	{
		long index, oldest = -1;

		base = ce_sound_pool_gap(entries, count, placing, capacity, entries[placing].length, order);
		if (base >= 0)
			break;
		for (index = 0; index < count; index++)
		{
			if (index == placing || !entries[index].resident || (busy && busy(index, context)))
				continue;
			if (oldest < 0 || entries[index].last_used < entries[oldest].last_used)
				oldest = index;
		}
		if (oldest < 0)
			break;
		entries[oldest].resident = 0;
		if (evicted)
			(*evicted)++;
	}
	free(order);
	if (base >= 0)
	{
		entries[placing].base = (unsigned long)base;
		entries[placing].resident = 1;
	}
	return base;
}

#endif
