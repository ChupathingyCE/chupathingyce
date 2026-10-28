/*
BITMAP_BYTE_ORDER.C

A map's pixels as a big-endian console's CPU reads them
(bitmap_byte_order.h): each word put together from its bytes, the Xbox's
little-endian order, and written back in the CPU's own.
*/

#include <string.h>

#include "bitmap_byte_order.h"

unsigned short bitmap_byte_order_read16(void const *address)
{
	unsigned char const *bytes = address;

	return (unsigned short)(bytes[0] | (bytes[1] << 8));
}

unsigned long bitmap_byte_order_read32(void const *address)
{
	unsigned char const *bytes = address;

	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) | ((unsigned long)bytes[2] << 16) |
		((unsigned long)bytes[3] << 24);
}

static void copy16(void const *source, unsigned char *destination)
{
	unsigned short value = bitmap_byte_order_read16(source);

	memcpy(destination, &value, sizeof(value));
}

void bitmap_byte_order_s3tc_rgb(void const *block, void *copy)
{
	unsigned char const *source = block;
	unsigned char *destination = copy;
	/* (s3tc_block_rgb's bitmap is an unsigned long: four bytes on the
	consoles, the only builds that read through here) */
	unsigned int indices = (unsigned int)bitmap_byte_order_read32(source + 4);

	copy16(source, destination);
	copy16(source + 2, destination + 2);
	memcpy(destination + 4, &indices, sizeof(indices));
}

void bitmap_byte_order_s3tc_alpha4(void const *block, void *copy)
{
	unsigned char const *source = block;
	unsigned char *destination = copy;
	int row;

	for (row = 0; row < 4; row++)
		copy16(source + row * 2, destination + row * 2);
	bitmap_byte_order_s3tc_rgb(source + 8, destination + 8);
}

void bitmap_byte_order_s3tc_alpha3(void const *block, void *copy)
{
	unsigned char const *source = block;
	unsigned char *destination = copy;

	memcpy(destination, source, 8);
	bitmap_byte_order_s3tc_rgb(source + 8, destination + 8);
}

unsigned long bitmap_byte_order_s3tc_color(void const *color)
{
	return bitmap_byte_order_read32(color);
}
