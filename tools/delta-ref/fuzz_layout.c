/*
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later

FUZZ_LAYOUT.C

A fuzz target for delta_layout.c: hostile table facts and record sizes,
and the invariants every layout keeps (the record within its limits, the
options and local data inside it, the version word inside the join
request's bytes), and conversions between any two layouts so made.
delta_layout_fuzz_one is the body; with libFuzzer,

    clang -fsanitize=fuzzer,address,undefined -DDELTA_LAYOUT_LIBFUZZER \
        delta_layout.c fuzz_layout.c -o fuzz_layout

delta_layout_test.c runs it on its regression inputs and on random ones.
*/

#include "delta_layout.h"

#include <stdlib.h>
#include <string.h>

static uint32_t take32(uint8_t const **at, size_t *left)
{
	uint32_t value = 0;
	int index;

	for (index = 0; index < 4; index++)
	{
		value = value << 8;
		if (*left)
		{
			value |= **at;
			(*at)++;
			(*left)--;
		}
	}
	return value;
}

static uint8_t take8(uint8_t const **at, size_t *left)
{
	uint8_t value = 0;

	if (*left)
	{
		value = **at;
		(*at)++;
		(*left)--;
	}
	return value;
}

/* 0 when the layout keeps every invariant, else 1 */
static int broken(struct delta_layout const *layout)
{
	if (layout->options_size < DELTA_LAYOUT_OPTIONS_SIZE || layout->options_size > DELTA_LAYOUT_OPTIONS_MAXIMUM ||
		layout->settings_size > DELTA_LAYOUT_SETTINGS_MAXIMUM ||
		layout->settings_size < DELTA_LAYOUT_HEAD_SIZE + layout->options_size + DELTA_LAYOUT_LOCAL_DATA_SIZE)
		return 1;
	if (layout->join_request_extra > 1024)
		return 1;
	if (layout->join_request_version_at != -1 && (layout->join_request_version_at < 0 ||
		(unsigned)layout->join_request_version_at + 2 > layout->join_request_extra))
		return 1;
	if (layout->header_extra > 8 || layout->shield_effects_maximum > 15 || layout->rider_ejection_up > 999)
		return 1;
	return 0;
}

int delta_layout_fuzz_one(uint8_t const *data, size_t size)
{
	uint8_t const *at = data;
	size_t left = size;
	uint8_t mode = take8(&at, &left);

	if (mode & 1)
	{
		/* (a conversion of hostile sizes, each buffer exactly its size) */
		size_t from_size = take32(&at, &left) % 14000, to_size = take32(&at, &left) % 14000;
		unsigned from_options = take32(&at, &left), to_options = take32(&at, &left);
		uint8_t *from, *to;
		uint8_t extended[16];
		size_t index;
		int result;

		if (mode & 2)
		{
			from_options %= 256;
			to_options %= 256;
		}
		from = malloc(from_size ? from_size : 1);
		to = malloc(to_size ? to_size : 1);
		if (!from || !to)
		{
			free(from);
			free(to);
			return 0;
		}
		for (index = 0; index < from_size; index++)
			from[index] = left ? take8(&at, &left) : (uint8_t)index;
		result = delta_layout_convert(from, from_size, from_options, to, to_size, to_options,
			(mode & 4) ? extended : NULL);
		if (!result && (mode & 4) && extended[0] > 3)
			abort();
		/* (never in place) */
		if (from_size && delta_layout_convert(from, from_size, from_options, from, from_size, from_options, NULL) != -1)
			abort();
		free(from);
		free(to);
	}
	else
	{
		/* (hostile facts, then a conversion between the layouts they make) */
		struct delta_layout a, b;
		struct delta_layout_fact facts[4];
		unsigned va, vb;
		int index, part;

		memset(facts, 0, sizeof(facts));
		va = 11 + take32(&at, &left) % 50;
		vb = 11 + take32(&at, &left) % 50;
		for (index = 0; index < 4; index++)
		{
			struct delta_layout_fact *fact = &facts[index];

			fact->version = 40 + take32(&at, &left) % 20;
			fact->settings_size = take32(&at, &left);
			fact->options_size = take32(&at, &left);
			if (mode & 2)
			{
				fact->settings_size %= 0x10000;
				fact->options_size %= 0x10000;
			}
			fact->shield_effects_maximum = take32(&at, &left) % 32;
			fact->join_request_extra = take32(&at, &left);
			fact->join_request_said = mode & 8;
			fact->join_request_fill = take8(&at, &left) % 3;
			fact->join_request_version_at = take32(&at, &left);
			fact->join_request_version_said = mode & 16;
			if (mode & 2)
			{
				fact->join_request_extra %= 0x10000;
				fact->join_request_version_at %= 0x10000;
			}
			fact->header_extra = take32(&at, &left) % 16;
			fact->epoch_kind = take32(&at, &left) % 300;
			for (part = 0; part < 8; part++)
			{
				fact->unit_part_size[part] = take8(&at, &left);
				fact->unit_part_at[part] = (uint8_t)(take8(&at, &left) % 4);
			}
			fact->rider_ejection_up = take32(&at, &left) % 2000;
			for (part = 0; part < 32; part++)
				fact->dropped[part] = take8(&at, &left);
		}
		if (delta_layout_of(va, facts, 4, &a, NULL) && broken(&a))
			abort();
		if (delta_layout_of(va, facts, 4, &a, NULL) && delta_layout_of(vb, facts, 4, &b, NULL))
		{
			uint8_t *from, *to;
			uint8_t extended[16];

			if (broken(&b))
				abort();
			from = calloc(a.settings_size, 1);
			to = malloc(b.settings_size);
			if (from && to && delta_layout_convert_versions(from, &a, to, &b, extended))
				abort();
			free(from);
			free(to);
		}
	}
	return 0;
}

#ifdef DELTA_LAYOUT_LIBFUZZER
int LLVMFuzzerTestOneInput(uint8_t const *data, size_t size)
{
	return delta_layout_fuzz_one(data, size);
}
#endif
