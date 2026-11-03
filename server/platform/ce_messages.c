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

void ce_payload_put_player_add(struct ce_payload *payload, int player, unsigned int handle)
{
	put_delta_header(payload, CE_DELTA_PLAYER_ADD);
	put_bits(payload, 0, 8);
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
	put_bits(payload, (unsigned int)weapon->unknown_small, 8);
	put_position(payload, weapon->object.position);
	put_zero_velocity(payload);
	put_bits(payload, (unsigned int)weapon->ammunition[0], 16);
	put_bits(payload, (unsigned int)weapon->ammunition[1], 16);
	put_bits(payload, (unsigned int)weapon->unknown_6bits, 6);
	put_bits(payload, (unsigned int)weapon->magazine[0], 16);
	put_bits(payload, (unsigned int)weapon->magazine[1], 16);
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
	put_bits(payload, (unsigned int)biped->unknown_small, 8);
	put_position(payload, biped->object.position);
	put_zero_velocity(payload);
	put_bits(payload, (unsigned int)biped->grenades[0], 3);
	put_bits(payload, (unsigned int)biped->grenades[1], 3);
	put_bits(payload, (unsigned int)biped->unknown_3bits, 3);
	put_bits(payload, (unsigned int)biped->unknown_6bits_b, 6);
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
