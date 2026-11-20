/*
H3_SPARTAN.C

Halo 3's multiplayer Spartan read from the player's own MCC files
(h3_spartan.h, which says what it does; the cache file itself: h3_cache.c).

The mode tag (fields from the file itself, named after Assembly's Halo3MCC
plugins, GPL-3.0, read as a reference, no code): regions at 0xC (0x10 each:
name, permutations at 4, 0x18 each: name, mesh index, mesh count), nodes at
0x30 (0x60 each: name, parent at 4, the default inverse matrix at 0x28),
materials at 0x48 (0x24 each: the render method's datum at 0xC), meshes at
0x68 (0x4C each: parts at 0 - 0x10 each: material, index start at 4, count
at 6 - vertex buffer at 0x18, index buffer at 0x28, rigid node at 0x2D),
compression at 0x74 (0x2C each: position then texture bounds at 4), its
geometry's resource at 0xE0.

A mesh's vertices (a rigid one's declaration 2, stride 36; a skinned one's
3, 44): position three unsigned shorts over the bounds and a pad, texture
coordinates two more, normal, tangent and binormal three signed shorts and a
pad each, then the skinned's four nodes and four weights in bytes. They are
the model's, at its default pose. Indices: triangle strips, 0xFFFF none.

Render methods (rmsh): their postprocess at 0x28 (0x8C each), its textures
at 0x10 by the template's (rmt2) names at 0x6C. Bitmaps: images at 0x60, the
top level in the resource's optional page, the smaller ones in its required
page. Change colour maps: red the primary colour, green the secondary.

Every count and offset is checked: the file is anyone's.
*/

/* ---------- headers */

#include "h3_spartan.h"
#include "h3_cache.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef malloc
#undef free
#undef realloc
#undef calloc

/* (h3up: the Vita tier - what a Halo 3 level's make allocates is the
builder's own heap's, user RAM outside newlib's 48 MB C heap, as Halo 2's
make's: h2_build_heap.h; the desktop's PC tier as before) */
#define H2_BUILD_HEAP_WRAP
#include "h2_build_heap.h"

#define H3_ALIGN __attribute__((force_align_arg_pointer))

void platform_log(char const *format, ...);
unsigned long long vita_host_time_us(void);
int platform_mcc_root(char *out, unsigned long out_size);

/* ---------- constants */

#define SPARTAN_MODE_TAG "objects\\characters\\masterchief\\mp_masterchief\\mp_masterchief"
#define ELITE_MODE_TAG "objects\\characters\\elite\\mp_elite\\mp_elite"

enum
{
	MODE_REGIONS = 0x0C,
	MODE_REGION_SIZE = 0x10,
	MODE_PERMUTATION_SIZE = 0x18,
	MODE_NODES = 0x30,
	MODE_NODE_SIZE = 0x60,
	MODE_MATERIALS = 0x48,
	MODE_MATERIAL_SIZE = 0x24,
	MODE_MESHES = 0x68,
	MODE_MESH_SIZE = 0x4C,
	MODE_PART_SIZE = 0x10,
	MODE_COMPRESSION = 0x74,
	MODE_COMPRESSION_SIZE = 0x2C,
	MODE_RESOURCE = 0xE0,

	MAXIMUM_H3_NODES = 64,
	MAXIMUM_MESHES = 64,
	MAXIMUM_REQUESTS = 2 * MAXIMUM_MESHES + 2 * H3_SPARTAN_MAXIMUM_BITMAPS,

	/* a multipurpose map's and a bump map's side, and the largest base map kept */
	MULTIPURPOSE_SIDE = 512,
	BUMP_SIDE = 1024,
	MAXIMUM_BASE_SIDE = 1024,

	/* the colour masks' growth: texels, a mask counted as none under this, a base
	map counted as bright above this */
	MASK_GROWTH = 2,
	MASK_EMPTY = 24,
	BRIGHT_BASE = 120,

	/* (an Elite's height against the menus' Spartan's window: tenths) */
#define OWN_SCALE 0.72f
#define IDLE_AIM_DOWN 0.4f
	OWN_SKINNED_NODES = 43,
	/* Halo 3's bitmap formats of its bump maps: two channels, BC5 and signed
	bytes (their sizes checked against the top levels' pages) */
	H3_FORMAT_A8 = 0,
	H3_FORMAT_Y8 = 1,
	H3_FORMAT_AY8 = 2,
	H3_FORMAT_X8R8G8B8 = 10,
	H3_FORMAT_A8R8G8B8 = 11,
	H3_FORMAT_V8U8 = 22,
	H3_FORMAT_DXN = 33,
	/* (4096 a side, down to one texel) */
	MAXIMUM_IMAGE_LEVELS = 13,
};

/* a material's textures wanted */
enum
{
	_material_base = 0,
	_material_change_colour,
	_material_detail,
	_material_bump,
	_material_illum,
	MATERIAL_TEXTURES
};

/* ---------- structures */

struct buffer_wanted
{
	long vertex_count;
	short declaration;
	short stride;
	unsigned long vertex_offset, vertex_size;
	unsigned long index_offset, index_size;
	unsigned char *vertices;
	unsigned char *indices;
};

/* a node's way to the skeleton: its rotation and offset, the skeleton's node */
struct node_map
{
	float rotation[9];
	float offset[3];
	short target;
};

/* a bitmap wanted as RGBA or as it is */
struct image_wanted
{
	long tag;
	short index;                 /* which of the bitmap's images */
	unsigned short width, height, format, levels;
	short level;                 /* the first level kept */
	unsigned char *top;          /* the kept first level, if in the optional page */
	unsigned long top_size;
	unsigned char *chain;        /* the rest (or everything) from the required page */
	unsigned long chain_size;
};

/* ---------- private code */

H3_ALIGN static unsigned long read32(void const *p)
{
	unsigned char const *b = p;

	return (unsigned long)b[0] | ((unsigned long)b[1] << 8) | ((unsigned long)b[2] << 16) | ((unsigned long)b[3] << 24);
}

H3_ALIGN static short read16(void const *p)
{
	unsigned char const *b = p;

	return (short)(b[0] | (b[1] << 8));
}

H3_ALIGN static float readf(void const *p)
{
	union { unsigned long u; float f; } v;

	v.u = read32(p);
	return v.f;
}

H3_ALIGN static void why_set(char *why, long why_size, char const *text, char const *detail)
{
	if (why && why_size > 0)
		snprintf(why, (size_t)why_size, text, detail ? detail : "");
}

/* a node's name as both skeletons can be matched by: no "bip01 ", spaces as
underscores, lower case */
H3_ALIGN static void node_key(char const *name, char *key, unsigned long size)
{
	unsigned long i = 0;

	if (!strncmp(name, "bip01 ", 6))
		name += 6;
	for (; name[i] && i + 1 < size; i++)
		key[i] = name[i] == ' ' ? '_' : (name[i] >= 'A' && name[i] <= 'Z' ? (char)(name[i] + 32) : name[i]);
	key[i] = 0;
}

/* the 3x3 inverse rotation R and position p (columns forward, left, up:
local = R v + p) */
H3_ALIGN static void matrix_columns(float const *inverse, float r[9], float p[3])
{
	int c, k;

	for (c = 0; c < 3; c++)
	{
		for (k = 0; k < 3; k++)
			r[3 * k + c] = inverse[1 + 3 * c + k];
		p[c] = inverse[10 + c];
	}
}

/* the way from a node of Halo 3's default pose to one of the skeleton's:
v' = Rc^T (Rh v + ph - pc) */
H3_ALIGN static void node_map_make_from(float const *rh, float const *ph, float const *target_inverse, short target, struct node_map *map)
{
	float rc[9], pc[3];
	int i, j, k;

	matrix_columns(target_inverse, rc, pc);
	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < 3; j++)
		{
			float sum = 0.0f;

			for (k = 0; k < 3; k++)
				sum += rc[3 * k + i] * rh[3 * k + j];
			map->rotation[3 * i + j] = sum;
		}
		map->offset[i] = rc[0 + i] * (ph[0] - pc[0]) + rc[3 + i] * (ph[1] - pc[1]) + rc[6 + i] * (ph[2] - pc[2]);
	}
	map->target = target;
}

H3_ALIGN static void node_map_make(float const *h3_inverse, float const *target_inverse, short target, struct node_map *map)
{
	float rh[9], ph[3];

	matrix_columns(h3_inverse, rh, ph);
	node_map_make_from(rh, ph, target_inverse, target, map);
}

H3_ALIGN static void rotate(float const *r, float const *v, float *out)
{
	float x = r[0] * v[0] + r[1] * v[1] + r[2] * v[2];
	float y = r[3] * v[0] + r[4] * v[1] + r[5] * v[2];
	float z = r[6] * v[0] + r[7] * v[1] + r[8] * v[2];

	out[0] = x, out[1] = y, out[2] = z;
}

H3_ALIGN static void normalize(float *v)
{
	float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

	if (length > 1e-5f)
		v[0] /= length, v[1] /= length, v[2] /= length;
}

/* (HALO_H3_SPARTAN_PLAIN=0: Halo 3's uncompressed maps not read, as before) */
H3_ALIGN static boolean format_plain_read(void)
{
	static int read = -1;

	if (read < 0)
		read = !getenv("HALO_H3_SPARTAN_PLAIN") || atoi(getenv("HALO_H3_SPARTAN_PLAIN")) != 0;
	return read != 0;
}

H3_ALIGN static unsigned long mip_bytes(unsigned short format, unsigned long w, unsigned long h)
{
	w = w ? w : 1;
	h = h ? h : 1;
	if (format == 14)
		return ((w + 3) / 4) * ((h + 3) / 4) * 8;
	if (format == 15 || format == 16 || format == H3_FORMAT_DXN)
		return ((w + 3) / 4) * ((h + 3) / 4) * 16;
	if (format == H3_FORMAT_V8U8)
		return w * h * 2;
	/* (Halo 3's uncompressed maps - an assault rifle's lights' illumination map:
	x8r8g8b8 and a8r8g8b8 four bytes a texel, a8, y8 and ay8 one) */
	if (format_plain_read() && (format == H3_FORMAT_X8R8G8B8 || format == H3_FORMAT_A8R8G8B8))
		return w * h * 4;
	if (format_plain_read() && (format == H3_FORMAT_A8 || format == H3_FORMAT_Y8 || format == H3_FORMAT_AY8))
		return w * h;
	return 0;
}

H3_ALIGN static boolean format_plain(unsigned short format)
{
	return format_plain_read() && (format == H3_FORMAT_X8R8G8B8 || format == H3_FORMAT_A8R8G8B8 || format == H3_FORMAT_A8 || format == H3_FORMAT_Y8 ||
		format == H3_FORMAT_AY8);
}

/* a DXT colour block */
H3_ALIGN static void dxt_block(unsigned char const *b, boolean one_bit, unsigned char out[16][4])
{
	unsigned long c0 = (unsigned long)b[0] | ((unsigned long)b[1] << 8), c1 = (unsigned long)b[2] | ((unsigned long)b[3] << 8);
	unsigned long bits = read32(b + 4);
	long p[4][3];
	int i, c;

	p[0][0] = ((c0 >> 11) & 31) * 255 / 31, p[0][1] = ((c0 >> 5) & 63) * 255 / 63, p[0][2] = (c0 & 31) * 255 / 31;
	p[1][0] = ((c1 >> 11) & 31) * 255 / 31, p[1][1] = ((c1 >> 5) & 63) * 255 / 63, p[1][2] = (c1 & 31) * 255 / 31;
	for (c = 0; c < 3; c++)
	{
		if (c0 > c1 || !one_bit)
		{
			p[2][c] = (2 * p[0][c] + p[1][c]) / 3;
			p[3][c] = (p[0][c] + 2 * p[1][c]) / 3;
		}
		else
		{
			p[2][c] = (p[0][c] + p[1][c]) / 2;
			p[3][c] = 0;
		}
	}
	for (i = 0; i < 16; i++)
	{
		long const *q = p[(bits >> (2 * i)) & 3];

		out[i][0] = (unsigned char)q[0];
		out[i][1] = (unsigned char)q[1];
		out[i][2] = (unsigned char)q[2];
		out[i][3] = 255;
	}
}

/* a DXT5 alpha block */
H3_ALIGN static void dxt_alpha(unsigned char const *b, unsigned char out[16][4])
{
	unsigned long long bits = 0;
	long a[8];
	int i;

	for (i = 0; i < 6; i++)
		bits |= (unsigned long long)b[2 + i] << (8 * i);
	a[0] = b[0], a[1] = b[1];
	if (a[0] > a[1])
	{
		for (i = 2; i < 8; i++)
			a[i] = ((8 - i) * a[0] + (i - 1) * a[1]) / 7;
	}
	else
	{
		for (i = 2; i < 6; i++)
			a[i] = ((6 - i) * a[0] + (i - 1) * a[1]) / 5;
		a[6] = 0, a[7] = 255;
	}
	for (i = 0; i < 16; i++)
		out[i][3] = (unsigned char)a[(bits >> (3 * i)) & 7];
}

/* a signed BC5 channel's block, made unsigned (0x80 zero) in out's alpha */
H3_ALIGN static void dxn_channel(unsigned char const *b, unsigned char out[16][4])
{
	unsigned long long bits = 0;
	long a[8];
	int i;

	for (i = 0; i < 6; i++)
		bits |= (unsigned long long)b[2 + i] << (8 * i);
	a[0] = (signed char)b[0], a[1] = (signed char)b[1];
	if (a[0] > a[1])
	{
		for (i = 2; i < 8; i++)
			a[i] = ((8 - i) * a[0] + (i - 1) * a[1]) / 7;
	}
	else
	{
		for (i = 2; i < 6; i++)
			a[i] = ((6 - i) * a[0] + (i - 1) * a[1]) / 5;
		a[6] = -127, a[7] = 127;
	}
	for (i = 0; i < 16; i++)
		out[i][3] = (unsigned char)(a[(bits >> (3 * i)) & 7] + 128 > 255 ? 255 : a[(bits >> (3 * i)) & 7] + 128);
}

/* a DXT level as RGBA (malloc's; NULL if the format is another) */
H3_ALIGN static unsigned char *dxt_rgba(unsigned char const *data, unsigned long size, unsigned long w, unsigned long h,
	unsigned short format)
{
	unsigned long bw = (w + 3) / 4, bh = (h + 3) / 4, block = format == 14 ? 8 : 16, x, y;
	unsigned char *out;

	if (format_plain(format))
	{
		unsigned long n = w * h, i;

		if (mip_bytes(format, w, h) > size || !(out = malloc(n * 4)))
			return NULL;
		for (i = 0; i < n; i++)
		{
			unsigned char *o = out + 4 * i;

			switch (format)
			{
			case H3_FORMAT_A8:
				o[0] = o[1] = o[2] = 255, o[3] = data[i];
				break;
			case H3_FORMAT_Y8:
				o[0] = o[1] = o[2] = data[i], o[3] = 255;
				break;
			case H3_FORMAT_AY8:
				o[0] = o[1] = o[2] = o[3] = data[i];
				break;
			default:
				o[0] = data[4 * i + 2], o[1] = data[4 * i + 1], o[2] = data[4 * i];
				o[3] = format == H3_FORMAT_A8R8G8B8 ? data[4 * i + 3] : 255;
				break;
			}
		}
		return out;
	}
	/* (block formats alone: mip_bytes sizes V8U8 by the texel, this reads blocks) */
	if ((format != 14 && format != 15 && format != 16 && format != H3_FORMAT_DXN) ||
		mip_bytes(format, w, h) == 0 || mip_bytes(format, w, h) > size || !(out = malloc(w * h * 4)))
	{
		return NULL;
	}
	for (y = 0; y < bh; y++)
	{
		for (x = 0; x < bw; x++)
		{
			unsigned char const *b = data + block * (y * bw + x);
			unsigned char texels[16][4];
			unsigned long i;

			dxt_block(block == 8 ? b : b + 8, block == 8, texels);
			if (format == 16)
				dxt_alpha(b, texels);
			for (i = 0; i < 16; i++)
			{
				unsigned long px = x * 4 + (i & 3), py = y * 4 + (i >> 2);

				if (px < w && py < h)
					memcpy(out + 4 * (py * w + px), texels[i], 4);
			}
		}
	}
	return out;
}

H3_ALIGN static unsigned long morton(unsigned long x, unsigned long y)
{
	unsigned long out = 0, bit;

	for (bit = 0; bit < 12; bit++)
		out |= ((x >> bit) & 1) << (2 * bit) | ((y >> bit) & 1) << (2 * bit + 1);
	return out;
}

/* a render method's texture of name: its bitmap tag, NONE */
H3_ALIGN static long method_texture(struct h3_cache *cache, long method_tag, char const *name)
{
	struct h3_cache_tag method, template_info;
	long count, name_count, texture_count, i;
	unsigned long postprocess, names, textures;
	long template_tag;

	if (!h3_cache_tag_get(cache, method_tag, &method) ||
		!h3_cache_block(cache, method.address + 0x28, 0x8C, &count, &postprocess) || count < 1)
	{
		return NONE;
	}
	template_tag = h3_cache_reference(cache, postprocess);
	if (!h3_cache_tag_get(cache, template_tag, &template_info) ||
		!h3_cache_block(cache, template_info.address + 0x6C, 4, &name_count, &names) ||
		!h3_cache_block(cache, postprocess + 0x10, h3_level_odst() ? 0x18 : 0x1C, &texture_count, &textures))
	{
		return NONE;
	}
	for (i = 0; i < name_count && i < texture_count; i++)
	{
		if (!strcmp(h3_cache_string(cache, h3_cache_read32(cache, names + 4 * i)), name))
		{
			/* (ODST's postprocess texture is 0x18 bytes, MCC Halo 3's 0x1C: h3_level.c) */
			unsigned long texture_size = h3_level_odst() ? 0x18 : 0x1C;
			unsigned char const *texture = h3_cache_at(cache, textures + texture_size * i, texture_size);

			return texture ? h3_cache_tag_from_datum(cache, read32(texture + 0xC)) : NONE;
		}
	}
	return NONE;
}

/* a render method's value of name (four numbers; a colour's three, its alpha
fourth): FALSE if its template has none */
H3_ALIGN static boolean method_argument(struct h3_cache *cache, long method_tag, char const *name, float *out)
{
	struct h3_cache_tag method, template_info;
	long count, name_count, value_count, i;
	unsigned long postprocess, names, values;

	if (!h3_cache_tag_get(cache, method_tag, &method) ||
		!h3_cache_block(cache, method.address + 0x28, 0x8C, &count, &postprocess) || count < 1 ||
		!h3_cache_tag_get(cache, h3_cache_reference(cache, postprocess), &template_info) ||
		!h3_cache_block(cache, template_info.address + 0x48, 4, &name_count, &names) ||
		!h3_cache_block(cache, postprocess + 0x1C, 0x10, &value_count, &values))
	{
		return FALSE;
	}
	for (i = 0; i < name_count && i < value_count; i++)
	{
		if (!strcmp(h3_cache_string(cache, h3_cache_read32(cache, names + 4 * i)), name))
		{
			unsigned char const *value = h3_cache_at(cache, values + 0x10 * (unsigned long)i, 0x10);
			int c;

			if (!value)
				return FALSE;
			for (c = 0; c < 4; c++)
				out[c] = readf(value + 4 * c);
			return TRUE;
		}
	}
	return FALSE;
}

/* a render method's template's option of a category (the digits of its name,
_3_2_0_1_1_2_7_0_0_1_0_0_0: albedo, bump, alpha test, specular mask, material
model, environment map, self-illumination, blend mode...): -1 if none */
H3_ALIGN static int method_option(struct h3_cache *cache, long method_tag, int category)
{
	struct h3_cache_tag method;
	long count;
	unsigned long postprocess;
	char const *path, *at;
	int i;

	if (!h3_cache_tag_get(cache, method_tag, &method) ||
		!h3_cache_block(cache, method.address + 0x28, 0x8C, &count, &postprocess) || count < 1)
	{
		return -1;
	}
	path = h3_cache_tag_name(cache, h3_cache_reference(cache, postprocess));
	at = strrchr(path, '\\');
	at = at ? at + 1 : path;
	for (i = 0; i <= category; i++)
	{
		if (*at != '_' || at[1] < '0' || at[1] > '9')
			return -1;
		if (i == category)
			return atoi(at + 1);
		at = strchr(at + 1, '_');
		if (!at)
			return -1;
	}
	return -1;
}

H3_ALIGN static unsigned long colour_packed(float const *c, float scale)
{
	unsigned long out = 0;
	int k;

	for (k = 0; k < 3; k++)
	{
		float v = c[k] * scale;

		out = (out << 8) | (unsigned long)(v <= 0.0f ? 0 : v >= 1.0f ? 255 : (long)(v * 255.0f + 0.5f));
	}
	return out;
}

/* a look's value of a name: FALSE if it names none */
H3_ALIGN static boolean look_value(char const *look, char const *key, char *out, unsigned long size)
{
	char const *at = look;
	unsigned long length = (unsigned long)strlen(key);
	boolean found = FALSE;

	while (at && *at)
	{
		char const *comma = strchr(at, ',');
		unsigned long item = comma ? (unsigned long)(comma - at) : (unsigned long)strlen(at);

		if (item > length + 1 && at[length] == '=' && !strncmp(at, key, length) && item - length - 1 < size)
		{
			memcpy(out, at + length + 1, item - length - 1);
			out[item - length - 1] = 0;
			found = TRUE;
		}
		at = comma ? comma + 1 : NULL;
	}
	return found;
}

/* a permutation is one to wear: it has a mesh, and it is not the old body's
(whose name says "legacy"; its mesh holds the arms and legs too) */
H3_ALIGN static boolean permutation_real(char const *name, short first_mesh)
{
	return first_mesh >= 0 && strcmp(name, "empty") && strncmp(name, "legacy", 6);
}

H3_ALIGN static short look_model(char const *look)
{
	char model[16];

	return look_value(look, "model", model, sizeof(model)) && !strcmp(model, "elite") ? H3_MODEL_ELITE : H3_MODEL_SPARTAN;
}

H3_ALIGN static struct h3_cache *spartan_cache(char *why, long why_size, char *directory, unsigned long directory_size, char const *mode_tag)
{
	static char const *const maps[] = H3_SPARTAN_MAPS;
	unsigned long i;

	if (!h3_spartan_maps_directory(directory, directory_size))
	{
		why_set(why, why_size, "no Halo 3 maps in the content folder%s", NULL);
		return NULL;
	}
	for (i = 0; i < sizeof(maps) / sizeof(maps[0]); i++)
	{
		struct h3_cache *cache = h3_cache_open(directory, maps[i], why, why_size);

		if (cache && h3_cache_find_tag(cache, 'mode', mode_tag) != NONE)
			return cache;
		if (cache)
			h3_cache_close(cache);
	}
	why_set(why, why_size, "no multiplayer map of Halo 3 holds the Spartan%s", NULL);
	return NULL;
}

/* the geometry resource's mesh buffers wanted (the mesh's first vertex buffer
and its index buffer) */
H3_ALIGN static boolean buffer_find(struct h3_resource const *r, short vertex_buffer, short index_buffer, struct buffer_wanted *b)
{
	unsigned long kind, at, definition, data;

	memset(b, 0, sizeof(*b));
	if (vertex_buffer < 0 || (unsigned long)vertex_buffer >= h3_resource_read32(r, r->root + 0x18) ||
		index_buffer < 0 || (unsigned long)index_buffer >= h3_resource_read32(r, r->root + 0x24))
	{
		return FALSE;
	}
	if (!h3_resource_pointer(r, r->root + 0x1C, &kind, &at) || kind != 2 ||
		!h3_resource_pointer(r, at + 12 * (unsigned long)vertex_buffer, &kind, &definition) || kind != 2 ||
		!h3_resource_pointer(r, definition + 0x14, &kind, &data) || kind != 4)
	{
		return FALSE;
	}
	b->vertex_count = (long)h3_resource_read32(r, definition);
	b->declaration = (short)(h3_resource_read32(r, definition + 4) & 0xFFFF);
	b->stride = (short)(h3_resource_read32(r, definition + 4) >> 16);
	b->vertex_size = h3_resource_read32(r, definition + 8);
	b->vertex_offset = data;
	if (!h3_resource_pointer(r, r->root + 0x28, &kind, &at) || kind != 2 ||
		!h3_resource_pointer(r, at + 12 * (unsigned long)index_buffer, &kind, &definition) || kind != 2 ||
		!h3_resource_pointer(r, definition + 0x10, &kind, &data) || kind != 4)
	{
		return FALSE;
	}
	b->index_size = h3_resource_read32(r, definition + 4);
	b->index_offset = data;
	return b->vertex_count > 0 && b->vertex_count <= 0xFFFF && (b->stride == 36 || b->stride == 44) &&
		(unsigned long)b->vertex_count * (unsigned long)b->stride <= b->vertex_size &&
		b->vertex_offset <= r->size[0] && b->vertex_size <= r->size[0] - b->vertex_offset &&
		b->index_offset <= r->size[0] && b->index_size <= r->size[0] - b->index_offset;
}

/* a bitmap's requests: what it takes */
/* (h3up) the largest side a made map keeps: the level's textures' rule
(h3_level.c, HALO_H3_MAXIMUM_TEXTURE: 1024 on PC, the Vita's texture cache
256) - the sandbox's weapon, vehicle and arms maps are made as A8R8G8B8
with their mips, so at 1024 a side a Halo 3 level's sandbox kept 133 MB of
pixels (Guardian), more than the Vita's whole user memory allows beside the
make. A measured Vita fallback, the real fix named: keep the maps' own DXT
blocks (or DXT-compress the made ones) and stream the top mips (M-V2) */
/* (h3vita) a model read for the sandbox (h3_model_read: a weapon's, a
vehicle's, the arms'), not the player's Spartan for the look pack
(scene_pack_blocks.c reads A8R8G8B8 or DXT of the maps made here) */
static boolean model_read_sandbox;
/* (h3_sandbox.c) a level's sandbox whose maps came to more than its budget
is gathered again with its multipurpose maps DXT-encoded */
int h3_spartan_multipurpose_compact;

/* (h3vita) the sandbox's maps on the Vita tier: a bump map as this engine's
P8 bump - the level's bump maps' format (h3_level.c), a byte a texel where
A8R8G8B8 normals took four -, and a base map whose sides are not powers of two
DXT-encoded at its made side as the level's are (h3_level.c bitmap_got), not
A8R8G8B8. (h3/pc-acc) The desktop too, the sandbox's reads only: decoded
A8R8G8B8 up to 1024 a side its 17-21 weapons' and vehicles' maps were
107-131 bitmaps of 120-145 MB of pixels, more than the desktop's 96 MB
texture cache by themselves (physical_memory_map.h) - the cache thrashed
(a level map read again 100-200 times a minute) and the world drew through
the weapons' bump maps, red/green, on 11 of the 27 multiplayer maps once
the bots' weapons loaded (~/halo-test-out/h3pc-out/NOTES.md); encoded, 38-86
MB and no bands. Halo 3 holds these maps block-compressed itself (DXT
bases, DXN bumps). HALO_H3_SPARTAN_P8_BUMP / HALO_H3_SPARTAN_DXT 0 or 1 say
either way on either tier (0 and 0: the desktop's A8R8G8B8 as before) */
H3_ALIGN static boolean sandbox_setting(char const *name)
{
	char const *setting = getenv(name);

	if (setting)
		return atoi(setting) != 0;
	return model_read_sandbox;
}

/* (h3_level.c's rule) the power of two a side is made at: the one below it,
or the one above when the side is within 40% of it; not more than the limit */
H3_ALIGN static unsigned long side_made(unsigned long side, unsigned long limit)
{
	unsigned long p = 1;

	while (p * 2 <= side)
		p *= 2;
	if (p != side && side * 10 >= p * 14)
		p *= 2;
	while (p > limit)
		p /= 2;
	return p < 4 ? 4 : p;
}

H3_ALIGN static unsigned long base_side_limit(void)
{
	char const *setting = getenv("HALO_H3_MAXIMUM_TEXTURE");
	unsigned long limit;

#ifdef HALO_VITA
	limit = setting ? (unsigned long)atol(setting) : 256;
#else
	limit = setting ? (unsigned long)atol(setting) : MAXIMUM_BASE_SIDE;
#endif
	return limit < 4 ? 4 : limit > MAXIMUM_BASE_SIDE ? MAXIMUM_BASE_SIDE : limit;
}

H3_ALIGN static boolean image_plan_index(struct h3_cache *cache, long tag, long index, unsigned long limit, struct image_wanted *image,
	struct h3_page_request *requests, long *request_count, long *resource_kept, struct h3_resource *resources, long maximum);

H3_ALIGN static boolean image_plan(struct h3_cache *cache, long tag, unsigned long limit, struct image_wanted *image,
	struct h3_page_request *requests, long *request_count, long *resource_kept, struct h3_resource *resources, long maximum)
{
	return image_plan_index(cache, tag, 0, limit, image, requests, request_count, resource_kept, resources, maximum);
}

/* (index: which of the bitmap's images - a weapon's counter's numbers are ten
images, 0x38 bytes each, a resource each) */
H3_ALIGN static boolean image_plan_index(struct h3_cache *cache, long tag, long index, unsigned long limit, struct image_wanted *image,
	struct h3_page_request *requests, long *request_count, long *resource_kept, struct h3_resource *resources, long maximum)
{
	struct h3_cache_tag t;
	long count, resources_count, level = 0;
	unsigned long images, resource_list, opt_size, req_size, skip = 0, chain = 0, w, h;
	unsigned char data[0x30];
	struct h3_resource *r;
	long m, first_request = *request_count;

	memset(image, 0, sizeof(*image));
	image->tag = tag;
	image->index = (short)index;
	if (!h3_cache_tag_get(cache, tag, &t) || t.group_tag != 'bitm' ||
		!h3_cache_block(cache, t.address + 0x60, 0x30, &count, &images) || count < 1 || index < 0 || index >= count ||
		!h3_cache_read(cache, images + 0x38 * (unsigned long)index, 0x30, data) ||
		!h3_cache_block(cache, t.address + 0x8C, 8, &resources_count, &resource_list) || resources_count <= index)
	{
		return FALSE;
	}
	image->width = (unsigned short)read16(data + 4);
	image->height = (unsigned short)read16(data + 6);
	image->format = (unsigned short)read16(data + 0xC);
	image->levels = (unsigned short)(1 + data[0x14]);
	if (!mip_bytes(image->format, 4, 4) || image->width < 1 || image->height < 1 || image->width > 4096 || image->height > 4096 ||
		image->levels > MAXIMUM_IMAGE_LEVELS)
	{
		return FALSE;
	}
	/* (a resource kept even when the plan then fails: the images' count is not
	the resources') */
	if (*resource_kept >= maximum)
		return FALSE;
	r = &resources[*resource_kept];
	if (!h3_cache_resource_get(cache, (long)(h3_cache_read32(cache, resource_list + 8 * (unsigned long)index) & 0xFFFF), r))
		return FALSE;
	(*resource_kept)++;
	req_size = h3_resource_read32(r, 0);
	opt_size = h3_resource_read32(r, 0x14);
	if ((image->width & (image->width - 1)) || (image->height & (image->height - 1)))
	{
		/* (sides not powers of two - 768, 1536 -: made again at side_made's
		(base_bitmap_make), from the smallest level not under it: the level's
		rule. Before, the level under the limit, resampled up to 1024 whatever
		the limit: six 5.3 MB maps of Guardian's sandbox at the Vita's 256) */
		unsigned long made = side_made(image->width > image->height ? image->width : image->height, limit);

		while (level + 1 < image->levels && ((unsigned long)image->width >> (level + 1)) >= made && ((unsigned long)image->height >> (level + 1)) >= made)
			level++;
	}
	else
	{
		while (level + 1 < image->levels && ((unsigned long)image->width >> level > limit || (unsigned long)image->height >> level > limit))
			level++;
	}
	/* (a side the levels shift to nothing: nothing to keep) */
	if (!((unsigned long)image->width >> level) || !((unsigned long)image->height >> level))
		return FALSE;
	image->level = (short)level;
	w = image->width;
	h = image->height;
	/* (the optional page holds the top levels, as many as its size says: one
	for most maps, two for a 2048 - the Elite's -; the required page the rest) */
	{
		long optional_levels = 0;
		unsigned long optional_bytes = 0, before = 0;

		if (r->page[1] != NONE)
		{
			while (optional_bytes < opt_size && optional_levels < image->levels)
			{
				optional_bytes += mip_bytes(image->format, w >> optional_levels, h >> optional_levels);
				optional_levels++;
			}
			if (optional_bytes != opt_size || optional_bytes > r->size[1])
				optional_levels = 0;
		}
		for (m = 0; m < level && m < optional_levels; m++)
			before += mip_bytes(image->format, w >> m, h >> m);
		if (level < optional_levels)
		{
			image->top_size = optional_bytes - before;
			image->top = malloc(image->top_size);
			if (!image->top)
				goto failed;
			requests[*request_count].page = r->page[1];
			requests[*request_count].offset = r->offset[1] + before;
			requests[*request_count].size = image->top_size;
			requests[*request_count].out = image->top;
			(*request_count)++;
		}
		else
		{
			for (m = optional_levels; m < level; m++)
				skip += mip_bytes(image->format, w >> m, h >> m);
		}
		/* (the chain, down to 4 a side) */
		for (m = level > optional_levels ? level : optional_levels; m < image->levels && (w >> m) >= 4 && (h >> m) >= 4; m++)
			chain += mip_bytes(image->format, w >> m, h >> m);
	}
	if (!image->top && chain == 0)
		chain = mip_bytes(image->format, w >> level, h >> level);
	if (r->page[0] == NONE || skip + chain > req_size || skip + chain > r->size[0])
		chain = 0;
	if (chain)
	{
		image->chain_size = chain;
		image->chain = malloc(chain);
		if (!image->chain)
			goto failed;
		requests[*request_count].page = r->page[0];
		requests[*request_count].offset = r->offset[0] + skip;
		requests[*request_count].size = chain;
		requests[*request_count].out = image->chain;
		(*request_count)++;
	}
	if (image->top || image->chain)
		return TRUE;

failed:
	/* (nothing of it left behind: its buffers, and the requests that name them) */
	free(image->top);
	free(image->chain);
	image->top = image->chain = NULL;
	*request_count = first_request;
	return FALSE;
}

H3_ALIGN static void image_free(struct image_wanted *image)
{
	free(image->top);
	free(image->chain);
	image->top = image->chain = NULL;
}

/* an image's first kept level as RGBA */
H3_ALIGN static unsigned char *image_rgba(struct image_wanted const *image, unsigned long *w, unsigned long *h)
{
	*w = (unsigned long)image->width >> image->level;
	*h = (unsigned long)image->height >> image->level;
	return image->top ? dxt_rgba(image->top, image->top_size, *w, *h, image->format) :
		image->chain ? dxt_rgba(image->chain, image->chain_size, *w, *h, image->format) : NULL;
}

/* a bitmap made in this engine's format (pixels taken: its levels one after
another, DXT or P8 bump as h3_level.c lays them): its index */
H3_ALIGN static short made_bitmap_add(struct h3_spartan *spartan, char const *name, char const *suffix, unsigned char *pixels,
	unsigned long size, unsigned long side, unsigned short format, unsigned short mipmaps)
{
	struct h3_spartan_bitmap *bitmap;

	if (spartan->bitmap_count >= H3_SPARTAN_MAXIMUM_BITMAPS || !pixels)
	{
		free(pixels);
		return NONE;
	}
	bitmap = &spartan->bitmaps[spartan->bitmap_count];
	snprintf(bitmap->name, sizeof(bitmap->name), "%.48s %s", name, suffix);
	bitmap->width = bitmap->height = (unsigned short)side;
	bitmap->format = format;
	bitmap->mipmaps = mipmaps;
	bitmap->pixels = pixels;
	bitmap->size = size;
	return spartan->bitmap_count++;
}

/* a square map of side's RGBA as A8R8G8B8 with its mip levels, swizzled, a
bitmap of the Spartan's (rgba taken): its index */
H3_ALIGN static short argb_bitmap_make(struct h3_spartan *spartan, char const *name, char const *suffix, unsigned char *rgba,
	unsigned long side)
{
	struct h3_spartan_bitmap *bitmap;
	unsigned long size = 0, s, x, y, at = 0;
	unsigned short levels = 0;
	unsigned char *out;

	for (s = side; s >= 4; s /= 2)
		size += s * s * 4, levels++;
	if (spartan->bitmap_count >= H3_SPARTAN_MAXIMUM_BITMAPS || !(out = malloc(size)))
	{
		free(rgba);
		return NONE;
	}
	for (s = side; s >= 4; s /= 2)
	{
		for (y = 0; y < s; y++)
		{
			for (x = 0; x < s; x++)
			{
				unsigned char const *t = rgba + 4 * (y * s + x);
				unsigned char *o = out + at + 4 * morton(x, y);

				o[0] = t[2], o[1] = t[1], o[2] = t[0], o[3] = t[3];
			}
		}
		at += s * s * 4;
		/* (the next level: each four texels' mean, in place) */
		for (y = 0; y < s / 2; y++)
		{
			for (x = 0; x < s / 2; x++)
			{
				int c;

				for (c = 0; c < 4; c++)
				{
					rgba[4 * (y * (s / 2) + x) + c] = (unsigned char)((rgba[4 * (2 * y * s + 2 * x) + c] + rgba[4 * (2 * y * s + 2 * x + 1) + c] +
						rgba[4 * ((2 * y + 1) * s + 2 * x) + c] + rgba[4 * ((2 * y + 1) * s + 2 * x + 1) + c] + 2) / 4);
				}
			}
		}
	}
	free(rgba);
	bitmap = &spartan->bitmaps[spartan->bitmap_count];
	snprintf(bitmap->name, sizeof(bitmap->name), "%.48s %s", name, suffix);
	bitmap->width = bitmap->height = (unsigned short)side;
	bitmap->format = H3_SPARTAN_FORMAT_A8R8G8B8;
	bitmap->mipmaps = (unsigned short)(levels - 1);
	bitmap->pixels = out;
	bitmap->size = size;
	return spartan->bitmap_count++;
}

/* its multipurpose map: red the specular mask (the base map's alpha), green
the self-illumination map's red, blue the change colour map's red (the primary
colour), alpha its green (the secondary) */
H3_ALIGN static boolean multipurpose_make(struct h3_spartan *spartan, char const *name, struct image_wanted const *base,
	struct image_wanted const *cc, struct image_wanted const *illum, short *made)
{
	unsigned long bw, bh, cw = 0, ch = 0, iw = 0, ih = 0, x, y, side = 4;
	unsigned char *b = image_rgba(base, &bw, &bh), *c = cc ? image_rgba(cc, &cw, &ch) : NULL;
	unsigned char *g = illum ? image_rgba(illum, &iw, &ih) : NULL, *out, *grown;

	/* (no larger than what it is made from: an armour's maps are 512s, and an
	armoury holds some twenty of them; nor than the limit the maps are kept
	at - a base of 768 keeps its 384 level for its made 256) */
	while (b && side < MULTIPURPOSE_SIDE && side < base_side_limit() && (side < bw || side < bh || side < cw || side < ch || side < iw || side < ih))
		side *= 2;
	out = malloc(side * side * 4);
	if (!b || !out || (cc && !c))
	{
		free(b);
		free(c);
		free(g);
		free(out);
		return FALSE;
	}
	for (y = 0; y < side; y++)
	{
		for (x = 0; x < side; x++)
		{
			unsigned char *o = out + 4 * (y * side + x);

			o[0] = b[4 * ((y * bh / side) * bw + x * bw / side) + 3];
			if (g)
			{
				/* (the illumination map's brightest channel: a grey mask's red, and
				a coloured map's light whatever its colour) */
				unsigned char const *t = g + 4 * ((y * ih / side) * iw + x * iw / side);

				o[1] = !format_plain_read() ? t[0] : t[0] > t[1] ? (t[0] > t[2] ? t[0] : t[2]) : (t[1] > t[2] ? t[1] : t[2]);
			}
			else
				o[1] = 0;
			o[2] = c ? c[4 * ((y * ch / side) * cw + x * cw / side)] : 0;
			o[3] = c ? c[4 * ((y * ch / side) * cw + x * cw / side) + 1] : 0;
		}
	}
	/* (the colour masks grown a few texels over the painted armour beside them:
	the base map is near white under a mask, and an island's edge that the
	smaller change colour map misses by a texel or two drew a white line round
	each plate - the EVA shoulder's outlines. The suit under the plates is dark,
	and stays unmasked) */
	if (c)
	{
		int pass;

		for (pass = 0; pass < MASK_GROWTH && (grown = malloc(side * side * 2)) != NULL; pass++)
		{
			for (y = 0; y < side; y++)
			{
				for (x = 0; x < side; x++)
				{
					unsigned char const *o = out + 4 * (y * side + x);
					unsigned char const *t = b + 4 * ((y * bh / side) * bw + x * bw / side);
					int k;

					grown[2 * (y * side + x)] = o[2];
					grown[2 * (y * side + x) + 1] = o[3];
					if (o[2] >= MASK_EMPTY || o[3] >= MASK_EMPTY || t[0] + t[1] + t[2] < 3 * BRIGHT_BASE)
						continue;
					for (k = 0; k < 2; k++)
					{
						unsigned char left = out[4 * (y * side + (x + side - 1) % side) + 2 + k], right = out[4 * (y * side + (x + 1) % side) + 2 + k];
						unsigned char up = out[4 * (((y + side - 1) % side) * side + x) + 2 + k], down = out[4 * (((y + 1) % side) * side + x) + 2 + k];
						unsigned char most = left > right ? left : right;

						most = up > most ? up : most;
						grown[2 * (y * side + x) + (unsigned long)k] = down > most ? down : most;
					}
				}
			}
			for (y = 0; y < side * side; y++)
			{
				out[4 * y + 2] = grown[2 * y];
				out[4 * y + 3] = grown[2 * y + 1];
			}
			free(grown);
		}
	}
	free(b);
	free(c);
	free(g);
	/* (HALO_H3_SPARTAN_MP_DXT=1: the multipurpose map DXT-encoded - a quarter
	of the bytes, but its four masks share each block's one colour line: a
	visual fallback, measured, off by default on every tier) */
	if ((getenv("HALO_H3_SPARTAN_MP_DXT") && atoi(getenv("HALO_H3_SPARTAN_MP_DXT"))) ||
		(h3_spartan_multipurpose_compact && model_read_sandbox && !getenv("HALO_H3_SPARTAN_MP_DXT")))
	{
		unsigned char *encoded = NULL;
		unsigned long encoded_size = 0;
		unsigned short format = 0, mipmaps = 0;

		if (!h3_level_image_encode(out, side, side, &encoded, &encoded_size, &format, &mipmaps))
			encoded = NULL;
		free(out);
		*made = made_bitmap_add(spartan, name, "multipurpose", encoded, encoded_size, side, format, mipmaps);
		return *made != NONE;
	}
	*made = argb_bitmap_make(spartan, name, "multipurpose", out, side);
	return *made != NONE;
}

/* its bump map as this engine's: each texel a normal in red, green and blue */
H3_ALIGN static short bump_make(struct h3_spartan *spartan, char const *name, struct image_wanted const *image)
{
	unsigned long w = (unsigned long)image->width >> image->level, h = (unsigned long)image->height >> image->level, x, y;
	unsigned long side = 4, bw = (w + 3) / 4, bh = (h + 3) / 4;
	unsigned char const *data = image->top ? image->top : image->chain;
	unsigned long size = image->top ? image->top_size : image->chain_size;
	unsigned char *xy, *out;

	/* (no larger than what it is made from: multipurpose_make's; nor than
	the limit the maps are kept at) */
	while (side < BUMP_SIDE && side < base_side_limit() && (side < w || side < h))
		side *= 2;
	xy = malloc(w * h * 2);
	out = malloc(side * side * 4);

	/* (the two channel formats alone: the other block formats are a smaller size
	than the sixteen bytes a block this reads) */
	if (!xy || !out || !data || (image->format != H3_FORMAT_V8U8 && image->format != H3_FORMAT_DXN) ||
		mip_bytes(image->format, w, h) == 0 || mip_bytes(image->format, w, h) > size)
	{
		free(xy);
		free(out);
		return NONE;
	}
	if (image->format == H3_FORMAT_V8U8)
	{
		/* (signed: made unsigned) */
		for (y = 0; y < w * h * 2; y++)
			xy[y] = (unsigned char)(data[y] ^ 0x80);
	}
	else
	{
		for (y = 0; y < bh; y++)
		{
			for (x = 0; x < bw; x++)
			{
				unsigned char const *b = data + 16 * (y * bw + x);
				unsigned char red[16][4], green[16][4];
				unsigned long i;

				dxn_channel(b, red);
				dxn_channel(b + 8, green);
				for (i = 0; i < 16; i++)
				{
					unsigned long px = x * 4 + (i & 3), py = y * 4 + (i >> 2);

					if (px < w && py < h)
					{
						xy[2 * (py * w + px)] = red[i][3];
						xy[2 * (py * w + px) + 1] = green[i][3];
					}
				}
			}
		}
	}
	for (y = 0; y < side; y++)
	{
		for (x = 0; x < side; x++)
		{
			unsigned char const *t = xy + 2 * ((y * h / side) * w + x * w / side);
			unsigned char *o = out + 4 * (y * side + x);
			float u = (float)t[0] / 127.5f - 1.0f, v = (float)t[1] / 127.5f - 1.0f;
			float z = 1.0f - u * u - v * v;

			z = z > 0.0f ? sqrtf(z) : 0.0f;
			o[0] = t[0];
			o[1] = t[1];
			o[2] = (unsigned char)(127.5f + 127.5f * z);
			o[3] = 255;
		}
	}
	free(xy);
	if (sandbox_setting("HALO_H3_SPARTAN_P8_BUMP"))
	{
		unsigned long p8_size = 0;
		unsigned short p8_mipmaps = 0;
		unsigned char *p8 = h3_level_bump_p8_make(out, side, side, &p8_size, &p8_mipmaps);

		free(out);
		return made_bitmap_add(spartan, name, "bump", p8, p8_size, side, H3_SPARTAN_FORMAT_P8_BUMP, p8_mipmaps);
	}
	return argb_bitmap_make(spartan, name, "bump", out, side);
}

/* the kept levels one after another, a bitmap of the Spartan: its index */
H3_ALIGN static short base_bitmap_make(struct h3_spartan *spartan, struct image_wanted const *image, char const *name)
{
	struct h3_spartan_bitmap *bitmap;
	unsigned long size = image->top_size + image->chain_size;
	long m;

	if (spartan->bitmap_count >= H3_SPARTAN_MAXIMUM_BITMAPS || !size)
		return NONE;
	/* (a side not a power of two - the Elite's 768 -: this engine's textures
	are, so resampled, whole) */
	{
		unsigned long w = (unsigned long)image->width >> image->level, h = (unsigned long)image->height >> image->level;

		if ((w & (w - 1)) || (h & (h - 1)) || format_plain(image->format))
		{
			/* (the side it is made at: side_made's of the image's top, not more
			than the limit - image_plan kept the level for it) */
			unsigned long iw, ih, side = side_made(image->width > image->height ? image->width : image->height, base_side_limit()), x, y;
			unsigned char *from = image_rgba(image, &iw, &ih), *to = malloc(side * side * 4);

			if (!from || !to)
			{
				free(from);
				free(to);
				return NONE;
			}
			for (y = 0; y < side; y++)
			{
				for (x = 0; x < side; x++)
					memcpy(to + 4 * (y * side + x), from + 4 * ((y * ih / side) * iw + x * iw / side), 4);
			}
			free(from);
			if (sandbox_setting("HALO_H3_SPARTAN_DXT"))
			{
				unsigned char *encoded = NULL;
				unsigned long encoded_size = 0;
				unsigned short format = 0, mipmaps = 0;

				if (!h3_level_image_encode(to, side, side, &encoded, &encoded_size, &format, &mipmaps))
					encoded = NULL;
				free(to);
				return made_bitmap_add(spartan, name, "base", encoded, encoded_size, side, format, mipmaps);
			}
			return argb_bitmap_make(spartan, name, "base", to, side);
		}
	}
	bitmap = &spartan->bitmaps[spartan->bitmap_count];
	bitmap->pixels = malloc(size);
	if (!bitmap->pixels)
		return NONE;
	if (image->top)
		memcpy(bitmap->pixels, image->top, image->top_size);
	if (image->chain)
		memcpy(bitmap->pixels + image->top_size, image->chain, image->chain_size);
	snprintf(bitmap->name, sizeof(bitmap->name), "%s", name);
	bitmap->width = (unsigned short)(image->width >> image->level);
	bitmap->height = (unsigned short)(image->height >> image->level);
	bitmap->format = image->format;
	bitmap->size = size;
	bitmap->mipmaps = 0;
	{
		unsigned long used = mip_bytes(image->format, bitmap->width, bitmap->height);

		for (m = 1; used < size && m < MAXIMUM_IMAGE_LEVELS; m++)
		{
			used += mip_bytes(image->format, (unsigned long)bitmap->width >> m, (unsigned long)bitmap->height >> m);
			bitmap->mipmaps = (unsigned short)m;
		}
	}
	return spartan->bitmap_count++;
}

/* a self-illumination map as a bitmap drawn added: its texels times the
material's colour (its intensity in it), times their alpha where the material
says so (specular_type, kept there by material_for), opaque; NONE if its sides
are not one power of two */
H3_ALIGN static short illum_bitmap_make(struct h3_spartan *spartan, struct image_wanted const *image, struct h3_spartan_material const *m,
	char const *name)
{
	unsigned long w = 0, h = 0, i;
	unsigned char *rgba = image_rgba(image, &w, &h);

	if (!rgba || w != h || (w & (w - 1)) || w < 4)
	{
		free(rgba);
		return NONE;
	}
	for (i = 0; i < w * h; i++)
	{
		unsigned char *t = rgba + 4 * i;
		float alpha = m->specular_type ? t[3] / 255.0f : 1.0f;
		int c;

		for (c = 0; c < 3; c++)
		{
			float v = t[c] * m->illum_color[c] * alpha;

			t[c] = (unsigned char)(v > 255.0f ? 255.0f : v + 0.5f);
		}
		t[3] = 255;
	}
	/* (block-compressed where the sandbox's maps are: the texture cache's room) */
	if (sandbox_setting("HALO_H3_SPARTAN_DXT"))
	{
		unsigned char *encoded = NULL;
		unsigned long encoded_size = 0;
		unsigned short format = 0, mipmaps = 0;

		if (!h3_level_image_encode(rgba, w, w, &encoded, &encoded_size, &format, &mipmaps))
			encoded = NULL;
		free(rgba);
		return made_bitmap_add(spartan, name, "illumination", encoded, encoded_size, w, format, mipmaps);
	}
	return argb_bitmap_make(spartan, name, "illumination", rgba, w);
}

/* a material of the Spartan's by its render method; NONE: not drawn (a decal,
the first person arms' suit) */
H3_ALIGN static short material_for(struct h3_cache *cache, struct h3_spartan *spartan, long method, long *methods,
	long (*texture_tags)[MATERIAL_TEXTURES])
{
	struct h3_cache_tag t;
	struct h3_spartan_material *m;
	char const *path;
	char const *name;
	float value[4], coefficient = 0.0f, tint[4] = { 1, 1, 1, 1 };
	int option;
	short i;

	for (i = 0; i < spartan->material_count; i++)
	{
		if (methods[i] == method)
			return i;
	}
	if (!h3_cache_tag_get(cache, method, &t) || t.group_tag != 'rmsh' || spartan->material_count >= H3_SPARTAN_MAXIMUM_MATERIALS)
		return NONE;
	path = h3_cache_tag_name(cache, method);
	name = strrchr(path, '\\');
	name = name ? name + 1 : path;
	i = spartan->material_count;
	m = &spartan->materials[i];
	memset(m, 0, sizeof(*m));
	methods[i] = method;
	snprintf(m->name, sizeof(m->name), "%s", name);
	m->base_bitmap = m->multipurpose_bitmap = m->detail_bitmap = m->bump_bitmap = NONE;
	texture_tags[i][_material_base] = method_texture(cache, method, "base_map");
	texture_tags[i][_material_change_colour] = method_texture(cache, method, "change_color_map");
	texture_tags[i][_material_detail] = method_texture(cache, method, "detail_map");
	texture_tags[i][_material_bump] = method_texture(cache, method, "bump_map");
	texture_tags[i][_material_illum] = method_texture(cache, method, "self_illum_map");
	if (texture_tags[i][_material_base] == NONE)
	{
		float albedo[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

		/* (a material of its self-illumination alone, added - a weapon's display,
		its counter's digits: a sandbox read's; HALO_H3_WEAPON_DISPLAYS=0 none) */
		method_argument(cache, method, "albedo_color", albedo);
		if (!model_read_sandbox || texture_tags[i][_material_illum] == NONE || method_option(cache, method, 7) != 1 ||
			(method_option(cache, method, 6) != 1 && method_option(cache, method, 6) != 8) || albedo[0] + albedo[1] + albedo[2] > 0.05f ||
			(getenv("HALO_H3_WEAPON_DISPLAYS") && !atoi(getenv("HALO_H3_WEAPON_DISPLAYS"))))
		{
			return NONE;
		}
		m->illum_only = TRUE;
		m->digit = !strcmp(name, "tens") ? 1 : !strcmp(name, "ones") ? 2 : 0;
	}
	spartan->material_count++;
	m->change_colour = texture_tags[i][_material_change_colour] != NONE;
	m->visor = strstr(name, "visor") != NULL;
	m->additive = method_option(cache, method, 7) == 1;
	m->detail_scale[0] = m->detail_scale[1] = 1.0f;
	m->bump_scale[0] = m->bump_scale[1] = 1.0f;
	if (method_argument(cache, method, "detail_map", value))
		m->detail_scale[0] = value[0], m->detail_scale[1] = value[1];
	if (method_argument(cache, method, "bump_map", value))
		m->bump_scale[0] = value[0], m->bump_scale[1] = value[1];
	/* (the highlight: Halo 3's cook-torrance lobe in Halo 2's pass, its tint
	by the coefficient, its curve by the roughness) */
	option = method_option(cache, method, 4);
	if (option > 0 && method_option(cache, method, 3) > 0)
	{
		float roughness = 0.3f;

		method_argument(cache, method, "specular_coefficient", &coefficient);
		if (method_argument(cache, method, "specular_tint", value))
			memcpy(tint, value, sizeof(tint));
		m->specular_color = colour_packed(tint, 1.0f);
		m->specular_glancing_color = method_argument(cache, method, "fresnel_color", value) ? colour_packed(value, 1.0f) : m->specular_color;
		if (method_argument(cache, method, "roughness", value))
			roughness = value[0];
		m->specular_type = roughness < 0.22f ? 3 : roughness < 0.33f ? 1 : 2;
		m->specular_strength = coefficient > 1.0f ? 1.0f : coefficient;
		/* (the reflection: Halo 3's environment map's share, tinted) */
		if (method_option(cache, method, 5) > 0)
		{
			float contribution = 0.0f;

			method_argument(cache, method, "environment_map_specular_contribution", &contribution);
			m->reflection = contribution * coefficient;
			m->reflection_glancing = contribution > 0.0f ? 1.0f : 0.0f;
			m->reflection_tint[0] = m->reflection_tint[1] = m->reflection_tint[2] = 1.0f;
			if (method_argument(cache, method, "env_tint_color", value))
				memcpy(m->reflection_tint, value, sizeof(m->reflection_tint));
		}
	}
	m->illum_color[0] = m->illum_color[1] = m->illum_color[2] = 1.0f;
	if (texture_tags[i][_material_illum] != NONE)
	{
		m->illuminated = TRUE;
		if (method_argument(cache, method, "self_illum_color", value))
			memcpy(m->illum_color, value, sizeof(m->illum_color));
		if (m->illum_only)
		{
			/* (its colour times its intensity; option 8, simple with alpha mask:
			times the map's alpha too - illum_alpha) */
			float intensity[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			int c;

			method_argument(cache, method, "self_illum_intensity", intensity);
			for (c = 0; c < 3; c++)
				m->illum_color[c] *= intensity[0];
			m->specular_type = method_option(cache, method, 6) == 8;   /* (kept here: by its alpha) */
		}
	}
	return i;
}

/* a mesh's triangles added to the Spartan's parts by material */
H3_ALIGN static boolean mesh_add(struct h3_spartan *spartan, struct buffer_wanted const *b, float const *bounds, short rigid,
	struct node_map const *maps, short map_count, struct h3_cache *cache, unsigned long parts, long part_count,
	short const *material_of, short slot, short choice)
{
	struct h3_spartan_vertex *converted = malloc((unsigned long)b->vertex_count * sizeof(*converted));
	long *remap = malloc((unsigned long)b->vertex_count * sizeof(long));
	unsigned short const *strip = (unsigned short const *)b->indices;
	long strip_count = (long)(b->index_size / 2), v, p;

	if (!converted || !remap)
	{
		free(converted);
		free(remap);
		return FALSE;
	}
	for (v = 0; v < b->vertex_count; v++)
	{
		unsigned char const *in = b->vertices + (unsigned long)b->stride * (unsigned long)v;
		struct h3_spartan_vertex *o = &converted[v];
		float position[3], normal[3], tangent[3], binormal[3], moved[3] = { 0, 0, 0 }, n[3] = { 0, 0, 0 }, t[3] = { 0, 0, 0 }, bn[3] = { 0, 0, 0 };
		float weights[4] = { 1, 0, 0, 0 };
		int nodes[4] = { rigid, 0, 0, 0 }, count = 1, k, c;
		float best = 0.0f, second = 0.0f;
		short target_best = 0, target_second = 0;
		float target_weight[H3_SPARTAN_MAXIMUM_NODES];
		short touched[4];
		int touched_count = 0;

		for (k = 0; k < 3; k++)
		{
			position[k] = bounds[2 * k] + (float)(unsigned short)read16(in + 2 * k) / 65535.0f * (bounds[2 * k + 1] - bounds[2 * k]);
			normal[k] = (float)read16(in + 12 + 2 * k) / 32767.0f;
			tangent[k] = (float)read16(in + 20 + 2 * k) / 32767.0f;
			binormal[k] = (float)read16(in + 28 + 2 * k) / 32767.0f;
		}
		for (k = 0; k < 2; k++)
			o->uv[k] = bounds[6 + 2 * k] + (float)(unsigned short)read16(in + 8 + 2 * k) / 65535.0f * (bounds[6 + 2 * k + 1] - bounds[6 + 2 * k]);
		if (b->stride >= 44)
		{
			count = 0;
			for (k = 0; k < 4; k++)
			{
				if (in[40 + k])
				{
					nodes[count] = in[36 + k];
					weights[count] = (float)in[40 + k] / 255.0f;
					count++;
				}
			}
			if (!count)
				count = 1, nodes[0] = in[36], weights[0] = 1.0f;
		}
		memset(target_weight, 0, sizeof(target_weight));
		for (k = 0; k < count; k++)
		{
			struct node_map const *m;
			float r[3];

			if (nodes[k] < 0 || nodes[k] >= map_count)
				nodes[k] = 0;
			m = &maps[nodes[k]];
			rotate(m->rotation, position, r);
			for (c = 0; c < 3; c++)
				moved[c] += weights[k] * (r[c] + m->offset[c]);
			rotate(m->rotation, normal, r);
			for (c = 0; c < 3; c++)
				n[c] += weights[k] * r[c];
			rotate(m->rotation, tangent, r);
			for (c = 0; c < 3; c++)
				t[c] += weights[k] * r[c];
			rotate(m->rotation, binormal, r);
			for (c = 0; c < 3; c++)
				bn[c] += weights[k] * r[c];
			if (target_weight[m->target] == 0.0f && touched_count < 4)
				touched[touched_count++] = m->target;
			target_weight[m->target] += weights[k];
		}
		{
			float total = 0.0f;

			for (k = 0; k < touched_count; k++)
			{
				float w = target_weight[touched[k]];

				total += w;
				if (w > best)
					second = best, target_second = target_best, best = w, target_best = touched[k];
				else if (w > second)
					second = w, target_second = touched[k];
			}
			(void)total;
		}
		normalize(n);
		normalize(t);
		normalize(bn);
		memcpy(o->position, moved, sizeof(moved));
		memcpy(o->normal, n, sizeof(n));
		memcpy(o->tangent, t, sizeof(t));
		memcpy(o->binormal, bn, sizeof(bn));
		o->nodes[0] = (unsigned char)target_best;
		o->nodes[1] = (unsigned char)(second > 0.0f ? target_second : target_best);
		o->weight = best + second > 0.0f ? best / (best + second) : 1.0f;
		{
			float d = sqrtf(moved[0] * moved[0] + moved[1] * moved[1] + moved[2] * moved[2]);

			if (d > spartan->radius)
				spartan->radius = d;
		}
	}
	for (p = 0; p < part_count; p++)
	{
		unsigned char const *e = h3_cache_at(cache, parts + MODE_PART_SIZE * (unsigned long)p, MODE_PART_SIZE);
		long first, count, k, added = 0, used = 0;
		short material;
		struct h3_spartan_part *part;
		short q;
		unsigned short *triangles;
		struct h3_spartan_vertex *vertices;

		if (!e)
			continue;
		material = material_of[p];
		first = (unsigned short)read16(e + 4);
		count = (unsigned short)read16(e + 6);
		if (material == NONE || first > strip_count || count > strip_count - first || count < 3)
			continue;
		for (q = 0, part = NULL; q < spartan->part_count; q++)
		{
			if (spartan->parts[q].material == material && spartan->parts[q].slot == slot && spartan->parts[q].choice == choice &&
				spartan->parts[q].triangle_count + count <= H3_SPARTAN_MAXIMUM_PART_TRIANGLES)
			{
				part = &spartan->parts[q];
			}
		}
		if (!part)
		{
			if (spartan->part_count >= H3_SPARTAN_MAXIMUM_PARTS || count > H3_SPARTAN_MAXIMUM_PART_TRIANGLES)
				continue;
			part = &spartan->parts[spartan->part_count++];
			memset(part, 0, sizeof(*part));
			part->material = material;
			part->slot = slot;
			part->choice = choice;
		}
		triangles = realloc(part->triangles, ((unsigned long)part->triangle_count + (unsigned long)count) * 3 * sizeof(unsigned short));
		vertices = realloc(part->vertices, ((unsigned long)part->vertex_count + (unsigned long)b->vertex_count) * sizeof(*vertices));
		if (!triangles || !vertices)
		{
			free(converted);
			free(remap);
			if (triangles)
				part->triangles = triangles;
			if (vertices)
				part->vertices = vertices;
			return FALSE;
		}
		part->triangles = triangles;
		part->vertices = vertices;
		for (v = 0; v < b->vertex_count; v++)
			remap[v] = NONE;
		for (k = 0; k + 2 < count; k++)
		{
			unsigned short a = strip[first + k], c = strip[first + k + 1], d = strip[first + k + 2];
			unsigned short corners[3];
			int q2;

			if (a == c || c == d || a == d || a >= b->vertex_count || c >= b->vertex_count || d >= b->vertex_count)
				continue;
			corners[0] = a;
			corners[1] = k & 1 ? d : c;
			corners[2] = k & 1 ? c : d;
			for (q2 = 0; q2 < 3; q2++)
			{
				if (remap[corners[q2]] == NONE)
				{
					if (part->vertex_count + used >= 0xFFFF)
					{
						free(converted);
						free(remap);
						return FALSE;
					}
					remap[corners[q2]] = part->vertex_count + used;
					part->vertices[part->vertex_count + used] = converted[corners[q2]];
					used++;
				}
				part->triangles[3 * (part->triangle_count + added) + q2] = (unsigned short)remap[corners[q2]];
			}
			added++;
		}
		part->vertex_count += used;
		part->triangle_count += added;
		spartan->triangles += added;
		spartan->vertices += used;
	}
	free(converted);
	free(remap);
	return TRUE;
}

/* the parts left with no triangle dropped (a part's strips all degenerate): a
strip of none has no triangle count to write */
H3_ALIGN static void empty_parts_drop(struct h3_spartan *spartan)
{
	short from, to = 0;

	for (from = 0; from < spartan->part_count; from++)
	{
		if (spartan->parts[from].triangle_count < 1 || spartan->parts[from].vertex_count < 1)
		{
			free(spartan->parts[from].vertices);
			free(spartan->parts[from].triangles);
			continue;
		}
		if (to != from)
			spartan->parts[to] = spartan->parts[from];
		to++;
	}
	spartan->part_count = to;
}

/* a catalog's name: a region's or a permutation's. It goes into a pack's recipe,
a line of text, and into a look's "a=b,c=d", so only what those can carry */
H3_ALIGN static boolean catalog_name_valid(char const *name)
{
	unsigned long i;

	for (i = 0; name[i]; i++)
	{
		char c = name[i];

		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
			return FALSE;
	}
	return i > 0;
}

/* ---------- its own idle

The graph (jmad) the model's own game plays, read as the model is. Its nodes at
0x14 (0x20 each: the name), its animations at 0x50 (0x88 each: the name, frames at
0x10, its resource group and member at 0x28), its groups at 0xF8 (0xC each: the
resource's datum at 4). A resource's root is its members' block (0x30 each: frames
at 8, flags' sizes at 0xC and 0xD, static block at 0x12, animated at 0x18, the
data's pointer at 0x28); the data as Halo 2's (h2_animation.h). Offsets read from
the owner's maps: rifle idle's blocks and sizes add up to the byte */

enum
{
	JMAD_NODES = 0x14,
	JMAD_NODE_SIZE = 0x20,
	JMAD_ANIMATIONS = 0x50,
	JMAD_ANIMATION_SIZE = 0x88,
	JMAD_ANIMATION_FRAMES = 0x10,
	JMAD_ANIMATION_GROUP = 0x28,
	JMAD_GROUPS = 0xF8,
	JMAD_GROUP_SIZE = 0xC,
	JMAD_MEMBER_SIZE = 0x30,
	JMAD_MAXIMUM_FRAMES = 512,
	JMAD_MAXIMUM_BYTES = 1024 * 1024,
};

/* the idle's data wanted: read with the model's */
struct idle_wanted
{
	struct h3_resource resource;
	long jmad_nodes;
	unsigned long nodes;
	long frames;
	unsigned long static_size, animated_size, static_flags, animated_flags, size;
	unsigned char *data;
};

H3_ALIGN static void idle_plan(struct h3_cache *cache, short model, char const *name, struct idle_wanted *idle, struct h3_page_request *requests,
	long *request_count)
{
	static char const *const graphs[NUMBER_OF_H3_MODELS] = { "objects\\characters\\masterchief\\masterchief", "objects\\characters\\elite\\elite" };
	long tag = h3_cache_find_tag(cache, 'jmad', graphs[model]), count = 0, group_count = 0, i, group, member, members_count;
	unsigned long animations = 0, groups = 0, members, target, kind;
	struct h3_cache_tag t;
	unsigned char const *m;

	if (tag == NONE || !h3_cache_tag_get(cache, tag, &t) ||
		!h3_cache_block(cache, t.address + JMAD_NODES, JMAD_NODE_SIZE, &idle->jmad_nodes, &idle->nodes) ||
		!h3_cache_block(cache, t.address + JMAD_ANIMATIONS, JMAD_ANIMATION_SIZE, &count, &animations) ||
		!h3_cache_block(cache, t.address + JMAD_GROUPS, JMAD_GROUP_SIZE, &group_count, &groups) ||
		idle->jmad_nodes < 1 || idle->jmad_nodes > H2_ANIMATION_MAXIMUM_NODES)
	{
		return;
	}
	for (i = 0; i < count; i++)
	{
		if (!strcmp(h3_cache_string(cache, h3_cache_read32(cache, animations + JMAD_ANIMATION_SIZE * (unsigned long)i)), name))
			break;
	}
	if (i == count)
		return;
	idle->frames = (unsigned short)h3_cache_read16(cache, animations + JMAD_ANIMATION_SIZE * (unsigned long)i + JMAD_ANIMATION_FRAMES);
	group = h3_cache_read16(cache, animations + JMAD_ANIMATION_SIZE * (unsigned long)i + JMAD_ANIMATION_GROUP);
	member = h3_cache_read16(cache, animations + JMAD_ANIMATION_SIZE * (unsigned long)i + JMAD_ANIMATION_GROUP + 2);
	if (idle->frames < 1 || idle->frames > JMAD_MAXIMUM_FRAMES || group < 0 || group >= group_count ||
		!h3_cache_resource_get(cache, (long)(h3_cache_read32(cache, groups + JMAD_GROUP_SIZE * (unsigned long)group + 4) & 0xFFFF), &idle->resource))
	{
		return;
	}
	members_count = (long)h3_resource_read32(&idle->resource, idle->resource.root);
	if (member < 0 || member >= members_count || !h3_resource_pointer(&idle->resource, idle->resource.root + 4, &kind, &members) || kind != 2 ||
		members + JMAD_MEMBER_SIZE * ((unsigned long)member + 1) > idle->resource.control_size)
	{
		return;
	}
	m = idle->resource.control + members + JMAD_MEMBER_SIZE * (unsigned long)member;
	idle->static_flags = m[0xC];
	idle->animated_flags = m[0xD];
	idle->static_size = (unsigned short)read16(m + 0x12);
	idle->animated_size = read32(m + 0x18);
	idle->size = idle->static_size + idle->animated_size + idle->static_flags + idle->animated_flags;
	if ((long)(unsigned short)read16(m + 8) != idle->frames || idle->size > JMAD_MAXIMUM_BYTES || read32(m + 0x1C) < idle->size ||
		!h3_resource_pointer(&idle->resource, members + JMAD_MEMBER_SIZE * (unsigned long)member + 0x28, &kind, &target) ||
		kind != 4 || idle->resource.page[0] == NONE || *request_count >= MAXIMUM_REQUESTS || !(idle->data = malloc(idle->size)))
	{
		return;
	}
	memset(&requests[*request_count], 0, sizeof(requests[0]));
	requests[*request_count].page = idle->resource.page[0];
	requests[*request_count].offset = idle->resource.offset[0] + target;
	requests[*request_count].size = idle->size;
	requests[*request_count].out = idle->data;
	(*request_count)++;
}

/* a x b: b first */
H3_ALIGN static void quaternion_multiply(float const *a, float const *b, float *out)
{
	float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];

	out[0] = x, out[1] = y, out[2] = z, out[3] = w;
}

/* q v q^-1 */
H3_ALIGN static void quaternion_rotate(float const *q, float const *v, float *out)
{
	float t[3], x, y, z;

	t[0] = 2.0f * (q[1] * v[2] - q[2] * v[1]);
	t[1] = 2.0f * (q[2] * v[0] - q[0] * v[2]);
	t[2] = 2.0f * (q[0] * v[1] - q[1] * v[0]);
	x = v[0] + q[3] * t[0] + q[1] * t[2] - q[2] * t[1];
	y = v[1] + q[3] * t[1] + q[2] * t[0] - q[0] * t[2];
	z = v[2] + q[3] * t[2] + q[0] * t[1] - q[1] * t[0];
	out[0] = x, out[1] = y, out[2] = z;
}

/* a graph node's rest: the model's own node of that name; its key */
H3_ALIGN static boolean idle_node_rest(struct h3_cache *cache, struct idle_wanted const *idle, long n, unsigned long model_nodes, long model_node_count,
	struct h2_animation_pose *rest, char *key, unsigned long key_size)
{
	long i;

	memset(rest, 0, sizeof(*rest));
	rest->rotation[3] = 1.0f;
	rest->scale = 1.0f;
	node_key(h3_cache_string(cache, h3_cache_read32(cache, idle->nodes + JMAD_NODE_SIZE * (unsigned long)n)), key, key_size);
	for (i = 0; i < model_node_count; i++)
	{
		unsigned char const *node = h3_cache_at(cache, model_nodes + MODE_NODE_SIZE * (unsigned long)i, MODE_NODE_SIZE);
		char other[40];

		if (!node)
			return FALSE;
		node_key(h3_cache_string(cache, read32(node)), other, sizeof(other));
		if (!strcmp(key, other))
		{
			int c;

			for (c = 0; c < 3; c++)
				rest->translation[c] = readf(node + 0xC + 4 * c);
			for (c = 0; c < 4; c++)
				rest->rotation[c] = readf(node + 0x18 + 4 * c);
		}
	}
	return TRUE;
}

/* the model's nodes the skeleton has no such node of - fingers, on Halo CE's or
Halo 2's bones - held as the idle's first frame holds them: each is drawn on the
ancestor the skeleton has (the map's target), at the place the idle gives it from
there: a hand closed on a weapon, not Halo 3's default open hand.
x(hand) = Q (Ri v + pi) + t, Q and t the idle's way from the ancestor to the node */
H3_ALIGN static void fingers_held(struct h3_cache *cache, struct h3_spartan_skeleton const *skeleton, unsigned long model_nodes, long model_node_count,
	struct idle_wanted const *idle, struct node_map *maps)
{
	long count = idle->jmad_nodes, n, i;
	struct h2_animation_pose *poses = malloc((unsigned long)count * (unsigned long)idle->frames * sizeof(*poses));
	unsigned char *sets = calloc((unsigned long)count + 1, 1);
	struct h2_animation_pose world[H2_ANIMATION_MAXIMUM_NODES];
	char keys[H2_ANIMATION_MAXIMUM_NODES][40];

	if (!poses || !sets)
		goto done;
	for (n = 0; n < count; n++)
	{
		long f;

		if (!idle_node_rest(cache, idle, n, model_nodes, model_node_count, &poses[n], keys[n], sizeof(keys[n])))
			goto done;
		for (f = 1; f < idle->frames; f++)
			poses[f * count + n] = poses[n];
	}
	if (!h2_animation_blocks_decode(poses, sets, (short)count, (short)idle->frames, idle->data, idle->static_size, idle->animated_size,
		idle->static_flags, idle->animated_flags))
	{
		goto done;
	}
	for (n = 0; n < count; n++)
	{
		unsigned char const *node = h3_cache_at(cache, idle->nodes + JMAD_NODE_SIZE * (unsigned long)n, JMAD_NODE_SIZE);
		short parent = node ? read16(node + 8) : NONE;
		int c;

		/* (this engine's convention: idle_make's) */
		for (c = 0; c < 3; c++)
			poses[n].rotation[c] = -poses[n].rotation[c];
		world[n] = poses[n];
		if (parent >= 0 && parent < n)
		{
			float moved[3];

			quaternion_multiply(world[parent].rotation, poses[n].rotation, world[n].rotation);
			quaternion_rotate(world[parent].rotation, poses[n].translation, moved);
			for (c = 0; c < 3; c++)
				world[n].translation[c] = world[parent].translation[c] + moved[c];
		}
	}
	for (i = 0; i < model_node_count; i++)
	{
		unsigned char const *node = h3_cache_at(cache, model_nodes + MODE_NODE_SIZE * (unsigned long)i, MODE_NODE_SIZE);
		char key[40], other[40];
		long own = NONE, carrier = NONE;
		float inverse[13];

		if (!node)
			break;
		node_key(h3_cache_string(cache, read32(node)), key, sizeof(key));
		node_key(skeleton->names[maps[i].target], other, sizeof(other));
		if (!strcmp(key, other))
			continue;
		for (n = 0; n < count; n++)
		{
			if (!strcmp(keys[n], key))
				own = n;
			if (!strcmp(keys[n], other))
				carrier = n;
		}
		if (own == NONE || carrier == NONE)
			continue;
		for (n = 0; n < 13; n++)
			inverse[n] = readf(node + 0x28 + 4 * n);
		{
			float conjugate[4] = { -world[carrier].rotation[0], -world[carrier].rotation[1], -world[carrier].rotation[2], world[carrier].rotation[3] };
			float q[4], t[3], offset[3], rh[9], ph[3], ri[9], pi[3];
			int c, k;

			quaternion_multiply(conjugate, world[own].rotation, q);
			for (c = 0; c < 3; c++)
				offset[c] = world[own].translation[c] - world[carrier].translation[c];
			quaternion_rotate(conjugate, offset, t);
			matrix_columns(inverse, ri, pi);
			quaternion_rotate(q, pi, ph);
			for (c = 0; c < 3; c++)
				ph[c] += t[c];
			/* (Q Ri: Q on each column) */
			for (c = 0; c < 3; c++)
			{
				float column[3] = { ri[c], ri[3 + c], ri[6 + c] }, turned[3];

				quaternion_rotate(q, column, turned);
				for (k = 0; k < 3; k++)
					rh[3 * k + c] = turned[k];
			}
			node_map_make_from(rh, ph, skeleton->inverse[maps[i].target], maps[i].target, &maps[i]);
		}
	}
done:
	free(poses);
	free(sets);
}

/* the idle read made poses of the skeleton's nodes: by name, each of them; a node
it has no such place for stays out (idle_node_set) */
H3_ALIGN static void idle_make(struct h3_cache *cache, struct h3_spartan *spartan, struct h3_spartan_skeleton const *skeleton, boolean own,
	unsigned long model_nodes, long model_node_count, struct idle_wanted const *idle, struct idle_wanted const *aim)
{
	long count = idle->jmad_nodes, frames = idle->frames, n, f;
	struct h2_animation_pose *poses = malloc((unsigned long)count * (unsigned long)frames * sizeof(*poses));
	struct h2_animation_pose *out = malloc((unsigned long)skeleton->node_count * (unsigned long)frames * sizeof(*out));
	unsigned char *sets = calloc((unsigned long)count + 1, 1);
	long target[H2_ANIMATION_MAXIMUM_NODES];

	if (!poses || !out || !sets)
		goto done;
	/* (its rest: the model's own node of that name) */
	for (n = 0; n < count; n++)
	{
		char key[40], other[40];
		struct h2_animation_pose rest;
		short s;

		if (!idle_node_rest(cache, idle, n, model_nodes, model_node_count, &rest, key, sizeof(key)))
			goto done;
		for (f = 0; f < frames; f++)
			poses[f * count + n] = rest;
		target[n] = NONE;
		for (s = 0; s < skeleton->node_count; s++)
		{
			node_key(skeleton->names[s], other, sizeof(other));
			if (!strcmp(key, other))
				target[n] = s;
		}
	}
	if (!h2_animation_blocks_decode(poses, sets, (short)count, (short)frames, idle->data, idle->static_size, idle->animated_size,
		idle->static_flags, idle->animated_flags))
	{
		platform_log("h3 spartan: its idle is not as expected");
		goto done;
	}
	/* (the idle aims straight ahead, its rifle at its eye. As the menus' other
	characters - menu_scene_objects.c's MENU_SCENE_AIM_DOWN -, a calm stance: part of
	the way to the aim overlay's lowest, its frame 1 of its nine, frame 4 level; base
	x change. The Elite's idle stands calm as it is) */
	if (!own && aim->data && aim->jmad_nodes == count)
	{
		struct h2_animation_pose *change = calloc((unsigned long)count * (unsigned long)aim->frames, sizeof(*change));
		unsigned char *aim_sets = calloc((unsigned long)count + 1, 1);
		long at = 1;

		for (n = 0; change && n < count * aim->frames; n++)
		{
			change[n].rotation[3] = 1.0f;
			change[n].scale = 1.0f;
		}
		if (change && aim_sets && at < aim->frames && h2_animation_blocks_decode(change, aim_sets, (short)count, (short)aim->frames, aim->data, aim->static_size,
			aim->animated_size, aim->static_flags, aim->animated_flags))
		{
			for (f = 0; f < frames; f++)
			{
				for (n = 0; n < count; n++)
				{
					struct h2_animation_pose *pose = &poses[f * count + n];
					struct h2_animation_pose const *by = &change[at * count + n];
					float part[4], length = 0.0f;
					int c;

					for (c = 0; c < 4; c++)
					{
						/* (the short way from no change, whichever sign it has) */
						part[c] = (by->rotation[3] < 0.0f ? -by->rotation[c] : by->rotation[c]) * IDLE_AIM_DOWN + (c == 3 ? 1.0f - IDLE_AIM_DOWN : 0.0f);
						length += part[c] * part[c];
					}
					length = (float)sqrt(length);
					for (c = 0; c < 4 && length > 0.0f; c++)
						part[c] /= length;
					quaternion_multiply(pose->rotation, part, pose->rotation);
					for (c = 0; c < 3; c++)
						pose->translation[c] += by->translation[c] * IDLE_AIM_DOWN;
				}
			}
		}
		free(change);
		free(aim_sets);
	}
	/* (this engine's convention; and the skeleton's parents are not always Halo 3's
	- a cyborg's clavicles hang from its neck, not its spine -: each node's place in
	the model worked out, then under its parent there) */
	for (n = 0; n < count * frames; n++)
	{
		int c;

		for (c = 0; c < 3; c++)
		{
			poses[n].rotation[c] = -poses[n].rotation[c];
			poses[n].translation[c] *= own ? OWN_SCALE : 1.0f;
		}
	}
	for (f = 0; f < frames; f++)
	{
		struct h2_animation_pose *frame = &poses[f * count];
		struct h2_animation_pose *to = &out[f * skeleton->node_count];
		struct h2_animation_pose world[H2_ANIMATION_MAXIMUM_NODES], placed[H3_SPARTAN_MAXIMUM_NODES];
		boolean have[H3_SPARTAN_MAXIMUM_NODES];

		memset(have, 0, sizeof(have));
		for (n = 0; n < count; n++)
		{
			unsigned char const *node = h3_cache_at(cache, idle->nodes + JMAD_NODE_SIZE * (unsigned long)n, JMAD_NODE_SIZE);
			short parent = node ? read16(node + 8) : NONE;

			world[n] = frame[n];
			if (parent >= 0 && parent < n)
			{
				float moved[3];
				int c;

				quaternion_multiply(world[parent].rotation, frame[n].rotation, world[n].rotation);
				quaternion_rotate(world[parent].rotation, frame[n].translation, moved);
				for (c = 0; c < 3; c++)
					world[n].translation[c] = world[parent].translation[c] + moved[c];
			}
			if (target[n] != NONE)
			{
				placed[target[n]] = world[n];
				have[target[n]] = TRUE;
			}
		}
		for (n = 0; n < skeleton->node_count; n++)
		{
			short parent = skeleton->parents[n];

			if (!have[n])
				continue;
			spartan->idle_node_set[n] = TRUE;
			to[n] = placed[n];
			if (parent >= 0 && parent < skeleton->node_count && have[parent])
			{
				float inverse[4] = { -placed[parent].rotation[0], -placed[parent].rotation[1], -placed[parent].rotation[2], placed[parent].rotation[3] };
				float offset[3];
				int c;

				for (c = 0; c < 3; c++)
					offset[c] = placed[n].translation[c] - placed[parent].translation[c];
				quaternion_multiply(inverse, placed[n].rotation, to[n].rotation);
				quaternion_rotate(inverse, offset, to[n].translation);
			}
		}
	}
	spartan->idle_frames = (short)frames;
	spartan->idle_poses = out;
	out = NULL;
done:
	free(poses);
	free(out);
	free(sets);
}

/* where its weapon goes in the preview: its right hand's marker group (at 0x3C,
0x10 each: the name, its markers; a marker's node at 2, then position, rotation) */
H3_ALIGN static void hand_read(struct h3_cache *cache, struct h3_spartan *spartan, unsigned long mode, boolean own, struct node_map const *maps,
	long node_count)
{
	long count = 0, i, markers_count = 0;
	unsigned long groups = 0, markers = 0;

	if (!h3_cache_block(cache, mode + 0x3C, 0x10, &count, &groups))
		return;
	for (i = 0; i < count; i++)
	{
		char const *name = h3_cache_string(cache, h3_cache_read32(cache, groups + 0x10 * (unsigned long)i));

		if ((!strcmp(name, "right_hand") || !strcmp(name, "right_hand_elite")) &&
			h3_cache_block(cache, groups + 0x10 * (unsigned long)i + 4, 0x24, &markers_count, &markers) && markers_count > 0)
		{
			unsigned char const *m = h3_cache_at(cache, markers, 0x24);
			int c;

			if (!m)
				return;
			if (m[2] >= node_count)
				return;
			spartan->hand_node = maps[m[2]].target;
			for (c = 0; c < 3; c++)
				spartan->hand_translation[c] = readf(m + 4 + 4 * c) * (own ? OWN_SCALE : 1.0f);
			for (c = 0; c < 4; c++)
				spartan->hand_rotation[c] = c < 3 ? -readf(m + 16 + 4 * c) : readf(m + 16 + 4 * c);
		}
	}
}

/* classic Halo 3's multiplayer armour, by permutation name; each model's own
"base" is the rows' DEFAULT. From the mp_ selections of the customized characters
of Halo 3's multiplayer_globals tag (H3EK): helmets CQB to Security, shoulders,
chests with Katana and Bungie; the Elite's four sets. MCC's additions in the same
models - legacy_ (the same armour cut for the old one-piece body), el_, belos_,
blackguard_, drengr_, raven_, spi, xbox - are left out. That tag's mp_chest_security
has no mesh in the model, so no row */
H3_ALIGN static boolean classic_permutation(char const *name)
{
	static char const *const classic[] =
	{
		"mp_cqb", "mp_eod", "mp_eva", "mp_hayabusa", "mp_mark_v", "mp_odst", "mp_recon", "mp_rogue", "mp_scout", "mp_security",
		"mp_katana", "mp_bungie",
		"mp_blades", "mp_predator", "mp_raptor", "mp_scythe",
	};
	unsigned long i;

	for (i = 0; i < sizeof(classic) / sizeof(classic[0]); i++)
	{
		if (!strcmp(classic[i], name))
			return TRUE;
	}
	return FALSE;
}

/* whether a permutation is a choice of the catalog's */
H3_ALIGN static boolean catalog_offers(char const *name, short first_mesh)
{
	return permutation_real(name, first_mesh) && strlen(name) < H3_CATALOG_NAME_SIZE && catalog_name_valid(name) && classic_permutation(name);
}

/* ---------- public code */

H3_ALIGN boolean h3_spartan_maps_directory(char *out, unsigned long out_size)
{
	/* (the content folder as the game names it: the port's fopen knows it) */
	if (!platform_mcc_root(NULL, 0))
		return FALSE;
	return snprintf(out, out_size, "d:\\mcc\\halo3\\maps\\") < (int)out_size;
}

H3_ALIGN boolean h3_spartan_offered(void)
{
#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
	return FALSE;
#else
	static int offered = -1;

	if (offered < 0)
	{
		char const *setting = getenv("HALO_H3_MODELS");
		char directory[1100], path[1200];
		FILE *file;

		offered = 0;
		if (!(setting && !strcmp(setting, "0")) && h3_spartan_maps_directory(directory, sizeof(directory)))
		{
			snprintf(path, sizeof(path), "%sshared.map", directory);
			file = fopen(path, "rb");
			if (file)
			{
				unsigned long i;
				static char const *const maps[] = H3_SPARTAN_MAPS;

				fclose(file);
				for (i = 0; i < sizeof(maps) / sizeof(maps[0]) && !offered; i++)
				{
					snprintf(path, sizeof(path), "%s%s.map", directory, maps[i]);
					file = fopen(path, "rb");
					if (file)
					{
						fclose(file);
						offered = 1;
					}
				}
			}
		}
		platform_log("h3 spartan: %s", offered ? "offered (the content folder has Halo 3's maps)" : "not offered");
	}
	return offered ? TRUE : FALSE;
#endif
}

/* whether a read is to stop (said in why) */
H3_ALIGN static boolean read_stopped(struct h3_cache_read_control const *control, char *why, long why_size)
{
	if (!control || !control->cancel || !*control->cancel)
		return FALSE;
	why_set(why, why_size, "it was stopped%s", NULL);
	return TRUE;
}

/* a render model (the tag at index tag of cache) read and rebound to skeleton:
the Spartan or the Elite (biped: the look's armour, its idle and hands read
with it - h3_spartan_read), or any other model on its own bones (h3_model_read:
a weapon's, its first-person arms' - no idle, a region's permutation the look
names, else "base", else its first with a mesh). The cache stays the caller's */
H3_ALIGN static struct h3_spartan *model_read(struct h3_cache *cache, long tag, struct h3_spartan_skeleton const *skeleton, char const *look,
	boolean biped, struct h3_cache_read_control const *control, char *why, long why_size)
{
	struct h3_spartan *spartan = NULL;
	struct h3_resource geometry;
	struct h3_resource *image_resources = NULL;
	long image_resource_count = 0;
	struct h3_cache_tag mode;
	struct node_map maps[MAXIMUM_H3_NODES];
	struct buffer_wanted buffers[MAXIMUM_MESHES];
	short mesh_slots[MAXIMUM_MESHES], mesh_choices[MAXIMUM_MESHES];
	unsigned long mesh_addresses[MAXIMUM_MESHES], meshes = 0, nodes = 0, regions = 0, materials = 0, compression = 0;
	long mesh_count = 0, node_count = 0, region_count = 0, material_count = 0, compression_count = 0;
	struct h3_page_request *requests = NULL;
	long request_count = 0;
	struct image_wanted *images = NULL;
	long methods[H3_SPARTAN_MAXIMUM_MATERIALS], texture_tags[H3_SPARTAN_MAXIMUM_MATERIALS][MATERIAL_TEXTURES];
	short material_images[H3_SPARTAN_MAXIMUM_MATERIALS][MATERIAL_TEXTURES];
	short *part_materials[MAXIMUM_MESHES];
	long image_count = 0;
	short digit_images[10] = { NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE }, digit_material = NONE;
	float bounds[10];
	long i, j, wanted = 0;
	boolean ok = FALSE;
	char log_look[256] = "";
	int log_length = 0;
	char flag_text[8];
	boolean own = biped && look_value(look, "own", flag_text, sizeof(flag_text));
	boolean armoury = biped && look_value(look, "armoury", flag_text, sizeof(flag_text));
	struct idle_wanted idle, aim;
	short slot_count = 1, slot_choices[H3_SPARTAN_MAXIMUM_SLOTS];
	unsigned long long started = vita_host_time_us(), times[3] = { 0, 0, 0 };

	memset(&geometry, 0, sizeof(geometry));
	memset(&idle, 0, sizeof(idle));
	memset(&aim, 0, sizeof(aim));
	memset(buffers, 0, sizeof(buffers));
	memset(part_materials, 0, sizeof(part_materials));
	if (!cache)
		return NULL;
	if (tag == NONE || !h3_cache_tag_get(cache, tag, &mode) || mode.group_tag != 'mode' ||
		!h3_cache_block(cache, mode.address + MODE_NODES, MODE_NODE_SIZE, &node_count, &nodes) || node_count < 1 || node_count > MAXIMUM_H3_NODES ||
		!h3_cache_block(cache, mode.address + MODE_REGIONS, MODE_REGION_SIZE, &region_count, &regions) || region_count < 1 ||
		!h3_cache_block(cache, mode.address + MODE_MATERIALS, MODE_MATERIAL_SIZE, &material_count, &materials) ||
		!h3_cache_block(cache, mode.address + MODE_MESHES, MODE_MESH_SIZE, &mesh_count, &meshes) ||
		!h3_cache_block(cache, mode.address + MODE_COMPRESSION, MODE_COMPRESSION_SIZE, &compression_count, &compression) || compression_count < 1 ||
		!h3_cache_read(cache, compression + 4, sizeof(bounds), bounds))
	{
		why_set(why, why_size, "Halo 3's Spartan's model is not as expected%s", NULL);
		goto done;
	}
	memset(material_images, 0xFF, sizeof(material_images));
	spartan = malloc(sizeof(*spartan));
	requests = malloc(MAXIMUM_REQUESTS * sizeof(*requests));
	images = malloc(H3_SPARTAN_MAXIMUM_BITMAPS * sizeof(*images));
	image_resources = malloc(H3_SPARTAN_MAXIMUM_BITMAPS * sizeof(*image_resources));
	if (!spartan || !requests || !images || !image_resources)
	{
		why_set(why, why_size, "out of memory%s", NULL);
		goto done;
	}
	memset(spartan, 0, sizeof(*spartan));
	memset(spartan->digit_bitmaps, 0xFF, sizeof(spartan->digit_bitmaps));
	memset(requests, 0, MAXIMUM_REQUESTS * sizeof(*requests));
	memset(images, 0, H3_SPARTAN_MAXIMUM_BITMAPS * sizeof(*images));
	/* the nodes rebound: by name, else the nearest ancestor the skeleton has */
	for (i = 0; i < node_count; i++)
	{
		unsigned char node[MODE_NODE_SIZE];
		char key[40], other[40];
		char const *name;
		float inverse[13];
		short target = NONE;
		short s, parent;
		long up = i;
		short walked = 0;

		do
		{
			if (!h3_cache_read(cache, nodes + MODE_NODE_SIZE * (unsigned long)up, MODE_NODE_SIZE, node))
				goto done;
			name = h3_cache_string(cache, read32(node));
			parent = read16(node + 4);
			node_key(name, key, sizeof(key));
			for (s = 0; s < skeleton->node_count; s++)
			{
				node_key(skeleton->names[s], other, sizeof(other));
				if (!strcmp(key, other))
					target = s;
			}
			up = parent;
			walked++;
		} while (target == NONE && up >= 0 && up < node_count && walked < 32);
		if (target == NONE)
			target = 0;
		/* (the node's own inverse: its rotation about its own bind) */
		if (!h3_cache_read(cache, nodes + MODE_NODE_SIZE * (unsigned long)i + 0x28, sizeof(inverse), inverse))
			goto done;
		/* (a finger on the hand: moved as the hand moves) */
		{
			float hand[13];
			long source = i;
			short steps;

			for (steps = 0; steps < 32; steps++)
			{
				unsigned char walk[MODE_NODE_SIZE];

				if (!h3_cache_read(cache, nodes + MODE_NODE_SIZE * (unsigned long)source, MODE_NODE_SIZE, walk))
					goto done;
				node_key(h3_cache_string(cache, read32(walk)), key, sizeof(key));
				node_key(skeleton->names[target], other, sizeof(other));
				if (!strcmp(key, other))
				{
					memcpy(hand, walk + 0x28, sizeof(hand));
					{
						int f;

						for (f = 0; f < 13; f++)
							inverse[f] = readf(&hand[f]);
					}
					break;
				}
				source = read16(walk + 4);
				if (source < 0 || source >= node_count)
					break;
			}
		}
		if (getenv("HALO_H3_DEBUG"))
		{
			unsigned char own[MODE_NODE_SIZE];

			if (h3_cache_read(cache, nodes + MODE_NODE_SIZE * (unsigned long)i, MODE_NODE_SIZE, own))
				platform_log("h3 spartan: node %s: %s", h3_cache_string(cache, read32(own)), skeleton->names[target]);
		}
		node_map_make(inverse, skeleton->inverse[target], target, &maps[i]);
		/* (its own bones: nothing moved, all of it the preview's size) */
		if (own)
		{
			memset(maps[i].rotation, 0, sizeof(maps[i].rotation));
			memset(maps[i].offset, 0, sizeof(maps[i].offset));
			maps[i].rotation[0] = maps[i].rotation[4] = maps[i].rotation[8] = OWN_SCALE;
			/* (a finger past what the renderer skins, rasterizer.h's 44: on the
			ancestor it can) */
			for (up = i, walked = 0; up >= OWN_SKINNED_NODES && skeleton->parents[up] >= 0 && walked < 32; up = skeleton->parents[up], walked++);
			maps[i].target = (short)(up < OWN_SKINNED_NODES ? up : 0);
		}
	}
	/* the meshes of the look */
	for (i = 0; i < region_count; i++)
	{
		unsigned long region = regions + MODE_REGION_SIZE * (unsigned long)i, permutations;
		long permutation_count, chosen = NONE, k;
		short count, first;
		char name[48], wanted_name[48];

		snprintf(name, sizeof(name), "%s", h3_cache_string(cache, h3_cache_read32(cache, region)));
		boolean named = look_value(look, name, wanted_name, sizeof(wanted_name));

		short slot = 0, choice = 0;

		if (!h3_cache_block(cache, region + 4, MODE_PERMUTATION_SIZE, &permutation_count, &permutations))
			continue;
		/* (an armoury's region with choices: a slot of its own, each of them there
		beside the model's own) */
		if (armoury && catalog_name_valid(name) && strlen(name) < H3_CATALOG_NAME_SIZE)
		{
			for (j = 0; j < permutation_count; j++)
			{
				unsigned char const *p = h3_cache_at(cache, permutations + MODE_PERMUTATION_SIZE * (unsigned long)j, MODE_PERMUTATION_SIZE);

				if (!p || !catalog_offers(h3_cache_string(cache, read32(p)), read16(p + 4)))
					continue;
				if (!slot)
				{
					if (slot_count >= H3_SPARTAN_MAXIMUM_SLOTS)
					{
						why_set(why, why_size, "Halo 3's model has more regions of armour than an object%s", NULL);
						goto done;
					}
					slot = slot_count++;
				}
				choice++;
				for (count = read16(p + 6), first = read16(p + 4), k = 0; first >= 0 && k < count && wanted < MAXIMUM_MESHES; k++)
				{
					if (first + k < mesh_count)
					{
						mesh_slots[wanted] = slot;
						mesh_choices[wanted] = choice;
						mesh_addresses[wanted++] = meshes + MODE_MESH_SIZE * (unsigned long)(first + k);
					}
				}
			}
			if (slot)
				slot_choices[slot] = (short)(choice + 1);
		}
		/* (what the look names, else "base" - which has no mesh where a shared
		region draws it: the Elite's chest) */
		for (j = 0; j < permutation_count; j++)
		{
			unsigned char const *p = h3_cache_at(cache, permutations + MODE_PERMUTATION_SIZE * (unsigned long)j, MODE_PERMUTATION_SIZE);
			char const *text = p ? h3_cache_string(cache, read32(p)) : "";
			short first = p ? read16(p + 4) : NONE;

			if (!permutation_real(text, first))
				continue;
			if (named && !strcmp(text, wanted_name))
			{
				chosen = j;
				break;
			}
			if (!strcmp(text, "base"))
				chosen = j;
			/* (another model - a weapon's "standard" -: its first with a mesh
			where it has no "base"; the biped's shared regions keep none) */
			if (!biped && chosen == NONE && !named)
				chosen = j;
		}
		if (chosen == NONE)
			continue;
		snprintf(wanted_name, sizeof(wanted_name), "%s", h3_cache_string(cache,
			h3_cache_read32(cache, permutations + MODE_PERMUTATION_SIZE * (unsigned long)chosen)));
		{
			unsigned char const *p = h3_cache_at(cache, permutations + MODE_PERMUTATION_SIZE * (unsigned long)chosen, MODE_PERMUTATION_SIZE);
			short first = p ? read16(p + 4) : NONE, count = p ? read16(p + 6) : 0;

			log_length += snprintf(log_look + log_length, sizeof(log_look) - (size_t)log_length, "%s=%s ", name, wanted_name);
			if (log_length >= (int)sizeof(log_look) - 1)
				log_length = (int)sizeof(log_look) - 1;
			for (j = 0; first >= 0 && j < count && wanted < MAXIMUM_MESHES; j++)
			{
				if (first + j < mesh_count)
				{
					mesh_slots[wanted] = slot;
					mesh_choices[wanted] = 0;
					mesh_addresses[wanted++] = meshes + MODE_MESH_SIZE * (unsigned long)(first + j);
				}
			}
		}
	}
	platform_log("h3 spartan: %s: %s(%ld meshes)", h3_cache_name(cache), log_look, wanted);
	if (!wanted)
	{
		why_set(why, why_size, "the look has no meshes%s", NULL);
		goto done;
	}
	if (!h3_cache_resource_get(cache, (long)(h3_cache_read32(cache, mode.address + MODE_RESOURCE) & 0xFFFF), &geometry))
	{
		why_set(why, why_size, "the Spartan's geometry resource cannot be read%s", NULL);
		goto done;
	}
	for (i = 0; i < wanted; i++)
	{
		unsigned char mesh[MODE_MESH_SIZE];
		long part_count;
		unsigned long parts;

		if (!h3_cache_read(cache, mesh_addresses[i], MODE_MESH_SIZE, mesh) ||
			!h3_cache_block(cache, mesh_addresses[i], MODE_PART_SIZE, &part_count, &parts) ||
			!buffer_find(&geometry, read16(mesh + 0x18), read16(mesh + 0x28), &buffers[i]))
		{
			why_set(why, why_size, "a mesh of the Spartan cannot be read%s", NULL);
			goto done;
		}
		buffers[i].vertices = malloc(buffers[i].vertex_size);
		buffers[i].indices = malloc(buffers[i].index_size);
		part_materials[i] = malloc(sizeof(short) * (unsigned long)(part_count + 1));
		if (!buffers[i].vertices || !buffers[i].indices || !part_materials[i])
			goto done;
		requests[request_count].page = geometry.page[0];
		requests[request_count].offset = geometry.offset[0] + buffers[i].vertex_offset;
		requests[request_count].size = buffers[i].vertex_size;
		requests[request_count].out = buffers[i].vertices;
		request_count++;
		requests[request_count].page = geometry.page[0];
		requests[request_count].offset = geometry.offset[0] + buffers[i].index_offset;
		requests[request_count].size = buffers[i].index_size;
		requests[request_count].out = buffers[i].indices;
		request_count++;
		/* its parts' materials, and each new material's bitmaps wanted */
		for (j = 0; j < part_count; j++)
		{
			unsigned char const *p = h3_cache_at(cache, parts + MODE_PART_SIZE * (unsigned long)j, MODE_PART_SIZE);
			short material = p ? read16(p) : NONE;
			long method = NONE;
			short before = spartan->material_count;

			if (material >= 0 && material < material_count)
				method = h3_cache_reference(cache, materials + MODE_MATERIAL_SIZE * (unsigned long)material);
			part_materials[i][j] = method != NONE ? material_for(cache, spartan, method, methods, texture_tags) : NONE;
			if (spartan->material_count > before)
			{
				short m = before;
				int texture;

				for (texture = 0; texture < MATERIAL_TEXTURES; texture++)
				{
					long wanted_tag = texture_tags[m][texture], k;

					material_images[m][texture] = NONE;
					if (wanted_tag == NONE)
						continue;
					for (k = 0; k < image_count; k++)
					{
						if (images[k].tag == wanted_tag && images[k].index == 0)
							material_images[m][texture] = (short)k;
					}
					if (material_images[m][texture] == NONE && image_count < H3_SPARTAN_MAXIMUM_BITMAPS &&
						request_count + 2 <= MAXIMUM_REQUESTS && image_plan(cache, wanted_tag, base_side_limit(), &images[image_count],
							requests, &request_count, &image_resource_count, image_resources, H3_SPARTAN_MAXIMUM_BITMAPS))
					{
						material_images[m][texture] = (short)image_count++;
					}
				}
			}
			/* (a counter's digits: its numbers' ten images, once) */
			if (spartan->material_count > before && spartan->materials[before].digit && digit_images[0] == NONE &&
				texture_tags[before][_material_illum] != NONE)
			{
				long d;

				for (d = 0; d < 10; d++)
				{
					if (image_count < H3_SPARTAN_MAXIMUM_BITMAPS && request_count + 2 <= MAXIMUM_REQUESTS &&
						image_plan_index(cache, texture_tags[before][_material_illum], d, base_side_limit(), &images[image_count], requests,
							&request_count, &image_resource_count, image_resources, H3_SPARTAN_MAXIMUM_BITMAPS))
					{
						digit_images[d] = (short)image_count++;
					}
				}
				digit_material = before;
			}
			/* (no base map to be had, a tag's one-texel default: not drawn - but a
			material of its illumination alone, by that map) */
			if (part_materials[i][j] != NONE && material_images[part_materials[i][j]][_material_base] == NONE &&
				!(spartan->materials[part_materials[i][j]].illum_only && material_images[part_materials[i][j]][_material_illum] != NONE))
			{
				part_materials[i][j] = NONE;
			}
		}
	}
	/* (a pack on a level's bones is only drawn: only the preview's has an idle; each
reads it, for its fingers: fingers_held) */
	if (biped)
	{
		idle_plan(cache, look_model(look), "combat:rifle:idle", &idle, requests, &request_count);
		if (!skeleton->nodes || own)
			idle_plan(cache, look_model(look), "combat:rifle:aim_still_up", &aim, requests, &request_count);
	}
	times[0] = vita_host_time_us();
	if (read_stopped(control, why, why_size) || !h3_cache_pages_read(cache, requests, request_count, why, why_size, control))
		goto done;
	times[1] = vita_host_time_us();
	for (i = 0; i < request_count; i++)
		spartan->source_bytes += requests[i].size;
	/* (HALO_H2_DEBUG: the idle's bytes hashed, beside h3_animation.c's read
	of the same animation for the biped's graph: the two must agree) */
	if (idle.data && getenv("HALO_H2_DEBUG"))
	{
		unsigned long k, hash = 2166136261UL;

		for (k = 0; k < idle.size; k++)
			hash = (hash ^ idle.data[k]) * 16777619UL;
		platform_log("h3 spartan: idle check: combat:rifle:idle %d frames %ld nodes, %lu bytes (static %lu animated %lu flags %lu+%lu) hash %08lx",
			(int)idle.frames, idle.jmad_nodes, idle.size, idle.static_size, idle.animated_size, idle.static_flags, idle.animated_flags, hash);
	}
	spartan->hand_node = NONE;
	if (idle.data && (!skeleton->nodes || own))
		idle_make(cache, spartan, skeleton, own, nodes, node_count, &idle, &aim);
	if (idle.data && !own)
		fingers_held(cache, skeleton, nodes, node_count, &idle, maps);
	/* (its own idle's hands: where the weapon goes in them) */
	if (biped && (!skeleton->nodes || own))
		hand_read(cache, spartan, mode.address, own, maps, node_count);
	for (i = 0; i < wanted; i++)
	{
		long part_count;
		unsigned long parts;
		unsigned char mesh[MODE_MESH_SIZE];

		if (read_stopped(control, why, why_size))
			goto done;
		if (!h3_cache_read(cache, mesh_addresses[i], MODE_MESH_SIZE, mesh) ||
			!h3_cache_block(cache, mesh_addresses[i], MODE_PART_SIZE, &part_count, &parts) ||
			!mesh_add(spartan, &buffers[i], bounds, (short)(signed char)mesh[0x2D], maps, (short)node_count, cache, parts, part_count, part_materials[i],
				mesh_slots[i], mesh_choices[i]))
		{
			why_set(why, why_size, "a mesh of the Spartan cannot be made%s", NULL);
			goto done;
		}
	}
	empty_parts_drop(spartan);
	spartan->slot_count = slot_count;
	slot_choices[0] = 1;
	memcpy(spartan->slot_choices, slot_choices, sizeof(short) * (unsigned long)slot_count);
	if (!spartan->part_count)
	{
		why_set(why, why_size, "the look has no triangles%s", NULL);
		goto done;
	}
	times[2] = vita_host_time_us();
	/* the bitmaps */
	{
		short made_base[H3_SPARTAN_MAXIMUM_BITMAPS], made_bump[H3_SPARTAN_MAXIMUM_BITMAPS];
		short effective_illum[H3_SPARTAN_MAXIMUM_MATERIALS];

		for (i = 0; i < image_count; i++)
			made_base[i] = made_bump[i] = NONE;
		for (i = 0; i < H3_SPARTAN_MAXIMUM_MATERIALS; i++)
			effective_illum[i] = NONE;
		for (i = 0; i < spartan->material_count; i++)
		{
			struct h3_spartan_material *m = &spartan->materials[i];
			short const *wanted_images = material_images[i];
			struct image_wanted const *base = wanted_images[_material_base] != NONE ? &images[wanted_images[_material_base]] : NULL;
			struct image_wanted const *cc = wanted_images[_material_change_colour] != NONE ? &images[wanted_images[_material_change_colour]] : NULL;
			struct image_wanted const *illum = wanted_images[_material_illum] != NONE ? &images[wanted_images[_material_illum]] : NULL;
			short image, illum_image = wanted_images[_material_illum];
			boolean illum_borrowed = FALSE;

			if (read_stopped(control, why, why_size))
				goto done;
			if (!base && m->illum_only && illum)
			{
				/* (its illumination map times its colour, added) */
				/* (a colour of nothing - a vehicle's boost glow, lit by a function
				of its own: not drawn) */
				m->base_bitmap = m->illum_color[0] + m->illum_color[1] + m->illum_color[2] > 0.01f ?
					illum_bitmap_make(spartan, illum, m, m->name) : NONE;
				platform_log("h3 spartan: material %s: its self-illumination alone (bitmap %d, colour %.2f %.2f %.2f%s%s)", m->name, m->base_bitmap,
					m->illum_color[0], m->illum_color[1], m->illum_color[2], m->specular_type ? ", by its alpha" : "",
					m->digit ? ", a counter's digit" : "");
				m->specular_type = 0;
				m->illuminated = FALSE;
				continue;
			}
			if (!base)
				continue;
			image = wanted_images[_material_base];
			if (made_base[image] == NONE)
			{
				char const *path = h3_cache_tag_name(cache, base->tag);
				char const *name = strrchr(path, '\\');

				made_base[image] = base_bitmap_make(spartan, base, name ? name + 1 : path);
			}
			m->base_bitmap = made_base[image];
			/* (a sandbox read: a material with no illumination map of its own
			takes the one another material of the same base and change colour
			maps has - a weapon's rubber and its lit body share one texture
			sheet -, so the two share one multipurpose map as they did while
			the lights' uncompressed map went unread: a 512 map each was 20 MB
			more on Valhalla, past the texture cache. It glows nothing: its
			colour none) */
			if (!illum && model_read_sandbox && (cc || m->specular_strength > 0.0f))
			{
				long other;

				for (other = 0; other < spartan->material_count && !illum; other++)
				{
					if (other != i && !spartan->materials[other].illum_only && material_images[other][_material_illum] != NONE &&
						material_images[other][_material_base] == wanted_images[_material_base] &&
						material_images[other][_material_change_colour] == wanted_images[_material_change_colour])
					{
						illum_image = material_images[other][_material_illum];
						illum = &images[illum_image];
						illum_borrowed = TRUE;
					}
				}
			}
			effective_illum[i] = illum_image;
			if (cc || illum || m->specular_strength > 0.0f)
			{
				/* (one made from the same maps is the same map: a visor's and its
				helmet's) */
				long other;

				for (other = 0; other < i; other++)
				{
					if (spartan->materials[other].multipurpose_bitmap != NONE && material_images[other][_material_base] == wanted_images[_material_base] &&
						material_images[other][_material_change_colour] == wanted_images[_material_change_colour] &&
						effective_illum[other] == illum_image)
					{
						m->multipurpose_bitmap = spartan->materials[other].multipurpose_bitmap;
						break;
					}
				}
				if (other == i)
					multipurpose_make(spartan, m->name, base, cc, illum, &m->multipurpose_bitmap);
			}
			m->change_colour = cc && m->multipurpose_bitmap != NONE;
			m->illuminated = illum && !illum_borrowed && m->multipurpose_bitmap != NONE;
			if (illum_borrowed)
				m->illum_color[0] = m->illum_color[1] = m->illum_color[2] = 0.0f;
			image = wanted_images[_material_detail];
			if (image != NONE)
			{
				if (made_base[image] == NONE)
				{
					char const *path = h3_cache_tag_name(cache, images[image].tag);
					char const *name = strrchr(path, '\\');

					made_base[image] = base_bitmap_make(spartan, &images[image], name ? name + 1 : path);
				}
				m->detail_bitmap = made_base[image];
			}
			image = wanted_images[_material_bump];
			if (image != NONE)
			{
				if (made_bump[image] == NONE)
				{
					char const *path = h3_cache_tag_name(cache, images[image].tag);
					char const *name = strrchr(path, '\\');

					made_bump[image] = bump_make(spartan, name ? name + 1 : path, &images[image]);
				}
				m->bump_bitmap = made_bump[image];
			}
			platform_log("h3 spartan: material %s: base %d multipurpose %d detail %d bump %d specular %.2f type %d reflection %.2f%s%s", m->name,
				m->base_bitmap, m->multipurpose_bitmap, m->detail_bitmap, m->bump_bitmap, m->specular_strength, m->specular_type, m->reflection,
				m->visor ? " visor" : "", m->additive ? " additive" : "");
		}
	}
	/* (a counter's digits' ten bitmaps) */
	if (digit_material != NONE)
	{
		for (i = 0; i < 10; i++)
		{
			if (digit_images[i] != NONE)
			{
				char name[32];

				snprintf(name, sizeof(name), "digit %ld", (long)i);
				spartan->digit_bitmaps[i] = illum_bitmap_make(spartan, &images[digit_images[i]], &spartan->materials[digit_material], name);
			}
		}
	}
	ok = TRUE;
	platform_log("h3 spartan: %ld triangles, %ld vertices, %d materials, %d bitmaps, %lu KB read; ms: plan %.0f, pages %.0f, meshes %.0f, bitmaps %.0f",
		spartan->triangles, spartan->vertices, spartan->material_count, spartan->bitmap_count, spartan->source_bytes / 1024,
		(double)(times[0] - started) / 1000.0, (double)(times[1] - times[0]) / 1000.0, (double)(times[2] - times[1]) / 1000.0,
		(double)(vita_host_time_us() - times[2]) / 1000.0);

done:
	if (!ok && why && why_size > 0 && !why[0])
		why_set(why, why_size, "Halo 3's Spartan cannot be read%s", NULL);
	for (i = 0; i < MAXIMUM_MESHES; i++)
	{
		free(buffers[i].vertices);
		free(buffers[i].indices);
		free(part_materials[i]);
	}
	for (i = 0; i < image_count; i++)
		image_free(&images[i]);
	for (i = 0; i < image_resource_count; i++)
		h3_resource_free(&image_resources[i]);
	free(image_resources);
	free(images);
	free(requests);
	free(idle.data);
	free(aim.data);
	h3_resource_free(&idle.resource);
	h3_resource_free(&aim.resource);
	h3_resource_free(&geometry);
	if (!ok && spartan)
	{
		h3_spartan_free(spartan);
		spartan = NULL;
	}
	return spartan;
}

H3_ALIGN struct h3_spartan *h3_spartan_read(struct h3_spartan_skeleton const *skeleton, char const *look,
	struct h3_cache_read_control const *control, char *why, long why_size)
{
	char directory[1100];
	char const *mode_tag = look_model(look) == H3_MODEL_ELITE ? ELITE_MODE_TAG : SPARTAN_MODE_TAG;
	struct h3_cache *cache = spartan_cache(why, why_size, directory, sizeof(directory), mode_tag);
	struct h3_spartan *spartan;

	if (!cache)
		return NULL;
	spartan = model_read(cache, h3_cache_find_tag(cache, 'mode', mode_tag), skeleton, look, TRUE, control, why, why_size);
	h3_cache_close(cache);
	return spartan;
}

H3_ALIGN struct h3_spartan *h3_model_read(struct h3_cache *cache, long mode_tag, char const *look,
	struct h3_cache_read_control const *control, char *why, long why_size)
{
	struct h3_spartan_skeleton *skeleton = calloc(1, sizeof(*skeleton));
	struct h3_cache_tag mode;
	struct h3_spartan *spartan;
	long node_count = 0, i;
	unsigned long nodes = 0;

	if (!skeleton)
	{
		why_set(why, why_size, "out of memory%s", NULL);
		return NULL;
	}
	/* its own skeleton: each node's name, parent and inverse as the model
	holds them (every node its own: nothing moved, every bone kept) */
	if (mode_tag == NONE || !h3_cache_tag_get(cache, mode_tag, &mode) || mode.group_tag != 'mode' ||
		!h3_cache_block(cache, mode.address + MODE_NODES, MODE_NODE_SIZE, &node_count, &nodes) || node_count < 1 ||
		node_count > H3_SPARTAN_MAXIMUM_NODES)
	{
		why_set(why, why_size, "Halo 3's model is not as expected%s", NULL);
		free(skeleton);
		return NULL;
	}
	skeleton->node_count = (short)node_count;
	for (i = 0; i < node_count; i++)
	{
		unsigned char node[MODE_NODE_SIZE];
		int f;

		if (!h3_cache_read(cache, nodes + MODE_NODE_SIZE * (unsigned long)i, MODE_NODE_SIZE, node))
		{
			why_set(why, why_size, "Halo 3's model's nodes cannot be read%s", NULL);
			free(skeleton);
			return NULL;
		}
		snprintf(skeleton->names[i], sizeof(skeleton->names[i]), "%s", h3_cache_string(cache, read32(node)));
		skeleton->parents[i] = read16(node + 4);
		for (f = 0; f < 13; f++)
			skeleton->inverse[i][f] = readf(node + 0x28 + 4 * f);
	}
	model_read_sandbox = TRUE;
	spartan = model_read(cache, mode_tag, skeleton, look, FALSE, control, why, why_size);
	model_read_sandbox = FALSE;
	free(skeleton);
	return spartan;
}

H3_ALIGN boolean h3_spartan_own_skeleton(char const *look, struct h3_spartan_skeleton *skeleton)
{
	/* (this engine's model node: scene_pack_blocks.c checks its size) */
	enum
	{
		NODE_SIZE = H3_SPARTAN_MODEL_NODE_SIZE,
		NODE_NEXT_SIBLING = 0x20,
		NODE_FIRST_CHILD = 0x22,
		NODE_PARENT = 0x24,
		NODE_TRANSLATION = 0x28,
		NODE_ROTATION = 0x34,
		NODE_DISTANCE = 0x44,
		NODE_INVERSE = 0x68,
	};
	char directory[1100], why[H3_SPARTAN_WHY_SIZE];
	char const *mode_tag = look_model(look) == H3_MODEL_ELITE ? ELITE_MODE_TAG : SPARTAN_MODE_TAG;
	struct h3_cache *cache = spartan_cache(why, sizeof(why), directory, sizeof(directory), mode_tag);
	struct h3_cache_tag mode;
	unsigned char *out = NULL;
	long count = 0, i;
	unsigned long nodes = 0;
	boolean ok = FALSE;

	if (!cache)
		return FALSE;
	if (h3_cache_tag_get(cache, h3_cache_find_tag(cache, 'mode', mode_tag), &mode) &&
		h3_cache_block(cache, mode.address + MODE_NODES, MODE_NODE_SIZE, &count, &nodes) && count > 0 && count <= H3_SPARTAN_MAXIMUM_NODES &&
		(out = calloc((unsigned long)count, NODE_SIZE)) != NULL)
	{
		short children[H3_SPARTAN_MAXIMUM_NODES], siblings[H3_SPARTAN_MAXIMUM_NODES];

		ok = TRUE;
		for (i = 0; i < count; i++)
			children[i] = siblings[i] = NONE;
		for (i = count - 1; i >= 0 && ok; i--)
		{
			unsigned char const *node = h3_cache_at(cache, nodes + MODE_NODE_SIZE * (unsigned long)i, MODE_NODE_SIZE);
			unsigned char *o = out + (unsigned long)i * NODE_SIZE;
			float value, length = 0.0f;
			int c;

			if (!node || read16(node + 4) >= count)
			{
				ok = FALSE;
				break;
			}
			snprintf(skeleton->names[i], sizeof(skeleton->names[i]), "%.31s", h3_cache_string(cache, read32(node)));
			memcpy(o, skeleton->names[i], sizeof(skeleton->names[i]));
			skeleton->parents[i] = read16(node + 4);
			if (skeleton->parents[i] >= 0)
			{
				siblings[i] = children[skeleton->parents[i]];
				children[skeleton->parents[i]] = (short)i;
			}
			memcpy(o + NODE_PARENT, &skeleton->parents[i], 2);
			memcpy(o + NODE_NEXT_SIBLING, &siblings[i], 2);
			for (c = 0; c < 3; c++)
			{
				value = readf(node + 0xC + 4 * c) * OWN_SCALE;
				memcpy(o + NODE_TRANSLATION + 4 * c, &value, 4);
				length += value * value;
			}
			length = (float)sqrt(length);
			memcpy(o + NODE_DISTANCE, &length, 4);
			for (c = 0; c < 4; c++)
			{
				value = c < 3 ? -readf(node + 0x18 + 4 * c) : readf(node + 0x18 + 4 * c);
				memcpy(o + NODE_ROTATION + 4 * c, &value, 4);
			}
			for (c = 0; c < 13; c++)
			{
				skeleton->inverse[i][c] = readf(node + 0x28 + 4 * c);
				value = skeleton->inverse[i][c] * (c >= 10 ? OWN_SCALE : 1.0f);
				memcpy(o + NODE_INVERSE + 4 * c, &value, 4);
			}
		}
		for (i = 0; i < count && ok; i++)
			memcpy(out + (unsigned long)i * NODE_SIZE + NODE_FIRST_CHILD, &children[i], 2);
		/* (each's next sibling was written before its parent's list went on) */
	}
	if (ok)
	{
		skeleton->node_count = (short)count;
		skeleton->node_size = NODE_SIZE;
		skeleton->nodes = out;
	}
	else
		free(out);
	h3_cache_close(cache);
	return ok;
}

H3_ALIGN void h3_spartan_nodes_free(unsigned char *nodes)
{
	free(nodes);
}

H3_ALIGN void h3_spartan_free(struct h3_spartan *spartan)
{
	short i;

	if (!spartan)
		return;
	for (i = 0; i < spartan->part_count; i++)
	{
		free(spartan->parts[i].vertices);
		free(spartan->parts[i].triangles);
	}
	for (i = 0; i < spartan->bitmap_count; i++)
		free(spartan->bitmaps[i].pixels);
	free(spartan->idle_poses);
	free(spartan);
}

H3_ALIGN boolean h3_model_catalog_read(short model, struct h3_model_catalog *catalog)
{
	char directory[1100], why[H3_SPARTAN_WHY_SIZE];
	char const *mode_tag = model == H3_MODEL_ELITE ? ELITE_MODE_TAG : SPARTAN_MODE_TAG;
	struct h3_cache *cache = spartan_cache(why, sizeof(why), directory, sizeof(directory), mode_tag);
	struct h3_cache_tag mode;
	long region_count = 0, i, j, count;
	unsigned long regions, permutations, hash = 2166136261UL;

	memset(catalog, 0, sizeof(*catalog));
	if (!cache)
		return FALSE;
	if (h3_cache_tag_get(cache, h3_cache_find_tag(cache, 'mode', mode_tag), &mode) &&
		h3_cache_block(cache, mode.address + MODE_REGIONS, MODE_REGION_SIZE, &region_count, &regions))
	{
		for (i = 0; i < region_count && catalog->region_count < H3_CATALOG_MAXIMUM_REGIONS; i++)
		{
			unsigned long region = regions + MODE_REGION_SIZE * (unsigned long)i;
			short out = catalog->region_count;
			char const *text = h3_cache_string(cache, h3_cache_read32(cache, region));
			unsigned long k;

			if (!catalog_name_valid(text) || strlen(text) >= H3_CATALOG_NAME_SIZE ||
				!h3_cache_block(cache, region + 4, MODE_PERMUTATION_SIZE, &count, &permutations))
			{
				continue;
			}
			snprintf(catalog->regions[out].name, H3_CATALOG_NAME_SIZE, "%s", text);
			for (j = 0; j < count && catalog->regions[out].permutation_count < H3_CATALOG_MAXIMUM_PERMUTATIONS; j++)
			{
				unsigned char const *p = h3_cache_at(cache, permutations + MODE_PERMUTATION_SIZE * (unsigned long)j, MODE_PERMUTATION_SIZE);

				if (!p)
					continue;
				text = h3_cache_string(cache, read32(p));
				if (!catalog_offers(text, read16(p + 4)))
					continue;
				snprintf(catalog->regions[out].permutations[catalog->regions[out].permutation_count++], H3_CATALOG_NAME_SIZE, "%s", text);
				for (k = 0; text[k]; k++)
					hash = ((hash ^ (unsigned char)text[k]) * 16777619UL) & 0xFFFFFFFFUL;
				hash = ((hash ^ '/') * 16777619UL) & 0xFFFFFFFFUL;
			}
			catalog->region_count++;
		}
	}
	catalog->identity = hash;
	catalog->read = catalog->region_count > 0;
	h3_cache_close(cache);
	return catalog->read;
}

H3_ALIGN void h3_spartan_log_permutations(void)
{
	char directory[1100], why[H3_SPARTAN_WHY_SIZE];
	struct h3_cache *cache = spartan_cache(why, sizeof(why), directory, sizeof(directory), SPARTAN_MODE_TAG);
	struct h3_cache_tag mode;
	long region_count, i, j, count;
	unsigned long regions, permutations;

	if (!cache)
	{
		platform_log("h3 spartan: %s", why);
		return;
	}
	if (h3_cache_tag_get(cache, h3_cache_find_tag(cache, 'mode', SPARTAN_MODE_TAG), &mode) &&
		h3_cache_block(cache, mode.address + MODE_REGIONS, MODE_REGION_SIZE, &region_count, &regions))
	{
		for (i = 0; i < region_count; i++)
		{
			unsigned long region = regions + MODE_REGION_SIZE * (unsigned long)i;

			if (!h3_cache_block(cache, region + 4, MODE_PERMUTATION_SIZE, &count, &permutations))
				continue;
			for (j = 0; j < count; j++)
			{
				platform_log("h3 spartan: %s: %s", h3_cache_string(cache, h3_cache_read32(cache, region)),
					h3_cache_string(cache, h3_cache_read32(cache, permutations + MODE_PERMUTATION_SIZE * (unsigned long)j)));
			}
		}
	}
	h3_cache_close(cache);
}

/* ---------- public code: bitmaps read whole (h3_hud.c)

A bitmap tag's first image's top level as RGBA, for the HUD's sprite sheets
(ui\chud\bitmaps: DXT1/3/5 and A8R8G8B8, sides of any size): each tag's pages
asked for in one pass (h3_cache_pages_read). pixels[i] is NULL where a tag
could not be read; the caller frees with h3_bitmap_free. */

H3_ALIGN void h3_bitmaps_read_rgba(struct h3_cache *cache, long const *tags, long count, unsigned char **pixels, unsigned long *widths,
	unsigned long *heights)
{
	struct h3_page_request *requests = calloc((size_t)count + 1, sizeof(*requests));
	unsigned short *formats = calloc((size_t)count + 1, sizeof(*formats));
	long *owner = calloc((size_t)count + 1, sizeof(*owner));
	long i, request_count = 0;
	char why[H3_CACHE_WHY_SIZE];

	for (i = 0; i < count; i++)
		pixels[i] = NULL, widths[i] = heights[i] = 0;
	if (!requests || !formats || !owner)
		goto done;
	for (i = 0; i < count; i++)
	{
		struct h3_cache_tag t;
		struct h3_resource r;
		long image_count, resources_count;
		unsigned long images, resource_list, w, h, bytes;
		unsigned char data[0x30];
		unsigned short format;
		int which;

		if (!h3_cache_tag_get(cache, tags[i], &t) || t.group_tag != 'bitm' ||
			!h3_cache_block(cache, t.address + 0x60, 0x30, &image_count, &images) || image_count < 1 ||
			!h3_cache_read(cache, images, 0x30, data) ||
			!h3_cache_block(cache, t.address + 0x8C, 8, &resources_count, &resource_list) || resources_count < 1)
		{
			continue;
		}
		w = (unsigned short)read16(data + 4);
		h = (unsigned short)read16(data + 6);
		format = (unsigned short)read16(data + 0xC);
		bytes = format == 10 || format == 11 ? w * h * 4 : mip_bytes(format, w, h);
		if (!bytes || w < 1 || h < 1 || w > 4096 || h > 4096 || format == H3_FORMAT_V8U8 || format == H3_FORMAT_DXN ||
			!h3_cache_resource_get(cache, (long)(h3_cache_read32(cache, resource_list) & 0xFFFF), &r))
		{
			continue;
		}
		/* (the top level: the optional page's first bytes when it has one that
		holds it, else the required page's) */
		which = r.page[1] != NONE && h3_resource_read32(&r, 0x14) >= bytes && r.size[1] >= bytes ? 1 : 0;
		if (r.page[which] != NONE && r.size[which] >= bytes)
		{
			requests[request_count].page = r.page[which];
			requests[request_count].offset = r.offset[which];
			requests[request_count].size = bytes;
			requests[request_count].out = malloc(bytes);
			if (requests[request_count].out)
			{
				owner[request_count] = i;
				formats[i] = format;
				widths[i] = w;
				heights[i] = h;
				request_count++;
			}
		}
		h3_resource_free(&r);
	}
	why[0] = 0;
	if (request_count && !h3_cache_pages_read(cache, requests, request_count, why, sizeof(why), NULL))
		platform_log("h3 bitmaps: %s", why);
	for (i = 0; i < request_count; i++)
	{
		long k = owner[i];
		unsigned long w = widths[k], h = heights[k], n;

		if (requests[i].got && requests[i].out)
		{
			if (formats[k] == 10 || formats[k] == 11)
			{
				unsigned char *out = malloc(w * h * 4);

				for (n = 0; out && n < w * h; n++)
				{
					unsigned char const *p = requests[i].out + 4 * n;

					out[4 * n] = p[2], out[4 * n + 1] = p[1], out[4 * n + 2] = p[0], out[4 * n + 3] = formats[k] == 10 ? 255 : p[3];
				}
				pixels[k] = out;
			}
			else
				pixels[k] = dxt_rgba(requests[i].out, requests[i].size, w, h, formats[k]);
		}
		free(requests[i].out);
		if (!pixels[k])
			widths[k] = heights[k] = 0;
	}
done:
	free(requests);
	free(formats);
	free(owner);
}

H3_ALIGN void h3_bitmap_free(unsigned char *pixels)
{
	free(pixels);
}
