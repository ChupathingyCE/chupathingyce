/*
MESSAGE_HEADER.H

header included in hcex build.
*/

#ifndef __MESSAGE_HEADER_H
#define __MESSAGE_HEADER_H
#pragma once

/* ---------- constants */

enum
{
	MAXIMUM_MESSAGE_SIZE = 0xFFF,
	NUMBER_OF_MESSAGE_TYPES = 4,
	MESSAGE_FLAG_BITS_MASK = 3,
};

enum message_header_byte_order
{
	_byte_order_host = 0,
	_byte_order_network,
};

enum message_type
{
	_message_type_packet = 3,
};

/* ---------- macros */

#define GET_MESSAGE_FLAGS(message) ((message) & MESSAGE_FLAG_BITS_MASK)
#define GET_MESSAGE_TYPE(message) (((message) >> 2) & 3)
#define GET_MESSAGE_SIZE(message) ((message) >> 4)
#define SET_MESSAGE_FLAGS(message, flags) \
	((message) = ((message) & ~MESSAGE_FLAG_BITS_MASK) | (flags))
/* port: a header between this machine's order and the network's, which is
big-endian: a little-endian machine (the original Xbox's x86) swaps it, a
big-endian one (HALO_BIG_ENDIAN: the consoles' PowerPC) has it already */
#ifdef HALO_BIG_ENDIAN
#define MESSAGE_HEADER_SWAP(value) (value)
#else
#define MESSAGE_HEADER_SWAP(value) (((value) << 8) | ((value) >> 8))
#endif

/* ---------- structures */

typedef word message_header;

/* ---------- prototypes/MESSAGE_HEADER.C */

void build_message_header(word *msg, word length, byte type, byte flags);
void byte_swap_message_header(word *header, enum message_header_byte_order byte_order);
void *create_message(long type, void const *data, unsigned long data_size, void *buffer, word buffer_size);

/* ---------- globals */

/* ---------- public code */

#endif // __MESSAGE_HEADER_H
