/*
RASTERIZER_VERTEX_BYTE_ORDER.H

port: a structure bsp's vertices (its materials' compressed_vertex_data)
stay the map's bytes, the Xbox's little-endian order, on every machine. The
big-endian consoles' GPUs read them that way, and their map loader's byte
swap leaves tag data like this alone (port/console/game/tag_byte_swap.c).
The game also reads some of them on the CPU: the level's triangles for
visibility and culling, and the lightmap and normals under an object for its
lighting. These read such a field the same on any machine; on a
little-endian one it is a plain load.
Custom Edition vertices that ce_bsp.c compresses are in machine order; a
big-endian Custom Edition build would need them written little-endian.
*/

#ifndef __RASTERIZER_VERTEX_BYTE_ORDER_H
#define __RASTERIZER_VERTEX_BYTE_ORDER_H
#pragma once

/* ---------- inline code */

static __inline unsigned int vertex_data_read_dword(
	void const *data)
{
	unsigned char const *bytes = (unsigned char const *)data;

	return (unsigned int)bytes[0] |
		(unsigned int)bytes[1] << 8 |
		(unsigned int)bytes[2] << 16 |
		(unsigned int)bytes[3] << 24;
}

static __inline short vertex_data_read_short(
	void const *data)
{
	unsigned char const *bytes = (unsigned char const *)data;

	return (short)(unsigned short)(bytes[0] | bytes[1] << 8);
}

static __inline float vertex_data_read_float(
	void const *data)
{
	union
	{
		unsigned int bits;
		float value;
	} word;

	word.bits = vertex_data_read_dword(data);
	return word.value;
}

/* a compressed vertex's position (its first three floats) into a point's x,
y and z, for the per-triangle walks: inline, so on a little-endian machine it
is three loads */
static __inline void vertex_data_read_point(
	void const *data,
	float *point)
{
	unsigned char const *bytes = (unsigned char const *)data;

	point[0] = vertex_data_read_float(bytes);
	point[1] = vertex_data_read_float(bytes + 4);
	point[2] = vertex_data_read_float(bytes + 8);
}

#endif /* __RASTERIZER_VERTEX_BYTE_ORDER_H */
