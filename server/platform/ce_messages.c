/*
CE_MESSAGES.C

Halo Custom Edition 1.0.10's payloads (ce_messages.h).

A payload is a bit stream, least significant bit first: 11 bits of its
length in bytes, then messages until fewer than 8 bits are left, each
starting with a bit:
  - 0: a data packet. A 16-bit header (4 bits of flags, 0xc, and the size
    in bytes with the header), then its bytes: a version byte, its fields
    (big-endian), its type last;
  - 1: a message delta. A bit (0: every field sent; 1: a bit before each
    field says whether it is), a 6-bit type, then the fields at their
    widths: integers 8, 16 or 32 bits, booleans 1, reals 32; a string a
    length (as many bits as its most characters need) and 8-bit characters,
    a wide string the same with 16-bit characters; a record its fields in
    order; an array its elements. A delta that repeats (up to N entries)
    first has the entries' count less one, in as many bits as N - 1 needs.

The game settings delta (type 33), in full, carries the network game: its
name (wide, 64), map (version, then name, 128), game variant (name, wide,
24; engine; the universal variant; the engine's 56 bytes; a flag bit),
two 8-bit and two 16-bit numbers (minimum and most players, difficulty,
player count), the 16 player slots (name, wide, 12; color and icon, 16
bits; machine, controller, team and list index, 8 bits) and three 32-bit
numbers (the random seed, games played, a local flag).
*/

#include "ce_messages.h"

#include <math.h>
#include <string.h>

enum
{
	MESSAGE_FLAGS = 0xc,
};

/* the universal variant's field widths, in the wire's order */
static const unsigned char universal_widths[CE_UNIVERSAL_FIELDS] =
{
	1, 8, 8, 8, 9, 32, 1, 8, 8, 8, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 8, 32, 1, 32,
};

static void put_bits(struct ce_payload *payload, unsigned int value, int count)
{
	int index;

	if (payload->overflow || payload->position + count > payload->size * 8)
	{
		payload->overflow = 1;
		return;
	}
	for (index = 0; index < count; index++, payload->position++)
	{
		unsigned char mask = (unsigned char)(1 << (payload->position & 7));

		if ((value >> index) & 1)
			payload->data[payload->position >> 3] |= mask;
		else
			payload->data[payload->position >> 3] &= (unsigned char)~mask;
	}
}

static int width_for(int most)
{
	int width = 0;

	while (most >> width)
		width++;
	return width;
}

static void put_text(struct ce_payload *payload, const char *text, int most)
{
	int length = (int)strnlen(text, (size_t)most);
	int index;

	put_bits(payload, (unsigned int)length, width_for(most));
	for (index = 0; index < length; index++)
		put_bits(payload, (unsigned char)text[index], 8);
}

static void put_wide_text(struct ce_payload *payload, const char *text, int most)
{
	int length = (int)strnlen(text, (size_t)most);
	int index;

	put_bits(payload, (unsigned int)length, width_for(most));
	for (index = 0; index < length; index++)
		put_bits(payload, (unsigned char)text[index], 16);
}

void ce_payload_begin(struct ce_payload *payload, unsigned char *out, int size)
{
	payload->data = out;
	payload->size = size;
	payload->position = 0;
	payload->overflow = 0;
	if (size > 0)
		memset(out, 0, (size_t)size);
	put_bits(payload, 0, 11);
}

void ce_payload_put_message(struct ce_payload *payload, const unsigned char *message, int length)
{
	int index;

	if (length < 2 || length + 2 > 0xfff)
	{
		payload->overflow = 1;
		return;
	}
	put_bits(payload, 0, 1);
	put_bits(payload, MESSAGE_FLAGS | ((unsigned int)(length + 2) << 4), 16);
	for (index = 0; index < length; index++)
		put_bits(payload, message[index], 8);
}

void ce_payload_put_game_settings(struct ce_payload *payload, const struct ce_game_settings *settings)
{
	int index;

	put_bits(payload, 1, 1);
	/* (every field) */
	put_bits(payload, 0, 1);
	put_bits(payload, CE_DELTA_GAME_SETTINGS, 6);
	put_wide_text(payload, settings->name, 64);
	put_bits(payload, (unsigned int)settings->map_version, 32);
	put_text(payload, settings->map, 128);
	put_wide_text(payload, settings->variant_name, 24);
	put_bits(payload, (unsigned int)settings->engine, 32);
	for (index = 0; index < CE_UNIVERSAL_FIELDS; index++)
		put_bits(payload, settings->universal[index], universal_widths[index]);
	for (index = 0; index < CE_ENGINE_VARIANT_SIZE; index++)
		put_bits(payload, settings->engine_variant[index], 8);
	put_bits(payload, (unsigned int)settings->variant_flags, 1);
	put_bits(payload, (unsigned int)settings->minimum_players, 8);
	put_bits(payload, (unsigned int)settings->maximum_players, 8);
	put_bits(payload, (unsigned int)settings->difficulty, 16);
	put_bits(payload, (unsigned int)settings->player_count, 16);
	for (index = 0; index < CE_GAME_PLAYERS; index++)
	{
		const struct ce_network_player *player = &settings->players[index];

		put_wide_text(payload, player->name, 12);
		put_bits(payload, (unsigned int)player->color, 16);
		put_bits(payload, (unsigned int)player->icon, 16);
		put_bits(payload, (unsigned int)player->machine, 8);
		put_bits(payload, (unsigned int)player->controller, 8);
		put_bits(payload, (unsigned int)player->team, 8);
		put_bits(payload, (unsigned int)player->list_index, 8);
	}
	put_bits(payload, settings->random_seed, 32);
	put_bits(payload, (unsigned int)settings->games_played, 32);
	put_bits(payload, (unsigned int)settings->local_data, 32);
}

void ce_payload_put_server_keep_alive(struct ce_payload *payload, int index, unsigned int value)
{
	put_bits(payload, 1, 1);
	put_bits(payload, 0, 1);
	put_bits(payload, CE_DELTA_SERVER_KEEP_ALIVE, 6);
	/* (entries less one, in 4 bits: a delta of up to 16 entries) */
	put_bits(payload, 0, 4);
	put_bits(payload, (unsigned int)index, 8);
	put_bits(payload, value, 32);
}

static void put_real(struct ce_payload *payload, float value)
{
	unsigned int bits;

	memcpy(&bits, &value, sizeof(bits));
	put_bits(payload, bits, 32);
}

static void put_delta_header(struct ce_payload *payload, int type)
{
	put_bits(payload, 1, 1);
	put_bits(payload, 0, 1);
	put_bits(payload, (unsigned int)type, 6);
}

/* (a velocity: 1, the delta's first preset, zero; other velocities are not
written yet) */
static void put_zero_velocity(struct ce_payload *payload)
{
	put_bits(payload, 1, 1);
}

static void put_position(struct ce_payload *payload, const float position[3])
{
	put_real(payload, position[0]);
	put_real(payload, position[1]);
	put_real(payload, position[2]);
}

static void put_object_start(struct ce_payload *payload, const struct ce_object_create *object)
{
	put_bits(payload, object->definition, 32);
	put_bits(payload, (unsigned int)object->object, 9);
	put_bits(payload, (unsigned int)object->owner, 16);
	put_bits(payload, (unsigned int)object->player, 5);
	put_bits(payload, (unsigned int)object->parent, 9);
}

static void put_orientation(struct ce_payload *payload, const struct ce_object_create *object)
{
	put_bits(payload, object->forward[0], 20);
	put_bits(payload, object->forward[1], 20);
	put_bits(payload, object->up[0], 20);
	put_bits(payload, object->up[1], 20);
}

void ce_payload_put_player_add(struct ce_payload *payload, int list_index, unsigned int handle, int player)
{
	put_delta_header(payload, CE_DELTA_PLAYER_ADD);
	put_bits(payload, (unsigned int)list_index, 8);
	put_bits(payload, handle, 32);
	put_bits(payload, (unsigned int)player, 5);
	put_bits(payload, 0, 32);
}

void ce_payload_put_weapon_create(struct ce_payload *payload, const struct ce_weapon_create *weapon)
{
	put_delta_header(payload, CE_DELTA_WEAPON_CREATE);
	put_object_start(payload, &weapon->object);
	put_bits(payload, weapon->unknown_large, 32);
	put_orientation(payload, &weapon->object);
	put_bits(payload, (unsigned int)weapon->baseline, 8);
	put_position(payload, weapon->object.position);
	put_zero_velocity(payload);
	put_bits(payload, (unsigned int)weapon->ammunition[0], 16);
	put_bits(payload, (unsigned int)weapon->ammunition[1], 16);
	put_bits(payload, (unsigned int)weapon->age, 6);
	put_bits(payload, (unsigned int)weapon->magazine[0], 16);
	put_bits(payload, (unsigned int)weapon->magazine[1], 16);
}

void ce_payload_put_equipment_create(struct ce_payload *payload, const struct ce_object_create *equipment, int baseline)
{
	put_delta_header(payload, CE_DELTA_EQUIPMENT_CREATE);
	put_object_start(payload, equipment);
	put_bits(payload, 0, 32);
	put_orientation(payload, equipment);
	put_bits(payload, (unsigned int)baseline, 8);
	put_position(payload, equipment->position);
	put_zero_velocity(payload);
	put_zero_velocity(payload);
}

void ce_payload_put_weapon_update(struct ce_payload *payload, const struct ce_weapon_update *update)
{
	put_delta_header(payload, CE_DELTA_WEAPON_UPDATE);
	put_bits(payload, (unsigned int)update->object, 9);
	put_bits(payload, (unsigned int)update->baseline, 8);
	/* (a full update: its own number 0, kept as the baseline) */
	put_bits(payload, 0, 8);
	put_bits(payload, 1, 1);
	put_position(payload, update->position);
	put_zero_velocity(payload);
	put_bits(payload, (unsigned int)update->ammunition[0], 16);
	put_bits(payload, (unsigned int)update->ammunition[1], 16);
	put_bits(payload, (unsigned int)update->age, 6);
}

void ce_payload_put_action_result(struct ce_payload *payload, const struct ce_action_result *result)
{
	put_delta_header(payload, CE_DELTA_ACTION_RESULT);
	put_bits(payload, (unsigned int)result->player, 5);
	put_bits(payload, (unsigned int)result->weapon_pickup, 32);
	put_bits(payload, (unsigned int)result->object, 9);
	put_bits(payload, (unsigned int)result->result, 16);
	put_bits(payload, (unsigned int)result->seat, 16);
	put_bits(payload, (unsigned int)result->dropped, 9);
}

void ce_payload_put_biped_create(struct ce_payload *payload, const struct ce_biped_create *biped)
{
	int index;

	put_delta_header(payload, CE_DELTA_BIPED_CREATE);
	put_object_start(payload, &biped->object);
	put_orientation(payload, &biped->object);
	for (index = 0; index < 12; index++)
		put_real(payload, biped->change_colors[index]);
	put_bits(payload, (unsigned int)biped->unknown_boolean, 1);
	put_bits(payload, (unsigned int)biped->unknown_6bits_a, 6);
	put_bits(payload, (unsigned int)biped->baseline, 8);
	put_position(payload, biped->object.position);
	put_zero_velocity(payload);
	put_bits(payload, (unsigned int)biped->grenades[0], 3);
	put_bits(payload, (unsigned int)biped->grenades[1], 3);
	put_bits(payload, (unsigned int)biped->body, 3);
	put_bits(payload, (unsigned int)biped->shield, 6);
	put_bits(payload, (unsigned int)biped->unknown_boolean_b, 1);
}

void ce_payload_put_player_spawn(struct ce_payload *payload, const struct ce_player_spawn *spawn)
{
	int index;

	put_delta_header(payload, CE_DELTA_PLAYER_SPAWN);
	put_bits(payload, (unsigned int)spawn->player, 5);
	put_bits(payload, (unsigned int)spawn->biped, 9);
	put_bits(payload, spawn->unknown_large, 32);
	put_bits(payload, (unsigned int)spawn->unknown_object, 9);
	put_bits(payload, (unsigned int)spawn->unknown_medium, 16);
	for (index = 0; index < 4; index++)
		put_bits(payload, (unsigned int)spawn->weapons[index], 9);
	for (index = 0; index < 3; index++)
		put_bits(payload, (unsigned int)spawn->unknown_mediums[index], 16);
}

/* ---------- directions, positions, throttle */

enum
{
	DIRECTION_BITS = 20,
	POSITION_BITS = 31,
	VELOCITY_BITS = 31,
	/* a whole position's range, each axis: -5000 to 5000 world units */
	POSITION_RANGE = 10000,
	POSITION_OFFSET = 5000,
};

void ce_direction(const float vector[3], unsigned int out[2])
{
	/* (a direction is two angles: from straight up, 0 to pi; around, from
	the y axis's negative side, 0 to 2 pi) */
	const double maximum = (double)((1u << DIRECTION_BITS) - 1);
	const double pi = 3.14159265358979323846;
	double z = vector[2] > 1.0f ? 1.0 : vector[2] < -1.0f ? -1.0 : vector[2];
	double from_up = acos(z);
	double around = atan2(vector[1], vector[0]) + pi / 2.0;

	while (around < 0.0)
		around += 2.0 * pi;
	while (around >= 2.0 * pi)
		around -= 2.0 * pi;
	out[0] = (unsigned int)floor(from_up / pi * maximum + 0.5);
	out[1] = (unsigned int)floor(around / (2.0 * pi) * maximum + 0.5) & ((1u << DIRECTION_BITS) - 1);
}

static void put_position_full(struct ce_payload *payload, const float position[3])
{
	const double maximum = (double)((1u << POSITION_BITS) - 1);
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		double value = ((double)position[axis] + POSITION_OFFSET) / POSITION_RANGE;

		value = value < 0.0 ? 0.0 : value > 1.0 ? 1.0 : value;
		put_bits(payload, (unsigned int)floor(value * maximum + 0.5), POSITION_BITS);
	}
}

void ce_throttle(int bits, float *forward, float *left)
{
	float values[2] = { 0.0f, 0.0f };
	float length;
	int component;

	/* (2 bits a component, left/right first: the low bit -1, the high +1) */
	for (component = 1; component >= 0; component--)
	{
		if (bits & 1)
			values[component] = -1.0f;
		if (bits & 2)
			values[component] = 1.0f;
		bits >>= 2;
	}
	length = sqrtf(values[0] * values[0] + values[1] * values[1]);
	if (length > 0.0001f)
	{
		values[0] /= length;
		values[1] /= length;
	}
	*forward = values[0];
	*left = values[1];
}

/* ---------- object and player updates */

/* (a velocity: 1, the first preset, for none; else 0 and each component
in 31 bits over -limit to limit, the full motion (the retail binary's
0x4d8120; 16 bits only for a client asked for the compact motion)) */
static void put_velocity(struct ce_payload *payload, const float velocity[3], float limit)
{
	const double maximum = (double)((1u << VELOCITY_BITS) - 1);
	int axis;

	if (velocity[0] == 0.0f && velocity[1] == 0.0f && velocity[2] == 0.0f)
	{
		put_zero_velocity(payload);
		return;
	}
	put_bits(payload, 0, 1);
	for (axis = 0; axis < 3; axis++)
	{
		double value = ((double)velocity[axis] + limit) / (2.0 * limit);

		value = value < 0.0 ? 0.0 : value > 1.0 ? 1.0 : value;
		put_bits(payload, (unsigned int)floor(value * maximum + 0.5), VELOCITY_BITS);
	}
}

void ce_payload_put_vehicle_create(struct ce_payload *payload, const struct ce_vehicle_create *vehicle)
{
	int index;

	put_delta_header(payload, CE_DELTA_VEHICLE_CREATE);
	put_object_start(payload, &vehicle->object);
	for (index = 0; index < 4; index++)
		put_bits(payload, (unsigned int)vehicle->weapons[index], 9);
	put_bits(payload, (unsigned int)vehicle->baseline, 8);
	put_position(payload, vehicle->object.position);
	put_orientation(payload, &vehicle->object);
	put_zero_velocity(payload);
	put_zero_velocity(payload);
}

static void put_vehicle_motion(struct ce_payload *payload, const struct ce_vehicle_motion *motion)
{
	put_position_full(payload, motion->position);
	put_velocity(payload, motion->velocity, 100.0f);
	put_velocity(payload, motion->angular_velocity, 8.0f);
	put_bits(payload, motion->forward[0], DIRECTION_BITS);
	put_bits(payload, motion->forward[1], DIRECTION_BITS);
	put_bits(payload, motion->up[0], DIRECTION_BITS);
	put_bits(payload, motion->up[1], DIRECTION_BITS);
}

void ce_payload_put_local_vehicle_update(struct ce_payload *payload, int sequence, int update_id, int vehicle,
	const struct ce_vehicle_motion *motion)
{
	put_delta_header(payload, CE_DELTA_LOCAL_VEHICLE_UPDATE);
	put_bits(payload, (unsigned int)sequence, 5);
	put_bits(payload, (unsigned int)update_id, 6);
	put_bits(payload, (unsigned int)vehicle, 9);
	put_vehicle_motion(payload, motion);
}

void ce_payload_put_remote_vehicle_position(struct ce_payload *payload, int player, int update_id, int sequence,
	int vehicle, const struct ce_vehicle_motion *motion)
{
	put_delta_header(payload, CE_DELTA_REMOTE_VEHICLE_POSITION);
	put_bits(payload, (unsigned int)player, 5);
	put_bits(payload, (unsigned int)update_id, 6);
	put_bits(payload, (unsigned int)sequence, 3);
	put_bits(payload, (unsigned int)vehicle, 9);
	put_vehicle_motion(payload, motion);
}

void ce_payload_put_vehicle_update(struct ce_payload *payload, const struct ce_vehicle_update *update)
{
	put_delta_header(payload, CE_DELTA_VEHICLE_UPDATE);
	/* (header: the update's number in its baseline (0: a full update), the
	baseline, the object, the time, kept as the baseline) */
	put_bits(payload, 0, 8);
	put_bits(payload, (unsigned int)update->baseline, 8);
	put_bits(payload, (unsigned int)update->object, 9);
	put_bits(payload, update->time, 32);
	put_bits(payload, 1, 1);
	put_bits(payload, update->at_rest ? 1 : 0, 1);
	put_vehicle_motion(payload, &update->motion);
}

void ce_payload_put_unit_exit(struct ce_payload *payload, int unit, int flag)
{
	put_delta_header(payload, CE_DELTA_UNIT_EXIT);
	put_bits(payload, (unsigned int)unit, 9);
	put_bits(payload, flag ? 1 : 0, 1);
}

void ce_payload_put_object_delete(struct ce_payload *payload, int object)
{
	put_delta_header(payload, CE_DELTA_OBJECT_DELETE);
	put_bits(payload, (unsigned int)object, 9);
}

void ce_payload_put_biped_update(struct ce_payload *payload, const struct ce_biped_update *update)
{
	put_delta_header(payload, CE_DELTA_BIPED_UPDATE);
	put_bits(payload, (unsigned int)update->object, 9);
	put_bits(payload, (unsigned int)update->baseline, 8);
	/* (the update's own number within the baseline; a full update) */
	put_bits(payload, 0, 8);
	/* (the client keeps it as its baseline) */
	put_bits(payload, 1, 1);
	put_bits(payload, update->apply_shield ? 1 : 0, 1);
	put_bits(payload, (unsigned int)update->grenades[0], 3);
	put_bits(payload, (unsigned int)update->grenades[1], 3);
	put_bits(payload, (unsigned int)update->body, 3);
	put_bits(payload, (unsigned int)update->shield, 6);
	put_bits(payload, (unsigned int)update->unknown_boolean, 1);
}

int ce_body_bits(float vitality)
{
	vitality = vitality < 0.0f ? 0.0f : vitality > 1.0f ? 1.0f : vitality;
	return (int)(vitality * 7.0f + 0.5f);
}

int ce_shield_bits(float vitality)
{
	/* (an overshield's up to 3) */
	vitality = vitality < 0.0f ? 0.0f : vitality > 3.0f ? 3.0f : vitality;
	return (int)(vitality * 21.0f + 0.5f);
}

void ce_payload_put_local_player_update(struct ce_payload *payload, int sequence, int update_id,
	const float position[3])
{
	put_delta_header(payload, CE_DELTA_LOCAL_PLAYER_UPDATE);
	put_bits(payload, (unsigned int)sequence, 5);
	put_bits(payload, (unsigned int)update_id, 6);
	put_position(payload, position);
}

void ce_payload_put_remote_player_position(struct ce_payload *payload, int player, int update_id, int sequence,
	const float position[3])
{
	put_delta_header(payload, CE_DELTA_REMOTE_PLAYER_POSITION);
	put_bits(payload, (unsigned int)player, 5);
	put_bits(payload, (unsigned int)update_id, 6);
	put_bits(payload, (unsigned int)sequence, 3);
	put_position_full(payload, position);
}

/* the control flags a client sends (the others are the server's own) */
static const unsigned char control_flags_sent[15] = { 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1 };

void ce_payload_put_remote_player_actions(struct ce_payload *payload, const struct ce_remote_actions *actions)
{
	int bit;

	put_delta_header(payload, CE_DELTA_REMOTE_PLAYER_ACTIONS);
	put_bits(payload, (unsigned int)actions->player, 5);
	put_bits(payload, (unsigned int)actions->update_id, 6);
	/* (no baseline) */
	put_bits(payload, 0, 1);
	put_bits(payload, (unsigned int)actions->ticks, 5);
	for (bit = 0; bit < 15; bit++)
	{
		if (control_flags_sent[bit])
			put_bits(payload, (actions->control_flags >> bit) & 1, 1);
	}
	put_bits(payload, actions->facing[0], DIRECTION_BITS);
	put_bits(payload, actions->facing[1], DIRECTION_BITS);
	put_bits(payload, (unsigned int)actions->throttle, 4);
	put_bits(payload, (unsigned int)actions->primary_trigger, 1);
	put_bits(payload, (unsigned int)actions->weapon, 3);
	put_bits(payload, (unsigned int)actions->grenade, 2);
}

/* ---------- deaths */

void ce_payload_put_unit_killed(struct ce_payload *payload, int object, int respawn_ticks)
{
	/* (the flags as Custom Edition's server sets them for a death with no
	direction: the aftermath is there, the unit died, nothing else, no
	direction) */
	static const unsigned char flags[7] = { 1, 1, 0, 0, 0, 0, 1 };
	int index;

	put_delta_header(payload, CE_DELTA_UNIT_KILLED);
	put_bits(payload, (unsigned int)object, 9);
	for (index = 0; index < 7; index++)
		put_bits(payload, flags[index], 1);
	/* (no part of the body named; no force; no direction) */
	put_bits(payload, 0xffff, 16);
	put_real(payload, 0.0f);
	put_real(payload, 0.0f);
	put_real(payload, 0.0f);
	put_bits(payload, (unsigned int)respawn_ticks, 32);
}

/* ---------- chat and scores */

void ce_payload_put_chat(struct ce_payload *payload, int kind, int player, const char *text)
{
	int length = (int)strnlen(text, CE_CHAT_SIZE);
	int index;

	put_delta_header(payload, CE_DELTA_CHAT);
	put_bits(payload, (unsigned int)kind, 8);
	put_bits(payload, (unsigned int)player, 8);
	/* (the line: its length in 8 bits, then 16-bit characters) */
	put_bits(payload, (unsigned int)length, 8);
	for (index = 0; index < length; index++)
		put_bits(payload, (unsigned char)text[index], 16);
}

void ce_payload_put_postgame_mode(struct ce_payload *payload, int mode)
{
	put_delta_header(payload, CE_DELTA_POSTGAME_MODE);
	put_bits(payload, (unsigned int)mode, 32);
}

void ce_payload_put_slayer_scores(struct ce_payload *payload, const int players[CE_GAME_PLAYERS],
	const int teams[CE_GAME_PLAYERS])
{
	int index;

	put_delta_header(payload, CE_DELTA_SLAYER_SCORES);
	for (index = 0; index < CE_GAME_PLAYERS; index++)
		put_bits(payload, (unsigned int)players[index], 32);
	for (index = 0; index < CE_GAME_PLAYERS; index++)
		put_bits(payload, (unsigned int)teams[index], 32);
}

void ce_payload_put_player_statistics(struct ce_payload *payload, int player,
	const struct ce_player_statistics *statistics)
{
	int index;

	put_delta_header(payload, CE_DELTA_PLAYER_STATISTICS);
	put_bits(payload, (unsigned int)player, 5);
	for (index = 0; index < 4; index++)
		put_bits(payload, (unsigned int)statistics->kills[index], 16);
	for (index = 0; index < 4; index++)
		put_bits(payload, (unsigned int)statistics->assists[index], 16);
	put_bits(payload, (unsigned int)statistics->betrayals, 16);
	put_bits(payload, (unsigned int)statistics->deaths, 16);
	put_bits(payload, (unsigned int)statistics->suicides, 16);
	for (index = 0; index < 3; index++)
		put_bits(payload, (unsigned int)statistics->game_specific[index], 16);
	put_bits(payload, (unsigned int)statistics->multiplayer_special, 32);
	put_bits(payload, (unsigned int)statistics->flag, 1);
	put_real(payload, statistics->speed_multiplier);
}

/* ---------- reading a client's payload */

struct reader
{
	const unsigned char *data;
	int size;
	int position;
};

static int get_bits(struct reader *reader, int count, unsigned int *value)
{
	int index;

	if (reader->position + count > reader->size * 8)
		return 0;
	*value = 0;
	for (index = 0; index < count; index++, reader->position++)
		*value |= (unsigned int)((reader->data[reader->position >> 3] >> (reader->position & 7)) & 1) << index;
	return 1;
}

static int refused_facings;

/* a client's yaw or pitch (a float's bits): a number the game can turn by.
A real client sends angles in radians; one that is not a number, or is
absurdly large, is refused with its update */
static int facing_valid(unsigned int bits)
{
	float angle;

	memcpy(&angle, &bits, sizeof(angle));
	if (isfinite(angle) && fabsf(angle) <= 1000.0f)
		return 1;
	refused_facings++;
	return 0;
}

int ce_refused_facings(void)
{
	return refused_facings;
}

static int read_client_update(struct reader *reader, int every_field, struct ce_client_update *update)
{
	int present[9];
	unsigned int value;
	int index, bit;

	for (index = 0; index < 9; index++)
	{
		present[index] = 1;
		if (!every_field && !get_bits(reader, 1, &value))
			return 0;
		if (!every_field)
			present[index] = (int)value;
	}
	if (!get_bits(reader, 6, &value))
		return 0;
	update->update_id = (int)value;
	if (present[0])
	{
		if (!get_bits(reader, 5, &value))
			return 0;
		update->ticks = (int)value;
	}
	if (present[1])
	{
		for (bit = 0; bit < 15; bit++)
		{
			if (!control_flags_sent[bit])
				continue;
			if (!get_bits(reader, 1, &value))
				return 0;
			update->control_flags = (update->control_flags & ~(1u << bit)) | (value << bit);
		}
	}
	if (present[2])
	{
		if (!get_bits(reader, 32, &value) || !facing_valid(value))
			return 0;
		memcpy(&update->yaw, &value, sizeof(value));
	}
	if (present[3])
	{
		if (!get_bits(reader, 32, &value) || !facing_valid(value))
			return 0;
		memcpy(&update->pitch, &value, sizeof(value));
	}
	if (present[4])
	{
		if (!get_bits(reader, 4, &value))
			return 0;
		update->throttle = (int)value;
	}
	if (present[5])
	{
		if (!get_bits(reader, 1, &value))
			return 0;
		update->primary_trigger = (int)value;
	}
	if (present[6])
	{
		if (!get_bits(reader, 3, &value))
			return 0;
		update->weapon = (int)value;
	}
	if (present[7])
	{
		if (!get_bits(reader, 2, &value))
			return 0;
		update->grenade = (int)value;
	}
	if (present[8])
	{
		if (!get_bits(reader, 16, &value))
			return 0;
		update->unknown_medium = (int)value;
	}
	return 1;
}

int ce_payload_read_client_updates(const unsigned char *payload, int length, struct ce_client_update *update,
	void (*each)(void *context, const struct ce_client_update *update), void *context)
{
	struct reader reader;
	unsigned int value, kind, flag, type;
	int count = 0;

	reader.data = payload;
	reader.size = length;
	reader.position = 0;
	if (!get_bits(&reader, 11, &value) || (int)value != length)
		return 0;
	while (reader.size * 8 - reader.position >= 8)
	{
		if (!get_bits(&reader, 1, &kind))
			break;
		if (!kind)
		{
			/* (a data packet: skipped) */
			if (!get_bits(&reader, 16, &value) || (int)(value >> 4) < 2)
				break;
			reader.position += ((int)(value >> 4) - 2) * 8;
			continue;
		}
		if (!get_bits(&reader, 1, &flag) || !get_bits(&reader, 6, &type))
			break;
		if (type == CE_DELTA_CLIENT_UPDATE)
		{
			/* (an update refused part way leaves the last one as it was) */
			struct ce_client_update next = *update;

			if (!read_client_update(&reader, !flag, &next))
				break;
			*update = next;
			if (each)
				each(context, update);
			count++;
		}
		else if (type == CE_DELTA_CLIENT_KEEP_ALIVE && !flag)
			reader.position += 8;
		else
			/* (another delta: its length isn't known here) */
			break;
	}
	return count;
}

int ce_payload_end(struct ce_payload *payload)
{
	int length = (payload->position + 7) / 8;
	int position = payload->position;

	if (payload->overflow || length > 0x7ff)
		return 0;
	payload->position = 0;
	put_bits(payload, (unsigned int)length, 11);
	payload->position = position;
	return length;
}

void ce_game_settings_slayer(struct ce_game_settings *settings)
{
	/* (the values Custom Edition 1.0.10's server sends for its built-in
	Slayer: free for all, 25 kills, no time limit's end, the stock respawn
	and health, every vehicle set) */
	static const unsigned int slayer[CE_UNIVERSAL_FIELDS] =
	{
		0, 0, 0, 0, 259, 0, 0, 0, 0, 0, 0, 0, 150, 0, 0x3f800000u, 25, 0, 2396736, 2396736, 1800, 1, 0, 0, 36000,
	};
	int index;

	memset(settings, 0, sizeof(*settings));
	memcpy(settings->variant_name, "Slayer", 7);
	settings->engine = 2;
	memcpy(settings->universal, slayer, sizeof(slayer));
	settings->engine_variant[0] = 1;
	settings->engine_variant[1] = 1;
	settings->variant_flags = 1;
	settings->maximum_players = CE_GAME_PLAYERS;
	settings->difficulty = 1;
	settings->games_played = 1;
	settings->local_data = 1;
	for (index = 0; index < CE_GAME_PLAYERS; index++)
	{
		struct ce_network_player *player = &settings->players[index];

		player->color = 0xffff;
		player->icon = 0xffff;
		player->machine = 0xff;
		player->controller = 0xff;
		player->team = 0xff;
		player->list_index = 0xff;
	}
}
