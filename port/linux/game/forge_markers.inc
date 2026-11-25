/*
FORGE_MARKERS.INC

Forge's markers (included by forge.c, for its statics): the layout's player
spawn points, teleporters and game type objects (forge_spawns.inc,
forge_netgame.inc), which are no objects, shown to the monitor as small
see-through markers on the ground in their team's or channel's color, and
taken, carried, let go, turned and deleted like an object, their properties
opened from them (X while holding one). The map's own player spawns are
drawn beside them, gray and read only.

A marker is drawn (forge_marker_draw: the game's debug-geometry shaders, a
color a vertex, blended), never an object: nothing of it is sent anywhere
or saved but the layout record it stands for. Shown only while the player
is the monitor in a Forge game: the Spartan in a Forge game, and anyone in
a layout played, sees none, teleporters' included (a teleporter is no
object: a layout shows where one is with the scenery placed there, as a
map's own teleporters are shown).

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

/* ---------- constants */

/* the marker, flat on the ground: its ring's outer and inner radii, how far
over the ground, the height its pick and its culling take (world units);
where the crosshair takes it, forge.c's FORGE_MARKER_PICK_RADIUS */
#define FORGE_MARKER_DISC_RADIUS 0.42f
#define FORGE_MARKER_RING_INNER 0.33f
#define FORGE_MARKER_LIFT 0.07f
#define FORGE_MARKER_HEIGHT 0.2f
#define FORGE_MARKER_SIDES 14
/* a marker let go stands this far over the ground under it */
#define FORGE_MARKER_CLEARANCE 0.25f
/* the most drawn a frame (a triangle budget: each is some 70), and the most
of them the map's own spawns */
#define FORGE_MARKER_MOST_DRAWN 96
#define FORGE_MARKER_MOST_MAP_DRAWN 32
/* the map's own spawns drawn this near the camera at most (world units) */
#define FORGE_SPAWN_MAP_DRAW_DISTANCE 60.f
/* (the pick's and culling's height of an initial spawn's: the same, flat) */
#define FORGE_MARKER_INITIAL_HEIGHT FORGE_MARKER_HEIGHT
#define FORGE_MARKER_TRIANGLES 176

enum
{
	/* (a marker of the layout: forge_spawns.inc) */
	_forge_marker_player_spawn = 0,
	/* (the map's own: gray) */
	_forge_marker_map_spawn,
	NUMBER_OF_FORGE_MARKER_DRAW_KINDS
};

/* ---------- structures */

/* a vertex of the debug geometry (_rasterizer_vertex_type_debug) */
struct forge_marker_vertex
{
	real_point3d point;
	unsigned long color;
};

struct forge_marker
{
	short kind;
	real_point3d position;
	real_vector3d forward;
	/* its color */
	real red;
	real green;
	real blue;
	/* under the crosshair, or held: brighter */
	boolean lit;
	/* (a player spawn) an initial spawn: a second ring, a filled center */
	boolean initial;
	/* (a patrol area: forge_ai_move.inc) a thin ring this wide round it */
	real area_radius;
};

/* ---------- private code */

static unsigned long forge_marker_pixel(
	struct forge_marker const *marker,
	real light,
	real alpha)
{
	real boost = marker->lit ? 1.35f : 1.f;
	unsigned long r = (unsigned long)(PIN(marker->red * light * boost, 0.f, 1.f) * 255.f + 0.5f);
	unsigned long g = (unsigned long)(PIN(marker->green * light * boost, 0.f, 1.f) * 255.f + 0.5f);
	unsigned long b = (unsigned long)(PIN(marker->blue * light * boost, 0.f, 1.f) * 255.f + 0.5f);
	unsigned long a = (unsigned long)(PIN(alpha, 0.f, 1.f) * 255.f + 0.5f);

	return (a << 24) | (r << 16) | (g << 8) | b;
}

static void forge_marker_triangle(
	struct forge_marker_vertex *vertices,
	long *count,
	real_point3d const *a,
	real_point3d const *b,
	real_point3d const *c,
	unsigned long color)
{
	if (*count + 3 > FORGE_MARKER_TRIANGLES * 3)
		return;
	vertices[*count].point = *a;
	vertices[(*count)++].color = color;
	vertices[*count].point = *b;
	vertices[(*count)++].color = color;
	vertices[*count].point = *c;
	vertices[(*count)++].color = color;
}

/* ---------- public code (forge.c's) */

/* a ring's quads, flat, from radius inner to outer */
static void forge_marker_ring(
	struct forge_marker_vertex *vertices,
	long *count,
	real_point3d const *center,
	real inner,
	real outer,
	unsigned long color)
{
	short side;

	for (side = 0; side < FORGE_MARKER_SIDES; side++)
	{
		real angle0 = (real)side * (real)(2.0 * 3.14159265358979) / (real)FORGE_MARKER_SIDES;
		real angle1 = (real)(side + 1) * (real)(2.0 * 3.14159265358979) / (real)FORGE_MARKER_SIDES;
		real_point3d in0 = *center, in1 = *center, out0 = *center, out1 = *center;

		in0.x += cosine(angle0) * inner;
		in0.y += sine(angle0) * inner;
		in1.x += cosine(angle1) * inner;
		in1.y += sine(angle1) * inner;
		out0.x += cosine(angle0) * outer;
		out0.y += sine(angle0) * outer;
		out1.x += cosine(angle1) * outer;
		out1.y += sine(angle1) * outer;
		if (inner <= 0.f)
		{
			forge_marker_triangle(vertices, count, center, &out0, &out1, color);
			continue;
		}
		forge_marker_triangle(vertices, count, &in0, &out0, &out1, color);
		forge_marker_triangle(vertices, count, &in0, &out1, &in1, color);
	}
}

/* a marker drawn in the 3D (render.c's hook, after the objects), flat on
the ground: a
ring in its color over a faint disc, a chevron on it pointing where it
faces; an initial spawn's a second ring inside and a filled center;
blended, a little over the ground under it, the walls in front hiding it */
static void forge_marker_draw(
	struct forge_marker const *marker)
{
	static struct forge_marker_vertex vertices[FORGE_MARKER_TRIANGLES * 3];
	real alpha = marker->lit ? 0.9f : marker->kind == _forge_marker_map_spawn ? 0.4f : 0.7f;
	real_vector3d forward = marker->forward;
	real_vector3d left;
	real_point3d center = marker->position;
	long count = 0;
	long buffer;

	forward.k = 0.f;
	if (normalize3d(&forward) == 0.f)
		forward = *global_forward3d;
	left.i = -forward.j;
	left.j = forward.i;
	left.k = 0.f;
	/* (on the ground under it: a marker stands its clearance over it) */
	{
		real_vector3d down;
		real_point3d from = center;
		struct collision_result collision;

		from.z += 0.1f;
		down.i = 0.f;
		down.j = 0.f;
		down.k = -(FORGE_MARKER_CLEARANCE + 1.f);
		if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit), &from,
			&down, NONE, &collision))
		{
			center.z = from.z + down.k * collision.t;
		}
	}
	center.z += FORGE_MARKER_LIFT;
	/* (the faint disc, the ring) */
	forge_marker_ring(vertices, &count, &center, 0.f, FORGE_MARKER_RING_INNER, forge_marker_pixel(marker, 0.8f, alpha * 0.35f));
	forge_marker_ring(vertices, &count, &center, FORGE_MARKER_RING_INNER, FORGE_MARKER_DISC_RADIUS,
		forge_marker_pixel(marker, 1.f, alpha));
	/* (an initial spawn's: a second ring and a filled center) */
	if (marker->initial)
	{
		forge_marker_ring(vertices, &count, &center, FORGE_MARKER_DISC_RADIUS * 0.5f, FORGE_MARKER_DISC_RADIUS * 0.62f,
			forge_marker_pixel(marker, 1.f, alpha));
		forge_marker_ring(vertices, &count, &center, 0.f, FORGE_MARKER_DISC_RADIUS * 0.22f, forge_marker_pixel(marker, 1.f, alpha));
	}
	/* (a patrol area's: a thin ring at its edge) */
	if (marker->area_radius > FORGE_MARKER_DISC_RADIUS)
	{
		forge_marker_ring(vertices, &count, &center, marker->area_radius - 0.08f, marker->area_radius + 0.08f,
			forge_marker_pixel(marker, 1.f, alpha * 0.8f));
	}
	/* (the chevron: two flat arms meeting at its tip, past the ring where
	it faces) */
	{
		real tip_out = FORGE_MARKER_DISC_RADIUS * 1.45f;
		real arm_back = FORGE_MARKER_DISC_RADIUS * 0.75f;
		real arm_side = FORGE_MARKER_DISC_RADIUS * 0.55f;
		real thick = FORGE_MARKER_DISC_RADIUS * 0.28f;
		short arm;

		for (arm = -1; arm <= 1; arm += 2)
		{
			real_point3d tip = center, tip_in = center, end = center, end_in = center;

			tip.x += forward.i * tip_out;
			tip.y += forward.j * tip_out;
			tip_in.x += forward.i * (tip_out - thick);
			tip_in.y += forward.j * (tip_out - thick);
			end.x += forward.i * arm_back + left.i * arm_side * arm;
			end.y += forward.j * arm_back + left.j * arm_side * arm;
			end_in.x += forward.i * (arm_back - thick) + left.i * arm_side * arm;
			end_in.y += forward.j * (arm_back - thick) + left.j * arm_side * arm;
			tip.z = tip_in.z = end.z = end_in.z = center.z + 0.005f;
			forge_marker_triangle(vertices, &count, &tip, &end, &end_in, forge_marker_pixel(marker, 1.f, alpha));
			forge_marker_triangle(vertices, &count, &tip, &end_in, &tip_in, forge_marker_pixel(marker, 1.f, alpha));
		}
	}
	if (count < 3)
		return;
	buffer = rasterizer_dynamic_vertices_new(_rasterizer_vertex_type_debug, count);
	if (buffer == NONE)
		return;
	{
		void *locked;

		rasterizer_globals.current_lock_operation = _rasterizer_lock_debug;
		locked = rasterizer_dynamic_vertices_lock(buffer);
		if (locked)
		{
			csmemcpy(locked, vertices, (size_t)count * sizeof(vertices[0]));
			rasterizer_dynamic_vertices_unlock(buffer);
			rasterizer_debug_drawing_begin(FALSE);
			rasterizer_draw_dynamic_vertices(0, count / 3, buffer, 3);
			rasterizer_debug_drawing_end();
		}
		rasterizer_globals.current_lock_operation = _rasterizer_lock_none;
	}
	rasterizer_dynamic_vertices_delete(buffer);
}

/* where a player spawn point's marker stands and faces (the one held: where
it is held) */
static boolean forge_marker_of_spawn(
	struct forge_spawn const *spawn,
	real_point3d *position,
	real_vector3d *forward)
{
	if (spawn->id == forge_globals.held_marker_id)
	{
		*position = forge_globals.held_marker_position;
		*forward = forge_globals.held_forward;
		return TRUE;
	}
	*position = spawn->home.position;
	*forward = spawn->home.forward;
	return TRUE;
}

/* a player spawn point's color: its team's (any: white); a teleporter's
its channel's (a sender and its receiver the same); a game type object's
its own */
static void forge_marker_spawn_color(
	struct forge_spawn const *spawn,
	struct forge_marker *marker)
{
	/* (a volume's: its type's, forge_volumes.inc) */
	if (forge_spawn_is_volume(spawn))
	{
		forge_volume_color(spawn, marker);
		return;
	}
	/* (an AI route's point or patrol area: its route's color) */
	if (spawn->kind == _forge_spawn_ai_route || spawn->kind == _forge_spawn_ai_area)
	{
		static real const route_colors[4][3] =
		{
			{ 1.00f, 0.70f, 0.10f },
			{ 0.20f, 0.95f, 0.85f },
			{ 0.95f, 0.35f, 0.95f },
			{ 0.70f, 1.00f, 0.25f },
		};
		short route = (short)(spawn->channel & 3);
		real shade = 1.f - 0.15f * (real)((spawn->channel >> 2) & 3);

		marker->red = route_colors[route][0] * shade;
		marker->green = route_colors[route][1] * shade;
		marker->blue = route_colors[route][2] * shade;
		return;
	}
	/* (an effect's: pale cyan; a decal's: tan) */
	if (forge_spawn_is_effect(spawn))
	{
		marker->red = spawn->kind == _forge_spawn_effect ? 0.55f : 0.82f;
		marker->green = spawn->kind == _forge_spawn_effect ? 0.95f : 0.66f;
		marker->blue = spawn->kind == _forge_spawn_effect ? 1.00f : 0.42f;
		return;
	}
	/* (a game type object's: a CTF flag its team's, a hill point yellow, an
	oddball spawn white, a race checkpoint orange) */
	if (forge_spawn_is_objective(spawn))
	{
		static real const objective_colors[4][3] =
		{
			{ 1.00f, 1.00f, 1.00f }, /* (a flag: its team's, below) */
			{ 1.00f, 0.92f, 0.25f },
			{ 0.95f, 0.95f, 0.95f },
			{ 1.00f, 0.55f, 0.15f },
		};
		short kind = (short)(spawn->kind - _forge_spawn_flag);

		marker->red = objective_colors[kind][0];
		marker->green = objective_colors[kind][1];
		marker->blue = objective_colors[kind][2];
		if (spawn->kind == _forge_spawn_flag)
		{
			marker->red = spawn->channel == 0 ? 1.f : 0.28f;
			marker->green = spawn->channel == 0 ? 0.28f : 0.55f;
			marker->blue = spawn->channel == 0 ? 0.22f : 1.f;
		}
		return;
	}
	if (forge_spawn_is_teleporter(spawn))
	{
		static real const channel_colors[8][3] =
		{
			{ 0.35f, 1.00f, 0.35f }, /* green, the teleporters' flash */
			{ 0.30f, 0.85f, 1.00f }, /* cyan */
			{ 1.00f, 0.80f, 0.20f }, /* gold */
			{ 0.85f, 0.45f, 1.00f }, /* violet */
			{ 1.00f, 0.50f, 0.25f }, /* orange */
			{ 0.55f, 0.65f, 1.00f }, /* periwinkle */
			{ 1.00f, 0.45f, 0.70f }, /* pink */
			{ 0.75f, 1.00f, 0.75f }, /* mint */
		};
		short channel = (short)(spawn->channel & 7);
		real shade = spawn->channel >= 8 ? 0.7f : 1.f;

		marker->red = channel_colors[channel][0] * shade;
		marker->green = channel_colors[channel][1] * shade;
		marker->blue = channel_colors[channel][2] * shade;
		return;
	}
	if (spawn->team == 0)
	{
		marker->red = 1.f;
		marker->green = 0.28f;
		marker->blue = 0.22f;
	}
	else if (spawn->team == 1)
	{
		marker->red = 0.28f;
		marker->green = 0.55f;
		marker->blue = 1.f;
	}
	else
	{
		marker->red = 0.92f;
		marker->green = 0.92f;
		marker->blue = 0.85f;
	}
}

/* whether the markers are shown: the monitor's, in a Forge game (not a
layout played) */
static boolean forge_markers_shown(
	void)
{
	return forge_globals.active && forge_globals.game_active && !forge_globals.playing;
}

/* the marker the crosshair is on (its id; NONE), and how far along the
crosshair's line */
static long forge_marker_under_crosshair(
	real *distance)
{
	real_point3d position;
	real_vector3d forward;
	real nearest = forge_grab_range();
	long nearest_id = NONE;
	short index;

	if (!forge_markers_shown() || !forge_camera_get(&position, &forward))
		return NONE;
	/* (the layout's markers) */
	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];
		real_point3d at;
		real_vector3d facing;
		real_vector3d to_center;
		real along;
		real across_squared;
		real entry;

		if (spawn->id == forge_globals.held_marker_id || !forge_marker_of_spawn(spawn, &at, &facing))
			continue;
		at.z += (spawn->kind == _forge_spawn_initial ? FORGE_MARKER_INITIAL_HEIGHT : FORGE_MARKER_HEIGHT) * 0.5f;
		to_center.i = at.x - position.x;
		to_center.j = at.y - position.y;
		to_center.k = at.z - position.z;
		along = dot_product3d(&to_center, &forward);
		across_squared = dot_product3d(&to_center, &to_center) - along * along;
		if (along < 0.f || across_squared > FORGE_MARKER_PICK_RADIUS * FORGE_MARKER_PICK_RADIUS)
			continue;
		entry = along - square_root(FORGE_MARKER_PICK_RADIUS * FORGE_MARKER_PICK_RADIUS - across_squared);
		if (entry < nearest)
		{
			nearest = entry;
			nearest_id = spawn->id;
		}
	}
	if (distance)
		*distance = nearest;
	return nearest_id;
}

/* an AI route drawn: a flat band from each point to the next (the last to
the first), in the route's color, a little over the points' markers */
static void forge_markers_render_routes(
	void)
{
	short index;

	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
	{
		struct forge_spawn const *from = &forge_globals.spawns[index];
		struct forge_spawn const *to = NULL;
		struct forge_marker marker;
		struct forge_marker_vertex vertices[6];
		real_point3d a, b, a0, a1, b0, b1;
		real_vector3d along, side, unused;
		long count = 0;
		long buffer;
		short other;
		void *locked;

		if (from->kind != _forge_spawn_ai_route)
			continue;
		for (other = (short)(index + 1); other < forge_globals.spawn_count && !to; other++)
		{
			if (forge_globals.spawns[other].kind == _forge_spawn_ai_route && forge_globals.spawns[other].channel == from->channel)
				to = &forge_globals.spawns[other];
		}
		for (other = 0; other < index && !to; other++)
		{
			if (forge_globals.spawns[other].kind == _forge_spawn_ai_route && forge_globals.spawns[other].channel == from->channel)
				to = &forge_globals.spawns[other];
		}
		if (!to)
			continue;
		forge_marker_of_spawn(from, &a, &unused);
		forge_marker_of_spawn(to, &b, &unused);
		a.z += 0.3f;
		b.z += 0.3f;
		vector_from_points3d(&a, &b, &along);
		along.k = 0.f;
		if (normalize3d(&along) == 0.f)
			continue;
		side.i = -along.j;
		side.j = along.i;
		side.k = 0.f;
		point_from_line3d(&a, &side, 0.07f, &a0);
		point_from_line3d(&a, &side, -0.07f, &a1);
		point_from_line3d(&b, &side, 0.07f, &b0);
		point_from_line3d(&b, &side, -0.07f, &b1);
		csmemset(&marker, 0, sizeof(marker));
		forge_marker_spawn_color(from, &marker);
		forge_marker_triangle(vertices, &count, &a0, &b0, &b1, forge_marker_pixel(&marker, 1.f, 0.75f));
		forge_marker_triangle(vertices, &count, &a0, &b1, &a1, forge_marker_pixel(&marker, 1.f, 0.75f));
		buffer = rasterizer_dynamic_vertices_new(_rasterizer_vertex_type_debug, count);
		if (buffer == NONE)
			continue;
		rasterizer_globals.current_lock_operation = _rasterizer_lock_debug;
		locked = rasterizer_dynamic_vertices_lock(buffer);
		if (locked)
		{
			csmemcpy(locked, vertices, (size_t)count * sizeof(vertices[0]));
			rasterizer_dynamic_vertices_unlock(buffer);
			rasterizer_debug_drawing_begin(FALSE);
			rasterizer_draw_dynamic_vertices(0, count / 3, buffer, 3);
			rasterizer_debug_drawing_end();
		}
		rasterizer_globals.current_lock_operation = _rasterizer_lock_none;
		rasterizer_dynamic_vertices_delete(buffer);
	}
}

/* (render.c's hook) every marker of the layout, and the map's own spawns */
static void forge_markers_render(
	void)
{
	long under;
	short index;
	short drawn = 0;

	if (!forge_markers_shown())
	{
		forge_globals.markers_drawn = 0;
		forge_globals.map_markers_drawn = 0;
		return;
	}
	under = forge_globals.held_marker_id ? NONE : forge_marker_under_crosshair(NULL);
	/* (the player spawn points, in their team's color) */
	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE && drawn < FORGE_MARKER_MOST_DRAWN; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];
		struct forge_marker marker;
		real_vector3d to_marker;

		csmemset(&marker, 0, sizeof(marker));
		marker.kind = _forge_marker_player_spawn;
		/* (an initial spawn's, a teleporter sender's: a second ring and a
		filled center) */
		marker.initial = spawn->kind == _forge_spawn_initial || spawn->kind == _forge_spawn_teleporter_sender ||
			spawn->kind == _forge_spawn_flag || spawn->kind == _forge_spawn_oddball;
		forge_marker_of_spawn(spawn, &marker.position, &marker.forward);
		to_marker.i = marker.position.x - render.camera.position.x;
		to_marker.j = marker.position.y - render.camera.position.y;
		to_marker.k = marker.position.z - render.camera.position.z;
		forge_marker_spawn_color(spawn, &marker);
		marker.lit = spawn->id == under || spawn->id == forge_globals.held_marker_id;
		/* (a volume: its wireframe, whenever any of it is in view; its
		marker at its floor's middle, where the crosshair takes it) */
		if (forge_spawn_is_volume(spawn))
		{
			real radius = forge_volume_bounding_radius(spawn);

			if (!render_frustum_sphere_visible(&render.frustum, &marker.position, radius))
				continue;
			forge_volume_draw(spawn, &marker);
			drawn++;
		}
		if (spawn->kind == _forge_spawn_ai_area)
			marker.area_radius = forge_ai_area_radii[PIN(spawn->option, 0, FORGE_FILE_AI_AREA_SIZES - 1)];
		if (dot_product3d(&to_marker, &render.camera.forward) < -FORGE_MARKER_INITIAL_HEIGHT ||
			!render_frustum_sphere_visible(&render.frustum, &marker.position, FORGE_MARKER_INITIAL_HEIGHT))
		{
			continue;
		}
		forge_marker_draw(&marker);
		drawn++;
	}
	forge_globals.markers_drawn = drawn;
	/* (each AI route's points joined in their order, the last to the first:
	forge_ai_move.inc) */
	forge_markers_render_routes();
	/* (the map's own player spawns, gray, read only: near ones) */
	forge_globals.map_markers_drawn = 0;
	if (global_scenario_index != NONE)
	{
		struct scenario *scenario = global_scenario_get();
		short map_drawn = 0;

		for (index = 0; scenario && index < scenario->players.count && drawn < FORGE_MARKER_MOST_DRAWN &&
			map_drawn < FORGE_MARKER_MOST_MAP_DRAWN; index++)
		{
			struct player_starting_location const *location =
				TAG_BLOCK_GET_ELEMENT(&scenario->players, index, struct player_starting_location);
			struct forge_marker marker;
			real_vector3d to_marker;

			csmemset(&marker, 0, sizeof(marker));
			marker.kind = _forge_marker_map_spawn;
			marker.position = location->position;
			marker.forward.i = cosine(location->facing);
			marker.forward.j = sine(location->facing);
			marker.forward.k = 0.f;
			to_marker.i = marker.position.x - render.camera.position.x;
			to_marker.j = marker.position.y - render.camera.position.y;
			to_marker.k = marker.position.z - render.camera.position.z;
			if (dot_product3d(&to_marker, &to_marker) > FORGE_SPAWN_MAP_DRAW_DISTANCE * FORGE_SPAWN_MAP_DRAW_DISTANCE ||
				dot_product3d(&to_marker, &render.camera.forward) < -FORGE_MARKER_HEIGHT ||
				!render_frustum_sphere_visible(&render.frustum, &marker.position, FORGE_MARKER_HEIGHT))
			{
				continue;
			}
			marker.red = marker.green = marker.blue = 0.6f;
			forge_marker_draw(&marker);
			drawn++;
			map_drawn++;
		}
		forge_globals.map_markers_drawn = map_drawn;
	}
}

/* a marker taken (RT on it): held where it is along the crosshair, facing
as it faces */
static void forge_marker_grab(
	long id,
	real distance)
{
	struct forge_spawn *spawn = forge_spawn_of_id(id);

	if (!spawn)
		return;
	forge_globals.held_marker_id = id;
	forge_globals.held_marker_position = spawn->home.position;
	forge_globals.held_forward = spawn->home.forward;
	forge_globals.held_up = *global_up3d;
	forge_globals.held_distance = PIN(distance + FORGE_MARKER_PICK_RADIUS, FORGE_MINIMUM_HOLD_DISTANCE,
		FORGE_MAXIMUM_HOLD_DISTANCE);
	forge_spawn_log("grabbed", spawn);
}

/* (each update while one is held) where the camera holds it: on the ground
under the crosshair's point, as an object let go would fall */
static void forge_marker_held_update(
	void)
{
	real_point3d position;
	real_vector3d forward;
	real_vector3d down;
	struct collision_result collision;
	/* (what a marker is put on: the structure and the solid objects - scenery,
	machines, controls -, so that it stands on a crate or a ledge's object where
	the crosshair meets it, as an object put there would; never a PHASED
	object, which a player falls through, nor the monitor's own unit) */
	unsigned long flags = FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
		FLAG(_collision_test_objects_machines_bit) | FLAG(_collision_test_objects_controls_bit);
	long player_index = local_player_get_player_index(forge_globals.local_player_index);
	long ignored = forge_globals.unit_index != NONE ? forge_globals.unit_index :
		player_index != NONE ? player_get(player_index)->unit_index : NONE;

	if (!forge_spawn_of_id(forge_globals.held_marker_id))
	{
		forge_globals.held_marker_id = 0;
		return;
	}
	if (!forge_camera_get(&position, &forward))
		return;
	/* (along the crosshair as far as it is held, or to the ground or wall
	the crosshair meets first) */
	{
		real_vector3d along;

		along.i = forward.i * forge_globals.held_distance;
		along.j = forward.j * forge_globals.held_distance;
		along.k = forward.k * forge_globals.held_distance;
		if (collision_test_vector(flags, &position, &along, ignored, &collision))
		{
			real t = MAX(collision.t * forge_globals.held_distance - 0.1f, 0.f);

			position.x += forward.i * t;
			position.y += forward.j * t;
			position.z += forward.k * t;
		}
		else
		{
			position.x += along.i;
			position.y += along.j;
			position.z += along.k;
		}
	}
	{
		real_vector3d facing = forge_globals.held_forward;
		real_vector3d up = forge_globals.held_up;

		forge_snapped(&position, &facing, &up);
	}
	/* (stood on what is under it, within a few meters) */
	down.i = 0.f;
	down.j = 0.f;
	down.k = -4.f;
	if (collision_test_vector(flags, &position, &down, ignored, &collision))
	{
		/* (a little over it, never in it) */
		position.z += down.k * collision.t + FORGE_MARKER_CLEARANCE;
	}
	forge_globals.held_marker_position = position;
}

/* the marker held let go: there, upright, facing along the ground */
static void forge_marker_drop(
	void)
{
	struct forge_spawn *spawn = forge_spawn_of_id(forge_globals.held_marker_id);
	struct forge_transform home;

	forge_globals.held_marker_id = 0;
	if (!spawn)
		return;
	home.position = forge_globals.held_marker_position;
	home.forward = forge_globals.held_forward;
	home.forward.k = 0.f;
	if (normalize3d(&home.forward) < 0.0001f)
		home.forward = *global_forward3d;
	home.up = *global_up3d;
	spawn->home = home;
	forge_spawn_log("moved", spawn);
}

/* (render.c, each player's window, after the objects) Forge's markers */
void forge_render_world(
	void)
{
	if (!forge_globals.game_active)
		return;
	forge_markers_render();
}
