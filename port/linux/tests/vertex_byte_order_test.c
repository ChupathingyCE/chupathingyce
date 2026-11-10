/*
VERTEX_BYTE_ORDER_TEST.C

A structure bsp's vertices as a map keeps them, the Xbox's little-endian
bytes, read through source/rasterizer/rasterizer_vertex_byte_order.h
(tools/test_vertex_byte_order.py runs this natively and on a big-endian
machine under QEMU). Three level vertices in the compressed layout the
culling and visibility walks read (position, packed normal, binormal,
tangent, texcoord: 32 bytes each) and one lightmap vertex (incident
radiosity, then u and v) must read back as the same numbers on either
machine.

    vertex_byte_order_test
*/

#include "../../../source/rasterizer/rasterizer_vertex_byte_order.h"

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

enum
{
	VERTEX_SIZE = 32,
	NORMAL_OFFSET = 12,
	TEXCOORD_OFFSET = 24,
	LIGHTMAP_VERTEX_SIZE = 8
};

/* a word's bytes, least significant first, as the Xbox wrote them */
static void put_little_endian(unsigned char *bytes, unsigned int value, int count)
{
	int i;

	for (i = 0; i < count; i++)
	{
		bytes[i] = (unsigned char)(value >> (8 * i));
	}
}

static unsigned int float_bits(float value)
{
	unsigned int bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

int main(void)
{
	/* a triangle off the origin, with values whose bytes all differ, so a
	read in the wrong order can't land on the same number */
	static float const points[3][3] =
	{
		{ 12.375f, -48.5f, 3.0625f },
		{ 1000.25f, 0.15625f, -7.75f },
		{ -0.5f, 65.125f, 2048.5f }
	};
	static unsigned int const normals[3] = { 0x1234ABCDu, 0x7FE00401u, 0x80C0FF01u };
	unsigned char vertices[3 * VERTEX_SIZE];
	unsigned char lightmap_vertex[LIGHTMAP_VERTEX_SIZE];
	int vertex, axis;

	memset(vertices, 0xEE, sizeof(vertices));
	for (vertex = 0; vertex < 3; vertex++)
	{
		unsigned char *bytes = vertices + vertex * VERTEX_SIZE;

		for (axis = 0; axis < 3; axis++)
		{
			put_little_endian(bytes + 4 * axis, float_bits(points[vertex][axis]), 4);
		}
		put_little_endian(bytes + NORMAL_OFFSET, normals[vertex], 4);
		put_little_endian(bytes + TEXCOORD_OFFSET, float_bits(0.25f * (float)vertex), 4);
		put_little_endian(bytes + TEXCOORD_OFFSET + 4, float_bits(-1.5f), 4);
	}
	for (vertex = 0; vertex < 3; vertex++)
	{
		unsigned char const *bytes = vertices + vertex * VERTEX_SIZE;

		for (axis = 0; axis < 3; axis++)
		{
			CHECK(vertex_data_read_float(bytes + 4 * axis) == points[vertex][axis]);
		}
		{
			float point[3];

			vertex_data_read_point(bytes, point);
			CHECK(point[0] == points[vertex][0] && point[1] == points[vertex][1] && point[2] == points[vertex][2]);
		}
		CHECK(vertex_data_read_dword(bytes + NORMAL_OFFSET) == normals[vertex]);
		CHECK(vertex_data_read_float(bytes + TEXCOORD_OFFSET) == 0.25f * (float)vertex);
		CHECK(vertex_data_read_float(bytes + TEXCOORD_OFFSET + 4) == -1.5f);
	}

	/* a lightmap vertex: its packed radiosity direction, then u and v, signed */
	put_little_endian(lightmap_vertex, 0xC0FFEE11u, 4);
	put_little_endian(lightmap_vertex + 4, 0xFFFEu, 2);
	put_little_endian(lightmap_vertex + 6, 0x7123u, 2);
	CHECK(vertex_data_read_dword(lightmap_vertex) == 0xC0FFEE11u);
	CHECK(vertex_data_read_short(lightmap_vertex + 4) == -2);
	CHECK(vertex_data_read_short(lightmap_vertex + 6) == 0x7123);

	/* the facing test's input: the triangle's edges from the points read */
	{
		float edge[3];

		for (axis = 0; axis < 3; axis++)
		{
			edge[axis] = vertex_data_read_float(vertices + VERTEX_SIZE + 4 * axis) -
				vertex_data_read_float(vertices + 4 * axis);
			CHECK(edge[axis] == points[1][axis] - points[0][axis]);
		}
	}

	if (failures)
	{
		fprintf(stderr, "vertex_byte_order_test: %d failed\n", failures);
		return 1;
	}
	{
		unsigned int one = 1;

		printf("vertex_byte_order_test: ok (%s-endian)\n", *(unsigned char *)&one ? "little" : "big");
	}
	return 0;
}
