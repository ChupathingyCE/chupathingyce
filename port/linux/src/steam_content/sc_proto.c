/*
SC_PROTO.C

The protocol buffers wire codec (sc_proto.h).

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h): a console builds this to nothing */

#include "sc_proto.h"

#include <string.h>

void pb_writer_init(struct pb_writer *writer, void *buffer, size_t capacity)
{
	writer->data = (unsigned char *)buffer;
	writer->length = 0;
	writer->capacity = capacity;
	writer->failed = 0;
}

static void put(struct pb_writer *writer, const void *data, size_t length)
{
	if (writer->failed || length > writer->capacity - writer->length)
	{
		writer->failed = 1;
		return;
	}
	memcpy(writer->data + writer->length, data, length);
	writer->length += length;
}

void pb_put_raw_varint(struct pb_writer *writer, uint64_t value)
{
	unsigned char bytes[10];
	size_t count = 0;

	do
	{
		bytes[count] = (unsigned char)(value & 0x7F);
		value >>= 7;
		if (value)
			bytes[count] |= 0x80;
		count++;
	} while (value);
	put(writer, bytes, count);
}

static void put_key(struct pb_writer *writer, unsigned field, int wire)
{
	pb_put_raw_varint(writer, ((uint64_t)field << 3) | (unsigned)wire);
}

void pb_put_varint(struct pb_writer *writer, unsigned field, uint64_t value)
{
	put_key(writer, field, PB_VARINT);
	pb_put_raw_varint(writer, value);
}

void pb_put_fixed32(struct pb_writer *writer, unsigned field, uint32_t value)
{
	unsigned char bytes[4];
	int index;

	for (index = 0; index < 4; index++)
		bytes[index] = (unsigned char)(value >> (8 * index));
	put_key(writer, field, PB_FIXED32);
	put(writer, bytes, 4);
}

void pb_put_fixed64(struct pb_writer *writer, unsigned field, uint64_t value)
{
	unsigned char bytes[8];
	int index;

	for (index = 0; index < 8; index++)
		bytes[index] = (unsigned char)(value >> (8 * index));
	put_key(writer, field, PB_FIXED64);
	put(writer, bytes, 8);
}

void pb_put_bytes(struct pb_writer *writer, unsigned field, const void *data, size_t length)
{
	put_key(writer, field, PB_BYTES);
	pb_put_raw_varint(writer, length);
	put(writer, data, length);
}

void pb_put_string(struct pb_writer *writer, unsigned field, const char *text)
{
	pb_put_bytes(writer, field, text, strlen(text));
}

void pb_reader_init(struct pb_reader *reader, const void *data, size_t length)
{
	reader->at = (const unsigned char *)data;
	reader->end = reader->at + length;
}

static int read_varint(struct pb_reader *reader, uint64_t *value)
{
	uint64_t result = 0;
	int shift;

	for (shift = 0; shift < 64; shift += 7)
	{
		unsigned char byte;

		if (reader->at >= reader->end)
			return 0;
		byte = *reader->at++;
		result |= (uint64_t)(byte & 0x7F) << shift;
		if (!(byte & 0x80))
		{
			*value = result;
			return 1;
		}
	}
	return 0;
}

static uint64_t little_endian(const unsigned char *bytes, int count)
{
	uint64_t value = 0;

	while (count--)
		value = (value << 8) | bytes[count];
	return value;
}

int pb_next(struct pb_reader *reader, struct pb_field *field)
{
	uint64_t key, length;

	if (reader->at >= reader->end)
		return 0;
	memset(field, 0, sizeof(*field));
	if (!read_varint(reader, &key) || (key >> 3) == 0 || (key >> 3) > 0x1FFFFFFF)
		return -1;
	field->number = (unsigned)(key >> 3);
	field->wire = (int)(key & 7);
	switch (field->wire)
	{
	case PB_VARINT:
		return read_varint(reader, &field->value) ? 1 : -1;
	case PB_FIXED64:
		if (reader->end - reader->at < 8)
			return -1;
		field->value = little_endian(reader->at, 8);
		reader->at += 8;
		return 1;
	case PB_FIXED32:
		if (reader->end - reader->at < 4)
			return -1;
		field->value = little_endian(reader->at, 4);
		reader->at += 4;
		return 1;
	case PB_BYTES:
		if (!read_varint(reader, &length) || length > (uint64_t)(reader->end - reader->at))
			return -1;
		field->data = reader->at;
		field->length = (size_t)length;
		reader->at += length;
		return 1;
	default:
		return -1;
	}
}

int pb_find(const void *data, size_t length, unsigned number, struct pb_field *field)
{
	struct pb_reader reader;
	int result;

	pb_reader_init(&reader, data, length);
	while ((result = pb_next(&reader, field)) == 1)
	{
		if (field->number == number)
			return 1;
	}
	return 0;
}

uint64_t pb_get_number(const void *data, size_t length, unsigned number, uint64_t fallback)
{
	struct pb_field field;

	return pb_find(data, length, number, &field) && field.wire != PB_BYTES ? field.value : fallback;
}

int pb_get_bytes(const void *data, size_t length, unsigned number, const unsigned char **bytes, size_t *size)
{
	struct pb_field field;

	if (!pb_find(data, length, number, &field) || field.wire != PB_BYTES)
		return 0;
	*bytes = field.data;
	*size = field.length;
	return 1;
}

int pb_get_string(const void *data, size_t length, unsigned number, char *text, size_t size)
{
	const unsigned char *bytes;
	size_t count;

	if (size)
		text[0] = 0;
	if (!pb_get_bytes(data, length, number, &bytes, &count) || !size)
		return 0;
	if (count > size - 1)
		count = size - 1;
	memcpy(text, bytes, count);
	text[count] = 0;
	return 1;
}

int32_t pb_int32(uint64_t value)
{
	return (int32_t)(uint32_t)(value & 0xFFFFFFFFu);
}
#endif /* HALO_MCC */
