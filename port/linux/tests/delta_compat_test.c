/*
DELTA_COMPAT_TEST.C

The wire compatibility ranges' tests (tools/test_delta_compat.py runs them):
the advertisement's bytes, the old slot an exact-match client reads, and the
join decision for every pair of wires that matters.

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
the old slot (0.7.5a's delta_peer_advertised_wire_matches, as it was) */
static int old_exact(const unsigned char *reserved, int major, const char *own)
{
	const unsigned char *wire = reserved + DELTA_ADVERTISED_WIRE_OFFSET + 1;
	int length = 0;

	if (reserved[DELTA_ADVERTISED_WIRE_OFFSET] != major)
		return -1;
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

static void host(unsigned char *reserved, const char *wire)
{
	memset(reserved, 0, RESERVED);
	delta_compat_advertise(reserved, RESERVED, DELTA_MAJOR, wire);
}

/* a host of a build before the ranges: its wire in the old slot alone */
static void released_host(unsigned char *reserved, const char *wire)
{
	memset(reserved, 0, RESERVED);
	reserved[DELTA_ADVERTISED_WIRE_OFFSET] = DELTA_MAJOR;
	memcpy(reserved + DELTA_ADVERTISED_WIRE_OFFSET + 1, wire, strlen(wire));
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
	int index;

	host(reserved, "chupa-32a");
	/* the Delta major, then the OLDEST compatible wire a released client knows in the old slot */
	CHECK(reserved[DELTA_ADVERTISED_WIRE_OFFSET] == DELTA_MAJOR);
	CHECK(!strncmp((const char *)reserved + DELTA_ADVERTISED_WIRE_OFFSET + 1, "chupa-29a", DELTA_ADVERTISED_WIRE_SIZE));
	CHECK(reserved[DELTA_ADVERTISED_WIRE_OFFSET + 1 + 9] == 0);
	/* the range bytes: version, own, oldest, check */
	CHECK(DELTA_ADVERTISED_RANGE_OFFSET == 20 && DELTA_ADVERTISED_RANGE_OFFSET + DELTA_ADVERTISED_RANGE_SIZE == RESERVED);
	CHECK(reserved[20] == 1 && reserved[21] == 32 && reserved[22] == 'a' && reserved[23] == 28 && reserved[24] == 'a');
	CHECK(reserved[25] == (0xA5 ^ 1 ^ 32 ^ 'a' ^ 28 ^ 'a'));
	CHECK(delta_compat_read_range(reserved, RESERVED, &wire, &oldest) && wire.number == 32 && oldest.number == 28);
	/* the port's version and flags are left alone */
	for (index = 0; index < DELTA_ADVERTISED_WIRE_OFFSET; index++)
		CHECK(reserved[index] == 0);

	/* 29a's own advertisement: itself in the slot */
	host(reserved, "chupa-29a");
	CHECK(!strcmp((const char *)reserved + DELTA_ADVERTISED_WIRE_OFFSET + 1, "chupa-29a"));
	CHECK(delta_compat_read_range(reserved, RESERVED, &wire, &oldest) && wire.number == 29 && oldest.number == 28);
	/* a wire with no row: itself in both places */
	host(reserved, "chupa-40a");
	CHECK(!strcmp((const char *)reserved + DELTA_ADVERTISED_WIRE_OFFSET + 1, "chupa-40a"));
	CHECK(delta_compat_read_range(reserved, RESERVED, &wire, &oldest) && wire.number == 40 && oldest.number == 40);
	/* a wire older than the first advertised cannot be a slot newer than itself */
	host(reserved, "chupa-28a");
	CHECK(!strcmp((const char *)reserved + DELTA_ADVERTISED_WIRE_OFFSET + 1, "chupa-28a"));
	/* too small, or no wire: nothing */
	memset(reserved, 0, sizeof(reserved));
	delta_compat_advertise(reserved, DELTA_ADVERTISED_RANGE_OFFSET + 5, DELTA_MAJOR, "chupa-32a");
	delta_compat_advertise(reserved, RESERVED, DELTA_MAJOR, "not a wire");
	for (index = 0; index < RESERVED; index++)
		CHECK(reserved[index] == 0);
	/* (bytes in the way are cleared) */
	memset(reserved, 0xEE, sizeof(reserved));
	delta_compat_advertise(reserved, RESERVED, DELTA_MAJOR, "chupa-29a");
	CHECK(reserved[DELTA_ADVERTISED_WIRE_OFFSET + 1 + 9] == 0 && reserved[DELTA_ADVERTISED_WIRE_OFFSET + 16] == 0);
}

static void test_range_bytes(void)
{
	unsigned char reserved[RESERVED];
	struct delta_wire_id wire, oldest;

	/* a wrong layout version, check byte, order or letter is not a range */
	host(reserved, "chupa-32a");
	reserved[20] = 2;
	CHECK(!delta_compat_read_range(reserved, RESERVED, &wire, &oldest));
	host(reserved, "chupa-32a");
	reserved[25] ^= 1;
	CHECK(!delta_compat_read_range(reserved, RESERVED, &wire, &oldest));
	host(reserved, "chupa-32a");
	reserved[23] = 33;
	reserved[25] = (unsigned char)(0xA5 ^ 1 ^ 32 ^ 'a' ^ 33 ^ 'a');
	CHECK(!delta_compat_read_range(reserved, RESERVED, &wire, &oldest));
	host(reserved, "chupa-32a");
	reserved[22] = 'A';
	reserved[25] = (unsigned char)(0xA5 ^ 1 ^ 32 ^ 'A' ^ 28 ^ 'a');
	CHECK(!delta_compat_read_range(reserved, RESERVED, &wire, &oldest));
	CHECK(!delta_compat_read_range(reserved, RESERVED - 1, &wire, &oldest));
	/* (a broken range falls back to the old slot's rule: 29a in the slot is in 32a's range) */
	host(reserved, "chupa-32a");
	reserved[25] ^= 1;
	CHECK(decide(reserved, "chupa-32a") == 1 && decide(reserved, "chupa-29a") == 1);
}

static void test_matrix(void)
{
	unsigned char h28[RESERVED], h29[RESERVED], h29old[RESERVED], h32[RESERVED], h33[RESERVED], h33wide[RESERVED];
	char theirs[24];

	host(h28, "chupa-28a");
	host(h29, "chupa-29a");
	released_host(h29old, "chupa-29a");
	host(h32, "chupa-32a");
	/* a future wire that breaks (no row: its own oldest), and one that plays with 29 on */
	host(h33, "chupa-33a");
	host(h33wide, "chupa-33a");
	h33wide[23] = 29;
	h33wide[25] = (unsigned char)(0xA5 ^ 1 ^ 33 ^ 'a' ^ 29 ^ 'a');
	memcpy(h33wide + DELTA_ADVERTISED_WIRE_OFFSET + 1, "chupa-29a\0\0\0\0\0\0\0", 16);

	/* a 32a client */
	CHECK(decide(h32, "chupa-32a") == 1);
	CHECK(decide(h29, "chupa-32a") == 1);
	CHECK(decide(h29old, "chupa-32a") == 1);
	CHECK(decide(h28, "chupa-32a") == 1);
	CHECK(decide(h33, "chupa-32a") == 0);
	CHECK(decide(h33wide, "chupa-32a") == 1);
	/* a 29a client (the host of 32a plays with it, whatever the client's table) */
	CHECK(decide(h32, "chupa-29a") == 1);
	CHECK(decide(h29, "chupa-29a") == 1);
	CHECK(decide(h28, "chupa-29a") == 1);
	CHECK(decide(h33, "chupa-29a") == 0);
	CHECK(decide(h33wide, "chupa-29a") == 1);
	/* a 28a client of this code (the Xbox after it takes it): 29a and 32a hosts' ranges have it */
	CHECK(decide(h32, "chupa-28a") == 1);
	CHECK(decide(h29, "chupa-28a") == 1);
	CHECK(decide(h28, "chupa-28a") == 1);
	CHECK(decide(h33, "chupa-28a") == 0);
	/* a client of a breaking future wire, against every host of ours */
	CHECK(decide(h32, "chupa-33a") == 0);
	CHECK(decide(h29old, "chupa-33a") == 0);
	/* a client further on that plays with 29 on (its row is its own to say; here a host's) */
	CHECK(decide(h33wide, "chupa-33a") == 1);

	/* the message names the host's wire */
	CHECK(delta_compat_decide(h33, RESERVED, DELTA_MAJOR, "chupa-32a", theirs, (int)sizeof(theirs)) == 0 &&
		!strcmp(theirs, "chupa-33a"));
	released_host(h29old, "chupa-12a");
	CHECK(delta_compat_decide(h29old, RESERVED, DELTA_MAJOR, "chupa-32a", theirs, (int)sizeof(theirs)) == 0 &&
		!strcmp(theirs, "chupa-12a"));
	/* (a small buffer is cut, ended) */
	CHECK(delta_compat_decide(h33, RESERVED, DELTA_MAJOR, "chupa-32a", theirs, 5) == 0 && !strcmp(theirs, "chup"));

	/* the OpenCE numbers decide when it does not say: no wire, another major, a host older than the slot, too short */
	memset(h32, 0, sizeof(h32));
	CHECK(decide(h32, "chupa-32a") == -1);
	host(h32, "chupa-32a");
	h32[DELTA_ADVERTISED_WIRE_OFFSET] = DELTA_MAJOR + 1;
	CHECK(decide(h32, "chupa-32a") == -1);
	host(h32, "chupa-32a");
	CHECK(delta_compat_decide(h32, DELTA_ADVERTISED_WIRE_OFFSET + 5, DELTA_MAJOR, "chupa-32a", theirs, (int)sizeof(theirs)) == -1);
	CHECK(delta_compat_decide(h32, RESERVED, DELTA_MAJOR, "not a wire", theirs, (int)sizeof(theirs)) == -1);
	/* a slot of junk is refused, as it was */
	released_host(h29old, "chupa-?\x01 junk");
	CHECK(decide(h29old, "chupa-32a") == 0);
}

/* the builds before the ranges: an exact-match client, whatever host of ours advertises, joins every host
whose old slot is its own wire, and a host of 32a is joinable by a client of 29a */
static void test_old_clients(void)
{
	unsigned char reserved[RESERVED];

	host(reserved, "chupa-32a");
	CHECK(old_exact(reserved, DELTA_MAJOR, "chupa-29a") == 1);
	CHECK(old_exact(reserved, DELTA_MAJOR, "chupa-32a") == 0);
	host(reserved, "chupa-29a");
	CHECK(old_exact(reserved, DELTA_MAJOR, "chupa-29a") == 1);
	/* (its own wire when it has no older one) */
	host(reserved, "chupa-40a");
	CHECK(old_exact(reserved, DELTA_MAJOR, "chupa-40a") == 1 && old_exact(reserved, DELTA_MAJOR, "chupa-29a") == 0);
	/* (the major, the OpenCE numbers and flags are not touched) */
	host(reserved, "chupa-32a");
	CHECK(reserved[0] == 0 && reserved[1] == 0 && reserved[2] == 0);
}

static void test_host_wire(void)
{
	unsigned char reserved[RESERVED];
	struct delta_wire_id wire;

	host(reserved, "chupa-32a");
	CHECK(delta_compat_host_wire(reserved, RESERVED, DELTA_MAJOR, &wire) && wire.number == 32);
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
	test_range_bytes();
	test_matrix();
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
