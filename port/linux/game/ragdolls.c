/*
RAGDOLLS.C

Ragdolls (display.ragdolls, off by default; Video Setup's RAGDOLLS): a
character killed goes limp and falls as a body falls - knocked by the blow
that killed it, its arms and legs thrown apart by a grenade's blast - and
comes to rest on the ground, the steps or the rocks it lands on, instead of
its death animation's last pose laid onto the ground.

Only the picture. A ragdoll is a pose drawn for a dead body: the body itself
- its place, its node matrices, its hit boxes and markers, what projectiles
meet - is the game's as ever, and nothing here is game state: nothing goes
over the network, into a checkpoint or a saved game, nothing draws from the
game's random numbers or its memory. Each machine of a network game
simulates its own. debug.determinism proves it: a game replayed with
ragdolls and without keeps the same state, tick for tick.

The idea is jojoxdk's "Halo 1 MCC Box3D Physics Mod" (MIT): ragdolls whose
joint types and limits come from the animation graph's limp body nodes,
knees and elbows bending as far as its hinges let them (150 and 145
degrees), a body thrown by the blow that killed it. None of its code or rig
files is used: each rig here is made at run time from the dying body's own
model, collision model and animation graph, and the physics is Jolt Physics
(port/third_party/jolt, MIT; physics_jolt.cpp).

Which bodies: those the game itself lays limp, by their biped's own flag
(uses limp body physics): the marines, crewmen, the Master Chief and the
players, the Covenant, the Flood's combat forms; not the cinematics'
characters, Cortana, the Monitor, sentinels, infection forms or the
scripts' wounded marines. A Custom Edition map's bipeds as their maker set
them.

The rig, made when a body dies:
- a rigid body a joint of the body - the pelvis, the spine and the chest,
  the head, the upper arms, forearms and hands, the thighs, calves (an
  Elite's and a Jackal's horselinks) and feet; the clavicles and the neck
  ride on the chest, fingers, guns, eyes and the like on their nearest
  ancestor's body, every node fixed in its body's frame as it stood at the
  death, so no bone ever stretches;
- its shape the convex hull of the collision model's geometry of the nodes
  it carries (the game's own hit geometry), a limb's trimmed of what lies
  far off its bone (a Jackal's shield); with none, a box about its nodes;
- its mass a human's segment share (Winter's: the head 8.1 per cent, the
  trunk 49.7, a thigh 10, a calf 4.65, a foot 1.45, an upper arm 2.8, a
  forearm 1.6, a hand 0.6) of 80 kg at 1.8 m, as tall as the body is; its
  inertia its shape's, made steady for its joints (Jolt's Stabilize);
- its joint to its parent at its node: a knee, an elbow or a horselink a
  hinge about its node's up axis, bending only the way it bends, from
  straight to the limit (the knee 150 degrees, the elbow 145); the rest a
  swing and twist with a joint of the body's limits each way (the hip 100
  degrees forward, 20 back, 45 out, 15 across; the shoulder raised to 70
  above level; the spine 25 forward, 15 back and aside; the head 45 down,
  35 back; the wrist; the ankle), three fifths of that for a joint the
  animation graph holds still, a limit the dying pose is already past
  allowed that far; friction in every joint, so a limb comes to rest;
- its parts meet the level (the structure's collision surfaces and the
  fixed scenery's, each breakable pane its own body until it breaks), the
  vehicles and machines where the game has them, other ragdolls, and each
  other but those two joints or fewer apart.

The death: unit_died (not a feigned one) names the body; the next tick its
ragdoll starts from its pose then, moving as the game threw it but no
faster than 6 m/s (Halo's blasts throw a body at 15 m/s and more, 20 m and
further; a ragdoll a body's length or two; a local player's own, which the
camera follows, as the game threw it), and the blow that killed it
(object_damage_aftermath) adds to that: an explosion spreads the limbs from
its centre, the nearest the most; a shot pushes the part nearest its line
(3.5 m/s at least) and knocks the whole body back; a melee blow throws the
whole body along it at 12 m/s or more. A blow that strikes a ragdoll wakes
it and throws it again. The render finds a ragdoll in the cluster it lies
in, not only where the game has its body.

The budget: four awake at most, twelve kept (asleep, drawn as they came to
rest). One falls asleep when Jolt puts it to sleep, when no part has moved
1.5 mm a tick for half a second, or after ten seconds awake; a death past
four awake puts to sleep one nearly at rest, else waits up to three seconds
(its death animation playing) and starts from where that has it; two start
in a tick at most; past twelve kept, the one asleep and not drawn longest is
let go (its body drawn as the game lays it again). A new map, a checkpoint
revert or a saved game lets every one go.

debug.ragdolls (config.toml): 1 a log line each time a ragdoll starts,
sleeps, wakes or is let go, and the cost every ten seconds while any is
kept; 2 each rig too. With it the console's "ragdolls" commands
(ragdolls_command) test them: "ragdolls stats", "ragdolls boom|shoot|punch|
kill [count] [range] [name]" (a grenade's blast beside, a rifle bullet, a
melee blow or a plain kill of the nearest living characters, those whose
tag's name holds name, or other players' with "players"), "ragdolls cam
<distance> <yaw> <pitch>" (the debug camera looking at the last one),
"ragdolls shot" (a screenshot).
*/

#include "cseries.h"

#ifdef HALO_PORT_RAGDOLLS

#include "real_math.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "game/game.h"
#include "game/game_globals.h"
#include "objects/damage.h"
#include "objects/damage_effect_definitions.h"
#include "models/model_definitions.h"
#include "models/model_animation_definitions.h"
#include "units/units.h"
#include "units/bipeds.h"
#include "units/biped_definitions.h"
#include "physics/collisions.h"
#include "physics/collision_model_definitions.h"
#include "physics/collision_bsp_definitions.h"
#include "physics/breakable_surfaces.h"
#include "physics/physics.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "structures/structures.h"
#include "structures/structure_bsp_definitions.h"
#include "game/players.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "cache/cache_files.h"
#include "render/render.h"
#include "render/render_cameras_internal.h"

#include "ragdolls.h"
#include "physics_jolt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* port/linux/src/port_config.c */
int config_boolean(const char *name);
long config_integer(char const *name);
unsigned long config_changes(void);
void platform_log(const char *format, ...);
/* models.c */
void model_get_node_matrices(struct model const *model, real_matrix4x3 *node_matrices, real_point3d const *origin,
	real_vector3d const *forward, real_vector3d const *up);
/* damage.c */
void damage_kill_object_for_player(long object_index, long player_index);
/* director.c's: the debug camera put somewhere (the tests' "ragdolls cam") */
void director_set_flying_camera(short local_player_index, real_point3d const *position, real_vector3d const *forward);
/* (the double versions throughout: sin, cos, asin and atan2 are
halo_math.h's, the same on every port) */

/* ---------- constants */

enum
{
	/* bodies whose pose is a ragdoll's, awake or asleep */
	RAGDOLL_SLOTS = 12,
	/* awake at once */
	RAGDOLL_ACTIVE = 4,
	RAGDOLL_MAXIMUM_NODES = MAXIMUM_NODES_PER_MODEL,
	/* a rig's joints of the body, and its bodies */
	RAGDOLL_MAXIMUM_PARTICLES = 24,
	RAGDOLL_MAXIMUM_BODIES = 24,
	RAGDOLL_PENDING = 16,
	RAGDOLL_DAMAGE_NOTES = 16,
	/* (a motion of a tick is kept as two steps' displacement: the blows'
	pushes are worked out on it, and it is the bodies' velocity at the
	start) */
	RAGDOLL_SUBSTEPS = 2,
	/* the fewest joints of the body a rig may have (a sentinel's, an
	infection form's skeleton is no body) */
	RAGDOLL_MINIMUM_PARTICLES = 8,

	/* a body dead in a vehicle's seat waits this many ticks to fall out, or
	for room among the awake */
	RAGDOLL_PENDING_TICKS = 90,
	/* a blow is the death's if it came this many ticks before it */
	RAGDOLL_NOTE_TICKS = 3,
	/* still this long, asleep (after at least the second) */
	RAGDOLL_SLEEP_TICKS = 15,
	RAGDOLL_SLEEP_AGE = 20,
	/* awake this long at most */
	RAGDOLL_AWAKE_TICKS = 300,
	/* the game's body still thrown about this long after the death: its
	moves are not the end of a ragdoll asleep */
	RAGDOLL_FLIGHT_TICKS = 150,
	/* ragdolls started a tick at most */
	RAGDOLL_STARTS_PER_TICK = 2,
	/* awake this long at least before put to sleep for another, and still
	this long */
	RAGDOLL_ROOM_TICKS = 30,
	RAGDOLL_ROOM_STILL_TICKS = 3,
	/* kept, not drawn for this many frames: let go first */
	RAGDOLL_UNSEEN_FRAMES = 30,
	/* the level's breakable panes, vehicles and machines' parts in the world */
	RAGDOLL_WORLD_PANES = 256,
	RAGDOLL_WORLD_VEHICLES = 32,
	RAGDOLL_WORLD_MACHINE_PARTS = 64,
	RAGDOLL_WORLD_HULL_POINTS = 256,
};

enum
{
	_ragdoll_free = 0,
	_ragdoll_awake,
	_ragdoll_asleep,
};

/* why a ragdoll was let go (the log's words) */
enum
{
	_ragdoll_released_gone,
	_ragdoll_released_alive,
	_ragdoll_released_attached,
	_ragdoll_released_changed,
	_ragdoll_released_setting,
	_ragdoll_released_map,
	_ragdoll_released_not_a_number,
	_ragdoll_released_far,
	_ragdoll_released_replaced,
	_ragdoll_released_body_moved,
	NUMBER_OF_RAGDOLL_RELEASE_REASONS
};

static char const *const ragdoll_release_names[NUMBER_OF_RAGDOLL_RELEASE_REASONS] =
{
	"gone", "alive again", "attached", "its model changed", "the setting off", "the game state replaced",
	"not a number", "too far from its body", "replaced", "its body moved",
};

/* animation graph nodes' joint flags (the limp body's) */
enum
{
	_ragdoll_joint_ball_and_socket_bit = 0,
	_ragdoll_joint_hinge_bit,
	_ragdoll_joint_no_movement_bit,
};

/* the shortest bone a joint of the body stands for (world units: 2.5 cm) */
#define RAGDOLL_MINIMUM_BONE 0.025f
/* the limp body node radius when the graph gives none, and its bounds */
#define RAGDOLL_DEFAULT_RADIUS 0.03f
#define RAGDOLL_MAXIMUM_RADIUS 0.07f
/* the most a body's own motion starts its ragdoll with (6 m/s), world
units a tick */
#define RAGDOLL_LAUNCH_SPEED (6.0f / 3.048f / TICKS_PER_SECOND)
/* moved less than this in a tick (1.5 mm): still */
#define RAGDOLL_STILL 0.0005f
/* a shot's least push (3.5 m/s), world units a tick */
#define RAGDOLL_SHOT_SPEED (3.5f / 3.048f / TICKS_PER_SECOND)
/* the shot that kills knocks the whole body back along it, a little
upward: at least 3.5 m/s, at most 6 */
#define RAGDOLL_SHOT_KNOCKBACK (3.5f / 3.048f / TICKS_PER_SECOND)
#define RAGDOLL_SHOT_KNOCKBACK_SCALE 0.5f
/* a melee blow's least push (12 m/s), world units a tick: the whole body
thrown along it (the damage category melee) */
#define RAGDOLL_MELEE_SPEED (12.0f / 3.048f / TICKS_PER_SECOND)
#define RAGDOLL_DAMAGE_CATEGORY_MELEE 6
/* the most an impulse adds to a part (8 m/s); a melee blow's whole body
throw, 14 */
#define RAGDOLL_MAXIMUM_IMPULSE (8.0f / 3.048f / TICKS_PER_SECOND)
#define RAGDOLL_MELEE_MAXIMUM (14.0f / 3.048f / TICKS_PER_SECOND)
/* a ragdoll this far from its body's root node is let go (world units:
150 m; only a body put elsewhere) */
#define RAGDOLL_TETHER 50.0f
/* asleep, its body moved this far by the game: let go (world units: 6 m) */
#define RAGDOLL_BODY_MOVED 2.0f
/* the drawn sphere: around the parts, this much more */
#define RAGDOLL_SPHERE_MARGIN 0.12f
#define RAGDOLL_SOLID_FLAGS FLAG(_collision_test_structure_bit)
#define RAGDOLL_DEGREES(d) ((d) * 0.017453292f)

/* Jolt works in metres (a world unit is 10 feet) */
#define RAGDOLL_JOLT_METRES 3.048f
/* world units a substep -> metres a second */
#define RAGDOLL_JOLT_SPEED (RAGDOLL_JOLT_METRES * TICKS_PER_SECOND * RAGDOLL_SUBSTEPS)
/* a rig's hulls' points: one body's, and all of them */
#define RAGDOLL_RIG_POINTS 384
#define RAGDOLL_RIG_POOL 640
#define RAGDOLL_RIG_LEAST_MASS 0.4f

/* ---------- structures */

/* an animation graph's node, its joint's type and range (the limp body's) */
struct ragdoll_graph_node
{
	char name[TAG_STRING_LENGTH + 1];
	short next_sibling_node_index;
	short first_child_node_index;
	short parent_node_index;
	word pad;
	unsigned long flags;
	real_vector3d base_vector;
	real range;
	long pad1;
};

/* a rotation: the images of the three axes */
struct ragdoll_rotation
{
	real_vector3d axes[3];
};

/* a joint of the body while the rig is made, then each body of the rig:
where it is (the tick's, the last tick's: their difference its motion) */
struct ragdoll_particle
{
	real_point3d position;
	real_point3d previous;
	/* (a body's: where it stood at the death) */
	real_point3d start;
	real radius;
	short node_index;
	/* its parent (NONE: the root) */
	short parent;
	/* the turn since the death */
	struct ragdoll_rotation delta;
};

/* a body of a rig, in its node's frame at the death, world units */
struct ragdoll_rig_body
{
	char node_name[32];
	/* a body of the rig, parents first; NONE: the root */
	short parent;
	/* PHYSICS_JOLT_RIG_JOINT_... to the parent */
	short joint;
	/* its shape: the convex hull of these of its rig's points when 4 or
	more, else a box */
	short hull_first, hull_count;
	real half[3], box_centre[3];
	real mass;
	real friction, restitution, linear_damping, angular_damping;
	/* the joint's axes and limits (physics_jolt_rig_body's) */
	real parent_axes[6], axes[6];
	real limits[6];
	/* kg wu^2 / s^2 */
	real joint_friction;
};

struct ragdoll_rig
{
	char source[32];
	short body_count;
	struct ragdoll_rig_body bodies[RAGDOLL_MAXIMUM_BODIES];
	/* its bodies' hulls' points, x y z in each body's frame */
	real points[RAGDOLL_RIG_POOL][3];
};

struct ragdoll
{
	long object_index;
	/* (ragdolls_reset's count when it started: one of an older game state
	is no ragdoll) */
	unsigned long generation;
	short state;
	short node_count, particle_count;
	short still_ticks;
	long age;
	long awake_ticks;
	/* the order it started in, and was last woken in */
	unsigned long started;
	unsigned long woken;
	/* (the joints of the body while the rig is made; then the rig's
	bodies, in its order) */
	struct ragdoll_particle particles[RAGDOLL_MAXIMUM_PARTICLES];
	/* each joint's limit (ragdoll_joint_cone), radians */
	real cones[RAGDOLL_MAXIMUM_PARTICLES];
	/* nodes, parents before children */
	short node_order[RAGDOLL_MAXIMUM_NODES];
	short node_parent[RAGDOLL_MAXIMUM_NODES];
	signed char node_particle[RAGDOLL_MAXIMUM_NODES];
	/* a body's node: its matrix at the death; any other: its matrix in its
	parent's */
	real_matrix4x3 local[RAGDOLL_MAXIMUM_NODES];
	/* the pose drawn */
	real_matrix4x3 pose[RAGDOLL_MAXIMUM_NODES];
	real_point3d center;
	real radius;
	/* where its body was the last tick it was awake (or, the first five
	seconds, the last tick: the game's body thrown further than its own) */
	real_point3d body;
	/* the tick it started on (ragdoll_ticks) */
	unsigned long started_tick;
	/* the cluster its middle is in (NONE: none): the render finds it there */
	short drawn_cluster;
	/* the frame it was last drawn in (ragdoll_frames) */
	unsigned long drawn_frame;
	/* its Jolt ragdoll, of the world of this generation (NONE: none) */
	int jolt;
	unsigned int jolt_generation;
	/* each body's turn at the death (x y z w) */
	float rig_turn[RAGDOLL_MAXIMUM_BODIES][4];
	/* how many of its joints' limits the dying pose was past */
	short widened;
};

struct ragdoll_pending
{
	long object_index;
	unsigned long tick;
};

struct ragdoll_damage_note
{
	long object_index;
	unsigned long tick;
	boolean explosion;
	/* a melee blow (a punch, a rifle butt, a sword): the whole body thrown */
	boolean melee;
	real_point3d origin;
	real_vector3d direction;
	/* world units a tick */
	real speed;
};

/* (the level's mesh being made: a growing list) */
struct ragdoll_mesh
{
	float *vertices;
	int *triangles;
	long vertex_count, vertex_most, triangle_count, triangle_most;
	/* (only counted: the arrays are sized by a count first) */
	boolean counting;
};

/* ---------- globals */

static struct ragdoll ragdolls[RAGDOLL_SLOTS];
static struct ragdoll_rig ragdoll_rigs[RAGDOLL_SLOTS];
/* by an object's absolute index, its ragdoll's slot plus one (0: none) */
static unsigned char ragdoll_slot_by_object[MAXIMUM_OBJECTS_PER_MAP];
static struct ragdoll_pending ragdoll_pending[RAGDOLL_PENDING];
static short ragdoll_pending_count;
static struct ragdoll_damage_note ragdoll_notes[RAGDOLL_DAMAGE_NOTES];
static short ragdoll_next_note;
/* the ragdolls' own count of game ticks, of frames drawn, of ragdolls
started, and of the game states the bodies were of (ragdolls_reset) */
static unsigned long ragdoll_ticks;
static unsigned long ragdoll_frames;
static unsigned long ragdoll_sequence;
static unsigned long ragdoll_generation = 1;
/* (the rig building's) the model's default pose */
static real_matrix4x3 ragdoll_bind[RAGDOLL_MAXIMUM_NODES];

/* the Jolt world: the level of this map and structure, its panes, vehicles
and machines' parts */
static struct
{
	boolean built;
	unsigned long generation;
	short structure;
	int level_body;
	long level_triangles, level_scenery;
	int panes[RAGDOLL_WORLD_PANES];
	short pane_count;
	struct
	{
		long object_index;
		int body;
		boolean seen;
	} vehicles[RAGDOLL_WORLD_VEHICLES];
	struct
	{
		long object_index;
		short node;
		int body;
	} machine_parts[RAGDOLL_WORLD_MACHINE_PARTS];
	short machine_part_count;
} ragdoll_world;

/* (the world that could not be made: its game state and structure) */
static struct
{
	unsigned long generation;
	short structure;
} ragdoll_world_failed;

/* (a frame drawn: the bodies whose spheres are the ragdolls', and theirs) */
static struct
{
	short count;
	long objects[RAGDOLL_SLOTS];
	real_point3d centers[RAGDOLL_SLOTS];
	real radii[RAGDOLL_SLOTS];
} ragdoll_swapped;

static struct
{
	unsigned long started, rejected, slept, woken, released[NUMBER_OF_RAGDOLL_RELEASE_REASONS];
	unsigned long steps;
	short most_awake, most_kept;
	/* the ticks with one awake, the time on them (the step and the rest),
	the most a tick; the starts' time */
	unsigned long busy_ticks;
	double busy_ms, most_ms, start_ms;
	/* (the log's window) */
	unsigned long window_ticks;
	double window_ms, window_most_ms;
} ragdoll_stats;

/* ---------- the settings */

static boolean ragdolls_enabled(
	void)
{
	static unsigned long read_at = (unsigned long)-1;
	static boolean enabled;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("display.ragdolls") != 0;
	}
	return enabled;
}

/* (debug.ragdolls: the log, the tests) */
static int ragdolls_debug(
	void)
{
	static unsigned long read_at = (unsigned long)-1;
	static int level;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		level = (int)config_integer("debug.ragdolls");
	}
	return level;
}

static char const *ragdoll_object_name(
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);

	return object ? tag_name_strip_path(tag_get_name(object->definition_index)) : "none";
}

/* the host's clock, microseconds (physics_jolt.cpp; it wraps, so only
differences are taken), and milliseconds since then */
static unsigned int ragdoll_now(
	void)
{
	return physics_jolt_clock_us();
}

static double ragdoll_ms_since(
	unsigned int then)
{
	return (double)(unsigned int)(physics_jolt_clock_us() - then) / 1000.0;
}

/* a dedicated server's game (HALO_DEDICATED): nothing is drawn */
static boolean ragdolls_dedicated(
	void)
{
	static int dedicated = -1;

	if (dedicated < 0)
	{
		char const *playlist = getenv("HALO_DEDICATED");

		dedicated = playlist && playlist[0] ? 1 : 0;
	}
	return dedicated != 0;
}

/* ---------- vectors */

static real ragdoll_dot(real_vector3d const *a, real_vector3d const *b)
{
	return a->i * b->i + a->j * b->j + a->k * b->k;
}

static void ragdoll_cross(real_vector3d const *a, real_vector3d const *b, real_vector3d *result)
{
	real_vector3d c;

	c.i = a->j * b->k - a->k * b->j;
	c.j = a->k * b->i - a->i * b->k;
	c.k = a->i * b->j - a->j * b->i;
	*result = c;
}

static void ragdoll_between(real_point3d const *from, real_point3d const *to, real_vector3d *result)
{
	result->i = to->x - from->x;
	result->j = to->y - from->y;
	result->k = to->z - from->z;
}

static real ragdoll_length(real_vector3d const *v)
{
	return (real)sqrt(ragdoll_dot(v, v));
}

static real ragdoll_distance(real_point3d const *a, real_point3d const *b)
{
	real_vector3d v;

	ragdoll_between(a, b, &v);
	return ragdoll_length(&v);
}

static real ragdoll_normalize(real_vector3d *v)
{
	real length = ragdoll_length(v);

	if (length > 1e-12f)
	{
		v->i /= length;
		v->j /= length;
		v->k /= length;
	}
	return length;
}

static void ragdoll_move(real_point3d *point, real_vector3d const *v, real scale)
{
	point->x += v->i * scale;
	point->y += v->j * scale;
	point->z += v->k * scale;
}

/* (neither infinite nor not a number: x - x is 0 only then) */
static boolean ragdoll_finite(real_point3d const *p)
{
	volatile real x = p->x, y = p->y, z = p->z;

	return x - x == 0.f && y - y == 0.f && z - z == 0.f;
}

/* ---------- rotations */

static void ragdoll_rotation_identity(
	struct ragdoll_rotation *r)
{
	memset(r, 0, sizeof(*r));
	r->axes[0].i = r->axes[1].j = r->axes[2].k = 1.f;
}

static void ragdoll_rotate(struct ragdoll_rotation const *r, real_vector3d const *v, real_vector3d *result)
{
	real_vector3d c;

	c.i = r->axes[0].i * v->i + r->axes[1].i * v->j + r->axes[2].i * v->k;
	c.j = r->axes[0].j * v->i + r->axes[1].j * v->j + r->axes[2].j * v->k;
	c.k = r->axes[0].k * v->i + r->axes[1].k * v->j + r->axes[2].k * v->k;
	*result = c;
}

/* (w, x, y, z) */
static void ragdoll_rotation_from_quaternion(
	real const q[4],
	struct ragdoll_rotation *r)
{
	real w = q[0], x = q[1], y = q[2], z = q[3];

	r->axes[0].i = 1.f - 2.f * (y * y + z * z);
	r->axes[0].j = 2.f * (x * y + w * z);
	r->axes[0].k = 2.f * (x * z - w * y);
	r->axes[1].i = 2.f * (x * y - w * z);
	r->axes[1].j = 1.f - 2.f * (x * x + z * z);
	r->axes[1].k = 2.f * (y * z + w * x);
	r->axes[2].i = 2.f * (x * z + w * y);
	r->axes[2].j = 2.f * (y * z - w * x);
	r->axes[2].k = 1.f - 2.f * (x * x + y * y);
}

/* a frame's (forward, left = up x forward, up) quaternion, x y z w */
static void ragdoll_jolt_quaternion(real_vector3d const *forward, real_vector3d const *up, float q[4])
{
	real_vector3d axes[3];
	real m00, m11, m22, trace;

	axes[0] = *forward;
	ragdoll_cross(up, forward, &axes[1]);
	axes[2] = *up;
	m00 = axes[0].i;
	m11 = axes[1].j;
	m22 = axes[2].k;
	trace = m00 + m11 + m22;
	/* (m[r][c]: column c the image of axis c) */
#define M(r, c) (axes[c].n[r])
	if (trace > 0.f)
	{
		real s = (real)sqrt(trace + 1.f) * 2.f;

		q[3] = 0.25f * s;
		q[0] = (M(2, 1) - M(1, 2)) / s;
		q[1] = (M(0, 2) - M(2, 0)) / s;
		q[2] = (M(1, 0) - M(0, 1)) / s;
	}
	else if (m00 > m11 && m00 > m22)
	{
		real s = (real)sqrt(1.f + m00 - m11 - m22) * 2.f;

		q[3] = (M(2, 1) - M(1, 2)) / s;
		q[0] = 0.25f * s;
		q[1] = (M(0, 1) + M(1, 0)) / s;
		q[2] = (M(0, 2) + M(2, 0)) / s;
	}
	else if (m11 > m22)
	{
		real s = (real)sqrt(1.f + m11 - m00 - m22) * 2.f;

		q[3] = (M(0, 2) - M(2, 0)) / s;
		q[0] = (M(0, 1) + M(1, 0)) / s;
		q[1] = 0.25f * s;
		q[2] = (M(1, 2) + M(2, 1)) / s;
	}
	else
	{
		real s = (real)sqrt(1.f + m22 - m00 - m11) * 2.f;

		q[3] = (M(1, 0) - M(0, 1)) / s;
		q[0] = (M(0, 2) + M(2, 0)) / s;
		q[1] = (M(1, 2) + M(2, 1)) / s;
		q[2] = 0.25f * s;
	}
#undef M
}

static void ragdoll_jolt_metres(real_point3d const *p, float out[3])
{
	out[0] = p->x * RAGDOLL_JOLT_METRES;
	out[1] = p->y * RAGDOLL_JOLT_METRES;
	out[2] = p->z * RAGDOLL_JOLT_METRES;
}

/* ---------- the joints of the body */

/* whether a node's name has part in it, whatever its case (the game's
characters name theirs "bip01 l thigh", "Bip01 Pelvis") */
static boolean ragdoll_name_has(
	char const *name,
	char const *part)
{
	char lower[TAG_STRING_LENGTH + 1];
	short index;

	for (index = 0; index < TAG_STRING_LENGTH && name[index]; index++)
		lower[index] = (char)(name[index] >= 'A' && name[index] <= 'Z' ? name[index] - 'A' + 'a' : name[index]);
	lower[index] = 0;
	return strstr(lower, part) != NULL;
}

/* a node no joint of the body stands for: carried by its parent */
static boolean ragdoll_node_excluded(
	char const *name)
{
	static char const *const parts[] =
	{
		"finger", "thumb", "gun", "frame", "eye", "jaw", "mouth", "lid", "brow", "tongue", "toe", "nub",
		"dummy", "ponytail", "antenna", "mandible",
	};
	short index;

	for (index = 0; index < (short)NUMBEROF(parts); index++)
	{
		if (ragdoll_name_has(name, parts[index]))
			return TRUE;
	}
	return FALSE;
}

/* two names the same, whatever their case, to the first size characters */
static boolean ragdoll_same_name(
	char const *a,
	char const *b,
	short size)
{
	short index;

	for (index = 0; index < size; index++)
	{
		char x = (char)(a[index] >= 'A' && a[index] <= 'Z' ? a[index] - 'A' + 'a' : a[index]);
		char y = (char)(b[index] >= 'A' && b[index] <= 'Z' ? b[index] - 'A' + 'a' : b[index]);

		if (x != y)
			return FALSE;
		if (!x)
			break;
	}
	return TRUE;
}

/* a joint of the body by its name, wherever it is */
static boolean ragdoll_node_major(
	char const *name)
{
	static char const *const parts[] =
	{
		"pelvis", "spine", "neck", "head", "clavicle", "upperarm", "forearm", "hand", "thigh", "calf", "horselink",
		"foot",
	};
	short index;

	for (index = 0; index < (short)NUMBEROF(parts); index++)
	{
		if (ragdoll_name_has(name, parts[index]))
			return TRUE;
	}
	return FALSE;
}

/* how far the bone below a joint may turn (a hinge's bend; a joint the
graph holds still, a little; the rest the graph's range or the body's) */
static real ragdoll_joint_cone(
	char const *name,
	struct ragdoll_graph_node const *graph_node)
{
	real cone;

	/* (the hinges: the knee, an Elite's lower leg and the elbow, jojoxdk's) */
	if (ragdoll_name_has(name, "calf") || ragdoll_name_has(name, "horselink"))
		return RAGDOLL_DEGREES(150.f);
	if (ragdoll_name_has(name, "forearm"))
		return RAGDOLL_DEGREES(145.f);
	if (graph_node && TEST_FLAG(graph_node->flags, _ragdoll_joint_no_movement_bit))
		return RAGDOLL_DEGREES(6.f);
	if (ragdoll_name_has(name, "thigh"))
		cone = RAGDOLL_DEGREES(45.f);
	else if (ragdoll_name_has(name, "upperarm"))
		cone = RAGDOLL_DEGREES(90.f);
	else if (ragdoll_name_has(name, "clavicle"))
		cone = RAGDOLL_DEGREES(15.f);
	else if (ragdoll_name_has(name, "spine") || ragdoll_name_has(name, "pelvis"))
		cone = RAGDOLL_DEGREES(12.f);
	else if (ragdoll_name_has(name, "neck") || ragdoll_name_has(name, "head"))
		cone = RAGDOLL_DEGREES(35.f);
	else if (ragdoll_name_has(name, "hand"))
		cone = RAGDOLL_DEGREES(50.f);
	else if (ragdoll_name_has(name, "foot"))
		cone = RAGDOLL_DEGREES(35.f);
	else if (graph_node && graph_node->range > 0.f && graph_node->range < 6.3f)
		cone = 0.5f * graph_node->range;
	else
		cone = RAGDOLL_DEGREES(30.f);
	return PIN(cone, RAGDOLL_DEGREES(6.f), RAGDOLL_DEGREES(90.f));
}

static struct ragdoll_graph_node const *ragdoll_graph_node_get(
	struct animation_graph const *graph,
	short node_index)
{
	if (!graph || !VALID_INDEX(node_index, graph->nodes.count))
		return NULL;
	return TAG_BLOCK_GET_ELEMENT(&graph->nodes, node_index, struct ragdoll_graph_node);
}

/* a dead body's joints of the body, from its model's nodes and its pose now
(FALSE: no body a ragdoll can be made of): the nodes in order, a joint at
each node that is not a finger, a gun, an eye or the like and lies at least
2.5 cm from the joint above it (or is a joint by its name) */
static boolean ragdoll_build(
	struct ragdoll *ragdoll,
	long object_index,
	struct object_datum *object)
{
	struct object_definition *definition = object_definition_get(object->definition_index);
	struct model const *model;
	struct animation_graph const *graph = NULL;
	real_matrix4x3 const *live;
	short node_count;
	short depth[RAGDOLL_MAXIMUM_NODES];
	short particle_of_ancestor[RAGDOLL_MAXIMUM_NODES];
	real limp_radius = RAGDOLL_DEFAULT_RADIUS;
	short index, count, level;

	if (!definition || definition->object.model.index == NONE)
		return FALSE;
	if (object->object.type != _object_type_biped ||
		!TEST_FLAG(biped_definition_get(object->definition_index)->biped.flags, _biped_uses_limp_body_physics_bit))
	{
		return FALSE;
	}
	model = model_definition_get(definition->object.model.index);
	node_count = (short)(object->object.node_matrices.size / (short)sizeof(real_matrix4x3));
	if (!model || node_count < RAGDOLL_MINIMUM_PARTICLES || node_count > RAGDOLL_MAXIMUM_NODES ||
		model->nodes.count < node_count)
	{
		return FALSE;
	}
	if (definition->object.animation_graph.index != NONE)
	{
		graph = animation_graph_definition_get(definition->object.animation_graph.index);
		if (graph && graph->nodes.count < node_count)
			graph = NULL;
		if (graph && graph->limp_body_node_collision_radius > 0.f &&
			graph->limp_body_node_collision_radius <= RAGDOLL_MAXIMUM_RADIUS)
		{
			limp_radius = graph->limp_body_node_collision_radius;
		}
	}
	live = (real_matrix4x3 const *)object_header_block_get(object_index, &object->object.node_matrices);
	model_get_node_matrices(model, ragdoll_bind, NULL, NULL, NULL);

	memset(ragdoll, 0, sizeof(*ragdoll));
	ragdoll->object_index = object_index;
	ragdoll->node_count = node_count;
	ragdoll->jolt = NONE;

	/* (the nodes by depth: parents before children) */
	for (index = 0; index < node_count; index++)
	{
		struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node);
		short parent = node->parent_node_index;
		short guard = 0;
		short at = index;

		ragdoll->node_parent[index] = VALID_INDEX(parent, node_count) && parent != index ? parent : NONE;
		if (index == 0)
			ragdoll->node_parent[index] = NONE;
		depth[index] = 0;
		while (at != 0 && guard++ < RAGDOLL_MAXIMUM_NODES)
		{
			short up = TAG_BLOCK_GET_ELEMENT(&model->nodes, at, struct model_node)->parent_node_index;

			if (!VALID_INDEX(up, node_count) || up == at)
				break;
			at = up;
			depth[index]++;
		}
		if (index != 0 && ragdoll->node_parent[index] == NONE)
			return FALSE;
	}
	count = 0;
	for (level = 0; level < RAGDOLL_MAXIMUM_NODES && count < node_count; level++)
	{
		for (index = 0; index < node_count; index++)
		{
			if (depth[index] == level)
				ragdoll->node_order[count++] = index;
		}
	}
	if (count != node_count)
		return FALSE;
	/* (a map's nodes whose parents run in a ring: no body) */
	for (count = 0; count < node_count; count++)
	{
		short parent = ragdoll->node_parent[ragdoll->node_order[count]];

		if (parent != NONE && depth[parent] != depth[ragdoll->node_order[count]] - 1)
			return FALSE;
	}

	for (count = 0; count < node_count; count++)
	{
		short node_index = ragdoll->node_order[count];
		struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);
		short parent = ragdoll->node_parent[node_index];
		short ancestor = parent == NONE ? NONE : (ragdoll->node_particle[parent] >= 0 ?
			ragdoll->node_particle[parent] : particle_of_ancestor[parent]);
		boolean wanted;

		particle_of_ancestor[node_index] = ancestor;
		ragdoll->node_particle[node_index] = -1;
		if (!ragdoll_finite(&live[node_index].position))
			return FALSE;
		if (parent == NONE)
			wanted = TRUE;
		else
		{
			/* (a joint of the body by its name - a grunt's hip is where its
			pelvis is -, any other node far enough from the one above) */
			wanted = ancestor != NONE && !ragdoll_node_excluded(node->name) &&
				(ragdoll_node_major(node->name) || ragdoll_distance(&live[node_index].position,
					&ragdoll->particles[ancestor].position) >= RAGDOLL_MINIMUM_BONE);
		}
		if (wanted && ragdoll->particle_count < RAGDOLL_MAXIMUM_PARTICLES)
		{
			short p = ragdoll->particle_count++;
			struct ragdoll_particle *particle = &ragdoll->particles[p];

			particle->node_index = node_index;
			particle->parent = ancestor;
			particle->position = live[node_index].position;
			particle->radius = ragdoll_name_has(node->name, "head") ? limp_radius * 1.5f : limp_radius;
			ragdoll->node_particle[node_index] = (signed char)p;
			ragdoll->cones[p] = ragdoll_joint_cone(node->name, ragdoll_graph_node_get(graph, node_index));
		}
	}
	return ragdoll->particle_count >= RAGDOLL_MINIMUM_PARTICLES;
}

/* ---------- the rig: a body a joint of the body */

enum
{
	_ragdoll_class_other = 0,
	_ragdoll_class_spine,
	_ragdoll_class_head,
	_ragdoll_class_shoulder,
	_ragdoll_class_hip,
	_ragdoll_class_wrist,
	_ragdoll_class_ankle,
	_ragdoll_class_elbow,
	_ragdoll_class_knee,
	NUMBER_OF_RAGDOLL_CLASSES
};

/* each class's limits, degrees: toward its front (forward; a foot's toes up),
back, out (a leg away from the other, an arm up, the spine and head aside),
in, its twist either way; its share of the body's mass (0: the trunk's,
ragdoll_trunk_share) */
static struct
{
	real front, back, out, in, twist;
	real mass;
} const ragdoll_limits[NUMBER_OF_RAGDOLL_CLASSES] =
{
	{ 30.f, 30.f, 30.f, 30.f, 20.f, 0.f },		/* other: its graph's cone */
	{ 25.f, 15.f, 15.f, 15.f, 15.f, 0.f },		/* spine (the pelvis 14.2, the abdomen 13.9, the chest 21.6) */
	{ 45.f, 35.f, 30.f, 30.f, 50.f, 0.081f },	/* head (with the neck) */
	{ 100.f, 40.f, 70.f, 85.f, 60.f, 0.028f },	/* shoulder (out, in: above and below level, set at the death) */
	{ 100.f, 20.f, 45.f, 15.f, 30.f, 0.100f },	/* hip */
	{ 60.f, 60.f, 25.f, 25.f, 20.f, 0.006f },	/* wrist */
	{ 25.f, 45.f, 30.f, 30.f, 30.f, 0.0145f },	/* ankle */
	{ 0.f, 0.f, 0.f, 0.f, 0.f, 0.016f },		/* elbow: a hinge */
	{ 0.f, 0.f, 0.f, 0.f, 0.f, 0.0465f },		/* knee: a hinge */
};

static short ragdoll_class(
	char const *name)
{
	if (ragdoll_name_has(name, "forearm"))
		return _ragdoll_class_elbow;
	if (ragdoll_name_has(name, "calf") || ragdoll_name_has(name, "horselink"))
		return _ragdoll_class_knee;
	if (ragdoll_name_has(name, "upperarm"))
		return _ragdoll_class_shoulder;
	if (ragdoll_name_has(name, "thigh"))
		return _ragdoll_class_hip;
	if (ragdoll_name_has(name, "hand"))
		return _ragdoll_class_wrist;
	if (ragdoll_name_has(name, "foot"))
		return _ragdoll_class_ankle;
	if (ragdoll_name_has(name, "head"))
		return _ragdoll_class_head;
	if (ragdoll_name_has(name, "spine") || ragdoll_name_has(name, "pelvis"))
		return _ragdoll_class_spine;
	return _ragdoll_class_other;
}

/* the trunk's share of a body (Winter): the pelvis, the abdomen, the chest */
static real ragdoll_trunk_share(
	char const *name,
	boolean chest)
{
	if (ragdoll_name_has(name, "pelvis"))
		return 0.142f;
	return chest ? 0.216f : 0.139f;
}

/* v in a node's frame (its rotation only), and back */
static void ragdoll_to_local(real_matrix4x3 const *frame, real_vector3d const *v, real_vector3d *result)
{
	real_vector3d out;

	out.i = ragdoll_dot(v, &frame->forward);
	out.j = ragdoll_dot(v, &frame->left);
	out.k = ragdoll_dot(v, &frame->up);
	*result = out;
}

static void ragdoll_to_world(real_matrix4x3 const *frame, real_vector3d const *v, real_vector3d *result)
{
	real_vector3d out;

	out.i = frame->forward.i * v->i + frame->left.i * v->j + frame->up.i * v->k;
	out.j = frame->forward.j * v->i + frame->left.j * v->j + frame->up.j * v->k;
	out.k = frame->forward.k * v->i + frame->left.k * v->j + frame->up.k * v->k;
	*result = out;
}

/* a rotation's quaternion (w x y z) from its columns */
static void ragdoll_columns_quaternion(real_vector3d const *x, real_vector3d const *y, real_vector3d const *z, real q[4])
{
	real trace = x->i + y->j + z->k, s;

	if (trace > 0.f)
	{
		s = 0.5f / (real)sqrt(trace + 1.f);
		q[0] = 0.25f / s;
		q[1] = (y->k - z->j) * s;
		q[2] = (z->i - x->k) * s;
		q[3] = (x->j - y->i) * s;
	}
	else if (x->i > y->j && x->i > z->k)
	{
		s = 2.f * (real)sqrt(1.f + x->i - y->j - z->k);
		q[0] = (y->k - z->j) / s;
		q[1] = 0.25f * s;
		q[2] = (y->i + x->j) / s;
		q[3] = (z->i + x->k) / s;
	}
	else if (y->j > z->k)
	{
		s = 2.f * (real)sqrt(1.f + y->j - x->i - z->k);
		q[0] = (z->i - x->k) / s;
		q[1] = (y->i + x->j) / s;
		q[2] = 0.25f * s;
		q[3] = (z->j + y->k) / s;
	}
	else
	{
		s = 2.f * (real)sqrt(1.f + z->k - x->i - y->j);
		q[0] = (x->j - y->i) / s;
		q[1] = (z->i + x->k) / s;
		q[2] = (z->j + y->k) / s;
		q[3] = 0.25f * s;
	}
}

/* a joint's angles as Jolt's swing and twist constraint takes them (its
pyramid's: Quat::GetSwingTwist, SwingTwistConstraintPart::ClampSwingTwist):
the twist about x, the swings about y and z, of frame b seen from frame a
(each its three axes in the world) */
static void ragdoll_joint_angles(real_vector3d const a[3], real_vector3d const b[3], real angles[3])
{
	real_vector3d x, y, z;
	real q[4], s, twist[2], swing[4];

	/* (b's axes in a's frame: the rotation a^-1 b) */
	x.i = ragdoll_dot(&b[0], &a[0]), x.j = ragdoll_dot(&b[0], &a[1]), x.k = ragdoll_dot(&b[0], &a[2]);
	y.i = ragdoll_dot(&b[1], &a[0]), y.j = ragdoll_dot(&b[1], &a[1]), y.k = ragdoll_dot(&b[1], &a[2]);
	z.i = ragdoll_dot(&b[2], &a[0]), z.j = ragdoll_dot(&b[2], &a[1]), z.k = ragdoll_dot(&b[2], &a[2]);
	ragdoll_columns_quaternion(&x, &y, &z, q);
	s = (real)sqrt(q[0] * q[0] + q[1] * q[1]);
	if (s > 1e-6f)
	{
		twist[0] = q[0] / s, twist[1] = q[1] / s;
		swing[0] = s;
		swing[1] = 0.f;
		swing[2] = (q[0] * q[2] - q[1] * q[3]) / s;
		swing[3] = (q[0] * q[3] + q[1] * q[2]) / s;
	}
	else
	{
		twist[0] = 1.f, twist[1] = 0.f;
		swing[0] = q[0], swing[1] = q[1], swing[2] = q[2], swing[3] = q[3];
	}
	if (twist[0] < 0.f)
		twist[0] = -twist[0], twist[1] = -twist[1];
	if (swing[0] < 0.f)
		swing[0] = -swing[0], swing[2] = -swing[2], swing[3] = -swing[3];
	angles[0] = 2.f * (real)atan2(twist[1], twist[0]);
	angles[1] = 2.f * (real)atan2(swing[2], swing[0]);
	angles[2] = 2.f * (real)atan2(swing[3], swing[0]);
}

/* the collision geometry of node (the collision model's node of its name) in
frame's axes about its origin, world units, appended to points */
static short ragdoll_node_points(
	struct collision_model const *collision,
	char const *name,
	real_matrix4x3 const *node,
	real_matrix4x3 const *frame,
	real_point3d *points,
	short count,
	short most)
{
	short index;

	if (!collision)
		return count;
	for (index = 0; index < collision->nodes.count; index++)
	{
		struct collision_node const *collision_node = TAG_BLOCK_GET_ELEMENT(&collision->nodes, index, struct collision_node);
		struct collision_bsp const *bsp;
		long vertex, step;

		if (!ragdoll_same_name(collision_node->name, name, sizeof(collision_node->name)) || collision_node->bsps.count <= 0)
			continue;
		/* (the first permutation's: the body as it stands) */
		bsp = TAG_BLOCK_GET_ELEMENT(&collision_node->bsps, 0, struct collision_bsp);
		step = MAX(1, bsp->vertices.count / MAX(1, (most - count) / 2));
		for (vertex = 0; vertex < bsp->vertices.count && count < most; vertex += step)
		{
			real_point3d const *p = (real_point3d const *)TAG_BLOCK_GET_ELEMENT(&bsp->vertices, vertex, struct collision_vertex);
			real_point3d world;
			real_vector3d offset;

			matrix4x3_transform_point(node, p, &world);
			ragdoll_between(&frame->position, &world, &offset);
			ragdoll_to_local(frame, &offset, (real_vector3d *)&points[count]);
			if (ragdoll_finite(&points[count]))
				count++;
		}
		break;
	}
	return count;
}

static int ragdoll_compare_reals(void const *a, void const *b)
{
	real x = *(real const *)a, y = *(real const *)b;

	return x < y ? -1 : x > y;
}

/* the rig of a dead body from its joints of the body (ragdoll_build): its
bodies, shapes, masses and joints; FALSE: none */
static boolean ragdoll_rig_build(
	struct ragdoll *ragdoll,
	struct object_datum *object,
	struct ragdoll_rig *rig)
{
	struct object_definition *definition = object_definition_get(object->definition_index);
	struct model const *model = model_definition_get(definition->object.model.index);
	struct animation_graph const *graph = definition->object.animation_graph.index != NONE ?
		animation_graph_definition_get(definition->object.animation_graph.index) : NULL;
	struct collision_model const *collision = definition->object.collision_model.index != NONE ?
		collision_model_definition_get(definition->object.collision_model.index) : NULL;
	real_matrix4x3 const *live = (real_matrix4x3 const *)object_header_block_get(ragdoll->object_index,
		&object->object.node_matrices);
	static real_point3d points[RAGDOLL_RIG_POINTS];
	static real off[RAGDOLL_RIG_POINTS], sorted[RAGDOLL_RIG_POINTS];
	short body_of_particle[RAGDOLL_MAXIMUM_PARTICLES];
	short body_particle[RAGDOLL_MAXIMUM_BODIES];
	short owner[RAGDOLL_MAXIMUM_NODES];
	short classes[RAGDOLL_MAXIMUM_BODIES];
	real_vector3d bone[RAGDOLL_MAXIMUM_BODIES];
	real low_z = REAL_MAX, high_z = -REAL_MAX, height, total_mass, shares = 0.f;
	short count = 0, index, node, widened = 0, pool = 0;

	if (graph && graph->nodes.count < ragdoll->node_count)
		graph = NULL;
	if (collision && collision->nodes.count <= 0)
		collision = NULL;
	memset(rig, 0, sizeof(*rig) - sizeof(rig->points));
	snprintf(rig->source, sizeof(rig->source), "%s", ragdoll_object_name(ragdoll->object_index));

	/* (the bodies: the joints of the body but the clavicles and the neck,
	which ride on the chest) */
	for (index = 0; index < ragdoll->particle_count; index++)
	{
		struct ragdoll_particle const *particle = &ragdoll->particles[index];
		char const *name = TAG_BLOCK_GET_ELEMENT(&model->nodes, particle->node_index, struct model_node)->name;

		if (particle->parent != NONE &&
			(ragdoll_name_has(name, "clavicle") || ragdoll_name_has(name, "neck") || count >= RAGDOLL_MAXIMUM_BODIES))
		{
			body_of_particle[index] = body_of_particle[particle->parent];
			continue;
		}
		body_of_particle[index] = count;
		body_particle[count] = index;
		snprintf(rig->bodies[count].node_name, sizeof(rig->bodies[count].node_name), "%s", name);
		rig->bodies[count].parent = particle->parent == NONE ? NONE : body_of_particle[particle->parent];
		classes[count] = ragdoll_class(name);
		count++;
	}
	if (count < RAGDOLL_MINIMUM_PARTICLES - 2)
		return FALSE;
	rig->body_count = count;
	/* (each node's body: its joint's, else its parent's; the body's height
	in its default pose) */
	for (index = 0; index < ragdoll->node_count; index++)
	{
		short n = ragdoll->node_order[index], p = ragdoll->node_particle[n];

		owner[n] = p >= 0 ? body_of_particle[p] : ragdoll->node_parent[n] != NONE ? owner[ragdoll->node_parent[n]] : 0;
		low_z = MIN(low_z, ragdoll_bind[n].position.z);
		high_z = MAX(high_z, ragdoll_bind[n].position.z);
	}
	height = (high_z - low_z) * RAGDOLL_JOLT_METRES * 1.1f;

	for (index = 0; index < count; index++)
	{
		struct ragdoll_rig_body *body = &rig->bodies[index];
		struct ragdoll_particle const *particle = &ragdoll->particles[body_particle[index]];
		short body_node = particle->node_index;
		real_matrix4x3 const *frame = &live[body_node];
		short point_count = 0, children = 0, child_body = NONE, child, k;
		real_vector3d to_child = { 0.f, 0.f, 0.f };
		real_point3d low, high;
		real radius = particle->radius;

		/* (its geometry: the collision model's of every node it carries) */
		for (node = 0; node < ragdoll->node_count; node++)
		{
			if (owner[node] == index)
				point_count = ragdoll_node_points(collision, TAG_BLOCK_GET_ELEMENT(&model->nodes, node, struct model_node)->name,
					&live[node], frame, points, point_count, RAGDOLL_RIG_POINTS);
		}
		/* (its bone: to its one child body; the chest to its head; a leaf
		toward its geometry's middle, else along its node) */
		for (child = index + 1; child < count; child++)
		{
			if (rig->bodies[child].parent != index)
				continue;
			children++;
			if (child_body == NONE || classes[child] == _ragdoll_class_head || classes[child] == _ragdoll_class_spine)
				child_body = child;
		}
		if (child_body != NONE && (children == 1 || classes[child_body] == _ragdoll_class_head ||
			classes[child_body] == _ragdoll_class_spine))
		{
			ragdoll_between(&frame->position, &live[ragdoll->particles[body_particle[child_body]].node_index].position, &to_child);
			ragdoll_to_local(frame, &to_child, &to_child);
		}
		else if (point_count >= 4)
		{
			for (k = 0; k < point_count; k++)
			{
				to_child.i += points[k].x / point_count;
				to_child.j += points[k].y / point_count;
				to_child.k += points[k].z / point_count;
			}
		}
		bone[index] = to_child;
		if (ragdoll_normalize(&bone[index]) < 0.005f)
		{
			bone[index].i = 1.f;
			bone[index].j = bone[index].k = 0.f;
		}

		/* (a limb's geometry about its bone: what lies far off it - a
		Jackal's shield on its forearm - is not the limb's body) */
		if (point_count >= 8 && classes[index] != _ragdoll_class_spine && classes[index] != _ragdoll_class_head &&
			classes[index] != _ragdoll_class_other)
		{
			real keep;
			short kept = 0;

			for (k = 0; k < point_count; k++)
			{
				real along = ragdoll_dot((real_vector3d const *)&points[k], &bone[index]);
				real_vector3d o = *(real_vector3d const *)&points[k];

				ragdoll_move((real_point3d *)&o, &bone[index], -along);
				off[k] = ragdoll_length(&o);
				sorted[k] = off[k];
			}
			qsort(sorted, point_count, sizeof(sorted[0]), ragdoll_compare_reals);
			keep = MAX(0.06f, 2.f * sorted[point_count / 2]);
			for (k = 0; k < point_count; k++)
			{
				if (off[k] <= keep)
					points[kept++] = points[k];
			}
			if (kept >= 4)
				point_count = kept;
		}
		/* (its shape: its collision geometry's hull - the game's own hit
		geometry -; with none, a box about its node, the nodes it carries
		and half way to its children's, the limp body radius about them) */
		if (point_count >= 4 && pool + point_count <= RAGDOLL_RIG_POOL)
		{
			body->hull_first = pool;
			body->hull_count = point_count;
			for (k = 0; k < point_count; k++, pool++)
			{
				rig->points[pool][0] = points[k].x;
				rig->points[pool][1] = points[k].y;
				rig->points[pool][2] = points[k].z;
			}
			low = high = points[0];
		}
		else
		{
			point_count = 0;
			low = high = *global_origin3d;
			for (node = 0; node < ragdoll->node_count; node++)
			{
				short up = ragdoll->node_parent[node];

				/* (its own nodes, and its children's first) */
				if (owner[node] == index || (up != NONE && owner[up] == index && ragdoll->node_particle[node] >= 0))
				{
					real_vector3d at;

					ragdoll_between(&frame->position, &live[node].position, &at);
					ragdoll_to_local(frame, &at, &at);
					if (owner[node] != index)
						at.i *= 0.5f, at.j *= 0.5f, at.k *= 0.5f;
					if (point_count < RAGDOLL_RIG_POINTS)
						points[point_count++] = *(real_point3d *)&at;
				}
			}
			for (k = 0; k < point_count; k++)
			{
				low.x = MIN(low.x, points[k].x - radius), low.y = MIN(low.y, points[k].y - radius), low.z = MIN(low.z, points[k].z - radius);
				high.x = MAX(high.x, points[k].x + radius), high.y = MAX(high.y, points[k].y + radius), high.z = MAX(high.z, points[k].z + radius);
			}
			point_count = 0;
		}
		for (k = 1; k < point_count; k++)
		{
			low.x = MIN(low.x, points[k].x), low.y = MIN(low.y, points[k].y), low.z = MIN(low.z, points[k].z);
			high.x = MAX(high.x, points[k].x), high.y = MAX(high.y, points[k].y), high.z = MAX(high.z, points[k].z);
		}
		body->half[0] = MAX(0.012f, 0.5f * (high.x - low.x));
		body->half[1] = MAX(0.012f, 0.5f * (high.y - low.y));
		body->half[2] = MAX(0.012f, 0.5f * (high.z - low.z));
		body->box_centre[0] = 0.5f * (high.x + low.x);
		body->box_centre[1] = 0.5f * (high.y + low.y);
		body->box_centre[2] = 0.5f * (high.z + low.z);
		body->friction = 0.8f;
		body->restitution = 0.05f;
		body->linear_damping = 0.05f;
		body->angular_damping = 0.3f;
		if (ragdolls_debug() >= 2)
		{
			platform_log("ragdolls: %s body %d %s (parent %d, class %d): %s of %d points, %.3f %.3f %.3f about %.3f %.3f %.3f, "
				"bone %.2f %.2f %.2f", rig->source, (int)index, body->node_name, (int)body->parent, (int)classes[index],
				body->hull_count ? "hull" : "box", (int)body->hull_count, (double)body->half[0], (double)body->half[1],
				(double)body->half[2], (double)body->box_centre[0], (double)body->box_centre[1], (double)body->box_centre[2],
				(double)bone[index].i, (double)bone[index].j, (double)bone[index].k);
		}
	}

	/* (the masses: a man's 80 kg at 1.8 m, as tall a body is, shared as a
	human's) */
	total_mass = PIN(80.f * (height / 1.8f) * (height / 1.8f) * (height / 1.8f), 20.f, 300.f);
	for (index = 0; index < count; index++)
	{
		struct ragdoll_rig_body *body = &rig->bodies[index];
		boolean chest = FALSE;
		short child;

		for (child = index + 1; child < count; child++)
			chest |= rig->bodies[child].parent == index && classes[child] == _ragdoll_class_shoulder;
		body->mass = classes[index] == _ragdoll_class_spine ? ragdoll_trunk_share(body->node_name, chest) :
			ragdoll_limits[classes[index]].mass;
		/* (an Elite's or a Jackal's lower leg in two: a calf's share each) */
		if (classes[index] == _ragdoll_class_knee && ragdoll_name_has(body->node_name, "horselink"))
			body->mass *= 0.6f;
		shares += body->mass;
	}
	for (index = 0; index < count; index++)
	{
		struct ragdoll_rig_body *body = &rig->bodies[index];

		body->mass = MAX(body->mass / MAX(shares, 0.01f) * total_mass, RAGDOLL_RIG_LEAST_MASS);
		/* (its joint's friction, N m in world units: a heavier limb's more) */
		body->joint_friction = (0.5f + 0.15f * body->mass) / (RAGDOLL_JOLT_METRES * RAGDOLL_JOLT_METRES);
	}

	/* (the joints) */
	for (index = 1; index < count; index++)
	{
		struct ragdoll_rig_body *body = &rig->bodies[index];
		struct ragdoll_particle const *particle = &ragdoll->particles[body_particle[index]];
		short parent_node, body_node = particle->node_index;
		real_matrix4x3 const *parent_live, *child_live = &live[body_node];
		struct ragdoll_graph_node const *graph_node = ragdoll_graph_node_get(graph, body_node);
		real cone = ragdoll->cones[body_particle[index]];
		short class_index = classes[index];

		if (body->parent == NONE)
			continue;
		parent_node = ragdoll->particles[body_particle[body->parent]].node_index;
		parent_live = &live[parent_node];
		if (class_index == _ragdoll_class_elbow || class_index == _ragdoll_class_knee)
		{
			/* (about its node's up, signed the way it bends - a knee or an
			elbow toward -up, a horselink toward +up, as every character's
			dying pose has it -: straight to bent; its references the bone
			above and this one) */
			real_vector3d axis, above, below, cross, a, b;
			real sign = ragdoll_name_has(body->node_name, "horselink") ? 1.f : -1.f, bend;

			axis.i = child_live->up.i * sign, axis.j = child_live->up.j * sign, axis.k = child_live->up.k * sign;
			ragdoll_between(&parent_live->position, &child_live->position, &above);
			ragdoll_to_world(child_live, &bone[index], &below);
			ragdoll_normalize(&above);
			body->joint = PHYSICS_JOLT_RIG_JOINT_HINGE;
			ragdoll_to_local(parent_live, &axis, &a);
			ragdoll_to_local(parent_live, &above, &b);
			body->parent_axes[0] = a.i, body->parent_axes[1] = a.j, body->parent_axes[2] = a.k;
			body->parent_axes[3] = b.i, body->parent_axes[4] = b.j, body->parent_axes[5] = b.k;
			ragdoll_to_local(child_live, &axis, &a);
			body->axes[0] = a.i, body->axes[1] = a.j, body->axes[2] = a.k;
			body->axes[3] = bone[index].i, body->axes[4] = bone[index].j, body->axes[5] = bone[index].k;
			body->limits[0] = -RAGDOLL_DEGREES(3.f);
			body->limits[1] = cone;
			/* (bent at the death: its angle, the limits widened to it) */
			ragdoll_cross(&above, &below, &cross);
			bend = (real)atan2(ragdoll_dot(&cross, &axis), ragdoll_dot(&above, &below) - ragdoll_dot(&above, &axis) *
				ragdoll_dot(&below, &axis));
			if (bend < body->limits[0] || bend > body->limits[1])
			{
				widened++;
				body->limits[0] = MIN(body->limits[0], bend - RAGDOLL_DEGREES(2.f));
				body->limits[1] = MAX(body->limits[1], bend + RAGDOLL_DEGREES(2.f));
			}
		}
		else
		{
			/* (a swing and twist about the bone, its zero the default pose:
			its frame the bone (x), the axis it swings to its front about (y),
			the axis it swings aside about (z), in the model's default pose,
			then in each body's frame) */
			real_matrix4x3 const *parent_bind = &ragdoll_bind[parent_node], *child_bind = &ragdoll_bind[body_node];
			real_vector3d x, y, front, outward, a, j1[3], j2[3];
			real scale = 1.f, front_limit, back_limit, out_limit, in_limit, twist, along, side, angles[3];
			short k;

			ragdoll_to_world(child_bind, &bone[index], &x);
			ragdoll_normalize(&x);
			/* (the front: the model's forward, a foot's toes up) */
			front.i = class_index == _ragdoll_class_ankle ? 0.f : 1.f;
			front.j = 0.f;
			front.k = class_index == _ragdoll_class_ankle ? 1.f : 0.f;
			along = ragdoll_dot(&front, &x);
			ragdoll_move((real_point3d *)&front, &x, -along);
			if (ragdoll_normalize(&front) < 0.3f)
			{
				front.i = 0.f, front.j = 0.f, front.k = 1.f;
				along = ragdoll_dot(&front, &x);
				ragdoll_move((real_point3d *)&front, &x, -along);
				ragdoll_normalize(&front);
			}
			ragdoll_cross(&x, &front, &y);
			ragdoll_normalize(&y);
			/* (out: an arm up, a leg or a hand or foot away from the body's
			middle, the spine and the head to the left) */
			outward.i = 0.f;
			outward.j = class_index == _ragdoll_class_shoulder ? 0.f : child_bind->position.y < -0.005f ? -1.f : 1.f;
			outward.k = class_index == _ragdoll_class_shoulder ? 1.f : 0.f;
			side = ragdoll_dot(&y, &outward) >= 0.f ? 1.f : -1.f;
			if (graph_node && TEST_FLAG(graph_node->flags, _ragdoll_joint_no_movement_bit))
				scale = 0.6f;
			front_limit = ragdoll_limits[class_index].front * scale;
			back_limit = ragdoll_limits[class_index].back * scale;
			out_limit = ragdoll_limits[class_index].out * scale;
			in_limit = ragdoll_limits[class_index].in * scale;
			twist = ragdoll_limits[class_index].twist * scale;
			if (class_index == _ragdoll_class_other)
				front_limit = back_limit = out_limit = in_limit = PIN(cone * 57.29578f, 6.f, 60.f);
			else if (class_index == _ragdoll_class_shoulder)
			{
				/* (raised 70 degrees above level at most, lowered to the side:
				from the default pose's arm) */
				real level = (real)asin(PIN(x.k, -1.f, 1.f)) * 57.29578f;

				out_limit = PIN(70.f - level, 10.f, 170.f);
				in_limit = PIN(85.f + level, 10.f, 170.f);
			}
			/* (z's turn takes the bone toward y: out is y's side of it) */
			body->joint = PHYSICS_JOLT_RIG_JOINT_RAGDOLL;
			body->limits[0] = -RAGDOLL_DEGREES(twist);
			body->limits[1] = RAGDOLL_DEGREES(twist);
			body->limits[2] = -RAGDOLL_DEGREES(back_limit);
			body->limits[3] = RAGDOLL_DEGREES(front_limit);
			body->limits[4] = -RAGDOLL_DEGREES(side > 0.f ? in_limit : out_limit);
			body->limits[5] = RAGDOLL_DEGREES(side > 0.f ? out_limit : in_limit);
			ragdoll_to_local(parent_bind, &x, &a);
			body->parent_axes[0] = a.i, body->parent_axes[1] = a.j, body->parent_axes[2] = a.k;
			ragdoll_to_local(parent_bind, &y, &a);
			body->parent_axes[3] = a.i, body->parent_axes[4] = a.j, body->parent_axes[5] = a.k;
			ragdoll_to_local(child_bind, &x, &a);
			body->axes[0] = a.i, body->axes[1] = a.j, body->axes[2] = a.k;
			ragdoll_to_local(child_bind, &y, &a);
			body->axes[3] = a.i, body->axes[4] = a.j, body->axes[5] = a.k;
			/* (its angles at the death, the limits widened to them) */
			for (k = 0; k < 2; k++)
			{
				real_vector3d in_parent, in_child;

				in_parent.i = body->parent_axes[3 * k], in_parent.j = body->parent_axes[3 * k + 1], in_parent.k = body->parent_axes[3 * k + 2];
				in_child.i = body->axes[3 * k], in_child.j = body->axes[3 * k + 1], in_child.k = body->axes[3 * k + 2];
				ragdoll_to_world(parent_live, &in_parent, &j1[k]);
				ragdoll_to_world(child_live, &in_child, &j2[k]);
			}
			ragdoll_cross(&j1[0], &j1[1], &j1[2]);
			ragdoll_cross(&j2[0], &j2[1], &j2[2]);
			ragdoll_joint_angles(j1, j2, angles);
			/* (the joint's zero the dying pose itself - the frames as one at
			the death, on the parent its default pose's -, the limits moved by
			the dying pose's angles: far from the swing's singular turn of 180
			degrees whatever the death's pose, a limit the dying pose is past
			allowed that far; each swing within 95 degrees of the dying pose,
			the pyramid's sound range, the twist within 150) */
			for (k = 0; k < 2; k++)
			{
				real_vector3d in_child;

				ragdoll_to_local(child_live, &j1[k], &in_child);
				body->axes[3 * k] = in_child.i, body->axes[3 * k + 1] = in_child.j, body->axes[3 * k + 2] = in_child.k;
			}
			for (k = 0; k < 3; k++)
			{
				real most = k == 0 ? RAGDOLL_DEGREES(150.f) : RAGDOLL_DEGREES(95.f);

				if (angles[k] < body->limits[2 * k] || angles[k] > body->limits[2 * k + 1])
					widened++;
				body->limits[2 * k] = PIN(body->limits[2 * k] - angles[k], -most, -RAGDOLL_DEGREES(2.f));
				body->limits[2 * k + 1] = PIN(body->limits[2 * k + 1] - angles[k], RAGDOLL_DEGREES(2.f), most);
			}
		}
	}
	ragdoll->widened = widened;
	if (ragdolls_debug())
	{
		platform_log("ragdolls: %08lx (%s) rig: %d bodies, %.0f kg (%.2f m tall), %d limits widened to the dying pose",
			(unsigned long)ragdoll->object_index, rig->source, (int)count, (double)total_mass, (double)height, (int)widened);
	}
	return TRUE;
}

/* the joints of the body become the rig's bodies: a body at each body's
node, each node's matrix at the death in its body's frame (or its own, for
a body's node) */
static boolean ragdoll_rig_particles(
	struct ragdoll *ragdoll,
	struct ragdoll_rig const *rig,
	struct object_datum *object)
{
	struct object_definition *definition = object_definition_get(object->definition_index);
	struct model const *model = model_definition_get(definition->object.model.index);
	real_matrix4x3 const *live = (real_matrix4x3 const *)object_header_block_get(ragdoll->object_index,
		&object->object.node_matrices);
	short nodes[RAGDOLL_MAXIMUM_BODIES];
	short index, node;

	for (index = 0; index < rig->body_count; index++)
	{
		for (node = 0; node < ragdoll->node_count; node++)
		{
			if (!strcmp(TAG_BLOCK_GET_ELEMENT(&model->nodes, node, struct model_node)->name, rig->bodies[index].node_name))
				break;
		}
		if (node == ragdoll->node_count)
			return FALSE;
		nodes[index] = node;
	}
	memset(ragdoll->node_particle, -1, sizeof(ragdoll->node_particle));
	ragdoll->particle_count = rig->body_count;
	for (index = 0; index < rig->body_count; index++)
	{
		struct ragdoll_particle *particle = &ragdoll->particles[index];

		node = nodes[index];
		memset(particle, 0, sizeof(*particle));
		particle->node_index = node;
		particle->parent = rig->bodies[index].parent;
		particle->radius = MIN(rig->bodies[index].half[0], MIN(rig->bodies[index].half[1], rig->bodies[index].half[2]));
		particle->position = particle->previous = particle->start = live[node].position;
		ragdoll_rotation_identity(&particle->delta);
		ragdoll->node_particle[node] = (signed char)index;
	}
	for (index = 0; index < ragdoll->node_count; index++)
	{
		short parent = ragdoll->node_parent[index];

		if (ragdoll->node_particle[index] >= 0 || parent == NONE)
			ragdoll->local[index] = live[index];
		else
		{
			real_matrix4x3 inverse;

			matrix4x3_inverse(&live[parent], &inverse);
			matrix4x3_multiply(&inverse, &live[index], &ragdoll->local[index]);
		}
		ragdoll->pose[index] = live[index];
	}
	return TRUE;
}

/* ---------- the world (the level, its panes, vehicles and machines) */

static boolean ragdoll_mesh_room(struct ragdoll_mesh *mesh, long vertices, long triangles)
{
	if (mesh->vertex_count + vertices > mesh->vertex_most)
	{
		long most = MAX(mesh->vertex_most * 2, mesh->vertex_count + vertices + 1024);
		float *grown = realloc(mesh->vertices, sizeof(float) * 3 * most);

		if (!grown)
			return FALSE;
		mesh->vertices = grown;
		mesh->vertex_most = most;
	}
	if (mesh->triangle_count + triangles > mesh->triangle_most)
	{
		long most = MAX(mesh->triangle_most * 2, mesh->triangle_count + triangles + 1024);
		int *grown = realloc(mesh->triangles, sizeof(int) * 3 * most);

		if (!grown)
			return FALSE;
		mesh->triangles = grown;
		mesh->triangle_most = most;
	}
	return TRUE;
}

/* a collision bsp's surfaces but the invisible ones, each fanned into
triangles wound to face along its plane (two-sided ones both ways), through
matrix (NULL: none, metres); pane: the structure's breakable panes each a
body of its own - NONE the rest, else only that pane's; -2 all */
static void ragdoll_mesh_add_bsp(struct ragdoll_mesh *mesh, struct collision_bsp const *bsp,
	real_matrix4x3 const *matrix, short pane)
{
	long surface_index;

	for (surface_index = 0; surface_index < bsp->surfaces.count; surface_index++)
	{
		struct collision_surface const *surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, surface_index,
			struct collision_surface);
		real_point3d corners[MAXIMUM_VERTICES_PER_COLLISION_SURFACE + 8];
		long edge_index = surface->first_edge_index, count = 0, k, first;
		long plane_index = surface->plane_designator < 0 ? ~surface->plane_designator : surface->plane_designator;
		real_vector3d normal, a, b, c;
		boolean flip, two_sided = TEST_FLAG(surface->flags, 0);

		if (TEST_FLAG(surface->flags, 1) || plane_index < 0 || plane_index >= bsp->bsp3d.planes.count)
			continue;
		if (pane != -2 && (TEST_FLAG(surface->flags, 3) ? surface->breakable_surface_index != pane : pane != NONE))
			continue;
		do
		{
			struct collision_edge const *edge;
			boolean right;

			if (edge_index < 0 || edge_index >= bsp->edges.count)
			{
				count = 0;
				break;
			}
			edge = TAG_BLOCK_GET_ELEMENT(&bsp->edges, edge_index, struct collision_edge);
			right = edge->surface_indices[1] == surface_index;
			if (!VALID_INDEX(edge->vertex_indices[right], bsp->vertices.count))
			{
				count = 0;
				break;
			}
			corners[count++] = TAG_BLOCK_GET_ELEMENT(&bsp->vertices, edge->vertex_indices[right],
				struct collision_vertex)->point;
			edge_index = edge->edge_indices[right];
		} while (edge_index != surface->first_edge_index && count < (long)NUMBEROF(corners));
		if (count < 3)
			continue;
		normal = TAG_BLOCK_GET_ELEMENT(&bsp->bsp3d.planes, plane_index, real_plane3d)->n;
		if (surface->plane_designator < 0)
		{
			normal.i = -normal.i;
			normal.j = -normal.j;
			normal.k = -normal.k;
		}
		ragdoll_between(&corners[0], &corners[1], &a);
		ragdoll_between(&corners[0], &corners[2], &b);
		ragdoll_cross(&a, &b, &c);
		flip = ragdoll_dot(&c, &normal) < 0.f;
		if (mesh->counting)
		{
			mesh->vertex_count += count;
			mesh->triangle_count += (two_sided ? 2 : 1) * (count - 2);
			continue;
		}
		if (!ragdoll_mesh_room(mesh, count, 2 * (count - 2)))
		{
			platform_log("ragdolls: no memory for the level's mesh past %ld triangles: the rest of it left out",
				mesh->triangle_count);
			return;
		}
		first = mesh->vertex_count;
		for (k = 0; k < count; k++)
		{
			real_point3d p = corners[k];

			if (matrix)
				matrix4x3_transform_point(matrix, &corners[k], &p);
			ragdoll_jolt_metres(&p, &mesh->vertices[3 * mesh->vertex_count++]);
		}
		for (k = 1; k + 1 < count; k++)
		{
			int *t = &mesh->triangles[3 * mesh->triangle_count++];

			t[0] = (int)first;
			t[1] = (int)(first + (flip ? k + 1 : k));
			t[2] = (int)(first + (flip ? k : k + 1));
			if (two_sided)
			{
				int *back = &mesh->triangles[3 * mesh->triangle_count++];

				back[0] = t[0];
				back[1] = t[2];
				back[2] = t[1];
			}
		}
	}
}

/* the structure's collision and the fixed scenery's into mesh (counting:
only counted); the scenery objects in it */
static long ragdoll_level_gather(struct ragdoll_mesh *mesh)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	struct object_iterator iterator;
	struct object_datum *object;
	long scenery = 0;

	if (structure && structure->collision_bsp.count > 0)
		ragdoll_mesh_add_bsp(mesh, TAG_BLOCK_GET_ELEMENT(&structure->collision_bsp, 0, struct collision_bsp), NULL, NONE);
	object_iterator_new(&iterator, _object_mask_scenery, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		struct object_definition const *definition = object_definition_get(object->definition_index);
		struct collision_model const *model;
		short node_index, bsp_index;

		if (object->object.parent_object_index != NONE || definition->object.collision_model.index == NONE)
			continue;
		model = collision_model_definition_get(definition->object.collision_model.index);
		for (node_index = 0; node_index < model->nodes.count; node_index++)
		{
			struct collision_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct collision_node);

			if (!object_has_node(iterator.index, node_index))
				continue;
			for (bsp_index = 0; bsp_index < node->bsps.count; bsp_index++)
			{
				ragdoll_mesh_add_bsp(mesh, TAG_BLOCK_GET_ELEMENT(&node->bsps, bsp_index, struct collision_bsp),
					object_get_node_matrix(iterator.index, node_index), NONE);
			}
		}
		scenery++;
	}
	return scenery;
}

/* the level's mesh: the structure's collision, the fixed scenery's (its
arrays sized by a count first, never grown) */
static void ragdoll_level_mesh(
	void)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	struct ragdoll_mesh mesh, count;
	short pane;

	memset(&mesh, 0, sizeof(mesh));
	memset(&count, 0, sizeof(count));
	count.counting = TRUE;
	ragdoll_level_gather(&count);
	/* (room() asks for a surface's whole fan both ways: a surface's worth
	more) */
	mesh.vertex_most = count.vertex_count + MAXIMUM_VERTICES_PER_COLLISION_SURFACE + 8;
	mesh.triangle_most = count.triangle_count + 2 * (MAXIMUM_VERTICES_PER_COLLISION_SURFACE + 8);
	mesh.vertices = malloc(sizeof(float) * 3 * (unsigned long)mesh.vertex_most);
	mesh.triangles = malloc(sizeof(int) * 3 * (unsigned long)mesh.triangle_most);
	if (!mesh.vertices || !mesh.triangles)
	{
		platform_log("ragdolls: no memory for the level's mesh (%ld vertices, %ld triangles)", count.vertex_count,
			count.triangle_count);
		free(mesh.vertices);
		free(mesh.triangles);
		return;
	}
	ragdoll_world.level_scenery = ragdoll_level_gather(&mesh);
	ragdoll_world.level_triangles = mesh.triangle_count;
	ragdoll_world.level_body = physics_jolt_add_mesh(mesh.vertices, (int)mesh.vertex_count, mesh.triangles,
		(int)mesh.triangle_count);
	ragdoll_world.structure = global_structure_bsp_index;
	free(mesh.vertices);
	free(mesh.triangles);
	/* (the panes that stand: a body each, gone when it breaks) */
	ragdoll_world.pane_count = 0;
	for (pane = 0; structure && structure->collision_bsp.count > 0 && pane < RAGDOLL_WORLD_PANES &&
		pane < structure->breakable_surfaces.count; pane++)
	{
		ragdoll_world.panes[pane] = NONE;
		ragdoll_world.pane_count = pane + 1;
		if (!breakable_surface_extant(pane))
			continue;
		memset(&mesh, 0, sizeof(mesh));
		ragdoll_mesh_add_bsp(&mesh, TAG_BLOCK_GET_ELEMENT(&structure->collision_bsp, 0, struct collision_bsp), NULL, pane);
		if (mesh.triangle_count > 0)
		{
			ragdoll_world.panes[pane] = physics_jolt_add_mesh(mesh.vertices, (int)mesh.vertex_count, mesh.triangles,
				(int)mesh.triangle_count);
		}
		free(mesh.vertices);
		free(mesh.triangles);
	}
}

static void ragdoll_world_release(
	void)
{
	short index;

	physics_jolt_destroy();
	memset(&ragdoll_world, 0, sizeof(ragdoll_world));
	ragdoll_world.level_body = NONE;
	for (index = 0; index < RAGDOLL_WORLD_VEHICLES; index++)
		ragdoll_world.vehicles[index].object_index = NONE;
}

/* an object's collision model's vertices in its own frame (its nodes'
matrices taken back into the object's), metres; how many */
static int ragdoll_object_points(long object_index, float *points, int most)
{
	struct object_datum *object = object_try_and_get(object_index);
	struct object_definition const *definition;
	struct collision_model const *model;
	real_vector3d left;
	short node_index, bsp_index;
	int count = 0;

	if (!object)
		return 0;
	definition = object_definition_get(object->definition_index);
	if (definition->object.collision_model.index == NONE)
		return 0;
	model = collision_model_definition_get(definition->object.collision_model.index);
	ragdoll_cross(&object->object.up, &object->object.forward, &left);
	for (node_index = 0; node_index < model->nodes.count; node_index++)
	{
		struct collision_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct collision_node);
		real_matrix4x3 const *matrix;

		if (!object_has_node(object_index, node_index))
			continue;
		matrix = object_get_node_matrix(object_index, node_index);
		for (bsp_index = 0; bsp_index < node->bsps.count; bsp_index++)
		{
			struct collision_bsp const *bsp = TAG_BLOCK_GET_ELEMENT(&node->bsps, bsp_index, struct collision_bsp);
			long vertex_index, step = MAX(1, bsp->vertices.count / MAX(1, most / 2));

			for (vertex_index = 0; vertex_index < bsp->vertices.count && count < most; vertex_index += step)
			{
				real_point3d const *p = (real_point3d const *)TAG_BLOCK_GET_ELEMENT(&bsp->vertices, vertex_index,
					struct collision_vertex);
				real_point3d world;
				real_vector3d d;

				matrix4x3_transform_point(matrix, p, &world);
				ragdoll_between(&object->object.position, &world, &d);
				points[3 * count + 0] = ragdoll_dot(&d, &object->object.forward) * RAGDOLL_JOLT_METRES;
				points[3 * count + 1] = ragdoll_dot(&d, &left) * RAGDOLL_JOLT_METRES;
				points[3 * count + 2] = ragdoll_dot(&d, &object->object.up) * RAGDOLL_JOLT_METRES;
				count++;
			}
		}
	}
	return count;
}

/* the vehicles where the game has them (a hull each, not moved by a body) */
static void ragdoll_world_vehicles(
	void)
{
	static float points[3 * RAGDOLL_WORLD_HULL_POINTS];
	struct object_iterator iterator;
	struct object_datum *object;
	short index;

	for (index = 0; index < RAGDOLL_WORLD_VEHICLES; index++)
		ragdoll_world.vehicles[index].seen = FALSE;
	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		short slot = NONE, free_slot = NONE;
		float position[3], rotation[4];

		if (object->object.parent_object_index != NONE)
			continue;
		for (index = 0; index < RAGDOLL_WORLD_VEHICLES; index++)
		{
			if (ragdoll_world.vehicles[index].object_index == iterator.index)
				slot = index;
			else if (free_slot == NONE && ragdoll_world.vehicles[index].object_index == NONE)
				free_slot = index;
		}
		ragdoll_jolt_metres(&object->object.position, position);
		ragdoll_jolt_quaternion(&object->object.forward, &object->object.up, rotation);
		if (slot == NONE)
		{
			int count;

			if (free_slot == NONE)
				continue;
			count = ragdoll_object_points(iterator.index, points, RAGDOLL_WORLD_HULL_POINTS);
			if (count < 4)
				continue;
			slot = free_slot;
			ragdoll_world.vehicles[slot].object_index = iterator.index;
			ragdoll_world.vehicles[slot].body = physics_jolt_add_moving_hull(points, count, 0.05f, position, rotation);
		}
		else
		{
			physics_jolt_set_enabled(ragdoll_world.vehicles[slot].body, TRUE);
			physics_jolt_move(ragdoll_world.vehicles[slot].body, position, rotation, 1.f / TICKS_PER_SECOND);
		}
		ragdoll_world.vehicles[slot].seen = TRUE;
	}
	/* (gone, or ridden by another: out of the world) */
	for (index = 0; index < RAGDOLL_WORLD_VEHICLES; index++)
	{
		if (ragdoll_world.vehicles[index].object_index == NONE || ragdoll_world.vehicles[index].seen)
			continue;
		if (!object_try_and_get(ragdoll_world.vehicles[index].object_index))
		{
			physics_jolt_remove_body(ragdoll_world.vehicles[index].body);
			ragdoll_world.vehicles[index].object_index = NONE;
		}
		else
			physics_jolt_set_enabled(ragdoll_world.vehicles[index].body, FALSE);
	}
}

/* each machine's parts (a door, a lift) where the machine has them */
static void ragdoll_world_machines(
	void)
{
	struct object_iterator iterator;
	struct object_datum *object;
	short index;

	object_iterator_new(&iterator, _object_mask_machine, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		struct object_definition const *definition = object_definition_get(object->definition_index);
		struct collision_model const *model;
		short node_index;

		if (definition->object.collision_model.index == NONE)
			continue;
		model = collision_model_definition_get(definition->object.collision_model.index);
		for (node_index = 0; node_index < model->nodes.count; node_index++)
		{
			struct collision_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct collision_node);
			real_matrix4x3 const *matrix;
			short slot = NONE;
			float position[3], rotation[4];

			if (!node->bsps.count || !object_has_node(iterator.index, node_index))
				continue;
			for (index = 0; index < ragdoll_world.machine_part_count && slot == NONE; index++)
			{
				if (ragdoll_world.machine_parts[index].object_index == iterator.index &&
					ragdoll_world.machine_parts[index].node == node_index)
				{
					slot = index;
				}
			}
			matrix = object_get_node_matrix(iterator.index, node_index);
			ragdoll_jolt_metres(&matrix->position, position);
			ragdoll_jolt_quaternion(&matrix->forward, &matrix->up, rotation);
			if (slot == NONE)
			{
				struct ragdoll_mesh mesh;
				short bsp_index;

				if (ragdoll_world.machine_part_count >= RAGDOLL_WORLD_MACHINE_PARTS)
					continue;
				memset(&mesh, 0, sizeof(mesh));
				for (bsp_index = 0; bsp_index < node->bsps.count; bsp_index++)
					ragdoll_mesh_add_bsp(&mesh, TAG_BLOCK_GET_ELEMENT(&node->bsps, bsp_index, struct collision_bsp), NULL, -2);
				slot = ragdoll_world.machine_part_count++;
				ragdoll_world.machine_parts[slot].object_index = iterator.index;
				ragdoll_world.machine_parts[slot].node = node_index;
				ragdoll_world.machine_parts[slot].body = physics_jolt_add_moving_mesh(mesh.vertices, (int)mesh.vertex_count,
					mesh.triangles, (int)mesh.triangle_count, position, rotation);
				free(mesh.vertices);
				free(mesh.triangles);
			}
			else
				physics_jolt_move(ragdoll_world.machine_parts[slot].body, position, rotation, 1.f / TICKS_PER_SECOND);
		}
	}
}

/* the world for this game state's level, made if need be: as the level
starts, not at the first death (a campaign level's mesh takes tens of
milliseconds); its other structure in its place after a switch */
static boolean ragdoll_world_ready(
	void)
{
	unsigned int started;
	float gravity[3];

	if (ragdoll_world.built && ragdoll_world.generation == ragdoll_generation &&
		ragdoll_world.structure == global_structure_bsp_index && physics_jolt_exists())
	{
		return TRUE;
	}
	/* (one that could not be made - no memory to spare - is not tried again
	before the next map, revert or structure) */
	if (ragdoll_world_failed.generation == ragdoll_generation &&
		ragdoll_world_failed.structure == global_structure_bsp_index)
	{
		return FALSE;
	}
	started = ragdoll_now();
	ragdoll_world_release();
	gravity[0] = gravity[1] = 0.f;
	gravity[2] = -global_gravity * RAGDOLL_JOLT_METRES * TICKS_PER_SECOND * TICKS_PER_SECOND;
	/* (the level and its panes, the vehicles and machines' parts, and a dozen
	ragdolls' bodies) */
	if (!physics_jolt_create(RAGDOLL_WORLD_PANES + RAGDOLL_WORLD_VEHICLES + RAGDOLL_WORLD_MACHINE_PARTS +
		RAGDOLL_SLOTS * RAGDOLL_MAXIMUM_BODIES + 8, gravity))
	{
		ragdoll_world_failed.generation = ragdoll_generation;
		ragdoll_world_failed.structure = global_structure_bsp_index;
		platform_log("ragdolls: no world for this level (no memory to spare): its dead bodies lie as the game lays them");
		return FALSE;
	}
	ragdoll_world.built = TRUE;
	ragdoll_world.generation = ragdoll_generation;
	ragdoll_level_mesh();
	ragdoll_world_vehicles();
	ragdoll_world_machines();
	physics_jolt_optimize();
	{
		struct physics_jolt_stats stats;

		physics_jolt_get_stats(&stats);
		platform_log("ragdolls: the level's world: %ld triangles (structure %d and %ld scenery), %d panes, %d bodies, "
			"made in %.1f ms, %.2f MB", ragdoll_world.level_triangles, (int)ragdoll_world.structure,
			ragdoll_world.level_scenery, (int)ragdoll_world.pane_count, stats.bodies, ragdoll_ms_since(started),
			(double)stats.bytes_now / 1048576.0);
	}
	return TRUE;
}

/* a tick with a ragdoll awake, before the step: what moves in the world
where the game has it, the broken panes gone */
static void ragdoll_world_update(
	void)
{
	short index;

	for (index = 0; index < ragdoll_world.pane_count; index++)
	{
		if (ragdoll_world.panes[index] != NONE && !breakable_surface_extant(index))
		{
			physics_jolt_remove_body(ragdoll_world.panes[index]);
			ragdoll_world.panes[index] = NONE;
		}
	}
	ragdoll_world_vehicles();
	ragdoll_world_machines();
}

/* ---------- impulses */

static struct ragdoll_damage_note const *ragdoll_note_find(
	long object_index)
{
	struct ragdoll_damage_note const *found = NULL;
	short index;

	for (index = 0; index < RAGDOLL_DAMAGE_NOTES; index++)
	{
		struct ragdoll_damage_note const *note = &ragdoll_notes[index];

		if (note->object_index == object_index && ragdoll_ticks - note->tick <= RAGDOLL_NOTE_TICKS &&
			(!found || note->tick >= found->tick))
		{
			found = note;
		}
	}
	return found;
}

/* a body's motion changed by speed (world units a tick) along direction */
static void ragdoll_push(
	struct ragdoll_particle *particle,
	real_vector3d const *direction,
	real speed)
{
	real per_step = MIN(speed, RAGDOLL_MELEE_MAXIMUM) / RAGDOLL_SUBSTEPS;

	particle->previous.x -= direction->i * per_step;
	particle->previous.y -= direction->j * per_step;
	particle->previous.z -= direction->k * per_step;
}

/* a blow: an explosion throws each part from its centre, the nearest the
most, a little upward; a melee blow the whole body along it; a shot the
part nearest its line, and its neighbours half as much, and the shot that
kills (killing) the whole body back too */
static void ragdoll_apply_note(
	struct ragdoll *ragdoll,
	struct ragdoll_damage_note const *note,
	boolean killing)
{
	short index;

	if (note->explosion)
	{
		real_vector3d pushes[RAGDOLL_MAXIMUM_PARTICLES];
		real_vector3d mean = { 0.f, 0.f, 0.f };
		real nearest = REAL_MAX, farthest = 0.f;

		for (index = 0; index < ragdoll->particle_count; index++)
		{
			real distance = ragdoll_distance(&note->origin, &ragdoll->particles[index].position);

			nearest = MIN(nearest, distance);
			farthest = MAX(farthest, distance);
		}
		for (index = 0; index < ragdoll->particle_count; index++)
		{
			struct ragdoll_particle *particle = &ragdoll->particles[index];
			real_vector3d *push = &pushes[index];
			real distance = ragdoll_distance(&note->origin, &particle->position);
			real near = farthest > nearest ? 1.f - (distance - nearest) / (farthest - nearest) : 1.f;
			real speed = MIN(note->speed, RAGDOLL_MAXIMUM_IMPULSE) * (0.3f + 0.7f * near);

			ragdoll_between(&note->origin, &particle->position, push);
			if (ragdoll_normalize(push) < 1e-5f)
				*push = note->direction;
			push->k += 0.25f;
			ragdoll_normalize(push);
			push->i *= speed;
			push->j *= speed;
			push->k *= speed;
			mean.i += push->i / ragdoll->particle_count;
			mean.j += push->j / ragdoll->particle_count;
			mean.k += push->k / ragdoll->particle_count;
		}
		/* (the blast spreads the limbs, the nearest the most: the whole body
		is thrown as the game threw it, which its motion already has) */
		for (index = 0; index < ragdoll->particle_count; index++)
		{
			real_vector3d spread;
			real speed;

			spread.i = pushes[index].i - mean.i;
			spread.j = pushes[index].j - mean.j;
			spread.k = pushes[index].k - mean.k;
			speed = ragdoll_normalize(&spread);
			ragdoll_push(&ragdoll->particles[index], &spread, speed);
		}
	}
	else
	{
		short nearest = NONE;
		real nearest_distance = REAL_MAX;
		real speed = note->melee ? MAX(note->speed, RAGDOLL_MELEE_SPEED) :
			MIN(MAX(note->speed, RAGDOLL_SHOT_SPEED), RAGDOLL_MAXIMUM_IMPULSE);

		for (index = 0; index < ragdoll->particle_count; index++)
		{
			real_vector3d to;
			real along, distance_squared;

			ragdoll_between(&note->origin, &ragdoll->particles[index].position, &to);
			along = ragdoll_dot(&to, &note->direction);
			distance_squared = ragdoll_dot(&to, &to) - along * along;
			if (distance_squared < nearest_distance)
			{
				nearest_distance = distance_squared;
				nearest = index;
			}
		}
		if (nearest == NONE)
			return;
		if (note->melee)
		{
			/* (a melee blow: the whole body thrown along it, a little upward,
			the part nearest its line a quarter harder, so it turns as it
			goes) */
			real_vector3d throw_direction = note->direction;

			throw_direction.k += 0.3f;
			ragdoll_normalize(&throw_direction);
			for (index = 0; index < ragdoll->particle_count; index++)
				ragdoll_push(&ragdoll->particles[index], &throw_direction, speed * 0.85f);
			ragdoll_push(&ragdoll->particles[nearest], &throw_direction, speed * 0.25f);
			return;
		}
		ragdoll_push(&ragdoll->particles[nearest], &note->direction, speed);
		for (index = 0; index < ragdoll->particle_count; index++)
		{
			if (ragdoll->particles[index].parent == nearest || ragdoll->particles[nearest].parent == index)
				ragdoll_push(&ragdoll->particles[index], &note->direction, speed * 0.5f);
		}
		if (killing)
		{
			real_vector3d back = note->direction;
			real knockback = PIN(note->speed * RAGDOLL_SHOT_KNOCKBACK_SCALE, RAGDOLL_SHOT_KNOCKBACK, RAGDOLL_LAUNCH_SPEED);

			back.k += 0.25f;
			ragdoll_normalize(&back);
			for (index = 0; index < ragdoll->particle_count; index++)
				ragdoll_push(&ragdoll->particles[index], &back, knockback);
		}
	}
}

/* ---------- Jolt's ragdoll */

static void ragdoll_jolt_release(
	struct ragdoll *ragdoll)
{
	if (ragdoll->jolt != NONE && ragdoll->jolt_generation == physics_jolt_generation())
		physics_jolt_rig_destroy(ragdoll->jolt);
	ragdoll->jolt = NONE;
}

static void ragdoll_copy(real const *in, float *out, int count, float scale)
{
	int c;

	for (c = 0; c < count; c++)
		out[c] = in[c] * scale;
}

/* the rig's bodies made in Jolt's world, each moving as its body moves
(its motion since previous) */
static boolean ragdoll_jolt_start(
	struct ragdoll *ragdoll,
	struct ragdoll_rig const *rig)
{
	static struct physics_jolt_rig_body bodies[RAGDOLL_MAXIMUM_BODIES];
	static float points[3 * RAGDOLL_RIG_POOL];
	struct physics_jolt_rig_options options;
	short index;

	memset(bodies, 0, sizeof(bodies));
	for (index = 0; index < rig->body_count; index++)
	{
		struct ragdoll_rig_body const *body = &rig->bodies[index];
		struct physics_jolt_rig_body *out = &bodies[index];
		struct ragdoll_particle const *particle = &ragdoll->particles[index];
		real_matrix4x3 const *node = &ragdoll->local[particle->node_index];

		out->parent = body->parent;
		out->joint = body->joint;
		ragdoll_jolt_metres(&node->position, out->position);
		ragdoll_jolt_quaternion(&node->forward, &node->up, out->rotation);
		memcpy(ragdoll->rig_turn[index], out->rotation, sizeof(ragdoll->rig_turn[index]));
		out->velocity[0] = (particle->position.x - particle->previous.x) * RAGDOLL_JOLT_SPEED;
		out->velocity[1] = (particle->position.y - particle->previous.y) * RAGDOLL_JOLT_SPEED;
		out->velocity[2] = (particle->position.z - particle->previous.z) * RAGDOLL_JOLT_SPEED;
		ragdoll_copy(body->half, out->half, 3, RAGDOLL_JOLT_METRES);
		ragdoll_copy(body->box_centre, out->box_centre, 3, RAGDOLL_JOLT_METRES);
		out->box_rotation[3] = 1.f;
		out->mass = body->mass;
		out->friction = body->friction;
		out->restitution = body->restitution;
		out->linear_damping = body->linear_damping;
		out->angular_damping = body->angular_damping;
		ragdoll_copy(body->parent_axes, out->parent_axes, 6, 1.f);
		ragdoll_copy(body->axes, out->axes, 6, 1.f);
		ragdoll_copy(body->limits, out->limits, 6, 1.f);
		out->joint_friction = body->joint_friction * RAGDOLL_JOLT_METRES * RAGDOLL_JOLT_METRES;
		/* (continuous collision for every part: a foot that passed into a
		rock's lip stayed there) */
		out->linear_cast = TRUE;
		if (body->hull_count >= 4 && body->hull_first + body->hull_count <= RAGDOLL_RIG_POOL)
		{
			ragdoll_copy(&rig->points[body->hull_first][0], points + 3 * body->hull_first, 3 * body->hull_count,
				RAGDOLL_JOLT_METRES);
			out->points = points + 3 * body->hull_first;
			out->point_count = body->hull_count;
		}
	}
	/* (its joints and parts solved with ten velocity and three position
	iterations) */
	options.velocity_steps = 10;
	options.position_steps = 3;
	ragdoll->jolt = physics_jolt_rig_create(bodies, rig->body_count, &options);
	ragdoll->jolt_generation = physics_jolt_generation();
	return ragdoll->jolt != NONE;
}

/* a blow to a ragdoll: what it gave its bodies (their motion since before)
their velocities, the ragdoll awake */
static void ragdoll_jolt_kick(
	struct ragdoll *ragdoll,
	real_point3d const *before)
{
	short index;

	if (ragdoll->jolt == NONE || ragdoll->jolt_generation != physics_jolt_generation())
		return;
	physics_jolt_rig_set_active(ragdoll->jolt, TRUE);
	for (index = 0; index < ragdoll->particle_count; index++)
	{
		struct ragdoll_particle *particle = &ragdoll->particles[index];
		float velocity[3];

		velocity[0] = (before[index].x - particle->previous.x) * RAGDOLL_JOLT_SPEED;
		velocity[1] = (before[index].y - particle->previous.y) * RAGDOLL_JOLT_SPEED;
		velocity[2] = (before[index].z - particle->previous.z) * RAGDOLL_JOLT_SPEED;
		particle->previous = particle->position;
		if (velocity[0] != 0.f || velocity[1] != 0.f || velocity[2] != 0.f)
			physics_jolt_rig_add_velocity(ragdoll->jolt, index, velocity);
	}
}

/* the pose drawn: each body's node where Jolt has the body and turned as it
has turned since the death, each child body drawn where its joint holds it
on its parent (no drawn bone ever changes length, whatever the solver
leaves between two bodies a step), the other nodes carried by their
parents; and the sphere about it */
static void ragdoll_pose_make(
	struct ragdoll *ragdoll)
{
	real_point3d drawn[RAGDOLL_MAXIMUM_PARTICLES];
	real_point3d low, high;
	short index;

	for (index = 0; index < ragdoll->particle_count; index++)
	{
		struct ragdoll_particle const *particle = &ragdoll->particles[index];

		drawn[index] = particle->position;
		if (particle->parent != NONE && particle->parent < index)
		{
			struct ragdoll_particle const *parent = &ragdoll->particles[particle->parent];
			real_vector3d offset, turned;

			ragdoll_between(&parent->start, &particle->start, &offset);
			ragdoll_rotate(&parent->delta, &offset, &turned);
			drawn[index] = drawn[particle->parent];
			ragdoll_move(&drawn[index], &turned, 1.f);
		}
	}
	for (index = 0; index < ragdoll->node_count; index++)
	{
		short node_index = ragdoll->node_order[index];
		short p = ragdoll->node_particle[node_index];
		real_matrix4x3 *pose = &ragdoll->pose[node_index];

		if (p >= 0)
		{
			struct ragdoll_particle const *particle = &ragdoll->particles[p];
			real_matrix4x3 const *death = &ragdoll->local[node_index];

			pose->scale = death->scale;
			ragdoll_rotate(&particle->delta, &death->forward, &pose->forward);
			ragdoll_rotate(&particle->delta, &death->left, &pose->left);
			ragdoll_rotate(&particle->delta, &death->up, &pose->up);
			pose->position = drawn[p];
		}
		else if (ragdoll->node_parent[node_index] != NONE)
			matrix4x3_multiply(&ragdoll->pose[ragdoll->node_parent[node_index]], &ragdoll->local[node_index], pose);
	}
	low = high = ragdoll->pose[0].position;
	for (index = 1; index < ragdoll->node_count; index++)
	{
		real_point3d const *p = &ragdoll->pose[index].position;

		low.x = MIN(low.x, p->x);
		low.y = MIN(low.y, p->y);
		low.z = MIN(low.z, p->z);
		high.x = MAX(high.x, p->x);
		high.y = MAX(high.y, p->y);
		high.z = MAX(high.z, p->z);
	}
	ragdoll->center.x = 0.5f * (low.x + high.x);
	ragdoll->center.y = 0.5f * (low.y + high.y);
	ragdoll->center.z = 0.5f * (low.z + high.z);
	ragdoll->radius = 0.5f * ragdoll_distance(&low, &high) + RAGDOLL_SPHERE_MARGIN;
	{
		struct location location;

		scenario_location_from_point(&location, &ragdoll->center);
		ragdoll->drawn_cluster = location.cluster_index;
	}
}

/* ---------- slots */

static void ragdoll_release(
	short slot,
	short reason)
{
	struct ragdoll *ragdoll = &ragdolls[slot];
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(ragdoll->object_index);

	if (ragdoll->state == _ragdoll_free)
		return;
	if (ragdolls_debug())
	{
		platform_log("ragdolls: %08lx (%s) let go: %s (%ld ticks, %s)", (unsigned long)ragdoll->object_index,
			reason == _ragdoll_released_map ? "-" : ragdoll_object_name(ragdoll->object_index),
			ragdoll_release_names[reason], ragdoll->age, ragdoll->state == _ragdoll_awake ? "awake" : "asleep");
	}
	if (VALID_INDEX(absolute_index, MAXIMUM_OBJECTS_PER_MAP) && ragdoll_slot_by_object[absolute_index] == slot + 1)
		ragdoll_slot_by_object[absolute_index] = 0;
	ragdoll_jolt_release(ragdoll);
	ragdoll->state = _ragdoll_free;
	ragdoll->object_index = NONE;
	ragdoll_stats.released[reason]++;
}

static void ragdoll_sleep(
	struct ragdoll *ragdoll,
	char const *why)
{
	ragdoll->state = _ragdoll_asleep;
	ragdoll_stats.slept++;
	if (ragdoll->jolt != NONE && ragdoll->jolt_generation == physics_jolt_generation())
		physics_jolt_rig_set_active(ragdoll->jolt, FALSE);
	if (ragdolls_debug())
	{
		platform_log("ragdolls: %08lx (%s) asleep: %s (%ld ticks) at %.3f %.3f %.3f", (unsigned long)ragdoll->object_index,
			ragdoll_object_name(ragdoll->object_index), why, ragdoll->age, (double)ragdoll->center.x,
			(double)ragdoll->center.y, (double)ragdoll->center.z);
	}
}

static struct ragdoll *ragdoll_of_object(
	long object_index)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	short slot;

	if (object_index == NONE || !VALID_INDEX(absolute_index, MAXIMUM_OBJECTS_PER_MAP))
		return NULL;
	slot = ragdoll_slot_by_object[absolute_index];
	if (!slot)
		return NULL;
	if (ragdolls[slot - 1].object_index != object_index || ragdolls[slot - 1].state == _ragdoll_free ||
		ragdolls[slot - 1].generation != ragdoll_generation)
	{
		return NULL;
	}
	return &ragdolls[slot - 1];
}

/* a slot for a new ragdoll: a free one, else the one asleep and not drawn
for longest (the oldest of them), else the oldest asleep, else the oldest */
static short ragdoll_slot_new(
	void)
{
	short slot, chosen = NONE;
	short pass;

	for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
	{
		if (ragdolls[slot].state == _ragdoll_free)
			return slot;
	}
	for (pass = 0; pass < 3 && chosen == NONE; pass++)
	{
		for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
		{
			struct ragdoll *ragdoll = &ragdolls[slot];

			if (pass < 2 && ragdoll->state != _ragdoll_asleep)
				continue;
			if (pass == 0 && ragdoll_frames - ragdoll->drawn_frame < RAGDOLL_UNSEEN_FRAMES)
				continue;
			if (chosen == NONE || ragdoll->started < ragdolls[chosen].started)
				chosen = slot;
		}
	}
	if (chosen != NONE)
		ragdoll_release(chosen, _ragdoll_released_replaced);
	return chosen;
}

/* the body is a local player's own unit (its dead camera follows where the
game has it) */
static boolean ragdoll_local_player_body(
	long object_index)
{
	short local_player_index;

	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
	{
		long player_index = local_player_get_player_index(local_player_index);
		struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;

		/* (player_died has it as its dead unit) */
		if (player && (player->unit_index == object_index || player->dead_unit_index == object_index))
			return TRUE;
	}
	return FALSE;
}

/* a body whose pose is to be a ragdoll's from now (FALSE: none made) */
static boolean ragdoll_start(
	long object_index,
	struct object_datum *object)
{
	struct ragdoll_damage_note const *note = ragdoll_note_find(object_index);
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	struct ragdoll *ragdoll;
	struct ragdoll_rig *rig;
	short slot;
	short index;
	real_vector3d velocity = object->object.translational_velocity;
	unsigned int started = ragdoll_now();
	boolean made;

	if (!VALID_INDEX(absolute_index, MAXIMUM_OBJECTS_PER_MAP) || ragdoll_of_object(object_index) || !ragdoll_world_ready())
		return FALSE;
	slot = ragdoll_slot_new();
	if (slot == NONE)
		return FALSE;
	ragdoll = &ragdolls[slot];
	rig = &ragdoll_rigs[slot];
	/* (a body whose root its pose leaves in solid structure - a grunt dead
	against a wall - is none) */
	made = ragdoll_build(ragdoll, object_index, object) &&
		!collision_test_point(RAGDOLL_SOLID_FLAGS, &ragdoll->particles[0].position, NONE) &&
		ragdoll_rig_build(ragdoll, object, rig) && ragdoll_rig_particles(ragdoll, rig, object);
	if (made)
	{
		/* (the body's own motion, as the game threw it; no faster than a
		blast plausibly throws a body, but a local player's own, which the
		camera follows) */
		if (!ragdoll_finite((real_point3d const *)&velocity))
			velocity = *global_zero_vector3d;
		if (!ragdoll_local_player_body(object_index))
		{
			real speed = ragdoll_length(&velocity);

			if (speed > RAGDOLL_LAUNCH_SPEED)
			{
				velocity.i *= RAGDOLL_LAUNCH_SPEED / speed;
				velocity.j *= RAGDOLL_LAUNCH_SPEED / speed;
				velocity.k *= RAGDOLL_LAUNCH_SPEED / speed;
			}
		}
		for (index = 0; index < ragdoll->particle_count; index++)
		{
			struct ragdoll_particle *particle = &ragdoll->particles[index];

			particle->previous.x = particle->position.x - velocity.i / RAGDOLL_SUBSTEPS;
			particle->previous.y = particle->position.y - velocity.j / RAGDOLL_SUBSTEPS;
			particle->previous.z = particle->position.z - velocity.k / RAGDOLL_SUBSTEPS;
		}
		if (note)
			ragdoll_apply_note(ragdoll, note, TRUE);
		made = ragdoll_jolt_start(ragdoll, rig);
	}
	if (!made)
	{
		memset(ragdoll, 0, sizeof(*ragdoll));
		ragdoll->state = _ragdoll_free;
		ragdoll->object_index = NONE;
		ragdoll->jolt = NONE;
		ragdoll_stats.rejected++;
		if (ragdolls_debug())
			platform_log("ragdolls: %08lx (%s) no ragdoll: no body to make one of", (unsigned long)object_index,
				ragdoll_object_name(object_index));
		return FALSE;
	}
	ragdoll->generation = ragdoll_generation;
	ragdoll->state = _ragdoll_awake;
	ragdoll->started = ragdoll->woken = ++ragdoll_sequence;
	ragdoll->body = object->object.position;
	ragdoll->started_tick = ragdoll_ticks;
	ragdoll->drawn_frame = ragdoll_frames;
	ragdoll_pose_make(ragdoll);
	ragdoll_slot_by_object[absolute_index] = (unsigned char)(slot + 1);
	ragdoll_stats.started++;
	ragdoll_stats.start_ms += ragdoll_ms_since(started);
	if (ragdolls_debug())
	{
		platform_log("ragdolls: %08lx (%s) started in slot %d: %d bodies; %s %.3f wu/tick", (unsigned long)object_index,
			ragdoll_object_name(object_index), (int)slot, (int)ragdoll->particle_count,
			note ? (note->explosion ? "an explosion" : note->melee ? "a melee blow" : "a blow") : "no blow",
			note ? (double)note->speed : 0.0);
	}
	return TRUE;
}

/* a tick of an awake ragdoll: its bodies where Jolt has them, the pose;
FALSE: let go (why in reason) */
static boolean ragdoll_step(
	struct ragdoll *ragdoll,
	struct object_datum *object,
	short *reason)
{
	real_matrix4x3 const *live = (real_matrix4x3 const *)object_header_block_get(ragdoll->object_index,
		&object->object.node_matrices);
	real most = 0.f;
	short index;

	if (ragdoll->jolt == NONE || ragdoll->jolt_generation != physics_jolt_generation())
	{
		*reason = _ragdoll_released_map;
		return FALSE;
	}
	for (index = 0; index < ragdoll->particle_count; index++)
	{
		struct ragdoll_particle *particle = &ragdoll->particles[index];
		float position[3], q[4];
		float const *o = ragdoll->rig_turn[index];
		float x = -o[0], y = -o[1], z = -o[2], w = o[3];
		real wijk[4];

		physics_jolt_rig_get_body(ragdoll->jolt, index, position, q);
		particle->previous = particle->position;
		particle->position.x = position[0] / RAGDOLL_JOLT_METRES;
		particle->position.y = position[1] / RAGDOLL_JOLT_METRES;
		particle->position.z = position[2] / RAGDOLL_JOLT_METRES;
		if (!ragdoll_finite(&particle->position))
		{
			*reason = _ragdoll_released_not_a_number;
			return FALSE;
		}
		most = MAX(most, ragdoll_distance(&particle->previous, &particle->position));
		/* (its turn since the death: now times the death's inverse) */
		wijk[0] = q[3] * w - q[0] * x - q[1] * y - q[2] * z;
		wijk[1] = q[3] * x + q[0] * w + q[1] * z - q[2] * y;
		wijk[2] = q[3] * y - q[0] * z + q[1] * w + q[2] * x;
		wijk[3] = q[3] * z + q[0] * y - q[1] * x + q[2] * w;
		ragdoll_rotation_from_quaternion(wijk, &particle->delta);
	}
	if (ragdoll_distance(&ragdoll->particles[0].position, &live[ragdoll->particles[0].node_index].position) >
		RAGDOLL_TETHER)
	{
		*reason = _ragdoll_released_far;
		return FALSE;
	}
	ragdoll_pose_make(ragdoll);
	ragdoll->body = object->object.position;
	ragdoll->age++;
	ragdoll->awake_ticks++;
	/* (Jolt's sleep, or ours: no part moved 1.5 mm a tick for half a second
	- a body that shivers in a corner) */
	ragdoll->still_ticks = most < RAGDOLL_STILL ? ragdoll->still_ticks + 1 : 0;
	if (!physics_jolt_rig_active(ragdoll->jolt))
		ragdoll_sleep(ragdoll, "at rest (jolt)");
	else if (ragdoll->still_ticks >= RAGDOLL_SLEEP_TICKS && ragdoll->age >= RAGDOLL_SLEEP_AGE)
		ragdoll_sleep(ragdoll, "at rest (still)");
	else if (ragdoll->awake_ticks >= RAGDOLL_AWAKE_TICKS)
		ragdoll_sleep(ragdoll, "awake too long");
	return TRUE;
}

/* the body a ragdoll is of, while it is still a dead body (NULL: let go, why
in reason) */
static struct object_datum *ragdoll_body(
	struct ragdoll const *ragdoll,
	short *reason)
{
	struct object_datum *object = object_try_and_get(ragdoll->object_index);
	struct unit_datum const *unit = (struct unit_datum const *)object;

	if (!object || object->object.type != _object_type_biped)
	{
		*reason = _ragdoll_released_gone;
		return NULL;
	}
	if (!TEST_FLAG(object->object.damage_flags, _object_dead_bit) || unit->unit.feign_death_timer > 0)
	{
		*reason = _ragdoll_released_alive;
		return NULL;
	}
	if (object->object.parent_object_index != NONE)
	{
		*reason = _ragdoll_released_attached;
		return NULL;
	}
	if (object->object.node_matrices.size / (short)sizeof(real_matrix4x3) != ragdoll->node_count)
	{
		*reason = _ragdoll_released_changed;
		return NULL;
	}
	return object;
}

/* an awake ragdoll that may be put to sleep for a new one: one awake a
second at least and nearly at rest (not one still falling, frozen in the
air), the stillest of them; NONE: none */
static short ragdoll_room(
	void)
{
	short slot, chosen = NONE;

	for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
	{
		struct ragdoll const *ragdoll = &ragdolls[slot];

		if (ragdoll->state != _ragdoll_awake || ragdoll->awake_ticks < RAGDOLL_ROOM_TICKS ||
			ragdoll->still_ticks < RAGDOLL_ROOM_STILL_TICKS)
		{
			continue;
		}
		if (chosen == NONE || ragdoll->still_ticks > ragdolls[chosen].still_ticks ||
			(ragdoll->still_ticks == ragdolls[chosen].still_ticks && ragdoll->woken < ragdolls[chosen].woken))
		{
			chosen = slot;
		}
	}
	return chosen;
}

/* ---------- public code: the game's side */

void ragdolls_reset(
	void)
{
	short slot;

	for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
	{
		if (ragdolls[slot].state != _ragdoll_free)
			ragdoll_stats.released[_ragdoll_released_map]++;
		ragdolls[slot].state = _ragdoll_free;
		ragdolls[slot].object_index = NONE;
		ragdolls[slot].jolt = NONE;
	}
	ragdoll_pending_count = 0;
	memset(ragdoll_notes, 0, sizeof(ragdoll_notes));
	memset(ragdoll_slot_by_object, 0, sizeof(ragdoll_slot_by_object));
	ragdoll_swapped.count = 0;
	ragdoll_generation++;
	/* (the world goes with the game state: its vehicles and machines are
	that state's objects; the level's mesh is kept for the next world of
	the same level) */
	ragdoll_world_release();
}

void ragdolls_new_map(
	void)
{
	ragdolls_reset();
	physics_jolt_forget_meshes();
}

void ragdolls_unit_died(
	long unit_index)
{
	struct object_datum *object;

	if (!ragdolls_enabled())
		return;
	object = object_try_and_get(unit_index);
	/* (a body the game lays limp: ragdoll_build) */
	if (!object || object->object.type != _object_type_biped ||
		!TEST_FLAG(biped_definition_get(object->definition_index)->biped.flags, _biped_uses_limp_body_physics_bit))
	{
		return;
	}
	if (ragdoll_pending_count >= RAGDOLL_PENDING)
	{
		/* (the oldest forgotten) */
		memmove(ragdoll_pending, ragdoll_pending + 1, sizeof(ragdoll_pending[0]) * (RAGDOLL_PENDING - 1));
		ragdoll_pending_count--;
	}
	ragdoll_pending[ragdoll_pending_count].object_index = unit_index;
	ragdoll_pending[ragdoll_pending_count].tick = ragdoll_ticks;
	ragdoll_pending_count++;
}

void ragdolls_note_damage(
	long object_index,
	struct damage_data const *damage)
{
	struct object_datum *object;
	struct object_definition *definition;
	struct damage_effect_definition *effect;
	struct ragdoll_damage_note *note;
	struct ragdoll *ragdoll;
	short index;

	if (!damage || damage->definition_index == NONE || !ragdolls_enabled())
		return;
	object = object_try_and_get(object_index);
	if (!object || object->object.type != _object_type_biped)
		return;
	definition = object_definition_get(object->definition_index);
	effect = damage_effect_definition_get(damage->definition_index);
	if (!definition || !effect)
		return;
	note = &ragdoll_notes[ragdoll_next_note];
	ragdoll_next_note = (short)((ragdoll_next_note + 1) % RAGDOLL_DAMAGE_NOTES);
	note->object_index = object_index;
	note->tick = ragdoll_ticks;
	/* (a melee blow first: the weapons' melee effects have a small radius
	of their own, but are not explosions) */
	note->melee = effect->damage.category == RAGDOLL_DAMAGE_CATEGORY_MELEE;
	note->explosion = !note->melee && effect->cutoff_radius > 0.f;
	note->origin = damage->origin;
	note->direction = damage->direction;
	if (ragdoll_normalize(&note->direction) < 1e-5f)
	{
		note->direction = *global_zero_vector3d;
		note->direction.k = 1.f;
	}
	/* (object_damage_aftermath's acceleration, world units a tick; an
	explosion's by how near) */
	note->speed = MAX(0.f, effect->damage.instantaneous_acceleration) * definition->object.acceleration_scale /
		TICKS_PER_SECOND;
	if (note->explosion)
		note->speed *= PIN(damage->scale, 0.f, 1.f);
	if (!(note->speed >= 0.f) || !ragdoll_finite(&note->origin))
	{
		note->object_index = NONE;
		return;
	}
	/* (a ragdoll struck: thrown again, awake) */
	ragdoll = ragdoll_of_object(object_index);
	if (ragdoll && ragdoll->jolt != NONE && (note->explosion || note->melee || note->speed > 0.f))
	{
		real_point3d before[RAGDOLL_MAXIMUM_PARTICLES];
		real_vector3d direction = note->direction;
		real speed = MIN(note->speed, RAGDOLL_LAUNCH_SPEED);

		/* (its bodies still, what the blow gives them their velocities:
		ragdoll_jolt_kick) */
		for (index = 0; index < ragdoll->particle_count; index++)
			before[index] = ragdoll->particles[index].previous = ragdoll->particles[index].position;
		if (ragdoll->state == _ragdoll_asleep)
		{
			ragdoll->state = _ragdoll_awake;
			ragdoll->still_ticks = 0;
			ragdoll->awake_ticks = 0;
			ragdoll->woken = ++ragdoll_sequence;
			ragdoll_stats.woken++;
			if (ragdolls_debug())
				platform_log("ragdolls: %08lx (%s) woken by a blow", (unsigned long)object_index,
					ragdoll_object_name(object_index));
		}
		ragdoll_apply_note(ragdoll, note, FALSE);
		/* (the whole body thrown as object_damage_aftermath throws it - a
		little upward -, no faster than a body's launch) */
		direction.k += 0.45f;
		if (speed > 0.f && ragdoll_normalize(&direction) > 1e-5f)
		{
			for (index = 0; index < ragdoll->particle_count; index++)
			{
				ragdoll->particles[index].previous.x -= direction.i * speed / RAGDOLL_SUBSTEPS;
				ragdoll->particles[index].previous.y -= direction.j * speed / RAGDOLL_SUBSTEPS;
				ragdoll->particles[index].previous.z -= direction.k * speed / RAGDOLL_SUBSTEPS;
			}
		}
		ragdoll_jolt_kick(ragdoll, before);
	}
}

void ragdolls_tick(
	void)
{
	unsigned int started, stepped = 0;
	double elapsed, starts_ms = 0.0, step_ms = 0.0;
	short awake = 0, kept = 0, woken_awake;
	short slot, index, starts;

	ragdoll_ticks++;
	if (!ragdolls_enabled())
	{
		/* (set off: every body as the game lays it again, and no world) */
		for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
			ragdoll_release(slot, _ragdoll_released_setting);
		ragdoll_pending_count = 0;
		if (ragdoll_world.built)
			ragdoll_world_release();
		return;
	}
	/* (the world with the level in it made as the level starts, not at the
	first death; a dedicated server's game none: it draws nothing) */
	if (!ragdolls_dedicated() && global_scenario_get() && global_scenario_get()->type != _scenario_type_main_menu)
		ragdoll_world_ready();
	/* (nothing to do) */
	for (slot = 0; slot < RAGDOLL_SLOTS && ragdolls[slot].state == _ragdoll_free; slot++)
		;
	if (slot == RAGDOLL_SLOTS && !ragdoll_pending_count)
		return;
	started = ragdoll_now();

	/* (the bodies that died, in the order they died: each started once on
	its own, out of a seat, with room among the awake - made by putting to
	sleep one that has fallen a while, the stillest -, else it waits a
	while, its death animation playing, and is started from where that has
	it then) */
	for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
		awake += ragdolls[slot].state == _ragdoll_awake;
	for (index = 0, starts = 0; index < ragdoll_pending_count;)
	{
		struct ragdoll_pending *pending = &ragdoll_pending[index];
		struct object_datum *object = object_try_and_get(pending->object_index);
		struct unit_datum const *unit = (struct unit_datum const *)object;
		boolean done = TRUE;

		if (object && object->object.type == _object_type_biped &&
			TEST_FLAG(object->object.damage_flags, _object_dead_bit) && unit->unit.feign_death_timer <= 0)
		{
			short room = awake < RAGDOLL_ACTIVE ? NONE : ragdoll_room();

			/* (two a tick at most: a grenade's four deaths at once start
			over two ticks) */
			if (starts >= RAGDOLL_STARTS_PER_TICK)
				done = FALSE;
			else if (object->object.parent_object_index == NONE && (awake < RAGDOLL_ACTIVE || room != NONE))
			{
				starts++;
				if (room != NONE)
				{
					ragdoll_sleep(&ragdolls[room], "made room");
					awake--;
				}
				awake += ragdoll_start(pending->object_index, object);
			}
			else if (ragdoll_ticks - pending->tick < RAGDOLL_PENDING_TICKS)
				done = FALSE;
		}
		if (done)
		{
			memmove(pending, pending + 1, sizeof(*pending) * (ragdoll_pending_count - index - 1));
			ragdoll_pending_count--;
		}
		else
			index++;
	}

	/* (what moves in the world where the game has it, then Jolt's step:
	two collision steps a tick, the joints and contacts solved at 60 a
	second) */
	starts_ms = ragdoll_ms_since(started);
	if (awake && physics_jolt_exists())
	{
		ragdoll_world_update();
		stepped = ragdoll_now();
		physics_jolt_step(1.f / TICKS_PER_SECOND, 2);
		step_ms = ragdoll_ms_since(stepped);
	}

	/* (a ragdoll asleep that something moving struck is woken by Jolt: kept
	awake only with room among the awake) */
	woken_awake = awake;
	awake = 0;
	for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
	{
		struct ragdoll *ragdoll = &ragdolls[slot];
		struct object_datum *object;
		short reason = _ragdoll_released_gone;

		if (ragdoll->state == _ragdoll_free)
			continue;
		object = ragdoll_body(ragdoll, &reason);
		if (!object)
		{
			ragdoll_release(slot, reason);
			continue;
		}
		/* (asleep, its body moved far by the game - pushed by a vehicle, put
		elsewhere by a script -: drawn as the game has it again; not while
		the game's body is still flying from the death) */
		if (ragdoll->state == _ragdoll_asleep && ragdoll_ticks - ragdoll->started_tick < RAGDOLL_FLIGHT_TICKS)
			ragdoll->body = object->object.position;
		else if (ragdoll->state == _ragdoll_asleep &&
			ragdoll_distance(&ragdoll->body, &object->object.position) > RAGDOLL_BODY_MOVED)
		{
			ragdoll_release(slot, _ragdoll_released_body_moved);
			continue;
		}
		kept++;
		if (ragdoll->state == _ragdoll_asleep && ragdoll->jolt != NONE &&
			ragdoll->jolt_generation == physics_jolt_generation() && physics_jolt_rig_active(ragdoll->jolt))
		{
			if (woken_awake >= RAGDOLL_ACTIVE)
				physics_jolt_rig_set_active(ragdoll->jolt, FALSE);
			else
			{
				ragdoll->state = _ragdoll_awake;
				ragdoll->still_ticks = 0;
				ragdoll->awake_ticks = 0;
				ragdoll->woken = ++ragdoll_sequence;
				ragdoll_stats.woken++;
				woken_awake++;
				if (ragdolls_debug())
					platform_log("ragdolls: %08lx woken by something moving", (unsigned long)ragdoll->object_index);
			}
		}
		if (ragdoll->state != _ragdoll_awake)
			continue;
		awake++;
		ragdoll_stats.steps++;
		if (!ragdoll_step(ragdoll, object, &reason))
		{
			ragdoll_release(slot, reason);
			kept--;
		}
	}
	ragdoll_stats.most_awake = MAX(ragdoll_stats.most_awake, awake);
	ragdoll_stats.most_kept = MAX(ragdoll_stats.most_kept, kept);
	elapsed = ragdoll_ms_since(started);
	if (ragdolls_debug() && elapsed > 5.0)
	{
		platform_log("ragdolls: a slow tick, %.2f ms: %.2f ms the starts, %.2f ms Jolt's step, %d awake", elapsed,
			starts_ms, step_ms, (int)awake);
	}
	if (awake)
	{
		ragdoll_stats.busy_ticks++;
		ragdoll_stats.busy_ms += elapsed;
		ragdoll_stats.most_ms = MAX(ragdoll_stats.most_ms, elapsed);
		ragdoll_stats.window_ticks++;
		ragdoll_stats.window_ms += elapsed;
		ragdoll_stats.window_most_ms = MAX(ragdoll_stats.window_most_ms, elapsed);
	}
	/* (debug.ragdolls: the cost, every ten seconds while any is kept) */
	if (ragdolls_debug() && ragdoll_ticks % (10 * TICKS_PER_SECOND) == 0 && (kept || ragdoll_stats.window_ticks))
	{
		platform_log("ragdolls: %d kept, %d awake; %lu ticks busy in 10 s, %.3f ms a tick on them, at most %.3f", (int)kept,
			(int)awake, ragdoll_stats.window_ticks, ragdoll_stats.window_ticks ?
			ragdoll_stats.window_ms / ragdoll_stats.window_ticks : 0.0, ragdoll_stats.window_most_ms);
		ragdoll_stats.window_ticks = 0;
		ragdoll_stats.window_ms = ragdoll_stats.window_most_ms = 0.0;
	}
}

/* ---------- public code: the render's side */

boolean ragdolls_pose(
	long object_index,
	real_matrix4x3 *matrices,
	short node_count)
{
	struct ragdoll const *ragdoll = ragdoll_of_object(object_index);

	if (!ragdoll || ragdoll->node_count != node_count)
		return FALSE;
	memcpy(matrices, ragdoll->pose, node_count * sizeof(real_matrix4x3));
	return TRUE;
}

real_matrix4x3 *ragdolls_drawn_pose(
	long object_index)
{
	struct ragdoll *ragdoll = ragdoll_of_object(object_index);

	if (!ragdoll)
		return NULL;
	ragdoll->drawn_frame = ragdoll_frames;
	return ragdoll->pose;
}

void ragdolls_frame_begin(
	void)
{
	short slot;

	ragdoll_frames++;
	ragdoll_swapped.count = 0;
	for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
	{
		struct ragdoll const *ragdoll = &ragdolls[slot];
		struct object_datum *object;
		short count = ragdoll_swapped.count;

		if (ragdoll->state == _ragdoll_free || ragdoll->generation != ragdoll_generation ||
			!(object = object_try_and_get(ragdoll->object_index)))
		{
			continue;
		}
		ragdoll_swapped.objects[count] = ragdoll->object_index;
		ragdoll_swapped.centers[count] = object->object.bounding_sphere_center;
		ragdoll_swapped.radii[count] = object->object.bounding_sphere_radius;
		object->object.bounding_sphere_center = ragdoll->center;
		object->object.bounding_sphere_radius = ragdoll->radius;
		ragdoll_swapped.count++;
	}
}

void ragdolls_frame_end(
	void)
{
	short index;

	for (index = 0; index < ragdoll_swapped.count; index++)
	{
		struct object_datum *object = object_try_and_get(ragdoll_swapped.objects[index]);

		if (object)
		{
			object->object.bounding_sphere_center = ragdoll_swapped.centers[index];
			object->object.bounding_sphere_radius = ragdoll_swapped.radii[index];
		}
	}
	ragdoll_swapped.count = 0;
}

short ragdolls_find_rendered(
	long *result,
	short maximum,
	boolean (*unmarked)(long object_index),
	boolean (*mark)(long object_index))
{
	short slot, found = 0;

	for (slot = 0; slot < RAGDOLL_SLOTS && found < maximum; slot++)
	{
		struct ragdoll const *ragdoll = &ragdolls[slot];
		short rendered_index;

		if (ragdoll->state == _ragdoll_free || ragdoll->generation != ragdoll_generation ||
			ragdoll->drawn_cluster == NONE || !object_try_and_get(ragdoll->object_index) || !unmarked(ragdoll->object_index))
		{
			continue;
		}
		for (rendered_index = 0; rendered_index < render.rendered_cluster_count; rendered_index++)
		{
			struct rendered_cluster *rendered = rendered_cluster_get(rendered_index);

			if (render.cluster_index != NONE && rendered->cluster_index != ragdoll->drawn_cluster)
				continue;
			if (render.cluster_index == NONE ||
				render_frustum_sphere_visible(&rendered->frustum, &ragdoll->center, ragdoll->radius))
			{
				result[found++] = ragdoll->object_index;
				mark(ragdoll->object_index);
			}
			break;
		}
	}
	return found;
}

/* ---------- the console */

/* the living characters nearest the first local player's unit within range,
whose tag's name holds filter ("" any; "players": other players' units, else
none of them): at most count, nearest first */
static short ragdoll_test_targets(
	long *targets,
	short count,
	real range,
	char const *filter,
	long *player_index_reference)
{
	long player_index = local_player_get_player_index(0);
	struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;
	struct object_datum *unit = player && player->unit_index != NONE ? object_try_and_get(player->unit_index) : NULL;
	long candidates[64];
	real distances[64];
	struct object_iterator iterator;
	struct object_datum *object;
	short candidate_count = 0;
	short found;

	*player_index_reference = player_index;
	if (!unit || count <= 0)
		return 0;
	object_iterator_new(&iterator, _object_mask_biped, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL &&
		candidate_count < (short)NUMBEROF(candidates))
	{
		struct unit_datum const *other = (struct unit_datum const *)object;
		real distance = ragdoll_distance(&object->object.position, &unit->object.position);

		boolean players = !strcmp(filter, "players");

		if (iterator.index == player->unit_index || (other->unit.player_index != NONE) != players ||
			(!players && filter[0] && !strstr(tag_get_name(object->definition_index), filter)) ||
			TEST_FLAG(object->object.damage_flags, _object_dead_bit) || object->object.parent_object_index != NONE ||
			distance > range)
		{
			continue;
		}
		candidates[candidate_count] = iterator.index;
		distances[candidate_count] = distance;
		candidate_count++;
	}
	for (found = 0; found < count && found < candidate_count; found++)
	{
		short nearest = found, index;

		for (index = (short)(found + 1); index < candidate_count; index++)
		{
			if (distances[index] < distances[nearest])
				nearest = index;
		}
		targets[found] = candidates[nearest];
		candidates[nearest] = candidates[found];
		distances[nearest] = distances[found];
	}
	return found;
}

/* a damage effect: one whose name holds the words, else the first that is
what is wanted (a melee blow, a bullet, an explosion) */
static long ragdoll_test_effect(
	char const *kind,
	char const *words)
{
	struct tag_iterator tags;
	long tag_index, effect = NONE;

	tag_iterator_new(&tags, DAMAGE_EFFECT_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&tags)) != NONE)
	{
		struct damage_effect_definition const *definition = damage_effect_definition_get(tag_index);
		char const *name = tag_get_name(tag_index);
		boolean melee = definition->damage.category == RAGDOLL_DAMAGE_CATEGORY_MELEE;
		boolean explosion = !melee && definition->cutoff_radius > 0.f;
		boolean wanted = !strcmp(kind, "punch") ? melee : !strcmp(kind, "boom") ? explosion :
			!melee && !explosion && strstr(name, "bullet") != NULL;

		if (!wanted)
			continue;
		if (strstr(name, words))
			return tag_index;
		if (effect == NONE)
			effect = tag_index;
	}
	return effect;
}

static void ragdoll_test_stats(
	void)
{
	struct physics_jolt_stats jolt;
	short slot, kept = 0, awake = 0;
	unsigned long released = 0;

	for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
	{
		if (!ragdoll_of_object(ragdolls[slot].object_index))
			continue;
		kept++;
		awake += ragdolls[slot].state == _ragdoll_awake;
	}
	for (slot = 0; slot < NUMBER_OF_RAGDOLL_RELEASE_REASONS; slot++)
		released += ragdoll_stats.released[slot];
	physics_jolt_get_stats(&jolt);
	platform_log("ragdolls stats: %d kept, %d awake (most %d kept, %d awake); %lu started, %lu not made, %lu asleep, "
		"%lu woken, %lu let go (%lu replaced, %lu game state replaced, %lu gone, %lu alive, %lu setting); %lu busy "
		"ticks, %.3f ms a tick on them, at most %.3f; %lu steps, %.3f ms a start; Jolt: %d bodies, %d awake, %u steps "
		"in all, %.3f ms a step, %.2f MB (at most %.2f)", (int)kept, (int)awake, (int)ragdoll_stats.most_kept,
		(int)ragdoll_stats.most_awake, ragdoll_stats.started, ragdoll_stats.rejected, ragdoll_stats.slept,
		ragdoll_stats.woken, released, ragdoll_stats.released[_ragdoll_released_replaced],
		ragdoll_stats.released[_ragdoll_released_map], ragdoll_stats.released[_ragdoll_released_gone],
		ragdoll_stats.released[_ragdoll_released_alive], ragdoll_stats.released[_ragdoll_released_setting],
		ragdoll_stats.busy_ticks, ragdoll_stats.busy_ticks ? ragdoll_stats.busy_ms / ragdoll_stats.busy_ticks : 0.0,
		ragdoll_stats.most_ms, ragdoll_stats.steps, ragdoll_stats.started ? ragdoll_stats.start_ms / ragdoll_stats.started : 0.0,
		jolt.bodies, jolt.active_bodies, jolt.steps, jolt.steps ? (double)jolt.step_ms_total / jolt.steps : 0.0,
		(double)jolt.bytes_now / 1048576.0, (double)jolt.bytes_peak / 1048576.0);
}

/* (the tests) a grenade's blast beside, a shot or a melee blow from the
player at, or a plain kill of each target */
static void ragdoll_test_hit(
	char const *word,
	short count,
	real range,
	char const *filter)
{
	long targets[RAGDOLL_SLOTS * 2];
	long player_index;
	short found = ragdoll_test_targets(targets, (short)PIN(count, 0, (int)NUMBEROF(targets)), range, filter,
		&player_index);
	struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;
	struct object_datum *attacker = player && player->unit_index != NONE ? object_try_and_get(player->unit_index) : NULL;
	long effect = NONE;
	short index;

	if (!strcmp(word, "punch"))
		effect = ragdoll_test_effect(word, "melee");
	else if (!strcmp(word, "shoot"))
		effect = ragdoll_test_effect(word, "assault rifle");
	else if (!strcmp(word, "boom"))
		effect = ragdoll_test_effect(word, "frag grenade\\explosion");
	for (index = 0; index < found; index++)
	{
		struct object_datum *target = object_try_and_get(targets[index]);
		struct damage_data damage;

		if (!target)
			continue;
		if (effect == NONE)
		{
			platform_log("ragdolls test: %08lx (%s) killed", (unsigned long)targets[index], ragdoll_object_name(targets[index]));
			damage_kill_object_for_player(targets[index], player_index);
			continue;
		}
		damage_data_new(&damage, effect);
		damage.scale = 1.f;
		if (player)
		{
			damage.owner_player_index = player_index;
			damage.owner_object_index = player->unit_index;
			damage.owner_team_index = (short)player->team_index;
		}
		if (!strcmp(word, "boom"))
		{
			/* (at its feet, a little beside it) */
			damage.origin = target->object.position;
			damage.origin.x += 0.15f;
			damage.origin.y += 0.1f;
			damage.origin.z += 0.05f;
			damage.epicenter = damage.origin;
			scenario_location_from_point(&damage.location, &damage.origin);
			platform_log("ragdolls test: %s beside %08lx (%s)", tag_get_name(effect), (unsigned long)targets[index],
				ragdoll_object_name(targets[index]));
			area_of_effect_cause_damage(&damage, NONE);
			continue;
		}
		/* (at its chest, from the player: lethal) */
		damage.origin = target->object.position;
		damage.origin.z += 0.2f;
		damage.epicenter = damage.origin;
		if (attacker)
		{
			damage.direction.i = target->object.position.x - attacker->object.position.x;
			damage.direction.j = target->object.position.y - attacker->object.position.y;
			damage.direction.k = 0.f;
		}
		else
			damage.direction = *global_forward3d;
		ragdoll_normalize(&damage.direction);
		scenario_location_from_point(&damage.location, &damage.origin);
		damage.multiplier = 100.f;
		platform_log("ragdolls test: %s %s %08lx (%s)", tag_get_name(effect), !strcmp(word, "punch") ? "punches" : "shoots",
			(unsigned long)targets[index], ragdoll_object_name(targets[index]));
		object_cause_damage(&damage, targets[index], NONE, NONE, NONE, NULL);
	}
	platform_log("ragdolls test: %s, %d of %d within %.1f", word, (int)found, (int)count, (double)range);
}

/* (the tests) the debug camera distance from the last ragdoll's middle, yaw
degrees about it from the way its body faced at the death (0 in front),
pitch up */
static void ragdoll_test_camera(
	real distance,
	real yaw,
	real pitch)
{
	struct ragdoll const *last = NULL;
	struct object_datum *object;
	real facing, up;
	real_point3d position;
	real_vector3d forward;
	short slot;

	for (slot = 0; slot < RAGDOLL_SLOTS; slot++)
	{
		if (ragdoll_of_object(ragdolls[slot].object_index) && (!last || ragdolls[slot].started > last->started))
			last = &ragdolls[slot];
	}
	if (!last || !(object = object_try_and_get(last->object_index)) || local_player_get_next(NONE) == NONE)
	{
		platform_log("ragdolls cam: no ragdoll");
		return;
	}
	facing = (real)atan2(object->object.forward.j, object->object.forward.i) + RAGDOLL_DEGREES(yaw);
	up = RAGDOLL_DEGREES(pitch);
	position.x = last->center.x + distance * (real)cos(facing) * (real)cos(up);
	position.y = last->center.y + distance * (real)sin(facing) * (real)cos(up);
	position.z = last->center.z + distance * (real)sin(up);
	ragdoll_between(&position, &last->center, &forward);
	ragdoll_normalize(&forward);
	director_set_flying_camera(local_player_get_next(NONE), &position, &forward);
	platform_log("ragdolls cam: %08lx (%s) from %.2f %.2f %.2f", (unsigned long)last->object_index,
		ragdoll_object_name(last->object_index), (double)position.x, (double)position.y, (double)position.z);
}

boolean ragdolls_command(
	char const *arguments)
{
	char word[16] = "", filter[32] = "";
	float a = 3.f, b = 10.f, c = 0.f;

	if (!arguments)
		return FALSE;
	sscanf(arguments, " %15s %f %f %f", word, &a, &b, &c);
	sscanf(arguments, " %*s %*f %*f %31s", filter);
	if (!strcmp(word, "stats"))
	{
		ragdoll_test_stats();
		return TRUE;
	}
	/* (the tests: debug.ragdolls, in a game this machine runs, not one it
	joined) */
	if (!ragdolls_debug() || game_connection() == _game_connection_network_client)
	{
		platform_log("ragdolls: \"%s\": the tests need debug.ragdolls and a game this machine runs", arguments);
		return FALSE;
	}
	if (!strcmp(word, "boom") || !strcmp(word, "shoot") || !strcmp(word, "punch") || !strcmp(word, "kill"))
	{
		ragdoll_test_hit(word, (short)a, b, filter);
		return TRUE;
	}
	if (!strcmp(word, "cam"))
	{
		ragdoll_test_camera(a, b, c);
		return TRUE;
	}
#ifdef HALO_CAPTURE
	/* (a screenshot, as the screenshot key takes one: capture.c) */
	if (!strcmp(word, "shot"))
	{
		void capture_request_screenshot(void);

		capture_request_screenshot();
		return TRUE;
	}
#endif
	platform_log("ragdolls: unknown \"%s\" (stats, boom, shoot, punch, kill, cam, shot)", arguments);
	return FALSE;
}

#endif // HALO_PORT_RAGDOLLS
