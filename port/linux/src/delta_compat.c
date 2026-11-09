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

/* (the tag's check: 0xA5 xor its eight bytes before, so that its last six,
where the old range bytes were, never pass for them: a released client
reads a host's own wire from the old slot alone) */
static unsigned char tag_check(const unsigned char *tag)
{
	unsigned char check = CHECK_SEED;
	int index;

	for (index = 0; index < DELTA_ADVERTISED_TAG_SIZE - 1; index++)
		check = (unsigned char)(check ^ tag[index]);
	return check;
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

void delta_compat_advertise(unsigned char *reserved, int size, int major, const char *wire)
{
	struct delta_wire_id own, oldest;
	unsigned char *tag;

	if (size < DELTA_ADVERTISED_RESERVED_SIZE)
		return;
	tag = reserved + DELTA_ADVERTISED_TAG_OFFSET;
	memset(tag, 0, DELTA_ADVERTISED_TAG_SIZE);
	if (!delta_compat_oldest(wire, &oldest) || !delta_wire_parse(wire, (int)strlen(wire), &own))
		return;
	tag[0] = DELTA_ADVERTISED_TAG_FIRST;
	tag[1] = DELTA_ADVERTISED_TAG_SECOND;
	tag[2] = (unsigned char)major;
	tag[3] = DELTA_ADVERTISED_TAG_VERSION;
	tag[4] = (unsigned char)own.number;
	tag[5] = (unsigned char)own.letter;
	tag[6] = (unsigned char)oldest.number;
	tag[7] = (unsigned char)oldest.letter;
	tag[8] = tag_check(tag);
}

void delta_compat_advertise_old(unsigned char *reserved, int size, int major, const char *wire)
{
	struct delta_wire_id own, oldest, first, slot;
	char text[16];
	int length;

	if (size < DELTA_ADVERTISED_RESERVED_SIZE)
		return;
	memset(reserved + DELTA_ADVERTISED_WIRE_OFFSET, 0, DELTA_ADVERTISED_TAG_OFFSET - DELTA_ADVERTISED_WIRE_OFFSET);
	if (!delta_compat_oldest(wire, &oldest) || !delta_wire_parse(wire, (int)strlen(wire), &own) ||
		!delta_wire_parse(DELTA_WIRE_FIRST_ADVERTISED, (int)strlen(DELTA_WIRE_FIRST_ADVERTISED), &first))
	{
		return;
	}
	/* (the slot: the oldest wire that a build of the old layout knows, never
	newer than the host's own, ended before the tag) */
	slot = delta_wire_compare(&oldest, &first) < 0 ? first : oldest;
	if (delta_wire_compare(&slot, &own) > 0)
		slot = own;
	delta_wire_format(&slot, text, (int)sizeof(text));
	length = (int)strlen(text);
	if (length >= DELTA_ADVERTISED_TAG_OFFSET - (DELTA_ADVERTISED_WIRE_OFFSET + 1))
		return;
	reserved[DELTA_ADVERTISED_WIRE_OFFSET] = (unsigned char)major;
	memcpy(reserved + DELTA_ADVERTISED_WIRE_OFFSET + 1, text, (size_t)length);
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

/* (the OpenCE number an advertisement announces) */
static int announced(const unsigned char *reserved)
{
	return reserved[0] | reserved[1] << 8;
}

/* the old layout's slot's wire, if there is one (0: none, or no old layout:
no flag, another major, nothing in the slot, or, from the version OpenCE
took the flag for its dedicated servers, a slot that is no wire ID) */
static int read_slot(const unsigned char *reserved, int size, int major, struct delta_wire_id *wire, char *text,
	int text_size)
{
	const unsigned char *slot = reserved + DELTA_ADVERTISED_WIRE_OFFSET + 1;
	int length = 0;
	int index;

	if (text_size > 0)
		text[0] = 0;
	if (size < DELTA_ADVERTISED_WIRE_OFFSET + 1 + DELTA_ADVERTISED_WIRE_SIZE ||
		!(reserved[DELTA_ADVERTISED_FLAGS_OFFSET] & DELTA_ADVERTISED_FLAG) ||
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
	if (!wire->number && announced(reserved) >= DELTA_ADVERTISED_FLAG_TAKEN_VERSION)
		return 0;
	return 1;
}

int delta_compat_speaks_delta(const unsigned char *reserved, int size)
{
	struct delta_wire_id wire, oldest;
	char text[24];
	int major;

	if (delta_compat_read_tag(reserved, size, &major, &wire, &oldest))
		return 1;
	if (size <= DELTA_ADVERTISED_FLAGS_OFFSET || !(reserved[DELTA_ADVERTISED_FLAGS_OFFSET] & DELTA_ADVERTISED_FLAG))
		return 0;
	/* (the old layout: the flag alone before OpenCE took it, and from then a
	wire in its slot, of any major) */
	if (announced(reserved) < DELTA_ADVERTISED_FLAG_TAKEN_VERSION)
		return 1;
	return size > DELTA_ADVERTISED_WIRE_OFFSET &&
		read_slot(reserved, size, reserved[DELTA_ADVERTISED_WIRE_OFFSET], &wire, text, (int)sizeof(text));
}

int delta_compat_host_wire(const unsigned char *reserved, int size, int major, struct delta_wire_id *wire)
{
	struct delta_wire_id oldest, slot;
	char text[24];
	int tag_major;

	if (delta_compat_read_tag(reserved, size, &tag_major, wire, &oldest))
		return tag_major == major;
	if (!read_slot(reserved, size, major, &slot, text, (int)sizeof(text)))
		return 0;
	if (delta_compat_read_range(reserved, size, wire, &oldest))
		return 1;
	if (slot.number)
	{
		*wire = slot;
		return 1;
	}
	return 0;
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
	struct delta_wire_id mine, mine_oldest, slot, host, host_oldest;
	char slot_text[24];
	int ranged, tag_major;

	if (theirs_size > 0)
		theirs[0] = 0;
	if (!delta_compat_oldest(own, &mine_oldest) || !delta_wire_parse(own, (int)strlen(own), &mine))
		return -1;
	/* the tag, first: a host of another major is joined by its number */
	if (delta_compat_read_tag(reserved, size, &tag_major, &host, &host_oldest))
	{
		if (tag_major != major)
			return -1;
		if (ranges_meet(&mine, &mine_oldest, &host, &host_oldest))
			return 1;
		if (theirs_size >= 16)
			delta_wire_format(&host, theirs, theirs_size);
		else if (theirs_size > 0)
		{
			char text[16];

			delta_wire_format(&host, text, (int)sizeof(text));
			strncpy(theirs, text, (size_t)theirs_size - 1);
			theirs[theirs_size - 1] = 0;
		}
		return 0;
	}
	/* then the old layout, of a host before the tag */
	if (!read_slot(reserved, size, major, &slot, slot_text, (int)sizeof(slot_text)))
		return -1;
	ranged = delta_compat_read_range(reserved, size, &host, &host_oldest);
	if (!ranged && slot.number)
	{
		/* (a host of a build before the range bytes: the slot is its own wire) */
		host = slot;
		host_oldest = slot;
		ranged = 1;
	}
	if (ranged && ranges_meet(&mine, &mine_oldest, &host, &host_oldest))
		return 1;
	/* (the exact rule of the builds before the ranges, on the old slot) */
	if (!strcmp(slot_text, own))
		return 1;
	if (theirs_size > 0)
	{
		if (ranged && theirs_size >= 16)
			delta_wire_format(&host, theirs, theirs_size);
		else
		{
			strncpy(theirs, slot_text, (size_t)theirs_size - 1);
			theirs[theirs_size - 1] = 0;
		}
	}
	return 0;
}

int delta_compat_opence_server(const unsigned char *reserved, int size)
{
	if (size <= DELTA_ADVERTISED_FLAGS_OFFSET || announced(reserved) < DELTA_ADVERTISED_FLAG_TAKEN_VERSION ||
		!(reserved[DELTA_ADVERTISED_FLAGS_OFFSET] & DELTA_ADVERTISED_FLAG))
	{
		return 0;
	}
	return !delta_compat_speaks_delta(reserved, size);
}

int delta_compat_opence_server_listing(int flags)
{
	return (flags & DELTA_COMPAT_LISTING_DEDICATED_OPENCE) && !(flags & DELTA_COMPAT_LISTING_DEDICATED);
}

/* where the name's first 0 is (count: none) */
static int name_end(const unsigned short *name, int count)
{
	int index;

	for (index = 0; index < count && name[index]; index++)
		;
	return index;
}

int delta_compat_join_mark(unsigned short *name, int count)
{
	int end;

	if (!name || count < 4)
		return 0;
	end = name_end(name, count);
	if (end > count - 4)
		return 0;
	memset(name + end, 0, (size_t)(count - end) * sizeof(name[0]));
	name[count - 3] = DELTA_COMPAT_JOIN_MARK_FIRST;
	name[count - 2] = DELTA_COMPAT_JOIN_MARK_SECOND;
	return 1;
}

int delta_compat_join_marked(unsigned short *name, int count)
{
	int end, marked;

	if (!name || count < 4)
		return 0;
	end = name_end(name, count);
	if (end >= count)
		return 0;
	marked = end <= count - 4 && name[count - 3] == DELTA_COMPAT_JOIN_MARK_FIRST &&
		name[count - 2] == DELTA_COMPAT_JOIN_MARK_SECOND && !name[count - 1];
	memset(name + end, 0, (size_t)(count - end) * sizeof(name[0]));
	return marked;
}
