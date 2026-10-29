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

/* what a client of the old layout with the ranges (0.8.0a to 0.8.0h, the
consoles on chupa-32a) decided, as its code was: the flag, the major and
slot at byte 3, the range bytes when right, else the slot's exact match */
static int old_ranged(const unsigned char *reserved, const char *own)
{
	const unsigned char *slot = reserved + DELTA_ADVERTISED_WIRE_OFFSET + 1;
	const unsigned char *range = reserved + DELTA_ADVERTISED_RANGE_OFFSET;
	struct delta_wire_id mine, mine_oldest, host, host_oldest;
	char text[DELTA_ADVERTISED_WIRE_SIZE + 1];
	int length = 0;

	if (!(reserved[DELTA_ADVERTISED_FLAGS_OFFSET] & DELTA_ADVERTISED_FLAG) ||
		reserved[DELTA_ADVERTISED_WIRE_OFFSET] != DELTA_MAJOR)
	{
		return -1;
	}
	while (length < DELTA_ADVERTISED_WIRE_SIZE && slot[length])
		length++;
	if (!length)
		return -1;
	memcpy(text, slot, (size_t)length);
	text[length] = 0;
	CHECK(delta_compat_oldest(own, &mine_oldest) && delta_wire_parse(own, (int)strlen(own), &mine));
	if (range[0] == DELTA_ADVERTISED_RANGE_VERSION &&
		range[5] == (0xA5 ^ range[0] ^ range[1] ^ range[2] ^ range[3] ^ range[4]))
	{
		host.number = range[1];
		host.letter = range[2];
		host_oldest.number = range[3];
		host_oldest.letter = range[4];
	}
	else if (!delta_wire_parse(text, length, &host))
		return strcmp(text, own) ? 0 : 1;
	else
		host_oldest = host;
	if ((delta_wire_compare(&host, &mine_oldest) >= 0 && delta_wire_compare(&host, &mine) <= 0) ||
		(delta_wire_compare(&mine, &host_oldest) >= 0 && delta_wire_compare(&mine, &host) <= 0))
	{
		return 1;
	}
	return strcmp(text, own) ? 0 : 1;
}

/* (the tag's check again, after a test changed its bytes) */
static void reseal(unsigned char *reserved)
{
	unsigned char *tag = reserved + DELTA_ADVERTISED_TAG_OFFSET;
	unsigned char check = 0xA5;
	int index;

	for (index = 0; index < DELTA_ADVERTISED_TAG_SIZE - 1; index++)
		check = (unsigned char)(check ^ tag[index]);
	tag[DELTA_ADVERTISED_TAG_SIZE - 1] = check;
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
	/* (and the old range's check is not the tag's: the tag is never read as old range bytes) */
	CHECK(!delta_compat_read_range(reserved, RESERVED, &wire, &oldest));
	/* (and without the flag and the old major, no old reader looks at it) */
	CHECK(old_exact(reserved, DELTA_MAJOR, "chupa-29a") == -1 && old_ranged(reserved, "chupa-32a") == -1);

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

	/* a wrong tag byte, major, layout version, check byte, order or letter is no tag */
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
	reseal(reserved);
	CHECK(!delta_compat_read_tag(reserved, RESERVED, &major, &wire, &oldest));
	CHECK(!delta_compat_read_tag(reserved, RESERVED - 1, &major, &wire, &oldest));
	/* (another major: Delta, but joined by the OpenCE number) */
	host(reserved, "chupa-32a");
	reserved[19] = DELTA_MAJOR + 1;
	reseal(reserved);
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
	reseal(h33wide);

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

/* the released clients (the old layout's readers: chupa-29a's exact match,
chupa-32a and chupa-38a's ranges) against a new host. A host with a player
at it writes no old layout, so they join it by its OpenCE number. A
dedicated server sets 0x04 (OpenCE's dedicated flag, the old Delta flag), so
they say HELLO, and in a game that is not Slayer it writes the old major and
slot too, so they join it by Delta whatever it announces; in Slayer (byte 3
OpenCE's kind) they join it by its number */
static void dedicated(unsigned char *reserved, int announced, int slayer)
{
	memset(reserved, 0, RESERVED);
	reserved[0] = (unsigned char)announced;
	reserved[DELTA_ADVERTISED_FLAGS_OFFSET] = 0x01 | 0x04;
	delta_compat_advertise(reserved, RESERVED, DELTA_MAJOR, "chupa-39a");
	if (!slayer)
		delta_compat_advertise_old(reserved, RESERVED, DELTA_MAJOR, "chupa-39a");
}

/* the neutral "OpenCE server" label: an OpenCE dedicated server of 39 or
later (its flag, no Delta), never one of ours (the tag, or the old layout's
Delta flag), never a player's game; and by a listing's flags, OpenCE's
dedicated bit without ours */
static void test_opence_server(void)
{
	unsigned char reserved[RESERVED];
	int slayer;

	/* OpenCE's dedicated server, 39 and later */
	memset(reserved, 0, sizeof(reserved));
	reserved[0] = 39;
	reserved[DELTA_ADVERTISED_FLAGS_OFFSET] = 0x01 | 0x04;
	CHECK(delta_compat_opence_server(reserved, RESERVED));
	reserved[0] = 44;
	CHECK(delta_compat_opence_server(reserved, RESERVED));
	/* (with a kind at byte 3, as OpenCE's 37 writes it) */
	reserved[3] = 2;
	CHECK(delta_compat_opence_server(reserved, RESERVED));
	/* a player's game: no flag */
	reserved[DELTA_ADVERTISED_FLAGS_OFFSET] = 0x01;
	CHECK(!delta_compat_opence_server(reserved, RESERVED));
	/* before 39 the flag was Delta's (an old Chupathingy host) */
	memset(reserved, 0, sizeof(reserved));
	reserved[0] = 38;
	reserved[DELTA_ADVERTISED_FLAGS_OFFSET] = 0x04;
	CHECK(!delta_compat_opence_server(reserved, RESERVED));
	/* our own dedicated servers: the tag, with the old layout or without */
	for (slayer = 0; slayer < 2; slayer++)
	{
		dedicated(reserved, 39, slayer);
		CHECK(!delta_compat_opence_server(reserved, RESERVED));
		dedicated(reserved, 32, slayer);
		CHECK(!delta_compat_opence_server(reserved, RESERVED));
	}
	/* a tag with a bad check byte is no tag */
	dedicated(reserved, 39, 1);
	reserved[DELTA_ADVERTISED_TAG_OFFSET + 8] ^= 0xFF;
	CHECK(delta_compat_opence_server(reserved, RESERVED));
	/* too short to say */
	CHECK(!delta_compat_opence_server(reserved, DELTA_ADVERTISED_FLAGS_OFFSET));

	/* listings: OpenCE's bit alone; ours with it or alone; none */
	CHECK(delta_compat_opence_server_listing(DELTA_COMPAT_LISTING_DEDICATED_OPENCE));
	CHECK(delta_compat_opence_server_listing(DELTA_COMPAT_LISTING_DEDICATED_OPENCE | 0x01 | 0x04));
	CHECK(!delta_compat_opence_server_listing(DELTA_COMPAT_LISTING_DEDICATED_OPENCE | DELTA_COMPAT_LISTING_DEDICATED));
	CHECK(!delta_compat_opence_server_listing(DELTA_COMPAT_LISTING_DEDICATED));
	CHECK(!delta_compat_opence_server_listing(0x01 | 0x02 | 0x04));
	CHECK(DELTA_COMPAT_LISTING_DEDICATED_OPENCE == 32 && DELTA_COMPAT_LISTING_DEDICATED == 128);
}

static void test_old_clients(void)
{
	unsigned char reserved[RESERVED];
	struct delta_wire_id wire, oldest;
	int major, index;

	/* a host with a player at it: the tag alone */
	host(reserved, "chupa-39a");
	CHECK(old_exact(reserved, DELTA_MAJOR, "chupa-29a") == -1);
	CHECK(old_exact(reserved, DELTA_MAJOR, "chupa-39a") == -1);
	CHECK(old_ranged(reserved, "chupa-32a") == -1 && old_ranged(reserved, "chupa-38a") == -1);
	CHECK(reserved[3] == 0);

	/* a dedicated server, not Slayer: the old layout as well */
	dedicated(reserved, 39, 0);
	CHECK(reserved[DELTA_ADVERTISED_WIRE_OFFSET] == DELTA_MAJOR);
	CHECK(!memcmp(reserved + DELTA_ADVERTISED_WIRE_OFFSET + 1, "chupa-29a", 10));
	for (index = DELTA_ADVERTISED_WIRE_OFFSET + 1 + 9; index < DELTA_ADVERTISED_TAG_OFFSET; index++)
		CHECK(reserved[index] == 0);
	/* (released clients: 0.7.5's exact match, and the ranges of 0.8.0a on, by the slot's wire) */
	CHECK(old_exact(reserved, DELTA_MAJOR, "chupa-29a") == 1);
	CHECK(old_ranged(reserved, "chupa-32a") == 1 && old_ranged(reserved, "chupa-38a") == 1);
	CHECK(old_ranged(reserved, "chupa-29a") == 1);
	CHECK(!delta_compat_read_range(reserved, RESERVED, &wire, &oldest));
	/* (the tag and OpenCE's bytes untouched by it) */
	CHECK(delta_compat_read_tag(reserved, RESERVED, &major, &wire, &oldest) && wire.number == 39);
	CHECK(reserved[0] == 39 && reserved[2] == (0x01 | 0x04));
	/* (the new reader: the tag decides) */
	CHECK(delta_compat_speaks_delta(reserved, RESERVED) && decide(reserved, "chupa-32a") == 1);
	CHECK(delta_compat_host_wire(reserved, RESERVED, DELTA_MAJOR, &wire) && wire.number == 39);
	/* (announcing 32, the same) */
	dedicated(reserved, 32, 0);
	CHECK(old_ranged(reserved, "chupa-32a") == 1 && decide(reserved, "chupa-38a") == 1);

	/* a dedicated server in Slayer: no major at byte 3 (OpenCE would name the game SWAT), the flag alone */
	dedicated(reserved, 39, 1);
	CHECK(reserved[3] == 0);
	CHECK(old_exact(reserved, DELTA_MAJOR, "chupa-29a") == -1 && old_ranged(reserved, "chupa-32a") == -1);
	CHECK(delta_compat_speaks_delta(reserved, RESERVED) && decide(reserved, "chupa-32a") == 1);

	/* the old writer: too small, or no wire, writes nothing; a wire of its own is the slot's when older than 29a */
	memset(reserved, 0xEE, sizeof(reserved));
	delta_compat_advertise_old(reserved, RESERVED - 1, DELTA_MAJOR, "chupa-39a");
	CHECK(reserved[3] == 0xEE);
	delta_compat_advertise_old(reserved, RESERVED, DELTA_MAJOR, "not a wire");
	for (index = 3; index < DELTA_ADVERTISED_TAG_OFFSET; index++)
		CHECK(reserved[index] == 0);
	CHECK(reserved[2] == 0xEE && reserved[DELTA_ADVERTISED_TAG_OFFSET] == 0xEE);
	memset(reserved, 0, sizeof(reserved));
	delta_compat_advertise_old(reserved, RESERVED, DELTA_MAJOR, "chupa-28a");
	CHECK(!memcmp(reserved + DELTA_ADVERTISED_WIRE_OFFSET + 1, "chupa-28a", 10));
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

/* a host of the Delta tag (the layout of the hosts to come, docs/delta.md):
OpenCE's bytes at the front (its version, no Delta flag, the game's kind 0),
and the tag at the tail, written here by hand from the layout */
static void tag_host(unsigned char *reserved, int announced, int major, const char *wire, const char *oldest)
{
	struct delta_wire_id own = id(wire), older = id(oldest);
	unsigned char *tag = reserved + DELTA_ADVERTISED_TAG_OFFSET;
	unsigned char check = 0xA5;
	int index;

	memset(reserved, 0, RESERVED);
	reserved[0] = (unsigned char)(announced & 0xFF);
	reserved[1] = (unsigned char)(announced >> 8);
	tag[0] = 'D';
	tag[1] = 'L';
	tag[2] = (unsigned char)major;
	tag[3] = 1;
	tag[4] = (unsigned char)own.number;
	tag[5] = (unsigned char)own.letter;
	tag[6] = (unsigned char)older.number;
	tag[7] = (unsigned char)older.letter;
	for (index = 0; index < 8; index++)
		check = (unsigned char)(check ^ tag[index]);
	tag[8] = check;
}

static void test_tag(void)
{
	unsigned char reserved[RESERVED];
	struct delta_wire_id wire, oldest;
	char theirs[24];
	int major = 0;

	/* the tag sits at the reserved bytes' last nine */
	CHECK(DELTA_ADVERTISED_TAG_OFFSET == 17 && DELTA_ADVERTISED_TAG_OFFSET + DELTA_ADVERTISED_TAG_SIZE == RESERVED);
	tag_host(reserved, 32, DELTA_MAJOR, "chupa-39a", "chupa-28a");
	CHECK(delta_compat_read_tag(reserved, RESERVED, &major, &wire, &oldest) && major == DELTA_MAJOR &&
		wire.number == 39 && oldest.number == 28);
	CHECK(delta_compat_speaks_delta(reserved, RESERVED));
	CHECK(delta_compat_host_wire(reserved, RESERVED, DELTA_MAJOR, &wire) && wire.number == 39);
	/* (no flag and no old slot: before the tag, such a host was OpenCE's) */
	CHECK(!(reserved[DELTA_ADVERTISED_FLAGS_OFFSET] & DELTA_ADVERTISED_FLAG) && reserved[DELTA_ADVERTISED_WIRE_OFFSET] == 0);

	/* the console floor's clients and this build join it by the range, whatever number it announces */
	CHECK(decide(reserved, "chupa-32a") == 1);
	CHECK(decide(reserved, "chupa-38a") == 1);
	CHECK(decide(reserved, DELTA_WIRE) == 1);
	tag_host(reserved, 39, DELTA_MAJOR, "chupa-39a", "chupa-28a");
	CHECK(decide(reserved, "chupa-32a") == 1);
	tag_host(reserved, 11, DELTA_MAJOR, "chupa-39a", "chupa-28a");
	CHECK(decide(reserved, "chupa-32a") == 1);

	/* a tag host that broke with the consoles: refused, named */
	tag_host(reserved, 32, DELTA_MAJOR, "chupa-45a", "chupa-40a");
	CHECK(delta_compat_decide(reserved, RESERVED, DELTA_MAJOR, "chupa-32a", theirs, (int)sizeof(theirs)) == 0 &&
		!strcmp(theirs, "chupa-45a"));
	/* (a wire whose own range is the client's: in the host's range) */
	tag_host(reserved, 32, DELTA_MAJOR, "chupa-45a", "chupa-30a");
	CHECK(decide(reserved, "chupa-32a") == 1);

	/* another major: the numbers decide; still Delta, to answer LEGACY */
	tag_host(reserved, 32, DELTA_MAJOR + 1, "chupa-39a", "chupa-28a");
	CHECK(decide(reserved, "chupa-32a") == -1);
	CHECK(delta_compat_speaks_delta(reserved, RESERVED));
	CHECK(!delta_compat_host_wire(reserved, RESERVED, DELTA_MAJOR, &wire));

	/* a broken tag is no tag: a wrong check, layout version, mark, order, or too short */
	tag_host(reserved, 32, DELTA_MAJOR, "chupa-39a", "chupa-28a");
	reserved[RESERVED - 1] ^= 1;
	CHECK(!delta_compat_read_tag(reserved, RESERVED, &major, &wire, &oldest) && decide(reserved, "chupa-32a") == -1);
	CHECK(!delta_compat_speaks_delta(reserved, RESERVED));
	tag_host(reserved, 32, DELTA_MAJOR, "chupa-39a", "chupa-28a");
	reserved[DELTA_ADVERTISED_TAG_OFFSET + 3] = 2;
	reserved[RESERVED - 1] ^= 1 ^ 2;
	CHECK(!delta_compat_read_tag(reserved, RESERVED, &major, &wire, &oldest));
	tag_host(reserved, 32, DELTA_MAJOR, "chupa-39a", "chupa-28a");
	reserved[DELTA_ADVERTISED_TAG_OFFSET] = 'd';
	reserved[RESERVED - 1] ^= 'D' ^ 'd';
	CHECK(!delta_compat_read_tag(reserved, RESERVED, &major, &wire, &oldest));
	tag_host(reserved, 32, DELTA_MAJOR, "chupa-39a", "chupa-28a");
	CHECK(!delta_compat_read_tag(reserved, RESERVED - 1, &major, &wire, &oldest));
	CHECK(delta_compat_decide(reserved, RESERVED - 1, DELTA_MAJOR, "chupa-32a", theirs, (int)sizeof(theirs)) == -1);

	/* an OpenCE host: neither layout; its numbers decide */
	memset(reserved, 0, sizeof(reserved));
	reserved[0] = 39;
	reserved[DELTA_ADVERTISED_FLAGS_OFFSET] = 0x01;
	CHECK(!delta_compat_speaks_delta(reserved, RESERVED) && decide(reserved, "chupa-32a") == -1);
}

/* the old layout and the tag through the one reader: a client finds the
hosts of both, and the old layout's slot never overrides a tag */
static void test_both_layouts(void)
{
	unsigned char reserved[RESERVED];
	struct delta_wire_id wire;

	/* a host of the old layout (0.8.0h's): the flag, then the slot and range */
	ranged_host(reserved, "chupa-38a", "chupa-29a", 28);
	CHECK(delta_compat_speaks_delta(reserved, RESERVED));
	CHECK(decide(reserved, "chupa-32a") == 1 && decide(reserved, DELTA_WIRE) == 1);
	CHECK(delta_compat_host_wire(reserved, RESERVED, DELTA_MAJOR, &wire) && wire.number == 38);
	/* (the old layout's bytes 20 to 25 are no valid tag: its marks are not 'D' 'L') */
	CHECK(reserved[DELTA_ADVERTISED_TAG_OFFSET] != 'D');
	/* a host of the tag (chupa-39a's) */
	tag_host(reserved, 32, DELTA_MAJOR, "chupa-39a", "chupa-28a");
	CHECK(decide(reserved, "chupa-32a") == 1 && decide(reserved, DELTA_WIRE) == 1);
}

/* the join mark: past the name's end, cleared by the host; a name too long
for it, or OpenCE's (no mark), is no marked one */
static void test_join_mark(void)
{
	unsigned short name[32];
	unsigned short copy[32];
	int index;
	struct
	{
		const char *wire;
		int number;
	} const rows[] = {
#define TEST_ROW(row_wire, row_number) { row_wire, row_number },
		DELTA_WIRE_LEGACY_NEWEST(TEST_ROW)
#undef TEST_ROW
	};

	memset(name, 0, sizeof(name));
	name[0] = 'S';
	name[1] = 'h';
	CHECK(delta_compat_join_mark(name, 32) && name[2] == 0 && name[29] == DELTA_COMPAT_JOIN_MARK_FIRST &&
		name[30] == DELTA_COMPAT_JOIN_MARK_SECOND && name[31] == 0);
	memcpy(copy, name, sizeof(copy));
	CHECK(delta_compat_join_marked(copy, 32));
	for (index = 2; index < 32; index++)
		CHECK(copy[index] == 0);
	CHECK(copy[0] == 'S' && copy[1] == 'h');
	/* (an unmarked name, and one of 28 characters, the longest marked) */
	memset(name, 0, sizeof(name));
	name[0] = 'S';
	CHECK(!delta_compat_join_marked(name, 32));
	for (index = 0; index < 28; index++)
		name[index] = 'a';
	CHECK(delta_compat_join_mark(name, 32) && delta_compat_join_marked(name, 32) && name[27] == 'a');
	/* (29 characters: no room) */
	memset(name, 0, sizeof(name));
	for (index = 0; index < 29; index++)
		name[index] = 'a';
	CHECK(!delta_compat_join_mark(name, 32) && !delta_compat_join_marked(name, 32));
	/* (a name with no end: left as it is) */
	for (index = 0; index < 32; index++)
		name[index] = 'b';
	CHECK(!delta_compat_join_marked(name, 32) && name[31] == 'b');
	/* (the mark's characters inside a name are no mark, and are kept) */
	memset(name, 0, sizeof(name));
	name[0] = DELTA_COMPAT_JOIN_MARK_FIRST;
	name[1] = DELTA_COMPAT_JOIN_MARK_SECOND;
	CHECK(!delta_compat_join_marked(name, 32) && name[0] == DELTA_COMPAT_JOIN_MARK_FIRST);
	CHECK(!delta_compat_join_mark(name, 3) && !delta_compat_join_marked(NULL, 32));

	/* the oldest version a tag reader says is the first tagged wire's */
	for (index = 0; index < (int)(sizeof(rows) / sizeof(rows[0])); index++)
	{
		if (!strcmp(rows[index].wire, "chupa-39a"))
			CHECK(rows[index].number == DELTA_TAG_OLDEST_LEGACY);
	}
}

int main(void)
{
	test_join_mark();
	test_ids();
	test_table();
	test_advertisement();
	test_tag_bytes();
	test_matrix();
	test_old_layout();
	test_opence_hosts();
	test_old_clients();
	test_host_wire();
	test_tag();
	test_both_layouts();
	test_opence_server();
	if (failures)
	{
		fprintf(stderr, "delta_compat_test: %d failures\n", failures);
		return 1;
	}
	printf("delta_compat_test: ok\n");
	return 0;
}
