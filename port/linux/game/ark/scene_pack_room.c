/*
SCENE_PACK_ROOM.C

(scene_pack_room.h)
*/

#include "scene_pack_room.h"

#include <string.h>

unsigned long scene_pack_room_fill(
	unsigned char *first,
	unsigned char *end,
	struct scene_pack_room_range const *kept,
	int kept_count,
	unsigned char fill)
{
	unsigned char *at = first;
	unsigned long filled = 0;

	while (at < end)
	{
		unsigned char *stop = end;
		int index;
		int moved = 0;

		/* (at inside a kept range: past it; else up to the next one) */
		for (index = 0; index < kept_count; index++)
		{
			if (kept[index].first >= kept[index].end)
				continue;
			if (kept[index].first <= at && at < kept[index].end)
			{
				at = kept[index].end;
				moved = 1;
			}
			else if (kept[index].first > at && kept[index].first < stop)
			{
				stop = kept[index].first;
			}
		}
		if (moved)
			continue;
		memset(at, fill, (size_t)(stop - at));
		filled += (unsigned long)(stop - at);
		at = stop;
	}
	return filled;
}
