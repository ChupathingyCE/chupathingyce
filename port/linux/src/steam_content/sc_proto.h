/*
SC_PROTO.H

A small protocol buffers wire codec for the Steam content client
(steam_content.h): writing fields into a fixed buffer, and reading a
message's fields one by one. No generated code and no allocation; the
field numbers live where each message is built or read.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/

#ifndef SC_PROTO_H
#define SC_PROTO_H

#include <stddef.h>
#include <stdint.h>

enum
{
	PB_VARINT = 0,
	PB_FIXED64 = 1,
	PB_BYTES = 2,
	PB_FIXED32 = 5,
};

/* a message being written: failed once something didn't fit (the rest is then dropped) */
struct pb_writer
{
	unsigned char *data;
	size_t length;
	size_t capacity;
	int failed;
};

void pb_writer_init(struct pb_writer *writer, void *buffer, size_t capacity);
void pb_put_varint(struct pb_writer *writer, unsigned field, uint64_t value);
void pb_put_fixed32(struct pb_writer *writer, unsigned field, uint32_t value);
void pb_put_fixed64(struct pb_writer *writer, unsigned field, uint64_t value);
void pb_put_bytes(struct pb_writer *writer, unsigned field, const void *data, size_t length);
/* a NUL-terminated string as bytes */
void pb_put_string(struct pb_writer *writer, unsigned field, const char *text);
/* one varint, no field key (the length prefix of an embedded message, and the tests) */
void pb_put_raw_varint(struct pb_writer *writer, uint64_t value);

struct pb_field
{
	unsigned number;
	int wire;
	/* PB_VARINT, PB_FIXED32 and PB_FIXED64 */
	uint64_t value;
	/* PB_BYTES */
	const unsigned char *data;
	size_t length;
};

struct pb_reader
{
	const unsigned char *at;
	const unsigned char *end;
};

void pb_reader_init(struct pb_reader *reader, const void *data, size_t length);
/* the next field: 1, 0 at the message's end, -1 when it is not a sound message */
int pb_next(struct pb_reader *reader, struct pb_field *field);
/* the first field of that number: 1 found, 0 not there (or not a sound message) */
int pb_find(const void *data, size_t length, unsigned number, struct pb_field *field);
/* a number field's value, else fallback */
uint64_t pb_get_number(const void *data, size_t length, unsigned number, uint64_t fallback);
/* a bytes field: 1 and where it is, 0 if absent */
int pb_get_bytes(const void *data, size_t length, unsigned number, const unsigned char **bytes, size_t *size);
/* a string field copied, NUL-terminated and cut to size - 1: 1 if present */
int pb_get_string(const void *data, size_t length, unsigned number, char *text, size_t size);
/* an int32 sent as a varint (negative values are sign-extended to 64 bits) */
int32_t pb_int32(uint64_t value);

#endif
