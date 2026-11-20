/*
H3_VEHICLES.C

Halo 3's vehicles made into this engine's, as h2_vehicles.c makes Halo 2's
(h2_level.c compiles this file after h3_weapons.c, as a part of itself: it
fills h2_vehicles.c's struct h2v_vehicle - on the rows of h2v_kinds from
H2V_H3_KIND_FIRST, Halo 3's - and h2_vehicles_write writes it like a Halo 2
vehicle on its own rig; built alone it is empty). Switch: HALO_H3_VEHICLES=1
(off by default; PC for now, the Halo 3 maps read whole as h3_animation.c's;
HALO_H3_VEHICLES_MAP=<name> the Halo 3 map read, else HALO_H3_WEAPONS_MAP's,
HALO_H3_ANIMATION_MAP's, or the first of H3_SPARTAN_MAPS holding the
vehicle).

What a Halo 3 vehicle is made of, each from the player's own MCC Halo 3 maps:
- its render model (the hlmt's mode) and its child objects' (the default
  variant's objects: the Warthog's chaingun or gauss turret, the Scorpion's
  cannon, the Wraith's mortar - their vehi's hlmt's mode, placed at the
  parent marker), read on their own bones by h3_spartan.c's reader through
  h3_weapons.c's h3w_model_add: Halo 3's rig as it is (h2_vehicles.c's own
  graph path: h2v_native_begin/finish);
- its animations: the hlmt's jmad (and each child's) opened as an
  h2_animation.h source (h3_animation_source_open), every base and overlay
  animation decoded with h2_weapons.c's codec reader into this engine's frames,
  turned into the unturned rig as h2v_graph_make turns Halo 2's; the
  stand-in's slots take them by Halo 2's names (h2v_graph_names: Halo 3 names
  its suspension, steering, velocity and aim the same);
- its physics: Halo 2's force model (h2_vehicle_physics.c) with Halo 3's
  values: the vehi's physics type block (human jeep: steering and engine with
  its gears; alien scout: speeds and slides; alien fighter: turn, speeds;
  human tank: engine and gears), its havok vehicle physics (ground friction,
  depth, damp, moving friction, slopes, bank lift and reaction, gravity,
  radius, anti-gravity and friction points at the model's markers), its
  physics model's rigid bodies (mass, damping, centre of mass, inertia) and
  their shapes (boxes and polyhedra, the Jolt hull);
- its seats (the unit's: label, marker, entry marker, flags, turnover time,
  entry radius, cone and facing angles, maximum relative velocity, camera
  marker, pitch range and camera track) and its child objects' seats (the
  gunner's), each with its camera track tag as h2_vehicles.c writes them;
- its gun: the child object's (or its own) unit weapon - the weap's barrel's
  rate, spin, error, projectile speed and gravity, its damage - on the
  stand-in's gun as a Gauss Warthog's is (h2v_vehicle values), its second
  barrel's bomb (the Banshee's) on the melee button;
- its damage: the hlmt's damage info's vitality, body and shield values.

Layouts (read against the owner's zanzibar.map and sandbox.map,
~/halo-test-out/h3-anim/AUDIT.md "M-B5 probe"):
- obje: 0x14 acceleration scale, 0x30 default variant sid, 0x34 hlmt;
- unit: 0x24C weapons (16: a weap reference), 0x258 seats (0xD4: 0 flags, 4
  label, 8 marker, 0xC entry marker, 0x24 turnover time, 0x6C camera marker,
  0x78/0x7C camera pitch range, 0x80 camera tracks (16: trak reference),
  0xB4/0xB8 yaw range, 0xBC entry radius, 0xC0 cone angle, 0xC4 facing angle,
  0xC8 maximum relative velocity);
- vehi: 0x2BC flags; physics types - a block of 0 or 1 each - 0x2C0 human
  tank (0x58: 0x2C engine momentum, 0x30 maximum angular velocity, 0x34 gears,
  0x50 wheel circumference), 0x2CC human jeep (0x40: 0 overdampen cusp, 4
  exponent, 8/0xC maximum left and right turn, 0x10 turn rate, 0x14 engine
  momentum, 0x18 maximum angular velocity, 0x1C gears (0x44, Halo 2's), 0x38
  wheel circumference), 0x2D8 human boat, 0x2E4 alien scout (0x70: 0 cusp, 4
  exponent, 8..0x14 maximum forward and reverse speed, acceleration,
  deceleration, 0x18..0x24 maximum left and right slide, acceleration,
  deceleration, 0x28 specific type, 0x34 torque scale), 0x2F0 alien fighter
  (0x60: 0 cusp, 4 exponent, 8/0xC turns, 0x10 turn rate, 0x14..0x20 speeds,
  0x24/0x28 slide acceleration and deceleration, 0x34 flying torque scale),
  0x2FC turret, 0x314 vtol, 0x320 chopper (0x70: the jeep's 0x40 then
  anti-gravity rotation), 0x32C guardian; 0x338 havok vehicle physics (+0
  flags, +4 ground friction, +8 depth, +0xC damp, +0x10 moving friction,
  +0x14/+0x18 slopes, +0x1C maximum normal force, +0x20 bank lift, +0x24 bank
  reaction, +0x28 gravity scale, +0x2C radius, +0x3C anti-gravity points
  (0x4C, Halo 2's), +0x48 friction points (0x4C, Halo 2's), +0x54 phantom
  shapes); 0x39C/0x3A0 flipping angular velocity, 0x3B8 flip message sid,
  0x3BC suspension sound, 0x3CC crash sound;
- hlmt: 0 mode, 0x10 coll, 0x20 jmad, 0x30 phmo, 0x64 variants (0x30: 0 name,
  0x14 regions, 0x20 objects (0x1C: 0 parent marker, 4 child marker, 8 child
  variant, 0xC object reference)), 0x88 damage info (0xC maximum vitality);
- phmo: 4 mass, 0x58 rigid bodies (0xC0: 0 node, 2 region, 4 permutation,
  0x28/0x2C linear and angular damping, 0x58 shape type, 0x5A shape index,
  0x60 mass, 0x70 centre of mass, 0x80/0x90/0xA0 inertia rows), 0x94 boxes
  (0xE0: 0xC friction, 0x10 restitution, 0x50 half extents, 0xA0/0xB0/0xC0
  rotation rows, 0xD0 translation), 0xAC polyhedra (0xA0: 0xC friction, 0x10
  restitution, 0x78 four-vector rows, 0x80 vertex count), 0xB8 four vectors
  (0x30: four x, four y, four z), 0xC4 planes, 0xDC lists (0x60: 0x38 child
  count), 0xE8 list shapes (0x20: 0 type, 2 index);
- trak: 4 control points (0x1C: position, orientation).
*/

#ifdef H2_LEVEL_INTERNAL

/* ---------- constants */

enum
{
	H3V_OBJE_ACCELERATION_SCALE = 0x14,
	H3V_OBJE_DEFAULT_VARIANT = 0x30,
	H3V_OBJE_MODEL = 0x34,
	H3V_UNIT_WEAPONS = 0x24C,
	H3V_UNIT_SEATS = 0x258,
	H3V_SEAT_SIZE = 0xD4,
	H3V_SEAT_FLAGS = 0x0,
	H3V_SEAT_LABEL = 0x4,
	H3V_SEAT_MARKER = 0x8,
	H3V_SEAT_ENTRY_MARKER = 0xC,
	H3V_SEAT_TURNOVER = 0x24,
	H3V_SEAT_CAMERA_MARKER = 0x6C,
	H3V_SEAT_CAMERA_PITCH = 0x78,
	H3V_SEAT_CAMERA_TRACKS = 0x80,
	H3V_SEAT_YAW = 0xB4,
	H3V_SEAT_ENTRY_RADIUS = 0xBC,
	H3V_SEAT_RELATIVE_VELOCITY = 0xC8,
	H3V_VEHI_FLAGS = 0x2BC,
	H3V_VEHI_TANK = 0x2C0,
	H3V_VEHI_JEEP = 0x2CC,
	H3V_VEHI_BOAT = 0x2D8,
	H3V_VEHI_SCOUT = 0x2E4,
	H3V_VEHI_FIGHTER = 0x2F0,
	H3V_VEHI_TURRET = 0x2FC,
	H3V_VEHI_VTOL = 0x314,
	H3V_VEHI_CHOPPER = 0x320,
	H3V_VEHI_GUARDIAN = 0x32C,
	H3V_VEHI_HAVOK = 0x338,
	H3V_HAVOK_ANTIGRAV = 0x3C,
	H3V_HAVOK_FRICTION = 0x48,
	H3V_POINT_SIZE = 0x4C,
	H3V_GEAR_SIZE = 0x44,
	H3V_VEHI_FLIP = 0x39C,
	H3V_VEHI_FLIP_MESSAGE = 0x3B8,
	H3V_VEHI_SUSPENSION_SOUND = 0x3BC,
	H3V_HLMT_MODEL = 0x0,
	H3V_HLMT_COLLISION = 0x10,
	H3V_HLMT_GRAPH = 0x20,
	H3V_HLMT_PHYSICS = 0x30,
	H3V_HLMT_VARIANTS = 0x64,
	H3V_VARIANT_SIZE = 0x30,
	H3V_VARIANT_OBJECTS = 0x20,
	H3V_VARIANT_OBJECT_SIZE = 0x1C,
	H3V_HLMT_DAMAGE_INFO = 0x88,
	H3V_DAMAGE_INFO_VITALITY = 0xC,
	H3V_PHMO_MASS = 0x4,
	H3V_PHMO_BODIES = 0x58,
	H3V_BODY_SIZE = 0xC0,
	H3V_BODY_DAMPING = 0x28,
	H3V_BODY_SHAPE = 0x58,
	H3V_BODY_MASS = 0x60,
	H3V_BODY_CENTER = 0x70,
	H3V_BODY_INERTIA = 0x80,
	H3V_PHMO_BOXES = 0x94,
	H3V_BOX_SIZE = 0xE0,
	H3V_BOX_HALF = 0x50,
	H3V_BOX_ROTATION = 0xA0,
	H3V_BOX_TRANSLATION = 0xD0,
	H3V_PHMO_POLYHEDRA = 0xAC,
	H3V_POLYHEDRON_SIZE = 0xA0,
	H3V_POLYHEDRON_ROWS = 0x78,
	H3V_POLYHEDRON_VERTICES = 0x80,
	H3V_PHMO_FOUR_VECTORS = 0xB8,
	H3V_FOUR_VECTOR_SIZE = 0x30,
	/* (a list - shape type 14 - of child shapes: the lists block, each
	its child count at 0x38; the list shapes block in the lists' order,
	each its child's type and index) */
	H3V_PHMO_LISTS = 0xDC,
	H3V_LIST_SIZE = 0x60,
	H3V_LIST_COUNT = 0x38,
	H3V_PHMO_LIST_SHAPES = 0xE8,
	H3V_LIST_SHAPE_SIZE = 0x20,
	H3V_SHAPE_FRICTION = 0xC,
	H3V_SHAPE_RESTITUTION = 0x10,
	H3V_TRAK_POINTS = 0x4,
	H3V_TRAK_POINT_SIZE = 0x1C,
	H3V_MAXIMUM_PIECES = 3,
	/* (h2_vehicle_physics.c's force model types) */
	H3V_TYPE_TANK = 0,
	H3V_TYPE_JEEP = 1,
	H3V_TYPE_SCOUT = 4,
	H3V_TYPE_FIGHTER = 5,
	H3V_TYPE_TURRET = 6,
};

/* ---------- structures */

/* what a Halo 3 vehicle keeps beside h2v_vehicle's fields: its names and
the values logged for the value table */
struct h3v_vehicle
{
	char h3_name[64];           /* the vehi's last part (warthog, warthog_gauss) */
	char variant[32];
	long vehi, hlmt, child_vehi[H3V_MAXIMUM_PIECES];
	long piece_count;
	long jmad[H3V_MAXIMUM_PIECES];
	char physics_type[16];      /* jeep, scout, fighter, tank, turret, vtol, chopper */
	unsigned long flags;
	float flip[2];
	long weapon;                /* the gun's weap (NONE none) */
};

/* ---------- private code: helpers */

H2_ALIGN static boolean h3_vehicles_on(void)
{
	char const *setting = getenv("HALO_H3_VEHICLES");

	return h3w_level.on || (setting && atoi(setting) > 0);
}

H2_ALIGN static struct h3_cache *h3v_cache_open(char *why, long why_size)
{
	static char const *const maps[] = H3_SPARTAN_MAPS;
	char directory[256];
	char const *named = getenv("HALO_H3_VEHICLES_MAP");
	unsigned long i;

	if (!named)
		named = getenv("HALO_H3_WEAPONS_MAP");
	if (!named)
		named = getenv("HALO_H3_ANIMATION_MAP");
	/* (a Halo 3 level: its own map - every multiplayer map holds its own
	vehicles' tags) */
	if (h3w_level.on)
		return h3w_level.cache ? h3w_level.cache : h3_cache_open(h3w_level.directory, h3w_level.name, why, why_size);
	if (!h3_spartan_maps_directory(directory, sizeof(directory)))
	{
		snprintf(why, (size_t)why_size, "no Halo 3 maps in the content folder");
		return NULL;
	}
	for (i = 0; i < (named ? 1 : sizeof(maps) / sizeof(maps[0])); i++)
	{
		struct h3_cache *cache = h3_cache_open(directory, named ? named : maps[i], why, why_size);

		if (cache && h3_cache_find_tag(cache, 'vehi', "objects\\vehicles\\warthog\\warthog") != NONE)
			return cache;
		if (cache)
			h3_cache_close(cache);
	}
	snprintf(why, (size_t)why_size, "no Halo 3 map found holds the vehicles");
	return NULL;
}

/* a Halo 3 seat (at: its 0xD4 bytes) read into h2_vehicles.c's seat */
H2_ALIGN static boolean h3v_seat_read(struct h3_cache *cache, unsigned long at, struct h2v_seat *seat, float *turnover)
{
	char const *text;
	long tracks;
	unsigned long track_block;

	memset(seat, 0, sizeof(*seat));
	seat->track_slot = NONE;
	text = h3_cache_string(cache, h3_cache_read32(cache, at + H3V_SEAT_LABEL));
	if (!text || !text[0])
		return FALSE;
	snprintf(seat->label, sizeof(seat->label), "%s", text);
	text = h3_cache_string(cache, h3_cache_read32(cache, at + H3V_SEAT_MARKER));
	snprintf(seat->marker, sizeof(seat->marker), "%s", text ? text : "");
	seat->flags = h3_cache_read32(cache, at + H3V_SEAT_FLAGS);
	seat->entry_radius = h3_cache_readf(cache, at + H3V_SEAT_ENTRY_RADIUS);
	h3w_name(cache, h3_cache_read32(cache, at + H3V_SEAT_CAMERA_MARKER), seat->camera_marker, sizeof(seat->camera_marker));
	text = h3_cache_string(cache, h3_cache_read32(cache, at + H3V_SEAT_CAMERA_MARKER));
	if (!text || !text[0])
		seat->camera_marker[0] = 0;
	seat->camera_pitch[0] = h3_cache_readf(cache, at + H3V_SEAT_CAMERA_PITCH);
	seat->camera_pitch[1] = h3_cache_readf(cache, at + H3V_SEAT_CAMERA_PITCH + 4);
	seat->maximum_relative_velocity = h3_cache_readf(cache, at + H3V_SEAT_RELATIVE_VELOCITY);
	if (turnover)
		*turnover = h3_cache_readf(cache, at + H3V_SEAT_TURNOVER);
	/* its first camera track's control points */
	if (h3_cache_block(cache, at + H3V_SEAT_CAMERA_TRACKS, 16, &tracks, &track_block) && tracks > 0)
	{
		long track = h3_cache_reference(cache, track_block), points;
		struct h3_cache_tag t;
		unsigned long point_block;

		if (track != NONE && h3_cache_tag_get(cache, track, &t) && t.group_tag == 'trak' &&
			h3_cache_block(cache, t.address + H3V_TRAK_POINTS, H3V_TRAK_POINT_SIZE, &points, &point_block))
		{
			long n;

			for (n = 0; n < points && n < 16; n++)
			{
				int q;

				for (q = 0; q < 7; q++)
					seat->camera_points[n][q] = h3_cache_readf(cache, point_block + H3V_TRAK_POINT_SIZE * (unsigned long)n + 4 * (unsigned long)q);
			}
			seat->camera_point_count = n;
		}
	}
	return TRUE;
}

/* the gears of a block (Halo 2's 0x44) */
H2_ALIGN static void h3v_gears_read(struct h3_cache *cache, unsigned long block_at, struct h2vp_definition *p)
{
	long count, i;
	unsigned long block;

	if (!h3_cache_block(cache, block_at, H3V_GEAR_SIZE, &count, &block))
		return;
	for (i = 0; i < count && p->gear_count < H2VP_MAXIMUM_GEARS; i++)
	{
		unsigned long at = block + H3V_GEAR_SIZE * (unsigned long)i;
		struct h2vp_gear *g = &p->gears[p->gear_count++];
		int q;

		for (q = 0; q < 6; q++)
		{
			g->loaded[q] = h3_cache_readf(cache, at + 4 * q);
			g->cruising[q] = h3_cache_readf(cache, at + 0x18 + 4 * q);
		}
		g->minimum_upshift_time = h3_cache_readf(cache, at + 0x30);
		g->upshift_scale = h3_cache_readf(cache, at + 0x34);
		g->ratio = h3_cache_readf(cache, at + 0x38);
		g->minimum_downshift_time = h3_cache_readf(cache, at + 0x3C);
		g->downshift_scale = h3_cache_readf(cache, at + 0x40);
	}
}

/* a rigid body's place (its node's space, or the model's for node -1) in
the model's space: the model's node's inverse (h3w_model_add copied it) */
H2_ALIGN static void h3v_body_place(struct h2w_model const *m, long node, float const *local, float *place)
{
	int c;

	if (node >= 0 && node < m->node_count)
	{
		float const *inv = m->nodes[node].inverse;
		float d[3];

		for (c = 0; c < 3; c++)
			d[c] = local[c] - inv[9 + c];
		for (c = 0; c < 3; c++)
			place[c] = inv[3 * c] * d[0] + inv[3 * c + 1] * d[1] + inv[3 * c + 2] * d[2];
	}
	else
	{
		for (c = 0; c < 3; c++)
			place[c] = local[c];
	}
}

/* one shape of a rigid body into the hull (h2v_hull_shape_read's Halo 3
twin): type 2 a box, 4 a polyhedron */
H2_ALIGN static void h3v_hull_shape_read(struct h3_cache *cache, struct h2v_vehicle *v, struct h2vp_definition *p, unsigned long phmo,
	short type, short index, long node)
{
	struct h2vp_hull_shape *shape;
	float (*out)[3] = p->hull_vertices;
	long used = 0, s, count = 0;
	unsigned long block = 0, at;

	for (s = 0; s < p->hull_shape_count; s++)
		used += p->hull_shapes[s].vertex_count;
	if (p->hull_shape_count >= H2VP_MAXIMUM_HULL_SHAPES)
		return;
	shape = &p->hull_shapes[p->hull_shape_count];
	if (type == 2)
	{
		float half[3], axes[9], move[3];
		int c, corner;

		if (!h3_cache_block(cache, phmo + H3V_PHMO_BOXES, H3V_BOX_SIZE, &count, &block) || index < 0 || index >= count ||
			used + 8 > H2VP_MAXIMUM_HULL_VERTICES)
		{
			return;
		}
		at = block + H3V_BOX_SIZE * (unsigned long)index;
		for (c = 0; c < 3; c++)
		{
			half[c] = h3_cache_readf(cache, at + H3V_BOX_HALF + 4 * (unsigned long)c);
			move[c] = h3_cache_readf(cache, at + H3V_BOX_TRANSLATION + 4 * (unsigned long)c);
			axes[c] = h3_cache_readf(cache, at + H3V_BOX_ROTATION + 4 * (unsigned long)c);
			axes[3 + c] = h3_cache_readf(cache, at + H3V_BOX_ROTATION + 0x10 + 4 * (unsigned long)c);
			axes[6 + c] = h3_cache_readf(cache, at + H3V_BOX_ROTATION + 0x20 + 4 * (unsigned long)c);
		}
		for (corner = 0; corner < 8; corner++)
		{
			float local[3], x = corner & 1 ? half[0] : -half[0], y = corner & 2 ? half[1] : -half[1], z = corner & 4 ? half[2] : -half[2];

			for (c = 0; c < 3; c++)
				local[c] = axes[c] * x + axes[3 + c] * y + axes[6 + c] * z + move[c];
			h3v_body_place(&v->model, node, local, out[used + corner]);
		}
		shape->vertex_count = 8;
	}
	else if (type == 4)
	{
		unsigned long rows = 0;
		long row_count_all = 0, first_row = 0, q, row_count, vertices, k;

		if (!h3_cache_block(cache, phmo + H3V_PHMO_POLYHEDRA, H3V_POLYHEDRON_SIZE, &count, &block) || index < 0 || index >= count ||
			!h3_cache_block(cache, phmo + H3V_PHMO_FOUR_VECTORS, H3V_FOUR_VECTOR_SIZE, &row_count_all, &rows))
		{
			return;
		}
		for (q = 0; q < index; q++)
			first_row += h3_cache_read16(cache, block + H3V_POLYHEDRON_SIZE * (unsigned long)q + H3V_POLYHEDRON_ROWS);
		at = block + H3V_POLYHEDRON_SIZE * (unsigned long)index;
		row_count = h3_cache_read16(cache, at + H3V_POLYHEDRON_ROWS);
		vertices = (long)h3_cache_read32(cache, at + H3V_POLYHEDRON_VERTICES);
		if (row_count <= 0 || vertices < 4 || vertices > row_count * 4 || used + vertices > H2VP_MAXIMUM_HULL_VERTICES ||
			first_row + row_count > row_count_all)
		{
			platform_log("h3 vehicles: %s: physics model polyhedron %d: %ld rows, %ld vertices (row %ld of %ld): left out", v->name, (int)index,
				row_count, vertices, first_row, row_count_all);
			return;
		}
		for (k = 0; k < vertices; k++)
		{
			float local[3];
			int c;

			for (c = 0; c < 3; c++)
				local[c] = h3_cache_readf(cache, rows + H3V_FOUR_VECTOR_SIZE * (unsigned long)(first_row + k / 4) + 0x10 * (unsigned long)c +
					4 * (unsigned long)(k % 4));
			h3v_body_place(&v->model, node, local, out[used + k]);
		}
		shape->vertex_count = vertices;
	}
	else if (type == 14)
	{
		long lists, list_shapes, q, first = 0, children, c;
		unsigned long lists_at, shapes_at;

		if (!h3_cache_block(cache, phmo + H3V_PHMO_LISTS, H3V_LIST_SIZE, &lists, &lists_at) || index < 0 || index >= lists ||
			!h3_cache_block(cache, phmo + H3V_PHMO_LIST_SHAPES, H3V_LIST_SHAPE_SIZE, &list_shapes, &shapes_at))
		{
			return;
		}
		for (q = 0; q < index; q++)
			first += h3_cache_read16(cache, lists_at + H3V_LIST_SIZE * (unsigned long)q + H3V_LIST_COUNT);
		children = h3_cache_read16(cache, lists_at + H3V_LIST_SIZE * (unsigned long)index + H3V_LIST_COUNT);
		for (c = 0; c < children && first + c < list_shapes; c++)
		{
			unsigned long at = shapes_at + H3V_LIST_SHAPE_SIZE * (unsigned long)(first + c);
			short child_type = h3_cache_read16(cache, at), child_index = h3_cache_read16(cache, at + 2);

			if (child_type != 14)
				h3v_hull_shape_read(cache, v, p, phmo, child_type, child_index, node);
		}
		return;
	}
	else
	{
		platform_log("h3 vehicles: %s: physics model shape of type %d: left out of the hull", v->name, (int)type);
		return;
	}
	shape->first_vertex = used;
	shape->friction = h3_cache_readf(cache, at + H3V_SHAPE_FRICTION);
	shape->restitution = h3_cache_readf(cache, at + H3V_SHAPE_RESTITUTION);
	p->hull_shape_count++;
}

/* its physics model: mass, the rigid bodies' damping, centre of mass,
inertia and shapes (the hull) - the living bodies' (permutation 0 or a
region's only one; the Scorpion's destroyed hull is permutation 1) */
H2_ALIGN static void h3v_phmo_read(struct h3_cache *cache, struct h2v_vehicle *v, struct h2vp_definition *p, long phmo_tag)
{
	struct h3_cache_tag t;
	long bodies, b;
	unsigned long body;
	float heaviest = 0.0f, total = 0.0f, sum[3] = { 0.0f, 0.0f, 0.0f }, inertia[9];
	int c;

	memset(inertia, 0, sizeof(inertia));
	if (phmo_tag == NONE || !h3_cache_tag_get(cache, phmo_tag, &t) || t.group_tag != 'phmo')
		return;
	p->mass = h3_cache_readf(cache, t.address + H3V_PHMO_MASS);
	if (!h3_cache_block(cache, t.address + H3V_PHMO_BODIES, H3V_BODY_SIZE, &bodies, &body))
		return;
	for (b = 0; b < bodies; b++)
	{
		unsigned long at = body + H3V_BODY_SIZE * (unsigned long)b;
		short node = h3_cache_read16(cache, at), permutation = h3_cache_read16(cache, at + 4);
		float mass = h3_cache_readf(cache, at + H3V_BODY_MASS), local[3], place[3];
		float linear = h3_cache_readf(cache, at + H3V_BODY_DAMPING), angular = h3_cache_readf(cache, at + H3V_BODY_DAMPING + 4);
		short shape_type = h3_cache_read16(cache, at + H3V_BODY_SHAPE), shape_index = h3_cache_read16(cache, at + H3V_BODY_SHAPE + 2);

		if (mass <= 0.0f || permutation > 0)
			continue;
		for (c = 0; c < 3; c++)
			local[c] = h3_cache_readf(cache, at + H3V_BODY_CENTER + 4 * (unsigned long)c);
		h3v_body_place(&v->model, node, local, place);
		for (c = 0; c < 3; c++)
			sum[c] += place[c] * mass;
		total += mass;
		for (c = 0; c < 9; c++)
			inertia[c] += h3_cache_readf(cache, at + H3V_BODY_INERTIA + 0x10 * (unsigned long)(c / 3) + 4 * (unsigned long)(c % 3));
		if (mass > heaviest && linear >= 0.0f && linear < 10.0f && angular > -10.0f && angular < 10.0f)
		{
			heaviest = mass;
			p->linear_damping = linear;
			p->angular_damping = angular > 0.0f ? angular : 0.0f;
		}
		h3v_hull_shape_read(cache, v, p, t.address, shape_type, shape_index, node);
		if (getenv("HALO_H2_DEBUG"))
			platform_log("h3 vehicles: %s: rigid body %ld node %d permutation %d shape %d:%d mass %.1f damping %.3f %.3f centre %.3f %.3f %.3f",
				v->name, b, (int)node, (int)permutation, (int)shape_type, (int)shape_index, mass, linear, angular, place[0], place[1], place[2]);
	}
	if (total > 0.0f)
	{
		for (c = 0; c < 3; c++)
			p->center_of_mass[c] = sum[c] / total;
		p->has_center_of_mass = 1;
		p->hull_mass = total;
		memcpy(p->hull_center, p->center_of_mass, sizeof(p->hull_center));
		memcpy(p->hull_inertia, inertia, sizeof(p->hull_inertia));
		p->hull_linear_damping = p->linear_damping;
		p->hull_angular_damping = p->angular_damping;
	}
	/* (the physics model's own mass word is one body's - the Warthog's
	28.7 is its bumper's -: the living bodies' sum is the vehicle's) */
	if (total > 0.0f)
		p->mass = total;
}

/* the vehicle's force model values from its vehi (h2v_physics_read's Halo 3
twin): the physics type block it has, the havok vehicle physics, its points
at the model's markers, its gears */
H2_ALIGN static void h3v_physics_read(struct h3_cache *cache, struct h2v_vehicle *v, struct h3v_vehicle *h3, unsigned long vehicle)
{
	struct h2vp_definition *p = &v->physics;
	unsigned long physics = vehicle + H3V_VEHI_HAVOK, block;
	long count, i;
	int q;
	static struct { unsigned long offset; short type; char const *name; } const types[] =
	{
		{ H3V_VEHI_TANK, H3V_TYPE_TANK, "tank" }, { H3V_VEHI_JEEP, H3V_TYPE_JEEP, "jeep" },
		{ H3V_VEHI_SCOUT, H3V_TYPE_SCOUT, "scout" }, { H3V_VEHI_FIGHTER, H3V_TYPE_FIGHTER, "fighter" },
		{ H3V_VEHI_TURRET, H3V_TYPE_TURRET, "turret" }, { H3V_VEHI_VTOL, H3V_TYPE_FIGHTER, "vtol" },
		{ H3V_VEHI_CHOPPER, H3V_TYPE_JEEP, "chopper" },
	};

	memset(p, 0, sizeof(*p));
	p->magic = H2VP_MAGIC;
	p->version = H2VP_VERSION;
	p->pad = 1;     /* (a Halo 3 vehicle's: h2v_values_map_loaded leaves it alone) */
	p->vehicle_flags = h3_cache_read32(cache, vehicle + H3V_VEHI_FLAGS);
	h3->flags = p->vehicle_flags;
	p->type = -1;
	snprintf(h3->physics_type, sizeof(h3->physics_type), "none");
	for (q = 0; q < (int)(sizeof(types) / sizeof(types[0])); q++)
	{
		unsigned long at;

		if (!h3_cache_block(cache, vehicle + types[q].offset, 4, &count, &at) || count < 1)
			continue;
		p->type = types[q].type;
		snprintf(h3->physics_type, sizeof(h3->physics_type), "%s", types[q].name);
		if (types[q].offset == H3V_VEHI_JEEP || types[q].offset == H3V_VEHI_CHOPPER)
		{
			/* (steering in radians: the force model's definition keeps
			degrees, as Halo 2's tags do) */
			p->overdampen_cusp = h3_cache_readf(cache, at) * 57.29578f;
			p->overdampen_exponent = h3_cache_readf(cache, at + 4);
			p->turn_angles[0] = h3_cache_readf(cache, at + 8) * 57.29578f;
			p->turn_angles[1] = h3_cache_readf(cache, at + 0xC) * 57.29578f;
			p->turn_rate = h3_cache_readf(cache, at + 0x10) * 57.29578f;
			p->engine_momentum = h3_cache_readf(cache, at + 0x14);
			p->engine_maximum_angular_velocity = h3_cache_readf(cache, at + 0x18);
			h3v_gears_read(cache, at + 0x1C, p);
			p->wheel_circumference = h3_cache_readf(cache, at + 0x38);
			/* (Halo 3's jeep has no speed or slide fields - its engine takes
			the stick, its steering the turn rate -: the force model's speed
			state, which Halo 2's jeep slews toward speed x throttle and
			scales the brake test and the written vehicle speed by, is made
			the stick itself - bounds 1, slewed at once) */
			p->speed[0] = p->speed[1] = 1.0f;
			p->speed[2] = p->speed[3] = 1000.0f;
			p->slide[0] = p->slide[1] = 1.0f;
			p->slide[2] = p->slide[3] = 1000.0f;
			v->driving[4] = h3_cache_readf(cache, at + 8);
			v->driving[5] = h3_cache_readf(cache, at + 0xC);
			v->driving[6] = p->wheel_circumference;
			v->driving[7] = h3_cache_readf(cache, at + 0x10);
		}
		else if (types[q].offset == H3V_VEHI_SCOUT)
		{
			p->overdampen_cusp = h3_cache_readf(cache, at) * 57.29578f;
			p->overdampen_exponent = h3_cache_readf(cache, at + 4);
			for (q = 0; q < 4; q++)
			{
				p->speed[q] = h3_cache_readf(cache, at + 8 + 4 * q);
				p->slide[q] = h3_cache_readf(cache, at + 0x18 + 4 * q);
				v->driving[q] = p->speed[q];
			}
			v->has_driving = p->speed[0] > 0.0f;
			p->specific_type = h3_cache_read16(cache, at + 0x28);
			p->turn_scale = h3_cache_readf(cache, at + 0x34);
			memcpy(v->slide, p->slide, sizeof(v->slide));
			break;
		}
		else if (types[q].offset == H3V_VEHI_FIGHTER)
		{
			p->overdampen_cusp = h3_cache_readf(cache, at) * 57.29578f;
			p->overdampen_exponent = h3_cache_readf(cache, at + 4);
			p->turn_angles[0] = h3_cache_readf(cache, at + 8) * 57.29578f;
			p->turn_angles[1] = h3_cache_readf(cache, at + 0xC) * 57.29578f;
			p->turn_rate = h3_cache_readf(cache, at + 0x10) * 57.29578f;
			for (q = 0; q < 4; q++)
			{
				p->speed[q] = h3_cache_readf(cache, at + 0x14 + 4 * q);
				v->driving[q] = p->speed[q];
			}
			/* (a fighter's reverse speed is negative in Halo 3's tag; the
			force model wants the magnitude, as Halo 2's) */
			if (p->speed[1] < 0.0f)
				p->speed[1] = -p->speed[1], v->driving[1] = p->speed[1];
			v->has_driving = p->speed[0] > 0.0f;
			p->slide[0] = p->slide[1] = p->speed[0];
			p->slide[2] = h3_cache_readf(cache, at + 0x24);
			p->slide[3] = h3_cache_readf(cache, at + 0x28);
			p->flying_torque_scale = h3_cache_readf(cache, at + 0x34);
			memcpy(v->slide, p->slide, sizeof(v->slide));
			break;
		}
		else if (types[q].offset == H3V_VEHI_TANK)
		{
			p->control = 2;
			p->turn_scale = h3_cache_readf(cache, at + 8);
			p->engine_momentum = h3_cache_readf(cache, at + 0x2C);
			p->engine_maximum_angular_velocity = h3_cache_readf(cache, at + 0x30);
			h3v_gears_read(cache, at + 0x34, p);
			p->wheel_circumference = h3_cache_readf(cache, at + 0x50);
			/* (its turn: the tank block's maximum turn speeds and their
			accelerations, 0xC..0x18) */
			for (q = 0; q < 4; q++)
				p->slide[q] = h3_cache_readf(cache, at + 0xC + 4 * q);
			/* (as the jeep's: the engine takes the stick) */
			p->speed[0] = p->speed[1] = 1.0f;
			p->speed[2] = p->speed[3] = 1000.0f;
			v->driving[6] = p->wheel_circumference;
			v->driving[7] = p->slide[0] * 57.29578f;
		}
		else if (types[q].offset == H3V_VEHI_VTOL)
		{
			/* (the Hornet: no Halo 2 model flies a vtol - the fighter's with
			the vtol's speeds, a FLAGGED stand-in until a vtol model is
			written; its fields: 0x34 forward speed?, read as logged) */
			p->speed[0] = h3_cache_readf(cache, at + 0x34);
			p->speed[1] = h3_cache_readf(cache, at + 0x38);
			p->speed[2] = h3_cache_readf(cache, at + 0x3C);
			p->speed[3] = h3_cache_readf(cache, at + 0x3C);
			p->slide[0] = p->slide[1] = h3_cache_readf(cache, at + 0x40);
			p->slide[2] = p->slide[3] = h3_cache_readf(cache, at + 0x44);
			p->turn_angles[0] = 60.0f;
			p->turn_angles[1] = -60.0f;
			p->turn_rate = 360.0f;
			p->overdampen_cusp = 1.0f;
			p->overdampen_exponent = 1.0f;
			p->flying_torque_scale = 1.0f;
			for (q = 0; q < 4; q++)
				v->driving[q] = p->speed[q];
			v->has_driving = p->speed[0] > 0.0f;
		}
		else if (types[q].offset == H3V_VEHI_TURRET)
		{
			p->has_yaw_limits = 0;
		}
		break;
	}
	p->physics_flags = h3_cache_read32(cache, physics);
	p->ground_friction = h3_cache_readf(cache, physics + 0x4);
	p->ground_depth = h3_cache_readf(cache, physics + 0x8);
	p->ground_damp = h3_cache_readf(cache, physics + 0xC);
	p->ground_moving_friction = h3_cache_readf(cache, physics + 0x10);
	p->slope[0] = h3_cache_readf(cache, physics + 0x14);
	p->slope[1] = h3_cache_readf(cache, physics + 0x18);
	p->bank_lift = h3_cache_readf(cache, physics + 0x20);
	p->bank_reaction = h3_cache_readf(cache, physics + 0x24);
	p->gravity_scale = h3_cache_readf(cache, physics + 0x28);
	p->radius = h3_cache_readf(cache, physics + 0x2C);
	if (h3_cache_block(cache, physics + H3V_HAVOK_ANTIGRAV, H3V_POINT_SIZE, &count, &block))
	{
		for (i = 0; i < count && p->antigrav_count < H2VP_MAXIMUM_POINTS; i++)
		{
			unsigned long at = block + H3V_POINT_SIZE * (unsigned long)i;
			struct h2vp_antigrav_point *a = &p->antigrav[p->antigrav_count];
			char name[64];

			h3w_name(cache, h3_cache_read32(cache, at), name, sizeof(name));
			if (!h2v_marker_place(&v->model, name, a->position))
			{
				platform_log("h3 vehicles: %s: no marker '%s' for its anti-gravity point: left out", v->name, name);
				continue;
			}
			a->flags = h3_cache_read32(cache, at + 4);
			a->strength = h3_cache_readf(cache, at + 0x8);
			a->height = h3_cache_readf(cache, at + 0x10);
			a->damp = h3_cache_readf(cache, at + 0x14);
			a->normal_k1 = h3_cache_readf(cache, at + 0x18);
			a->normal_k0 = h3_cache_readf(cache, at + 0x1C);
			a->radius = h3_cache_readf(cache, at + 0x20);
			a->error = h3_cache_readf(cache, at + 0x38);
			p->antigrav_count++;
		}
	}
	if (h3_cache_block(cache, physics + H3V_HAVOK_FRICTION, H3V_POINT_SIZE, &count, &block))
	{
		for (i = 0; i < count && p->friction_count < H2VP_MAXIMUM_POINTS; i++)
		{
			unsigned long at = block + H3V_POINT_SIZE * (unsigned long)i;
			struct h2vp_friction_point *f = &p->friction[p->friction_count];
			char name[64];

			h3w_name(cache, h3_cache_read32(cache, at), name, sizeof(name));
			if (!h2v_marker_place(&v->model, name, f->position))
			{
				platform_log("h3 vehicles: %s: no marker '%s' for its friction point: left out", v->name, name);
				continue;
			}
			f->flags = h3_cache_read32(cache, at + 4);
			f->fraction = h3_cache_readf(cache, at + 0x8);
			f->radius = h3_cache_readf(cache, at + 0xC);
			f->moving_velocity_difference = h3_cache_readf(cache, at + 0x18);
			f->e_brake_moving_friction = h3_cache_readf(cache, at + 0x1C);
			f->e_brake_friction = h3_cache_readf(cache, at + 0x20);
			f->e_brake_moving_velocity_difference = h3_cache_readf(cache, at + 0x24);
			p->friction_count++;
		}
	}
	/* (its driver's seat's turnover time) */
	if (h3_cache_block(cache, vehicle + H3V_UNIT_SEATS, H3V_SEAT_SIZE, &count, &block))
	{
		for (i = 0; i < count; i++)
		{
			char const *label = h3_cache_string(cache, h3_cache_read32(cache, block + H3V_SEAT_SIZE * (unsigned long)i + H3V_SEAT_LABEL));
			size_t length;

			if (label && (length = strlen(label)) > 2 && !strcmp(label + length - 2, "_d"))
			{
				p->turnover_time = h3_cache_readf(cache, block + H3V_SEAT_SIZE * (unsigned long)i + H3V_SEAT_TURNOVER);
				break;
			}
		}
	}
	h3->flip[0] = h3_cache_readf(cache, vehicle + H3V_VEHI_FLIP);
	h3->flip[1] = h3_cache_readf(cache, vehicle + H3V_VEHI_FLIP + 4);
	memcpy(v->flip, h3->flip, sizeof(v->flip));
	v->acceleration_scale = h3_cache_readf(cache, vehicle + H3V_OBJE_ACCELERATION_SCALE);
	p->seat_entrance_scale = p->seat_exit_scale = 1.0f;
	p->weight_at_body = h2v_kinds[v->kind].own_body;
	/* (a rider's mass: Halo 2's Spartan's, the level's; the kit's 1 if none) */
	p->rider_mass = 1.0f;
	{
		long biped = h2v_tag_named(h2v.cache_for_names, 'bipd', "objects\\characters\\masterchief\\masterchief_mp");
		struct h2_cache_tag t;

		if (biped != NONE && h2_cache_tag_get(h2v.cache_for_names, biped, &t))
			p->rider_mass = h2w_read_real(h2v.cache_for_names, t.address + 0x274);
	}
}

/* the gun: a vehi's unit weapon (its 0x24C block's weap) read into the
h2v_vehicle gun values a Gauss Warthog's are (h2v_vehicle_make's gun part),
with h3_weapons.c's weap, proj and jpt! layouts */
H2_ALIGN static void h3v_gun_read(struct h3_cache *cache, struct h2v_vehicle *v, struct h3v_vehicle *h3, long weapon)
{
	struct h3_cache_tag t, pt, jt;
	long count, trigger_count;
	unsigned long barrels, triggers, barrel;
	short primary = 0, secondary = NONE;

	h3->weapon = weapon;
	if (weapon == NONE || !h3_cache_tag_get(cache, weapon, &t) || t.group_tag != 'weap' ||
		!h3_cache_block(cache, t.address + H3W_WEAP_BARRELS, H3W_BARREL_SIZE, &count, &barrels) || count < 1)
	{
		return;
	}
	if (h3_cache_block(cache, t.address + H3W_WEAP_TRIGGERS, H3W_TRIGGER_SIZE, &trigger_count, &triggers) && trigger_count > 0)
	{
		primary = h3_cache_read16(cache, triggers + 8);
		secondary = h3_cache_read16(cache, triggers + H3W_TRIGGER_SECONDARY_BARREL);
		if (trigger_count > 1 && secondary == NONE)
			secondary = h3_cache_read16(cache, triggers + H3W_TRIGGER_SIZE + 8);
	}
	if (primary < 0 || primary >= count)
		primary = 0;
	barrel = barrels + H3W_BARREL_SIZE * (unsigned long)primary;
	v->gun_rounds_per_second = h3_cache_readf(cache, barrel + 4);
	v->gun_rounds_per_second_maximum = h3_cache_readf(cache, barrel + 8);
	v->gun_rate_times[0] = h3_cache_readf(cache, barrel + 0xC);
	v->gun_rate_times[1] = h3_cache_readf(cache, barrel + 0x10);
	v->gun_illumination_recovery = h3_cache_readf(cache, barrel + 0xE0);
	v->gun_error[0] = h3_cache_readf(cache, barrel + 0x80);
	v->gun_error[1] = h3_cache_readf(cache, barrel + 0x84);
	if (v->gun_rounds_per_second <= 0.0f && h3_cache_readf(cache, barrel + 0x20) > 0.0f)
		v->gun_rounds_per_second = 1.0f / h3_cache_readf(cache, barrel + 0x20);
	{
		long projectile = h3_cache_reference(cache, barrel + H3W_BARREL_PROJECTILE);

		if (projectile != NONE && h3_cache_tag_get(cache, projectile, &pt) && pt.group_tag == 'proj')
		{
			long damage = h3_cache_reference(cache, pt.address + H3W_PROJ_IMPACT_DAMAGE);
			long detonation = h3_cache_reference(cache, pt.address + H3W_PROJ_DETONATION_DAMAGE);

			v->gun_velocity = h3_cache_readf(cache, pt.address + H3W_PROJ_VELOCITY);
			v->gun_gravity = h3_cache_readf(cache, pt.address + H3W_PROJ_AIR_GRAVITY);
			v->gun_impact = damage != NONE;
			if (damage == NONE)
				damage = detonation;
			if (damage != NONE && h3_cache_tag_get(cache, damage, &jt) && jt.group_tag == 'jpt!')
			{
				v->gun_radius[0] = h3_cache_readf(cache, jt.address + H3W_JPT_RADIUS);
				v->gun_radius[1] = h3_cache_readf(cache, jt.address + H3W_JPT_RADIUS + 4);
				v->gun_damage[0] = h3_cache_readf(cache, jt.address + H3W_JPT_DAMAGE);
				v->gun_damage[1] = h3_cache_readf(cache, jt.address + H3W_JPT_DAMAGE + 4);
				v->gun_damage[2] = h3_cache_readf(cache, jt.address + H3W_JPT_DAMAGE + 8);
			}
			platform_log("h3 vehicles: %s: gun %s: %.2f..%.2f a second (spin %.2f/%.2f s), error %.2f..%.2f deg, %s at %.1f wu/s gravity %.2f, "
				"%s %s damage %.1f..%.1f radius %.2f..%.2f",
				v->name, tag_short_name(h3_cache_tag_name(cache, weapon)), v->gun_rounds_per_second, v->gun_rounds_per_second_maximum,
				v->gun_rate_times[0], v->gun_rate_times[1], v->gun_error[0] * 57.29578f, v->gun_error[1] * 57.29578f,
				tag_short_name(h3_cache_tag_name(cache, projectile)), v->gun_velocity, v->gun_gravity, v->gun_impact ? "impact" : "detonation",
				damage != NONE ? tag_short_name(h3_cache_tag_name(cache, damage)) : "none", v->gun_damage[0], v->gun_damage[2], v->gun_radius[0],
				v->gun_radius[1]);
		}
	}
	/* (a second barrel of its own projectile - the Banshee's fuel rod bomb -:
	on the melee button, the kit rocket's bolt with Halo 3's speed, gravity
	and guided turn; not the Scorpion's second trigger, its stand-in's own) */
	if (secondary >= 0 && secondary < count && secondary != primary && !getenv("HALO_H2_NO_SECOND_TRIGGER") &&
		strcmp(h2v_kinds[v->kind].base, "vehicles\\scorpion\\scorpion_mp"))
	{
		unsigned long barrel2 = barrels + H3W_BARREL_SIZE * (unsigned long)secondary;
		long projectile2 = h3_cache_reference(cache, barrel2 + H3W_BARREL_PROJECTILE);

		if (projectile2 != NONE && h3_cache_tag_get(cache, projectile2, &pt) && pt.group_tag == 'proj')
		{
			float recovery = h3_cache_readf(cache, barrel2 + 0x20);

			v->gun2_rate = h3_cache_readf(cache, barrel2 + 4);
			if (v->gun2_rate <= 0.0f && recovery > 0.0f)
				v->gun2_rate = 1.0f / recovery;
			v->gun2_velocity = h3_cache_readf(cache, pt.address + H3W_PROJ_VELOCITY);
			v->gun2_gravity = h3_cache_readf(cache, pt.address + H3W_PROJ_AIR_GRAVITY);
			v->gun2_guided = h3_cache_readf(cache, pt.address + H3W_PROJ_GUIDED);
			/* (its damage Halo 3's: h2w_projectile_write takes a Halo 2 tag;
			the kit rocket's detonation stands in - flagged) */
			v->gun2_projectile = NONE;
			platform_log("h3 vehicles: %s: its second barrel %.2f a second, %s at %.1f wu/s, gravity %.2f, guided %.1f deg/s (its damage the kit rocket's: flagged)",
				v->name, v->gun2_rate, tag_short_name(h3_cache_tag_name(cache, projectile2)), v->gun2_velocity, v->gun2_gravity, v->gun2_guided * 57.29578f);
		}
	}
}

/* ---------- private code: the graph */

/* the vehicle's graph: each piece's jmad through the Halo 3 source, every
base and overlay animation decoded and turned into the unturned rig as
h2v_graph_make turns Halo 2's (the same arithmetic over the source's reads) */
H2_ALIGN static struct h2w_graph *h3v_graph_make(struct build *build, struct h3_cache *cache, struct h2v_vehicle *v, struct h3v_vehicle *h3)
{
	struct h2w_graph *graph = calloc(1, sizeof(*graph));
	long n, p;

	if (!graph)
		return NULL;
	graph->node_count = v->model.node_count;
	for (n = 0; n < graph->node_count; n++)
	{
		long c;

		snprintf(graph->nodes[n].name, sizeof(graph->nodes[n].name), "%s", v->model.nodes[n].name);
		graph->nodes[n].parent = v->model.nodes[n].parent;
		graph->nodes[n].child = graph->nodes[n].sibling = NONE;
		graph->nodes[n].source = (short)n;
		for (c = 0; c < graph->node_count && graph->nodes[n].child == NONE; c++)
		{
			if (c != n && v->model.nodes[c].parent == n)
				graph->nodes[n].child = (short)c;
		}
		for (c = n + 1; c < graph->node_count; c++)
		{
			if (v->model.nodes[c].parent == v->model.nodes[n].parent)
			{
				graph->nodes[n].sibling = (short)c;
				break;
			}
		}
	}
	for (p = 0; p < h3->piece_count; p++)
	{
		struct h2_graph_source source;
		struct h3_animation_source *opened;
		struct h2_graph_layout const *layout;
		char why[H3_CACHE_WHY_SIZE];
		unsigned long group_tag, address, nodes, animations, screens = 0;
		long node_count, animation_count, screen_count = 0, i;
		short map[H2W_MAXIMUM_GRAPH_NODES];

		if (h3->jmad[p] == NONE)
			continue;
		why[0] = 0;
		opened = h3_animation_source_open(cache, h3->jmad[p], &source, why, sizeof(why));
		if (!opened)
		{
			platform_log("h3 vehicles: %s: graph %s not opened: %s", v->name, h3_cache_tag_name(cache, h3->jmad[p]), why);
			continue;
		}
		layout = source.layout;
		if (!(source.tag_get)(source.context, h3->jmad[p], &group_tag, &address) || group_tag != 'jmad' ||
			!source.block(source.context, address + layout->nodes, layout->node_size, &node_count, &nodes) ||
			!source.block(source.context, address + layout->animations, layout->animation_size, &animation_count, &animations) ||
			node_count < 1 || node_count > H2W_MAXIMUM_GRAPH_NODES)
		{
			h3_animation_source_close(opened);
			continue;
		}
		if (!source.block(source.context, address + layout->blend_screens, layout->blend_screen_size, &screen_count, &screens))
			screen_count = 0;
		for (i = 0; i < node_count; i++)
		{
			unsigned char const *c = source.at(source.context, nodes + layout->node_size * (unsigned long)i, layout->node_size);
			char name[32];

			map[i] = NONE;
			if (!c)
				continue;
			h3w_name(cache, read32(c), name, sizeof(name));
			map[i] = h2w_model_node(&v->model, name);
		}
		for (i = 0; i < animation_count && graph->animation_count < H2W_MAXIMUM_ANIMATIONS; i++)
		{
			unsigned long entry = animations + layout->animation_size * (unsigned long)i;
			unsigned char const *e = source.at(source.context, entry, layout->animation_size);
			struct h2w_samples s, kept;
			char h3_name[48], name[48];
			int type, screen;
			short frames;
			boolean starts_at_rest;
			struct h2w_animation *a;

			if (!e || (type = e[layout->a_type]) > 1)
				continue;
			frames = read16(e + layout->a_frames);
			screen = (signed char)e[layout->a_screen];
			source.string(source.context, read32(e), h3_name, sizeof(h3_name));
			snprintf(name, sizeof(name), "%ld:%s", p, h3_name);
			if (!h3w_animation_decode(&source, entry, node_count, frames, &s))
			{
				platform_log("h3 vehicles: %s: animation %s not decoded (%d frames)", v->name, name, (int)frames);
				continue;
			}
			starts_at_rest = TRUE;
			for (n = 0; n < node_count; n++)
			{
				if (s.rotation[n] && s.rotation_animated[n] && fabsf(s.rotation[n][3]) < 0.9995f)
					starts_at_rest = FALSE;
			}
			memset(&kept, 0, sizeof(kept));
			kept.frames = s.frames;
			for (n = 0; n < node_count; n++)
			{
				short m = map[n];
				struct h2v_native_node const *o;
				long f;

				if (m < 0 || m >= graph->node_count || kept.rotation[m] || kept.translation[m] || kept.scale[m])
					continue;
				o = &v->native[m];
				if (s.rotation[n])
				{
					float wt[9];

					h2v_matrix_transpose(o->W, wt);
					for (f = 0; f < s.frames; f++)
					{
						float r[9];
						float *q = s.rotation[n] + 4 * f;

						h2v_quaternion_matrix(q, v->convention, r);
						if (type == 1)
							h2v_matrix_multiply(o->W, r, r);
						else
							h2v_matrix_multiply(o->Wp, r, r);
						h2v_matrix_multiply(r, wt, r);
						h2v_matrix_quaternion(r, v->convention, q);
					}
					kept.rotation[m] = s.rotation[n], s.rotation[n] = NULL;
					kept.rotation_animated[m] = s.rotation_animated[n];
				}
				if (s.translation[n])
				{
					for (f = 0; f < s.frames; f++)
					{
						float *t = s.translation[n] + 3 * f, d[3];
						int c;

						for (c = 0; c < 3; c++)
							d[c] = type == 1 ? t[c] : t[c] - o->Dt[c];
						for (c = 0; c < 3; c++)
							t[c] = (type == 1 ? 0.0f : o->t[c]) + o->Wp[3 * c] * d[0] + o->Wp[3 * c + 1] * d[1] + o->Wp[3 * c + 2] * d[2];
					}
					kept.translation[m] = s.translation[n], s.translation[n] = NULL;
					kept.translation_animated[m] = s.translation_animated[n];
				}
				if (s.scale[n])
				{
					kept.scale[m] = s.scale[n], s.scale[n] = NULL;
					kept.scale_animated[m] = s.scale_animated[n];
				}
			}
			h2w_samples_free(&s);
			a = &graph->animations[graph->animation_count];
			if (!h2w_animation_make(graph, &v->model, &kept, type == 1 ? _animation_overlay : _animation_base, name, a))
			{
				platform_log("h3 vehicles: %s: animation %s not made", v->name, name);
				h2w_samples_free(&kept);
				continue;
			}
			a->slot = NONE;
			a->sound = a->graph_sound = NONE;
			a->second_key_frame = NONE;
			v->has_screen[graph->animation_count] = FALSE;
			if (screen >= 0 && screen < screen_count)
			{
				unsigned char const *b = source.at(source.context, screens + layout->blend_screen_size * (unsigned long)screen, layout->blend_screen_size);

				if (b)
				{
					memcpy(v->screens[graph->animation_count], b + 4, 0x18);
					v->has_screen[graph->animation_count] = TRUE;
				}
			}
			graph->animation_count++;
			if (type == 0 && (!strcmp(h3_name, "combat:opening") || !strcmp(h3_name, "combat:closing")) && !getenv("HALO_H2_NO_HATCH"))
			{
				boolean closing = !strcmp(h3_name, "combat:closing");

				if (closing ? starts_at_rest && !v->physics.has_hatch : v->physics.has_hatch != 2)
				{
					h2v_hatch_make(graph, v, &kept, p);
					if (v->physics.has_hatch && closing)
						v->physics.has_hatch = 2;
					platform_log("h3 vehicles: %s: its hatch the %s's overlay", v->name, h3_name);
				}
				else
					v->physics.hatch_seconds[1] = (float)kept.frames / 30.0f;
			}
			h2w_samples_free(&kept);
		}
		h3_animation_source_close(opened);
	}
	platform_log("h3 vehicles: %s: its graph Halo 3's, %ld nodes, %ld animations", v->name, graph->node_count, graph->animation_count);
	if (getenv("HALO_H2_DEBUG"))
	{
		for (n = 0; n < graph->animation_count; n++)
			platform_log("h3 vehicles: %s: animation %ld '%s' type %d, %d frames%s", v->name, n, graph->animations[n].name,
				(int)graph->animations[n].type, (int)graph->animations[n].frame_count, v->has_screen[n] ? ", a screen" : "");
	}
	if (!graph->animation_count)
	{
		h2w_graph_free(graph);
		return NULL;
	}
	return graph;
}

/* ---------- private code: the vehicle */

/* a Halo 3 vehicle of h2v_kinds' row kind (an H3 row) made; NULL if it
cannot be. h3 (the caller's) keeps what the value table reads */
/* a kind's vehi in the level made: the kind's own path, or - a level whose
palette has only its snow one (Avalanche's warthog_snow, ghost_snow,
mongoose_snow, scorpion_snow: the same vehicle in another variant's maps) -
that one. Before, a snow vehicle was no kind: the kit's Halo CE stand-in
stood there, or nothing (the Scorpion). HALO_H3_VEHICLE_SNOW=0: as before */
static char h3v_kind_paths[24][128];

H2_ALIGN static char const *h3v_kind_path(long kind)
{
	long slot = kind - H2V_H3_KIND_FIRST;
	char const *base = h2v_kinds[kind].pieces[0].render_model;
	char const *setting = getenv("HALO_H3_VEHICLE_SNOW");

	if (slot < 0 || slot >= 24 || !h3w_level.on || h3w_level_names(base) || (setting && !atoi(setting)))
		return base;
	snprintf(h3v_kind_paths[slot], sizeof(h3v_kind_paths[slot]), "%s_snow", base);
	return h3w_level_names(h3v_kind_paths[slot]) ? h3v_kind_paths[slot] : base;
}

H2_ALIGN static struct h2v_vehicle *h3v_vehicle_make(struct build *build, struct h3_cache *cache, long kind, struct h3v_vehicle *h3)
{
	struct h2v_kind const *k = &h2v_kinds[kind];
	struct h2v_vehicle *v = calloc(1, sizeof(*v));
	struct h3_cache_tag vt, ht, ct;
	long vehi, hlmt, mode, i;
	long count;
	unsigned long block;
	char const *variant;

	if (!v)
		return NULL;
	memset(h3, 0, sizeof(*h3));
	h3->weapon = NONE;
	for (i = 0; i < H3V_MAXIMUM_PIECES; i++)
		h3->child_vehi[i] = h3->jmad[i] = NONE;
	v->kind = kind;
	v->base_vehicle = NONE;
	v->blip_size = -1;
	v->gun_sound = NONE;
	v->gun_damage_tag = v->gun_detonation_tag = v->gun2_impact_tag = v->gun2_projectile = NONE;
	v->h2_collision = v->h2_render_model = NONE;
	snprintf(v->name, sizeof(v->name), "%s", k->collection);
	snprintf(h3->h3_name, sizeof(h3->h3_name), "%s", tag_short_name(k->pieces[0].render_model));
	vehi = h3_cache_find_tag(cache, 'vehi', h3v_kind_path(kind));
	if (vehi == NONE || !h3_cache_tag_get(cache, vehi, &vt))
	{
		platform_log("h3 vehicles: %s: not in %s", h3v_kind_path(kind), h3_cache_name(cache));
		free(v);
		return NULL;
	}
	h3->vehi = vehi;
	hlmt = h3_cache_reference(cache, vt.address + H3V_OBJE_MODEL);
	h3->hlmt = hlmt;
	variant = h3_cache_string(cache, h3_cache_read32(cache, vt.address + H3V_OBJE_DEFAULT_VARIANT));
	snprintf(h3->variant, sizeof(h3->variant), "%s", variant && variant[0] ? variant : "default");
	if (hlmt == NONE || !h3_cache_tag_get(cache, hlmt, &ht) || (mode = h3_cache_reference(cache, ht.address + H3V_HLMT_MODEL)) == NONE)
	{
		platform_log("h3 vehicles: %s: no model", v->name);
		free(v);
		return NULL;
	}
	h3->jmad[0] = h3_cache_reference(cache, ht.address + H3V_HLMT_GRAPH);
	h2w_model_begin(&v->model);
	v->model.regioned = FALSE;
	/* the vehicle's own model */
	if (!h3w_model_add(build, &v->model, cache, mode, NULL, NULL))
	{
		platform_log("h3 vehicles: %s: its model could not be read: the stand-in stays", v->name);
		free(v);
		return NULL;
	}
	h3->piece_count = 1;
	/* its child objects (the default variant's: a turret at its parent
	marker), each its vehi's hlmt's mode and jmad; a gunner's seats read
	later from its vehi */
	if (h3_cache_block(cache, ht.address + H3V_HLMT_VARIANTS, H3V_VARIANT_SIZE, &count, &block))
	{
		for (i = 0; i < count; i++)
		{
			unsigned long at = block + H3V_VARIANT_SIZE * (unsigned long)i, objects;
			long object_count, o;
			char const *name = h3_cache_string(cache, h3_cache_read32(cache, at));

			if (!name || strcmp(name, h3->variant) || !h3_cache_block(cache, at + H3V_VARIANT_OBJECTS, H3V_VARIANT_OBJECT_SIZE, &object_count, &objects))
				continue;
			for (o = 0; o < object_count && h3->piece_count < H3V_MAXIMUM_PIECES; o++)
			{
				unsigned long oa = objects + H3V_VARIANT_OBJECT_SIZE * (unsigned long)o;
				long child = h3_cache_reference(cache, oa + 0xC), child_hlmt, child_mode;
				char marker[32];
				float place[3];
				long before = v->model.node_count, n;

				h3w_name(cache, h3_cache_read32(cache, oa), marker, sizeof(marker));
				if (child == NONE || !h3_cache_tag_get(cache, child, &ct) || ct.group_tag != 'vehi')
					continue;
				child_hlmt = h3_cache_reference(cache, ct.address + H3V_OBJE_MODEL);
				if (child_hlmt == NONE || !h3_cache_tag_get(cache, child_hlmt, &ct) || (child_mode = h3_cache_reference(cache, ct.address + H3V_HLMT_MODEL)) == NONE)
					continue;
				if (!h2v_marker_place(&v->model, marker, place))
				{
					platform_log("h3 vehicles: %s: no marker '%s' for its %s: left out", v->name, marker, h3_cache_tag_name(cache, child));
					continue;
				}
				if (!h3w_model_add(build, &v->model, cache, child_mode, NULL, place))
					continue;
				/* (the child's root: the hull's child, at the marker - the
				positions and inverses moved by h3w_model_add's offset) */
				for (n = before; n < v->model.node_count; n++)
				{
					if (v->model.nodes[n].parent == NONE)
						v->model.nodes[n].parent = 0;
				}
				h3->child_vehi[h3->piece_count] = child;
				h3->jmad[h3->piece_count] = h3_cache_reference(cache, ct.address + H3V_HLMT_GRAPH);
				h3->piece_count++;
				platform_log("h3 vehicles: %s: its %s at '%s' (%.3f %.3f %.3f)", v->name, tag_short_name(h3_cache_tag_name(cache, child)), marker,
					place[0], place[1], place[2]);
			}
			break;
		}
	}
	/* (boarding markers as h2v_vehicle_make copies them: "<name> board") */
	if (h2_caps_boarding())
	{
		long c, marker_count = v->model.marker_count;

		for (c = 0; c < marker_count && v->model.marker_count < H2W_MAXIMUM_MARKERS; c++)
		{
			struct h2w_marker *mk = &v->model.markers[c];
			size_t length = strlen(mk->name);

			if (length > 12 && !strcmp(mk->name + length - 6, " enter") && strstr(mk->name, " b ") != NULL)
			{
				struct h2w_marker *copy = &v->model.markers[v->model.marker_count++];
				int q;

				*copy = *mk;
				snprintf(copy->name, sizeof(copy->name), "%.24s board", mk->name);
				for (q = 0; q < 3; q++)
					copy->rotation[q] = -copy->rotation[q];
			}
		}
	}
	h2v_native_begin(v);
	h2v_native_finish(v);
	if (!h2w_model_finish(build, &v->model))
	{
		platform_log("h3 vehicles: %s not made (its model could not be finished): the stand-in stays", v->name);
		free(v);
		return NULL;
	}
	/* its seats, and its children's (the gunner's) */
	if (h3_cache_block(cache, vt.address + H3V_UNIT_SEATS, H3V_SEAT_SIZE, &count, &block))
	{
		for (i = 0; i < count && v->seat_count < 16; i++)
		{
			struct h2v_seat *seat = &v->seats[v->seat_count];
			float turnover;

			if (!h3v_seat_read(cache, block + H3V_SEAT_SIZE * (unsigned long)i, seat, &turnover))
				continue;
			platform_log("h3 vehicles: %s seat %s: marker '%s' flags %lx, turnover %.2f, entry radius %.2f, maximum relative velocity %.2f, camera '%s' pitch %.1f..%.1f, %ld track points",
				v->name, seat->label, seat->marker, seat->flags, turnover, seat->entry_radius, seat->maximum_relative_velocity, seat->camera_marker,
				seat->camera_pitch[0] * 57.29578f, seat->camera_pitch[1] * 57.29578f, seat->camera_point_count);
			v->seat_count++;
		}
	}
	for (i = 1; i < h3->piece_count && v->child_seat_count < 4; i++)
	{
		long q, c;

		if (h3->child_vehi[i] == NONE || !h3_cache_tag_get(cache, h3->child_vehi[i], &ct) ||
			!h3_cache_block(cache, ct.address + H3V_UNIT_SEATS, H3V_SEAT_SIZE, &count, &block))
		{
			continue;
		}
		for (c = 0; c < count && v->child_seat_count < 4; c++)
		{
			struct h2v_seat *seat = &v->child_seats[v->child_seat_count];

			if (!h3v_seat_read(cache, block + H3V_SEAT_SIZE * (unsigned long)c, seat, NULL))
				continue;
			for (q = 0; q < v->seat_count && strcmp(v->seats[q].label, seat->label); q++)
				;
			if (q < v->seat_count)
				continue;
			platform_log("h3 vehicles: %s child seat %s (%s): marker '%s' camera '%s' pitch %.1f..%.1f, %ld track points", v->name, seat->label,
				tag_short_name(h3_cache_tag_name(cache, h3->child_vehi[i])), seat->marker, seat->camera_marker, seat->camera_pitch[0] * 57.29578f,
				seat->camera_pitch[1] * 57.29578f, seat->camera_point_count);
			if (seat->camera_point_count > 0)
				seat->track_slot = v->seat_track_count++;
			v->child_seat_count++;
		}
		/* (the gun: the first child's unit weapon) */
		if (h3->weapon == NONE && h3_cache_block(cache, ct.address + H3V_UNIT_WEAPONS, 16, &count, &block) && count > 0)
			h3v_gun_read(cache, v, h3, h3_cache_reference(cache, block));
	}
	/* (no child with a gun: its own unit weapon - the Ghost's, the Banshee's -
	unless it is a horn) */
	if (h3->weapon == NONE && h3_cache_block(cache, vt.address + H3V_UNIT_WEAPONS, 16, &count, &block) && count > 0)
	{
		long weapon = h3_cache_reference(cache, block);

		if (weapon != NONE && !strstr(h3_cache_tag_name(cache, weapon), "_horn"))
			h3v_gun_read(cache, v, h3, weapon);
	}
	for (i = 0; i < v->seat_count; i++)
	{
		if (v->seats[i].camera_point_count > 0)
			v->seats[i].track_slot = v->seat_track_count++;
	}
	/* its physics (the model's markers placed: after the model) */
	h3v_physics_read(cache, v, h3, vt.address);
	h3v_phmo_read(cache, v, &v->physics, h3_cache_reference(cache, ht.address + H3V_HLMT_PHYSICS));
	v->physics.secondary_on_melee = v->gun2_rate > 0.0f;
	/* its vitality (the hlmt's damage info) */
	if (h3_cache_block(cache, ht.address + H3V_HLMT_DAMAGE_INFO, 4, &count, &block) && count > 0)
		v->vitality = h3_cache_readf(cache, block + H3V_DAMAGE_INFO_VITALITY);
	platform_log("h3 vehicles: %s (%s, variant %s): %ld pieces, %ld nodes, %ld markers, %ld parts; %s physics type %d, mass %.1f (hull %.1f, %ld shapes, damping %.3f %.3f, centre %.3f %.3f %.3f), "
		"%ld friction points, %ld anti-gravity points, %ld gears; speeds %.2f %.2f %.2f %.2f, slides %.2f %.2f %.2f %.2f, turn %.1f..%.1f at %.1f deg/s, cusp %.2f exp %.2f, "
		"engine %.0f/%.2f, wheel %.2f; ground %.3f %.3f %.3f %.4f slopes %.3f %.3f, bank %.2f/%.2f, gravity %.2f, radius %.3f; vitality %.0f; flip %.1f..%.1f; %ld seats, %ld child seats",
		v->name, h3->h3_name, h3->variant, h3->piece_count, v->model.node_count, v->model.marker_count, build->models[v->model.model].part_count,
		h3->physics_type, (int)v->physics.type, v->physics.mass, v->physics.hull_mass, v->physics.hull_shape_count, v->physics.linear_damping,
		v->physics.angular_damping, v->physics.center_of_mass[0], v->physics.center_of_mass[1], v->physics.center_of_mass[2],
		v->physics.friction_count, v->physics.antigrav_count, v->physics.gear_count, v->physics.speed[0], v->physics.speed[1], v->physics.speed[2],
		v->physics.speed[3], v->physics.slide[0], v->physics.slide[1], v->physics.slide[2], v->physics.slide[3], v->physics.turn_angles[0],
		v->physics.turn_angles[1], v->physics.turn_rate, v->physics.overdampen_cusp, v->physics.overdampen_exponent, v->physics.engine_momentum,
		v->physics.engine_maximum_angular_velocity, v->physics.wheel_circumference, v->physics.ground_friction, v->physics.ground_depth,
		v->physics.ground_damp, v->physics.ground_moving_friction, v->physics.slope[0], v->physics.slope[1], v->physics.bank_lift,
		v->physics.bank_reaction, v->physics.gravity_scale, v->physics.radius, v->vitality, h3->flip[0], h3->flip[1], v->seat_count, v->child_seat_count);
	if (getenv("HALO_H2_DEBUG"))
	{
		for (i = 0; i < v->physics.friction_count; i++)
			platform_log("h3 vehicles: %s: friction point %ld at %.3f %.3f %.3f flags %lx fraction %.2f radius %.3f slip %.2f brake %.4f %.2f %.2f", v->name, i,
				v->physics.friction[i].position[0], v->physics.friction[i].position[1], v->physics.friction[i].position[2], v->physics.friction[i].flags,
				v->physics.friction[i].fraction, v->physics.friction[i].radius, v->physics.friction[i].moving_velocity_difference,
				v->physics.friction[i].e_brake_moving_friction, v->physics.friction[i].e_brake_friction, v->physics.friction[i].e_brake_moving_velocity_difference);
		for (i = 0; i < v->physics.antigrav_count; i++)
			platform_log("h3 vehicles: %s: anti-gravity point %ld at %.3f %.3f %.3f flags %lx strength %.2f height %.2f damp %.2f k %.2f/%.2f radius %.3f error %.2f",
				v->name, i, v->physics.antigrav[i].position[0], v->physics.antigrav[i].position[1], v->physics.antigrav[i].position[2],
				v->physics.antigrav[i].flags, v->physics.antigrav[i].strength, v->physics.antigrav[i].height, v->physics.antigrav[i].damp,
				v->physics.antigrav[i].normal_k0, v->physics.antigrav[i].normal_k1, v->physics.antigrav[i].radius, v->physics.antigrav[i].error);
		for (i = 0; i < v->physics.gear_count; i++)
			platform_log("h3 vehicles: %s: gear %ld loaded %.0f %.0f %.2f %.2f %.0f %.0f cruising %.0f %.0f %.2f %.2f %.0f %.0f shift %.2f %.2f ratio %.2f %.2f %.2f",
				v->name, i, v->physics.gears[i].loaded[0], v->physics.gears[i].loaded[1], v->physics.gears[i].loaded[2], v->physics.gears[i].loaded[3],
				v->physics.gears[i].loaded[4], v->physics.gears[i].loaded[5], v->physics.gears[i].cruising[0], v->physics.gears[i].cruising[1],
				v->physics.gears[i].cruising[2], v->physics.gears[i].cruising[3], v->physics.gears[i].cruising[4], v->physics.gears[i].cruising[5],
				v->physics.gears[i].minimum_upshift_time, v->physics.gears[i].upshift_scale, v->physics.gears[i].ratio,
				v->physics.gears[i].minimum_downshift_time, v->physics.gears[i].downshift_scale);
	}
	return v;
}

/* ---------- private code: h2_vehicles.c's hooks */

/* whether a Halo 2 kind is not made because Halo 3's vehicle of the same
collection is (HALO_H3_VEHICLES) */
H2_ALIGN static boolean h3_vehicles_replaces(long kind)
{
	long k;

	if (!h3_vehicles_on() || kind >= H2V_H3_KIND_FIRST)
		return FALSE;
	for (k = H2V_H3_KIND_FIRST; k < (long)(sizeof(h2v_kinds) / sizeof(h2v_kinds[0])); k++)
	{
		if (!strcmp(h2v_kinds[k].collection, h2v_kinds[kind].collection))
			return TRUE;
	}
	return FALSE;
}

static struct h3v_vehicle h3v_made[H2V_MAXIMUM_VEHICLES];
static long h3v_made_kind[H2V_MAXIMUM_VEHICLES];
static long h3v_made_count;

/* the Halo 3 vehicles gathered into h2v.vehicles (h2_vehicles_gather, after
its Halo 2 kinds, before the debris): those the level places (as a Halo 2
kind: h2v_placed, HALO_H2_ALL_VEHICLES every one) */
H2_ALIGN static void h3_vehicles_gather(struct build *build)
{
	struct h3_cache *cache;
	char why[H3_CACHE_WHY_SIZE];
	long k;

	h3v_made_count = 0;
	if (!h3_vehicles_on() || (!h3w_level.on && (!h2_cache_is_mcc(build->cache) || h2_level_is_main_menu(build->cache))))
		return;
	why[0] = 0;
	cache = h3v_cache_open(why, sizeof(why));
	if (!cache)
	{
		platform_log("h3 vehicles: not made: %s", why);
		return;
	}
	h3w_bitmaps.count = 0;
	for (k = H2V_H3_KIND_FIRST; k < (long)(sizeof(h2v_kinds) / sizeof(h2v_kinds[0])) && h2v.count < H2V_MAXIMUM_VEHICLES; k++)
	{
		struct h2v_vehicle *v;
		struct h3v_vehicle *h3 = &h3v_made[h3v_made_count];

		/* (the vehicles the level places: a Halo 3 level's palette's,
		else the Halo 2 level's netgame collections') */
		if (getenv("HALO_H3_TURRETS") && !atoi(getenv("HALO_H3_TURRETS")) && (!strcmp(h2v_kinds[k].collection, "machinegun_turret") ||
			!strcmp(h2v_kinds[k].collection, "plasma_cannon") || !strcmp(h2v_kinds[k].collection, "shade")))
		{
			continue;
		}
		if (!getenv("HALO_H2_ALL_VEHICLES") &&
			(h3w_level.on ? !h3w_level_names(h3v_kind_path(k)) : !h2v_placed(build->cache, h2v_kinds[k].collection)))
		{
			continue;
		}
		if (build->cache && build->gather_trim_bytes)
			h2_cache_trim_past(build->cache, build->gather_trim_bytes);
		{
			char unit[96];

			snprintf(unit, sizeof(unit), "the Halo 3 vehicle %s", h2v_kinds[k].collection);
			h2_build_heap_unit(unit);
		}
		v = h3v_vehicle_make(build, cache, k, h3);
		if (!v)
			continue;
		if (!getenv("HALO_H2_STANDIN_VEHICLE_GRAPH"))
			v->graph = h3v_graph_make(build, cache, v, h3);
		v->tag = h2_vehicles_tag_count() - h2v.blast_count;
		h3v_made_kind[h3v_made_count++] = k;
		h2v.vehicles[h2v.count++] = v;
	}
	h3w_model_kept_free();
	if (cache != h3w_level.cache)
		h3_cache_close(cache);
}

/* ---------- public code: the h3/direct-level lane's lookup */

/* the made Halo 3 vehicle a vehi tag name stands for - its full path
(objects\vehicles\warthog\warthog) or its last part (warthog) -: its tag
index from the vehicles' first (h2_vehicles_for's convention: v->tag), NONE
if none is made. Only after the gather of the level being made */
long h3_vehicles_for(char const *h3_name)
{
	long i;
	char const *slash;

	if (!h3_name || !h3_name[0])
		return NONE;
	slash = strrchr(h3_name, '\\');
	for (i = 0; i < h2v.count; i++)
	{
		struct h2v_vehicle const *v = h2v.vehicles[i];

		if (v->kind < H2V_H3_KIND_FIRST)
			continue;
		if (!strcmp(h3_name, h2v_kinds[v->kind].pieces[0].render_model) || !strcmp(slash ? slash + 1 : h3_name, tag_short_name(h2v_kinds[v->kind].pieces[0].render_model)))
			return v->tag;
		/* (its snow variant's path: the same made vehicle - h3v_kind_path) */
		{
			size_t base = strlen(h2v_kinds[v->kind].pieces[0].render_model);
			char const *setting = getenv("HALO_H3_VEHICLE_SNOW");

			if ((!setting || atoi(setting)) && !strncmp(h3_name, h2v_kinds[v->kind].pieces[0].render_model, base) && !strcmp(h3_name + base, "_snow"))
				return v->tag;
		}
	}
	return NONE;
}

#else

/* (built alone: nothing - h2_level.c compiles it) */
typedef int h3_vehicles_compiled_in_h2_level;

#endif
