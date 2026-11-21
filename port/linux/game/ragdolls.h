/*
RAGDOLLS.H

Ragdolls (ragdolls.c; display.ragdolls): a dead body's pose made by a rigid
body simulation of its own skeleton on Jolt Physics, drawn in place of the
game's. Only the picture: nothing here is game state, nothing goes over the
network or into a saved game, and no game random number is drawn.

The builds with Jolt (HALO_PORT_RAGDOLLS, halo_port_features.h); every call
below is compiled out elsewhere.
*/

#ifndef __RAGDOLLS_H
#define __RAGDOLLS_H

#include "cseries.h"
#include "real_math.h"

struct damage_data;

/* ---------- the game's side (the game's thread, in the tick) */

/* units.c's unit_died, not feigned: a body that may go limp */
void ragdolls_unit_died(long unit_index);
/* damage.c's object_damage_aftermath: the blow, for the throw of a death
that follows it, or to wake a ragdoll it strikes */
void ragdolls_note_damage(long object_index, struct damage_data const *damage);
/* game_time.c, after each game tick: the ragdolls a tick on */
void ragdolls_tick(void);
/* a checkpoint revert or a saved game loaded (game_state.c): none left, the
bodies are not these */
void ragdolls_reset(void);
/* a new map (game.c): none left, nor the last level's mesh */
void ragdolls_new_map(void);

/* ---------- the render's side (render_interpolation.c, render_objects.c) */

/* the ragdoll's pose over node_count matrices of the object's; FALSE:
the object has none */
boolean ragdolls_pose(long object_index, real_matrix4x3 *matrices, short node_count);
/* the ragdoll's pose to draw the object with, or NULL */
real_matrix4x3 *ragdolls_drawn_pose(long object_index);
/* while a frame is drawn: each ragdoll's body's sphere is the ragdoll's
(level of detail, lighting, culling), put back as it was at the frame's
end, before any tick */
void ragdolls_frame_begin(void);
void ragdolls_frame_end(void);
/* (find_rendered_objects) the ragdolls whose bodies the clusters drawn did
not find, found in the cluster each lies in and seen from there: at most
maximum into result, each marked */
short ragdolls_find_rendered(long *result, short maximum, boolean (*unmarked)(long object_index),
	boolean (*mark)(long object_index));

/* ---------- the console (hs.c): "ragdolls stats", and with debug.ragdolls
the tests' "ragdolls boom|shoot|headshot|backshot|punch|ram|kill [count]
[range]", "ragdolls spawn [count] [distance]" and "ragdolls cam <distance>
<yaw> <pitch>"; TRUE: done */
boolean ragdolls_command(char const *arguments);

#endif // __RAGDOLLS_H
