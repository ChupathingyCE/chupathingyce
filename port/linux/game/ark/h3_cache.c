/*
H3_CACHE.C

An MCC (PC) Halo 3 cache file read for the game (h3_cache.h).

The header (0x4000 bytes, little endian): 'head' (stored "daeh"), version
13; the tag names' count, data offset, data size and offsets' offset at
0x20/0x24/0x28/0x2C and the string ids' count, data offset, data size and
offsets' offset at 0x30/0x34/0x38/0x3C (offsets in the debug section: plus
its mask); the build at 0xA0, the name at 0xC0, the scenario's path at
0xE0; the tag section's virtual base (64-bit) at 0x2E0 and its index's
address at 0x2E8; the four sections' (debug, resource, tag, localization)
offset masks at 0x4CC and addresses and sizes at 0x4DC (a section's file
offset is its address plus its mask); 'foot' at 0x3FFC.
The index: the group count at 0, the groups' address (64-bit) at 8, the
tag count at 0x10, the tags' at 0x18. A group is 16 bytes, its tag first;
a tag 8: its group's index, salt, its 32-bit pointer (an address over 4).
The zone tag: resources at 0x64 (0x40 each: owner datum at 0xC, type at
0x12, control offset and size at 0x14/0x18, location at 0x22, root at
0x24, fixups at 0x28 - offset and value, 8 bytes each), the info (the
controls) size at 0x148 and pointer at 0x154. The play tag: the shared
files at 0x0C (0x108 each: a path first), pages at 0x18 (0x58 each: the
file at 4 - NONE this map, else a shared file's index - the offset in the
file's resource section at 8, compressed and uncompressed sizes at 0xC and
0x10: raw deflate when they differ), locations at 0x3C (0x10 each:
required and optional page, their offsets at 4 and 8).
A string id's index is its low 19 bits, its set the 8 bits above them: the
sets' sizes are a table in the debug section (its count at 0x40, its offset
at 0x44); set 0's first size are the global table's first, set 0 past them
the global table's after every set, set n the global table's after sets 0
to n - 1. shared.map and campaign.map hold resources alone (no tags: their
tag fields 0): they are opened for their pages only.
*/

/* ---------- headers */

#include "h3_cache.h"
#include "../../../third_party/zlib/zlib_prefixed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef malloc
#undef free
#undef realloc
#undef calloc

/* (h3up: the Vita tier - what a Halo 3 level's make allocates is the
builder's own heap's, user RAM outside newlib's 48 MB C heap, as Halo 2's
make's: h2_build_heap.h; the desktop's PC tier as before) */
#define H2_BUILD_HEAP_WRAP
#include "h2_build_heap.h"

#define H3_ALIGN __attribute__((force_align_arg_pointer))

/* ---------- constants */

enum
{
	H3_HEADER_SIGNATURE = 0x68656164,  /* 'head' */
	H3_FOOTER_SIGNATURE = 0x666F6F74,  /* 'foot' */
	H3_MAXIMUM_TAGS = 0x10000,
	H3_MAXIMUM_NAME_BYTES = 16 * 1024 * 1024,
	H3_MAXIMUM_STRINGS = 0x40000,
	H3_MAXIMUM_SHARED = 8,
	H3_BLOCK_SIZE = 0x10000,
	H3_BLOCK_SLOTS = 64,
	H3_SCRATCH_SLOTS = 32,
	H3_ZONE_RESOURCE_SIZE = 0x40,
	H3_PLAY_PAGE_SIZE = 0x58,
	H3_PLAY_LOCATION_SIZE = 0x10,
	H3_PLAY_SHARED_SIZE = 0x108,
	H3_WINDOW = 0x10000,
	H3_STRING_SET_MAXIMUM = 16,
};

/* ---------- structures */

/* a map file's header, as far as reading its sections goes */
struct h3_file
{
	FILE *file;
	unsigned long file_size;
	unsigned long section_offset[4];
	unsigned long section_size[4];
	unsigned long masks[4];
};

struct h3_cache
{
	char directory[H3_CACHE_NAME_SIZE];
	char name[32];
	char scenario[H3_CACHE_NAME_SIZE];
	struct h3_file map;
	unsigned long long virtual_base;

	long tag_count;
	unsigned long *tag_groups;      /* each tag's group tag */
	unsigned long *tag_addresses;   /* each tag's address, 0 none */
	char *names;
	unsigned long names_size;
	long *name_offsets;

	char *strings;
	unsigned long strings_size;
	long *string_offsets;
	long string_count;
	unsigned long string_sets[H3_STRING_SET_MAXIMUM];
	long string_set_count;

	/* the tag section's blocks read so far */
	struct h3_block
	{
		unsigned long index;   /* the block's number + 1, 0 empty */
		unsigned long age;
		unsigned char *data;
	} blocks[H3_BLOCK_SLOTS];
	unsigned long clock;
	unsigned char *scratch[H3_SCRATCH_SLOTS];
	unsigned long scratch_size[H3_SCRATCH_SLOTS];
	long scratch_next;

	/* the resources */
	unsigned long resources;        /* zone's resources' address */
	long resource_count;
	unsigned long info_address;
	unsigned long info_size;
	unsigned long pages;
	long page_count;
	unsigned long locations;
	long location_count;
	long shared_count;
	char shared_names[H3_MAXIMUM_SHARED][64];
	struct h3_file shared[H3_MAXIMUM_SHARED];
	boolean shared_tried[H3_MAXIMUM_SHARED];
};

/* ---------- private code */

H3_ALIGN static unsigned long le32(unsigned char const *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

H3_ALIGN static unsigned long long le64(unsigned char const *p)
{
	return (unsigned long long)le32(p) | ((unsigned long long)le32(p + 4) << 32);
}

H3_ALIGN static void why_set(char *why, long why_size, char const *text)
{
	if (why && why_size > 0)
	{
		strncpy(why, text, why_size - 1);
		why[why_size - 1] = 0;
	}
}

H3_ALIGN static boolean file_read_at(FILE *file, unsigned long offset, void *out, unsigned long size)
{
	return fseek(file, (long)offset, SEEK_SET) == 0 && fread(out, 1, size, file) == size;
}

/* a map's header read and checked: its sections */
H3_ALIGN static boolean file_open(struct h3_file *f, char const *path, unsigned char *header)
{
	int i;

	memset(f, 0, sizeof(*f));
	f->file = fopen(path, "rb");
	if (!f->file)
		return FALSE;
	if (fseek(f->file, 0, SEEK_END) != 0)
		goto failed;
	f->file_size = (unsigned long)ftell(f->file);
	if (!file_read_at(f->file, 0, header, H3_CACHE_HEADER_SIZE) || le32(header) != H3_HEADER_SIGNATURE ||
		le32(header + 4) != H3_CACHE_VERSION || le32(header + 0x3FFC) != H3_FOOTER_SIGNATURE)
	{
		goto failed;
	}
	for (i = 0; i < 4; i++)
	{
		f->masks[i] = le32(header + 0x4CC + 4 * i);
		f->section_size[i] = le32(header + 0x4E0 + 8 * i);
		f->section_offset[i] = (le32(header + 0x4DC + 8 * i) + f->masks[i]) & 0xFFFFFFFFUL;
		if (f->section_size[i] && (f->section_offset[i] > f->file_size ||
			f->section_size[i] > f->file_size - f->section_offset[i]))
		{
			goto failed;
		}
	}
	return TRUE;

failed:
	fclose(f->file);
	f->file = NULL;
	return FALSE;
}

H3_ALIGN static void file_close(struct h3_file *f)
{
	if (f->file)
		fclose(f->file);
	f->file = NULL;
}

/* the tag section's block number index, read if it is not there */
H3_ALIGN static unsigned char const *block_get(struct h3_cache *cache, unsigned long index)
{
	long i, oldest = 0;
	struct h3_block *b;
	unsigned long size;

	for (i = 0; i < H3_BLOCK_SLOTS; i++)
	{
		if (cache->blocks[i].index == index + 1)
		{
			cache->blocks[i].age = ++cache->clock;
			return cache->blocks[i].data;
		}
		if (cache->blocks[i].age < cache->blocks[oldest].age)
			oldest = i;
	}
	b = &cache->blocks[oldest];
	if (!b->data && !(b->data = malloc(H3_BLOCK_SIZE)))
		return NULL;
	size = cache->map.section_size[2] - index * H3_BLOCK_SIZE;
	if (size > H3_BLOCK_SIZE)
		size = H3_BLOCK_SIZE;
	b->index = 0;
	if (!file_read_at(cache->map.file, cache->map.section_offset[2] + index * H3_BLOCK_SIZE, b->data, size))
		return NULL;
	b->index = index + 1;
	b->age = ++cache->clock;

	return b->data;
}

/* a 32-bit tag pointer as an address (NONE: not in the section) */
H3_ALIGN static unsigned long address_of_pointer(struct h3_cache const *cache, unsigned long pointer)
{
	unsigned long long va = (unsigned long long)pointer << 2;

	if (!pointer || va < cache->virtual_base || va - cache->virtual_base >= cache->map.section_size[2])
		return 0xFFFFFFFFUL;
	return (unsigned long)(va - cache->virtual_base);
}

/* a shared map's file (its index in the play tag's list), opened once */
H3_ALIGN static struct h3_file *shared_file(struct h3_cache *cache, long index)
{
	if (index < 0 || index >= cache->shared_count)
		return NULL;
	if (!cache->shared_tried[index])
	{
		char path[H3_CACHE_NAME_SIZE + 80];
		unsigned char *header = malloc(H3_CACHE_HEADER_SIZE);

		cache->shared_tried[index] = TRUE;
		snprintf(path, sizeof(path), "%s%s", cache->directory, cache->shared_names[index]);
		if (!header || !file_open(&cache->shared[index], path, header))
			memset(&cache->shared[index], 0, sizeof(cache->shared[index]));
		free(header);
	}

	return cache->shared[index].file ? &cache->shared[index] : NULL;
}

/* the page's file, where its data starts in it, its sizes */
H3_ALIGN static boolean page_get(struct h3_cache *cache, long page, struct h3_file **file, unsigned long *offset,
	unsigned long *compressed, unsigned long *size)
{
	unsigned char const *p;
	short shared;

	if (page < 0 || page >= cache->page_count)
		return FALSE;
	p = h3_cache_at(cache, cache->pages + H3_PLAY_PAGE_SIZE * page, H3_PLAY_PAGE_SIZE);
	if (!p)
		return FALSE;
	shared = (short)(p[4] | (p[5] << 8));
	*file = shared < 0 ? &cache->map : shared_file(cache, shared);
	if (!*file)
		return FALSE;
	*compressed = le32(p + 0xC);
	*size = le32(p + 0x10);
	*offset = (*file)->section_offset[1] + le32(p + 8);
	if (le32(p + 8) > (*file)->section_size[1] || *compressed > (*file)->section_size[1] - le32(p + 8))
		return FALSE;

	return TRUE;
}

H3_ALIGN static int request_compare(void const *a, void const *b)
{
	struct h3_page_request const *x = *(struct h3_page_request * const *)a;
	struct h3_page_request const *y = *(struct h3_page_request * const *)b;

	if (x->page != y->page)
		return x->page < y->page ? -1 : 1;
	if (x->offset != y->offset)
		return x->offset < y->offset ? -1 : 1;
	return 0;
}

/* the bytes [at, at + size) of a page's data given to the requests that
want them (sorted by offset: first the first that may); a request's buffer
made as its first byte comes (FALSE: no memory for one) */
H3_ALIGN static boolean requests_feed(struct h3_page_request **requests, long count, long *first, unsigned long at,
	unsigned char const *data, unsigned long size)
{
	long i;

	for (i = *first; i < count; i++)
	{
		struct h3_page_request *r = requests[i];
		unsigned long lo, hi;

		if (r->offset >= at + size)
			break;
		if (r->got || r->offset + r->size <= at)
		{
			if (i == *first && (r->got || r->offset + r->size <= at))
				(*first)++;
			continue;
		}
		lo = r->offset > at ? r->offset : at;
		hi = r->offset + r->size < at + size ? r->offset + r->size : at + size;
		if (!r->out && !(r->out = malloc(r->size + 1)))
			return FALSE;
		memcpy(r->out + (lo - r->offset), data + (lo - at), hi - lo);
		if (hi == r->offset + r->size)
		{
			r->got = TRUE;
			if (r->done)
				r->done(r->context, r);
		}
	}
	return TRUE;
}

/* ---------- public code */

H3_ALIGN boolean h3_cache_file_is_halo3(char const *path)
{
	unsigned char *header = malloc(H3_CACHE_HEADER_SIZE);
	struct h3_file f;
	boolean result;

	if (!header)
		return FALSE;
	result = file_open(&f, path, header);
	if (result)
		file_close(&f);
	free(header);

	return result;
}

H3_ALIGN struct h3_cache *h3_cache_open(char const *maps_directory, char const *name, char *why, long why_size)
{
	struct h3_cache *cache = calloc(1, sizeof(*cache));
	unsigned char *header = malloc(H3_CACHE_HEADER_SIZE);
	char path[H3_CACHE_NAME_SIZE + 64];
	unsigned long index, debug_mask;
	long group_count, i;
	unsigned long long groups_va, tags_va;
	unsigned char const *ih;
	unsigned long *group_tags = NULL;

	if (!cache || !header)
	{
		why_set(why, why_size, "out of memory");
		goto failed;
	}
	snprintf(cache->directory, sizeof(cache->directory), "%s", maps_directory);
	snprintf(path, sizeof(path), "%s%s.map", maps_directory, name);
	if (!file_open(&cache->map, path, header))
	{
		why_set(why, why_size, "it is not a Halo 3 map this engine reads (MCC's, version 13)");
		goto failed;
	}
	memcpy(cache->name, header + 0xC0, 31);
	memcpy(cache->scenario, header + 0xE0, sizeof(cache->scenario) - 1);
	cache->virtual_base = le64(header + 0x2E0);
	index = (unsigned long)(le64(header + 0x2E8) - cache->virtual_base);
	debug_mask = cache->map.masks[0];

	/* the index, the groups and the tags */
	ih = h3_cache_at(cache, index, 0x20);
	if (!ih)
	{
		why_set(why, why_size, "its tag index is damaged");
		goto failed;
	}
	group_count = (long)le32(ih);
	groups_va = le64(ih + 8);
	cache->tag_count = (long)le32(ih + 0x10);
	tags_va = le64(ih + 0x18);
	if (group_count <= 0 || group_count > 0x400 || cache->tag_count <= 0 || cache->tag_count > H3_MAXIMUM_TAGS ||
		groups_va < cache->virtual_base || tags_va < cache->virtual_base)
	{
		why_set(why, why_size, "its tag index is damaged");
		goto failed;
	}
	group_tags = malloc(group_count * sizeof(unsigned long));
	cache->tag_groups = calloc(cache->tag_count, sizeof(unsigned long));
	cache->tag_addresses = calloc(cache->tag_count, sizeof(unsigned long));
	if (!group_tags || !cache->tag_groups || !cache->tag_addresses)
	{
		why_set(why, why_size, "out of memory");
		goto failed;
	}
	for (i = 0; i < group_count; i++)
	{
		unsigned char const *g = h3_cache_at(cache, (unsigned long)(groups_va - cache->virtual_base) + 16 * i, 16);

		if (!g)
		{
			why_set(why, why_size, "its tag groups are damaged");
			goto failed;
		}
		group_tags[i] = le32(g);
	}
	for (i = 0; i < cache->tag_count; i++)
	{
		unsigned char const *t = h3_cache_at(cache, (unsigned long)(tags_va - cache->virtual_base) + 8 * i, 8);
		short group;

		if (!t)
		{
			why_set(why, why_size, "its tags are damaged");
			goto failed;
		}
		group = (short)(t[0] | (t[1] << 8));
		cache->tag_groups[i] = group >= 0 && group < group_count ? group_tags[group] : 0;
		cache->tag_addresses[i] = group >= 0 ? address_of_pointer(cache, le32(t + 4)) : 0xFFFFFFFFUL;
		if (cache->tag_addresses[i] == 0xFFFFFFFFUL)
			cache->tag_addresses[i] = 0;
	}

	/* the names */
	{
		long count = (long)le32(header + 0x20);
		unsigned long data = (le32(header + 0x24) + debug_mask) & 0xFFFFFFFFUL;
		unsigned long size = le32(header + 0x28);
		unsigned long offsets = (le32(header + 0x2C) + debug_mask) & 0xFFFFFFFFUL;

		if (count < 0 || count > H3_MAXIMUM_TAGS || size > H3_MAXIMUM_NAME_BYTES ||
			!(cache->names = malloc(size + 1)) || !(cache->name_offsets = malloc((count + 1) * sizeof(long))) ||
			!file_read_at(cache->map.file, data, cache->names, size) ||
			!file_read_at(cache->map.file, offsets, cache->name_offsets, 4 * count))
		{
			why_set(why, why_size, "its tag names are damaged");
			goto failed;
		}
		cache->names[size] = 0;
		cache->names_size = size;
		for (i = 0; i < count; i++)
		{
			if (cache->name_offsets[i] < 0 || (unsigned long)cache->name_offsets[i] >= size)
				cache->name_offsets[i] = -1;
		}
		if (count < cache->tag_count)
		{
			long *grown = realloc(cache->name_offsets, cache->tag_count * sizeof(long));

			if (!grown)
				goto failed;
			cache->name_offsets = grown;
			for (i = count; i < cache->tag_count; i++)
				cache->name_offsets[i] = -1;
		}
	}

	/* the string ids */
	{
		long count = (long)le32(header + 0x30);
		unsigned long data = (le32(header + 0x34) + debug_mask) & 0xFFFFFFFFUL;
		unsigned long size = le32(header + 0x38);
		unsigned long offsets = (le32(header + 0x3C) + debug_mask) & 0xFFFFFFFFUL;
		if (count < 0 || count > H3_MAXIMUM_STRINGS || size > H3_MAXIMUM_NAME_BYTES ||
			!(cache->strings = malloc(size + 1)) || !(cache->string_offsets = malloc((count + 1) * sizeof(long))) ||
			!file_read_at(cache->map.file, data, cache->strings, size) ||
			!file_read_at(cache->map.file, offsets, cache->string_offsets, 4 * count))
		{
			why_set(why, why_size, "its string ids are damaged");
			goto failed;
		}
		cache->strings[size] = 0;
		cache->strings_size = size;
		cache->string_count = count;
		{
			long set_count = (long)le32(header + 0x40);
			unsigned long sets = (le32(header + 0x44) + debug_mask) & 0xFFFFFFFFUL;
			unsigned char raw[4 * H3_STRING_SET_MAXIMUM];

			if (set_count < 1 || set_count > H3_STRING_SET_MAXIMUM || !file_read_at(cache->map.file, sets, raw, 4 * (unsigned long)set_count))
			{
				why_set(why, why_size, "its string ids are damaged");
				goto failed;
			}
			cache->string_set_count = set_count;
			for (i = 0; i < set_count; i++)
				cache->string_sets[i] = le32(raw + 4 * i);
		}
		for (i = 0; i < count; i++)
		{
			if (cache->string_offsets[i] < 0 || (unsigned long)cache->string_offsets[i] >= size)
				cache->string_offsets[i] = -1;
		}
	}

	/* the resources: zone and play */
	{
		long zone = h3_cache_find_tag(cache, 'zone', NULL);
		long play = h3_cache_find_tag(cache, 'play', NULL);
		long shared_count;
		unsigned long shared;

		if (zone == NONE || play == NONE ||
			!h3_cache_block(cache, cache->tag_addresses[zone] + 0x64, H3_ZONE_RESOURCE_SIZE, &cache->resource_count,
				&cache->resources) ||
			!h3_cache_block(cache, cache->tag_addresses[play] + 0x18, H3_PLAY_PAGE_SIZE, &cache->page_count, &cache->pages) ||
			!h3_cache_block(cache, cache->tag_addresses[play] + 0x3C, H3_PLAY_LOCATION_SIZE, &cache->location_count,
				&cache->locations) ||
			!h3_cache_block(cache, cache->tag_addresses[play] + 0x0C, H3_PLAY_SHARED_SIZE, &shared_count, &shared))
		{
			why_set(why, why_size, "its resources are damaged");
			goto failed;
		}
		cache->info_size = h3_cache_read32(cache, cache->tag_addresses[zone] + 0x148);
		cache->info_address = address_of_pointer(cache, h3_cache_read32(cache, cache->tag_addresses[zone] + 0x154));
		if (cache->info_address == 0xFFFFFFFFUL || cache->info_size > cache->map.section_size[2] - cache->info_address)
		{
			why_set(why, why_size, "its resources are damaged");
			goto failed;
		}
		cache->shared_count = shared_count < H3_MAXIMUM_SHARED ? shared_count : H3_MAXIMUM_SHARED;
		for (i = 0; i < cache->shared_count; i++)
		{
			char const *p = h3_cache_at(cache, shared + H3_PLAY_SHARED_SIZE * i, 0x100);
			char text[0x101];
			char const *base;

			if (!p)
				continue;
			memcpy(text, p, 0x100);
			text[0x100] = 0;
			/* (the last of either separator: a name alone, never a way out of
			the folder - the file's own text) */
			base = strrchr(text, '\\');
			if (strrchr(text, '/') > base)
				base = strrchr(text, '/');
			base = base ? base + 1 : text;
			if (strstr(base, ".."))
				base = "";
			snprintf(cache->shared_names[i], sizeof(cache->shared_names[i]), "%s", base);
		}
	}
	free(group_tags);
	free(header);

	return cache;

failed:
	free(group_tags);
	free(header);
	if (cache)
		h3_cache_close(cache);
	return NULL;
}

H3_ALIGN void h3_cache_close(struct h3_cache *cache)
{
	long i;

	if (!cache)
		return;
	file_close(&cache->map);
	for (i = 0; i < H3_MAXIMUM_SHARED; i++)
		file_close(&cache->shared[i]);
	for (i = 0; i < H3_BLOCK_SLOTS; i++)
		free(cache->blocks[i].data);
	for (i = 0; i < H3_SCRATCH_SLOTS; i++)
		free(cache->scratch[i]);
	free(cache->tag_groups);
	free(cache->tag_addresses);
	free(cache->names);
	free(cache->name_offsets);
	free(cache->strings);
	free(cache->string_offsets);
	free(cache);
}

H3_ALIGN char const *h3_cache_name(struct h3_cache const *cache)
{
	return cache->name;
}

H3_ALIGN char const *h3_cache_scenario_path(struct h3_cache const *cache)
{
	return cache->scenario;
}

H3_ALIGN long h3_cache_tag_count(struct h3_cache const *cache)
{
	return cache->tag_count;
}

H3_ALIGN boolean h3_cache_tag_get(struct h3_cache const *cache, long index, struct h3_cache_tag *tag)
{
	if (index < 0 || index >= cache->tag_count || !cache->tag_addresses[index])
		return FALSE;
	tag->group_tag = cache->tag_groups[index];
	tag->address = cache->tag_addresses[index];
	return TRUE;
}

H3_ALIGN char const *h3_cache_tag_name(struct h3_cache const *cache, long index)
{
	if (index < 0 || index >= cache->tag_count || !cache->name_offsets || cache->name_offsets[index] < 0)
		return "";
	return cache->names + cache->name_offsets[index];
}

H3_ALIGN long h3_cache_find_tag(struct h3_cache const *cache, unsigned long group_tag, char const *name)
{
	long i;

	for (i = 0; i < cache->tag_count; i++)
	{
		if (cache->tag_groups[i] == group_tag && cache->tag_addresses[i] &&
			(!name || !strcmp(h3_cache_tag_name(cache, i), name)))
		{
			return i;
		}
	}
	return NONE;
}

H3_ALIGN long h3_cache_tag_from_datum(struct h3_cache const *cache, unsigned long datum)
{
	long index = (long)(datum & 0xFFFF);

	if (datum == 0xFFFFFFFFUL || index >= cache->tag_count || !cache->tag_addresses[index])
		return NONE;
	return index;
}

H3_ALIGN boolean h3_cache_read(struct h3_cache *cache, unsigned long address, unsigned long size, void *out)
{
	unsigned char *o = out;

	if (address > cache->map.section_size[2] || size > cache->map.section_size[2] - address)
		return FALSE;
	while (size)
	{
		unsigned long block = address / H3_BLOCK_SIZE, within = address % H3_BLOCK_SIZE;
		unsigned long n = H3_BLOCK_SIZE - within;
		unsigned char const *data = block_get(cache, block);

		if (!data)
			return FALSE;
		if (n > size)
			n = size;
		memcpy(o, data + within, n);
		o += n;
		address += n;
		size -= n;
	}
	return TRUE;
}

H3_ALIGN void const *h3_cache_at(struct h3_cache *cache, unsigned long address, unsigned long size)
{
	long slot = cache->scratch_next;

	if (size > H3_CACHE_AT_MAXIMUM)
		return NULL;
	if (cache->scratch_size[slot] < size || !cache->scratch[slot])
	{
		unsigned long want = size < 256 ? 256 : size;
		unsigned char *grown = realloc(cache->scratch[slot], want);

		if (!grown)
			return NULL;
		cache->scratch[slot] = grown;
		cache->scratch_size[slot] = want;
	}
	if (!h3_cache_read(cache, address, size, cache->scratch[slot]))
		return NULL;
	cache->scratch_next = (slot + 1) % H3_SCRATCH_SLOTS;

	return cache->scratch[slot];
}

H3_ALIGN unsigned long h3_cache_read32(struct h3_cache *cache, unsigned long address)
{
	unsigned char b[4];

	return h3_cache_read(cache, address, 4, b) ? le32(b) : 0;
}

H3_ALIGN short h3_cache_read16(struct h3_cache *cache, unsigned long address)
{
	unsigned char b[2];

	return h3_cache_read(cache, address, 2, b) ? (short)(b[0] | (b[1] << 8)) : 0;
}

H3_ALIGN float h3_cache_readf(struct h3_cache *cache, unsigned long address)
{
	unsigned long v = h3_cache_read32(cache, address);
	float f;

	memcpy(&f, &v, 4);
	return f;
}

/* a tag data reference at address (its size at 0, its pointer at 0xC): the
size and the bytes' address; FALSE if they are not in the map */
H3_ALIGN boolean h3_cache_data(struct h3_cache *cache, unsigned long address, unsigned long *size, unsigned long *bytes)
{
	unsigned char b[0x10];
	unsigned long at;

	*size = 0;
	*bytes = 0;
	if (!h3_cache_read(cache, address, sizeof(b), b))
		return FALSE;
	if (le32(b) == 0)
		return TRUE;
	at = address_of_pointer(cache, le32(b + 0xC));
	if (at == 0xFFFFFFFFUL || le32(b) > 0x1000000 || le32(b) > cache->map.section_size[2] - at)
		return FALSE;
	*size = le32(b);
	*bytes = at;
	return TRUE;
}

H3_ALIGN boolean h3_cache_block(struct h3_cache *cache, unsigned long address, unsigned long element_size, long *count,
	unsigned long *elements)
{
	unsigned char b[8];
	long n;
	unsigned long at;

	*count = 0;
	*elements = 0;
	if (!h3_cache_read(cache, address, 8, b))
		return FALSE;
	n = (long)le32(b);
	if (n == 0)
		return TRUE;
	at = address_of_pointer(cache, le32(b + 4));
	if (n < 0 || n > 0x1000000 || at == 0xFFFFFFFFUL ||
		(unsigned long long)n * element_size > cache->map.section_size[2] - at)
	{
		return FALSE;
	}
	*count = n;
	*elements = at;
	return TRUE;
}

H3_ALIGN long h3_cache_reference(struct h3_cache *cache, unsigned long address)
{
	return h3_cache_tag_from_datum(cache, h3_cache_read32(cache, address + 0xC));
}

H3_ALIGN char const *h3_cache_string(struct h3_cache const *cache, unsigned long string_id)
{
	unsigned long raw = string_id & 0x7FFFFUL, set = (string_id >> 19) & 0xFF, first = 0, all = 0;
	long index, i;

	for (i = 0; i < cache->string_set_count; i++)
	{
		if ((unsigned long)i < set)
			first += cache->string_sets[i];
		all += cache->string_sets[i];
	}
	if (set >= (unsigned long)cache->string_set_count)
		return "";
	if (set == 0)
		index = (long)(raw < cache->string_sets[0] ? raw : raw - cache->string_sets[0] + all);
	else
		index = (long)(raw + first);
	if (index < 0 || index >= cache->string_count || cache->string_offsets[index] < 0)
		return "";
	return cache->strings + cache->string_offsets[index];
}

H3_ALIGN boolean h3_cache_resource_get(struct h3_cache *cache, long index, struct h3_resource *resource)
{
	unsigned char const *e;
	unsigned long control_offset, fixups;
	long fixup_count, i;
	short location;

	memset(resource, 0, sizeof(*resource));
	resource->page[0] = resource->page[1] = NONE;
	if (index < 0 || index >= cache->resource_count)
		return FALSE;
	e = h3_cache_at(cache, cache->resources + H3_ZONE_RESOURCE_SIZE * index, H3_ZONE_RESOURCE_SIZE);
	if (!e)
		return FALSE;
	resource->index = index;
	resource->type = e[0x12];
	control_offset = le32(e + 0x14);
	resource->control_size = le32(e + 0x18);
	location = (short)(e[0x22] | (e[0x23] << 8));
	resource->root = le32(e + 0x24) & 0x0FFFFFFFUL;
	if (control_offset > cache->info_size || resource->control_size > cache->info_size - control_offset ||
		!h3_cache_block(cache, cache->resources + H3_ZONE_RESOURCE_SIZE * index + 0x28, 8, &fixup_count, &fixups))
	{
		return FALSE;
	}
	resource->control = malloc(resource->control_size + 4);
	if (!resource->control || !h3_cache_read(cache, cache->info_address + control_offset, resource->control_size,
		resource->control))
	{
		h3_resource_free(resource);
		return FALSE;
	}
	for (i = 0; i < fixup_count; i++)
	{
		unsigned char const *f = h3_cache_at(cache, fixups + 8 * i, 8);
		unsigned long at = f ? le32(f) : 0xFFFFFFFFUL;

		if (at <= resource->control_size - 4 && resource->control_size >= 4)
			memcpy(resource->control + at, f + 4, 4);
	}
	if (location >= 0 && location < cache->location_count)
	{
		unsigned char const *l = h3_cache_at(cache, cache->locations + H3_PLAY_LOCATION_SIZE * location, H3_PLAY_LOCATION_SIZE);
		int k;

		for (k = 0; l && k < 2; k++)
		{
			short page = (short)(l[2 * k] | (l[2 * k + 1] << 8));
			unsigned long offset = le32(l + 4 + 4 * k);
			unsigned long size = h3_cache_page_size(cache, page);

			if (page >= 0 && size && offset <= size)
			{
				resource->page[k] = page;
				resource->offset[k] = offset;
				resource->size[k] = size - offset;
			}
		}
	}

	return TRUE;
}

H3_ALIGN void h3_resource_free(struct h3_resource *resource)
{
	free(resource->control);
	resource->control = NULL;
	resource->control_size = 0;
}

H3_ALIGN unsigned long h3_resource_read32(struct h3_resource const *resource, unsigned long offset)
{
	if (!resource->control || offset + 4 > resource->control_size || offset + 4 < offset)
		return 0;
	return le32(resource->control + offset);
}

H3_ALIGN boolean h3_resource_pointer(struct h3_resource const *resource, unsigned long offset, unsigned long *kind,
	unsigned long *target)
{
	unsigned long v;

	if (!resource->control || offset + 4 > resource->control_size)
		return FALSE;
	v = le32(resource->control + offset);
	*kind = v >> 28;
	*target = v & 0x0FFFFFFFUL;
	if (*kind == 2 && *target >= resource->control_size)
		return FALSE;
	return *kind == 2 || *kind == 4 || *kind == 6;
}

H3_ALIGN unsigned long h3_cache_page_size(struct h3_cache *cache, long page)
{
	struct h3_file *file;
	unsigned long offset, compressed, size;

	return page_get(cache, page, &file, &offset, &compressed, &size) ? size : 0;
}

/* (read_watch: the bytes between control's progress calls) */
#define H3_PROGRESS_BYTES (1024UL * 1024UL)

/* a window's bytes counted for control: FALSE if the read is to stop */
H3_ALIGN static boolean read_watch(struct h3_cache_read_control const *control, unsigned long bytes, unsigned long *since)
{
	if (!control)
		return TRUE;
	if (control->cancel && *control->cancel)
		return FALSE;
	*since += bytes;
	if (*since >= H3_PROGRESS_BYTES)
	{
		if (control->progress)
			control->progress(*since);
		*since = 0;
	}
	return TRUE;
}

H3_ALIGN boolean h3_cache_pages_read(struct h3_cache *cache, struct h3_page_request *requests, long count, char *why,
	long why_size, struct h3_cache_read_control const *control)
{
	struct h3_page_request **sorted = malloc((count + 1) * sizeof(*sorted));
	unsigned char *in = malloc(H3_WINDOW), *out = malloc(H3_WINDOW);
	long i, first;
	boolean ok = TRUE, stopped = FALSE;
	unsigned long since_progress = 0;

	if (!sorted || !in || !out)
	{
		free(sorted);
		free(in);
		free(out);
		why_set(why, why_size, "out of memory");
		return FALSE;
	}
	for (i = 0; i < count; i++)
	{
		sorted[i] = &requests[i];
		requests[i].got = FALSE;
	}
	qsort(sorted, count, sizeof(*sorted), request_compare);
	for (first = 0; first < count && ok; )
	{
		long page = sorted[first]->page, last;
		struct h3_file *file;
		unsigned long offset, compressed, size, end = 0;
		long next;

		for (last = first; last < count && sorted[last]->page == page; last++)
		{
			if (sorted[last]->offset + sorted[last]->size > end)
				end = sorted[last]->offset + sorted[last]->size;
		}
		if (!page_get(cache, page, &file, &offset, &compressed, &size) || end > size)
		{
			why_set(why, why_size, "a resource page is missing or damaged (is shared.map beside the map?)");
			ok = FALSE;
			break;
		}
		/* (h3vita) a request's buffer is made when its first byte arrives
		(requests_feed), not all of a page's here: a Halo 3 level's one pass
		over shared.map's page 228 named 2000 requests, 29 MB of buffers
		taken at once before a byte was inflated, while each is given to its
		done call - and freed or kept by it - as soon as it is whole */
		next = first;
		if (compressed == size)
		{
			/* stored: each read where it is */
			for (i = first; i < last && ok; i++)
			{
				if (!sorted[i]->out && !(sorted[i]->out = malloc(sorted[i]->size + 1)))
				{
					why_set(why, why_size, "out of memory");
					ok = FALSE;
				}
				else if (!read_watch(control, sorted[i]->size, &since_progress))
					ok = FALSE, stopped = TRUE;
				else if (!file_read_at(file->file, offset + sorted[i]->offset, sorted[i]->out, sorted[i]->size))
					ok = FALSE;
				else
				{
					sorted[i]->got = TRUE;
					if (sorted[i]->done)
						sorted[i]->done(sorted[i]->context, sorted[i]);
				}
			}
		}
		else
		{
			/* raw deflate: inflated a window at a time up to the last byte wanted */
			z_stream stream;
			unsigned long read = 0, at = 0;
			int status = Z_OK;

			memset(&stream, 0, sizeof(stream));
			if (inflateInit2(&stream, -15) != Z_OK || fseek(file->file, (long)offset, SEEK_SET) != 0)
				ok = FALSE;
			while (ok && at < end && status != Z_STREAM_END)
			{
				if (stream.avail_in == 0)
				{
					unsigned long n = compressed - read < H3_WINDOW ? compressed - read : H3_WINDOW;

					if (!read_watch(control, n, &since_progress))
					{
						ok = FALSE;
						stopped = TRUE;
						break;
					}
					if (!n || fread(in, 1, n, file->file) != n)
					{
						ok = FALSE;
						break;
					}
					read += n;
					stream.next_in = in;
					stream.avail_in = (uInt)n;
				}
				stream.next_out = out;
				stream.avail_out = H3_WINDOW;
				status = inflate(&stream, Z_NO_FLUSH);
				if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR)
				{
					ok = FALSE;
					break;
				}
				if (H3_WINDOW - stream.avail_out)
				{
					if (!requests_feed(sorted + 0, last, &next, at, out, H3_WINDOW - stream.avail_out))
					{
						why_set(why, why_size, "out of memory");
						ok = FALSE;
						break;
					}
					at += H3_WINDOW - stream.avail_out;
				}
			}
			inflateEnd(&stream);
			if (ok && at < end)
				ok = FALSE;
			if (!ok)
				why_set(why, why_size, stopped ? "it was stopped" : "a resource page cannot be inflated");
		}
		first = last;
	}
	free(sorted);
	free(in);
	free(out);

	return ok;
}
