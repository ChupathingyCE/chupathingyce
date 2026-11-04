/*
NETWORK_RULES_FUZZ.C

libFuzzer's entry for the checks network_rules.c makes of what a host sends
its clients (tools/test_host_fuzz.py builds it): the aim assist policy
(message 90) and the shield effects message (91), a player input's head,
the game settings record (of any size a layout allows, folded into the
older layout as a client does), the further gametype options, a listing's
kind byte, and the layouts a legacy table's facts make (its rows are signed,
but the limits hold whatever they say).
*/

#include "../game/network_rules.h"
#include "../include/delta.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static struct delta_layout_fact source_facts[4];
static int source_count;

static int source(struct delta_layout_fact *facts, int count)
{
	int index;

	for (index = 0; index < source_count && index < count; index++)
		facts[index] = source_facts[index];
	return index;
}

static unsigned read16(const uint8_t *data)
{
	return data[0] | (unsigned)data[1] << 8;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct network_rules_layout layout;
	uint8_t *copy;
	uint8_t extended[NETWORK_RULES_EXTENDED_OPTIONS_SIZE];
	unsigned version;
	int block, effects, hardcore;
	size_t used;

	if (size < 16)
		return 0;
	/* (the messages, each in an allocation of its own size) */
	copy = malloc(size);
	if (!copy)
		return 0;
	memcpy(copy, data, size);
	network_rules_aim_assist_read(copy, (int)size, data[0] & 1, &block);
	if (network_rules_shield_effects_read(copy, (int)size, data[0] & 2, data[1] % 64, &effects) &&
		(effects < 0 || effects > data[1] % 64))
	{
		__builtin_trap();
	}
	{
		int player, bsp, no_friendly_fire;
		int32_t tick, host_tick;

		if (size >= NETWORK_RULES_INPUT_HEAD_SIZE)
			network_rules_input_head_read(copy, &player, &bsp, &no_friendly_fire, &tick, &host_tick);
	}
	free(copy);

	/* (the further options, as any machine takes them, and a kind byte) */
	memcpy(extended, data, sizeof(extended));
	network_rules_extended_options_sanitize(extended);
	network_rules_extended_options_mode(extended, data[2] & 1, data[2] & 2, &hardcore);
	network_rules_kind_name(data[3], (signed char)data[4], data[5] & 1);

	/* (up to four facts from the input, a version, and the layout) */
	used = 16;
	source_count = 0;
	while (source_count < (int)(sizeof(source_facts) / sizeof(source_facts[0])) &&
		used + sizeof(struct delta_layout_fact) <= size && (data[6] >> source_count) & 1)
	{
		memcpy(&source_facts[source_count++], data + used, sizeof(struct delta_layout_fact));
		used += sizeof(struct delta_layout_fact);
	}
	network_rules_set_layout_source(source);
	version = data[7];
	network_rules_layout(version, &layout);
	if (layout.settings_size > NETWORK_RULES_SETTINGS_MAXIMUM ||
		(layout.settings_size && layout.settings_size < NETWORK_RULES_SETTINGS_SIZE) ||
		layout.options_size > NETWORK_RULES_OPTIONS_EXTENDED_SIZE + NETWORK_RULES_LAYOUT_GROWTH_MAXIMUM ||
		layout.join_request_extra < 0 || layout.join_request_extra > NETWORK_RULES_JOIN_REQUEST_EXTRA_LIMIT)
	{
		__builtin_trap();
	}
	network_rules_join_request_extra(version);
	network_rules_layout_newest();
	network_rules_host_settings_size(version, read16(data + 8), data[10] & 1);
	network_rules_host_settings_options(version, read16(data + 8));

	/* (a settings record of a size the client would take, filled from the
	input, folded as network_client_message_handler.c does) */
	{
		unsigned total = read16(data + 12) % (NETWORK_RULES_SETTINGS_MAXIMUM + 1);

		if (network_rules_settings_size_taken(version, total))
		{
			uint8_t *record = calloc(1, total ? total : 1);
			int options_size = network_rules_settings_options_size(version, total);
			size_t index;

			if (record)
			{
				for (index = 0; index < total && used + index < size; index++)
					record[index] = data[used + index];
				network_rules_settings_fold(record, (int)total, options_size, extended);
				network_rules_extended_options_sanitize(extended);
				free(record);
			}
		}
	}
	network_rules_set_layout_source(NULL);
	return 0;
}
