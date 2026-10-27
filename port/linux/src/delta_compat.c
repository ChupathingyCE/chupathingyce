/*
DELTA_COMPAT.C

The rules of delta_compat.h.
*/

#include "delta_compat.h"
#include "delta.h"

#include <string.h>

#define PREFIX "chupa-"
#define PREFIX_LENGTH 6
#define MAXIMUM_NUMBER 255
#define CHECK_SEED 0xA5

int delta_wire_parse(const char *text, int length, struct delta_wire_id *id)
{
	int number = 0;
	int index = PREFIX_LENGTH;
	int digits = 0;

	if (length <= PREFIX_LENGTH + 1 || memcmp(text, PREFIX, PREFIX_LENGTH))
		return 0;
	while (index < length - 1 && text[index] >= '0' && text[index] <= '9' && digits < 4)
	{
		number = number * 10 + (text[index] - '0');
		index++;
		digits++;
	}
	if (!digits || index != length - 1 || number < 1 || number > MAXIMUM_NUMBER ||
		text[index] < 'a' || text[index] > 'z')
	{
		return 0;
	}
	id->number = number;
	id->letter = text[index];
	return 1;
}

int delta_wire_compare(const struct delta_wire_id *a, const struct delta_wire_id *b)
{
	if (a->number != b->number)
		return a->number < b->number ? -1 : 1;
	if (a->letter != b->letter)
		return a->letter < b->letter ? -1 : 1;
	return 0;
}

void delta_wire_format(const struct delta_wire_id *id, char *text, int size)
{
	if (size < 16)
	{
		if (size > 0)
			text[0] = 0;
		return;
	}
	{
		int number = id->number;
		int index = PREFIX_LENGTH;

		memcpy(text, PREFIX, PREFIX_LENGTH);
		if (number >= 100)
			text[index++] = (char)('0' + number / 100 % 10);
		if (number >= 10)
			text[index++] = (char)('0' + number / 10 % 10);
		text[index++] = (char)('0' + number % 10);
		text[index++] = (char)id->letter;
		text[index] = 0;
	}
}

int delta_compat_oldest(const char *wire, struct delta_wire_id *oldest)
{
	struct delta_wire_id own;

	if (!delta_wire_parse(wire, (int)strlen(wire), &own))
		return 0;
	*oldest = own;
#define DELTA_COMPAT_ROW(row_wire, row_oldest) \
	if (!strcmp(wire, row_wire)) \
	{ \
		struct delta_wire_id older; \
		\
		if (delta_wire_parse(row_oldest, (int)strlen(row_oldest), &older) && delta_wire_compare(&older, &own) <= 0) \
			*oldest = older; \
	}
	DELTA_WIRE_COMPATIBILITY(DELTA_COMPAT_ROW)
#undef DELTA_COMPAT_ROW
	return 1;
}

static unsigned char range_check(const unsigned char *range)
{
	return (unsigned char)(CHECK_SEED ^ range[0] ^ range[1] ^ range[2] ^ range[3] ^ range[4]);
}

void delta_compat_advertise(unsigned char *reserved, int size, int major, const char *wire)
{
	struct delta_wire_id own, oldest, first, slot;
	unsigned char *range;
	char text[16];

	if (size < DELTA_ADVERTISED_RANGE_OFFSET + DELTA_ADVERTISED_RANGE_SIZE)
		return;
	memset(reserved + DELTA_ADVERTISED_WIRE_OFFSET, 0,
		1 + DELTA_ADVERTISED_WIRE_SIZE + DELTA_ADVERTISED_RANGE_SIZE);
	if (!delta_compat_oldest(wire, &oldest) || !delta_wire_parse(wire, (int)strlen(wire), &own) ||
		!delta_wire_parse(DELTA_WIRE_FIRST_ADVERTISED, (int)strlen(DELTA_WIRE_FIRST_ADVERTISED), &first))
	{
		return;
	}
	/* (the slot: the oldest wire that a build of the advertisement knows,
	never newer than the host's own) */
	slot = delta_wire_compare(&oldest, &first) < 0 ? first : oldest;
	if (delta_wire_compare(&slot, &own) > 0)
		slot = own;
	delta_wire_format(&slot, text, (int)sizeof(text));
	reserved[DELTA_ADVERTISED_WIRE_OFFSET] = (unsigned char)major;
	memcpy(reserved + DELTA_ADVERTISED_WIRE_OFFSET + 1, text, strlen(text));
	range = reserved + DELTA_ADVERTISED_RANGE_OFFSET;
	range[0] = DELTA_ADVERTISED_RANGE_VERSION;
	range[1] = (unsigned char)own.number;
	range[2] = (unsigned char)own.letter;
	range[3] = (unsigned char)oldest.number;
	range[4] = (unsigned char)oldest.letter;
	range[5] = range_check(range);
}

/* (two wires as a range says them: IDs, the oldest not past the wire) */
static int range_ids(const unsigned char *bytes, struct delta_wire_id *wire, struct delta_wire_id *oldest)
{
	struct delta_wire_id theirs, older;

	theirs.number = bytes[0];
	theirs.letter = bytes[1];
	older.number = bytes[2];
	older.letter = bytes[3];
	if (theirs.number < 1 || older.number < 1 || theirs.letter < 'a' || theirs.letter > 'z' ||
		older.letter < 'a' || older.letter > 'z' || delta_wire_compare(&older, &theirs) > 0)
	{
		return 0;
	}
	*wire = theirs;
	*oldest = older;
	return 1;
}

int delta_compat_read_range(const unsigned char *reserved, int size, struct delta_wire_id *wire,
	struct delta_wire_id *oldest)
{
	const unsigned char *range;

	if (size < DELTA_ADVERTISED_RANGE_OFFSET + DELTA_ADVERTISED_RANGE_SIZE)
		return 0;
	range = reserved + DELTA_ADVERTISED_RANGE_OFFSET;
	if (range[0] != DELTA_ADVERTISED_RANGE_VERSION || range[5] != range_check(range))
		return 0;
	return range_ids(range + 1, wire, oldest);
}

static unsigned char tag_check(const unsigned char *tag)
{
	unsigned char check = CHECK_SEED;
	int index;

	for (index = 0; index < DELTA_ADVERTISED_TAG_SIZE - 1; index++)
		check = (unsigned char)(check ^ tag[index]);
	return check;
}

int delta_compat_read_tag(const unsigned char *reserved, int size, int *major, struct delta_wire_id *wire,
	struct delta_wire_id *oldest)
{
	const unsigned char *tag;

	if (size < DELTA_ADVERTISED_RESERVED_SIZE)
		return 0;
	tag = reserved + DELTA_ADVERTISED_TAG_OFFSET;
	if (tag[0] != DELTA_ADVERTISED_TAG_FIRST || tag[1] != DELTA_ADVERTISED_TAG_SECOND ||
		tag[3] != DELTA_ADVERTISED_TAG_VERSION || tag[8] != tag_check(tag) || !range_ids(tag + 4, wire, oldest))
	{
		return 0;
	}
	*major = tag[2];
	return 1;
}

int delta_compat_speaks_delta(const unsigned char *reserved, int size)
{
	struct delta_wire_id wire, oldest;
	int major;

	if (delta_compat_read_tag(reserved, size, &major, &wire, &oldest))
		return 1;
	return size > DELTA_ADVERTISED_FLAGS_OFFSET && (reserved[DELTA_ADVERTISED_FLAGS_OFFSET] & DELTA_ADVERTISED_FLAG) != 0;
}

/* the old slot's wire, if there is one (0: none) */
static int read_slot(const unsigned char *reserved, int size, int major, struct delta_wire_id *wire, char *text,
	int text_size)
{
	const unsigned char *slot = reserved + DELTA_ADVERTISED_WIRE_OFFSET + 1;
	int length = 0;
	int index;

	if (text_size > 0)
		text[0] = 0;
	if (size < DELTA_ADVERTISED_WIRE_OFFSET + 1 + DELTA_ADVERTISED_WIRE_SIZE ||
		reserved[DELTA_ADVERTISED_WIRE_OFFSET] != major)
	{
		return 0;
	}
	while (length < DELTA_ADVERTISED_WIRE_SIZE && slot[length])
		length++;
	if (!length)
		return 0;
	/* (printable ASCII alone, for the player's message and the log) */
	for (index = 0; index < length && index < text_size - 1; index++)
		text[index] = slot[index] >= 0x20 && slot[index] < 0x7F ? (char)slot[index] : '?';
	if (text_size > 0)
		text[index] = 0;
	memset(wire, 0, sizeof(*wire));
	if (length < text_size)
		delta_wire_parse(text, length, wire);
	return 1;
}

/* the host's wire and its range, as its advertisement says them, the one
reader of both layouts: the tag (of this major) first, else the old layout's
slot of this major, with its range bytes, else the slot's wire as both ends.
1: they are known; 0: an old slot whose bytes are no wire ID (slot_text has
them, printable); -1: the advertisement says neither (no tag and no slot of
this major, a tag of another major). slot_text is the old slot's text, empty
for a tag. */
static int read_host(const unsigned char *reserved, int size, int major, struct delta_wire_id *host,
	struct delta_wire_id *host_oldest, char *slot_text, int slot_text_size)
{
	struct delta_wire_id slot;
	int tag_major;

	if (slot_text_size > 0)
		slot_text[0] = 0;
	if (delta_compat_read_tag(reserved, size, &tag_major, host, host_oldest))
		return tag_major == major ? 1 : -1;
	if (!read_slot(reserved, size, major, &slot, slot_text, slot_text_size))
		return -1;
	if (delta_compat_read_range(reserved, size, host, host_oldest))
		return 1;
	if (slot.number)
	{
		/* (a host of a build before the range bytes: the slot is its own wire) */
		*host = slot;
		*host_oldest = slot;
		return 1;
	}
	return 0;
}

int delta_compat_host_wire(const unsigned char *reserved, int size, int major, struct delta_wire_id *wire)
{
	struct delta_wire_id oldest;
	char text[24];

	return read_host(reserved, size, major, wire, &oldest, text, (int)sizeof(text)) > 0;
}

/* whether wires of the two ranges play together: the host's own wire in the
client's range, or the client's in the host's */
static int ranges_meet(const struct delta_wire_id *mine, const struct delta_wire_id *mine_oldest,
	const struct delta_wire_id *host, const struct delta_wire_id *host_oldest)
{
	return (delta_wire_compare(host, mine_oldest) >= 0 && delta_wire_compare(host, mine) <= 0) ||
		(delta_wire_compare(mine, host_oldest) >= 0 && delta_wire_compare(mine, host) <= 0);
}

int delta_compat_decide(const unsigned char *reserved, int size, int major, const char *own, char *theirs,
	int theirs_size)
{
	struct delta_wire_id mine, mine_oldest, host, host_oldest;
	char slot_text[24];
	int known;

	if (theirs_size > 0)
		theirs[0] = 0;
	if (!delta_compat_oldest(own, &mine_oldest) || !delta_wire_parse(own, (int)strlen(own), &mine))
		return -1;
	known = read_host(reserved, size, major, &host, &host_oldest, slot_text, (int)sizeof(slot_text));
	if (known < 0)
		return -1;
	if (known && ranges_meet(&mine, &mine_oldest, &host, &host_oldest))
		return 1;
	/* (the exact rule of the builds before the ranges, on the old slot) */
	if (slot_text[0] && !strcmp(slot_text, own))
		return 1;
	if (theirs_size > 0)
	{
		char text[24];

		if (known)
			delta_wire_format(&host, text, (int)sizeof(text));
		else
			strcpy(text, slot_text);
		strncpy(theirs, text, (size_t)theirs_size - 1);
		theirs[theirs_size - 1] = 0;
	}
	return 0;
}
