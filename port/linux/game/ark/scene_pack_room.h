/*
SCENE_PACK_ROOM.H

The tag cache's free room as the scene packs share it with a structure bsp, as
data, with no game in it, so that it is tested on its own
(port/vita/tests/scene_pack_room_test.c). A structure bsp's load clears the tag
cache after the map's tags: never the ranges kept here (the packs' tags after
the tags and after the bsps, a build's scratch).
*/

#ifndef __SCENE_PACK_ROOM_H
#define __SCENE_PACK_ROOM_H

enum
{
	/* the packs before the bsps, the packs after them, a build's scratch */
	SCENE_PACK_ROOM_KEPT_RANGES = 4,
};

/* bytes from first to end, in the same block; first == end: none */
struct scene_pack_room_range
{
	unsigned char *first;
	unsigned char *end;
};

/* the bytes from first to end filled, but the kept ranges (any order, they may
overlap, or reach past either end); how many bytes were filled */
unsigned long scene_pack_room_fill(
	unsigned char *first,
	unsigned char *end,
	struct scene_pack_room_range const *kept,
	int kept_count,
	unsigned char fill);

#endif
