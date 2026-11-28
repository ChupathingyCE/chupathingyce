/*
RAGDOLLS_PROPS.H

Physics props (ragdolls_props.c; graphics.h2_physics, HALO_H2_PHYSICS): a
Halo 2 level's loose objects - its crates (Halo 2's 'bloc' objects: drums,
tires, pallets, packing crates, the dumpster, concrete chunks, fusion
coils) - move as rigid bodies, knocked by explosions, shots, players and
vehicles, and come to rest; its hinged ones (the main crane's arm and
link, the hinge lights, the awnings) swing when struck and settle. The
ragdolls' solver (ragdolls_solver.h) moves them.

The loose ones are the game's: the machine that runs the game (the host,
or a game played alone) moves them, as game state, and tells a network
game's clients where they are (_distributed_message_props_states); a
client never moves its own. The hinged ones are only the picture, each
machine's own, as the ragdolls are.
*/

#ifndef __RAGDOLLS_PROPS_H
#define __RAGDOLLS_PROPS_H

#include "cseries.h"
#include "real_math.h"

struct damage_data;

/* (cache_files.c) a map loaded: a Halo 2 level's crates' physics read from
its Halo 2 map (nothing for any other) */
void ragdolls_props_map_loaded(char const *map_name);
/* (ragdolls_reset) none left: a new map, a revert */
void ragdolls_props_reset(void);
/* (objects_dispose_from_old_map) a map left: its level mesh no longer kept for a revert */
void ragdolls_props_map_left(void);
/* (game_time.c) after each game tick, before its poses are copied */
void ragdolls_props_tick(void);
/* (objects.c, object_new_by_name) an object a script made by its name
(Colossus's cans, Elongation's crates; a client's copy the host's names
made): a prop from the next tick if it is a loose crate */
void ragdolls_props_note_made_by_name(long object_index);
/* (breakable_surfaces.c) a breakable surface (centre, radius) broke: the
props resting near it woken - one on a pane falls through, as Havok's
bodies on Halo 2's glass do (Beaver Creek's skylight, insider.5 tester:
a rock left floating over the hole) */
void ragdolls_props_note_surface_broken(real_point3d const *centre, real radius);
/* (ragdolls_note_damage) a blow to an object */
void ragdolls_props_note_damage(long object_index, struct damage_data const *damage);
/* (object_cause_damage) the node about to take a direct blow; NONE: none */
void ragdolls_props_note_struck_node(long object_index, short node_index);
/* (area_of_effect_cause_damage_to_object) an explosion near a prop whose
section holds it: Halo 2's indirect damage, measured from its parts */
void ragdolls_props_note_area_damage(long object_index, struct damage_data const *damage);

/* the render's: the object is a prop that moves now (its poses copied each
tick, ragdolls_object_has_pose) */
boolean ragdolls_props_object_moving(long object_index);
/* ... a hinged prop's pose over the game's (its swing); FALSE: none */
boolean ragdolls_props_pose_override(long object_index, real_matrix4x3 *matrices, short node_count);
/* ... the pose with no tick running, or NULL */
real_matrix4x3 *ragdolls_props_live_node_matrices(long object_index);
/* (objects.c) a swinging prop's game nodes put where it is drawn: its collision turns with it */
void ragdolls_props_swing_nodes_apply(long object_index);

/* (network_distributed.c) the host, each tick: where its props are, to
its clients; a client: the host's word */
void ragdolls_props_network_host_tick(void);
word ragdolls_props_network_entry_size(void);
void ragdolls_props_network_handle_states(void const *entries, short count);

/* (HALO_PHYSICS_ENGINE=jolt, a HALO_JOLT build) the props' Jolt world - the
level's mesh, the props, the vehicles -, which the ragdolls share: TRUE it
exists; stepped once a tick, by the props' tick (the machine that runs the
game) or else by the ragdolls' */
boolean ragdolls_props_jolt_world(void);
void ragdolls_props_jolt_step(void);
/* (h2_jolt_hull.c, a build with Jolt) the world with the level in it, made if
need be whatever engine the props are on; a tick's step, once; a body's
rotation from its forward and up, and back */
boolean ragdolls_props_hull_world(void);
void ragdolls_props_rigid_ragdolls_tick(void);
void ragdolls_props_hull_step(void);
void ragdolls_props_jolt_quaternion(real_vector3d const *forward, real_vector3d const *up, float q[4]);
void ragdolls_props_jolt_axes(float const q[4], real_vector3d *forward, real_vector3d *up);

/* (main.c's "@props ...") tests */
void ragdolls_props_test_command(char const *arguments);

#endif // __RAGDOLLS_PROPS_H
