/* forge_flyable.inc: the campaign's Pelican and Covenant dropship, brought
in from the player's own campaign maps for a multiplayer map's Forge game
(forge_ai_tags.c), made ones a player flies. Included by forge.c.

Their tags are the copies in memory of this game's import (nothing of a
map file is changed) and are gone with the map. In the campaign their
pilots are AI on scripted paths; what a player needs is changed:
- the pilot's seat takes an animation set the player's unit has (the
  Warthog driver's: its seat is invisible, the pilot not drawn) at a marker
  a player on foot reaches ("driver enter" before the Pelican's nose, the
  dropship's front left pod), where a player walks up, presses action and
  holds it to get out again; the seat keeps its third person camera,
  without its pitch limits (below), and the chase camera stands back as
  far as the hull needs (forge_flyable_camera_scale);
- the riders' seats take the Pelican's front riders' animations of their
  side (P-riderLF, P-riderRF), at their own markers: ten on the Pelican,
  eight in the dropship's side pods;
- the dropship's gun seat brings no AI gunner (below): its gun is that
  gunner's, so this dropship flies unarmed; the Pelican has no gun;
- the top speed and how soon it is reached: the campaign's (0.5 world units
  a tick, 0.03 more a tick) are its pilots' slow scripted flights; a
  player's Pelican 0.55 (16 world units a second), the heavier dropship
  0.45.
The flight itself is the campaign's own human plane physics (vehicles.c
update_human_plane_physics): the stick's throttle, the aim's heading, jump
to climb, crouch to descend, and with neither a hover; the climb, hold and
descent a player's (forge_flyable_lift), and a craft kept in the level
(forge_flyable_sweep). */

enum
{
	FORGE_FLYABLE_MAXIMUM_SEATS = 32,
	/* (vehicles.c struct vehicle_definition: the speed block after the
	unit's and the vehicle's flags and type) */
	FORGE_FLYABLE_SPEED_OFFSET = 0x2F8,
	FORGE_FLYABLE_ACCELERATION_OFFSET = 0x300,
};

static struct
{
	char const *path;
	char const *pilot_label;
	char const *pilot_marker;
	char const *rider_prefix;
	real speed;
	real acceleration;
	/* its chase camera's distance, of the default track's (the campaign's
	pilots have none: the default fits a Warthog) */
	real camera_scale;
} const forge_flyables[] =
{
	{ "vehicles\\pelican\\pelican", "P-driver", "driver enter", "P-rider", 0.55f, 0.03f, 1.3f },
	{ "vehicles\\c_dropship\\c_dropship", "c_dropship driver", "cd-passengerl01", "CD-passenger", 0.45f, 0.025f, 2.6f },
};

static void forge_flyable_string(
	char *field,
	char const *text)
{
	csmemset(field, 0, TAG_STRING_LENGTH + 1);
	csstrncpy(field, text, TAG_STRING_LENGTH);
}

/* whether a model has a marker */
static boolean forge_flyable_marker(
	long model_index,
	char const *name)
{
	struct model *model = model_index != NONE ? model_definition_get(model_index) : NULL;
	short index;

	for (index = 0; model && index < model->markers.count; index++)
	{
		if (!_stricmp(TAG_BLOCK_GET_ELEMENT(&model->markers, index, struct model_marker)->name, name))
			return TRUE;
	}
	return FALSE;
}

/* one of them, if this game's import has it and it is the campaign's (its
pilot's seat and the marker there): its seats and speeds changed. The
labels the player's unit has animations for are never a campaign seat's,
so a tag changed is not changed again */
static void forge_flyable_tune_one(
	short flyable)
{
	long tag_index = tag_loaded(VEHICLE_DEFINITION_TAG, forge_flyables[flyable].path);
	struct unit_definition *definition;
	byte *vehicle;
	short seat_index;
	short pilot = NONE;
	short riders = 0;
	size_t prefix_length = strlen(forge_flyables[flyable].rider_prefix);

	if (tag_index == NONE || !forge_ai_tags_source_map(tag_index))
		return;
	definition = unit_definition_get(tag_index);
	vehicle = (byte *)definition;
	for (seat_index = 0; seat_index < definition->unit.seats.count && seat_index < FORGE_FLYABLE_MAXIMUM_SEATS; seat_index++)
	{
		struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, seat_index, struct unit_seat);

		if (pilot == NONE && TEST_FLAG(seat->flags, _unit_seat_driver_bit) &&
			!_strnicmp(seat->label, forge_flyables[flyable].pilot_label, TAG_STRING_LENGTH))
		{
			pilot = seat_index;
		}
	}
	if (pilot == NONE || !forge_flyable_marker(definition->object.model.index, forge_flyables[flyable].pilot_marker))
		return;
	{
		struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, pilot, struct unit_seat);

		forge_flyable_string(seat->label, "W-driver");
		forge_flyable_string(seat->marker_name, forge_flyables[flyable].pilot_marker);
		/* (its camera's pitch limits are an AI pilot's view, which the
		player's aim takes relative to the craft's own pitch
		(player_control.c): a craft nosing down took the aim down with it,
		into a dive the player could not pull out of. Without them the aim
		levels as on foot) */
		seat->camera.pitch_minimum = 0.0f;
		seat->camera.pitch_maximum = 0.0f;
	}
	for (seat_index = 0; seat_index < definition->unit.seats.count && seat_index < FORGE_FLYABLE_MAXIMUM_SEATS; seat_index++)
	{
		struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, seat_index, struct unit_seat);
		char side;

		/* (the dropship's gun seat brings its own AI gunner, a biped of no
		team, with the craft: every player's enemy, so no player could get
		in. None) */
		seat->built_in_actor_reference.index = NONE;

		if (_strnicmp(seat->label, forge_flyables[flyable].rider_prefix, prefix_length))
			continue;
		side = seat->label[prefix_length];
		side = side >= 'A' && side <= 'Z' ? side - 'A' + 'a' : side;
		if (side != 'l' && side != 'r')
			continue;
		riders++;
		forge_flyable_string(seat->label, side == 'l' ? "P-riderLF" : "P-riderRF");
	}
	*(real *)(vehicle + FORGE_FLYABLE_SPEED_OFFSET) = forge_flyables[flyable].speed;
	*(real *)(vehicle + FORGE_FLYABLE_ACCELERATION_OFFSET) = forge_flyables[flyable].acceleration;
	platform_log("forge: %s flown by a player: its pilot's seat at %s, %d riders' seats, %.2f a tick at most",
		forge_tag_leaf(tag_index), forge_flyables[flyable].pilot_marker, riders, forge_flyables[flyable].speed);
}

/* (following_camera.c, each frame: the camera's alone) a unit's chase
camera distance scale: one of these flown, its own; any other 1 */
real forge_flyable_camera_scale(
	long unit_index)
{
	struct object_datum *object = unit_index != NONE ? object_try_and_get(unit_index) : NULL;
	short flyable;

	if (!object || object->object.type != _object_type_vehicle || !forge_ai_tags_source_map(object->definition_index))
		return 1.0f;
	for (flyable = 0; flyable < (short)NUMBEROF(forge_flyables); flyable++)
	{
		if (!_stricmp(tag_get_name(object->definition_index), forge_flyables[flyable].path))
			return forge_flyables[flyable].camera_scale;
	}
	return 1.0f;
}

/* the hover's lift, of the craft's weight 1.3 times this (vehicles.c
update_human_plane_physics): the campaign's 1.0 climbing (jump), 0.75
holding (a slow sink) and 0.25 descending (crouch) are its AI pilots'
gentle climbs and its drops onto a landing zone. A player's: a climb that
takes it over Blood Gulch's hills in a few seconds, a hover that holds
its height, a descent that sets it down gently */
#define FORGE_FLYABLE_LIFT_CLIMB 1.6f
#define FORGE_FLYABLE_LIFT_HOLD 0.77f
#define FORGE_FLYABLE_LIFT_DESCEND 0.5f

real forge_flyable_lift(
	long vehicle_index,
	real factor)
{
	struct object_datum *vehicle = object_get(vehicle_index);

	if (!forge_ai_tags_source_map(vehicle->definition_index))
		return factor;
	return factor >= 1.0f ? FORGE_FLYABLE_LIFT_CLIMB : factor <= 0.25f ? FORGE_FLYABLE_LIFT_DESCEND : FORGE_FLYABLE_LIFT_HOLD;
}

/* the campaign's physics meets the level only where a craft's mass points
are: flown into a slope or a cliff by a player (as its AI pilots never
fly it), the hull pressed in until its middle was in the rock, and it fell
through the level and out of it. Each tick of one brought in (only these
two of the imported are human planes), after its physics: if its middle is
in the level's open space, the spot is kept; if not (in the rock or out of
the level), it goes back to the last spot kept and stops. The tick's own
work, the same on every machine; kept for the few a game has, by object
(a slot reused when it is full) */
#define FORGE_FLYABLE_SPOTS 8

static struct
{
	long object_index;
	real_point3d position;
} forge_flyable_spots[FORGE_FLYABLE_SPOTS];
static short forge_flyable_spot_next;

void forge_flyable_sweep(
	long vehicle_index)
{
	struct object_datum *vehicle = object_get(vehicle_index);
	struct location location;
	short spot;

	if (!forge_ai_tags_source_map(vehicle->definition_index))
		return;
	for (spot = 0; spot < FORGE_FLYABLE_SPOTS && forge_flyable_spots[spot].object_index != vehicle_index; spot++)
		;
	scenario_location_from_point(&location, &vehicle->object.position);
	if (location.cluster_index != NONE)
	{
		if (spot >= FORGE_FLYABLE_SPOTS)
		{
			spot = forge_flyable_spot_next;
			forge_flyable_spot_next = (short)((forge_flyable_spot_next + 1) % FORGE_FLYABLE_SPOTS);
			forge_flyable_spots[spot].object_index = vehicle_index;
		}
		forge_flyable_spots[spot].position = vehicle->object.position;
		return;
	}
	if (spot >= FORGE_FLYABLE_SPOTS)
		return;
	object_set_position(vehicle_index, &forge_flyable_spots[spot].position, NULL, NULL);
	vehicle->object.translational_velocity.i = 0.0f;
	vehicle->object.translational_velocity.j = 0.0f;
	vehicle->object.translational_velocity.k = 0.0f;
	vehicle->object.angular_velocity.i = 0.0f;
	vehicle->object.angular_velocity.j = 0.0f;
	vehicle->object.angular_velocity.k = 0.0f;
}

/* (forge_update, as an import finishes) each of them this game has */
static void forge_flyable_tune(
	void)
{
	short flyable;

	csmemset(forge_flyable_spots, 0xFF, sizeof(forge_flyable_spots));
	forge_flyable_spot_next = 0;
	for (flyable = 0; flyable < (short)NUMBEROF(forge_flyables); flyable++)
		forge_flyable_tune_one(flyable);
}
