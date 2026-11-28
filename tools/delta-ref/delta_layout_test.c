/*
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later

DELTA_LAYOUT_TEST.C

delta_layout.c's tests: the built-in facts of 11 to 45 (layouts.json's),
the record converted between 32's, 41's and 44's layouts and Content
Evolved's plain record at 44, a table's facts within their limits (32-bit values that wrap too),
conversion never in place, and fuzz_layout.c's target on the review's
crash and random inputs.
Prints one line, exits 0 when everything held.
*/

#include "delta_layout.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition) \
	do { if (!(condition)) { failures++; printf("delta_layout_test: %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static uint8_t from[DELTA_LAYOUT_SETTINGS_MAXIMUM];
static uint8_t to[DELTA_LAYOUT_SETTINGS_MAXIMUM];
static uint8_t back[DELTA_LAYOUT_SETTINGS_MAXIMUM];

/* a record of a layout whose every byte is its offset's, and whose local
data is 4 marked bytes */
static void fill(uint8_t *record, struct delta_layout const *layout)
{
	unsigned index;
	unsigned local = delta_layout_local_data_offset(layout);

	for (index = 0; index < layout->settings_size; index++)
		record[index] = (uint8_t)(index * 7 + 3);
	for (index = 0; index < 4; index++)
		record[local + index] = (uint8_t)(0xA0 + index);
}

static void built_in(void)
{
	struct delta_layout layout;
	unsigned version;

	CHECK(!delta_layout_built_in(10, &layout));
	for (version = 11; version <= 45; version++)
	{
		CHECK(delta_layout_built_in(version, &layout));
		CHECK(layout.shield_effects_maximum == 6);
		if (version < 35)
		{
			CHECK(layout.settings_size == 13120 && layout.options_size == 0x1C);
			CHECK(delta_layout_local_data_offset(&layout) == 13116 && delta_layout_trailing_size(&layout) == 0);
		}
		else if (version < 42)
		{
			CHECK(layout.settings_size == 13136 && layout.options_size == 0x2C);
			CHECK(delta_layout_local_data_offset(&layout) == 13132 && delta_layout_trailing_size(&layout) == 0);
		}
		else
		{
			CHECK(layout.settings_size == 13200 && layout.options_size == 0x2C);
			CHECK(delta_layout_local_data_offset(&layout) == 13132 && delta_layout_trailing_size(&layout) == 64);
		}
		CHECK(delta_layout_drops(&layout, 91) == (version >= 36));
		CHECK(delta_layout_drops(&layout, 94) == (version >= 41));
		CHECK(delta_layout_drops(&layout, 97) == (version >= 42));
		CHECK(delta_layout_drops(&layout, 107) == (version >= 43));
		CHECK(!delta_layout_drops(&layout, 90) && !delta_layout_drops(&layout, 96));
		CHECK(layout.join_request_extra == (version >= 42 ? 236u : version >= 40 ? 234u : 0u));
		CHECK(layout.join_request_version_at == (version >= 42 ? 234 : -1));
		CHECK(layout.header_extra == (version >= 42 ? 4u : 0u) && layout.epoch_kind == (version >= 42 ? 96u : 0u));
		CHECK(layout.unit_part_size[6] == (version >= 42 ? 30 : 0) && layout.unit_part_size[7] == (version >= 43 ? 28 : 0));
		CHECK(layout.rider_ejection_up == (version >= 43 ? 200u : 0u));
	}
}

static void converts(unsigned from_version, unsigned to_version)
{
	struct delta_layout a, b;
	uint8_t extended[16], defaults[16];
	unsigned index;
	unsigned a_local, b_local;

	delta_layout_built_in(from_version, &a);
	delta_layout_built_in(to_version, &b);
	a_local = delta_layout_local_data_offset(&a);
	b_local = delta_layout_local_data_offset(&b);
	fill(from, &a);
	if (a.options_size > 0x1C)
	{
		/* (SWAT, Hardcore, and the defaults otherwise) */
		delta_layout_extended_default(from + DELTA_LAYOUT_HEAD_SIZE + 0x1C);
		from[DELTA_LAYOUT_HEAD_SIZE + 0x1C] = 1;
		from[DELTA_LAYOUT_HEAD_SIZE + 0x1D] = 1;
	}
	memset(to, 0xEE, sizeof(to));
	CHECK(delta_layout_convert_versions(from, &a, to, &b, extended) == 0);
	CHECK(!memcmp(to, from, DELTA_LAYOUT_HEAD_SIZE));
	CHECK(!memcmp(to + DELTA_LAYOUT_HEAD_SIZE, from + DELTA_LAYOUT_HEAD_SIZE, 0x1C));
	CHECK(!memcmp(to + b_local, from + a_local, 4));
	delta_layout_extended_default(defaults);
	if (a.options_size > 0x1C)
		CHECK(extended[0] == 1 && extended[1] == 1 && !memcmp(extended + 2, defaults + 2, 14));
	else
		CHECK(!memcmp(extended, defaults, 16));
	if (b.options_size > 0x1C)
		CHECK(!memcmp(to + DELTA_LAYOUT_HEAD_SIZE + 0x1C, a.options_size > 0x1C ? from + DELTA_LAYOUT_HEAD_SIZE + 0x1C :
			defaults, 16));
	for (index = 0; index < delta_layout_trailing_size(&b); index++)
		CHECK(to[b_local + 4 + index] == (index < delta_layout_trailing_size(&a) ? from[a_local + 4 + index] : 0));
	CHECK(to[b.settings_size] == 0xEE);
	/* (and back: the source's own bytes, but further options and trailing
	bytes the other had none of, which are the defaults and zeros) */
	CHECK(delta_layout_convert_versions(to, &b, back, &a, NULL) == 0);
	CHECK(!memcmp(back, from, DELTA_LAYOUT_HEAD_SIZE + 0x1C) && !memcmp(back + a_local, from + a_local, 4));
	if (a.options_size > 0x1C)
		CHECK(!memcmp(back + DELTA_LAYOUT_HEAD_SIZE + 0x1C, b.options_size > 0x1C ? from + DELTA_LAYOUT_HEAD_SIZE + 0x1C :
			defaults, 16));
	for (index = 0; index < delta_layout_trailing_size(&a); index++)
		CHECK(back[a_local + 4 + index] == (index < delta_layout_trailing_size(&b) ? from[a_local + 4 + index] : 0));
}

static void content_evolved(void)
{
	/* (its plain record at 44: options 0x58, 13,244 bytes, the 64 MCC
	bytes last) */
	struct delta_layout ours;
	uint8_t extended[16];
	unsigned index;

	delta_layout_built_in(44, &ours);
	for (index = 0; index < 13244; index++)
		from[index] = (uint8_t)(index * 5 + 1);
	from[DELTA_LAYOUT_HEAD_SIZE + 0x1C] = 3;
	CHECK(delta_layout_convert(from, 13244, 0x58, to, ours.settings_size, ours.options_size, extended) == 0);
	CHECK(!memcmp(to, from, DELTA_LAYOUT_HEAD_SIZE + 0x2C));
	CHECK(!memcmp(to + 13132, from + DELTA_LAYOUT_HEAD_SIZE + 0x58, 4 + 64));
	CHECK(extended[0] == 3);
	/* (a record shorter than its options and local data, or past the limit) */
	CHECK(delta_layout_convert(from, 13120, 0x2C, to, 13200, 0x2C, NULL) == -1);
	CHECK(delta_layout_convert(from, 13300, 0x2C, to, 13200, 0x2C, NULL) == -1);
	CHECK(delta_layout_convert(from, 13200, 0x2C, to, 13200, 0x10, NULL) == -1);
}

int delta_layout_fuzz_one(uint8_t const *data, size_t size);

static void table_facts(void)
{
	struct delta_layout layout;
	struct delta_layout_fact facts[3];
	int refused;

	memset(facts, 0, sizeof(facts));
	/* (a fact for 44 is the build's own: not taken) */
	facts[0].version = 44;
	facts[0].options_size = 0x40;
	facts[1].version = 46;
	facts[1].options_size = 0x30;
	facts[1].shield_effects_maximum = 7;
	facts[1].dropped[110 >> 3] = (uint8_t)(1u << (110 & 7));
	facts[2].version = 47;
	facts[2].settings_size = 13000; /* (shrinks: not taken) */
	facts[2].rider_ejection_up = 1000; /* (past 999: not taken) */
	CHECK(delta_layout_of(44, facts, 3, &layout, &refused) && refused == 0 && layout.options_size == 0x2C);
	CHECK(delta_layout_of(46, facts, 3, &layout, &refused) && refused == 0);
	CHECK(layout.options_size == 0x30 && layout.settings_size == 13204 && layout.shield_effects_maximum == 7);
	CHECK(delta_layout_drops(&layout, 110) && delta_layout_local_data_offset(&layout) == 13136);
	CHECK(delta_layout_of(47, facts, 3, &layout, &refused) && refused == 2 && layout.settings_size == 13204 &&
		layout.rider_ejection_up == 200);
	/* (one past the newest fact is presumed; two past is not played) */
	CHECK(delta_layout_of(48, facts, 3, &layout, NULL) && !delta_layout_of(49, facts, 3, &layout, NULL));
	CHECK(!delta_layout_of(46, NULL, 0, &layout, NULL) && delta_layout_of(45, NULL, 0, &layout, NULL));
	CHECK(!delta_layout_of(10, NULL, 0, &layout, NULL));
	/* (built in: 11 to 45 alone) */
	CHECK(!delta_layout_built_in(46, &layout) && layout.settings_size == 0 && !delta_layout_built_in(0xFFFF, &layout));
}

static void wrapped_facts(void)
{
	/* (the review's: 32-bit values that wrap, each refused and the layout
	left as it was) */
	struct delta_layout layout;
	struct delta_layout_fact fact;
	int refused;

	memset(&fact, 0, sizeof(fact));
	fact.version = 45;
	fact.options_size = 0xFFFFFFFFu;
	CHECK(delta_layout_of(45, &fact, 1, &layout, &refused) && refused == 1);
	CHECK(layout.options_size == 0x2C && layout.settings_size == 13200 && delta_layout_local_data_offset(&layout) == 13132);
	fact.options_size = DELTA_LAYOUT_OPTIONS_MAXIMUM + 1;
	CHECK(delta_layout_of(45, &fact, 1, &layout, &refused) && refused == 1 && layout.options_size == 0x2C);
	fact.options_size = 0x2C + 64;
	CHECK(delta_layout_of(45, &fact, 1, &layout, &refused) && refused == 0 && layout.settings_size == 13264);
	memset(&fact, 0, sizeof(fact));
	fact.version = 45;
	fact.join_request_version_said = 1;
	fact.join_request_version_at = 0xFFFFFFFEu;
	CHECK(delta_layout_of(45, &fact, 1, &layout, &refused) && refused == 1 && layout.join_request_version_at == 234);
	fact.join_request_version_at = 235; /* (its second byte past the 236) */
	CHECK(delta_layout_of(45, &fact, 1, &layout, &refused) && refused == 1 && layout.join_request_version_at == 234);
	/* (a request of no bytes past 0x70 has no word, and takes none) */
	fact.join_request_said = 1;
	fact.join_request_extra = 0;
	fact.join_request_version_at = 0;
	CHECK(delta_layout_of(45, &fact, 1, &layout, &refused) && refused == 1);
	CHECK(layout.join_request_extra == 0 && layout.join_request_version_at == -1);
	/* (another fill: the length before kept) */
	memset(&fact, 0, sizeof(fact));
	fact.version = 45;
	fact.join_request_said = 1;
	fact.join_request_extra = 300;
	fact.join_request_fill = DELTA_LAYOUT_FILL_UNKNOWN;
	CHECK(delta_layout_of(45, &fact, 1, &layout, &refused) && refused == 1 && layout.join_request_extra == 236);
	fact.join_request_fill = DELTA_LAYOUT_FILL_ZEROS;
	CHECK(delta_layout_of(45, &fact, 1, &layout, &refused) && refused == 0 && layout.join_request_extra == 300 &&
		layout.join_request_version_at == 234);
}

static void in_place(void)
{
	/* (never in place: refused whole, the buffer untouched) */
	struct delta_layout ours;

	delta_layout_built_in(32, &ours);
	fill(from, &ours);
	memcpy(back, from, 13120);
	CHECK(delta_layout_convert(from, 13120, 0x1C, from, 13120, 0x1C, NULL) == -1);
	CHECK(delta_layout_convert(from, 13120, 0x1C, from + 16, 13136, 0x2C, NULL) == -1);
	CHECK(!memcmp(from, back, 13120));
}

static void fuzzed(void)
{
	/* (the review's crash, then random inputs: an invariant broken aborts) */
	static const uint8_t crash[] = {
		0x00, 0x0b, 0x56, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0x7a, 0x0b, 0x0a, 0xc7 };
	static uint8_t input[600];
	unsigned state = 1124;
	int round;
	size_t index;

	delta_layout_fuzz_one(crash, sizeof(crash));
	for (round = 0; round < 20000; round++)
	{
		size_t size = (size_t)(round % (int)sizeof(input));

		for (index = 0; index < size; index++)
		{
			state = state * 1103515245u + 12345u;
			/* (bytes of all ones half the time: the wrapping values) */
			input[index] = (state >> 30) & 1 ? 0xFF : (uint8_t)(state >> 16);
		}
		delta_layout_fuzz_one(input, size);
	}
}

int main(void)
{
	built_in();
	converts(32, 44);
	converts(41, 44);
	converts(44, 32);
	converts(44, 41);
	converts(32, 41);
	converts(41, 32);
	converts(11, 45);
	content_evolved();
	table_facts();
	wrapped_facts();
	in_place();
	fuzzed();
	printf("delta_layout_test: %s\n", failures ? "failed" : "ok");
	return failures ? 1 : 0;
}
