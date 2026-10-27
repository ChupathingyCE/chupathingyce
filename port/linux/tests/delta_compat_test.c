/*
DELTA_COMPAT_TEST.C

The wire compatibility ranges' tests (tools/test_delta_compat.py runs them):
the advertisement's Delta tag, the old layout new clients still read, what
released clients make of a new host, OpenCE's own bytes (the kind, the
dedicated flag) never taken for Delta, and the join decision for every pair
of wires that matters.

    delta_compat_test
*/

#include "../src/delta_compat.h"
#include "delta.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

enum { RESERVED = 0x1A };

static struct delta_wire_id id(const char *text)
{
	struct delta_wire_id wire = { 0, 0 };

	CHECK(delta_wire_parse(text, (int)strlen(text), &wire));
	return wire;
}

/* what an exact-match client of the builds before the ranges decided from
the old slot (0.7.5a's delta_peer_advertised_wire_matches, as it was: the
flag, then the slot) */
static int old_exact(const unsigned char *reserved, int major, const char *own)
{
	const unsigned char *wire = reserved + DELTA_ADVERTISED_WIRE_OFFSET + 1;
	int length = 0;

	if (!(reserved[DELTA_ADVERTISED_FLAGS_OFFSET] & DELTA_ADVERTISED_FLAG) ||
		reserved[DELTA_ADVERTISED_WIRE_OFFSET] != major)
	{
		return -1;
	}
	while (length < DELTA_ADVERTISED_WIRE_SIZE && wire[length])
		length++;
	if (!length)
		return -1;
	return length == (int)strlen(own) && !memcmp(wire, own, (size_t)length) ? 1 : 0;
}

static int decide(const unsigned char *reserved, const char *own)
{
	char theirs[24];

	return delta_compat_decide(reserved, RESERVED, DELTA_MAJOR, own, theirs, (int)sizeof(theirs));
}

/* a host of this build: OpenCE's version (32) and distributed flag, then
the tag */
static void host(unsigned char *reserved, const char *wire)
{
	memset(reserved, 0, RESERVED);
	reserved[0] = 32;
	reserved[DELTA_ADVERTISED_FLAGS_OFFSET] = 0x01;
	delta_compat_advertise(reserved, RESERVED, DELTA_MAJOR, wire);
}

/* a host of the old layout before the ranges: the flag, its wire in the
old slot alone */
static void released_host(unsigned char *reserved, const char *wire)
{
	memset(reserved, 0, RESERVED);
	reserved[0] = 32;
	reserved[DELTA_ADVERTISED_FLAGS_OFFSET] = 0x01 | DELTA_ADVERTISED_FLAG;
	reserved[DELTA_ADVERTISED_WIRE_OFFSET] = DELTA_MAJOR;
	memcpy(reserved + DELTA_ADVERTISED_WIRE_OFFSET + 1, wire, strlen(wire));
}

/* a host of the old layout with the range bytes (0.8.0a to 0.8.0h): the
flag, the oldest compatible wire from chupa-29a on in the slot, then its own
wire and oldest in the range bytes */
static void ranged_host(unsigned char *reserved, const char *wire, const char *slot, int oldest)
{
	unsigned char *range = reserved + DELTA_ADVERTISED_RANGE_OFFSET;
	struct delta_wire_id own;

	released_host(reserved, slot);
	CHECK(delta_wire_parse(wire, (int)strlen(wire), &own));
	range[0] = DELTA_ADVERTISED_RANGE_VERSION;
	range[1] = (unsigned char)own.number;
	range[2] = (unsigned char)own.letter;
	range[3] = (unsigned char)oldest;
	range[4] = 'a';
	range[5] = (unsigned char)(0xA5 ^ range[0] ^ range[1] ^ range[2] ^ range[3] ^ range[4]);
}

static void test_ids(void)
{
	struct delta_wire_id a = id("chupa-29a"), b = id("chupa-32a"), c = id("chupa-32b"), d = { 0, 0 };
	char text[16];

	CHECK(a.number == 29 && a.letter == 'a' && b.number == 32);
	CHECK(delta_wire_compare(&a, &b) < 0 && delta_wire_compare(&b, &a) > 0 && delta_wire_compare(&b, &b) == 0);
	CHECK(delta_wire_compare(&b, &c) < 0);
	delta_wire_format(&b, text, (int)sizeof(text));
	CHECK(!strcmp(text, "chupa-32a"));
	/* (not wire IDs) */
	CHECK(!delta_wire_parse("", 0, &d) && !delta_wire_parse("chupa-", 6, &d) && !delta_wire_parse("chupa-a", 7, &d));
	CHECK(!delta_wire_parse("chupa-32", 8, &d) && !delta_wire_parse("chupa-32A", 9, &d));
	CHECK(!delta_wire_parse("chupa-0a", 8, &d) && !delta_wire_parse("chupa-999a", 10, &d));
	CHECK(!delta_wire_parse("chupa-3 2a", 10, &d) && !delta_wire_parse("xhupa-32a", 9, &d));
	CHECK(!delta_wire_parse("chupa-32ab", 10, &d) && !delta_wire_parse("chupa-00032a", 12, &d));
}

static void test_table(void)
{
	struct delta_wire_id oldest;

	CHECK(delta_compat_oldest("chupa-32a", &oldest) && delta_wire_compare(&oldest, &(struct delta_wire_id){ 28, 'a' }) == 0);
	CHECK(delta_compat_oldest("chupa-29a", &oldest) && oldest.number == 28);
	/* (no row: itself alone) */
	CHECK(delta_compat_oldest("chupa-28a", &oldest) && oldest.number == 28);
	CHECK(delta_compat_oldest("chupa-40a", &oldest) && oldest.number == 40);
	CHECK(!delta_compat_oldest("nonsense", &oldest));
	/* (this build's own row never reaches past its own wire) */
	CHECK(delta_compat_oldest(DELTA_WIRE, &oldest) && delta_wire_compare(&oldest, &(struct delta_wire_id){ 1, 'a' }) > 0);
}

static void test_advertisement(void)
{
	unsigned char reserved[RESERVED];
	struct delta_wire_id wire, oldest;
	int index, major;

	host(reserved, "chupa-32a");
	/* the tag at the tail: 'D', 'L', the major, the layout version, own, oldest, check */
	CHECK(DELTA_ADVERTISED_TAG_OFFSET == 17 && DELTA_ADVERTISED_TAG_OFFSET + DELTA_ADVERTISED_TAG_SIZE == RESERVED);
	CHECK(reserved[17] == 'D' && reserved[18] == 'L' && reserved[19] == DELTA_MAJOR && reserved[20] == 1);
	CHECK(reserved[21] == 32 && reserved[22] == 'a' && reserved[23] == 28 && reserved[24] == 'a');
	CHECK(reserved[25] == (0xA5 ^ 'D' ^ 'L' ^ DELTA_MAJOR ^ 1 ^ 32 ^ 'a' ^ 28 ^ 'a'));
	CHECK(delta_compat_read_tag(reserved, RESERVED, &major, &wire, &oldest) && major == DELTA_MAJOR &&
		wire.number == 32 && oldest.number == 28);
	/* OpenCE's bytes are left alone: the version, the flags (no 0x04), the kind and what follows it */
	CHECK(reserved[0] == 32 && reserved[1] == 0 && reserved[2] == 0x01);
	for (index = 3; index < DELTA_ADVERTISED_TAG_OFFSET; index++)
		CHECK(reserved[index] == 0);
	/* (and the old range's check is not the tag's: a new host is never read as an old one) */
	CHECK(!delta_compat_read_range(reserved, RESERVED, &wire, &oldest));

	/* this build's own wire, and a wire with no row */
	host(reserved, DELTA_WIRE);
	CHECK(delta_compat_read_tag(reserved, RESERVED, &major, &wire, &oldest) && oldest.number == 28);
	host(reserved, "chupa-40a");
	CHECK(delta_compat_read_tag(reserved, RESERVED, &major, &wire, &oldest) && wire.number == 40 &&
		oldest.number == 40);
	/* too small, or no wire: no tag */
	memset(reserved, 0, sizeof(reserved));
	delta_compat_advertise(reserved, RESERVED - 1, DELTA_MAJOR, "chupa-32a");
	delta_compat_advertise(reserved, RESERVED, DELTA_MAJOR, "not a wire");
	for (index = 0; index < RESERVED; index++)
		CHECK(reserved[index] == 0);
	/* (a tag's bytes in the way are cleared, the rest kept) */
	memset(reserved, 0xEE, sizeof(reserved));
	delta_compat_advertise(reserved, RESERVED, DELTA_MAJOR, "not a wire");
	CHECK(reserved[16] == 0xEE && reserved[17] == 0 && reserved[25] == 0);
}

static void test_tag_bytes(void)
{
	unsigned char reserved[RESERVED];
	struct delta_wire_id wire, oldest;
	int major, index;

	/* a wrong tag byte, layout version, check byte, order or letter is no tag */
	for (index = DELTA_ADVERTISED_TAG_OFFSET; index < RESERVED; index++)
	{
		host(reserved, "chupa-32a");
		reserved[index] ^= 0x20;
		CHECK(!delta_compat_read_tag(reserved, RESERVED, &major, &wire, &oldest));
		CHECK(!delta_compat_speaks_delta(reserved, RESERVED));
		CHECK(decide(reserved, "chupa-32a") == -1);
	}
	host(reserved, "chupa-32a");
	reserved[23] = 33;
	reserved[25] = (unsigned char)(0xA5 ^ 'D' ^ 'L' ^ DELTA_MAJOR ^ 1 ^ 32 ^ 'a' ^ 33 ^ 'a');
	CHECK(!delta_compat_read_tag(reserved, RESERVED, &major, &wire, &oldest));
	CHECK(!delta_compat_read_tag(reserved, RESERVED - 1, &major, &wire, &oldest));
	/* (another major: Delta, but joined by the OpenCE number) */
	host(reserved, "chupa-32a");
	reserved[19] = DELTA_MAJOR + 1;
	reserved[25] ^= (unsigned char)(DELTA_MAJOR ^ (DELTA_MAJOR + 1));
	CHECK(delta_compat_read_tag(reserved, RESERVED, &major, &wire, &oldest) && major == DELTA_MAJOR + 1);
	CHECK(delta_compat_speaks_delta(reserved, RESERVED) && decide(reserved, "chupa-32a") == -1);
	CHECK(!delta_compat_host_wire(reserved, RESERVED, DELTA_MAJOR, &wire));
}

static void test_matrix(void)
{
	unsigned char h28[RESERVED], h29[RESERVED], h32[RESERVED], h38[RESERVED], h39[RESERVED], h33[RESERVED],
		h33wide[RESERVED];
	char theirs[24];

	host(h28, "chupa-28a");
	host(h29, "chupa-29a");
	host(h32, "chupa-32a");
	host(h38, "chupa-38a");
	host(h39, "chupa-39a");
	/* a future wire that breaks (no row: its own oldest), and one that plays with 29 on */
	host(h33, "chupa-33a");
	host(h33wide, "chupa-33a");
	h33wide[23] = 29;
	h33wide[25] = (unsigned char)(0xA5 ^ 'D' ^ 'L' ^ DELTA_MAJOR ^ 1 ^ 33 ^ 'a' ^ 29 ^ 'a');

	/* a 39a client: every wire of ours from 28a on */
	CHECK(decide(h39, "chupa-39a") == 1 && decide(h38, "chupa-39a") == 1 && decide(h32, "chupa-39a") == 1);
	CHECK(decide(h29, "chupa-39a") == 1 && decide(h28, "chupa-39a") == 1);
	/* a 32a client of this code (a console after its submodule takes it): the 39a host's range has it */
	CHECK(decide(h39, "chupa-32a") == 1);
	CHECK(decide(h32, "chupa-32a") == 1);
	CHECK(decide(h29, "chupa-32a") == 1);
	CHECK(decide(h28, "chupa-32a") == 1);
	CHECK(decide(h33, "chupa-32a") == 0);
	CHECK(decide(h33wide, "chupa-32a") == 1);
	/* a 29a client */
	CHECK(decide(h39, "chupa-29a") == 1);
	CHECK(decide(h32, "chupa-29a") == 1);
	CHECK(decide(h33, "chupa-29a") == 0);
	CHECK(decide(h33wide, "chupa-29a") == 1);
	/* a 28a client */
	CHECK(decide(h39, "chupa-28a") == 1);
	CHECK(decide(h28, "chupa-28a") == 1);
	CHECK(decide(h33, "chupa-28a") == 0);
	/* a client of a breaking future wire: a host whose range has it plays (39a: 28a to 39a), the others not */
	CHECK(decide(h39, "chupa-33a") == 1);
	CHECK(decide(h32, "chupa-33a") == 0);
	CHECK(decide(h33wide, "chupa-33a") == 1);

	/* the message names the host's wire */
	CHECK(delta_compat_decide(h33, RESERVED, DELTA_MAJOR, "chupa-32a", theirs, (int)sizeof(theirs)) == 0 &&
		!strcmp(theirs, "chupa-33a"));
	/* (a small buffer is cut, ended) */
	CHECK(delta_compat_decide(h33, RESERVED, DELTA_MAJOR, "chupa-32a", theirs, 5) == 0 && !strcmp(theirs, "chup"));

	/* the OpenCE numbers decide when it does not say: nothing, too short, this client no wire */
	memset(h32, 0, sizeof(h32));
	CHECK(decide(h32, "chupa-32a") == -1);
	host(h32, "chupa-32a");
	CHECK(delta_compat_decide(h32, DELTA_ADVERTISED_WIRE_OFFSET + 5, DELTA_MAJOR, "chupa-32a", theirs, (int)sizeof(theirs)) == -1);
	CHECK(delta_compat_decide(h32, RESERVED, DELTA_MAJOR, "not a wire", theirs, (int)sizeof(theirs)) == -1);
}

/* new clients still read the old layout: hosts released before the tag */
static void test_old_layout(void)
{
	unsigned char h29old[RESERVED], h32[RESERVED], h38[RESERVED];
	struct delta_wire_id wire;
	char theirs[24];

	released_host(h29old, "chupa-29a");
	ranged_host(h32, "chupa-32a", "chupa-29a", 28);
	ranged_host(h38, "chupa-38a", "chupa-29a", 28);
	CHECK(delta_compat_speaks_delta(h29old, RESERVED) && delta_compat_speaks_delta(h38, RESERVED));
	CHECK(decide(h29old, "chupa-39a") == 1 && decide(h32, "chupa-39a") == 1 && decide(h38, "chupa-39a") == 1);
	CHECK(decide(h38, "chupa-32a") == 1 && decide(h29old, "chupa-32a") == 1);
	CHECK(decide(h38, "chupa-40a") == 0);
	CHECK(delta_compat_host_wire(h38, RESERVED, DELTA_MAJOR, &wire) && wire.number == 38);
	CHECK(delta_compat_host_wire(h29old, RESERVED, DELTA_MAJOR, &wire) && wire.number == 29);
	released_host(h29old, "chupa-12a");
	CHECK(delta_compat_decide(h29old, RESERVED, DELTA_MAJOR, "chupa-32a", theirs, (int)sizeof(theirs)) == 0 &&
		!strcmp(theirs, "chupa-12a"));
	/* (a broken range falls back to the old slot's rule: 29a in the slot is in 32a's range) */
	ranged_host(h32, "chupa-32a", "chupa-29a", 28);
	h32[25] ^= 1;
	CHECK(decide(h32, "chupa-32a") == 1 && decide(h32, "chupa-29a") == 1);
	/* another major, or no flag: the numbers decide */
	ranged_host(h32, "chupa-32a", "chupa-29a", 28);
	h32[DELTA_ADVERTISED_WIRE_OFFSET] = DELTA_MAJOR + 1;
	CHECK(decide(h32, "chupa-32a") == -1);
	ranged_host(h32, "chupa-32a", "chupa-29a", 28);
	h32[DELTA_ADVERTISED_FLAGS_OFFSET] = 0x01;
	CHECK(decide(h32, "chupa-32a") == -1 && !delta_compat_speaks_delta(h32, RESERVED));
	/* a slot of junk is refused, as it was, before OpenCE took the flag */
	released_host(h29old, "chupa-?\x01 junk");
	CHECK(decide(h29old, "chupa-32a") == 0);
	/* a host before the wire was advertised: the flag alone, Delta, joined by its number */
	memset(h32, 0, sizeof(h32));
	h32[0] = 20;
	h32[DELTA_ADVERTISED_FLAGS_OFFSET] = 0x01 | DELTA_ADVERTISED_FLAG;
	CHECK(delta_compat_speaks_delta(h32, RESERVED) && decide(h32, "chupa-32a") == -1);
}

/* OpenCE's own bytes are never Delta: a host of 37 or later's kind at byte 3
(1, SWAT, is our major), and 39's dedicated flag (our old flag) */
static void test_opence_hosts(void)
{
	unsigned char reserved[RESERVED];
	struct delta_wire_id wire;
	int kind;

	for (kind = 0; kind < 256; kind += 0x41)
	{
		memset(reserved, 0, sizeof(reserved));
		reserved[0] = 39;
		reserved[DELTA_ADVERTISED_FLAGS_OFFSET] = 0x01 | 0x04;
		reserved[3] = (unsigned char)kind;
		CHECK(!delta_compat_speaks_delta(reserved, RESERVED));
		CHECK(decide(reserved, "chupa-39a") == -1 && decide(reserved, "chupa-32a") == -1);
		CHECK(!delta_compat_host_wire(reserved, RESERVED, DELTA_MAJOR, &wire));
		/* (and bytes of theirs past the kind, should they come: not a wire) */
		memcpy(reserved + 4, "something", 9);
		CHECK(!delta_compat_speaks_delta(reserved, RESERVED) && decide(reserved, "chupa-39a") == -1);
	}
	/* a kind of 1 on a host of 37 without the flag */
	memset(reserved, 0, sizeof(reserved));
	reserved[0] = 37;
	reserved[2] = 0x01;
	reserved[3] = 1;
	CHECK(!delta_compat_speaks_delta(reserved, RESERVED) && decide(reserved, "chupa-39a") == -1);
}

/* the released clients (the old layout's readers: chupa-32a, chupa-38a)
against a new host: no flag and no major at byte 3, so they join it by its
OpenCE number; a dedicated server announcing 39 sets 0x04 (OpenCE's
dedicated flag), and they still find no wire, so join by number */
static void test_old_clients(void)
{
	unsigned char reserved[RESERVED];

	host(reserved, "chupa-39a");
	CHECK(old_exact(reserved, DELTA_MAJOR, "chupa-29a") == -1);
	CHECK(old_exact(reserved, DELTA_MAJOR, "chupa-39a") == -1);
	reserved[0] = 39;
	reserved[DELTA_ADVERTISED_FLAGS_OFFSET] |= 0x04;
	CHECK(old_exact(reserved, DELTA_MAJOR, "chupa-32a") == -1);
	/* (the new reader: the tag decides, whatever the flag) */
	CHECK(delta_compat_speaks_delta(reserved, RESERVED) && decide(reserved, "chupa-32a") == 1);
}

static void test_host_wire(void)
{
	unsigned char reserved[RESERVED];
	struct delta_wire_id wire;

	host(reserved, "chupa-39a");
	CHECK(delta_compat_host_wire(reserved, RESERVED, DELTA_MAJOR, &wire) && wire.number == 39);
	released_host(reserved, "chupa-29a");
	CHECK(delta_compat_host_wire(reserved, RESERVED, DELTA_MAJOR, &wire) && wire.number == 29);
	memset(reserved, 0, sizeof(reserved));
	CHECK(!delta_compat_host_wire(reserved, RESERVED, DELTA_MAJOR, &wire));
}

int main(void)
{
	test_ids();
	test_table();
	test_advertisement();
	test_tag_bytes();
	test_matrix();
	test_old_layout();
	test_opence_hosts();
	test_old_clients();
	test_host_wire();
	if (failures)
	{
		fprintf(stderr, "delta_compat_test: %d failures\n", failures);
		return 1;
	}
	printf("delta_compat_test: ok\n");
	return 0;
}
