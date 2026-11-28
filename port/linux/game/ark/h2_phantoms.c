/*
H2_PHANTOMS.C

port: Halo 2's phantoms - the gravity lifts and the man cannons. A Halo 2
object's physics model (phmo) may give a shape a material whose phantom type
(phmo 0x10) makes it a phantom: no collision, a volume that pushes what is
in it. A phantom type names a marker of the object's render model whose
forward is its direction (direction_up, direction_out) and, all in world
units a second (and a second squared), how it pushes:
- along the direction: velocity along it brought up to "direction max vel"
  at "direction acc";
- to the axis (the line through the marker along the direction): velocity
  toward it up to "axis max vel" at "axis acc" (a rider held in the column);
- to the centre (the marker): "center acc" / "center max vel";
- its flags: whom it leaves alone (players 3, non-players 4, bipeds 5,
  vehicles 6, weapons 7, equipment 8, garbage 9, dead bipeds 27) and
  "negates gravity" (2: the game's gravity given back while in it).
Coagulation's base (powerhaus): a column up the back (air_up: up 7 to 5
wu/s, held to its axis 6 to 3) and at its top one out over the roof
(air_out: out 5 to 3, held 9 to 7, vehicles too).

Halo 2's own is Havok's phantom finding who overlaps the shape and Bungie's
code accelerating them; here each tick (the machine that runs the game: the
host, or a game played alone) every unit, vehicle and loose item whose middle
is in a phantom of a level object is accelerated so. Gravity is the game's
as ever: the rider rises at direction acc less gravity.

The phantoms are read from the level's Halo 2 map when it loads (the
scenario's scenery, crates and machines palettes); the objects are those the
level builder named h2\objects\<the Halo 2 tag's name>, and where the level
has none (an object of no model drawn, which the builder leaves out: most of
the MP maps' lifts are such invisible crates) the scenario's placement of it
is a fixed volume. The shapes are pills, boxes and polyhedra (convex, their
planes: Lockout's, Warlock's and Desolation's lifts, Relic's tower); a
biped is in one when its middle or its feet are.

HALO_H2_PHANTOM_LOG=1 logs the phantoms found and each object entering one;
=2 also every tick of an object in one. HALO_H2_PHANTOMS=0 turns them off.
*/

#include "cseries.h"
#include "real_math.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "units/units.h"
#include "units/bipeds.h"
#include "units/biped_definitions.h"
#include "game/game.h"
#include "tag_files/tag_groups.h"
#include "cache/cache_files.h"
#include "h2_cache.h"
#include "h2_level.h"
#include "h2_phantoms.h"
#include "native_switch.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);
char const *cache_files_map_directory(void);
float sqrtf(float x);
float fabsf(float x);
/* physics.c: world units a tick, each tick */
extern real global_gravity;

/* ---------- constants */

enum
{
	PHANTOM_KINDS_MAXIMUM = 16,
	PHANTOM_TYPES_MAXIMUM = 8,
	PHANTOM_SHAPES_MAXIMUM = 8,
	/* (a kind's polyhedra's planes, all of them: a phantom's are among
	every polyhedron's, in turn) */
	PHANTOM_PLANES_MAXIMUM = 256,
	PHANTOM_OBJECTS_MAXIMUM = 32,
	/* the scenario's placements of the kinds (those the level has no
	object for kept: static_frames) */
	PHANTOM_PLACEMENTS_MAXIMUM = 32,
	/* who is in one, remembered for the log */
	PHANTOM_RIDERS_MAXIMUM = 64
};

/* (Halo 2's layouts, MCC's maps: an object's model (hlmt) 0x34; the model's
render model 0x00 and physics model 0x20) */
#define H2_OBJECT_MODEL 0x34
#define H2_MODEL_RENDER_MODEL 0x00
#define H2_MODEL_PHYSICS_MODEL 0x20

/* (a physics model's phantom types at 0x28 (0x68 each: flags, the sizes
at 4, the marker's name at 8, the alignment marker's at 0x0C, hooke's law
0x18, dead radius 0x1C, centre acceleration and most 0x20/0x24, axis
0x28/0x2C, direction 0x30/0x34), materials at 0x40 (0x0C each: name, the
global material's, the phantom type 8), spheres 0x48, pills 0x58 (0x60
each: name, material 4, bottom 0x40, radius 0x4C, top 0x50), boxes 0x60;
polyhedra 0x70 (0x120 each: name, material 4, the
box around it - half extents 0x40, middle 0x50 -, its planes' count 0x110)
and their planes 0x80 (0x10 each: i j k d, inside where i j k . p + d is
not above 0; each polyhedron's in turn); a render model's marker groups at 0x58 (0x0C each: name, markers at 4 -
0x24 each: region, permutation, node, translation 4, rotation 0x10)) */
#define H2_PHMO_PHANTOM_TYPES 0x28
#define H2_PHMO_PHANTOM_TYPE_SIZE 0x68
#define H2_PHMO_MATERIALS 0x40
#define H2_PHMO_MATERIAL_SIZE 0x0C
#define H2_PHMO_SPHERES 0x48
#define H2_PHMO_PILLS 0x58
#define H2_PHMO_PILL_SIZE 0x60
#define H2_PHMO_BOXES 0x60
#define H2_PHMO_BOX_SIZE 0xB0
#define H2_PHMO_POLYHEDRA 0x70
#define H2_PHMO_POLYHEDRON_SIZE 0x120
#define H2_PHMO_POLYHEDRON_PLANES 0x80
#define H2_RENDER_MODEL_MARKER_GROUPS 0x58
/* (a phantom type's flags: what it leaves alone, and gravity) */
enum
{
	_h2_phantom_negates_gravity_bit = 2,
	_h2_phantom_ignores_players_bit = 3,
	_h2_phantom_ignores_nonplayers_bit = 4,
	_h2_phantom_ignores_bipeds_bit = 5,
	_h2_phantom_ignores_vehicles_bit = 6,
	_h2_phantom_ignores_weapons_bit = 7,
	_h2_phantom_ignores_equipment_bit = 8,
	_h2_phantom_ignores_garbage_bit = 9,
	_h2_phantom_ignores_dead_bipeds_bit = 27
};

/* (the scenario's palettes, 0x28 each: scenery, crates, machines; and
their placements: the block, the size of one) */
static unsigned long const h2_scenario_palettes[] = { 0x58, 0x330, 0xB0 };
static unsigned long const h2_scenario_placements[][2] = { { 0x50, 0x5C }, { 0x328, 0x4C }, { 0xA8, 0x48 } };

/* ---------- structures */

struct phantom_type
{
	unsigned long flags;
	char marker[32];
	real hookes_law;
	real dead_radius;
	real center_acceleration, center_maximum;
	real axis_acceleration, axis_maximum;
	real direction_acceleration, direction_maximum;
	/* the marker in the object's space: its place, its forward */
	real_point3d marker_position;
	real_vector3d marker_forward;
	boolean marker_found;
};

/* a phantom shape, in the object's space: a pill (a capsule, bottom to
top, radius), a box (its middle, its axes, its half extents) or a
polyhedron (its planes, the kind's from plane_first: Lockout's, Warlock's,
Desolation's and Colossus's lifts, Relic's tower) */
struct phantom_shape
{
	boolean box;
	boolean polyhedron;
	short plane_first, plane_count;
	real_point3d bottom, top;
	real radius;
	real_point3d middle;
	real_vector3d axes[3];
	real half_extents[3];
	short type;
};

struct phantom_kind
{
	char name[64];
	short type_count;
	short shape_count;
	struct phantom_type types[PHANTOM_TYPES_MAXIMUM];
	struct phantom_shape shapes[PHANTOM_SHAPES_MAXIMUM];
	short plane_count;
	real planes[PHANTOM_PLANES_MAXIMUM][4];
};

/* where a phantom's object is: its place, its axes and scale, and the
sphere around it (a level object's, or a placement's the level has no
object for) */
struct phantom_frame
{
	real_point3d position;
	real_vector3d forward, up;
	real scale;
	real_point3d middle;
	real radius;
};

static struct
{
	boolean active;
	short kind_count;
	struct phantom_kind kinds[PHANTOM_KINDS_MAXIMUM];
	/* the level's objects of the kinds (found as the game starts) */
	short object_count;
	long objects[PHANTOM_OBJECTS_MAXIMUM];
	short object_kinds[PHANTOM_OBJECTS_MAXIMUM];
	long objects_found_at;
	/* the scenario's placements of the kinds; those the level has no
	object of the kind at (found with the objects) pushed as fixed volumes:
	a crate or machine of no model drawn the level builder leaves out -
	Warlock's, Foundation's, Tombstone's and Backwash's lifts, Coagulation's
	powerhaus, Gemini's jump pad, Elongation's cushions (h2_level.c's
	object_for) */
	short placement_count;
	short placement_kinds[PHANTOM_PLACEMENTS_MAXIMUM];
	struct phantom_frame placements[PHANTOM_PLACEMENTS_MAXIMUM];
	boolean placement_static[PHANTOM_PLACEMENTS_MAXIMUM];
	/* (the log) who was in one last tick */
	short rider_count;
	long riders[PHANTOM_RIDERS_MAXIMUM];
} h2_phantoms;

/* ---------- private code */

static int h2_phantoms_log_level(void)
{
	static int level = -1;

	if (level < 0)
		level = getenv("HALO_H2_PHANTOM_LOG") ? atoi(getenv("HALO_H2_PHANTOM_LOG")) : 0;
	return level;
}

static real h2_phantoms_read_real(struct h2_cache const *cache, unsigned long address)
{
	unsigned long bits = h2_cache_read32(cache, address);
	real value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static short h2_phantoms_read_short(struct h2_cache const *cache, unsigned long address)
{
	unsigned char const *p = h2_cache_at(cache, address, 2);

	return p ? (short)(p[0] | (p[1] << 8)) : NONE;
}

static void h2_phantoms_dump(struct h2_cache const *cache, unsigned long address, unsigned long size, char const *what)
{
	unsigned long o;

	for (o = 0; o < size; o += 16)
	{
		platform_log("h2 phantoms: %s +%03lX: %08lX %08lX %08lX %08lX", what, o, h2_cache_read32(cache, address + o),
			h2_cache_read32(cache, address + o + 4), h2_cache_read32(cache, address + o + 8),
			h2_cache_read32(cache, address + o + 12));
	}
}

static void h2_phantoms_read_point(struct h2_cache const *cache, unsigned long address, real_point3d *point)
{
	point->x = h2_phantoms_read_real(cache, address);
	point->y = h2_phantoms_read_real(cache, address + 4);
	point->z = h2_phantoms_read_real(cache, address + 8);
}

/* a render model's marker by name: its place and its forward (the marker's
rotation, a quaternion i j k w, turning +x), node 0's space */
static boolean h2_phantoms_marker(struct h2_cache *cache, unsigned long render_model, char const *name,
	real_point3d *position, real_vector3d *forward)
{
	unsigned long groups, markers;
	long group_count = 0, marker_count = 0, g;

	if (!h2_cache_block(cache, render_model + H2_RENDER_MODEL_MARKER_GROUPS, 0x0C, &group_count, &groups))
		return FALSE;
	for (g = 0; g < group_count; g++)
	{
		char text[64] = "";

		if (!h2_cache_string(cache, h2_cache_read32(cache, groups + 0x0C * g), text, sizeof(text)) || strcmp(text, name) ||
			!h2_cache_block(cache, groups + 0x0C * g + 4, 0x24, &marker_count, &markers) || marker_count < 1)
		{
			continue;
		}
		{
			real x = h2_phantoms_read_real(cache, markers + 0x10), y = h2_phantoms_read_real(cache, markers + 0x14);
			real z = h2_phantoms_read_real(cache, markers + 0x18), w = h2_phantoms_read_real(cache, markers + 0x1C);

			h2_phantoms_read_point(cache, markers + 4, position);
			forward->i = 1.f - 2.f * (y * y + z * z);
			forward->j = 2.f * (x * y + w * z);
			forward->k = 2.f * (x * z - w * y);
			normalize3d(forward);
		}
		return TRUE;
	}
	return FALSE;
}

static short h2_phantoms_kind_read(struct h2_cache *cache, long tag)
{
	struct h2_cache_tag object, model, physics, render;
	char const *name = h2_cache_tag_name(cache, tag);
	char const *slash = name ? strrchr(name, '\\') : NULL;
	struct phantom_kind *kind;
	unsigned long types, materials, pills;
	long type_count = 0, material_count = 0, pill_count = 0, k;
	short kind_index;

	if (h2_phantoms.kind_count >= PHANTOM_KINDS_MAXIMUM ||
		!h2_cache_tag_get(cache, tag, &object) ||
		!h2_cache_tag_get(cache, h2_cache_reference(cache, object.address + H2_OBJECT_MODEL), &model) ||
		!h2_cache_tag_get(cache, h2_cache_reference(cache, model.address + H2_MODEL_PHYSICS_MODEL), &physics) ||
		!h2_cache_block(cache, physics.address + H2_PHMO_PHANTOM_TYPES, H2_PHMO_PHANTOM_TYPE_SIZE, &type_count, &types) ||
		type_count < 1)
	{
		return NONE;
	}
	for (kind_index = 0; kind_index < h2_phantoms.kind_count; kind_index++)
	{
		if (!strcmp(h2_phantoms.kinds[kind_index].name, slash ? slash + 1 : name))
			return kind_index;
	}
	kind = &h2_phantoms.kinds[h2_phantoms.kind_count];
	memset(kind, 0, sizeof(*kind));
	snprintf(kind->name, sizeof(kind->name), "%s", slash ? slash + 1 : name);
	if (h2_phantoms_log_level() >= 3)
	{
		h2_phantoms_dump(cache, types, (unsigned long)type_count * H2_PHMO_PHANTOM_TYPE_SIZE, "types");
		h2_phantoms_dump(cache, physics.address, 0x100, "phmo");
	}
	render.address = 0;
	h2_cache_tag_get(cache, h2_cache_reference(cache, model.address + H2_MODEL_RENDER_MODEL), &render);
	for (k = 0; k < type_count && k < PHANTOM_TYPES_MAXIMUM; k++)
	{
		unsigned long t = types + H2_PHMO_PHANTOM_TYPE_SIZE * k;
		struct phantom_type *type = &kind->types[k];

		type->flags = h2_cache_read32(cache, t);
		h2_cache_string(cache, h2_cache_read32(cache, t + 0x08), type->marker, sizeof(type->marker));
		type->hookes_law = h2_phantoms_read_real(cache, t + 0x18);
		type->dead_radius = h2_phantoms_read_real(cache, t + 0x1C);
		type->center_acceleration = h2_phantoms_read_real(cache, t + 0x20);
		type->center_maximum = h2_phantoms_read_real(cache, t + 0x24);
		type->axis_acceleration = h2_phantoms_read_real(cache, t + 0x28);
		type->axis_maximum = h2_phantoms_read_real(cache, t + 0x2C);
		type->direction_acceleration = h2_phantoms_read_real(cache, t + 0x30);
		type->direction_maximum = h2_phantoms_read_real(cache, t + 0x34);
		type->marker_forward = *global_up3d;
		type->marker_found = type->marker[0] && render.address &&
			h2_phantoms_marker(cache, render.address, type->marker, &type->marker_position, &type->marker_forward);
		kind->type_count++;
	}
	/* (the shapes whose material has a phantom type: pills) */
	h2_cache_block(cache, physics.address + H2_PHMO_MATERIALS, H2_PHMO_MATERIAL_SIZE, &material_count, &materials);
	if (h2_cache_block(cache, physics.address + H2_PHMO_PILLS, H2_PHMO_PILL_SIZE, &pill_count, &pills))
	{
		for (k = 0; k < pill_count && kind->shape_count < PHANTOM_SHAPES_MAXIMUM; k++)
		{
			unsigned long pill = pills + H2_PHMO_PILL_SIZE * k;
			short material = h2_phantoms_read_short(cache, pill + 4);
			short type = material >= 0 && material < material_count ?
				h2_phantoms_read_short(cache, materials + H2_PHMO_MATERIAL_SIZE * material + 8) : NONE;
			struct phantom_shape *shape = &kind->shapes[kind->shape_count];

			if (type < 0 || type >= kind->type_count)
				continue;
			shape->type = type;
			h2_phantoms_read_point(cache, pill + 0x40, &shape->bottom);
			h2_phantoms_read_point(cache, pill + 0x50, &shape->top);
			shape->radius = h2_phantoms_read_real(cache, pill + 0x4C);
			kind->shape_count++;
		}
	}
	/* (and boxes: their half extents 0x40, axes 0x70/0x80/0x90, middle 0xA0) */
	if (h2_cache_block(cache, physics.address + H2_PHMO_BOXES, H2_PHMO_BOX_SIZE, &pill_count, &pills))
	{
		for (k = 0; k < pill_count && kind->shape_count < PHANTOM_SHAPES_MAXIMUM; k++)
		{
			unsigned long box = pills + H2_PHMO_BOX_SIZE * k;
			short material = h2_phantoms_read_short(cache, box + 4);
			short type = material >= 0 && material < material_count ?
				h2_phantoms_read_short(cache, materials + H2_PHMO_MATERIAL_SIZE * material + 8) : NONE;
			struct phantom_shape *shape = &kind->shapes[kind->shape_count];
			short a;

			if (type < 0 || type >= kind->type_count)
				continue;
			memset(shape, 0, sizeof(*shape));
			shape->box = TRUE;
			shape->type = type;
			for (a = 0; a < 3; a++)
			{
				real_point3d axis;

				shape->half_extents[a] = h2_phantoms_read_real(cache, box + 0x40 + 4 * a);
				h2_phantoms_read_point(cache, box + 0x70 + 0x10 * a, &axis);
				shape->axes[a].i = axis.x;
				shape->axes[a].j = axis.y;
				shape->axes[a].k = axis.z;
			}
			h2_phantoms_read_point(cache, box + 0xA0, &shape->middle);
			kind->shape_count++;
		}
	}
	/* (and polyhedra: their planes, each polyhedron's after the one
	before's) */
	if (h2_cache_block(cache, physics.address + H2_PHMO_POLYHEDRA, H2_PHMO_POLYHEDRON_SIZE, &pill_count, &pills))
	{
		unsigned long planes;
		long plane_total = 0, plane_at = 0;

		h2_cache_block(cache, physics.address + H2_PHMO_POLYHEDRON_PLANES, 0x10, &plane_total, &planes);
		for (k = 0; k < pill_count; k++)
		{
			unsigned long polyhedron = pills + H2_PHMO_POLYHEDRON_SIZE * k;
			short material = h2_phantoms_read_short(cache, polyhedron + 4);
			short type = material >= 0 && material < material_count ?
				h2_phantoms_read_short(cache, materials + H2_PHMO_MATERIAL_SIZE * material + 8) : NONE;
			long plane_count = (long)h2_cache_read32(cache, polyhedron + 0x110);
			struct phantom_shape *shape = &kind->shapes[kind->shape_count];
			long n;

			if (plane_count < 0 || plane_at + plane_count > plane_total)
				break;
			plane_at += plane_count;
			if (type < 0 || type >= kind->type_count || kind->shape_count >= PHANTOM_SHAPES_MAXIMUM ||
				plane_count < 4 || kind->plane_count + plane_count > PHANTOM_PLANES_MAXIMUM)
			{
				if (type >= 0 && type < kind->type_count)
					platform_log("h2 phantoms: %s polyhedron %ld (%ld planes) left out", kind->name, k, plane_count);
				continue;
			}
			memset(shape, 0, sizeof(*shape));
			shape->polyhedron = TRUE;
			shape->type = type;
			h2_phantoms_read_point(cache, polyhedron + 0x50, &shape->middle);
			for (n = 0; n < 3; n++)
				shape->half_extents[n] = h2_phantoms_read_real(cache, polyhedron + 0x40 + 4 * n);
			shape->plane_first = kind->plane_count;
			shape->plane_count = (short)plane_count;
			for (n = 0; n < plane_count; n++)
			{
				short c;

				for (c = 0; c < 4; c++)
					kind->planes[kind->plane_count][c] = h2_phantoms_read_real(cache, planes + 0x10 * (plane_at - plane_count + n) + 4 * c);
				kind->plane_count++;
			}
			kind->shape_count++;
		}
	}
	{
		long sphere_count = 0, box_count = 0;
		unsigned long elements;

		/* (spheres are not read: logged, should a level's be one) */
		h2_cache_block(cache, physics.address + H2_PHMO_SPHERES, 1, &sphere_count, &elements);
		(void)box_count;
		if (sphere_count > 0)
			platform_log("h2 phantoms: %s has %ld spheres, not read as phantoms", kind->name, sphere_count);
		if (h2_phantoms_log_level() >= 3 && sphere_count > 0)
			h2_phantoms_dump(cache, h2_cache_read32(cache, physics.address + H2_PHMO_SPHERES + 4), 0x80, "spheres");
	}
	if (!kind->shape_count)
		return NONE;
	h2_phantoms.kind_count++;
	if (h2_phantoms_log_level())
	{
		for (k = 0; k < kind->type_count; k++)
		{
			struct phantom_type const *type = &kind->types[k];

			platform_log("h2 phantoms: %s type %ld: flags %lX, marker %s%s at %.2f %.2f %.2f forward %.2f %.2f %.2f; "
				"centre %.1f/%.1f, axis %.1f/%.1f, direction %.1f/%.1f (acceleration/most), hooke %.2f, dead %.2f",
				kind->name, k, type->flags, type->marker, type->marker_found ? "" : " (not found)",
				(double)type->marker_position.x, (double)type->marker_position.y, (double)type->marker_position.z,
				(double)type->marker_forward.i, (double)type->marker_forward.j, (double)type->marker_forward.k,
				(double)type->center_acceleration, (double)type->center_maximum, (double)type->axis_acceleration,
				(double)type->axis_maximum, (double)type->direction_acceleration, (double)type->direction_maximum,
				(double)type->hookes_law, (double)type->dead_radius);
		}
		for (k = 0; k < kind->shape_count; k++)
		{
			struct phantom_shape const *shape = &kind->shapes[k];

			if (shape->polyhedron)
			{
				platform_log("h2 phantoms: %s shape %ld (type %d): polyhedron of %d planes around %.2f %.2f %.2f, half %.2f %.2f %.2f",
					kind->name, k, (int)shape->type, (int)shape->plane_count, (double)shape->middle.x, (double)shape->middle.y,
					(double)shape->middle.z, (double)shape->half_extents[0], (double)shape->half_extents[1], (double)shape->half_extents[2]);
			}
			else if (shape->box)
			{
				platform_log("h2 phantoms: %s shape %ld (type %d): box at %.2f %.2f %.2f, half %.2f %.2f %.2f", kind->name, k,
					(int)shape->type, (double)shape->middle.x, (double)shape->middle.y, (double)shape->middle.z,
					(double)shape->half_extents[0], (double)shape->half_extents[1], (double)shape->half_extents[2]);
			}
			else
			{
				platform_log("h2 phantoms: %s shape %ld (type %d): pill %.2f %.2f %.2f to %.2f %.2f %.2f, radius %.2f", kind->name, k,
					(int)shape->type, (double)shape->bottom.x, (double)shape->bottom.y, (double)shape->bottom.z, (double)shape->top.x,
					(double)shape->top.y, (double)shape->top.z, (double)shape->radius);
			}
		}
	}
	return (short)(h2_phantoms.kind_count - 1);
}

/* how far from its object's origin a kind's shapes reach */
static real h2_phantoms_kind_reach(struct phantom_kind const *kind)
{
	real reach = 0.f;
	short s;

	for (s = 0; s < kind->shape_count; s++)
	{
		struct phantom_shape const *shape = &kind->shapes[s];
		real r;

		if (shape->box || shape->polyhedron)
		{
			r = sqrtf(shape->middle.x * shape->middle.x + shape->middle.y * shape->middle.y + shape->middle.z * shape->middle.z) +
				sqrtf(shape->half_extents[0] * shape->half_extents[0] + shape->half_extents[1] * shape->half_extents[1] +
				shape->half_extents[2] * shape->half_extents[2]);
		}
		else
		{
			r = shape->radius + MAX(sqrtf(shape->bottom.x * shape->bottom.x + shape->bottom.y * shape->bottom.y + shape->bottom.z * shape->bottom.z),
				sqrtf(shape->top.x * shape->top.x + shape->top.y * shape->top.y + shape->top.z * shape->top.z));
		}
		reach = MAX(reach, r);
	}
	return reach;
}

/* the level's objects of the phantoms' kinds (h2\objects\<kind>) */
static void h2_phantoms_objects_find(void)
{
	struct object_iterator iterator;
	struct object_datum *object;

	h2_phantoms.object_count = 0;
	object_iterator_new(&iterator, 0xFFFFFFFFUL, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL &&
		h2_phantoms.object_count < PHANTOM_OBJECTS_MAXIMUM)
	{
		char const *name = tag_get_name(object->definition_index);
		short k;

		if (!name || strncmp(name, "h2\\objects\\", 11))
			continue;
		for (k = 0; k < h2_phantoms.kind_count; k++)
		{
			if (!strcmp(name + 11, h2_phantoms.kinds[k].name))
			{
				h2_phantoms.objects[h2_phantoms.object_count] = iterator.index;
				h2_phantoms.object_kinds[h2_phantoms.object_count] = k;
				h2_phantoms.object_count++;
				if (h2_phantoms_log_level())
				{
					platform_log("h2 phantoms: %08lx %s at %.2f %.2f %.2f", (unsigned long)iterator.index, name,
						(double)object->object.position.x, (double)object->object.position.y, (double)object->object.position.z);
				}
				break;
			}
		}
	}
	/* (each placement the level has no object of its kind at: a fixed
	volume there) */
	{
		short p;

		for (p = 0; p < h2_phantoms.placement_count; p++)
		{
			boolean covered = FALSE;
			short o;

			for (o = 0; o < h2_phantoms.object_count && !covered; o++)
			{
				struct object_datum const *owner = object_try_and_get(h2_phantoms.objects[o]);

				covered = owner && h2_phantoms.object_kinds[o] == h2_phantoms.placement_kinds[p] &&
					distance3d(&owner->object.position, &h2_phantoms.placements[p].position) < 1.f;
			}
			if (h2_phantoms_log_level() && h2_phantoms.placement_static[p] != !covered)
			{
				platform_log("h2 phantoms: %s placed at %.2f %.2f %.2f forward %.2f %.2f %.2f: %s",
					h2_phantoms.kinds[h2_phantoms.placement_kinds[p]].name,
					(double)h2_phantoms.placements[p].position.x, (double)h2_phantoms.placements[p].position.y,
					(double)h2_phantoms.placements[p].position.z, (double)h2_phantoms.placements[p].forward.i,
					(double)h2_phantoms.placements[p].forward.j, (double)h2_phantoms.placements[p].forward.k,
					covered ? "its object's" : "no object, a fixed volume");
			}
			h2_phantoms.placement_static[p] = !covered;
		}
	}
}

/* a level object's frame */
static void h2_phantoms_object_frame(struct object_datum const *object, struct phantom_frame *frame)
{
	frame->position = object->object.position;
	frame->forward = object->object.forward;
	frame->up = object->object.up;
	frame->scale = object->object.scale > 0.001f ? object->object.scale : 1.f;
	frame->middle = object->object.bounding_sphere_center;
	frame->radius = object->object.bounding_sphere_radius;
}

/* a point of the object's space in the world (and a direction) */
static void h2_phantoms_to_world(struct phantom_frame const *frame, real_point3d const *local, real_point3d *world)
{
	real_vector3d left;
	real scale = frame->scale;
	real_vector3d const *f = &frame->forward, *u = &frame->up;

	cross_product3d(u, f, &left);
	world->x = frame->position.x + scale * (f->i * local->x + left.i * local->y + u->i * local->z);
	world->y = frame->position.y + scale * (f->j * local->x + left.j * local->y + u->j * local->z);
	world->z = frame->position.z + scale * (f->k * local->x + left.k * local->y + u->k * local->z);
}

static void h2_phantoms_direction_to_world(struct phantom_frame const *frame, real_vector3d const *local, real_vector3d *world)
{
	real_vector3d left;
	real_vector3d const *f = &frame->forward, *u = &frame->up;

	cross_product3d(u, f, &left);
	world->i = f->i * local->i + left.i * local->j + u->i * local->k;
	world->j = f->j * local->i + left.j * local->j + u->j * local->k;
	world->k = f->k * local->i + left.k * local->j + u->k * local->k;
}

/* the distance from point to the segment a..b */
static real h2_phantoms_segment_distance(real_point3d const *point, real_point3d const *a, real_point3d const *b)
{
	real_vector3d ab, ap;
	real t, length2;
	real_point3d nearest;

	vector_from_points3d(a, b, &ab);
	vector_from_points3d(a, point, &ap);
	length2 = dot_product3d(&ab, &ab);
	t = length2 > 1e-6f ? PIN(dot_product3d(&ap, &ab) / length2, 0.f, 1.f) : 0.f;
	nearest.x = a->x + ab.i * t;
	nearest.y = a->y + ab.j * t;
	nearest.z = a->z + ab.k * t;
	return distance3d(point, &nearest);
}

/* velocity (world units a second) along unit vector `toward` brought up to
`most` at `acceleration` a tick (no further than `most`, and not slowed) */
static void h2_phantoms_accelerate(real_vector3d *velocity, real_vector3d const *toward, real acceleration, real most)
{
	real along = dot_product3d(velocity, toward);
	real wanted;

	if (acceleration <= 0.f || most <= 0.f || along >= most)
		return;
	wanted = MIN(most, along + acceleration / TICKS_PER_SECOND);
	velocity->i += toward->i * (wanted - along);
	velocity->j += toward->j * (wanted - along);
	velocity->k += toward->k * (wanted - along);
}

/* ... toward a point or a line `offset` away (its length the distance): no
faster than it closes in a tick, slowed to that if faster */
static void h2_phantoms_pull(real_vector3d *velocity, real_vector3d const *offset, real acceleration, real most, real dead_radius)
{
	real_vector3d toward = *offset;
	real distance = normalize3d(&toward);
	real along, wanted;

	if (acceleration <= 0.f || most <= 0.f || distance <= MAX(dead_radius, 1e-3f))
		return;
	most = MIN(most, (distance - dead_radius) * TICKS_PER_SECOND);
	along = dot_product3d(velocity, &toward);
	if (along < most)
		wanted = MIN(most, along + acceleration / TICKS_PER_SECOND);
	else
		wanted = MAX(most, along - acceleration / TICKS_PER_SECOND);
	velocity->i += toward.i * (wanted - along);
	velocity->j += toward.j * (wanted - along);
	velocity->k += toward.k * (wanted - along);
}

/* whether a phantom type's flags leave the object alone */
static boolean h2_phantoms_ignores(unsigned long flags, struct object_datum const *object)
{
	switch (object->object.type)
	{
	case _object_type_biped:
	{
		struct unit_datum const *unit = (struct unit_datum const *)object;

		if ((flags & FLAG(_h2_phantom_ignores_bipeds_bit)) ||
			((flags & FLAG(_h2_phantom_ignores_dead_bipeds_bit)) && TEST_FLAG(object->object.damage_flags, _object_dead_bit)))
		{
			return TRUE;
		}
		return (flags & FLAG(unit->unit.player_index != NONE ? _h2_phantom_ignores_players_bit : _h2_phantom_ignores_nonplayers_bit)) != 0;
	}
	case _object_type_vehicle:
		return (flags & FLAG(_h2_phantom_ignores_vehicles_bit)) != 0;
	case _object_type_weapon:
		return (flags & FLAG(_h2_phantom_ignores_weapons_bit)) != 0;
	case _object_type_equipment:
		return (flags & FLAG(_h2_phantom_ignores_equipment_bit)) != 0;
	case _object_type_garbage:
		return (flags & FLAG(_h2_phantom_ignores_garbage_bit)) != 0;
	}
	return FALSE;
}

/* whether point is in a shape of a kind (its object's frame), or no
further out than reach */
static boolean h2_phantoms_shape_contains(struct phantom_kind const *kind, struct phantom_shape const *shape,
	struct phantom_frame const *owner, real_point3d const *point, real reach)
{
	real_point3d bottom, top;

	if (shape->polyhedron)
	{
		/* (the point in the object's space, inside each plane
		or no further out than reach) */
		real_vector3d to, left;
		real_point3d local;
		real scale = owner->scale;
		short n;

		vector_from_points3d(&owner->position, point, &to);
		cross_product3d(&owner->up, &owner->forward, &left);
		local.x = dot_product3d(&to, &owner->forward) / scale;
		local.y = dot_product3d(&to, &left) / scale;
		local.z = dot_product3d(&to, &owner->up) / scale;
		for (n = 0; n < shape->plane_count; n++)
		{
			real const *plane = kind->planes[shape->plane_first + n];

			if (plane[0] * local.x + plane[1] * local.y + plane[2] * local.z + plane[3] > reach / scale)
				break;
		}
		if (n < shape->plane_count)
			return FALSE;
	}
	else if (shape->box)
	{
		real_point3d box_middle;
		real_vector3d to, axis;
		short a;
		boolean inside = TRUE;

		h2_phantoms_to_world(owner, &shape->middle, &box_middle);
		vector_from_points3d(&box_middle, point, &to);
		for (a = 0; a < 3 && inside; a++)
		{
			h2_phantoms_direction_to_world(owner, &shape->axes[a], &axis);
			inside = fabsf(dot_product3d(&to, &axis)) <= shape->half_extents[a] + reach;
		}
		if (!inside)
			return FALSE;
	}
	else
	{
		h2_phantoms_to_world(owner, &shape->bottom, &bottom);
		h2_phantoms_to_world(owner, &shape->top, &top);
		if (h2_phantoms_segment_distance(point, &bottom, &top) > shape->radius + reach)
			return FALSE;
	}
	return TRUE;
}

static boolean h2_phantoms_was_riding(long object_index)
{
	short k;

	for (k = 0; k < h2_phantoms.rider_count; k++)
	{
		if (h2_phantoms.riders[k] == object_index)
			return TRUE;
	}
	return FALSE;
}

/* ---------- public code */

void h2_phantoms_map_loaded(
	char const *map_name)
{
	char const *directory = map_name ? h2_maps_directory_of(map_name) : cache_files_map_directory();
	struct h2_cache *cache;
	struct h2_cache_tag scenario;
	char why[160];
	short p;

	memset(&h2_phantoms, 0, sizeof(h2_phantoms));
	h2_phantoms.objects_found_at = NONE;
	if (!map_name || !directory || !h2_level_is_halo2_map(directory, map_name) ||
		(getenv("HALO_H2_PHANTOMS") && !atoi(getenv("HALO_H2_PHANTOMS"))))
	{
		return;
	}
	cache = h2_cache_open(directory, map_name, why, sizeof(why));
	if (!cache)
		return;
	if (h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario))
	{
		for (p = 0; p < (short)NUMBEROF(h2_scenario_palettes); p++)
		{
			unsigned long palette, placements;
			long count = 0, index, placed = 0;
			short palette_kinds[256];

			if (!h2_cache_block(cache, scenario.address + h2_scenario_palettes[p], 0x28, &count, &palette))
				continue;
			for (index = 0; index < count && index < (long)NUMBEROF(palette_kinds); index++)
			{
				long tag = h2_cache_reference(cache, palette + 0x28 * index);

				palette_kinds[index] = tag != NONE ? h2_phantoms_kind_read(cache, tag) : NONE;
			}
			/* (and the palette's placements of them: placement flags at 4 -
			not placed automatically 0, never placed 6 -, position 8,
			rotation 0x14 (yaw, pitch, roll), scale 0x20) */
			if (!h2_cache_block(cache, scenario.address + h2_scenario_placements[p][0], h2_scenario_placements[p][1], &placed,
				&placements))
			{
				continue;
			}
			for (index = 0; index < placed && h2_phantoms.placement_count < PHANTOM_PLACEMENTS_MAXIMUM; index++)
			{
				unsigned long at = placements + h2_scenario_placements[p][1] * index;
				short entry = h2_phantoms_read_short(cache, at);
				struct phantom_frame *frame = &h2_phantoms.placements[h2_phantoms.placement_count];
				real yaw, pitch, roll, scale;
				real_vector3d left0, up0, left;

				if (entry < 0 || entry >= count || entry >= (short)NUMBEROF(palette_kinds) || palette_kinds[entry] == NONE ||
					(h2_cache_read32(cache, at + 4) & ((1 << 0) | (1 << 6))))
				{
					continue;
				}
				h2_phantoms_read_point(cache, at + 8, &frame->position);
				yaw = h2_phantoms_read_real(cache, at + 0x14);
				pitch = h2_phantoms_read_real(cache, at + 0x18);
				roll = h2_phantoms_read_real(cache, at + 0x1C);
				scale = h2_phantoms_read_real(cache, at + 0x20);
				frame->scale = scale > 0.001f ? scale : 1.f;
				frame->forward.i = cosf(yaw) * cosf(pitch);
				frame->forward.j = sinf(yaw) * cosf(pitch);
				frame->forward.k = sinf(pitch);
				left0.i = -sinf(yaw);
				left0.j = cosf(yaw);
				left0.k = 0.f;
				cross_product3d(&frame->forward, &left0, &up0);
				left.i = cosf(roll) * left0.i + sinf(roll) * up0.i;
				left.j = cosf(roll) * left0.j + sinf(roll) * up0.j;
				left.k = cosf(roll) * left0.k + sinf(roll) * up0.k;
				cross_product3d(&frame->forward, &left, &frame->up);
				frame->middle = frame->position;
				frame->radius = frame->scale * h2_phantoms_kind_reach(&h2_phantoms.kinds[palette_kinds[entry]]);
				h2_phantoms.placement_kinds[h2_phantoms.placement_count] = palette_kinds[entry];
				h2_phantoms.placement_static[h2_phantoms.placement_count] = FALSE;
				h2_phantoms.placement_count++;
			}
		}
	}
	h2_cache_close(cache);
}

long h2_phantoms_lift_slots(
	void)
{
	return h2_phantoms.kind_count ? (long)(h2_phantoms.object_count + h2_phantoms.placement_count) * PHANTOM_SHAPES_MAXIMUM : 0;
}

boolean h2_phantoms_lift_get(
	long slot,
	real_point3d *bottom,
	real_point3d *top)
{
	struct phantom_frame frame;
	struct phantom_kind const *kind;
	struct phantom_shape const *shape;
	struct phantom_type const *type;
	real_point3d marker;
	real_vector3d direction;
	short o = (short)(slot / PHANTOM_SHAPES_MAXIMUM), s = (short)(slot % PHANTOM_SHAPES_MAXIMUM);
	boolean found = FALSE;
	real along;

	if (slot < 0 || slot >= h2_phantoms_lift_slots())
		return FALSE;
	if (o < h2_phantoms.object_count)
	{
		struct object_datum const *owner_object = object_try_and_get(h2_phantoms.objects[o]);

		if (!owner_object)
			return FALSE;
		h2_phantoms_object_frame(owner_object, &frame);
		kind = &h2_phantoms.kinds[h2_phantoms.object_kinds[o]];
	}
	else
	{
		short p = (short)(o - h2_phantoms.object_count);

		if (!h2_phantoms.placement_static[p])
			return FALSE;
		frame = h2_phantoms.placements[p];
		kind = &h2_phantoms.kinds[h2_phantoms.placement_kinds[p]];
	}
	if (s >= kind->shape_count)
		return FALSE;
	shape = &kind->shapes[s];
	type = &kind->types[shape->type];
	/* (one that lifts a player: up, and none of players, bipeds) */
	if ((type->flags & (FLAG(_h2_phantom_ignores_players_bit) | FLAG(_h2_phantom_ignores_bipeds_bit))) ||
		type->direction_maximum <= 0.f)
	{
		return FALSE;
	}
	h2_phantoms_to_world(&frame, &type->marker_position, &marker);
	h2_phantoms_direction_to_world(&frame, &type->marker_forward, &direction);
	normalize3d(&direction);
	if (direction.k < 0.7f)
		return FALSE;
	/* (its column: the lowest and highest points of the marker's line in
	the shape) */
	for (along = -3.f; along <= 30.f; along += 0.25f)
	{
		real_point3d point;

		point.x = marker.x + direction.i * along;
		point.y = marker.y + direction.j * along;
		point.z = marker.z + direction.k * along;
		if (h2_phantoms_shape_contains(kind, shape, &frame, &point, 0.1f))
		{
			if (!found)
				*bottom = point;
			*top = point;
			found = TRUE;
		}
	}
	return found;
}

boolean h2_phantoms_riding(
	long object_index)
{
	return h2_phantoms.kind_count && h2_phantoms_was_riding(object_index);
}

void h2_phantoms_tick(
	void)
{
	struct object_iterator iterator;
	struct object_datum *object;
	long riders[PHANTOM_RIDERS_MAXIMUM];
	short rider_count = 0;
	short o;

	if (!h2_phantoms.kind_count || game_connection() == _game_connection_network_client)
		return;
	/* (the level's objects, found once it runs, again now and then) */
	if (h2_phantoms.objects_found_at == NONE || game_time_get() - h2_phantoms.objects_found_at >= 10 * TICKS_PER_SECOND ||
		game_time_get() < h2_phantoms.objects_found_at)
	{
		h2_phantoms_objects_find();
		h2_phantoms.objects_found_at = game_time_get();
	}
	/* (perf/cairo-30fps-2) each owner looked up and its frame made once a
	tick, not once for each unit and item: the loop below writes only the
	riders' velocities and flags, nothing of a frame (HALO_PHANTOM_FRAMES=0:
	as before) */
	static struct phantom_frame owner_frames[PHANTOM_OBJECTS_MAXIMUM];
	static boolean owner_present[PHANTOM_OBJECTS_MAXIMUM];
	static int frames_on = -1;

	if (frames_on < 0)
		frames_on = !(getenv("HALO_PHANTOM_FRAMES") && atoi(getenv("HALO_PHANTOM_FRAMES")) == 0);
	if (frames_on)
	{
		for (o = 0; o < h2_phantoms.object_count; o++)
		{
			struct object_datum const *owner_object = object_try_and_get(h2_phantoms.objects[o]);

			owner_present[o] = owner_object != NULL;
			if (owner_object)
				h2_phantoms_object_frame(owner_object, &owner_frames[o]);
		}
	}
	/* (perf/vita-native) every unit and item against every phantom volume
	each tick (Cairo Station: ~250 of them against each of the level's
	volumes; "phantoms" 0.85-1.0 ms a tick on the Vita, nearly all of them
	nowhere near one): the box around all the volumes that can act this
	tick, each by its radius, in doubles. An object whose middle is outside
	it by more than its own reach - the loop's (owner radius + object radius
	+ 8) * 1.001 + 0.01, the owner's radius taken by the box, and a slack of
	1 + 0.002 * the largest radius over the float rounding - is passed over
	by that test for every volume, so for it the loop below does nothing:
	skipped whole. A middle that is not a number is not skipped.
	(HALO_PHANTOM_BOUNDS=0: every object asked each volume, as before) */
	static int bounds_on = -1;
	static unsigned long bounds_seen;
	double bounds_low[3] = {1.0e30, 1.0e30, 1.0e30}, bounds_high[3] = {-1.0e30, -1.0e30, -1.0e30};
	double bounds_slack = 1.0;

	halo_native_switch("HALO_PHANTOM_BOUNDS", &bounds_on, &bounds_seen); /* (native_switch.h) */
	if (bounds_on && frames_on)
	{
		double largest = 0.0;

		for (o = 0; o < h2_phantoms.object_count + h2_phantoms.placement_count; o++)
		{
			struct phantom_frame const *owner;

			if (o < h2_phantoms.object_count)
			{
				if (!owner_present[o])
					continue;
				owner = &owner_frames[o];
			}
			else
			{
				if (!h2_phantoms.placement_static[o - h2_phantoms.object_count])
					continue;
				owner = &h2_phantoms.placements[o - h2_phantoms.object_count];
			}
			/* (a volume whose middle or radius is not a number: no box, every object asked) */
			if (!(owner->radius >= 0.f) || !(owner->middle.x == owner->middle.x) || !(owner->middle.y == owner->middle.y) ||
				!(owner->middle.z == owner->middle.z))
			{
				bounds_low[0] = bounds_low[1] = bounds_low[2] = -1.0e30;
				bounds_high[0] = bounds_high[1] = bounds_high[2] = 1.0e30;
				break;
			}
			if ((double)owner->radius > largest)
				largest = (double)owner->radius;
			if ((double)owner->middle.x - (double)owner->radius < bounds_low[0]) bounds_low[0] = (double)owner->middle.x - (double)owner->radius;
			if ((double)owner->middle.y - (double)owner->radius < bounds_low[1]) bounds_low[1] = (double)owner->middle.y - (double)owner->radius;
			if ((double)owner->middle.z - (double)owner->radius < bounds_low[2]) bounds_low[2] = (double)owner->middle.z - (double)owner->radius;
			if ((double)owner->middle.x + (double)owner->radius > bounds_high[0]) bounds_high[0] = (double)owner->middle.x + (double)owner->radius;
			if ((double)owner->middle.y + (double)owner->radius > bounds_high[1]) bounds_high[1] = (double)owner->middle.y + (double)owner->radius;
			if ((double)owner->middle.z + (double)owner->radius > bounds_high[2]) bounds_high[2] = (double)owner->middle.z + (double)owner->radius;
		}
		bounds_slack = 1.0 + 0.002 * largest;
	}
	/* (perf/grunt-tick) the volumes that can act this tick sorted by their
	middle's x, for each object only those its x is within reach of: Cairo's
	Grunt fight asked ~215 objects each of its ~23 volumes a tick (the box
	around them all holds the fight) and ~6% passed the loop's first test,
	each axis apart by more than the reach. An object's candidates are the
	volumes whose x is within the largest such reach of its own, widened by
	the box's slack over the float rounding (each passed over would be by
	that test), taken in the loop's order: the same volumes in the same order
	act on it. An object whose x is not a number, or a tick with a volume
	whose middle or radius is not one, asks every volume as before.
	(HALO_PHANTOM_SORTED=0: every volume asked; 2: every volume asked, and
	each one passing that test checked to be among the candidates) */
	static int sorted_on = -1;
	static unsigned long sorted_seen, sorted_checked, sorted_missed;
	short sorted_owner[PHANTOM_OBJECTS_MAXIMUM + PHANTOM_PLACEMENTS_MAXIMUM];
	double sorted_x[PHANTOM_OBJECTS_MAXIMUM + PHANTOM_PLACEMENTS_MAXIMUM];
	short sorted_count = 0;
	double sorted_radius = 0.0;
	boolean sorted_usable = FALSE;

	halo_native_switch("HALO_PHANTOM_SORTED", &sorted_on, &sorted_seen); /* (native_switch.h) */
	if (sorted_on && frames_on)
	{
		sorted_usable = TRUE;
		for (o = 0; o < h2_phantoms.object_count + h2_phantoms.placement_count; o++)
		{
			struct phantom_frame const *owner;
			short at;

			if (o < h2_phantoms.object_count)
			{
				if (!owner_present[o])
					continue;
				owner = &owner_frames[o];
			}
			else
			{
				if (!h2_phantoms.placement_static[o - h2_phantoms.object_count])
					continue;
				owner = &h2_phantoms.placements[o - h2_phantoms.object_count];
			}
			if (!(owner->radius >= 0.f) || !(owner->middle.x == owner->middle.x) ||
				!(owner->middle.y == owner->middle.y) || !(owner->middle.z == owner->middle.z))
			{
				sorted_usable = FALSE;
				break;
			}
			if ((double)owner->radius > sorted_radius)
				sorted_radius = (double)owner->radius;
			/* (an insertion: a few dozen) */
			for (at = sorted_count; at > 0 && sorted_x[at - 1] > (double)owner->middle.x; at--)
			{
				sorted_x[at] = sorted_x[at - 1];
				sorted_owner[at] = sorted_owner[at - 1];
			}
			sorted_x[at] = (double)owner->middle.x;
			sorted_owner[at] = o;
			sorted_count++;
		}
	}
	object_iterator_new(&iterator, _object_mask_unit | _object_mask_item, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		boolean vehicle = object->object.type == _object_type_vehicle;
		boolean biped = object->object.type == _object_type_biped;
		real reach;
		real_point3d middle = object->object.bounding_sphere_center, feet;
		real_vector3d velocity;
		boolean in = FALSE, upward, negate_gravity = FALSE;

		if (object->object.parent_object_index != NONE)
			continue;
		if (bounds_on && frames_on)
		{
			double gap = ((double)object->object.bounding_sphere_radius + 8.0) * 1.001 + 0.01 + bounds_slack;

			/* (a radius that is not a number or below zero: asked as before) */
			if (object->object.bounding_sphere_radius >= 0.f &&
				((double)middle.x < bounds_low[0] - gap || (double)middle.x > bounds_high[0] + gap ||
				(double)middle.y < bounds_low[1] - gap || (double)middle.y > bounds_high[1] + gap ||
				(double)middle.z < bounds_low[2] - gap || (double)middle.z > bounds_high[2] + gap))
			{
				continue;
			}
		}
		/* (how far into the pill it must be: a biped's capsule, a vehicle's
		half its sphere, an item's middle) */
		reach = biped ? MAX(0.1f, biped_definition_get(object->definition_index)->biped.collision_radius) :
			vehicle ? MIN(1.f, 0.5f * object->object.bounding_sphere_radius) : 0.05f;
		feet = object->object.position;
		feet.z += reach;
		velocity.i = object->object.translational_velocity.i * TICKS_PER_SECOND;
		velocity.j = object->object.translational_velocity.j * TICKS_PER_SECOND;
		velocity.k = object->object.translational_velocity.k * TICKS_PER_SECOND;
		{
		/* (HALO_PHANTOM_SORTED, above) the volumes to ask, in the loop's order */
		short order[PHANTOM_OBJECTS_MAXIMUM + PHANTOM_PLACEMENTS_MAXIMUM];
		boolean candidate[PHANTOM_OBJECTS_MAXIMUM + PHANTOM_PLACEMENTS_MAXIMUM];
		short order_count = 0, k;
		boolean use_candidates = sorted_usable && middle.x == middle.x && object->object.bounding_sphere_radius >= 0.f;

		if (use_candidates)
		{
			double reach_x = (sorted_radius + (double)object->object.bounding_sphere_radius + 8.0) * 1.001 + 0.01 + bounds_slack;
			double low = (double)middle.x - reach_x, high = (double)middle.x + reach_x;
			short first = 0, last = sorted_count, c;

			while (first < last)
			{
				short half = (short)((first + last) / 2);

				if (sorted_x[half] < low)
					first = (short)(half + 1);
				else
					last = half;
			}
			csmemset(candidate, 0, sizeof(candidate));
			for (c = first; c < sorted_count && sorted_x[c] <= high; c++)
				candidate[sorted_owner[c]] = TRUE;
			if (sorted_on == 2)
				use_candidates = FALSE;
			else
			{
				for (o = 0; o < h2_phantoms.object_count + h2_phantoms.placement_count; o++)
				{
					if (candidate[o])
						order[order_count++] = o;
				}
			}
			if (sorted_on == 2)
				sorted_checked++;
		}
		if (!use_candidates)
		{
			for (o = 0; o < h2_phantoms.object_count + h2_phantoms.placement_count; o++)
				order[order_count++] = o;
		}
		for (k = 0; k < order_count; k++)
		{
			struct phantom_frame frame;
			struct phantom_frame const *owner = &frame;
			struct phantom_kind const *kind;
			short s;

			o = order[k];
			/* (the level's objects, then the placements none is at) */
			if (o < h2_phantoms.object_count && frames_on)
			{
				if (!owner_present[o])
					continue;
				owner = &owner_frames[o];
				kind = &h2_phantoms.kinds[h2_phantoms.object_kinds[o]];
			}
			else if (o < h2_phantoms.object_count)
			{
				struct object_datum const *owner_object = object_try_and_get(h2_phantoms.objects[o]);

				if (!owner_object)
					continue;
				h2_phantoms_object_frame(owner_object, &frame);
				kind = &h2_phantoms.kinds[h2_phantoms.object_kinds[o]];
			}
			else
			{
				short p = (short)(o - h2_phantoms.object_count);

				if (!h2_phantoms.placement_static[p])
					continue;
				frame = h2_phantoms.placements[p];
				kind = &h2_phantoms.kinds[h2_phantoms.placement_kinds[p]];
			}
			{
				/* (an axis apart by more than the reach, widened by far more
				than the distance's rounding: the distance is more, passed over
				without it) */
				real reach_apart = owner->radius + object->object.bounding_sphere_radius + 8.f;
				real apart = reach_apart * 1.001f + 0.01f;

				if (frames_on && (fabs(owner->middle.x - middle.x) > apart || fabs(owner->middle.y - middle.y) > apart ||
					fabs(owner->middle.z - middle.z) > apart))
				{
					continue;
				}
				if (sorted_on == 2 && sorted_usable && middle.x == middle.x && object->object.bounding_sphere_radius >= 0.f &&
					!candidate[o])
				{
					sorted_missed++;
					platform_log("h2 phantoms: HALO_PHANTOM_SORTED=2: volume %d passes the first test but is not a candidate (%lu missed, %lu objects checked)",
						(int)o, sorted_missed, sorted_checked);
				}
				if (distance3d(&owner->middle, &middle) > reach_apart)
					continue;
			}
			for (s = 0; s < kind->shape_count; s++)
			{
				struct phantom_shape const *shape = &kind->shapes[s];
				struct phantom_type const *type = &kind->types[shape->type];
				real_point3d marker;
				real_vector3d direction, offset, axis_offset;
				real along;

				if (h2_phantoms_ignores(type->flags, object))
					continue;
				/* (its middle in the shape, or a biped's feet - the bottom of its
				capsule: Havok's phantom finds the whole of it, and Gemini's
				jump pads are lower than a standing player's middle) */
				if (!h2_phantoms_shape_contains(kind, shape, owner, &middle, reach) &&
					(!biped || !h2_phantoms_shape_contains(kind, shape, owner, &feet, reach)))
				{
					continue;
				}
				in = TRUE;
				if (type->flags & FLAG(_h2_phantom_negates_gravity_bit))
					negate_gravity = TRUE;
				h2_phantoms_to_world(owner, &type->marker_position, &marker);
				h2_phantoms_direction_to_world(owner, &type->marker_forward, &direction);
				normalize3d(&direction);
				vector_from_points3d(&middle, &marker, &offset);
				along = dot_product3d(&offset, &direction);
				axis_offset.i = offset.i - direction.i * along;
				axis_offset.j = offset.j - direction.j * along;
				axis_offset.k = offset.k - direction.k * along;
				h2_phantoms_pull(&velocity, &offset, type->center_acceleration, type->center_maximum, type->dead_radius);
				h2_phantoms_pull(&velocity, &axis_offset, type->axis_acceleration, type->axis_maximum, type->dead_radius);
				h2_phantoms_accelerate(&velocity, &direction, type->direction_acceleration, type->direction_maximum);
				if (h2_phantoms_log_level() >= 2 || (h2_phantoms_log_level() && !h2_phantoms_was_riding(iterator.index)))
				{
					platform_log("h2 phantoms: tick %ld %08lx %s in %s shape %d at %.2f %.2f %.2f: velocity %.2f %.2f %.2f -> %.2f %.2f %.2f wu/s",
						(long)game_time_get(), (unsigned long)iterator.index, tag_get_name(object->definition_index), kind->name, (int)s,
						(double)middle.x, (double)middle.y, (double)middle.z,
						(double)(object->object.translational_velocity.i * TICKS_PER_SECOND),
						(double)(object->object.translational_velocity.j * TICKS_PER_SECOND),
						(double)(object->object.translational_velocity.k * TICKS_PER_SECOND),
						(double)velocity.i, (double)velocity.j, (double)velocity.k);
				}
			}
		}
		}
		if (!in)
			continue;
		/* (gravity undone: the game's own, a tick's, given back) */
		if (negate_gravity)
			velocity.k += global_gravity * TICKS_PER_SECOND;
		if (rider_count < PHANTOM_RIDERS_MAXIMUM)
			riders[rider_count++] = iterator.index;
		upward = velocity.k > object->object.translational_velocity.k * TICKS_PER_SECOND + 1e-4f;
		object->object.translational_velocity.i = velocity.i / TICKS_PER_SECOND;
		object->object.translational_velocity.j = velocity.j / TICKS_PER_SECOND;
		object->object.translational_velocity.k = velocity.k / TICKS_PER_SECOND;
		SET_FLAG(object->object.flags, _object_at_rest_bit, FALSE);
		/* (a biped lifted leaves the ground: its own physics then fly it) */
		if (biped && upward && velocity.k > 0.f)
			SET_FLAG(((struct biped_datum *)object)->biped.flags, _biped_airborne_bit, TRUE);
	}
	memcpy(h2_phantoms.riders, riders, sizeof(riders[0]) * rider_count);
	h2_phantoms.rider_count = rider_count;
	if (sorted_on == 2 && sorted_checked && (game_time_get() % 900) == 0)
		platform_log("h2 phantoms: HALO_PHANTOM_SORTED=2: %lu objects checked, %lu volumes missed", sorted_checked, sorted_missed);
}
