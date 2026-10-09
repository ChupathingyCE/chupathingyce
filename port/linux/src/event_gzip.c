/*
EVENT_GZIP.C

A batch compressed for its upload (event_log.h, event_upload.c): one gzip
member (RFC 1952) of one deflate block with the fixed codes (RFC 1951,
3.2.6), its repeats found with hash chains over the last 32 KB. The fixed
codes need no tables of their own, and a batch, JSON of numbers and a few
names, compresses about seven to one with them. The game's zlib (the
port's copy) inflates only, which is why this is here.

No state between calls; the unit tests (server/tests/events_test.c) check
its output with zlib's inflate.
*/

#include "event_log.h"

#include <stdlib.h>
#include <string.h>

enum
{
	WINDOW = 32768,
	HASH_BITS = 15,
	HASH_SIZE = 1 << HASH_BITS,
	MINIMUM_MATCH = 3,
	MAXIMUM_MATCH = 258,
	/* the candidates tried at each place: a batch's repeats are short ones
	close by, so a short chain finds nearly all of them */
	MAXIMUM_CHAIN = 48,
	/* a match this long is taken at once */
	GOOD_MATCH = 64,
};

static unsigned short const length_bases[29] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
};
static unsigned char const length_extra[29] = {
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};
static unsigned short const distance_bases[30] = {
	1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
	6145, 8193, 12289, 16385, 24577,
};
static unsigned char const distance_extra[30] = {
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
};

struct bits
{
	unsigned char *data;
	size_t used;
	size_t size;
	unsigned int buffer;
	int count;
	int failed;
};

static void put_byte(struct bits *bits, unsigned char byte)
{
	if (bits->failed)
		return;
	if (bits->used == bits->size)
	{
		size_t size = bits->size ? bits->size * 2 : 4096;
		unsigned char *data = realloc(bits->data, size);

		if (!data)
		{
			bits->failed = 1;
			return;
		}
		bits->data = data;
		bits->size = size;
	}
	bits->data[bits->used++] = byte;
}

/* value's count low bits, first bit first (deflate's order for all but
the codes) */
static void put_bits(struct bits *bits, unsigned int value, int count)
{
	bits->buffer |= (value & ((1u << count) - 1)) << bits->count;
	bits->count += count;
	while (bits->count >= 8)
	{
		put_byte(bits, (unsigned char)bits->buffer);
		bits->buffer >>= 8;
		bits->count -= 8;
	}
}

/* a Huffman code, its first bit its highest */
static void put_code(struct bits *bits, unsigned int code, int length)
{
	unsigned int reversed = 0;
	int index;

	for (index = 0; index < length; index++)
		reversed |= ((code >> index) & 1u) << (length - 1 - index);
	put_bits(bits, reversed, length);
}

/* a literal or length symbol, with the fixed codes */
static void put_symbol(struct bits *bits, int symbol)
{
	if (symbol < 144)
		put_code(bits, 0x30u + (unsigned int)symbol, 8);
	else if (symbol < 256)
		put_code(bits, 0x190u + (unsigned int)(symbol - 144), 9);
	else if (symbol < 280)
		put_code(bits, (unsigned int)(symbol - 256), 7);
	else
		put_code(bits, 0xC0u + (unsigned int)(symbol - 280), 8);
}

static void put_match(struct bits *bits, int length, int distance)
{
	int code;

	for (code = 28; length_bases[code] > length; code--)
		;
	put_symbol(bits, 257 + code);
	put_bits(bits, (unsigned int)(length - length_bases[code]), length_extra[code]);
	for (code = 29; distance_bases[code] > distance; code--)
		;
	put_code(bits, (unsigned int)code, 5);
	put_bits(bits, (unsigned int)(distance - distance_bases[code]), distance_extra[code]);
}

static unsigned int crc32_of(unsigned char const *data, size_t length)
{
	static unsigned int table[256];
	static int ready;
	unsigned int crc = 0xFFFFFFFFu;
	size_t index;

	if (!ready)
	{
		unsigned int value;
		int bit;

		for (value = 0; value < 256; value++)
		{
			unsigned int entry = value;

			for (bit = 0; bit < 8; bit++)
				entry = entry & 1 ? 0xEDB88320u ^ (entry >> 1) : entry >> 1;
			table[value] = entry;
		}
		ready = 1;
	}
	for (index = 0; index < length; index++)
		crc = table[(crc ^ data[index]) & 0xFF] ^ (crc >> 8);
	return crc ^ 0xFFFFFFFFu;
}

static unsigned int hash3(unsigned char const *at)
{
	return (((unsigned int)at[0] << 10) ^ ((unsigned int)at[1] << 5) ^ at[2]) & (HASH_SIZE - 1);
}

unsigned char *event_gzip(unsigned char const *data, size_t length, size_t *compressed_length)
{
	static unsigned char const header[10] = { 0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 0xFF };
	struct bits bits;
	int *head = malloc(HASH_SIZE * sizeof(int));
	int *previous = malloc(WINDOW * sizeof(int));
	unsigned int crc = crc32_of(data, length);
	size_t position = 0;
	int index;

	*compressed_length = 0;
	memset(&bits, 0, sizeof(bits));
	if (!head || !previous)
	{
		free(head);
		free(previous);
		return NULL;
	}
	for (index = 0; index < HASH_SIZE; index++)
		head[index] = -1;
	for (index = 0; index < (int)sizeof(header); index++)
		put_byte(&bits, header[index]);
	/* one final block, of the fixed codes */
	put_bits(&bits, 3, 3);
	while (position < length)
	{
		int best_length = 0, best_distance = 0;

		if (length - position >= MINIMUM_MATCH)
		{
			unsigned int hash = hash3(data + position);
			int candidate = head[hash];
			int chain = MAXIMUM_CHAIN;
			size_t most = length - position < MAXIMUM_MATCH ? length - position : MAXIMUM_MATCH;

			while (candidate >= 0 && chain-- > 0 && position - (size_t)candidate <= WINDOW - 1)
			{
				unsigned char const *a = data + candidate;
				unsigned char const *b = data + position;
				size_t matched = 0;

				if (a[best_length] == b[best_length])
				{
					while (matched < most && a[matched] == b[matched])
						matched++;
					if ((int)matched > best_length)
					{
						best_length = (int)matched;
						best_distance = (int)(position - (size_t)candidate);
						if (best_length >= GOOD_MATCH || matched == most)
							break;
					}
				}
				{
					int next = previous[candidate & (WINDOW - 1)];

					/* (a link older than the window, or overwritten) */
					if (next >= candidate)
						break;
					candidate = next;
				}
			}
		}
		if (best_length >= MINIMUM_MATCH)
		{
			size_t stop = position + (size_t)best_length;

			put_match(&bits, best_length, best_distance);
			for (; position < stop; position++)
			{
				if (length - position >= MINIMUM_MATCH)
				{
					unsigned int hash = hash3(data + position);

					previous[position & (WINDOW - 1)] = head[hash];
					head[hash] = (int)position;
				}
			}
		}
		else
		{
			if (length - position >= MINIMUM_MATCH)
			{
				unsigned int hash = hash3(data + position);

				previous[position & (WINDOW - 1)] = head[hash];
				head[hash] = (int)position;
			}
			put_symbol(&bits, data[position]);
			position++;
		}
	}
	put_symbol(&bits, 256);
	if (bits.count)
		put_bits(&bits, 0, 8 - bits.count);
	for (index = 0; index < 4; index++)
		put_byte(&bits, (unsigned char)(crc >> (8 * index)));
	for (index = 0; index < 4; index++)
		put_byte(&bits, (unsigned char)((unsigned int)length >> (8 * index)));
	free(head);
	free(previous);
	if (bits.failed)
	{
		free(bits.data);
		return NULL;
	}
	*compressed_length = bits.used;
	return bits.data;
}
