/* h2_render_nodes.h: Halo 2's render-only nodes' matrices made where they are drawn, not in the tick (h2_render_nodes.c) */

#ifndef __H2_RENDER_NODES_H
#define __H2_RENDER_NODES_H

/* (h2_render_only.c, at a Halo 2 level's map open) the definitions' deferred
nodes; dispose at the next */
void h2_render_nodes_map_loaded(void);
void h2_render_nodes_dispose(void);

/* (objects.c, object_compute_node_matrices) before the matrices: the nodes
whose matrices the tick leaves to the render (bit n: node n; 0: none), and
what the compute's end needs in *compute (on the caller's stack: no
thread-local storage, which the Vita's toolchain reaches through a call) */
struct h2_render_nodes_compute
{
	short definition;
	unsigned long long mask;
};
struct object_datum;
/* (perf/ce-floor-2) the definitions with render-only subtrees: 0 - none, a
Halo CE level - and h2_render_nodes_begin leaves the compute empty (objects.c
skips the call then) */
extern short h2_render_nodes_definition_count;
unsigned long long h2_render_nodes_begin(long object_index, struct object_datum const *object,
	struct h2_render_nodes_compute *compute);
/* ... and after them (the cinematics' poses included, before any
postprocess): what the render needs kept (the deferred subtrees' parents) */
void h2_render_nodes_end(long object_index, struct h2_render_nodes_compute const *compute,
	struct real_matrix4x3 const *matrices);

/* (h2_cinematics.c) about to pose the computed object's matrices (a cutscene,
a speaking face, an emotion): its deferred ones made now, as the tree would
have, and this compute not deferred */
void h2_render_nodes_make_whole(long object_index, struct h2_render_nodes_compute *compute,
	struct real_matrix4x3 *matrices);

/* (render_interpolation.c) a capture: the object's deferred nodes now (0:
none), with their orientations and their parents' matrices copied to
orientations[] (the deferred count) and parents[] (RENDER_NODES_PARENTS_MAXIMUM);
the definition's handle in *handle, *check when the tick made them all
(the check: the render compares) */
#define RENDER_NODES_PARENTS_MAXIMUM 8
unsigned long long h2_render_nodes_capture(long object_index, short *handle, unsigned char *check,
	struct real_orientation *orientations, short orientation_room, struct real_matrix4x3 *parents, short *orientation_count);
/* the render: the deferred nodes' matrices made in matrices[] (the object's
node count) from its parents' and the copied orientations (check: compared
with what matrices[] holds, nothing written) */
void h2_render_nodes_fill(short handle, struct real_matrix4x3 *matrices, short node_count,
	struct real_orientation const *orientations, struct real_matrix4x3 const *parents, int check);

/* the render, an object drawn from the live matrices (no snapshot): the
matrices to draw, the deferred made in a copy (NULL: the live ones are whole) */
struct real_matrix4x3 *h2_render_nodes_live(long object_index, struct real_matrix4x3 const *live, short node_count);

/* (the tick) an object's live matrices made whole now (a reader of all its
nodes: a body going limp, a ragdoll made) */
void h2_render_nodes_complete(long object_index);

/* the switch: 0 off, 1 on, 2 check (all computed, the render's fill compared), 3 on + poisoned (a test) */
int h2_render_nodes_mode(void);

/* (tick_hash.c) HALO_TICK_HASH_MASK_RENDER_NODES: the deferrable nodes'
matrices hashed as zeros (0), put back (1) */
void h2_render_nodes_hash(int restore);

#endif
