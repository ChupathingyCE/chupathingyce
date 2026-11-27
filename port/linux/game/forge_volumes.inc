/*
FORGE_VOLUMES.INC

Forge's volumes (included by forge.c, for its statics): zones of a layout
that act on whoever is in them, the Forge menu's VOLUMES category - a KILL
VOLUME (killed at once, as the engine kills a unit that falls out of the
world: unit_kill, an act of god), a DAMAGE VOLUME (a share of full health a
second, shields first, then the body; at none, killed the same way) and a
GRAVITY VOLUME (gravity scaled for what is in the air in it: a jump, a
falling vehicle or weapon). Each is a box (its half width, half length and
height from its floor) or a sphere (its radius, resting on its floor),
turned by its yaw, acting on every unit or only on players (or one team's
players).

A volume is one of the layout's markers (forge_spawns.inc: placed from the
menu, carried, let go, its properties opened from it, deleted, saved as a
version 14 record of forge_files.h), drawn for the monitor alone as a
wireframe in its type's colour (red, orange, blue), never for the Spartan
or in a layout played: who plays sees nothing of it, as a map's own kill
zones show nothing. Its effects are the game's, a tick at a time
(game.c's game_tick, forge_volumes_tick): the same in a Forge game, as the
Spartan, and in a layout played, from the layout alone - nothing of the
player's settings, nothing of the frame rate. Forge's games are local: no
other machine runs them.
*/

/* ---------- constants */

/* a new volume's size (its own bytes: quarter world units) and amounts */
#define FORGE_VOLUME_DEFAULT_HALF_WIDTH 8
#define FORGE_VOLUME_DEFAULT_HALF_LENGTH 8
#define FORGE_VOLUME_DEFAULT_HEIGHT 12
#define FORGE_VOLUME_DEFAULT_RADIUS 8
#define FORGE_VOLUME_DEFAULT_DAMAGE 25
#define FORGE_VOLUME_DEFAULT_GRAVITY 50
/* the size rows' step (quarter world units: a world unit) */
#define FORGE_VOLUME_SIZE_STEP 4
/* the wireframe: a sphere's circles' sides, and the most lines a volume
draws */
#define FORGE_VOLUME_SPHERE_SIDES 24
#define FORGE_VOLUME_MOST_LINES (FORGE_VOLUME_SPHERE_SIDES * 3 + 12)

/* the AMOUNT row's choices: a damage volume's percent a second, a gravity
volume's percent of gravity */
static unsigned char const forge_volume_damages[] = { 5, 10, 25, 50, 100 };
static unsigned char const forge_volume_gravities[] = { 0, 25, 50, 75, 150, 200 };

/* ---------- private code */

static boolean forge_spawn_is_volume(
	struct forge_spawn const *spawn)
{
	return spawn && spawn->kind >= _forge_spawn_volume_kill && spawn->kind <= _forge_spawn_volume_gravity;
}

static short forge_volumes_count(
	void)
{
	short index;
	short count = 0;

	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
		count += forge_spawn_is_volume(&forge_globals.spawns[index]) ? 1 : 0;
	return count;
}

/* a volume's type's (FORGE_FILE_VOLUME_*) default amount */
static unsigned char forge_volume_default_amount(
	short kind)
{
	return (unsigned char)(kind == _forge_spawn_volume_damage ? FORGE_VOLUME_DEFAULT_DAMAGE :
		kind == _forge_spawn_volume_gravity ? FORGE_VOLUME_DEFAULT_GRAVITY : 0);
}

/* a new volume's shape, size, amount and target */
static void forge_volume_defaults(
	struct forge_spawn *spawn)
{
	spawn->volume_shape = FORGE_FILE_VOLUME_BOX;
	spawn->volume_size[0] = FORGE_VOLUME_DEFAULT_HALF_WIDTH;
	spawn->volume_size[1] = FORGE_VOLUME_DEFAULT_HALF_LENGTH;
	spawn->volume_size[2] = FORGE_VOLUME_DEFAULT_HEIGHT;
	spawn->volume_amount = forge_volume_default_amount(spawn->kind);
	spawn->volume_target = FORGE_FILE_VOLUME_EVERY_UNIT;
}

/* a version 14 record's volume (checked by the validator) as a marker's */
static void forge_volume_from_record(
	struct forge_spawn *spawn,
	struct forge_file_record const *record)
{
	spawn->volume_shape = record->params[FORGE_FILE_VOLUME_PARAM_SHAPE];
	spawn->volume_size[0] = record->params[FORGE_FILE_VOLUME_PARAM_SIZE];
	spawn->volume_size[1] = record->params[FORGE_FILE_VOLUME_PARAM_SIZE + 1];
	spawn->volume_size[2] = record->params[FORGE_FILE_VOLUME_PARAM_SIZE + 2];
	spawn->volume_amount = record->params[FORGE_FILE_VOLUME_PARAM_AMOUNT];
	spawn->volume_target = record->params[FORGE_FILE_VOLUME_PARAM_TARGET];
	spawn->team = (signed char)(record->team == 0 || record->team == 1 ? record->team : NONE);
}

/* a volume as its version 14 record (its place and yaw the caller's) */
static void forge_volume_to_record(
	struct forge_spawn const *spawn,
	struct forge_file_record *record)
{
	record->kind = _forge_record_volume;
	record->variant = (unsigned char)(spawn->kind - _forge_spawn_volume_kill);
	record->team = (signed char)(spawn->team == 0 || spawn->team == 1 ? spawn->team : -1);
	record->params[FORGE_FILE_VOLUME_PARAM_SHAPE] = spawn->volume_shape;
	record->params[FORGE_FILE_VOLUME_PARAM_SIZE] = spawn->volume_size[0];
	record->params[FORGE_FILE_VOLUME_PARAM_SIZE + 1] = (unsigned char)(spawn->volume_shape == FORGE_FILE_VOLUME_SPHERE ? 0 :
		spawn->volume_size[1]);
	record->params[FORGE_FILE_VOLUME_PARAM_SIZE + 2] = (unsigned char)(spawn->volume_shape == FORGE_FILE_VOLUME_SPHERE ? 0 :
		spawn->volume_size[2]);
	record->params[FORGE_FILE_VOLUME_PARAM_AMOUNT] = spawn->kind == _forge_spawn_volume_kill ? 0 : spawn->volume_amount;
	/* (a team's: players only) */
	record->params[FORGE_FILE_VOLUME_PARAM_TARGET] = (unsigned char)(record->team != -1 ? FORGE_FILE_VOLUME_PLAYERS :
		spawn->volume_target);
}

/* where a volume is (the one held: where it is held), its floor's middle,
and its forward along the ground */
static void forge_volume_place(
	struct forge_spawn const *spawn,
	real_point3d *base,
	real_vector3d *forward)
{
	if (spawn->id == forge_globals.held_marker_id)
	{
		*base = forge_globals.held_marker_position;
		*forward = forge_globals.held_forward;
	}
	else
	{
		*base = spawn->home.position;
		*forward = spawn->home.forward;
	}
	forward->k = 0.f;
	if (normalize3d(forward) < 0.0001f)
		*forward = *global_forward3d;
}

/* a volume's radius from its floor's middle that holds all of it (the
drawing's culling) */
static real forge_volume_bounding_radius(
	struct forge_spawn const *spawn)
{
	real width = spawn->volume_size[0] * FORGE_FILE_VOLUME_SIZE_UNIT;

	if (spawn->volume_shape == FORGE_FILE_VOLUME_SPHERE)
		return width * 2.f;
	{
		real length = spawn->volume_size[1] * FORGE_FILE_VOLUME_SIZE_UNIT;
		real height = spawn->volume_size[2] * FORGE_FILE_VOLUME_SIZE_UNIT;

		return square_root(width * width + length * length + height * height);
	}
}

/* whether a point is in a volume (its home: the game's, never the one
held) */
static boolean forge_volume_contains(
	struct forge_spawn const *spawn,
	real_point3d const *point)
{
	real_vector3d forward = spawn->home.forward;
	real_vector3d offset;
	real radius;

	offset.i = point->x - spawn->home.position.x;
	offset.j = point->y - spawn->home.position.y;
	offset.k = point->z - spawn->home.position.z;
	if (spawn->volume_shape == FORGE_FILE_VOLUME_SPHERE)
	{
		radius = spawn->volume_size[0] * FORGE_FILE_VOLUME_SIZE_UNIT;
		offset.k -= radius;
		return dot_product3d(&offset, &offset) <= radius * radius;
	}
	forward.k = 0.f;
	if (normalize3d(&forward) < 0.0001f)
		forward = *global_forward3d;
	{
		real along = offset.i * forward.i + offset.j * forward.j;
		real across = offset.j * forward.i - offset.i * forward.j;

		return offset.k >= 0.f && offset.k <= spawn->volume_size[2] * FORGE_FILE_VOLUME_SIZE_UNIT &&
			along >= -spawn->volume_size[1] * FORGE_FILE_VOLUME_SIZE_UNIT &&
			along <= spawn->volume_size[1] * FORGE_FILE_VOLUME_SIZE_UNIT &&
			across >= -spawn->volume_size[0] * FORGE_FILE_VOLUME_SIZE_UNIT &&
			across <= spawn->volume_size[0] * FORGE_FILE_VOLUME_SIZE_UNIT;
	}
}

/* whether a volume acts on a unit: every unit, or players only (of its
team, when it has one) */
static boolean forge_volume_takes_unit(
	struct forge_spawn const *spawn,
	struct unit_datum const *unit)
{
	long player_index = unit->unit.player_index;

	if (spawn->volume_target == FORGE_FILE_VOLUME_EVERY_UNIT && spawn->team == NONE)
		return TRUE;
	if (player_index == NONE)
		return FALSE;
	return spawn->team == NONE || player_get(player_index)->team_index == spawn->team;
}

/* ---- the properties: TARGET, TYPE, AMOUNT, YAW, SHAPE, WIDTH (a sphere's
RADIUS), LENGTH, HEIGHT, on the properties' rows in their order */

enum
{
	_forge_volume_row_target = _forge_property_team,
	_forge_volume_row_type = _forge_property_respawn,
	_forge_volume_row_amount = _forge_property_place_at_start,
	_forge_volume_row_yaw = _forge_property_yaw,
	_forge_volume_row_shape = _forge_property_pitch,
	_forge_volume_row_width = _forge_property_roll,
	_forge_volume_row_length = _forge_property_reset_rotation,
	_forge_volume_row_height = _forge_property_physics,
};

/* TARGET's choices in order: every unit, players, red's, blue's */
static short forge_volume_target_index(
	struct forge_spawn const *spawn)
{
	if (spawn->team == 0)
		return 2;
	if (spawn->team == 1)
		return 3;
	return spawn->volume_target == FORGE_FILE_VOLUME_PLAYERS ? 1 : 0;
}

static unsigned char forge_volume_amount_step(
	unsigned char const *choices,
	short count,
	unsigned char amount,
	short step)
{
	short index;

	for (index = 0; index < count - 1; index++)
	{
		if (choices[index] >= amount)
			break;
	}
	return choices[(index + step + count) % count];
}

/* a volume's property stepped (its rows); FALSE for none of its own */
static boolean forge_volume_property_apply(
	struct forge_spawn *spawn,
	short property,
	short step)
{
	short axis;

	switch (property)
	{
	case _forge_volume_row_target:
	{
		short target = (short)((forge_volume_target_index(spawn) + step + 4) % 4);

		spawn->team = (signed char)(target == 2 ? 0 : target == 3 ? 1 : NONE);
		spawn->volume_target = (unsigned char)(target == 0 ? FORGE_FILE_VOLUME_EVERY_UNIT : FORGE_FILE_VOLUME_PLAYERS);
		break;
	}
	case _forge_volume_row_type:
		spawn->kind = (short)(_forge_spawn_volume_kill +
			(spawn->kind - _forge_spawn_volume_kill + step + NUMBER_OF_FORGE_FILE_VOLUME_TYPES) % NUMBER_OF_FORGE_FILE_VOLUME_TYPES);
		spawn->volume_amount = forge_volume_default_amount(spawn->kind);
		break;
	case _forge_volume_row_amount:
		if (spawn->kind == _forge_spawn_volume_damage)
			spawn->volume_amount = forge_volume_amount_step(forge_volume_damages, (short)NUMBEROF(forge_volume_damages),
				spawn->volume_amount, step);
		else if (spawn->kind == _forge_spawn_volume_gravity)
			spawn->volume_amount = forge_volume_amount_step(forge_volume_gravities, (short)NUMBEROF(forge_volume_gravities),
				spawn->volume_amount, step);
		break;
	case _forge_volume_row_shape:
		spawn->volume_shape = (unsigned char)(spawn->volume_shape == FORGE_FILE_VOLUME_BOX ? FORGE_FILE_VOLUME_SPHERE :
			FORGE_FILE_VOLUME_BOX);
		/* (a box from a sphere: its length and height again) */
		for (axis = 1; axis < 3; axis++)
		{
			if (!spawn->volume_size[axis])
				spawn->volume_size[axis] = spawn->volume_size[0];
		}
		break;
	case _forge_volume_row_width:
	case _forge_volume_row_length:
	case _forge_volume_row_height:
	{
		/* (a world unit a step, from one to the most, round) */
		long size;

		axis = (short)(property == _forge_volume_row_width ? 0 : property == _forge_volume_row_length ? 1 : 2);
		if (spawn->volume_shape == FORGE_FILE_VOLUME_SPHERE && axis > 0)
			return TRUE;
		size = (long)spawn->volume_size[axis] + step * FORGE_VOLUME_SIZE_STEP;
		if (size > 255)
			size = FORGE_VOLUME_SIZE_STEP;
		else if (size < 1)
			size = 255 / FORGE_VOLUME_SIZE_STEP * FORGE_VOLUME_SIZE_STEP;
		spawn->volume_size[axis] = (unsigned char)size;
		break;
	}
	default:
		return FALSE;
	}
	forge_volume_log("properties of", spawn);
	return TRUE;
}

static void forge_volume_log(
	char const *what,
	struct forge_spawn const *spawn)
{
	platform_log("forge: %s the %s #%ld (%s, %s %.2f %.2f %.2f, amount %d, %s) at %.2f %.2f %.2f facing %.2f %.2f", what,
		forge_spawn_kind_log_names[spawn->kind % NUMBER_OF_FORGE_MARKER_KINDS], spawn->id,
		spawn->volume_shape == FORGE_FILE_VOLUME_SPHERE ? "sphere" : "box",
		spawn->volume_shape == FORGE_FILE_VOLUME_SPHERE ? "radius" : "half width, half length, height",
		spawn->volume_size[0] * FORGE_FILE_VOLUME_SIZE_UNIT, spawn->volume_size[1] * FORGE_FILE_VOLUME_SIZE_UNIT,
		spawn->volume_size[2] * FORGE_FILE_VOLUME_SIZE_UNIT, spawn->volume_amount,
		spawn->team == 0 ? "red players" : spawn->team == 1 ? "blue players" :
		spawn->volume_target == FORGE_FILE_VOLUME_PLAYERS ? "players" : "every unit", spawn->home.position.x,
		spawn->home.position.y, spawn->home.position.z, spawn->home.forward.i, spawn->home.forward.j);
}

/* a volume's rows' words (forge_menu_link.inc's forge_property_text) */
static void forge_volume_property_text(
	struct forge_properties const *properties,
	short row,
	char *label,
	int label_size,
	char *value,
	int value_size)
{
	static char const *const targets[] = { "EVERYONE", "PLAYERS", "RED PLAYERS", "BLUE PLAYERS" };
	static char const *const types[NUMBER_OF_FORGE_FILE_VOLUME_TYPES] = { "KILL", "DAMAGE", "GRAVITY" };
	boolean sphere = properties->volume_shape == FORGE_FILE_VOLUME_SPHERE;

	switch (row)
	{
	case _forge_volume_row_target:
		snprintf(label, (size_t)label_size, "%s", "TARGET");
		snprintf(value, (size_t)value_size, "%s", targets[PIN(properties->volume_target_index, 0, 3)]);
		break;
	case _forge_volume_row_type:
		snprintf(label, (size_t)label_size, "%s", "TYPE");
		snprintf(value, (size_t)value_size, "%s",
			types[PIN(properties->spawn_kind - _forge_spawn_volume_kill, 0, NUMBER_OF_FORGE_FILE_VOLUME_TYPES - 1)]);
		break;
	case _forge_volume_row_amount:
		if (properties->spawn_kind == _forge_spawn_volume_damage)
		{
			snprintf(label, (size_t)label_size, "%s", "DAMAGE");
			snprintf(value, (size_t)value_size, "%d%% / S", properties->volume_amount);
		}
		else if (properties->spawn_kind == _forge_spawn_volume_gravity)
		{
			snprintf(label, (size_t)label_size, "%s", "GRAVITY");
			snprintf(value, (size_t)value_size, "%d%%", properties->volume_amount);
		}
		else
		{
			snprintf(label, (size_t)label_size, "%s", "AMOUNT");
			snprintf(value, (size_t)value_size, "%s", "-");
		}
		break;
	case _forge_volume_row_yaw:
		snprintf(label, (size_t)label_size, "%s", "YAW");
		snprintf(value, (size_t)value_size, "%d", properties->yaw);
		break;
	case _forge_volume_row_shape:
		snprintf(label, (size_t)label_size, "%s", "SHAPE");
		snprintf(value, (size_t)value_size, "%s", sphere ? "SPHERE" : "BOX");
		break;
	case _forge_volume_row_width:
		snprintf(label, (size_t)label_size, "%s", sphere ? "RADIUS" : "WIDTH");
		snprintf(value, (size_t)value_size, "%.1f M", (sphere ? 1.f : 2.f) * properties->volume_size[0] *
			FORGE_FILE_VOLUME_SIZE_UNIT * FORGE_METRES_PER_WORLD_UNIT);
		break;
	case _forge_volume_row_length:
		snprintf(label, (size_t)label_size, "%s", "LENGTH");
		if (sphere)
			snprintf(value, (size_t)value_size, "%s", "-");
		else
			snprintf(value, (size_t)value_size, "%.1f M", 2.f * properties->volume_size[1] * FORGE_FILE_VOLUME_SIZE_UNIT *
				FORGE_METRES_PER_WORLD_UNIT);
		break;
	case _forge_volume_row_height:
		snprintf(label, (size_t)label_size, "%s", "HEIGHT");
		if (sphere)
			snprintf(value, (size_t)value_size, "%s", "-");
		else
			snprintf(value, (size_t)value_size, "%.1f M", properties->volume_size[2] * FORGE_FILE_VOLUME_SIZE_UNIT *
				FORGE_METRES_PER_WORLD_UNIT);
		break;
	}
}

static void forge_volume_properties_get(
	struct forge_spawn const *spawn,
	struct forge_properties *properties)
{
	properties->volume = TRUE;
	properties->volume_shape = spawn->volume_shape;
	properties->volume_size[0] = spawn->volume_size[0];
	properties->volume_size[1] = spawn->volume_size[1];
	properties->volume_size[2] = spawn->volume_size[2];
	properties->volume_amount = spawn->volume_amount;
	properties->volume_target_index = forge_volume_target_index(spawn);
}

/* ---- the wireframe (forge_markers.inc's drawing: the monitor's alone) */

static void forge_volume_line(
	struct forge_marker_vertex *vertices,
	long *count,
	real_point3d const *a,
	real_point3d const *b,
	unsigned long color)
{
	if (*count + 2 > FORGE_VOLUME_MOST_LINES * 2)
		return;
	vertices[*count].point = *a;
	vertices[(*count)++].color = color;
	vertices[*count].point = *b;
	vertices[(*count)++].color = color;
}

/* a volume's colour: a kill volume's red, a damage volume's orange, a
gravity volume's blue */
static void forge_volume_color(
	struct forge_spawn const *spawn,
	struct forge_marker *marker)
{
	static real const colors[NUMBER_OF_FORGE_FILE_VOLUME_TYPES][3] =
	{
		{ 1.00f, 0.22f, 0.18f },
		{ 1.00f, 0.62f, 0.15f },
		{ 0.35f, 0.62f, 1.00f },
	};
	short type = (short)PIN(spawn->kind - _forge_spawn_volume_kill, 0, NUMBER_OF_FORGE_FILE_VOLUME_TYPES - 1);

	marker->red = colors[type][0];
	marker->green = colors[type][1];
	marker->blue = colors[type][2];
}

/* a volume's edges drawn (a box's twelve, a sphere's three circles and its
floor's ring), in its colour, blended, the walls in front hiding them */
static void forge_volume_draw(
	struct forge_spawn const *spawn,
	struct forge_marker const *marker)
{
	static struct forge_marker_vertex vertices[FORGE_VOLUME_MOST_LINES * 2];
	unsigned long color = forge_marker_pixel(marker, 1.f, marker->lit ? 0.95f : 0.75f);
	real_point3d base;
	real_vector3d forward;
	real_vector3d left;
	long count = 0;
	long buffer;
	short index;

	forge_volume_place(spawn, &base, &forward);
	left.i = -forward.j;
	left.j = forward.i;
	left.k = 0.f;
	if (spawn->volume_shape == FORGE_FILE_VOLUME_SPHERE)
	{
		real radius = spawn->volume_size[0] * FORGE_FILE_VOLUME_SIZE_UNIT;
		real_point3d center = base;
		short circle;

		center.z += radius;
		for (circle = 0; circle < 3; circle++)
		{
			for (index = 0; index < FORGE_VOLUME_SPHERE_SIDES; index++)
			{
				real angle0 = (real)index * (real)(2.0 * 3.14159265358979) / (real)FORGE_VOLUME_SPHERE_SIDES;
				real angle1 = (real)(index + 1) * (real)(2.0 * 3.14159265358979) / (real)FORGE_VOLUME_SPHERE_SIDES;
				real_point3d a = center, b = center;
				real_vector3d const *u = circle == 2 ? &left : &forward;
				real_vector3d const *v = circle == 0 ? &left : global_up3d;

				a.x += (u->i * cosine(angle0) + v->i * sine(angle0)) * radius;
				a.y += (u->j * cosine(angle0) + v->j * sine(angle0)) * radius;
				a.z += (u->k * cosine(angle0) + v->k * sine(angle0)) * radius;
				b.x += (u->i * cosine(angle1) + v->i * sine(angle1)) * radius;
				b.y += (u->j * cosine(angle1) + v->j * sine(angle1)) * radius;
				b.z += (u->k * cosine(angle1) + v->k * sine(angle1)) * radius;
				forge_volume_line(vertices, &count, &a, &b, color);
			}
		}
	}
	else
	{
		real width = spawn->volume_size[0] * FORGE_FILE_VOLUME_SIZE_UNIT;
		real length = spawn->volume_size[1] * FORGE_FILE_VOLUME_SIZE_UNIT;
		real height = spawn->volume_size[2] * FORGE_FILE_VOLUME_SIZE_UNIT;
		real_point3d corners[8];

		for (index = 0; index < 8; index++)
		{
			real along = (index & 1) ? length : -length;
			real across = (index & 2) ? width : -width;

			corners[index].x = base.x + forward.i * along + left.i * across;
			corners[index].y = base.y + forward.j * along + left.j * across;
			corners[index].z = base.z + ((index & 4) ? height : 0.f);
		}
		for (index = 0; index < 8; index++)
		{
			/* (each corner to the ones one bit away: twelve edges) */
			if (!(index & 1))
				forge_volume_line(vertices, &count, &corners[index], &corners[index | 1], color);
			if (!(index & 2))
				forge_volume_line(vertices, &count, &corners[index], &corners[index | 2], color);
			if (!(index & 4))
				forge_volume_line(vertices, &count, &corners[index], &corners[index | 4], color);
		}
	}
	if (count < 2)
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
			rasterizer_draw_dynamic_vertices(0, count / 2, buffer, 2);
			rasterizer_debug_drawing_end();
		}
		rasterizer_globals.current_lock_operation = _rasterizer_lock_none;
	}
	rasterizer_dynamic_vertices_delete(buffer);
}

/* ---------- public code */

/* (game.c's game_tick, after the scripts, before the objects move) each
volume of a Forge game or a layout played acts on what is in it: the kill
volumes kill a unit in them, the damage volumes take their share of its
health (shields first; at none, killed), the gravity volumes scale gravity
for what is in the air in them. Units by their bounding sphere's centre;
the monitor's hidden unit, a unit that takes no damage and the dead are
left alone. A tick's, from the layout alone: the same on every run */
void forge_volumes_tick(
	void)
{
	struct object_iterator iterator;
	struct object_datum *object;
	short volumes = 0;
	short index;

	if (!forge_globals.game_active || forge_globals.round_resetting)
		return;
	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
		volumes += forge_spawn_is_volume(&forge_globals.spawns[index]) ? 1 : 0;
	if (!volumes)
		return;
	object_iterator_new(&iterator, _object_mask_biped | _object_mask_vehicle | _object_mask_weapon | _object_mask_equipment, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		long object_index = iterator.index;
		boolean biped = object->object.type == _object_type_biped;
		boolean dead = TEST_FLAG(object->object.damage_flags, _object_dead_bit);
		boolean killed = FALSE;
		real gravity = 1.f;
		boolean in_gravity = FALSE;

		if (forge_globals.active && object_index == forge_globals.unit_index)
			continue;
		for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
		{
			struct forge_spawn const *spawn = &forge_globals.spawns[index];

			/* (the one held acts only once it is put down: a volume just
			taken from the OBJECT BROWSER has its home where it appeared, under
			the monitor, and killed whoever stood there while it was carried
			off) */
			if (!forge_spawn_is_volume(spawn) || spawn->id == forge_globals.held_marker_id ||
				!forge_volume_contains(spawn, &object->object.bounding_sphere_center))
				continue;
			if (spawn->kind == _forge_spawn_volume_gravity)
			{
				/* (the lowest of the volumes it is in; a vehicle or an item
				only in a volume for everyone) */
				if (biped ? forge_volume_takes_unit(spawn, (struct unit_datum const *)object) :
					spawn->volume_target == FORGE_FILE_VOLUME_EVERY_UNIT && spawn->team == NONE)
				{
					real scale = spawn->volume_amount / 100.f;

					gravity = in_gravity ? MIN(gravity, scale) : scale;
					in_gravity = TRUE;
				}
				continue;
			}
			if (!biped || dead || killed || TEST_FLAG(object->object.damage_flags, _object_cannot_take_damage_bit) ||
				!forge_volume_takes_unit(spawn, (struct unit_datum const *)object))
			{
				continue;
			}
			if (spawn->kind == _forge_spawn_volume_kill)
			{
				unit_kill(object_index);
				killed = TRUE;
				platform_log("forge: the kill volume #%ld killed %s (tick %ld)", spawn->id, forge_tag_leaf(object->definition_index),
					(long)game_time_get());
				continue;
			}
			/* (a damage volume's share this tick: shields first, then the
			body; the shields kept from charging while it lasts) */
			{
				real share = spawn->volume_amount / (100.f * (real)TICKS_PER_SECOND);

				if (object->object.shield_vitality > 0.f)
				{
					real taken = MIN(object->object.shield_vitality, share);

					object->object.shield_vitality -= taken;
					share -= taken;
				}
				object->object.shield_stun_ticks = (short)MAX(object->object.shield_stun_ticks, TICKS_PER_SECOND);
				if (share > 0.f)
				{
					object->object.body_vitality -= share;
					if (object->object.body_vitality <= 0.f)
					{
						object->object.body_vitality = 0.f;
						unit_kill(object_index);
						killed = TRUE;
						platform_log("forge: the damage volume #%ld killed %s (tick %ld)", spawn->id,
							forge_tag_leaf(object->definition_index), (long)game_time_get());
					}
				}
			}
		}
		/* (gravity scaled: what is in the air - a biped jumping or falling,
		a vehicle or an item that is not at rest - is given back the share
		of gravity the volume takes away; riders ride) */
		if (in_gravity && gravity != 1.f && object->object.parent_object_index == NONE &&
			(biped ? TEST_FLAG(((struct biped_datum *)object)->biped.flags, _biped_airborne_bit) :
				!TEST_FLAG(object->object.flags, _object_at_rest_bit)))
		{
			object->object.translational_velocity.k += (1.f - gravity) * global_gravity;
		}
	}
}
