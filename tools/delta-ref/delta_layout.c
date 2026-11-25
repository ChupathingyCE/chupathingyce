/*
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later

DELTA_LAYOUT.C

The layout facts and record converter of delta_layout.h. The facts are
versioning.md's ("Layout facts by network version") and layouts.json's;
the tests (delta_layout_test.c) hold them to the file.
*/

#include "delta_layout.h"

static void drop(struct delta_layout *layout, unsigned kind)
{
	if (kind > 0 && kind < 256)
		layout->dropped[kind >> 3] = (uint8_t)(layout->dropped[kind >> 3] | (1u << (kind & 7)));
}

int delta_layout_built_in(unsigned version, struct delta_layout *layout)
{
	/* (the MCC-only kinds of 42 on) */
	static const uint8_t mcc_kinds[] = { 97, 98, 99, 100, 103, 104, 105, 106 };
	unsigned rules = version > DELTA_LAYOUT_NEWEST ? DELTA_LAYOUT_NEWEST : version;
	unsigned index;
	uint8_t *bytes = (uint8_t *)layout;

	for (index = 0; index < sizeof(*layout); index++)
		bytes[index] = 0;
	if (version < DELTA_LAYOUT_OLDEST || version > DELTA_LAYOUT_NEWEST + DELTA_LAYOUT_PRESUMED)
		return 0;
	layout->version = version;
	layout->settings_size = DELTA_LAYOUT_SETTINGS_SIZE;
	layout->options_size = DELTA_LAYOUT_OPTIONS_SIZE;
	layout->shield_effects_maximum = 6;
	layout->join_request_version_at = -1;
	if (rules >= 35)
	{
		layout->settings_size = DELTA_LAYOUT_SETTINGS_EXTENDED_SIZE;
		layout->options_size = DELTA_LAYOUT_OPTIONS_EXTENDED_SIZE;
	}
	if (rules >= 36)
		drop(layout, 91);
	if (rules >= 40)
		layout->join_request_extra = 234;
	if (rules >= 41)
	{
		drop(layout, 78);
		drop(layout, 79);
		drop(layout, 94);
		drop(layout, 95);
	}
	if (rules >= 42)
	{
		layout->settings_size = DELTA_LAYOUT_SETTINGS_EPOCH_SIZE;
		layout->join_request_extra = 236;
		layout->join_request_version_at = 234;
		layout->header_extra = 4;
		layout->epoch_kind = 96;
		layout->unit_part_size[6] = 30;
		layout->unit_part_at[6] = DELTA_LAYOUT_PART_LAST;
		for (index = 0; index < sizeof(mcc_kinds); index++)
			drop(layout, mcc_kinds[index]);
	}
	if (rules >= 43)
	{
		layout->unit_part_size[7] = 28;
		layout->unit_part_at[7] = DELTA_LAYOUT_PART_AFTER_UP;
		layout->rider_ejection_up = 200;
		drop(layout, 107);
	}
	return 1;
}

int delta_layout_apply(struct delta_layout *layout, struct delta_layout_fact const *facts, int count)
{
	int refused = 0;
	int index;

	if (!layout->version || layout->version <= DELTA_LAYOUT_NEWEST || !facts)
		return 0;
	for (index = 0; index < count; index++)
	{
		struct delta_layout_fact const *fact = &facts[index];
		unsigned bit;

		if (fact->version <= DELTA_LAYOUT_NEWEST || fact->version > layout->version)
			continue;
		if (fact->options_size)
		{
			/* (the options grow in front of the local data, the record with
			them; each bound checked before any sum) */
			if (fact->options_size >= layout->options_size && fact->options_size <= DELTA_LAYOUT_OPTIONS_MAXIMUM &&
				layout->settings_size + (fact->options_size - layout->options_size) <= DELTA_LAYOUT_SETTINGS_MAXIMUM)
			{
				layout->settings_size += fact->options_size - layout->options_size;
				layout->options_size = fact->options_size;
			}
			else
				refused++;
		}
		if (fact->settings_size)
		{
			if (fact->settings_size >= layout->settings_size && fact->settings_size <= DELTA_LAYOUT_SETTINGS_MAXIMUM)
				layout->settings_size = fact->settings_size;
			else
				refused++;
		}
		if (fact->shield_effects_maximum)
		{
			if (fact->shield_effects_maximum >= layout->shield_effects_maximum && fact->shield_effects_maximum <= 15)
				layout->shield_effects_maximum = fact->shield_effects_maximum;
			else
				refused++;
		}
		if (fact->join_request_said)
		{
			/* (zeros alone: another fill, or past the limit, is not taken) */
			if (fact->join_request_fill == DELTA_LAYOUT_FILL_ZEROS && fact->join_request_extra <= 1024)
			{
				layout->join_request_extra = fact->join_request_extra;
				/* (a version word no longer within it is gone) */
				if (layout->join_request_version_at >= 0 &&
					(unsigned)layout->join_request_version_at + 2 > layout->join_request_extra)
					layout->join_request_version_at = -1;
			}
			else
				refused++;
		}
		if (fact->join_request_version_said)
		{
			/* (the version word within the request's bytes past 0x70) */
			if (layout->join_request_extra >= 2 && fact->join_request_version_at <= layout->join_request_extra - 2)
				layout->join_request_version_at = (int)fact->join_request_version_at;
			else
				refused++;
		}
		if (fact->header_extra)
		{
			if (fact->header_extra <= 8 && !(layout->epoch_kind && fact->header_extra < 4))
				layout->header_extra = fact->header_extra;
			else
				refused++;
		}
		if (fact->epoch_kind)
		{
			if (layout->header_extra >= 4 && fact->epoch_kind >= 93 && fact->epoch_kind <= 255)
				layout->epoch_kind = fact->epoch_kind;
			else
				refused++;
		}
		for (bit = 0; bit < 8; bit++)
		{
			if (!fact->unit_part_size[bit] && !fact->unit_part_at[bit])
				continue;
			/* (bits 0 to 5 are every build's own parts) */
			if (bit >= 6 && fact->unit_part_size[bit] && fact->unit_part_size[bit] <= 64 &&
				(fact->unit_part_at[bit] == DELTA_LAYOUT_PART_AFTER_UP || fact->unit_part_at[bit] == DELTA_LAYOUT_PART_LAST))
			{
				layout->unit_part_size[bit] = fact->unit_part_size[bit];
				layout->unit_part_at[bit] = fact->unit_part_at[bit];
			}
			else
				refused++;
		}
		if (fact->rider_ejection_up)
		{
			if (fact->rider_ejection_up <= 999)
				layout->rider_ejection_up = fact->rider_ejection_up;
			else
				refused++;
		}
		for (bit = 0; bit < 32; bit++)
			layout->dropped[bit] = (uint8_t)(layout->dropped[bit] | (fact->dropped[bit] & (bit ? 0xFF : 0xFE)));
	}
	return refused;
}

int delta_layout_of(unsigned version, struct delta_layout_fact const *facts, int count, struct delta_layout *layout,
	int *refused)
{
	unsigned newest = DELTA_LAYOUT_NEWEST + DELTA_LAYOUT_PRESUMED;
	int index;
	int taken;

	if (refused)
		*refused = 0;
	/* (the newest a table's facts say, and one past it presumed) */
	for (index = 0; facts && index < count; index++)
	{
		if (facts[index].version > DELTA_LAYOUT_NEWEST && facts[index].version < 0xFFFF &&
			facts[index].version + DELTA_LAYOUT_PRESUMED > newest)
			newest = facts[index].version + DELTA_LAYOUT_PRESUMED;
	}
	if (version < DELTA_LAYOUT_OLDEST || version > newest)
	{
		delta_layout_built_in(0, layout);
		return 0;
	}
	delta_layout_built_in(version > DELTA_LAYOUT_NEWEST ? DELTA_LAYOUT_NEWEST : version, layout);
	layout->version = version;
	taken = delta_layout_apply(layout, facts, count);
	if (refused)
		*refused = taken;
	return 1;
}

unsigned delta_layout_local_data_offset(struct delta_layout const *layout)
{
	return DELTA_LAYOUT_HEAD_SIZE + layout->options_size;
}

unsigned delta_layout_trailing_size(struct delta_layout const *layout)
{
	unsigned end = delta_layout_local_data_offset(layout) + DELTA_LAYOUT_LOCAL_DATA_SIZE;

	return layout->settings_size > end ? layout->settings_size - end : 0;
}

int delta_layout_drops(struct delta_layout const *layout, unsigned kind)
{
	return kind > 0 && kind < 256 && ((layout->dropped[kind >> 3] >> (kind & 7)) & 1);
}

void delta_layout_extended_default(uint8_t *extended)
{
	int index;

	for (index = 0; index < DELTA_LAYOUT_EXTENDED_SIZE; index++)
		extended[index] = 0;
	extended[DELTA_LAYOUT_OPTION_FIRST_ZOMBIES] = 1;
	extended[DELTA_LAYOUT_OPTION_ZOMBIE_SPEED] = 150;
	extended[DELTA_LAYOUT_OPTION_SKULL_DAMAGE] = 200;
	extended[DELTA_LAYOUT_OPTION_ZOMBIE_RESPAWN] = 5;
	extended[DELTA_LAYOUT_OPTION_COUNTDOWN] = 10;
	extended[DELTA_LAYOUT_OPTION_ZOMBIE_SHIELDS] = 1;
	extended[DELTA_LAYOUT_OPTION_MELEE_DEMOTES] = 1;
}

static uint8_t ranged(uint8_t value, uint8_t least, uint8_t most, uint8_t otherwise)
{
	return value < least || value > most ? otherwise : value;
}

void delta_layout_extended_sanitize(uint8_t *extended)
{
	int index;

	if (extended[DELTA_LAYOUT_OPTION_MODE] > 3)
		extended[DELTA_LAYOUT_OPTION_MODE] = 0;
	extended[DELTA_LAYOUT_OPTION_HARDCORE] = extended[DELTA_LAYOUT_OPTION_HARDCORE] != 0;
	extended[DELTA_LAYOUT_OPTION_FIRST_ZOMBIES] = ranged(extended[DELTA_LAYOUT_OPTION_FIRST_ZOMBIES], 1, 16, 1);
	extended[DELTA_LAYOUT_OPTION_ZOMBIE_SPEED] = ranged(extended[DELTA_LAYOUT_OPTION_ZOMBIE_SPEED], 100, 200, 150);
	extended[DELTA_LAYOUT_OPTION_SKULL_DAMAGE] = ranged(extended[DELTA_LAYOUT_OPTION_SKULL_DAMAGE], 100, 250, 200);
	extended[DELTA_LAYOUT_OPTION_ZOMBIE_RESPAWN] = ranged(extended[DELTA_LAYOUT_OPTION_ZOMBIE_RESPAWN], 1, 30, 5);
	extended[DELTA_LAYOUT_OPTION_COUNTDOWN] = ranged(extended[DELTA_LAYOUT_OPTION_COUNTDOWN], 0, 30, 10);
	extended[DELTA_LAYOUT_OPTION_ZOMBIE_SHIELDS] = extended[DELTA_LAYOUT_OPTION_ZOMBIE_SHIELDS] != 0;
	if (extended[DELTA_LAYOUT_OPTION_WEAPON_ORDER] > 2)
		extended[DELTA_LAYOUT_OPTION_WEAPON_ORDER] = 0;
	extended[DELTA_LAYOUT_OPTION_MELEE_DEMOTES] = extended[DELTA_LAYOUT_OPTION_MELEE_DEMOTES] != 0;
	for (index = DELTA_LAYOUT_OPTION_MELEE_DEMOTES + 1; index < DELTA_LAYOUT_EXTENDED_SIZE; index++)
		extended[index] = 0;
}

static int record_fits(size_t size, unsigned options)
{
	return options >= DELTA_LAYOUT_OPTIONS_SIZE && options <= DELTA_LAYOUT_OPTIONS_MAXIMUM &&
		size >= (size_t)DELTA_LAYOUT_HEAD_SIZE + options + DELTA_LAYOUT_LOCAL_DATA_SIZE &&
		size <= DELTA_LAYOUT_SETTINGS_MAXIMUM;
}

int delta_layout_convert(uint8_t const *from, size_t from_size, unsigned from_options, uint8_t *to,
	size_t to_size, unsigned to_options, uint8_t *extended)
{
	uint8_t defaults[DELTA_LAYOUT_EXTENDED_SIZE];
	size_t from_tail;
	size_t to_tail;
	size_t index;

	if (!record_fits(from_size, from_options) || !record_fits(to_size, to_options))
		return -1;
	/* (never in place: the parts move, so the two buffers must not overlap) */
	if ((uintptr_t)from < (uintptr_t)to + to_size && (uintptr_t)to < (uintptr_t)from + from_size)
		return -1;
	from_tail = from_size - DELTA_LAYOUT_HEAD_SIZE - from_options - DELTA_LAYOUT_LOCAL_DATA_SIZE;
	to_tail = to_size - DELTA_LAYOUT_HEAD_SIZE - to_options - DELTA_LAYOUT_LOCAL_DATA_SIZE;
	delta_layout_extended_default(defaults);
	if (extended)
	{
		for (index = 0; index < DELTA_LAYOUT_EXTENDED_SIZE; index++)
		{
			size_t at = DELTA_LAYOUT_OPTIONS_SIZE + index;

			extended[index] = at < from_options ? from[DELTA_LAYOUT_HEAD_SIZE + at] : defaults[index];
		}
		delta_layout_extended_sanitize(extended);
	}
	for (index = 0; index < DELTA_LAYOUT_HEAD_SIZE; index++)
		to[index] = from[index];
	for (index = 0; index < to_options; index++)
	{
		uint8_t value = 0;

		if (index < from_options)
			value = from[DELTA_LAYOUT_HEAD_SIZE + index];
		else if (index >= DELTA_LAYOUT_OPTIONS_SIZE && index < DELTA_LAYOUT_OPTIONS_EXTENDED_SIZE)
			value = defaults[index - DELTA_LAYOUT_OPTIONS_SIZE];
		to[DELTA_LAYOUT_HEAD_SIZE + index] = value;
	}
	for (index = 0; index < DELTA_LAYOUT_LOCAL_DATA_SIZE; index++)
		to[DELTA_LAYOUT_HEAD_SIZE + to_options + index] = from[DELTA_LAYOUT_HEAD_SIZE + from_options + index];
	for (index = 0; index < to_tail; index++)
	{
		size_t at = DELTA_LAYOUT_HEAD_SIZE + to_options + DELTA_LAYOUT_LOCAL_DATA_SIZE + index;

		to[at] = index < from_tail ? from[DELTA_LAYOUT_HEAD_SIZE + from_options + DELTA_LAYOUT_LOCAL_DATA_SIZE + index] : 0;
	}
	return 0;
}

int delta_layout_convert_versions(uint8_t const *from, struct delta_layout const *from_layout, uint8_t *to,
	struct delta_layout const *to_layout, uint8_t *extended)
{
	return delta_layout_convert(from, from_layout->settings_size, from_layout->options_size, to,
		to_layout->settings_size, to_layout->options_size, extended);
}
