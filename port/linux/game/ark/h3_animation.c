/*
H3_ANIMATION.C

Halo 3's animation graphs made this engine's (h3_animation.h says how).

What this file does itself is the read: the graph tag's blocks through
h3_cache.c, and every animation's data gathered from the resource pages in
one pass before the graph is made (h2_animation.c decodes them as it fills
the tables; a page is one deflate stream, read once). The layout below is
AUDIT.md's; the mode, class and type tables, the overlays baked, the variants
and the compression are h2_animation.c's through its source.
*/

/* ---------- headers */

#include "cseries.h"
#include "h3_animation.h"
#include "h2_animation.h"
#include "h3_cache.h"
#include "h3_spartan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (the C heap's own, as h3_cache.c's and h3_spartan.c's: not cseries'
checked allocator, whose free refuses what it did not give) */
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

void platform_log(char const *format, ...);
unsigned long long vita_host_time_us(void);

/* ---------- constants */

enum
{
	/* Halo 3's jmad (MCC): the blocks h2_animation.c does not read by the
	layout (the resource groups and their members) */
	JMAD_ANIMATIONS = 0x50,
	JMAD_ANIMATION_SIZE = 0x88,
	JMAD_ANIMATION_GROUP = 0x28,
	JMAD_GROUPS = 0xF8,
	JMAD_GROUP_SIZE = 0xC,
	/* a resource member (the group's control's root block): frames at 8,
	the flags' sizes at 0xC and 0xD, the movement's at 0xE, the static block's
	at 0x12, the animated block's at 0x18, the total at 0x1C, the data's
	pointer (the required page) at 0x28 */
	JMAD_MEMBER_SIZE = 0x30,
	MAXIMUM_GROUPS = 1024,
	MAXIMUM_ANIMATIONS = 4096,
	/* (the Spartan's 806 animations are 4.75 MB) */
	MAXIMUM_DATA_BYTES = 64 * 1024 * 1024,
	MAXIMUM_ANIMATION_BYTES = 1024 * 1024,
};

/* where Halo 3's graph keeps what h2_animation.c reads (h2_animation.h's
struct h2_graph_layout; AUDIT.md's table) */
static struct h2_graph_layout const h3_layout =
{
	0x14, 0x20,             /* skeleton nodes */
	0x38, 0x1C,             /* blend screens */
	0x50, 0x88,             /* animations */
	0x5C, 0x28,             /* modes */
	0x1C,                   /* a weapon class */
	0x34, 0x10, 0x1C,       /* a weapon type: its overlays, its deaths and damage */
	0x10, 0x0C,             /* a damage entry, a direction */
	0, 0,                   /* (no inheritance list read yet: the Spartan's graph inherits none) */
	/* an animation entry: type, frame info type, blend screen, frames, (no
	weight: 1), loop frame, next variant, frame events block */
	0x12, 0x13, 0x0C, 0x10, 0, 0x08, 0x26, 0x2C,
	TRUE,
};

/* ---------- structures */

/* an animation's data read from the pages */
struct animation_data
{
	unsigned long offset;       /* in the data gathered */
	unsigned long static_size, animated_size, static_flags, animated_flags, movement;
	boolean got;
};

/* the source: the map and the animations' data */
struct h3_graph
{
	struct h3_cache *cache;
	boolean cache_borrowed;     /* (h3_animation_source_open: the caller's, not closed here) */
	long tag;
	unsigned long address;
	long animation_count;
	unsigned long animations;
	struct animation_data *data;
	unsigned char *bytes;
	unsigned long bytes_size;
	long got;
};

/* ---------- private code */

H3_ALIGN static unsigned long read32(void const *p)
{
	unsigned char const *b = p;

	return (unsigned long)b[0] | ((unsigned long)b[1] << 8) | ((unsigned long)b[2] << 16) | ((unsigned long)b[3] << 24);
}

H3_ALIGN static unsigned short read16(void const *p)
{
	unsigned char const *b = p;

	return (unsigned short)(b[0] | (b[1] << 8));
}

/* ---------- private code: the source over an h3_cache */

H3_ALIGN static boolean source_tag_get(void *context, long tag, unsigned long *group_tag, unsigned long *address)
{
	struct h3_graph *g = context;
	struct h3_cache_tag t;

	if (!h3_cache_tag_get(g->cache, tag, &t) || !t.address)
		return FALSE;
	*group_tag = t.group_tag;
	*address = t.address;
	return TRUE;
}

H3_ALIGN static char const *source_tag_name(void *context, long tag)
{
	struct h3_graph *g = context;

	return h3_cache_tag_name(g->cache, tag);
}

H3_ALIGN static boolean source_block(void *context, unsigned long address, unsigned long element_size, long *count, unsigned long *elements)
{
	struct h3_graph *g = context;

	return h3_cache_block(g->cache, address, element_size, count, elements);
}

H3_ALIGN static void const *source_at(void *context, unsigned long address, unsigned long size)
{
	struct h3_graph *g = context;

	return h3_cache_at(g->cache, address, size);
}

H3_ALIGN static long source_reference(void *context, unsigned long address)
{
	struct h3_graph *g = context;

	return h3_cache_reference(g->cache, address);
}

H3_ALIGN static boolean source_string(void *context, unsigned long id, char *out, long size)
{
	struct h3_graph *g = context;
	char const *text = h3_cache_string(g->cache, id);

	if (size <= 0)
		return FALSE;
	snprintf(out, (size_t)size, "%s", text ? text : "");
	return text && text[0];
}

/* an animation entry's data: the member's, gathered already */
H3_ALIGN static boolean source_animation_data(void *context, unsigned long entry, struct h2_animation_data *out)
{
	struct h3_graph *g = context;
	long index;
	struct animation_data const *d;

	memset(out, 0, sizeof(*out));
	if (entry < g->animations || (entry - g->animations) % JMAD_ANIMATION_SIZE)
		return FALSE;
	index = (long)((entry - g->animations) / JMAD_ANIMATION_SIZE);
	if (index < 0 || index >= g->animation_count || !g->data[index].got)
		return FALSE;
	d = &g->data[index];
	out->bytes = g->bytes + d->offset;
	out->static_size = d->static_size;
	out->animated_size = d->animated_size;
	out->static_flags = d->static_flags;
	out->animated_flags = d->animated_flags;
	out->movement = d->movement;
	return TRUE;
}

/* ---------- private code: the read */

H3_ALIGN static void why_set(char *why, long why_size, char const *text)
{
	if (why && why_size > 0)
		snprintf(why, (size_t)why_size, "%s", text);
}

/* the graph's map opened: HALO_H3_ANIMATION_MAP's, else the first of
H3_SPARTAN_MAPS that holds the graph */
H3_ALIGN static struct h3_cache *graph_cache_open(char const *graph_name, char *why, long why_size)
{
	static char const *const maps[] = H3_SPARTAN_MAPS;
	char directory[256];
	char const *named = getenv("HALO_H3_ANIMATION_MAP");
	unsigned long i;

	if (!h3_spartan_maps_directory(directory, sizeof(directory)))
	{
		why_set(why, why_size, "no Halo 3 maps in the content folder");
		return NULL;
	}
	for (i = 0; i < (named ? 1 : sizeof(maps) / sizeof(maps[0])); i++)
	{
		struct h3_cache *cache = h3_cache_open(directory, named ? named : maps[i], why, why_size);

		if (cache && h3_cache_find_tag(cache, 'jmad', graph_name) != NONE)
			return cache;
		if (cache)
			h3_cache_close(cache);
	}
	why_set(why, why_size, "no Halo 3 map found holds the graph");
	return NULL;
}

/* every animation's data planned (its member's sizes and place in its
group's resource) and read from the pages in one pass */
H3_ALIGN static boolean animations_read(struct h3_graph *g, char *why, long why_size)
{
	struct h3_cache *cache = g->cache;
	long group_count = 0, i, request_count = 0, planned = 0;
	unsigned long groups = 0, total = 0;
	struct h3_resource *resources;
	unsigned char *have;
	struct h3_page_request *requests;
	boolean ok = FALSE;

	if (!h3_cache_block(cache, g->address + JMAD_GROUPS, JMAD_GROUP_SIZE, &group_count, &groups) || group_count < 1 ||
		group_count > MAXIMUM_GROUPS)
	{
		why_set(why, why_size, "its resource groups cannot be read");
		return FALSE;
	}
	resources = calloc((size_t)group_count, sizeof(*resources));
	have = calloc((size_t)group_count, 1);
	requests = calloc((size_t)g->animation_count, sizeof(*requests));
	g->data = calloc((size_t)g->animation_count, sizeof(*g->data));
	if (!resources || !have || !requests || !g->data)
	{
		why_set(why, why_size, "out of memory");
		goto done;
	}
	for (i = 0; i < group_count; i++)
	{
		if (h3_cache_resource_get(cache, (long)(h3_cache_read32(cache, groups + JMAD_GROUP_SIZE * (unsigned long)i + 4) & 0xFFFF), &resources[i]))
			have[i] = TRUE;
	}
	/* each animation's member */
	for (i = 0; i < g->animation_count; i++)
	{
		unsigned long entry = g->animations + JMAD_ANIMATION_SIZE * (unsigned long)i;
		long group = h3_cache_read16(cache, entry + JMAD_ANIMATION_GROUP);
		long member = h3_cache_read16(cache, entry + JMAD_ANIMATION_GROUP + 2);
		struct h3_resource const *r;
		unsigned long members, kind, target, size;
		long members_count;
		unsigned char const *m;
		struct animation_data *d = &g->data[i];

		if (group < 0 || group >= group_count || !have[group])
			continue;
		r = &resources[group];
		members_count = (long)h3_resource_read32(r, r->root);
		if (member < 0 || member >= members_count || !h3_resource_pointer(r, r->root + 4, &kind, &members) || kind != 2 ||
			members + JMAD_MEMBER_SIZE * ((unsigned long)member + 1) > r->control_size)
		{
			continue;
		}
		m = r->control + members + JMAD_MEMBER_SIZE * (unsigned long)member;
		d->static_flags = m[0xC];
		d->animated_flags = m[0xD];
		d->movement = read16(m + 0xE);
		d->static_size = read16(m + 0x12);
		d->animated_size = read32(m + 0x18);
		size = d->static_size + d->animated_size + d->static_flags + d->animated_flags + d->movement;
		if (!size || size > MAXIMUM_ANIMATION_BYTES || read32(m + 0x1C) < size ||
			!h3_resource_pointer(r, members + JMAD_MEMBER_SIZE * (unsigned long)member + 0x28, &kind, &target) || kind != 4 ||
			r->page[0] == NONE || total + size > MAXIMUM_DATA_BYTES)
		{
			continue;
		}
		d->offset = total;
		total += size;
		requests[request_count].page = r->page[0];
		requests[request_count].offset = r->offset[0] + target;
		requests[request_count].size = size;
		requests[request_count].tag = i;
		request_count++;
		planned++;
	}
	if (!planned)
	{
		why_set(why, why_size, "none of its animations' data can be found");
		goto done;
	}
	g->bytes = malloc(total ? total : 1);
	g->bytes_size = total;
	if (!g->bytes)
	{
		why_set(why, why_size, "out of memory");
		goto done;
	}
	for (i = 0; i < request_count; i++)
		requests[i].out = g->bytes + g->data[requests[i].tag].offset;
	if (!h3_cache_pages_read(cache, requests, request_count, why, why_size, NULL))
		goto done;
	for (i = 0; i < request_count; i++)
	{
		if (requests[i].got)
		{
			g->data[requests[i].tag].got = TRUE;
			g->got++;
		}
	}
	ok = g->got > 0;
	if (!ok)
		why_set(why, why_size, "none of its animations' data was read");
	/* (HALO_H2_DEBUG: the rifle idle's bytes hashed, beside h3_spartan.c's
	own read of the same animation - the two must agree byte for byte) */
	if (ok && getenv("HALO_H2_DEBUG"))
	{
		for (i = 0; i < g->animation_count; i++)
		{
			unsigned long entry = g->animations + JMAD_ANIMATION_SIZE * (unsigned long)i;

			if (g->data[i].got && !strcmp(h3_cache_string(cache, h3_cache_read32(cache, entry)), "combat:rifle:idle"))
			{
				struct animation_data const *d = &g->data[i];
				unsigned long size = d->static_size + d->animated_size + d->static_flags + d->animated_flags + d->movement, k, hash = 2166136261UL;
				unsigned long poses_hash = 2166136261UL;
				short frames = h3_cache_read16(cache, entry + 0x10), nodes = (short)((unsigned char const *)h3_cache_at(cache, entry + 0xC, 4))[3];
				struct h2_animation_pose *poses = calloc((size_t)frames * (size_t)nodes, sizeof(*poses));
				unsigned char *sets = calloc((size_t)nodes + 1, 1);

				for (k = 0; k < size; k++)
					hash = (hash ^ g->bytes[d->offset + k]) * 16777619UL;
				for (k = 0; poses && k < (unsigned long)frames * (unsigned long)nodes; k++)
				{
					poses[k].rotation[3] = 1.0f;
					poses[k].scale = 1.0f;
				}
				if (poses && sets && h2_animation_blocks_decode(poses, sets, nodes, frames, g->bytes + d->offset, d->static_size, d->animated_size,
					d->static_flags, d->animated_flags))
				{
					unsigned char const *b = (unsigned char const *)poses;

					for (k = 0; k < (unsigned long)frames * (unsigned long)nodes * sizeof(*poses); k++)
						poses_hash = (poses_hash ^ b[k]) * 16777619UL;
				}
				else
					poses_hash = 0;
				platform_log("h3 animation: idle check: combat:rifle:idle %d frames %d nodes, %lu bytes (static %lu animated %lu flags %lu+%lu movement %lu) hash %08lx, decoded poses hash %08lx",
					(int)frames, (int)nodes, size, d->static_size, d->animated_size, d->static_flags, d->animated_flags, d->movement, hash, poses_hash);
				free(poses);
				free(sets);
			}
		}
	}
done:
	for (i = 0; resources && i < group_count; i++)
		h3_resource_free(&resources[i]);
	free(resources);
	free(have);
	free(requests);
	return ok;
}

H3_ALIGN static void h3_graph_free(struct h3_graph *g)
{
	if (!g)
		return;
	free(g->data);
	free(g->bytes);
	if (g->cache && !g->cache_borrowed)
		h3_cache_close(g->cache);
	free(g);
}

/* the source's calls over a graph */
H3_ALIGN static void source_fill(struct h2_graph_source *source, struct h3_graph *g)
{
	memset(source, 0, sizeof(*source));
	source->context = g;
	source->layout = &h3_layout;
	source->tag_get = source_tag_get;
	source->tag_name = source_tag_name;
	source->block = source_block;
	source->at = source_at;
	source->reference = source_reference;
	source->string = source_string;
	source->animation_data = source_animation_data;
}

/* (HALO_H2_DEBUG) the graph's skeleton beside the model's: each graph node,
its parent, the model node of its name and that one's parent - where the
two skeletons disagree, an animation's node is placed under another parent */
H3_ALIGN static boolean names_alike(char const *a, char const *b)
{
	if (!strncmp(a, "bip01 ", 6))
		a += 6;
	if (!strncmp(b, "bip01 ", 6))
		b += 6;
	for (; *a && *b; a++, b++)
	{
		char x = *a == '_' ? ' ' : (char)((*a >= 'A' && *a <= 'Z') ? *a + 32 : *a), y = *b == '_' ? ' ' : (char)((*b >= 'A' && *b <= 'Z') ? *b + 32 : *b);

		if (x != y)
			return FALSE;
	}
	return *a == *b;
}

H3_ALIGN static void skeletons_log(struct h3_graph *g, char const (*node_names)[32], short const *node_parents, short node_count)
{
	long count = 0, i;
	unsigned long nodes = 0;
	short to_model[128];

	if (!getenv("HALO_H2_DEBUG") || !h3_cache_block(g->cache, g->address + 0x14, 0x20, &count, &nodes) || count > 128)
		return;
	for (i = 0; i < count; i++)
	{
		char const *name = h3_cache_string(g->cache, h3_cache_read32(g->cache, nodes + 0x20 * (unsigned long)i));
		short n;

		to_model[i] = NONE;
		for (n = 0; n < node_count; n++)
		{
			if (names_alike(node_names[n], name))
				to_model[i] = n;
		}
	}
	for (i = 0; i < count; i++)
	{
		char const *name = h3_cache_string(g->cache, h3_cache_read32(g->cache, nodes + 0x20 * (unsigned long)i));
		short parent = h3_cache_read16(g->cache, nodes + 0x20 * (unsigned long)i + 8);
		char const *parent_name = parent >= 0 && parent < count ? h3_cache_string(g->cache, h3_cache_read32(g->cache, nodes + 0x20 * (unsigned long)parent)) : "-";
		short m = to_model[i], mp = m != NONE ? node_parents[m] : NONE;
		boolean agree = m == NONE || (parent < 0 ? mp == NONE : (mp != NONE && to_model[parent] == mp));

		platform_log("h3 animation: node %2ld %-14s parent %-10s -> model %2d %-14s parent %-14s%s", i, name, parent_name, (int)m,
			m != NONE ? node_names[m] : "-", mp != NONE ? node_names[mp] : "-", agree ? "" : "  DIFFERS");
	}
	for (i = 0; i < node_count; i++)
	{
		long k;

		for (k = 0; k < count && to_model[k] != i; k++)
			;
		if (k == count)
			platform_log("h3 animation: model node %2ld %-14s (parent %s): none in the graph", i, node_names[i],
				node_parents[i] != NONE ? node_names[node_parents[i]] : "-");
	}
}

/* the Halo 3 graph a biped plays (NULL: none) */
H3_ALIGN static char const *graph_of(char const *biped_name)
{
	if (!strcmp(biped_name, "spartan"))
		return "objects\\characters\\masterchief\\masterchief";
	return NULL;
}

/* ---------- public code */

H3_ALIGN boolean h3_animation_wanted(
	char const *biped_name)
{
	char const *setting = getenv("HALO_H3_ANIMATION");

	return setting && atoi(setting) > 0 && biped_name && graph_of(biped_name) != NULL;
}

H3_ALIGN struct h2_graph *h3_animation_graph_make(
	char const *biped_name,
	char const (*node_names)[32],
	struct h2_animation_pose const *node_defaults,
	short const *node_parents,
	short node_count,
	char *why,
	long why_size)
{
	return h3_animation_graph_make_from(NULL, biped_name, node_names, node_defaults, node_parents, node_count, why, why_size);
}

H3_ALIGN struct h2_graph *h3_animation_graph_make_from(
	struct h3_cache *cache,
	char const *biped_name,
	char const (*node_names)[32],
	struct h2_animation_pose const *node_defaults,
	short const *node_parents,
	short node_count,
	char *why,
	long why_size)
{
	char const *graph_name = biped_name ? graph_of(biped_name) : NULL;
	struct h3_graph *g;
	struct h3_cache_tag t;
	struct h2_graph_source source;
	struct h2_graph *graph = NULL;
	unsigned long long started = vita_host_time_us();
	unsigned long long read_done;

	if (why && why_size > 0)
		why[0] = 0;
	if (!graph_name)
	{
		why_set(why, why_size, "no Halo 3 graph for this biped");
		return NULL;
	}
	g = calloc(1, sizeof(*g));
	if (!g)
	{
		why_set(why, why_size, "out of memory");
		return NULL;
	}
	/* (the caller's open map - a Halo 3 level's own, h3_level.c - where it
	holds the graph; else one found) */
	if (cache && h3_cache_find_tag(cache, 'jmad', graph_name) != NONE)
	{
		g->cache = cache;
		g->cache_borrowed = TRUE;
	}
	else
		g->cache = graph_cache_open(graph_name, why, why_size);
	if (!g->cache)
	{
		h3_graph_free(g);
		return NULL;
	}
	g->tag = h3_cache_find_tag(g->cache, 'jmad', graph_name);
	if (g->tag == NONE || !h3_cache_tag_get(g->cache, g->tag, &t) || !t.address ||
		!h3_cache_block(g->cache, t.address + JMAD_ANIMATIONS, JMAD_ANIMATION_SIZE, &g->animation_count, &g->animations) ||
		g->animation_count < 1 || g->animation_count > MAXIMUM_ANIMATIONS)
	{
		why_set(why, why_size, "its animation graph cannot be read");
		h3_graph_free(g);
		return NULL;
	}
	g->address = t.address;
	if (!animations_read(g, why, why_size))
	{
		h3_graph_free(g);
		return NULL;
	}
	read_done = vita_host_time_us();
	skeletons_log(g, node_names, node_parents, node_count);
	source_fill(&source, g);
	graph = h2_graph_make_from(&source, g->tag, node_names, node_defaults, node_parents, node_count, FALSE, why, why_size);
	if (graph)
	{
		struct h2_graph_report report;

		h2_graph_report(graph, &report);
		platform_log("h3 animation: %s: %s's graph from %s: %ld of %ld animations' data (%lu KB) read in %.1f ms; %ld animations made of %ld, "
			"%d seats, %ld classes, %ld slots missing, %ld KB, made in %.1f ms",
			biped_name, graph_name, h3_cache_name(g->cache), g->got, g->animation_count, g->bytes_size / 1024,
			(double)(read_done - started) / 1000.0, report.animations, report.h2_animations, (int)report.seats, report.classes, report.missing,
			report.bytes / 1024, (double)(vita_host_time_us() - read_done) / 1000.0);
	}
	else
		platform_log("h3 animation: %s: %s's graph not made: %s", biped_name, graph_name, why && why[0] ? why : "?");
	h3_graph_free(g);
	return graph;
}

H3_ALIGN struct h3_animation_source *h3_animation_source_open(
	struct h3_cache *cache,
	long jmad_tag,
	struct h2_graph_source *source,
	char *why,
	long why_size)
{
	struct h3_graph *g;
	struct h3_cache_tag t;

	if (why && why_size > 0)
		why[0] = 0;
	if (!cache || !source || jmad_tag == NONE)
	{
		why_set(why, why_size, "no graph to open");
		return NULL;
	}
	g = calloc(1, sizeof(*g));
	if (!g)
	{
		why_set(why, why_size, "out of memory");
		return NULL;
	}
	g->cache = cache;
	g->cache_borrowed = TRUE;
	g->tag = jmad_tag;
	if (!h3_cache_tag_get(cache, jmad_tag, &t) || t.group_tag != 'jmad' || !t.address ||
		!h3_cache_block(cache, t.address + JMAD_ANIMATIONS, JMAD_ANIMATION_SIZE, &g->animation_count, &g->animations) ||
		g->animation_count < 1 || g->animation_count > MAXIMUM_ANIMATIONS)
	{
		why_set(why, why_size, "the animation graph cannot be read");
		h3_graph_free(g);
		return NULL;
	}
	g->address = t.address;
	if (!animations_read(g, why, why_size))
	{
		h3_graph_free(g);
		return NULL;
	}
	source_fill(source, g);
	return (struct h3_animation_source *)g;
}

H3_ALIGN void h3_animation_source_close(
	struct h3_animation_source *opened)
{
	h3_graph_free((struct h3_graph *)opened);
}
