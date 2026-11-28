/*
H2_RENDER_ONLY.C

(perf/vita-native-2) Halo 2's render-only nodes: a Halo 2 model (hlmt)
names, at 0xA4, the nodes nothing but its drawing needs (a character's
fingers, toes, face and eyes, a weapon's moving bits: an Elite's 30 of 55,
a Marine's 31 of 53). Halo 2's tick does not sample their animation (they
keep the render model's default orientations) and its draw samples them
for the objects it draws (the Xbox's 0xbd090 / 0xbd610).

Here, a visual-only saving with the same gameplay: an object none of whose
own or its parents' drawing was found in the last frames does not decode
its render-only nodes' keyframes while its node matrices are computed
(model_animations.c gives an identity for them: its caches never keep an
identity); its matrices are still all computed, so an object that comes
into view shows their real pose a tick later (at worst a frame or two of
rest-pose fingers and face). The node 0 never. Nothing of the game reads
those nodes (the proof: the tick hash with them left out of it,
HALO_TICK_HASH_MASK_RENDER_ONLY=1, IDENTICAL between HALO_H2_RENDER_ONLY=0
and =2 - every object's skipped).

HALO_H2_RENDER_ONLY: 1 as above, 0 (default for now) every node sampled
(as before), 2 the render-only nodes of every object skipped (the proof's
and a timing's upper bound); read again at @set. Never while a cinematic
plays.
The masks: made at a Halo 2 level's load (h2_scripts.c, the level's map
open) for the made bipeds (the characters: most of the nodes), from the
Halo 2 biped or creature each is made of (h2_bipeds_made_from), matched to
this engine's model's nodes by name (the maker's node collapse keeps names).
*/

#include "cseries.h"
#include "h2_cache.h"
#include "h2_biped.h"
#include "h2_render_only.h"
#include "h2_render_nodes.h"
#include "cache/cache_files.h"
#include "cutscene/cinematics.h"
#include "models/model_definitions.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "tag_files/tag_files.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void platform_log(const char *format, ...);

#define RENDER_ONLY_TABLE 2048
#define RENDER_ONLY_RECENT_FRAMES 8

struct render_only_entry
{
	long definition_index;
	unsigned long long mask;
};

static struct render_only_entry render_only_table[RENDER_ONLY_TABLE];
/* (perf/ce-floor-2: global - h2_render_only.h, objects.c asks it before the call) */
long h2_render_only_definitions;
#define render_only_definitions h2_render_only_definitions
/* (the render's frames, and the frame each object was last found to be drawn in, +1) */
static volatile unsigned long render_only_frame;
static volatile unsigned long render_only_drawn_frame[MAXIMUM_OBJECTS_PER_MAP];

/* (model_animations.c) the nodes skipped now: the tick's, inside an object's node compute */
unsigned long long h2_render_only_skip_mask;
unsigned long h2_render_only_census[3]; /* computes with a mask, of them skipped, keyframes skipped */

/* (read again at @set; off unless set: a visual approximation) */
static int render_only_mode(void)
{
	extern volatile unsigned long halo_settings_generation;
	static int mode = -1;
	static unsigned long seen;

	if (mode < 0 || seen != halo_settings_generation)
	{
		const char *setting = getenv("HALO_H2_RENDER_ONLY");

		seen = halo_settings_generation;
		mode = setting && *setting ? atoi(setting) : 0;
	}
	return mode;
}

static struct render_only_entry *render_only_slot(long definition_index)
{
	unsigned long k = (unsigned long)DATUM_INDEX_TO_ABSOLUTE_INDEX(definition_index) % RENDER_ONLY_TABLE;
	unsigned long n;

	for (n = 0; n < RENDER_ONLY_TABLE; n++, k = (k + 1) % RENDER_ONLY_TABLE)
	{
		struct render_only_entry *entry = &render_only_table[k];

		if (entry->definition_index == definition_index || entry->definition_index == NONE)
			return entry;
	}
	return NULL;
}

unsigned long long h2_render_only_definition_mask(long definition_index)
{
	struct render_only_entry *entry;

	if (!render_only_definitions)
		return 0;
	entry = render_only_slot(definition_index);
	return entry && entry->definition_index == definition_index ? entry->mask : 0;
}

/* (h2_render_nodes.c) the definitions with masks, one by one: FALSE past the last */
boolean h2_render_only_definition_at(short which, long *definition_index, unsigned long long *mask)
{
	long i;
	short seen = 0;

	for (i = 0; i < RENDER_ONLY_TABLE; i++)
	{
		if (render_only_table[i].definition_index == NONE || !render_only_table[i].mask)
			continue;
		if (seen++ == which)
		{
			*definition_index = render_only_table[i].definition_index;
			*mask = render_only_table[i].mask;
			return TRUE;
		}
	}
	return FALSE;
}

void h2_render_only_dispose(void)
{
	long i;

	h2_render_nodes_dispose();
	for (i = 0; i < RENDER_ONLY_TABLE; i++)
	{
		render_only_table[i].definition_index = NONE;
		render_only_table[i].mask = 0;
	}
	render_only_definitions = 0;
}

/* one definition's mask: its Halo 2 tag's model's render-only nodes, by name in this engine's model */
static long render_only_why[6]; /* no model, no Halo 2 tag, no hlmt, no bits, no render model, none matched */

static unsigned long long definition_mask(struct h2_cache *cache, long tag_index, long h2_tag, long *h2_nodes, long *h2_set)
{
	struct object_definition const *definition = object_definition_get(tag_index);
	struct model const *model;
	struct h2_cache_tag t, hlmt, mode;
	long hlmt_tag, mode_tag, count, i, n;
	unsigned long nodes;
	unsigned char const *bits;
	unsigned char set[32];
	unsigned long long mask = 0;
	short model_node_count;

	*h2_nodes = *h2_set = 0;
	if (!definition || definition->object.model.index == NONE)
		return render_only_why[0]++, 0;
	model = model_definition_get(definition->object.model.index);
	if (!model || model->nodes.count < 2)
		return render_only_why[0]++, 0;
	model_node_count = (short)MIN(model->nodes.count, MAXIMUM_NODES_PER_MODEL);
	if (h2_tag == NONE || !h2_cache_tag_get(cache, h2_tag, &t))
		return render_only_why[1]++, 0;
	hlmt_tag = h2_cache_reference(cache, t.address + 0x34);
	if (hlmt_tag == NONE || !h2_cache_tag_get(cache, hlmt_tag, &hlmt) || hlmt.group_tag != 'hlmt')
		return render_only_why[2]++, 0;
	bits = (unsigned char const *)h2_cache_at(cache, hlmt.address + 0xA4, 32);
	if (!bits)
		return render_only_why[2]++, 0;
	/* (a copy: a later read may let the piece go) */
	memcpy(set, bits, sizeof(set));
	for (i = 0; i < 32 && !set[i]; i++)
		;
	if (i == 32)
		return render_only_why[3]++, 0;
	mode_tag = h2_cache_reference(cache, hlmt.address + 0);
	if (mode_tag == NONE || !h2_cache_tag_get(cache, mode_tag, &mode) || mode.group_tag != 'mode' ||
		!h2_cache_block(cache, mode.address + 0x48, 0x60, &count, &nodes) || count < 1 || count > 256)
	{
		return render_only_why[4]++, 0;
	}
	*h2_nodes = count;
	for (i = 1; i < count; i++)
	{
		char name[64];

		if (!(set[i >> 3] >> (i & 7) & 1))
			continue;
		(*h2_set)++;
		if (!h2_cache_string(cache, h2_cache_read32(cache, nodes + 0x60 * (unsigned long)i), name, sizeof(name)))
			continue;
		/* (never the root) */
		for (n = 1; n < model_node_count; n++)
		{
			struct model_node const *ce = TAG_BLOCK_GET_ELEMENT(&model->nodes, n, struct model_node);

			if (!strncmp(ce->name, name, TAG_STRING_LENGTH))
			{
				mask |= 1ULL << n;
				break;
			}
		}
	}
	if (!mask)
		render_only_why[5]++;
	return mask;
}

void h2_render_only_map_loaded(struct h2_cache *cache)
{
	long definitions = 0, nodes_masked = 0, bipeds_seen = 0;
	short b;

	h2_render_only_dispose();
	memset(render_only_why, 0, sizeof(render_only_why));
	if (!cache)
	{
		platform_log("h2 render-only: no map open");
		return;
	}
	/* (the made bipeds - the characters, most of the nodes: "h2\characters\<name>_mp\<name>_mp",
	h2_biped.c - and the Halo 2 biped or creature each is made of) */
	for (b = 0; h2_bipeds_source_name(b); b++)
	{
		char path[256];
		long tag_index, h2_nodes, h2_set;
		unsigned long long mask;
		struct render_only_entry *entry;
		char const *name = h2_bipeds_source_name(b);

		snprintf(path, sizeof(path), "h2\\characters\\%s_mp\\%s_mp", name, name);
		tag_index = tag_loaded('bipd', path);
		if (tag_index == NONE)
			continue;
		bipeds_seen++;
		mask = definition_mask(cache, tag_index, h2_bipeds_made_from(cache, b), &h2_nodes, &h2_set);
		if (!mask || !(entry = render_only_slot(tag_index)))
			continue;
		entry->definition_index = tag_index;
		entry->mask = mask;
		definitions++;
		nodes_masked += __builtin_popcountll(mask);
		if (getenv("HALO_H2_RENDER_ONLY_LOG"))
			platform_log("h2 render-only: %s: %d of its %d nodes (Halo 2's model: %ld set of %ld)", path,
				__builtin_popcountll(mask), (int)MIN(model_definition_get(object_definition_get(tag_index)->object.model.index)->nodes.count, 64),
				h2_set, h2_nodes);
	}
	render_only_definitions = definitions;
	platform_log("h2 render-only: %ld of the level's %ld bipeds have render-only nodes (%ld nodes in all; the others: %ld no model, "
		"%ld no Halo 2 tag, %ld no hlmt, %ld none set, %ld no render model, %ld none matched); HALO_H2_RENDER_ONLY %d",
		definitions, bipeds_seen, nodes_masked, render_only_why[0], render_only_why[1], render_only_why[2], render_only_why[3],
		render_only_why[4], render_only_why[5], render_only_mode());
	/* (perf/h2-render-nodes) their matrices left to the render */
	h2_render_nodes_map_loaded();
}

/* (render_objects.c) an object found to be drawn this frame */
void h2_render_only_frame_begin(void)
{
	render_only_frame++;
}

void h2_render_only_drawn(long object_index)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);

	if (render_only_definitions && absolute_index >= 0 && absolute_index < MAXIMUM_OBJECTS_PER_MAP)
		render_only_drawn_frame[absolute_index] = render_only_frame + 1;
}

static boolean drawn_recently(long object_index)
{
	short depth;

	/* (or what it is attached to: a held weapon, a rider) */
	for (depth = 0; depth < 8 && object_index != NONE; depth++)
	{
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
		struct object_datum *object;
		unsigned long drawn;

		if (absolute_index < 0 || absolute_index >= MAXIMUM_OBJECTS_PER_MAP)
			return TRUE;
		drawn = render_only_drawn_frame[absolute_index];
		if (drawn && render_only_frame + 1 - drawn <= RENDER_ONLY_RECENT_FRAMES)
			return TRUE;
		object = object_try_and_get(object_index);
		if (!object)
			return TRUE;
		object_index = object->object.parent_object_index;
	}
	return FALSE;
}

/* (objects.c, object_compute_node_matrices) the nodes to skip for this compute (0: none) */
unsigned long long h2_render_only_begin(long object_index, long definition_index)
{
	unsigned long long mask;
	int mode;

	h2_render_only_skip_mask = 0;
	if (!render_only_definitions || !(mode = render_only_mode()))
		return 0;
	mask = h2_render_only_definition_mask(definition_index);
	if (!mask)
		return 0;
	h2_render_only_census[0]++;
	if (getenv("HALO_H2_RENDER_ONLY_LOG") && h2_render_only_census[0] % 9000 == 0)
		platform_log("h2 render-only census: %lu computes of objects with render-only nodes, %lu of them skipped, %lu keyframes not decoded",
			h2_render_only_census[0], h2_render_only_census[1], h2_render_only_census[2]);
	if (mode != 2 && (drawn_recently(object_index) || cinematic_in_progress()))
		return 0;
	h2_render_only_census[1]++;
	h2_render_only_skip_mask = mask;
	return mask;
}

void h2_render_only_end(void)
{
	h2_render_only_skip_mask = 0;
}

/* (tick_hash.c, HALO_TICK_HASH_MASK_RENDER_ONLY=1) every object's render-only
nodes' orientations (both) and matrices hashed as zeros: saved and zeroed
(restore 0), put back (1) */
static unsigned char *hash_saved;
static unsigned long hash_saved_size, hash_saved_used;

static void hash_node_block(long object_index, struct object_header_block_reference *reference, unsigned long element_size,
	unsigned long long mask, int restore)
{
	unsigned char *block;
	long count, n;

	if (!reference->size)
		return;
	block = (unsigned char *)object_header_block_get(object_index, reference);
	count = reference->size / (long)element_size;
	for (n = 1; n < count && n < 64; n++)
	{
		if (!(mask >> n & 1))
			continue;
		if (restore)
		{
			if (hash_saved_used + element_size <= hash_saved_size)
				memcpy(block + n * element_size, hash_saved + hash_saved_used, element_size);
		}
		else
		{
			if (hash_saved_used + element_size > hash_saved_size)
			{
				unsigned long size = hash_saved_size ? hash_saved_size * 2 : 1 << 20;
				unsigned char *grown = (unsigned char *)realloc(hash_saved, size);

				if (!grown)
					continue;
				hash_saved = grown;
				hash_saved_size = size;
			}
			memcpy(hash_saved + hash_saved_used, block + n * element_size, element_size);
			memset(block + n * element_size, 0, element_size);
		}
		hash_saved_used += element_size;
	}
}

void h2_render_only_hash(int restore)
{
	struct object_iterator iterator;
	struct object_datum *object;

	if (!render_only_definitions)
		return;
	hash_saved_used = 0;
	object_iterator_new(&iterator, NONE, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		unsigned long long mask = h2_render_only_definition_mask(object->definition_index);

		if (!mask)
			continue;
		hash_node_block(iterator.index, &object->object.node_orientations, sizeof(real_orientation), mask, restore);
		hash_node_block(iterator.index, &object->object.original_node_orientations, sizeof(real_orientation), mask, restore);
		hash_node_block(iterator.index, &object->object.node_matrices, sizeof(real_matrix4x3), mask, restore);
	}
}
