
/* ---------- distributed_messages_fuzz: a client's world, made for each input
(compiled at the end of network_objects.c as the build has it, every
`long` of the game an int: here too, whose table of the host's objects
this machine has is its own: tools/tests/host_fuzz/distributed_messages/
target.json). Its players and objects live in the Xbox address space the
64-bit build keeps the game's data arrays in (xbox_address.h), reserved here
where the sanitizers leave room for it, as tools/tests/map_fuzz.c does. */

#include <sys/mman.h>

/* (the build's strict C hides it from glibc's header: tools/tests/map_fuzz.c too) */
#if defined(__linux__) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS 0x20
#endif

struct data_array *player_data;
struct data_array *object_header_data;

enum
{
	WORLD_ARENA_BASE = 0x10000000,
	WORLD_ARENA_SIZE = 64 * 1024 * 1024,
	WORLD_PLAYERS = 4,
	WORLD_OBJECTS = 8,
	/* (each object in an allocation of its own, larger than any object's
	structure: a read past a structure the object's type has is not one
	past the allocation) */
	WORLD_OBJECT_SIZE = 8192,
};

static unsigned int world_arena_next;
static struct object_datum *world_objects[WORLD_OBJECTS];
static int world_object_indices[WORLD_OBJECTS];
static int world_player_indices[WORLD_PLAYERS];
int distributed_messages_fuzz_local_player = NONE;

/* (data.c's arrays, in the window) */
void *debug_malloc(
	unsigned int size,
	boolean clear,
	const char *file,
	int line)
{
	byte *pointer;

	(void)file;
	(void)line;
	if (!world_arena_next)
	{
		void *wanted = xbox_pointer(WORLD_ARENA_BASE);

		if (mmap(wanted, WORLD_ARENA_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) !=
			wanted)
		{
			abort();
		}
		world_arena_next = WORLD_ARENA_BASE;
	}
	size = (size + 15) & ~15U;
	if (world_arena_next + size > WORLD_ARENA_BASE + WORLD_ARENA_SIZE)
		abort();
	pointer = (byte *)xbox_pointer((unsigned int)world_arena_next);
	world_arena_next += size;
	if (clear)
		memset(pointer, 0, size);
	return pointer;
}

void debug_free(
	void *pointer,
	const char *file,
	int line)
{
	(void)pointer;
	(void)file;
	(void)line;
}

/* (objects.c's, over the harness's objects: a stale or wrong handle is NULL,
as in a release build) */
void *object_try_and_get_and_verify_type(
	int object_index,
	unsigned int valid_type_flags)
{
	struct object_header_datum *header = object_header_data ?
		(struct object_header_datum *)datum_try_and_get(object_header_data, object_index) : NULL;

	return header && TEST_FLAG(valid_type_flags, header->type) ? header->datum : NULL;
}

void *object_get_and_verify_type(
	int object_index,
	unsigned int valid_type_flags)
{
	struct object_header_datum *header = object_header_data ?
		(struct object_header_datum *)datum_try_and_get(object_header_data, object_index) : NULL;

	(void)valid_type_flags;
	return header ? header->datum : NULL;
}

int object_get_ultimate_parent(
	int object_index)
{
	struct object_datum *object = object_try_and_get(object_index);
	int depth = 0;

	while (object && object->object.parent_object_index != NONE && depth++ < 16)
	{
		object_index = object->object.parent_object_index;
		object = object_try_and_get(object_index);
	}
	return object_index;
}

real_point3d *object_get_origin(
	int object_index,
	real_point3d *origin)
{
	struct object_datum *object = object_get(object_index);

	*origin = object->object.position;
	return origin;
}

static int world_object_new(
	short type)
{
	int index = datum_new(object_header_data);
	struct object_header_datum *header = (struct object_header_datum *)datum_get(object_header_data, index);
	struct object_datum *object = calloc(1, WORLD_OBJECT_SIZE);
	struct _object_datum *data = &object->object;
	int attachment;

	header->type = (byte)type;
	header->datum = object;
	object->definition_index = NONE;
	data->type = type;
	data->owner_team_index = NONE;
	data->name_index = NONE;
	data->owner_player_index = NONE;
	data->owner_object_index = NONE;
	data->owner_object_definition_index = NONE;
	data->umbrella_shield_object_index = NONE;
	data->first_cluster_reference_index = NONE;
	data->next_garbage_object_index = NONE;
	data->next_object_index = NONE;
	data->first_child_object_index = NONE;
	data->parent_object_index = NONE;
	data->parent_node_index = NONE;
	for (attachment = 0; attachment < MAXIMUM_NUMBER_OF_ATTACHMENTS_PER_OBJECT; attachment++)
		data->attachment_indices[attachment] = NONE;
	data->first_widget_index = NONE;
	data->cached_render_state_index = NONE;
	data->forced_shader_permutation_index = NONE;
	data->position.x = 1.0f * index;
	data->position.y = 2.0f;
	data->position.z = 3.0f;
	if (type == _object_type_biped || type == _object_type_vehicle)
	{
		struct _unit_datum *unit = &((struct unit_datum *)object)->unit;
		int weapon;

		unit->actor_index = NONE;
		unit->swarm_actor_index = NONE;
		unit->swarm_next_unit_index = NONE;
		unit->swarm_prev_unit_index = NONE;
		unit->magic_seat_index = NONE;
		unit->player_index = NONE;
		unit->override_emotion_animation_index = NONE;
		unit->grenade_object_index = NONE;
		unit->parent_seat_index = NONE;
		unit->current_weapon_index = NONE;
		unit->desired_weapon_index = NONE;
		for (weapon = 0; weapon < MAXIMUM_WEAPONS_PER_UNIT; weapon++)
			unit->weapon_object_indices[weapon] = NONE;
		unit->equipment_object_index = NONE;
		unit->current_grenade_index = NONE;
		unit->desired_grenade_index = NONE;
		unit->driver_object_index = NONE;
		unit->gunner_object_index = NONE;
		unit->last_vehicle_index = NONE;
		unit->fake_encounter_index = NONE;
		unit->fake_squad_index = NONE;
		unit->dialogue_index = NONE;
		unit->delayed_damage_attacker_object_index = NONE;
		unit->flaming_death_attacker_object_index = NONE;
	}
	/* (one the host made, which this machine has) */
	objects_client_has[DATUM_INDEX_TO_ABSOLUTE_INDEX(index)] = index;
	return index;
}

/* a client in game: players 0 (this machine's, local player 0) to 3, each
on a biped of its own; a vehicle, two weapons and a scenery object. The
objects' handles in order, for the input to name: world_object_handle */
void distributed_messages_fuzz_world_reset(
	void)
{
	static short const types[WORLD_OBJECTS] =
	{
		_object_type_biped, _object_type_biped, _object_type_biped, _object_type_biped,
		_object_type_vehicle, _object_type_weapon, _object_type_weapon, _object_type_scenery,
	};
	int index;

	/* (the C library's free: cseries.h makes free the game's debug_free, the
	arena's, which keeps everything) */
	for (index = 0; index < WORLD_OBJECTS; index++)
		(free)(world_objects[index]);
	csmemset(objects_client_has, 0xFF, sizeof(objects_client_has));
	if (!player_data)
	{
		player_data = data_new("players", MAXIMUM_TRACKED_PLAYERS, sizeof(struct player_datum));
		object_header_data = data_new("object", 512, sizeof(struct object_header_datum));
	}
	data_make_valid(player_data);
	data_make_valid(object_header_data);
	for (index = 0; index < WORLD_OBJECTS; index++)
	{
		world_object_indices[index] = world_object_new(types[index]);
		world_objects[index] = object_get(world_object_indices[index]);
	}
	for (index = 0; index < WORLD_PLAYERS; index++)
	{
		int player_index = datum_new(player_data);

		world_player_indices[index] = player_index;
		struct player_datum *player = (struct player_datum *)datum_get(player_data, player_index);
		struct unit_datum *unit = (struct unit_datum *)world_objects[index];

		player->local_player_index = index == 0 ? 0 : NONE;
		player->squad_index = NONE;
		player->team_index = index & 1;
		player->action_object_index = NONE;
		player->action_seat_index = NONE;
		player->unit_index = world_object_indices[index];
		player->dead_unit_index = NONE;
		player->cluster_index = NONE;
		player->aim_assist_unit_index = NONE;
		player->teleporter_index = NONE;
		player->state_message_player_index = NONE;
		player->player_display_index = NONE;
		player->quit_out_of_game_time = NONE;
		player->network_player_data.machine_index = (char)index;
		player->network_player_data.controller_index = 0;
		player->network_player_data.team_index = (char)(index & 1);
		player->network_player_data.player_list_index = (char)index;
		player->network_player_data.name[0] = 'P';
		unit->unit.player_index = player_index;
		unit->object.owner_player_index = player_index;
		if (index == 0)
			distributed_messages_fuzz_local_player = player_index;
	}
}

int distributed_messages_fuzz_object_handle(
	int which)
{
	return world_object_indices[which % WORLD_OBJECTS];
}

int distributed_messages_fuzz_player_handle(
	int which)
{
	return world_player_indices[which % WORLD_PLAYERS];
}
