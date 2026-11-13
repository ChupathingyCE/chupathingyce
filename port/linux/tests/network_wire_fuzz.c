/*
NETWORK_WIRE_FUZZ.C

libFuzzer's entry for what a machine of a layout with chupa-43a's facts
reads of another (tools/test_host_fuzz.py builds it): a distributed message
or batch with the header's further bytes, stripped as a receiver does (no
byte read or written past either buffer, the result never longer than the
input, and what it keeps made again and stripped the same), a unit state's
length with the further parts (never past the bytes given), and the join
request's version word a host checks. The layout is 43's as a table row
says it, its fields varied by the input's first bytes within what a fact
may say.
*/

#include "../game/network_rules.h"
#include "../include/delta.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static struct delta_layout_fact source_fact;

static int source(struct delta_layout_fact *facts, int count)
{
	if (count < 1)
		return 0;
	facts[0] = source_fact;
	return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct network_rules_layout layout;
	uint8_t *message, *narrow, *wide, *again;
	uint32_t epoch = 0, want;
	int extra, has_epoch, narrow_size, wide_size, length;

	if (size < 8 || size > 0x1000)
		return 0;
	memset(&source_fact, 0, sizeof(source_fact));
	source_fact.version = 43;
	source_fact.header_extra = (unsigned char)(data[0] % 12);
	source_fact.epoch_kind = (unsigned char)(data[1]);
	source_fact.join_request_said = 1;
	source_fact.join_request_extra = 236;
	source_fact.join_request_version_said = 1;
	source_fact.join_request_version_at = (unsigned short)(data[2] % 240);
	source_fact.unit_part_size[7] = (unsigned char)(data[3] % 70);
	source_fact.unit_part_at[7] = (unsigned char)(data[4] % 4);
	source_fact.unit_part_size[6] = (unsigned char)(data[5] % 70);
	source_fact.unit_part_at[6] = (unsigned char)(data[6] % 4);
	network_rules_set_layout_source(source);
	network_rules_layout(43, &layout);
	if (layout.header_extra > NETWORK_RULES_HEADER_EXTRA_LIMIT ||
		(layout.epoch_kind && layout.header_extra < NETWORK_RULES_EPOCH_SIZE))
	{
		__builtin_trap();
	}
	extra = layout.header_extra;
	has_epoch = layout.epoch_kind != 0;
	want = data[7];
	data += 8;
	size -= 8;

	/* (each buffer of its own size: AddressSanitizer reports a byte past it) */
	message = malloc(size ? size : 1);
	narrow = malloc(size ? size : 1);
	if (!message || !narrow)
	{
		free(message);
		free(narrow);
		network_rules_set_layout_source(NULL);
		return 0;
	}
	memcpy(message, data, size);
	narrow_size = network_rules_header_strip(message, (int)size, narrow, extra, has_epoch, want, 0, &epoch);
	if (narrow_size < 0 || narrow_size > (int)size)
		__builtin_trap();
	if (narrow_size >= NETWORK_RULES_HEADER_SIZE)
	{
		/* (made again with the epoch it carries, and stripped the same) */
		int room = narrow_size + extra * (1 + narrow_size / 8);

		wide = malloc((size_t)room);
		again = malloc((size_t)narrow_size);
		if (wide && again)
		{
			wide_size = network_rules_header_expand(narrow, narrow_size, wide, room, extra, has_epoch, want);
			if (wide_size)
			{
				uint32_t back = 0;

				if (wide_size < narrow_size ||
					network_rules_header_strip(wide, wide_size, again, extra, has_epoch, want, 0, &back) != narrow_size ||
					memcmp(again + 2, narrow + 2, (size_t)narrow_size - 2) || (has_epoch && back != want))
				{
					__builtin_trap();
				}
			}
		}
		free(wide);
		free(again);
	}
	/* (a unit state from the bytes) */
	length = network_rules_unit_state_length(&layout, message, (int)size, 2);
	if (length < 0 || length > (int)size || (length && length < 3))
		__builtin_trap();
	/* (the join request's bytes past 0x70) */
	network_rules_join_request_refused(43, message, (int)size);
	free(message);
	free(narrow);
	network_rules_set_layout_source(NULL);
	return 0;
}
