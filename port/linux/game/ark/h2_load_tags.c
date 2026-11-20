/*
H2_LOAD_TAGS.C

Tags made as a level loads: see h2_load_tags.h.
*/

#include "cseries.h"
#include "h2_load_tags.h"

#include <stdlib.h>
#include <string.h>

/* (tag_validate.c) the makers' buffer tables, checked with the map's */
extern void tag_validate_extra_buffers_clear(void);
extern boolean tag_validate_extra_buffers_add(void *vertex_buffers, long vertex_buffer_count, void *index_buffers,
	long index_buffer_count);
/* (cache_files_windows.c) */
struct cache_file_tag_header;
extern void tags_header_register_vertex_and_index_buffers(struct cache_file_tag_header *header);

/* the makers' vertex and index buffer tables (h2_load_tags_buffers_add):
registered once the tags are checked (h2_load_tags_buffers_register) */
#define MAXIMUM_BUFFER_TABLES 4

static struct
{
	void *vertex_buffers;
	long vertex_buffer_count;
	void *index_buffers;
	long index_buffer_count;
} buffer_tables[MAXIMUM_BUFFER_TABLES];
static long buffer_table_count;

void platform_log(const char *format, ...);

enum
{
	INSTANCE_SIZE = 0x20,
	RANGE_WORDS = 4,
	/* (the room after the map's tags a level made with no record may not
	have: none is taken) */
	ALIGNMENT = 16,
};

struct h2_load_tags
{
	unsigned char *tags;
	unsigned long used;          /* (offsets from tags: the room's next byte) */
	unsigned long limit;
	unsigned char *instances;
	long count;
	long first, range_count;     /* (the range being made) */
	boolean failed;
};

static unsigned long read_u32(unsigned char const *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static void write_u32(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8), p[2] = (unsigned char)(v >> 16), p[3] = (unsigned char)(v >> 24);
}

/* ---------- the builder's */

boolean h2_load_tags_header_add(unsigned char *header, unsigned long maker, long first, long count, unsigned long reserve)
{
	unsigned char *record = header + H2_LOAD_TAGS_HEADER_OFFSET;
	unsigned long ranges = read_u32(record) == H2_LOAD_TAGS_MAGIC ? read_u32(record + 4) : 0;
	unsigned char *range;

	if (ranges >= H2_LOAD_TAGS_MAXIMUM_RANGES || first < 0 || count <= 0)
		return FALSE;
	write_u32(record, H2_LOAD_TAGS_MAGIC);
	write_u32(record + 4, ranges + 1);
	range = record + 8 + 4 * RANGE_WORDS * ranges;
	write_u32(range, maker);
	write_u32(range + 4, (unsigned long)first);
	write_u32(range + 8, (unsigned long)count);
	write_u32(range + 12, reserve);
	return TRUE;
}

unsigned long h2_load_tags_header_reserve(unsigned char const *header)
{
	unsigned char const *record = header + H2_LOAD_TAGS_HEADER_OFFSET;
	unsigned long ranges, k, reserve = 0;

	if (read_u32(record) != H2_LOAD_TAGS_MAGIC)
		return 0;
	ranges = read_u32(record + 4);
	for (k = 0; k < ranges && k < H2_LOAD_TAGS_MAXIMUM_RANGES; k++)
		reserve += read_u32(record + 8 + 4 * RANGE_WORDS * k + 12);
	return reserve;
}

/* ---------- a maker's */

void *h2_load_tags_alloc(struct h2_load_tags *make, unsigned long size)
{
	unsigned long at = (make->used + ALIGNMENT - 1) & ~(unsigned long)(ALIGNMENT - 1);

	if (make->failed || size > make->limit || at > make->limit - size)
	{
		if (!make->failed)
			platform_log("h2 load tags: no room for %lu bytes more (%lu of %lu used)", size, make->used, make->limit);
		make->failed = TRUE;
		return NULL;
	}
	make->used = at + size;
	memset(make->tags + at, 0, size);
	return make->tags + at;
}

boolean h2_load_tags_place(struct h2_load_tags *make, long index, void *root)
{
	unsigned char *p = root;

	if (index < make->first || index >= make->first + make->range_count || index >= make->count || !p ||
		p < make->tags || p >= make->tags + make->used)
	{
		platform_log("h2 load tags: index %ld placed outside its range or room", index);
		make->failed = TRUE;
		return FALSE;
	}
	write_u32(make->instances + INSTANCE_SIZE * (unsigned long)index + 20, (unsigned long)p);
	return TRUE;
}

unsigned long h2_load_tags_link(struct h2_load_tags const *make)
{
	return (unsigned long)make->tags;
}

unsigned char *h2_load_tags_instance(struct h2_load_tags const *make, long index)
{
	return index >= 0 && index < make->count ? make->instances + INSTANCE_SIZE * (unsigned long)index : NULL;
}

long h2_load_tags_count(struct h2_load_tags const *make)
{
	return make->count;
}

void h2_load_tags_room(struct h2_load_tags const *make, unsigned long *used, unsigned long *limit)
{
	*used = make->used;
	*limit = make->limit;
}

void h2_load_tags_room_used(struct h2_load_tags *make, unsigned long used)
{
	if (used < make->used || used > make->limit)
		make->failed = TRUE;
	else
		make->used = used;
}

/* ---------- the makers */

/* (HALO_H2_LOAD_TAGS_TEST's) a string list of one string, made here */
static boolean test_make(struct h2_load_tags *make, long first, long count, char const *map_name)
{
	static char const text[] = "made at load";
	long k;

	for (k = 0; k < count; k++)
	{
		unsigned char *root = h2_load_tags_alloc(make, 12);
		unsigned char *element = h2_load_tags_alloc(make, 20);
		unsigned char *data = h2_load_tags_alloc(make, sizeof(text));

		if (!root || !element || !data)
			return FALSE;
		memcpy(data, text, sizeof(text));
		write_u32(root, 1);
		write_u32(root + 4, (unsigned long)element);
		write_u32(element, sizeof(text));
		write_u32(element + 12, (unsigned long)data);
		if (!h2_load_tags_place(make, first + k, root))
			return FALSE;
	}
	platform_log("h2 load tags: %s: %ld test string lists made", map_name, count);
	return TRUE;
}

/* each maker by its name (one more: its case here) */
static boolean h2_load_tags_maker_call(unsigned long maker, struct h2_load_tags *make, long first, long count, char const *map_name)
{
	switch (maker)
	{
	case H2_LOAD_TAGS_MAKER_TEST:
		return test_make(make, first, count, map_name);
	case H2_LOAD_TAGS_MAKER_EFFECTS:
	{
		extern boolean h2_effects_load_make(struct h2_load_tags *make, long first, long count, char const *map_name);

		return h2_effects_load_make(make, first, count, map_name);
	}
	case H2_LOAD_TAGS_MAKER_MODELS:
	{
		extern boolean h2_models_load_make(struct h2_load_tags *make, long first, long count, char const *map_name);

		return h2_models_load_make(make, first, count, map_name);
	}
	case H2_LOAD_TAGS_MAKER_WEAPON_MODELS:
	{
		extern boolean h2_models_weapons_load_make(struct h2_load_tags *make, long first, long count, char const *map_name);

		return h2_models_weapons_load_make(make, first, count, map_name);
	}
	}
	platform_log("h2 load tags: %s: no maker '%c%c%c%c' in this game", map_name, (int)(maker >> 24 & 255),
		(int)(maker >> 16 & 255), (int)(maker >> 8 & 255), (int)(maker & 255));
	return FALSE;
}

boolean h2_load_tags_make(unsigned char const *file_header, unsigned char *tags, unsigned long *tag_data_size,
	unsigned long tag_cache_size, char const *map_name)
{
	unsigned char const *record = file_header + H2_LOAD_TAGS_HEADER_OFFSET;
	struct h2_load_tags make;
	unsigned long ranges, k, recorded, reserve, bsp_room;
	long made = 0;

	/* (the last level's tables forgotten, whatever this map is) */
	buffer_table_count = 0;
	tag_validate_extra_buffers_clear();
	if (read_u32(record) != H2_LOAD_TAGS_MAGIC)
		return TRUE;
	ranges = read_u32(record + 4);
	reserve = h2_load_tags_header_reserve(file_header);
	recorded = read_u32(file_header + 0x730);
	memset(&make, 0, sizeof(make));
	make.tags = tags;
	make.used = *tag_data_size;
	make.count = (long)read_u32(tags + 12);
	make.instances = tags + (read_u32(tags) - (unsigned long)tags);
	/* (the structure BSP loads at the top: its room what the header's tag
	cache room has past the tags and the ranges' reserve) */
	bsp_room = recorded > *tag_data_size + reserve ? recorded - *tag_data_size - reserve : 0;
	make.limit = tag_cache_size > bsp_room ? tag_cache_size - bsp_room : 0;
	if (ranges > H2_LOAD_TAGS_MAXIMUM_RANGES || make.count <= 0 || read_u32(tags) < (unsigned long)tags ||
		read_u32(tags) - (unsigned long)tags > *tag_data_size - INSTANCE_SIZE * (unsigned long)make.count || !recorded)
	{
		platform_log("h2 load tags: %s: its header's ranges or its tag table cannot be trusted", map_name);
		return FALSE;
	}
	for (k = 0; k < ranges && !make.failed; k++)
	{
		unsigned char const *range = record + 8 + 4 * RANGE_WORDS * k;
		unsigned long maker = read_u32(range);
		long first = (long)read_u32(range + 4), count = (long)read_u32(range + 8), i;
		boolean empty;

		if (first < 0 || count <= 0 || first > make.count - count)
		{
			platform_log("h2 load tags: %s: a range past its tags (%ld, %ld of %ld)", map_name, first, count, make.count);
			return FALSE;
		}
		/* (a range the level already has the data of - made by a builder that
		saved it -: left as it is) */
		/* (made when any of it has none: its maker writes them all) */
		empty = FALSE;
		for (i = first; i < first + count && !empty; i++)
			empty = read_u32(make.instances + INSTANCE_SIZE * (unsigned long)i + 20) == 0;
		if (!empty)
			continue;
		make.first = first;
		make.range_count = count;
		if (!h2_load_tags_maker_call(maker, &make, first, count, map_name) || make.failed)
		{
			platform_log("h2 load tags: %s: its tags %ld..%ld could not be made", map_name, first, first + count - 1);
			return FALSE;
		}
		for (i = first; i < first + count; i++)
		{
			if (!read_u32(make.instances + INSTANCE_SIZE * (unsigned long)i + 20))
			{
				platform_log("h2 load tags: %s: its tag %ld was not made", map_name, i);
				return FALSE;
			}
		}
		made += count;
	}
	if (made)
		platform_log("h2 load tags: %s: %ld tags made at load, %lu KB (of %lu KB reserved)", map_name, made,
			(make.used - *tag_data_size) / 1024, reserve / 1024);
	*tag_data_size = (make.used + ALIGNMENT - 1) & ~(unsigned long)(ALIGNMENT - 1);
	return TRUE;
}

boolean h2_load_tags_buffers_add(void *vertex_buffers, long vertex_buffer_count, void *index_buffers, long index_buffer_count)
{
	if (buffer_table_count >= MAXIMUM_BUFFER_TABLES ||
		!tag_validate_extra_buffers_add(vertex_buffers, vertex_buffer_count, index_buffers, index_buffer_count))
	{
		return FALSE;
	}
	buffer_tables[buffer_table_count].vertex_buffers = vertex_buffers;
	buffer_tables[buffer_table_count].vertex_buffer_count = vertex_buffer_count;
	buffer_tables[buffer_table_count].index_buffers = index_buffers;
	buffer_tables[buffer_table_count].index_buffer_count = index_buffer_count;
	buffer_table_count++;

	return TRUE;
}

void h2_load_tags_buffers_register(void)
{
	long k;

	for (k = 0; k < buffer_table_count; k++)
	{
		/* (a tag header of the table's counts and pointers alone: the
		counts at 16 and 24, the tables at 20 and 28, as the level's are
		registered) */
		unsigned char header[0x24];

		memset(header, 0, sizeof(header));
		write_u32(header + 16, (unsigned long)buffer_tables[k].vertex_buffer_count);
		write_u32(header + 20, (unsigned long)buffer_tables[k].vertex_buffers);
		write_u32(header + 24, (unsigned long)buffer_tables[k].index_buffer_count);
		write_u32(header + 28, (unsigned long)buffer_tables[k].index_buffers);
		tags_header_register_vertex_and_index_buffers((struct cache_file_tag_header *)header);
	}
	buffer_table_count = 0;
}
