/*
NETWORK_GAME_LAYOUT.C

The game settings record and the game types' states between the PC
builds' and the console's slot counts (network_game_layout.h).
*/

#include <string.h>

#include "network_game_layout.h"

#define NONE_BYTE 0xFF

static const char *const error_strings[] =
{
	"ok",
	"a size the layouts don't have",
	"more machines than the slots",
	"more players than the slots",
	"a machine in a slot past the slots",
	"a player in a slot past the slots",
	"an entry naming a machine, slot or team past the slots",
};

const char *network_game_layout_error_string(int error)
{
	return error >= 0 && error < (int)(sizeof(error_strings) / sizeof(error_strings[0])) ?
		error_strings[error] : "unknown error";
}

static int valid_slots(int count)
{
	return count > 0 && count <= NETWORK_GAME_LAYOUT_PC_SLOTS;
}

unsigned long network_game_layout_size(int machines, int players)
{
	return NETWORK_GAME_LAYOUT_MACHINES_OFFSET + (unsigned long)machines * NETWORK_GAME_LAYOUT_MACHINE_SIZE + 2 +
		(unsigned long)players * NETWORK_GAME_LAYOUT_PLAYER_SIZE + NETWORK_GAME_LAYOUT_TAIL_SIZE;
}

static int read_short(const unsigned char *data)
{
	return (short)(data[0] | (data[1] << 8));
}

/* a slot's index byte (signed char): used if 0 up to slots - 1 */
static int index_in(unsigned char value, int slots)
{
	return (signed char)value >= 0 && (signed char)value < slots;
}

/* an index byte a used entry may hold: one within the slots, or NONE */
static int index_fits(unsigned char value, int slots)
{
	return value == NONE_BYTE || index_in(value, slots);
}

int network_game_layout_convert(const unsigned char *from, int from_machines, int from_players,
	unsigned char *to, int to_machines, int to_players, int *clamped)
{
	const unsigned char *from_machine = from + NETWORK_GAME_LAYOUT_MACHINES_OFFSET;
	const unsigned char *from_player_count = from_machine + from_machines * NETWORK_GAME_LAYOUT_MACHINE_SIZE;
	const unsigned char *from_player = from_player_count + 2;
	const unsigned char *from_tail = from_player + from_players * NETWORK_GAME_LAYOUT_PLAYER_SIZE;
	unsigned char *to_machine = to + NETWORK_GAME_LAYOUT_MACHINES_OFFSET;
	unsigned char *to_player_count = to_machine + to_machines * NETWORK_GAME_LAYOUT_MACHINE_SIZE;
	unsigned char *to_player = to_player_count + 2;
	int machine_count, player_count, index;

	if (clamped)
		*clamped = 0;
	if (!from || !to || !valid_slots(from_machines) || !valid_slots(from_players) || !valid_slots(to_machines) ||
		!valid_slots(to_players))
	{
		return NETWORK_GAME_LAYOUT_BAD_SIZE;
	}
	machine_count = read_short(from + NETWORK_GAME_LAYOUT_MACHINE_COUNT_OFFSET);
	player_count = read_short(from_player_count);
	if (machine_count < 0 || machine_count > to_machines)
		return NETWORK_GAME_LAYOUT_TOO_MANY_MACHINES;
	if (player_count < 0 || player_count > to_players)
		return NETWORK_GAME_LAYOUT_TOO_MANY_PLAYERS;

	/* the used slots past the narrower layout's, and what the used ones name */
	for (index = 0; index < from_machines; index++)
	{
		unsigned char machine_index = from_machine[index * NETWORK_GAME_LAYOUT_MACHINE_SIZE + NETWORK_GAME_LAYOUT_MACHINE_INDEX];

		if (!index_in(machine_index, from_machines))
			continue;
		if (index >= to_machines)
			return NETWORK_GAME_LAYOUT_MACHINE_SLOT;
		if (!index_in(machine_index, to_machines))
			return NETWORK_GAME_LAYOUT_INDEX;
	}
	for (index = 0; index < from_players; index++)
	{
		const unsigned char *player = from_player + index * NETWORK_GAME_LAYOUT_PLAYER_SIZE;

		if (!index_in(player[NETWORK_GAME_LAYOUT_PLAYER_LIST_INDEX], from_players))
			continue;
		if (index >= to_players)
			return NETWORK_GAME_LAYOUT_PLAYER_SLOT;
		if (!index_in(player[NETWORK_GAME_LAYOUT_PLAYER_LIST_INDEX], to_players) ||
			!index_fits(player[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX], to_machines) ||
			!index_fits(player[NETWORK_GAME_LAYOUT_PLAYER_TEAM_INDEX], to_players))
		{
			return NETWORK_GAME_LAYOUT_INDEX;
		}
	}

	/* the header, the slots both layouts have, then empty ones; the tail */
	memmove(to, from, NETWORK_GAME_LAYOUT_MACHINES_OFFSET);
	for (index = 0; index < to_machines; index++)
	{
		unsigned char *machine = to_machine + index * NETWORK_GAME_LAYOUT_MACHINE_SIZE;

		if (index < from_machines)
			memcpy(machine, from_machine + index * NETWORK_GAME_LAYOUT_MACHINE_SIZE, NETWORK_GAME_LAYOUT_MACHINE_SIZE);
		else
		{
			memset(machine, 0, NETWORK_GAME_LAYOUT_MACHINE_SIZE);
			machine[NETWORK_GAME_LAYOUT_MACHINE_INDEX] = NONE_BYTE;
		}
	}
	memcpy(to_player_count, from_player_count, 2);
	for (index = 0; index < to_players; index++)
	{
		unsigned char *player = to_player + index * NETWORK_GAME_LAYOUT_PLAYER_SIZE;

		if (index < from_players)
			memcpy(player, from_player + index * NETWORK_GAME_LAYOUT_PLAYER_SIZE, NETWORK_GAME_LAYOUT_PLAYER_SIZE);
		else
		{
			memset(player, 0, NETWORK_GAME_LAYOUT_PLAYER_SIZE);
			memset(player + NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX, NONE_BYTE, 4);
		}
	}
	memcpy(to_player + to_players * NETWORK_GAME_LAYOUT_PLAYER_SIZE, from_tail, NETWORK_GAME_LAYOUT_TAIL_SIZE);
	if (to[NETWORK_GAME_LAYOUT_MAXIMUM_PLAYERS_OFFSET] > to_players)
	{
		to[NETWORK_GAME_LAYOUT_MAXIMUM_PLAYERS_OFFSET] = (unsigned char)to_players;
		if (clamped)
			*clamped = 1;
	}
	return NETWORK_GAME_LAYOUT_OK;
}

/* ---------- the game types' states */

unsigned long network_game_layout_fields_size(const struct network_game_layout_field *fields, int field_count,
	int slots)
{
	unsigned long size = 0;
	int index;

	for (index = 0; index < field_count; index++)
		size += (unsigned long)fields[index].element_size * (fields[index].per_slot ? (unsigned long)slots : 1);
	return size;
}

int network_game_layout_fold_fields(const unsigned char *from, unsigned long from_size, int from_slots,
	unsigned char *to, unsigned long to_size, int to_slots, const struct network_game_layout_field *fields,
	int field_count, int *dropped)
{
	int index;

	if (dropped)
		*dropped = 0;
	if (!from || !to || !fields || field_count <= 0 || !valid_slots(from_slots) || !valid_slots(to_slots) ||
		from_size != network_game_layout_fields_size(fields, field_count, from_slots) ||
		to_size != network_game_layout_fields_size(fields, field_count, to_slots))
	{
		return NETWORK_GAME_LAYOUT_BAD_SIZE;
	}
	for (index = 0; index < field_count; index++)
	{
		const struct network_game_layout_field *field = &fields[index];
		unsigned long size = field->element_size;
		int element;

		if (!field->per_slot)
		{
			memmove(to, from, size);
			from += size;
			to += size;
			continue;
		}
		for (element = 0; element < from_slots || element < to_slots; element++)
		{
			if (element < from_slots && element < to_slots)
				memmove(to + element * size, from + element * size, size);
			else if (element < to_slots)
				memset(to + element * size, field->blank, size);
			else if (dropped)
			{
				unsigned long offset;

				for (offset = 0; offset < size && from[element * size + offset] == field->blank; offset++)
					;
				if (offset < size)
					(*dropped)++;
			}
		}
		from += size * (unsigned long)from_slots;
		to += size * (unsigned long)to_slots;
	}
	return NETWORK_GAME_LAYOUT_OK;
}
