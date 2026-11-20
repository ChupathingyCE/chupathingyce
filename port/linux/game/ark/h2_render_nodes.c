/*
H2_RENDER_NODES.C

(perf/h2-render-nodes) Halo 2's render-only nodes, their matrices made where
the model is drawn. A Halo 2 model (hlmt 0xA4) names the nodes nothing but
its drawing needs - a character's fingers, toes, face and eyes: an Elite's
30 of 55, the Master Chief's 14 of 33 - and Halo 2's tick leaves them to the
draw (the Xbox's 0xbd090 / 0xbd610: the draw resamples them for the objects
it draws). Here the tick still makes their orientations - the whole blend,
overlay, aiming and interpolation chain, as before: the orientations are
game state (an animation change's interpolation starts from them) and that
chain barely keys these nodes (perf/vita-native-2's census) - but leaves
their MATRICES (the node tree's per-node work) to the render, which makes
them for the objects it draws, from the copy of the tick it draws: the same
operation on the same inputs (matrix4x3_multiply_orientation of the parent's
matrix and the node's orientation), so the same bits.

Which nodes ("deferred"): the render-only nodes (h2_render_only.c's masks:
the made bipeds' Halo 2 hlmt bits, by node name) less any node something of
the game reads - a node with a marker (attachments, weapons, IK, effects,
lights, sounds: markers are read from the matrices) or with collision
geometry or a pathfinding sphere - and less any node with such a node below
it: what is left is whole subtrees (a hand's fingers, the face under the
head), each hanging from a parent the tick makes.

The tick (objects.c): an object not dead and with no ragdoll skips the
deferred subtrees in its node tree, then keeps those subtrees' parents'
matrices as the tree made them (before the unit's IK moves a hand: a finger
hangs from the hand as the tree had it, as before). One a cutscene, a
speaking face or an emotion poses (h2_cinematics_node_matrices, right after
the tree) has them made there first and is not deferred that compute
(h2_render_nodes_make_whole: no second walk of the cutscene's objects per
compute - its entries stay listed after a cutscene, and each is a cache miss
on the Vita). Its deferred
nodes' live matrices are then stale; nothing of the tick reads them (the
proof: the tick hash with those matrices left out, IDENTICAL between
HALO_H2_RENDER_NODES 0 and 1, and with them poisoned, 3). A tick reader of
every node (a body going limp, a ragdoll made) makes them whole first
(h2_render_nodes_complete: the same operation, the same bits).

The render (render_interpolation.c): each capture of the tick's poses copies
a deferred object's deferred orientations and the kept parents; the render
makes the matrices in that capture the first time it reads the object from
it (both snapshots of an interpolated frame: each its own tick's). An object
drawn from the live matrices (no tick in flight, or no capture) has them
made in a copy of its own. Nothing of the game state is written by the
render.

HALO_H2_RENDER_NODES (a native switch: HALO_NATIVE_TICK=0 turns it off; read
again at @set): 1 on (default), 0 off (every matrix in the tick, as before),
2 check (the tick makes every matrix as before AND the render makes the
deferred ones again from the capture, compared bit for bit, logged), 3 on with
the deferred live matrices poisoned (not a number: a test of the tick's
readers). HALO_H2_RENDER_NODES_LOG=1: each definition's deferred nodes.
*/

#include "cseries.h"
#include "h2_render_only.h"
#include "h2_render_nodes.h"
#include "native_switch.h"
#include "render_epoch.h"
#include "math/real_math.h"
#include "models/model_definitions.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "objects/object_types.h"
#include "physics/collision_model_definitions.h"
#include "physics/collision_bsp_definitions.h"
#include "tag_files/tag_groups.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void platform_log(const char *format, ...);
extern boolean ragdolls_object_ragdolled(long object_index);

#define RENDER_NODES_DEFINITIONS 256
#define RENDER_NODES_SLOTS 256

struct render_nodes_definition
{
	long definition_index;
	unsigned long long mask;
	short node_count;
	/* the deferred nodes in the tick's tree order (parents first), each
	one's parent, and where that parent's matrix is: a deferred node's
	(parent_kept NONE) or a kept parent's (its index in kept[]) */
	short order_count;
	signed char order[MAXIMUM_NODES_PER_MODEL];
	signed char parent[MAXIMUM_NODES_PER_MODEL];
	signed char parent_kept[MAXIMUM_NODES_PER_MODEL];
	short kept_count;
	signed char kept[RENDER_NODES_PARENTS_MAXIMUM];
};

/* an object whose last compute deferred nodes: its kept parents */
struct render_nodes_slot
{
	long object_index;
	/* (the game state it is of: a revert or a load leaves it meaningless) */
	unsigned long generation;
	short definition;
	/* (the check: the tick made them all, the render compares) */
	boolean made_all;
	unsigned long long deferred;
	real_matrix4x3 kept[RENDER_NODES_PARENTS_MAXIMUM];
};

static struct render_nodes_definition definitions[RENDER_NODES_DEFINITIONS];
/* (perf/ce-floor-2: global - h2_render_nodes.h, objects.c asks it before the call) */
short h2_render_nodes_definition_count;
#define definition_count h2_render_nodes_definition_count
static struct render_nodes_slot slots[RENDER_NODES_SLOTS];
/* by absolute object index: its slot + 1 (0: none) */
static short slot_of[MAXIMUM_OBJECTS_PER_MAP];
static short slots_free[RENDER_NODES_SLOTS];
static short slots_free_count;

static struct
{
	unsigned long computes, deferred_computes, nodes_deferred, completes, refused_slot, posed;
	unsigned long fills, filled_nodes, live_fills;
	unsigned long checked, different;
} census;
static unsigned long census_check_logged;

int h2_render_nodes_mode(void)
{
	static int mode = -1;
	static unsigned long seen;

	halo_native_switch("HALO_H2_RENDER_NODES", &mode, &seen);
	return mode;
}

/* ---------- the definitions */

void h2_render_nodes_dispose(void)
{
	short i;

	definition_count = 0;
	memset(slot_of, 0, sizeof(slot_of));
	for (i = 0; i < RENDER_NODES_SLOTS; i++)
	{
		slots[i].object_index = NONE;
		slots[i].deferred = 0;
		slots_free[i] = (short)(RENDER_NODES_SLOTS - 1 - i);
	}
	slots_free_count = RENDER_NODES_SLOTS;
}

static short definition_find(long definition_index)
{
	short i;

	for (i = 0; i < definition_count; i++)
	{
		if (definitions[i].definition_index == definition_index)
			return i;
	}
	return NONE;
}

/* why a definition has none: no model, a tree it cannot walk, nothing left, no room */
static long none_why[4];

static boolean definition_build(long definition_index, unsigned long long render_only, struct render_nodes_definition *d)
{
	struct object_definition const *definition = object_definition_get(definition_index);
	struct model const *model;
	unsigned long long read = 0, mask;
	short node_count, n, i, queue_count, queue_at;
	short queue[MAXIMUM_NODES_PER_MODEL];
	unsigned char seen[MAXIMUM_NODES_PER_MODEL];
	boolean changed;

	memset(d, 0, sizeof(*d));
	if (!definition || definition->object.model.index == NONE ||
		!(model = model_definition_get(definition->object.model.index)) || model->nodes.count < 2 ||
		model->nodes.count > MAXIMUM_NODES_PER_MODEL)
	{
		return none_why[0]++, FALSE;
	}
	node_count = (short)model->nodes.count;
	/* (the tree, walked as object_compute_node_matrices walks it: each
	node once, every link one the model has, parents first) */
	memset(seen, 0, sizeof(seen));
	queue[0] = 0;
	queue_count = 1;
	for (queue_at = 0; queue_at < queue_count; queue_at++)
	{
		struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, queue[queue_at], struct model_node);

		if (seen[queue[queue_at]]++)
			return none_why[1]++, FALSE;
		if (queue[queue_at] != 0 && !VALID_INDEX(node->parent_node_index, node_count))
			return none_why[1]++, FALSE;
		if (node->next_sibling_node_index != NONE)
		{
			if (!VALID_INDEX(node->next_sibling_node_index, node_count) || queue_count >= MAXIMUM_NODES_PER_MODEL)
				return none_why[1]++, FALSE;
			queue[queue_count++] = node->next_sibling_node_index;
		}
		if (node->first_child_node_index != NONE)
		{
			if (!VALID_INDEX(node->first_child_node_index, node_count) || queue_count >= MAXIMUM_NODES_PER_MODEL)
				return none_why[1]++, FALSE;
			queue[queue_count++] = node->first_child_node_index;
		}
	}
	if (queue_count != node_count)
		return none_why[1]++, FALSE;
	/* (what the game reads: the markers' nodes) */
	for (i = 0; i < model->markers.count; i++)
	{
		struct model_marker const *marker = TAG_BLOCK_GET_ELEMENT(&model->markers, i, struct model_marker);
		long k;

		for (k = 0; k < marker->instances.count; k++)
		{
			struct model_marker_instance const *instance =
				TAG_BLOCK_GET_ELEMENT(&marker->instances, k, struct model_marker_instance);

			if (instance->node_index < MAXIMUM_NODES_PER_MODEL)
				read |= 1ULL << instance->node_index;
		}
	}
	/* (... the collision model's nodes with geometry, its pathfinding spheres') */
	if (definition->object.collision_model.index != NONE)
	{
		struct collision_model const *collision = collision_model_definition_get(definition->object.collision_model.index);

		for (i = 0; collision && i < collision->nodes.count && i < MAXIMUM_NODES_PER_MODEL; i++)
		{
			struct collision_node const *node = TAG_BLOCK_GET_ELEMENT(&collision->nodes, i, struct collision_node);

			if (node->bsps.count > 0)
				read |= 1ULL << i;
		}
		for (i = 0; collision && i < collision->pathfinding_spheres.count; i++)
		{
			struct pathfinding_sphere const *sphere =
				TAG_BLOCK_GET_ELEMENT(&collision->pathfinding_spheres, i, struct pathfinding_sphere);

			if (sphere->node_index >= 0 && sphere->node_index < MAXIMUM_NODES_PER_MODEL)
				read |= 1ULL << sphere->node_index;
		}
	}
	/* (whole subtrees: a node is kept if it is read or anything below it is) */
	mask = render_only & ~read & ~1ULL;
	if (node_count < 64)
		mask &= (1ULL << node_count) - 1;
	do
	{
		changed = FALSE;
		for (n = 1; n < node_count; n++)
		{
			struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, n, struct model_node);
			short parent = node->parent_node_index;

			if (!(mask >> n & 1) && (mask >> parent & 1))
			{
				mask &= ~(1ULL << parent);
				changed = TRUE;
			}
		}
	}
	while (changed);
	if (!mask)
		return none_why[2]++, FALSE;
	d->definition_index = definition_index;
	d->mask = mask;
	d->node_count = node_count;
	for (i = 0; i < node_count; i++)
	{
		struct model_node const *node;
		short parent, k;

		n = queue[i];
		if (!(mask >> n & 1))
			continue;
		node = TAG_BLOCK_GET_ELEMENT(&model->nodes, n, struct model_node);
		parent = node->parent_node_index;
		d->order[d->order_count] = (signed char)n;
		d->parent[d->order_count] = (signed char)parent;
		d->parent_kept[d->order_count] = NONE;
		if (!(mask >> parent & 1))
		{
			for (k = 0; k < d->kept_count && d->kept[k] != parent; k++)
				;
			if (k == d->kept_count)
			{
				if (d->kept_count >= RENDER_NODES_PARENTS_MAXIMUM)
					return none_why[3]++, FALSE;
				d->kept[d->kept_count++] = (signed char)parent;
			}
			d->parent_kept[d->order_count] = (signed char)k;
		}
		d->order_count++;
	}
	return TRUE;
}

void h2_render_nodes_map_loaded(void)
{
	long nodes = 0;
	short i;

	h2_render_nodes_dispose();
	memset(none_why, 0, sizeof(none_why));
	/* (h2_render_only.c's definitions: the made bipeds) */
	for (i = 0; i < RENDER_NODES_DEFINITIONS; i++)
	{
		long definition_index;
		unsigned long long render_only;

		if (!h2_render_only_definition_at(i, &definition_index, &render_only))
			break;
		if (definition_count >= RENDER_NODES_DEFINITIONS)
			break;
		if (!definition_build(definition_index, render_only, &definitions[definition_count]))
			continue;
		nodes += definitions[definition_count].order_count;
		if (getenv("HALO_H2_RENDER_NODES_LOG"))
		{
			struct render_nodes_definition const *d = &definitions[definition_count];

			platform_log("h2 render nodes: %s: %d of its %d nodes deferred (render-only %d), %d kept parents",
				tag_get_name(definition_index), d->order_count, d->node_count, __builtin_popcountll(render_only), d->kept_count);
		}
		definition_count++;
	}
	platform_log("h2 render nodes: %d definitions, %ld nodes deferred (none: %ld no model, %ld tree, %ld all read, %ld parents); "
		"HALO_H2_RENDER_NODES %d", definition_count, nodes, none_why[0], none_why[1], none_why[2], none_why[3],
		h2_render_nodes_mode());
}

/* ---------- the tick */

static struct render_nodes_slot *slot_get(long object_index)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	short s;

	if (absolute_index < 0 || absolute_index >= MAXIMUM_OBJECTS_PER_MAP || !(s = slot_of[absolute_index]))
		return NULL;
	return slots[s - 1].object_index == object_index && slots[s - 1].generation == halo_map_generation ? &slots[s - 1] : NULL;
}

static void slot_release(long object_index)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	struct render_nodes_slot *slot = slot_get(object_index);

	if (!slot)
		return;
	slot->object_index = NONE;
	slot->deferred = 0;
	slots_free[slots_free_count++] = slot_of[absolute_index] - 1;
	slot_of[absolute_index] = 0;
}

static struct render_nodes_slot *slot_new(long object_index, short definition)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	struct render_nodes_slot *slot;
	short s;

	if (absolute_index < 0 || absolute_index >= MAXIMUM_OBJECTS_PER_MAP)
		return NULL;
	if ((slot = slot_get(object_index)) != NULL)
	{
		slot->definition = definition;
		return slot;
	}
	/* (an index's slot of an earlier game state or of an object deleted
	since: its own) */
	if ((s = slot_of[absolute_index]) != 0)
	{
		slot = &slots[s - 1];
		slot->object_index = object_index;
		slot->generation = halo_map_generation;
		slot->definition = definition;
		slot->deferred = 0;
		return slot;
	}
	if (!slots_free_count)
	{
		/* (slots of objects deleted since, given back) */
		for (s = 0; s < RENDER_NODES_SLOTS; s++)
		{
			if (slots[s].object_index != NONE &&
				(slots[s].generation != halo_map_generation || !object_try_and_get(slots[s].object_index)))
			{
				long owner = DATUM_INDEX_TO_ABSOLUTE_INDEX(slots[s].object_index);

				if (owner >= 0 && owner < MAXIMUM_OBJECTS_PER_MAP && slot_of[owner] == s + 1)
					slot_of[owner] = 0;
				slots[s].object_index = NONE;
				slots[s].deferred = 0;
				slots_free[slots_free_count++] = s;
			}
		}
		if (!slots_free_count)
			return NULL;
	}
	s = slots_free[--slots_free_count];
	slot = &slots[s];
	slot->object_index = object_index;
	slot->generation = halo_map_generation;
	slot->definition = definition;
	slot->deferred = 0;
	slot_of[absolute_index] = (short)(s + 1);
	return slot;
}

unsigned long long h2_render_nodes_begin(long object_index, struct object_datum const *object,
	struct h2_render_nodes_compute *compute)
{
	int mode;
	short d;

	compute->definition = NONE;
	compute->mask = 0;
	if (!definition_count)
		return 0;
	mode = h2_render_nodes_mode();
	if (!mode || (d = definition_find(object->definition_index)) == NONE)
	{
		if (slot_get(object_index) && halo_epoch_mutator_alone_inline())
			slot_release(object_index);
		else if (slot_get(object_index))
			slot_get(object_index)->deferred = 0;
		return 0;
	}
	census.computes++;
	/* (not when anything reads every node of them: a dead body, a ragdoll;
	what poses them after the tree - a cutscene, a speaking face - makes
	them whole first, h2_render_nodes_make_whole) */
	if (TEST_FLAG(object->object.flags, _object_dead_bit) || ragdolls_object_ragdolled(object_index))
	{
		if (slot_get(object_index))
			slot_get(object_index)->deferred = 0;
		return 0;
	}
	compute->definition = d;
	compute->mask = definitions[d].mask;
	/* (the check: every matrix made, as before) */
	return mode == 2 ? 0 : compute->mask;
}

void h2_render_nodes_end(long object_index, struct h2_render_nodes_compute const *compute, real_matrix4x3 const *matrices)
{
	struct render_nodes_definition const *d;
	struct render_nodes_slot *slot;
	short k;

	if (compute->definition == NONE)
		return;
	d = &definitions[compute->definition];
	slot = halo_epoch_mutator_alone_inline() ? slot_new(object_index, (short)(d - definitions)) : slot_get(object_index);
	if (!slot)
	{
		/* (no room: this compute's skipped matrices made here after all) */
		census.refused_slot++;
		if (h2_render_nodes_mode() != 2)
		{
			struct object_datum *object = object_get(object_index);
			real_orientation const *orientations = (real_orientation const *)object_header_block_get(object_index,
				&object->object.node_orientations);
			real_matrix4x3 *live = (real_matrix4x3 *)object_header_block_get(object_index, &object->object.node_matrices);

			for (k = 0; k < d->order_count; k++)
				matrix4x3_multiply_orientation(&live[d->parent[k]], &orientations[d->order[k]], &live[d->order[k]]);
		}
		return;
	}
	for (k = 0; k < d->kept_count; k++)
		slot->kept[k] = matrices[d->kept[k]];
	slot->deferred = compute->mask;
	slot->made_all = h2_render_nodes_mode() == 2;
	census.deferred_computes++;
	census.nodes_deferred += d->order_count;
	if (h2_render_nodes_mode() == 3)
	{
		/* (the test: the deferred live matrices not a number) */
		real_matrix4x3 *live = (real_matrix4x3 *)matrices;
		real poison = (real)nan("");

		for (k = 0; k < d->order_count; k++)
		{
			real *p = (real *)&live[d->order[k]];
			unsigned long w;

			for (w = 0; w < sizeof(real_matrix4x3) / sizeof(real); w++)
				p[w] = poison;
		}
	}
	if (census.computes % 20000 == 0 && getenv("HALO_H2_RENDER_NODES_LOG"))
	{
		platform_log("h2 render nodes: %lu computes of objects with deferrable nodes, %lu deferred (%lu nodes), %lu completed "
			"by the tick, %lu posed by cinematics/faces, %lu no slot; render: %lu fills (%lu nodes), %lu live; check %lu compared, "
			"%lu different",
			census.computes, census.deferred_computes, census.nodes_deferred, census.completes, census.posed, census.refused_slot,
			census.fills, census.filled_nodes, census.live_fills, census.checked, census.different);
	}
}

static void fill(struct render_nodes_definition const *d, real_matrix4x3 *matrices, real_orientation const *orientations,
	short orientation_stride_all, real_matrix4x3 const *kept, int check);

void h2_render_nodes_make_whole(long object_index, struct h2_render_nodes_compute *compute, real_matrix4x3 *matrices)
{
	struct render_nodes_definition const *d;
	struct render_nodes_slot *slot;
	struct object_datum *object;

	if (compute->definition == NONE)
		return;
	d = &definitions[compute->definition];
	object = object_get(object_index);
	/* (the tree's parents, before any postprocess: what it would have made;
	the check: the tree made them, compared) */
	fill(d, matrices, (real_orientation const *)object_header_block_get(object_index, &object->object.node_orientations),
		TRUE, NULL, h2_render_nodes_mode() == 2);
	census.posed++;
	compute->definition = NONE;
	compute->mask = 0;
	if ((slot = slot_get(object_index)) != NULL)
		slot->deferred = 0;
}

/* the deferred matrices made from the parents' and the orientations: into
matrices[] (check: compared with what is there) */
static void fill(struct render_nodes_definition const *d, real_matrix4x3 *matrices, real_orientation const *orientations,
	short orientation_stride_all, real_matrix4x3 const *kept, int check)
{
	short k;

	for (k = 0; k < d->order_count; k++)
	{
		short n = d->order[k];
		real_matrix4x3 const *parent = d->parent_kept[k] == NONE || !kept ? &matrices[d->parent[k]] : &kept[d->parent_kept[k]];
		real_orientation const *orientation = orientation_stride_all ? &orientations[n] : &orientations[k];

		if (check)
		{
			real_matrix4x3 made;

			matrix4x3_multiply_orientation(parent, orientation, &made);
			census.checked++;
			if (memcmp(&made, &matrices[n], sizeof(made)))
			{
				if (census.different++ < 8)
					platform_log("h2 render nodes check: node %d made differs (%lu of %lu compared)", n, census.different,
						census.checked);
			}
			if (census.checked >= census_check_logged + 1000000)
			{
				census_check_logged = census.checked;
				platform_log("h2 render nodes check: %lu compared, %lu different", census.checked, census.different);
			}
		}
		else
			matrix4x3_multiply_orientation(parent, orientation, &matrices[n]);
	}
}

void h2_render_nodes_complete(long object_index)
{
	struct render_nodes_slot *slot = slot_get(object_index);
	struct object_datum *object;

	if (!slot || !slot->deferred)
		return;
	object = object_get(object_index);
	if (!slot->made_all)
	{
		fill(&definitions[slot->definition],
			(real_matrix4x3 *)object_header_block_get(object_index, &object->object.node_matrices),
			(real_orientation const *)object_header_block_get(object_index, &object->object.node_orientations), TRUE,
			slot->kept, FALSE);
	}
	census.completes++;
	slot->deferred = 0;
}

/* (game_state.c) before a saved game is written: every object's matrices
whole, the game state as it was */
void h2_render_nodes_complete_all(void)
{
	short s;

	for (s = 0; s < RENDER_NODES_SLOTS; s++)
	{
		if (slots[s].object_index != NONE && slots[s].deferred && slots[s].generation == halo_map_generation &&
			object_try_and_get(slots[s].object_index))
		{
			h2_render_nodes_complete(slots[s].object_index);
		}
	}
}

/* ---------- the render */

unsigned long long h2_render_nodes_capture(long object_index, short *handle, boolean *check,
	real_orientation *orientations, short orientation_room, real_matrix4x3 *parents, short *orientation_count)
{
	struct render_nodes_slot *slot = slot_get(object_index);
	struct render_nodes_definition const *d;
	struct object_datum *object;
	real_orientation const *live;
	short k;

	*orientation_count = 0;
	if (!slot || !slot->deferred)
		return 0;
	d = &definitions[slot->definition];
	if (d->order_count > orientation_room)
		return 0;
	object = object_get(object_index);
	live = (real_orientation const *)object_header_block_get(object_index, &object->object.node_orientations);
	for (k = 0; k < d->order_count; k++)
		orientations[k] = live[d->order[k]];
	memcpy(parents, slot->kept, d->kept_count * sizeof(real_matrix4x3));
	*handle = slot->definition;
	*check = slot->made_all;
	*orientation_count = d->order_count;
	return slot->deferred;
}

void h2_render_nodes_fill(short handle, real_matrix4x3 *matrices, short node_count,
	real_orientation const *orientations, real_matrix4x3 const *parents, int check)
{
	struct render_nodes_definition const *d;

	if (handle < 0 || handle >= definition_count)
		return;
	d = &definitions[handle];
	if (node_count < d->node_count)
		return;
	fill(d, matrices, orientations, FALSE, parents, check);
	census.fills++;
	census.filled_nodes += d->order_count;
}

/* the live copies: by absolute index, made once a frame */
static real_matrix4x3 *live_copies[MAXIMUM_OBJECTS_PER_MAP];
static unsigned long live_copy_frame[MAXIMUM_OBJECTS_PER_MAP];
static long live_copy_owner[MAXIMUM_OBJECTS_PER_MAP];

real_matrix4x3 *h2_render_nodes_live(long object_index, real_matrix4x3 const *live, short node_count)
{
	extern unsigned long render_interpolation_frame_number(void);
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	struct render_nodes_slot *slot;
	struct render_nodes_definition const *d;
	struct object_datum *object;
	unsigned long frame = render_interpolation_frame_number();

	if (absolute_index < 0 || absolute_index >= MAXIMUM_OBJECTS_PER_MAP || !(slot = slot_get(object_index)) ||
		!slot->deferred)
	{
		return NULL;
	}
	d = &definitions[slot->definition];
	if (node_count < d->node_count || node_count > MAXIMUM_NODES_PER_MODEL)
		return NULL;
	if (live_copies[absolute_index] && live_copy_frame[absolute_index] == frame && live_copy_owner[absolute_index] == object_index)
		return live_copies[absolute_index];
	if (!live_copies[absolute_index])
	{
		/* (the C library's malloc: the render's thread) */
		live_copies[absolute_index] = (real_matrix4x3 *)(malloc)(MAXIMUM_NODES_PER_MODEL * sizeof(real_matrix4x3));
		if (!live_copies[absolute_index])
			return NULL;
	}
	object = object_get(object_index);
	memcpy(live_copies[absolute_index], live, node_count * sizeof(real_matrix4x3));
	fill(d, live_copies[absolute_index],
		(real_orientation const *)object_header_block_get(object_index, &object->object.node_orientations), TRUE,
		slot->kept, slot->made_all);
	census.live_fills++;
	if (slot->made_all)
		return NULL;
	live_copy_frame[absolute_index] = frame;
	live_copy_owner[absolute_index] = object_index;
	return live_copies[absolute_index];
}

/* ---------- the proof */

static unsigned char *hash_saved;
static unsigned long hash_saved_size;

void h2_render_nodes_hash(int restore)
{
	struct object_iterator iterator;
	struct object_datum *object;
	unsigned long used = 0;

	if (!definition_count)
		return;
	object_iterator_new(&iterator, NONE, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		short d = definition_find(object->definition_index);
		real_matrix4x3 *matrices;
		short k, count;

		if (d == NONE)
			continue;
		matrices = (real_matrix4x3 *)object_header_block_get(iterator.index, &object->object.node_matrices);
		count = (short)(object->object.node_matrices.size / (short)sizeof(real_matrix4x3));
		for (k = 0; k < definitions[d].order_count; k++)
		{
			short n = definitions[d].order[k];

			if (n >= count)
				continue;
			if (!restore && used + sizeof(real_matrix4x3) > hash_saved_size)
			{
				unsigned long size = hash_saved_size ? hash_saved_size * 2 : 1 << 20;
				unsigned char *grown = (unsigned char *)(realloc)(hash_saved, size);

				if (!grown)
					return;
				hash_saved = grown;
				hash_saved_size = size;
			}
			if (restore)
				memcpy(&matrices[n], hash_saved + used, sizeof(real_matrix4x3));
			else
			{
				memcpy(hash_saved + used, &matrices[n], sizeof(real_matrix4x3));
				memset(&matrices[n], 0, sizeof(real_matrix4x3));
			}
			used += sizeof(real_matrix4x3);
		}
	}
}
