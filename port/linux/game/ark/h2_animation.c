/*
H2_ANIMATION.C

Halo 2's animation graphs made this engine's (h2_animation.h says how
Halo 2 keeps them and how they are made).

The graph's tables (which Halo 2 animation goes where this engine's units
look for one, by name):
- each Halo 2 mode is a unit seat: "combat" is "stand", "crouch" itself,
  the vehicles' modes their own names and the stock vehicles' seat labels
  (stand-ins: Halo 2's warthog_d as the Warthog's "W-driver"...);
- each weapon class of a mode (but "any") a weapon class of the same name,
  its "any" type's actions and overlays its animations (seat_names and
  class_names below), each other type a weapon type of the same name with
  the stock weapons' labels beside them ("ar" the SMG's...);
- the "any" class's actions and overlays the seat's (airborne-dead...);
- "any any any"'s deaths and damage the graph's unit damage (Halo 2's
  directions and parts are this engine's, in the same order).
An animation a slot wants as a base animation that Halo 2 has as a
replacement or an overlay (a throw, a melee) is baked over the class's
idle's first frame; a replacement wanted from an overlay the same way.
Variants (Halo 2's next variant sibling) are this engine's permutations.
*/

/* ---------- headers */

#include "cseries.h"
#include "h2_animation.h"
#include "h2_biped.h"
#include "h2_cache.h"
#include "h2_caps.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef malloc
#undef free
#undef realloc

/* (the Vita tier: what a level's build allocates is the builder's own
heap's, not the C heap's - h2_build_heap.h) */
#define H2_BUILD_HEAP_WRAP
#include "h2_build_heap.h"
#ifdef HALO_LINUX
#include "render_epoch.h"
#endif

#define H2_ALIGN __attribute__((force_align_arg_pointer))

void platform_log(char const *format, ...);

/* ---------- constants */

/* Halo 2's jmad (MCC): where its blocks and an animation entry's fields are
(h2_animation.h's struct h2_graph_layout; Halo 3's is h3_animation.c's) */
static struct h2_graph_layout const h2_layout =
{
	0x0C, 0x20,             /* nodes */
	0x24, 0x1C,             /* blend screens */
	0x2C, 0x60,             /* animations */
	0x34, 0x14,             /* modes */
	0x14,                   /* a class */
	0x34, 0x0C, 0x14,       /* a type: its overlays, its deaths and damage */
	0x0C, 0x08,             /* a damage entry, a direction */
	0x4C, 0x20,             /* inheritance */
	/* an animation entry: type, frame info type, blend screen, frames, weight,
	loop frame, next variant, frame events block */
	0x10, 0x11, 0x12, 0x14, 0x1C, 0x20, 0x24, 0x40,
	FALSE,
};

/* a graph's layout (its source's) */
#define LAYOUT(graph) ((graph)->source.layout)

enum
{
	MAXIMUM_INHERITED = 4,
	MAXIMUM_ANIMATION_BYTES = 1024 * 1024,
	MAXIMUM_FRAMES = 2048,

	/* this engine's */
	CE_ANIMATION_SIZE = 0xB4,
	CE_NODE_SIZE = 0x40,
	CE_SEAT_SIZE = 0x64,
	CE_CLASS_SIZE = 0xBC,
	CE_TYPE_SIZE = 0x3C,
	CE_IK_SIZE = 0x40,
	CE_SEAT_ANIMATIONS = 30,
	/* (this engine's 55 and Halo 2's 8 gestures after them: units.h) */
	CE_CLASS_ANIMATIONS = 63,
	/* (this engine's ten, then Halo 2's overlays units.c plays from the
	slots after them: units.h's _unit_weapon_type_animation_h2_*) */
	CE_TYPE_ANIMATIONS = 25,
	/* (the type's own class actions, 20..24: filled from the type alone,
	never from the class's "any" - NONE: the class's own) */
	TYPE_OWN_FIRST = 20,
	CE_DAMAGE_ANIMATIONS = 4 * 4 * 11,
	/* (the arrays' sizes: the most of any tier; each tier's own limits are
	h2_caps.h's, below as SEATS_LIMIT... - the Vita's 32 seats, 12 classes,
	16 types and 256 animations, the PC's more) */
	MAXIMUM_SEATS = H2_CAPS_MAXIMUM_GRAPH_SEATS,
	MAXIMUM_CLASSES = H2_CAPS_MAXIMUM_SEAT_CLASSES,
	MAXIMUM_TYPES = H2_CAPS_MAXIMUM_CLASS_TYPES,
	/* (mode_labels' first: combat, crouch, the stock vehicles' seats) */
	ESSENTIAL_MODES = 8,

	/* (a slot left out on purpose: slots_fill fills NONE slots alone) */
	SLOT_SKIPPED = -2,

	ANIMATION_BASE = 0,
	ANIMATION_OVERLAY = 1,
	ANIMATION_REPLACEMENT = 2,
};

/* each tier's limits (h2_caps.h): the animations a graph (the Vita's 256,
the Xbox graph's most), an animation's variants made (Halo 2's next variant
siblings: the Vita's two), the seats, a seat's classes, a class's types */
#define MAXIMUM_CE_ANIMATIONS h2_caps_graph_animations()
#define MAXIMUM_VARIANTS h2_caps_animation_variants()
#define SEATS_LIMIT h2_caps_graph_seats()
#define CLASSES_LIMIT h2_caps_seat_classes()
#define TYPES_LIMIT h2_caps_class_types()

/* how a Halo 2 animation is made for a slot */
enum
{
	_make_as_is = 0,        /* its own type */
	_make_base,             /* a base animation (baked over the idle if it is not one) */
	_make_replacement,      /* a replacement (baked over the idle if an overlay) */
	_make_overlay,          /* an overlay */
};

/* ---------- structures */

/* a Halo 2 animation graph read (one of the graph and the graphs it
inherits: each one's nodes mapped to the model's) */
struct jmad
{
	long tag;
	unsigned long address;
	long node_count;
	short node_to_model[H2_ANIMATION_MAXIMUM_NODES];
	long animation_count;
	unsigned long animations;
};

/* a slot's animation decided: Halo 2's (its graph and index) made a way */
struct source
{
	short graph;            /* 0: the graph, 1..: its inherited ones */
	short index;
	short make;
	short bake_graph, bake_index;   /* (baked) the idle under it */
	short ce_index;         /* this engine's, once made */
};

struct ce_animation
{
	char name[32];
	short type;
	short frame_count;
	short frame_info_type;
	short loop_frame;
	short key_frame;
	short second_key_frame;
	short left_foot, right_foot;
	short next;
	float weight;
	float normalized_weight;
	unsigned long flags[6];     /* rotation, translation, scale: 2 words each */
	unsigned char *data;
	long data_size;
	unsigned char *frame_info;
	long frame_info_size;
	short frame_size;
};

struct ce_type
{
	char label[32];
	short animations[CE_TYPE_ANIMATIONS];
};

struct ce_class
{
	char label[32];
	unsigned char aiming[24];
	short animations[CE_CLASS_ANIMATIONS];
	short type_count;
	struct ce_type types[MAXIMUM_TYPES];
};

struct ce_seat
{
	char label[32];
	unsigned char looking[24];
	short animations[CE_SEAT_ANIMATIONS];
	short class_count;
	struct ce_class *classes;
	boolean alias;          /* (the same as the seat before it, another label) */
	short alias_of;
	unsigned long mode;     /* Halo 2's mode */
	short mode_label;       /* (mode_labels') */
};

struct h2_graph
{
	struct h2_cache *cache;     /* (a Halo 2 source's: its context) */
	struct h2_graph_source source;
	short graph_count;
	struct jmad graphs[1 + MAXIMUM_INHERITED];
	short node_count;
	char (*node_names)[32];
	struct h2_animation_pose *node_defaults;
	short *parents;
	short *first_children;
	short *next_siblings;
	unsigned long node_list_checksum;
	/* (its biped flies: its standing seat is Halo 2's flight mode) */
	int flies;

	struct source *sources;
	long source_count, source_capacity;
	/* (source_make) the sources made, by a hash of what they were made from:
	each slot the index of one in sources, NONE empty; a walk over them all
	at each make was a fifth of a direct level's bipeds' gather */
	long *source_hash;
	unsigned long source_hash_mask;
	struct ce_animation *animations;
	long animation_count;

	struct ce_seat seats[MAXIMUM_SEATS];
	short seat_count;
	short damage[CE_DAMAGE_ANIMATIONS];

	struct h2_graph_report report;
	char why[160];
};

/* a decoded animation: poses frame by frame, node by node (the graph's
nodes mapped to the model's), Halo 2's convention */
struct decoded
{
	short frame_count;
	short node_count;
	short type;
	short frame_info_type;
	struct h2_animation_pose *poses;
	unsigned char *sets;
	float *frame_info;          /* 4 a frame: dx dy dz dyaw */
	short loop_frame;
	short key_frame, second_key_frame, left_foot, right_foot;
	float weight;
	short next_variant;
	unsigned long name_id;
	/* (a window of the frames: poses holds window_count frames from
	window_first; 0 and 0: all of them) */
	short window_first, window_count;
};

/* ---------- private code: the source's reads */

H2_ALIGN static boolean src_block(struct h2_graph const *graph, unsigned long address, unsigned long element_size, long *count,
	unsigned long *elements)
{
	return graph->source.block(graph->source.context, address, element_size, count, elements);
}

H2_ALIGN static void const *src_at(struct h2_graph const *graph, unsigned long address, unsigned long size)
{
	return graph->source.at(graph->source.context, address, size);
}

H2_ALIGN static unsigned long src_read32(struct h2_graph const *graph, unsigned long address)
{
	unsigned char const *p = src_at(graph, address, 4);

	return p ? (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24) : 0;
}

H2_ALIGN static long src_reference(struct h2_graph const *graph, unsigned long address)
{
	return graph->source.reference(graph->source.context, address);
}

H2_ALIGN static char const *src_tag_name(struct h2_graph const *graph, long tag)
{
	char const *name = graph->source.tag_name(graph->source.context, tag);

	return name ? name : "";
}

H2_ALIGN static boolean src_tag_get(struct h2_graph const *graph, long tag, struct h2_cache_tag *out)
{
	return (graph->source.tag_get)(graph->source.context, tag, &out->group_tag, &out->address);
}

/* Halo 2's own source: an h2_cache */
H2_ALIGN static boolean h2_source_tag_get(void *context, long tag, unsigned long *group_tag, unsigned long *address)
{
	struct h2_cache_tag t;

	if (!h2_cache_tag_get(context, tag, &t))
		return FALSE;
	*group_tag = t.group_tag;
	*address = t.address;
	return TRUE;
}

H2_ALIGN static char const *h2_source_tag_name(void *context, long tag)
{
	return h2_cache_tag_name(context, tag);
}

H2_ALIGN static boolean h2_source_block(void *context, unsigned long address, unsigned long element_size, long *count, unsigned long *elements)
{
	return h2_cache_block(context, address, element_size, count, elements);
}

H2_ALIGN static void const *h2_source_at(void *context, unsigned long address, unsigned long size)
{
	return h2_cache_at(context, address, size);
}

H2_ALIGN static long h2_source_reference(void *context, unsigned long address)
{
	return h2_cache_reference(context, address);
}

H2_ALIGN static boolean h2_source_string(void *context, unsigned long id, char *out, long size)
{
	return h2_cache_string(context, id, out, size);
}

/* the data of an animation entry (its data field, 0x28: size and address;
the flags' sizes at 0x30 and 0x31, the movement's at 0x32, the static
block's at 0x36, the animated block's at 0x3C) */
static unsigned long read32(void const *p);
static short read16(void const *p);
static char *string(struct h2_graph *graph, unsigned long id, char *out, long size);
H2_ALIGN static boolean h2_source_animation_data(void *context, unsigned long entry, struct h2_animation_data *out)
{
	unsigned char const *e = h2_cache_at(context, entry, 0x60);
	unsigned long size, address;

	memset(out, 0, sizeof(*out));
	if (!e)
		return FALSE;
	size = read32(e + 0x28);
	address = read32(e + 0x2C);
	out->static_flags = e[0x30];
	out->animated_flags = e[0x31];
	out->movement = (unsigned short)read16(e + 0x32);
	out->static_size = (unsigned short)read16(e + 0x36);
	out->animated_size = read32(e + 0x3C);
	if (size > MAXIMUM_ANIMATION_BYTES ||
		out->static_size + out->animated_size + out->static_flags + out->animated_flags + out->movement > size)
	{
		return FALSE;
	}
	out->bytes = h2_cache_at(context, address, size);
	return out->bytes != NULL;
}

H2_ALIGN static void h2_source_make(struct h2_graph_source *source, struct h2_cache *cache)
{
	memset(source, 0, sizeof(*source));
	source->context = cache;
	source->layout = &h2_layout;
	source->tag_get = h2_source_tag_get;
	source->tag_name = h2_source_tag_name;
	source->block = h2_source_block;
	source->at = h2_source_at;
	source->reference = h2_source_reference;
	source->string = h2_source_string;
	source->animation_data = h2_source_animation_data;
}

/* ---------- private code: bytes and quaternions */

H2_ALIGN static unsigned long read32(void const *p)
{
	unsigned char const *b = p;

	return (unsigned long)b[0] | ((unsigned long)b[1] << 8) | ((unsigned long)b[2] << 16) | ((unsigned long)b[3] << 24);
}

H2_ALIGN static short read16(void const *p)
{
	unsigned char const *b = p;

	return (short)(b[0] | (b[1] << 8));
}

H2_ALIGN static float readf(void const *p)
{
	float f;

	memcpy(&f, p, 4);
	return f;
}

H2_ALIGN static void write32(void *p, unsigned long v)
{
	unsigned char *b = p;

	b[0] = (unsigned char)v;
	b[1] = (unsigned char)(v >> 8);
	b[2] = (unsigned char)(v >> 16);
	b[3] = (unsigned char)(v >> 24);
}

H2_ALIGN static void write16(void *p, long v)
{
	unsigned char *b = p;

	b[0] = (unsigned char)v;
	b[1] = (unsigned char)(v >> 8);
}

H2_ALIGN static void writef(void *p, float f)
{
	memcpy(p, &f, 4);
}

/* a x b (Hamilton) */
H2_ALIGN static void quaternion_multiply(float const *a, float const *b, float *out)
{
	float r[4];

	r[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	r[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	r[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	r[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
	memcpy(out, r, sizeof(r));
}

H2_ALIGN static void quaternion_conjugate(float const *q, float *out)
{
	out[0] = -q[0];
	out[1] = -q[1];
	out[2] = -q[2];
	out[3] = q[3];
}

H2_ALIGN static void quaternion_normalize(float *q)
{
	float m = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);

	if (m > 1e-6f)
	{
		q[0] /= m;
		q[1] /= m;
		q[2] /= m;
		q[3] /= m;
	}
	else
	{
		q[0] = q[1] = q[2] = 0.0f;
		q[3] = 1.0f;
	}
}

/* this engine's quaternions_interpolate_and_normalize */
H2_ALIGN static void quaternion_lerp(float const *a, float const *b, float t, float *out)
{
	float v = 1.0f - t;
	int k;

	if (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3] < 0.0f)
		t = -t;
	for (k = 0; k < 4; k++)
		out[k] = a[k] * v + b[k] * t;
	quaternion_normalize(out);
}

/* (model_animations.c's quaternion_decompress_6byte, the other way: 12 bits
each, i j k w) */
H2_ALIGN static void quaternion_compress_6(float const *q, unsigned char *out)
{
	long c[4];
	unsigned long w0, w1, w2;
	int k;

	for (k = 0; k < 4; k++)
	{
		float v = q[k] < -1.0f ? -1.0f : q[k] > 1.0f ? 1.0f : q[k];

		c[k] = (long)floorf(v * 32767.0f + 0.5f);
		/* (rounded to the 12 bits kept) */
		c[k] = (c[k] + 8) & ~15L;
		if (c[k] > 32752)
			c[k] = 32752;
		if (c[k] < -32768)
			c[k] = -32768;
	}
	w0 = ((unsigned long)c[0] & 0xFFF0UL) | (((unsigned long)c[1] >> 12) & 0xFUL);
	w1 = ((((unsigned long)c[1] >> 4) & 0xFFUL) << 8) | (((unsigned long)c[2] >> 8) & 0xFFUL);
	w2 = ((((unsigned long)c[2] >> 4) & 0xFUL) << 12) | (((unsigned long)c[3] >> 4) & 0xFFFUL);
	write16(out, (long)w0);
	write16(out + 2, (long)w1);
	write16(out + 4, (long)w2);
}

H2_ALIGN static void quaternion_decompress_6(unsigned char const *in, float *q)
{
	unsigned short word0 = (unsigned short)read16(in), word1 = (unsigned short)read16(in + 2), word2 = (unsigned short)read16(in + 4);
	short i = (short)((word0 >> 12) | (word0 & 0xFFF0));
	short j = (short)(((word1 >> 4) & 0x0FF0) | (word0 & 0x000F) | (word0 << 12));
	short k = (short)(((((word2 >> 4) & 0x0F00) | (word1 & 0x00F0)) >> 4) | (word1 << 8));
	short w = (short)(((word2 >> 8) & 0x000F) | (word2 << 4));

	q[0] = i / 32767.0f;
	q[1] = j / 32767.0f;
	q[2] = k / 32767.0f;
	q[3] = w / 32767.0f;
	quaternion_normalize(q);
}

/* ---------- private code: decoding Halo 2's */

H2_ALIGN static void decoded_free(struct decoded *d)
{
	free(d->poses);
	free(d->sets);
	free(d->frame_info);
	memset(d, 0, sizeof(*d));
}

/* the nodes a flag set names, in order (out: up to H2_ANIMATION_MAXIMUM_NODES - a
Prophet's 68) */
H2_ALIGN static long flag_nodes(unsigned char const *flags, long node_count, short *out)
{
	long n = 0, i;

	for (i = 0; i < node_count && i < H2_ANIMATION_MAXIMUM_NODES; i++)
	{
		if (flags[i / 8] & (1 << (i % 8)))
			out[n++] = (short)i;
	}
	return n;
}

/* a codec block at d + base (size bytes there) decoded into the frames of
the nodes the flags (rotation, translation, scale: flag_size bytes each -
8, or 4 for a graph of 32 nodes or fewer: the Grunt's, the Hunter's) name: the
graph's nodes (mapped to out's by map); FALSE if it is not one this reads */
H2_ALIGN static boolean codec_decode(struct decoded *out, unsigned char const *d, unsigned long size,
	unsigned char const *flags, unsigned long flag_size, long graph_nodes, short const *map, long *codec_counts)
{
	short rotated[H2_ANIMATION_MAXIMUM_NODES], translated[H2_ANIMATION_MAXIMUM_NODES], scaled[H2_ANIMATION_MAXIMUM_NODES];
	long nr, nt, ns, f, i, frames = out->frame_count;
	/* (the window of frames decoded: all of them as a rule; a cutscene's
	track takes them a few dozen at a time, h2_animation_source_decode) */
	long wf = out->window_first, we = out->window_count > 0 ? wf + out->window_count : frames;
	unsigned char codec;

	if (size < 12)
		return size == 0;
	if (wf < 0 || we > frames)
		return FALSE;
	codec = d[0];
	nr = flag_nodes(flags, graph_nodes, rotated);
	if (graph_nodes > (long)flag_size * 8)
		return FALSE;
	nt = flag_nodes(flags + flag_size, graph_nodes, translated);
	ns = flag_nodes(flags + 2 * flag_size, graph_nodes, scaled);
	if (nr != d[1] || nt != d[2] || ns != d[3])
		return FALSE;
	if (codec < 9 && codec_counts)
		codec_counts[codec]++;
	if (codec == 1 || codec == 3 || codec == 8)
	{
		unsigned long toff, soff, quaternion_size = codec == 8 ? 16 : 8;
		long n = codec == 1 ? 1 : frames;

		if (size < 32)
			return FALSE;
		toff = read32(d + 12);
		soff = read32(d + 16);
		if (32 + (unsigned long)(nr * n) * quaternion_size > size || toff > size || (unsigned long)(nt * n) * 12 > size - toff ||
			soff > size || (unsigned long)(ns * n) * 4 > size - soff)
		{
			return FALSE;
		}
		for (i = 0; i < nr; i++)
		{
			short node = map[rotated[i]];

			for (f = wf; f < we && node != NONE; f++)
			{
				unsigned char const *v = d + 32 + (i * n + (codec == 1 ? 0 : f)) * quaternion_size;
				float *q = out->poses[(f - wf) * out->node_count + node].rotation;
				int k;

				for (k = 0; k < 4; k++)
					q[k] = codec == 8 ? readf(v + 4 * k) : read16(v + 2 * k) / 32767.0f;
				quaternion_normalize(q);
			}
			if (node != NONE)
				out->sets[node] |= 1;
		}
		for (i = 0; i < nt; i++)
		{
			short node = map[translated[i]];

			for (f = wf; f < we && node != NONE; f++)
				memcpy(out->poses[(f - wf) * out->node_count + node].translation, d + toff + (i * n + (codec == 1 ? 0 : f)) * 12, 12);
			if (node != NONE)
				out->sets[node] |= 2;
		}
		for (i = 0; i < ns; i++)
		{
			short node = map[scaled[i]];

			for (f = wf; f < we && node != NONE; f++)
				out->poses[(f - wf) * out->node_count + node].scale = readf(d + soff + (i * n + (codec == 1 ? 0 : f)) * 4);
			if (node != NONE)
				out->sets[node] |= 4;
		}
		return TRUE;
	}
	if (codec >= 4 && codec <= 7)
	{
		unsigned long offsets[8];
		unsigned long key_size = codec == 4 || codec == 6 ? 1 : 2;
		long kind;

		if (size < 48)
			return FALSE;
		for (i = 0; i < 8; i++)
		{
			offsets[i] = read32(d + 12 + 4 * i);
			if (offsets[i] > size)
				return FALSE;
		}
		/* (definitions: rotation's at 48, translation's, scale's; key
		frames: rotation's, translation's, scale's; values the same) */
		for (kind = 0; kind < 3; kind++)
		{
			long count = kind == 0 ? nr : kind == 1 ? nt : ns;
			short const *nodes = kind == 0 ? rotated : kind == 1 ? translated : scaled;
			unsigned long definitions = kind == 0 ? 48 : offsets[kind - 1];
			unsigned long keys = offsets[2 + kind];
			unsigned long values = offsets[5 + kind];
			unsigned long value_size = kind == 0 ? 8 : kind == 1 ? 12 : 4;

			if (definitions + (unsigned long)count * 4 > size)
				return FALSE;
			for (i = 0; i < count; i++)
			{
				unsigned long definition = read32(d + definitions + 4 * i);
				long key_count = (long)(definition & 0xFFF), first = (long)(definition >> 12), k = 0;
				short node = map[nodes[i]];

				if (key_count < 1 || keys + (unsigned long)(first + key_count) * key_size > size ||
					values + (unsigned long)(first + key_count) * value_size > size)
				{
					return FALSE;
				}
				if (node == NONE)
					continue;
				out->sets[node] |= (unsigned char)(1 << kind);
				for (f = wf; f < we; f++)
				{
					long a, b;
					float t;
					struct h2_animation_pose *pose = &out->poses[(f - wf) * out->node_count + node];
#define KEY_FRAME(index) ((long)(key_size == 1 ? d[keys + first + (index)] : (unsigned short)read16(d + keys + 2 * (first + (index)))))
					while (k + 1 < key_count && KEY_FRAME(k + 1) <= f)
						k++;
					a = k;
					b = k + 1 < key_count ? k + 1 : k;
					t = b != a && KEY_FRAME(b) > KEY_FRAME(a) ? (float)(f - KEY_FRAME(a)) / (float)(KEY_FRAME(b) - KEY_FRAME(a)) : 0.0f;
					if (f < KEY_FRAME(0))
						a = b = 0, t = 0.0f;
					if (t > 1.0f)
						t = 1.0f;
#undef KEY_FRAME
					if (kind == 0)
					{
						float qa[4], qb[4];
						int c;

						for (c = 0; c < 4; c++)
						{
							qa[c] = read16(d + values + 8 * (first + a) + 2 * c) / 32767.0f;
							qb[c] = read16(d + values + 8 * (first + b) + 2 * c) / 32767.0f;
						}
						quaternion_lerp(qa, qb, t, pose->rotation);
					}
					else if (kind == 1)
					{
						int c;

						for (c = 0; c < 3; c++)
						{
							float va = readf(d + values + 12 * (first + a) + 4 * c), vb = readf(d + values + 12 * (first + b) + 4 * c);

							pose->translation[c] = va + (vb - va) * t;
						}
					}
					else
					{
						float va = readf(d + values + 4 * (first + a)), vb = readf(d + values + 4 * (first + b));

						pose->scale = va + (vb - va) * t;
					}
				}
			}
		}
		return TRUE;
	}
	return FALSE;
}

H2_ALIGN boolean h2_animation_blocks_decode(struct h2_animation_pose *poses, unsigned char *sets, short node_count, short frame_count,
	unsigned char const *data, unsigned long static_size, unsigned long animated_size, unsigned long static_flags_size,
	unsigned long animated_flags_size)
{
	struct decoded out;
	short map[H2_ANIMATION_MAXIMUM_NODES];
	short i;
	unsigned char const *flags = data + static_size + animated_size;

	if (node_count < 1 || node_count > H2_ANIMATION_MAXIMUM_NODES || !animated_flags_size || (animated_flags_size % 3) || (static_flags_size % 3))
		return FALSE;
	memset(&out, 0, sizeof(out));
	out.poses = poses;
	out.sets = sets;
	out.node_count = node_count;
	out.frame_count = frame_count;
	for (i = 0; i < node_count; i++)
		map[i] = i;
	return (!static_size || codec_decode(&out, data, static_size, flags, static_flags_size ? static_flags_size / 3 : animated_flags_size / 3, node_count, map, NULL)) &&
		codec_decode(&out, data + static_size, animated_size, flags + static_flags_size, animated_flags_size / 3, node_count, map, NULL);
}

/* an animation of a graph (graph's index into the graph's list) decoded,
its nodes the model's, the nodes it does not set at defaults (NULL: rest) */
H2_ALIGN static boolean decode(struct h2_graph *graph, short graph_index, long index, struct decoded *out)
{
	struct jmad const *j;
	struct h2_graph_layout const *L = LAYOUT(graph);
	unsigned char const *e;
	unsigned long entry, dd, comp, sf, af, mv;
	unsigned char const *d;
	struct h2_animation_data data;
	long f, n;

	memset(out, 0, sizeof(*out));
	if (graph_index < 0 || graph_index >= graph->graph_count)
		return FALSE;
	j = &graph->graphs[graph_index];
	entry = j->animations + L->animation_size * (unsigned long)index;
	if (index < 0 || index >= j->animation_count || !(e = src_at(graph, entry, L->animation_size)))
		return FALSE;
	out->name_id = read32(e);
	out->type = e[L->a_type];
	out->frame_info_type = e[L->a_frame_info_type];
	out->frame_count = read16(e + L->a_frames);
	out->weight = L->a_weight ? readf(e + L->a_weight) : 1.0f;
	out->loop_frame = read16(e + L->a_loop);
	out->next_variant = read16(e + L->a_next);
	out->key_frame = out->second_key_frame = out->left_foot = out->right_foot = NONE;
	out->node_count = graph->node_count;
	/* (its data: Halo 2's in the tag, Halo 3's in a resource page - the
	source's) */
	if (!graph->source.animation_data(graph->source.context, entry, &data))
		return FALSE;
	d = data.bytes;
	sf = data.static_flags;
	af = data.animated_flags;
	mv = data.movement;
	dd = data.static_size;
	comp = data.animated_size;
	if (out->frame_count < 1 || out->frame_count > MAXIMUM_FRAMES || out->type > 2 || out->frame_info_type > 3 ||
		dd + comp + sf + af + mv > MAXIMUM_ANIMATION_BYTES || (sf && sf != 36 && sf != 24 && sf != 12) || (af != 36 && af != 24 && af != 12) || !d)
	{
		return FALSE;
	}
	out->poses = calloc((unsigned long)out->frame_count * out->node_count, sizeof(*out->poses));
	out->sets = calloc((unsigned long)out->node_count + 1, 1);
	out->frame_info = calloc((unsigned long)out->frame_count * 4, sizeof(float));
	if (!out->poses || !out->sets || !out->frame_info)
	{
		decoded_free(out);
		return FALSE;
	}
	/* (nodes it does not set: the model's defaults - this engine's
	convention, back to Halo 2's) */
	for (f = 0; f < out->frame_count; f++)
	{
		for (n = 0; n < out->node_count; n++)
		{
			struct h2_animation_pose *p = &out->poses[f * out->node_count + n];

			*p = graph->node_defaults[n];
			quaternion_conjugate(p->rotation, p->rotation);
			if (out->type == ANIMATION_OVERLAY)
			{
				p->rotation[0] = p->rotation[1] = p->rotation[2] = 0.0f;
				p->rotation[3] = 1.0f;
				p->translation[0] = p->translation[1] = p->translation[2] = 0.0f;
				p->scale = 1.0f;
			}
		}
	}
	if ((dd && !codec_decode(out, d, dd, d + dd + comp, sf ? sf / 3 : af / 3, j->node_count, j->node_to_model,
			graph->report.codec_counts)) ||
		!codec_decode(out, d + dd, comp, d + dd + comp + sf, af / 3, j->node_count, j->node_to_model, graph->report.codec_counts))
	{
		decoded_free(out);
		return FALSE;
	}
	/* the movement */
	{
		unsigned long frame_size = out->frame_info_type == 1 ? 8 : out->frame_info_type == 2 ? 12 : out->frame_info_type == 3 ? 16 : 0;
		unsigned char const *m = d + dd + comp + sf + af;

		if (frame_size && mv >= frame_size * (unsigned long)out->frame_count)
		{
			for (f = 0; f < out->frame_count; f++)
			{
				unsigned char const *p = m + frame_size * f;

				out->frame_info[4 * f] = readf(p);
				out->frame_info[4 * f + 1] = readf(p + 4);
				if (out->frame_info_type == 2)
					out->frame_info[4 * f + 3] = readf(p + 8);
				else if (out->frame_info_type == 3)
				{
					out->frame_info[4 * f + 2] = readf(p + 8);
					out->frame_info[4 * f + 3] = readf(p + 12);
				}
			}
		}
		else
			out->frame_info_type = 0;
	}
	/* frame events (0x40, 4 bytes: type, frame): primary and secondary
	keyframes, the feet */
	{
		long count, i;
		unsigned long events;

		if (src_block(graph, entry + L->a_events, 4, &count, &events))
		{
			for (i = 0; i < count && i < 32; i++)
			{
				unsigned char const *v = src_at(graph, events + 4 * i, 4);
				short type = v ? read16(v) : -1, frame = v ? read16(v + 2) : -1;

				if (frame < 0 || frame >= out->frame_count)
					continue;
				if (type == 0 && out->key_frame == NONE)
					out->key_frame = frame;
				else if (type == 1 && out->second_key_frame == NONE)
					out->second_key_frame = frame;
				else if (type == 2 && out->left_foot == NONE)
					out->left_foot = frame;
				else if (type == 3 && out->right_foot == NONE)
					out->right_foot = frame;
			}
		}
	}
	if (HALO_ENV_ONCE("HALO_H2_DEBUG_ROOT"))
	{
		struct h2_animation_pose const *p0 = &out->poses[0];
		char text[96];

		text[0] = 0;
		string(graph, out->name_id, text, sizeof(text));
		if (strstr(text, getenv("HALO_H2_DEBUG_ROOT")))

		platform_log("h2 animation: root: %s type %d frames %d info %d: node 0 at %.3f %.3f %.3f rot %.3f %.3f %.3f %.3f (graph node 0 is the model's %d)",
			text, (int)out->type, (int)out->frame_count, (int)out->frame_info_type, p0->translation[0], p0->translation[1], p0->translation[2],
			p0->rotation[0], p0->rotation[1], p0->rotation[2], p0->rotation[3], (int)j->node_to_model[0]);
		if (strstr(text, getenv("HALO_H2_DEBUG_ROOT")) && (out->frame_count <= 4 || getenv("HALO_H2_DEBUG_ROOT_FRAMES")))
		{
			for (f = 0; f < out->frame_count; f += out->frame_count <= 4 ? 1 : 8)
			{
				struct h2_animation_pose const *q = &out->poses[f * out->node_count];

				platform_log("h2 animation: root: %s frame %ld: node 0 at %.3f %.3f %.3f rot %.3f %.3f %.3f %.3f, node 1 rot %.3f %.3f %.3f %.3f sets %d (graph %d, %d nodes)",
					text, f, q->translation[0], q->translation[1], q->translation[2], q->rotation[0], q->rotation[1], q->rotation[2], q->rotation[3],
					q[1].rotation[0], q[1].rotation[1], q[1].rotation[2], q[1].rotation[3], (int)out->sets[0], (int)graph_index, (int)j->node_count);
			}
		}
	}
	return TRUE;
}

/* ---------- private code: this engine's animations */

/* a channel's keyframes (this engine's compressed form): values (count
frames, width floats each; rotations quaternions) where linear
interpolation from the last kept misses by more than tolerance; the frames
kept (> 0, the last frame-1 always if any) to keys, how many */
H2_ALIGN static long channel_keys(float const *values, long frames, int width, boolean rotation, float tolerance,
	unsigned short *keys)
{
	long a = 0, count = 0, f, b, k;
	boolean moves = FALSE;

	for (f = 1; f < frames && !moves; f++)
	{
		for (k = 0; k < width; k++)
		{
			if (fabsf(values[f * width + k] - values[k]) > tolerance)
				moves = TRUE;
		}
	}
	if (!moves || frames < 2)
		return 0;
	while (a < frames - 1)
	{
		long best = a + 1;

		for (b = a + 2; b < frames; b++)
		{
			boolean fits = TRUE;

			for (f = a + 1; f < b && fits; f++)
			{
				float t = (float)(f - a) / (float)(b - a), v[4];

				if (rotation)
					quaternion_lerp(values + a * width, values + b * width, t, v);
				else
				{
					for (k = 0; k < width; k++)
						v[k] = values[a * width + k] + (values[b * width + k] - values[a * width + k]) * t;
				}
				for (k = 0; k < width; k++)
				{
					float x = values[f * width + k];

					/* (a quaternion's other sign is the same turn) */
					if (rotation && values[a * width] * x + values[a * width + 1] * values[f * width + 1] +
						values[a * width + 2] * values[f * width + 2] + values[a * width + 3] * values[f * width + 3] < 0.0f &&
						v[0] * values[f * width] + v[1] * values[f * width + 1] + v[2] * values[f * width + 2] + v[3] * values[f * width + 3] < 0.0f)
					{
						x = -x;
					}
					if (fabsf(v[k] - x) > tolerance)
						fits = FALSE;
				}
			}
			if (!fits)
				break;
			best = b;
		}
		keys[count++] = (unsigned short)best;
		a = best;
	}
	return count;
}

H2_ALIGN static void animation_compressed_sample(struct ce_animation const *a, long nodes, long node, long frame,
	float *rotation, float *translation);

/* poses (frames by the model's nodes, this engine's convention) made this
engine's compressed animation data; the nodes it animates in flags (the
ones that change, of those set; sets: per node, bits of rotation,
translation and scale) */
H2_ALIGN static boolean animation_compress(struct h2_graph *graph, struct h2_animation_pose const *poses, long frames,
	unsigned char const *sets, struct ce_animation *out)
{
	long nodes = graph->node_count, n, f;
	long counts[3] = { 0, 0, 0 }, total_keys[3] = { 0, 0, 0 };
	unsigned short *keys[3][H2_ANIMATION_MAXIMUM_NODES];
	long key_counts[3][H2_ANIMATION_MAXIMUM_NODES];
	float *channel = malloc((unsigned long)frames * 4 * sizeof(float) + 16);
	unsigned char *data;
	unsigned long offsets[11], at, header_size;
	boolean ok = TRUE;
	int kind;

	memset(keys, 0, sizeof(keys));
	memset(key_counts, 0, sizeof(key_counts));
	memset(out->flags, 0, sizeof(out->flags));
	if (!channel)
		return FALSE;
	for (kind = 0; kind < 3 && ok; kind++)
	{
		int width = kind == 0 ? 4 : kind == 1 ? 3 : 1;
		float tolerance = kind == 0 ? 0.0025f : kind == 1 ? 0.0004f : 0.001f;

		for (n = 0; n < nodes && ok; n++)
		{
			if (!(sets[n] & (1 << kind)))
				continue;
			for (f = 0; f < frames; f++)
			{
				struct h2_animation_pose const *p = &poses[f * nodes + n];

				if (kind == 0)
				{
					memcpy(channel + 4 * f, p->rotation, 16);
					/* (each the same side as the one before: this engine
					interpolates its keys as they are signed) */
					if (f && channel[4 * f] * channel[4 * f - 4] + channel[4 * f + 1] * channel[4 * f - 3] +
						channel[4 * f + 2] * channel[4 * f - 2] + channel[4 * f + 3] * channel[4 * f - 1] < 0.0f)
					{
						int k;

						for (k = 0; k < 4; k++)
							channel[4 * f + k] = -channel[4 * f + k];
					}
				}
				else if (kind == 1)
					memcpy(channel + 3 * f, p->translation, 12);
				else
					channel[f] = p->scale;
			}
			keys[kind][n] = malloc((unsigned long)frames * sizeof(unsigned short) + 2);
			if (!keys[kind][n])
			{
				ok = FALSE;
				break;
			}
			key_counts[kind][n] = channel_keys(channel, frames, width, kind == 0, tolerance, keys[kind][n]);
			if (key_counts[kind][n] > 0 && key_counts[kind][n] < 4096)
			{
				out->flags[2 * kind + n / 32] |= 1UL << (n % 32);
				counts[kind]++;
				total_keys[kind] += key_counts[kind][n];
			}
			else
				key_counts[kind][n] = 0;
		}
	}
	/* the layout: the header and the rotations' node headers, the others'
	node headers, the key frame indices, the defaults and keys */
	header_size = 0x2C + 4 * (unsigned long)counts[0];
	at = header_size;
	offsets[3] = at; at += 4 * (unsigned long)counts[1];          /* translation node headers */
	offsets[7] = at; at += 4 * (unsigned long)counts[2];          /* scale node headers */
	offsets[0] = at; at += 2 * (unsigned long)total_keys[0];      /* rotation key frames */
	offsets[4] = at; at += 2 * (unsigned long)total_keys[1];
	offsets[8] = at; at += 2 * (unsigned long)total_keys[2];
	at = (at + 3) & ~3UL;
	offsets[1] = at; at += 6 * (unsigned long)nodes;              /* default rotations */
	offsets[2] = at; at += 6 * (unsigned long)total_keys[0];
	at = (at + 3) & ~3UL;
	offsets[5] = at; at += 12 * (unsigned long)nodes;
	offsets[6] = at; at += 12 * (unsigned long)total_keys[1];
	offsets[9] = at; at += 4 * (unsigned long)nodes;
	offsets[10] = at; at += 4 * (unsigned long)total_keys[2];
	data = ok ? calloc(at + 4, 1) : NULL;
	if (data)
	{
		long first[3] = { 0, 0, 0 }, slot[3] = { 0, 0, 0 };
		int i;

		/* (the header: rotation key frames, default rotations, rotation
		keys, translation node headers, key frames, defaults, keys, scale's
		the same) */
		static int const order[11] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

		for (i = 0; i < 11; i++)
			write32(data + 4 * i, offsets[order[i]]);
		for (n = 0; n < nodes; n++)
		{
			struct h2_animation_pose const *p = &poses[n];

			quaternion_compress_6(p->rotation, data + offsets[1] + 6 * n);
			memcpy(data + offsets[5] + 12 * n, p->translation, 12);
			writef(data + offsets[9] + 4 * n, p->scale);
		}
		for (kind = 0; kind < 3; kind++)
		{
			unsigned long headers = kind == 0 ? 0x2C : offsets[kind == 1 ? 3 : 7];
			unsigned long frame_indices = offsets[kind == 0 ? 0 : kind == 1 ? 4 : 8];
			unsigned long values = offsets[kind == 0 ? 2 : kind == 1 ? 6 : 10];

			for (n = 0; n < nodes; n++)
			{
				long k;
				float previous[4];

				if (!key_counts[kind][n])
					continue;
				write32(data + headers + 4 * slot[kind]++, ((unsigned long)first[kind] << 12) | (unsigned long)key_counts[kind][n]);
				memcpy(previous, poses[n].rotation, 16);
				for (k = 0; k < key_counts[kind][n]; k++)
				{
					long frame = keys[kind][n][k];
					struct h2_animation_pose const *p = &poses[frame * nodes + n];

					write16(data + frame_indices + 2 * (first[kind] + k), frame);
					if (kind == 0)
					{
						float q[4];

						memcpy(q, p->rotation, 16);
						if (q[0] * previous[0] + q[1] * previous[1] + q[2] * previous[2] + q[3] * previous[3] < 0.0f)
						{
							q[0] = -q[0], q[1] = -q[1], q[2] = -q[2], q[3] = -q[3];
						}
						quaternion_compress_6(q, data + values + 6 * (first[kind] + k));
						memcpy(previous, q, 16);
					}
					else if (kind == 1)
						memcpy(data + values + 12 * (first[kind] + k), p->translation, 12);
					else
						writef(data + values + 4 * (first[kind] + k), p->scale);
				}
				first[kind] += key_counts[kind][n];
			}
		}
		out->data = data;
		out->data_size = (long)at;
		out->frame_size = (short)(8 * counts[0] + 12 * counts[1] + 4 * counts[2]);
		graph->report.keys += total_keys[0] + total_keys[1] + total_keys[2];
		/* (HALO_H2_DEBUG: read back as model_animations.c reads it, against
		the poses it was made from) */
		if (HALO_ENV_ONCE("HALO_H2_DEBUG"))
		{
			float worst_rotation = 0.0f, worst_translation = 0.0f, scale_low = 1.0f, scale_high = 1.0f;
			long worst_node = NONE;

			for (f = 0; f < frames; f++)
			{
				for (n = 0; n < nodes; n++)
				{
					struct h2_animation_pose const *p = &poses[f * nodes + n];
					float q[4], t[3], d = 0.0f, e = 0.0f;
					int k;

					animation_compressed_sample(out, nodes, n, f, q, t);
					for (k = 0; k < 4; k++)
						d += q[k] * p->rotation[k];
					d = 1.0f - fabsf(d);
					for (k = 0; k < 3; k++)
						e = fabsf(t[k] - p->translation[k]) > e ? fabsf(t[k] - p->translation[k]) : e;
					if (d > worst_rotation)
						worst_rotation = d, worst_node = n;
					if (e > worst_translation)
						worst_translation = e;
					if (p->scale < scale_low)
						scale_low = p->scale;
					if (p->scale > scale_high)
						scale_high = p->scale;
				}
			}
			if (worst_rotation > 0.002f || worst_translation > 0.002f || scale_low < 0.9f || scale_high > 1.1f)
				platform_log("h2 animation: check: %s: %ld frames, rotation off %.4f (node %ld), translation off %.4f, scale %.3f..%.3f",
					out->name, frames, worst_rotation, worst_node, worst_translation, scale_low, scale_high);
		}
	}
	for (kind = 0; kind < 3; kind++)
	{
		for (n = 0; n < nodes; n++)
			free(keys[kind][n]);
	}
	free(channel);
	return data != NULL;
}

/* (tests: the bytes above read back, as model_animations.c reads them) a
compressed animation's node at a frame */
H2_ALIGN static void animation_compressed_sample(struct ce_animation const *a, long nodes, long node, long frame,
	float *rotation, float *translation)
{
	unsigned char const *d = a->data;
	unsigned long rk = read32(d), dr = read32(d + 4), rv = read32(d + 8), th = read32(d + 12), tk = read32(d + 16),
		dt = read32(d + 20), tv = read32(d + 24);
	long kind;

	(void)nodes;
	for (kind = 0; kind < 2; kind++)
	{
		long index = 0, n;
		boolean flagged = (a->flags[2 * kind + node / 32] >> (node % 32)) & 1;

		for (n = 0; n < node; n++)
			index += (a->flags[2 * kind + n / 32] >> (n % 32)) & 1;
		if (!flagged)
		{
			if (kind == 0)
				quaternion_decompress_6(d + dr + 6 * node, rotation);
			else
				memcpy(translation, d + dt + 12 * node, 12);
			continue;
		}
		{
			unsigned long header = read32(d + (kind == 0 ? 0x2C : th) + 4 * index);
			long first = (long)(header >> 12), count = (long)(header & 0xFFF), k;
			unsigned long frames = kind == 0 ? rk : tk;
			long this_frame = 0, next_frame;
			float a0[4], a1[4], t;

			if (kind == 0)
				quaternion_decompress_6(d + dr + 6 * node, a0);
			else
				memcpy(a0, d + dt + 12 * node, 12);
			for (k = 0; k < count; k++)
			{
				next_frame = (unsigned short)read16(d + frames + 2 * (first + k));
				if (kind == 0)
					quaternion_decompress_6(d + rv + 6 * (first + k), a1);
				else
					memcpy(a1, d + tv + 12 * (first + k), 12);
				if (frame <= next_frame)
				{
					t = next_frame > this_frame ? (float)(frame - this_frame) / (float)(next_frame - this_frame) : 1.0f;
					if (kind == 0)
						quaternion_lerp(a0, a1, t, rotation);
					else
					{
						int c;

						for (c = 0; c < 3; c++)
							translation[c] = a0[c] + (a1[c] - a0[c]) * t;
					}
					break;
				}
				memcpy(a0, a1, 16);
				this_frame = next_frame;
			}
		}
	}
}

/* ---------- private code: the graph */

H2_ALIGN static char *string(struct h2_graph *graph, unsigned long id, char *out, long size)
{
	if (!graph->source.string(graph->source.context, id, out, size))
		out[0] = 0;
	return out;
}

/* whether two node names are one node's across the games' spellings: case,
'_' and ' ' alike, a "bip01 " prefix dropped (Halo CE's "bip01 l thigh", Halo
2's "l thigh", Halo 3's "l_thigh") */
H2_ALIGN static boolean node_names_alike(char const *a, char const *b)
{
	if (!strncmp(a, "bip01 ", 6))
		a += 6;
	if (!strncmp(b, "bip01 ", 6))
		b += 6;
	for (; *a && *b; a++, b++)
	{
		char x = *a == '_' ? ' ' : (char)tolower((unsigned char)*a), y = *b == '_' ? ' ' : (char)tolower((unsigned char)*b);

		if (x != y)
			return FALSE;
	}
	return *a == *b;
}

/* a jmad read: its nodes mapped to the model's by name */
H2_ALIGN static boolean jmad_read(struct h2_graph *graph, long tag, struct jmad *j)
{
	struct h2_cache_tag t;
	long count, i;
	unsigned long nodes;

	memset(j, 0, sizeof(*j));
	j->tag = tag;
	if (tag == NONE || !src_tag_get(graph, tag, &t) || t.group_tag != 'jmad' ||
		!src_block(graph, t.address + LAYOUT(graph)->nodes, LAYOUT(graph)->node_size, &count, &nodes) ||
		count < 1 || count > H2_ANIMATION_MAXIMUM_NODES ||
		!src_block(graph, t.address + LAYOUT(graph)->animations, LAYOUT(graph)->animation_size, &j->animation_count, &j->animations))
	{
		return FALSE;
	}
	j->address = t.address;
	j->node_count = count;
	for (i = 0; i < count; i++)
	{
		char name[64];
		short n;

		string(graph, src_read32(graph, nodes + LAYOUT(graph)->node_size * i), name, sizeof(name));
		j->node_to_model[i] = NONE;
		for (n = 0; n < graph->node_count; n++)
		{
			if (LAYOUT(graph)->names_loose ? node_names_alike(graph->node_names[n], name) : !strcmp(graph->node_names[n], name))
				j->node_to_model[i] = n;
		}
	}
	return TRUE;
}

/* the CE animation made from a source (made once, its variants after it):
its index, NONE */
H2_ALIGN static short source_make(struct h2_graph *graph, short graph_index, short index, short make,
	short bake_graph, short bake_index);

/* the slot's animation: Halo 2's of that label among a type's actions or
overlays (NONE: none) - the graph index and animation index */
H2_ALIGN static boolean type_find(struct h2_graph *graph, unsigned long type, char const *label, short *graph_index,
	short *index)
{
	long count, i;
	unsigned long entries;
	int list;

	for (list = 0; list < 2; list++)
	{
		if (!src_block(graph, type + (list ? LAYOUT(graph)->type_overlays : 0x04), 8, &count, &entries))
			continue;
		for (i = 0; i < count; i++)
		{
			char name[64];
			unsigned char const *e = src_at(graph, entries + 8 * i, 8);

			if (e && !strcmp(string(graph, read32(e), name, sizeof(name)), label))
			{
				*graph_index = (short)(read16(e + 4) + 1);
				*index = read16(e + 6);
				return *index >= 0;
			}
		}
	}
	return FALSE;
}

/* (by name: the class's "any" type or a type) */
H2_ALIGN static unsigned long type_named(struct h2_graph *graph, unsigned long class_address, char const *label)
{
	long count, i;
	unsigned long types;

	if (!src_block(graph, class_address + 4, LAYOUT(graph)->type_size, &count, &types))
		return 0;
	for (i = 0; i < count; i++)
	{
		char name[64];

		if (!strcmp(string(graph, src_read32(graph, types + LAYOUT(graph)->type_size * i), name, sizeof(name)), label))
			return types + LAYOUT(graph)->type_size * i;
	}
	return 0;
}

H2_ALIGN static unsigned long class_named(struct h2_graph *graph, unsigned long mode_address, char const *label)
{
	long count, i;
	unsigned long classes;

	if (!src_block(graph, mode_address + 4, LAYOUT(graph)->class_size, &count, &classes))
		return 0;
	for (i = 0; i < count; i++)
	{
		char name[64];

		if (!strcmp(string(graph, src_read32(graph, classes + LAYOUT(graph)->class_size * i), name, sizeof(name)), label))
			return classes + LAYOUT(graph)->class_size * i;
	}
	return 0;
}

/* this engine's weapon class animations from Halo 2's labels (the "any"
type's actions and overlays), and how each is made */
static struct
{
	short slot;
	char const *h2;
	short make;
} const class_names[] =
{
	/* (a boarding mode's - warthog_b_d... -: its board its idle, which the
	boarder plays in the boarding seat once it has entered, units.c; before
	"idle": the Elite's boarding modes have an idle of their own too) */
	{ 0, "board", _make_base },
	{ 0, "idle", _make_base },
	{ 2, "turn_left", _make_base },
	{ 3, "turn_right", _make_base },
	{ 8, "move_front", _make_base },
	{ 9, "move_back", _make_base },
	{ 10, "move_left", _make_base },
	{ 11, "move_right", _make_base },
	/* (walking: Halo 2's, when there is no run - this engine has one) */
	{ 8, "walk_front", _make_base },
	{ 9, "walk_back", _make_base },
	{ 10, "walk_left", _make_base },
	{ 11, "walk_right", _make_base },
	{ 16, "airborne", _make_base },
	{ 17, "land_soft", _make_base },
	{ 18, "land_hard", _make_base },
	{ 20, "throw_grenade", _make_base },
	/* (Halo 2's kind kept, as its action channel plays it - units.c
	plays an overlay there for a Halo 2 graph: the dual class's ready is an
	overlay, the others' replacements) */
	{ 23, "ready", _make_as_is },
	{ 24, "put_away", _make_as_is },
	{ 25, "aim_still_up", _make_overlay },
	{ 26, "aim_move_up", _make_overlay },
	{ 26, "aim_still_up", _make_overlay },
	/* (Halo 2's dives: the AI's dive from a grenade, actions.c's impulses 8
	10 11 - Halo 2 has no dive back) */
	{ 4, "dive_front", _make_base },
	{ 6, "dive_left", _make_base },
	{ 7, "dive_right", _make_base },
	{ 30, "evade_left", _make_base },
	{ 31, "evade_right", _make_base },
	/* (Halo 2's surprises: the AI's surprise, actions.c's impulses 4 5) */
	{ 27, "surprise_front", _make_base },
	{ 28, "surprise_back", _make_base },
	{ 35, "stunned_front", _make_base },
	{ 36, "stunned_back", _make_base },
	{ 37, "stunned_left", _make_base },
	{ 38, "stunned_right", _make_base },
	{ 39, "melee_strike_1", _make_base },
	{ 39, "melee", _make_base },
	{ 42, "melee_airborne", _make_base },
	{ 42, "melee_strike_1", _make_base },
	/* (a sword's lunge, units/bipeds.c biped_melee_animation: the class's
	slots no player otherwise plays - its dash in the leap's start and
	airborne, its lunge in the leap's melee and the continuous melee; Halo
	2's replacements, played in the action channel) */
	{ 48, "melee_dash", _make_as_is },
	{ 49, "melee_dash_airborne", _make_as_is },
	{ 50, "melee_lunge", _make_as_is },
	{ 46, "melee_lunge_airborne", _make_as_is },
	{ 48, "leap_start", _make_base },
	{ 49, "leap_airborne", _make_base },
	{ 50, "leap_melee", _make_base },
	{ 21, "drop", _make_replacement },
	{ 29, "berserk", _make_base },
	{ 32, "signal_move", _make_base },
	{ 33, "signal_attack", _make_base },
	{ 34, "warn", _make_base },
	{ 40, "celebrate", _make_base },
	{ 41, "panic", _make_base },
	/* (Halo 2's gestures, which its vocalizations play: units.h's
	_unit_weapon_class_animation_h2_*) */
	{ 55, "shakefist", _make_base },
	{ 56, "cheer", _make_base },
	{ 57, "taunt", _make_base },
	{ 58, "point", _make_base },
	{ 59, "hold", _make_base },
	{ 60, "wave", _make_base },
	{ 61, "advance", _make_base },
	{ 62, "fallback", _make_base },
};

/* weapon type animations */
static struct
{
	short slot;
	char const *h2;
	short make;
} const type_names[] =
{
	/* (the reloads as Halo 2 has them: a single weapon's a replacement, the
	dual class's an overlay - played over whatever the body does; units.c
	plays an overlay in the action channel for a Halo 2 graph) */
	{ 0, "reload_1", _make_as_is },
	{ 1, "reload_2", _make_as_is },
	{ 2, "chamber_1", _make_overlay },
	{ 3, "chamber_2", _make_overlay },
	{ 4, "fire_1", _make_overlay },
	{ 5, "fire_2", _make_overlay },
	{ 6, "charged_1", _make_overlay },
	{ 7, "charged_2", _make_overlay },
	/* (Halo 2's replacements: the strike over the legs, the overheat) */
	{ 8, "melee_strike_1", _make_as_is },
	{ 9, "overheated", _make_as_is },
	{ 9, "overheating", _make_as_is },
	/* (Halo 2's own, after this engine's: units.c, units.h's
	_unit_weapon_type_animation_h2_* - the grip held whatever the unit
	does, as Halo 2 has it (a replacement of the fingers, or an overlay);
	the left weapon's firing, reloading, chambering and readying (dual
	wielding); the arms' dual offsets; a plasma weapon's overheat and
	overcharge) */
	{ 10, "grip", _make_as_is },
	{ 11, "fire_1_left", _make_as_is },
	{ 12, "reload_1_left", _make_as_is },
	{ 13, "arm_offsets", _make_as_is },
	{ 14, "chamber_1_left", _make_as_is },
	{ 15, "o_h_exit", _make_as_is },
	{ 16, "overcharged", _make_as_is },
	{ 17, "overheating", _make_as_is },
	{ 18, "ready_left", _make_as_is },
	/* (Halo 2's class actions a weapon type may have of its own - the
	plasma rifle's - looked for in the type first, then the class's "any":
	the grenade throw as Halo 2's replacement, which units.c plays over the
	legs' own animation (unit_h2_legs_*); and only the type's own (slots
	units.c reads before the class's: TYPE_OWN_FIRST below) the ready,
	put away, idle and aim overlays. Slots 19..24, units.h's
	_unit_weapon_type_animation_h2_throw_grenade...) */
	{ 19, "throw_grenade", _make_as_is },
	{ 20, "ready", _make_as_is },
	{ 21, "put_away", _make_as_is },
	{ 22, "idle", _make_base },
	{ 23, "aim_still_up", _make_overlay },
	{ 24, "aim_move_up", _make_overlay },
	{ 24, "aim_still_up", _make_overlay },
};

/* seat animations (the mode's "any any" type) */
static struct
{
	short slot;
	char const *h2;
	short make;
} const seat_names[] =
{
	{ 0, "airborne_dead", _make_base },
	{ 1, "landing_dead", _make_base },
	{ 2, "acc_front_back", _make_overlay },
	{ 3, "acc_left_right", _make_overlay },
	{ 4, "acc_up_down", _make_overlay },
	{ 5, "push", _make_overlay },
	{ 6, "twist", _make_overlay },
	{ 7, "enter", _make_base },
	{ 8, "exit", _make_base },
	{ 9, "look", _make_overlay },
	{ 10, "talk", _make_overlay },
	/* (Halo 2's grip of the seat - the Warthog's wheel, the Ghost's
	handles - in this engine's unused slot: units.c) */
	{ 12, "grip", _make_as_is },
};

/* Halo 2's modes: this engine's seat labels (the first; the stock
vehicles' after it, stand-ins until Halo 2's vehicles are converted) */
static struct
{
	char const *h2;
	char const *labels[4];
} const mode_labels[] =
{
	{ "combat", { "stand", "alert", NULL } },
	{ "crouch", { "crouch", NULL } },
	/* (Halo 2's relaxed stance and walk, a character's: this engine's alert
	seat, which an actor not yet in combat uses; a graph with no patrol mode, a
	player's, keeps combat's as that seat) */
	{ "patrol", { "alert", NULL } },
	/* (Halo 2's flee: the run of a Grunt or Jackal that breaks - arms up, its
	own move, 1.88 wu/s for a Grunt -, this engine's flee seat, which a
	panicked actor's movement takes. Without it a fleeing Grunt fell back to
	its combat or crouch seat and crept away crouched, 0.26 wu/s) */
	{ "flee", { "flee", NULL } },
	{ "warthog_d", { "warthog_d", "W-driver", NULL } },
	{ "warthog_g", { "warthog_g", "W-gunner", NULL } },
	{ "warthog_p", { "warthog_p", "W-passenger", "W-rider", NULL } },
	{ "ghost_d", { "ghost_d", "G-driver", NULL } },
	{ "banshee_d", { "banshee_d", "B-driver", NULL } },
	{ "scorpion_d", { "scorpion_d", "S-driver", "scorpion-driver", NULL } },
	{ "spectre_d", { "spectre_d", NULL } },
	{ "spectre_g", { "spectre_g", NULL } },
	{ "spectre_p_l", { "spectre_p_l", NULL } },
	{ "spectre_p_r", { "spectre_p_r", NULL } },
	{ "wraith_d", { "wraith_d", NULL } },
	{ "c_turret_ap_d", { "c_turret_ap_d", "CT-driver", NULL } },
	{ "h_turret_ap_d", { "h_turret_ap_d", "GT-gunner", NULL } },
	{ "creep_d", { "creep_d", NULL } },
	{ "creep_g", { "creep_g", NULL } },
	{ "insertion_pod_d", { "insertion_pod_d", NULL } },
	{ "mongoose_d", { "mongoose_d", NULL } },
	/* (the multiplayer turrets' gunners: Halo 2's idle, firing and aiming
	in their modes' "any any") */
	{ "c_turret_mp_d", { "c_turret_mp_d", NULL } },
	{ "h_turret_mp_d", { "h_turret_mp_d", NULL } },
	/* (the campaign's: the Pelican's pilot, a Prophet on his throne) */
	{ "pelican_d", { "pelican_d", NULL } },
	{ "throne_d", { "throne_d", NULL } },
	/* (the Phantom's troops, each seat its own mode: their idle and their
	jump out - exit) */
	{ "phantom_p_a01", { "phantom_p_a01", NULL } },
	{ "phantom_p_a02", { "phantom_p_a02", NULL } },
	{ "phantom_p_a03", { "phantom_p_a03", NULL } },
	{ "phantom_p_b01", { "phantom_p_b01", NULL } },
	{ "phantom_p_b02", { "phantom_p_b02", NULL } },
	{ "phantom_p_b03", { "phantom_p_b03", NULL } },
	{ "phantom_p_c01", { "phantom_p_c01", NULL } },
	{ "phantom_p_c02", { "phantom_p_c02", NULL } },
	{ "phantom_p_c03", { "phantom_p_c03", NULL } },
	/* (the Pelican's troops: their enter and exit) */
	{ "pelican_p_l01", { "pelican_p_l01", NULL } },
	{ "pelican_p_l02", { "pelican_p_l02", NULL } },
	{ "pelican_p_l03", { "pelican_p_l03", NULL } },
	{ "pelican_p_l04", { "pelican_p_l04", NULL } },
	{ "pelican_p_l05", { "pelican_p_l05", NULL } },
	{ "pelican_p_r01", { "pelican_p_r01", NULL } },
	{ "pelican_p_r02", { "pelican_p_r02", NULL } },
	{ "pelican_p_r03", { "pelican_p_r03", NULL } },
	{ "pelican_p_r04", { "pelican_p_r04", NULL } },
	{ "pelican_p_r05", { "pelican_p_r05", NULL } },
	/* (boarding, h2_caps_boarding: the vehicles' boarding seats' labels) */
	{ "warthog_b_d", { "warthog_b_d", NULL } },
	{ "warthog_b_p", { "warthog_b_p", NULL } },
	{ "warthog_b_g", { "warthog_b_g", NULL } },
	{ "ghost_b_b", { "ghost_b_b", NULL } },
	{ "ghost_b_l", { "ghost_b_l", NULL } },
	/* (the vehicles made since - h2_vehicles.c's boarding seats) */
	{ "banshee_b_d", { "banshee_b_d", NULL } },
	{ "scorpion_b_d_l", { "scorpion_b_d_l", NULL } },
	{ "scorpion_b_d_r", { "scorpion_b_d_r", NULL } },
	{ "spectre_b_d", { "spectre_b_d", NULL } },
	{ "spectre_b_g", { "spectre_b_g", NULL } },
	{ "spectre_b_p_l", { "spectre_b_p_l", NULL } },
	{ "spectre_b_p_r", { "spectre_b_p_r", NULL } },
	/* (a tank's hull: its boarder beats on the hatch, h2_vehicle_damage.c) */
	{ "scorpion_b_b", { "scorpion_b_b", NULL } },
	{ "wraith_b_l", { "wraith_b_l", NULL } },
	{ "wraith_b_r", { "wraith_b_r", NULL } },
	{ "throne_b_l", { "throne_b_l", NULL } },
	{ "throne_b_r", { "throne_b_r", NULL } },
	/* (the Shadow's ten passengers and its scripted seat, h2_vehicles.c:
	Halo 2's Elites, Brutes and Jackals ride it in their creep_p_l01, its
	Grunts in their creep_p_r02 - each seat label that mode's) */
	{ "creep_p_l01", { "creep_p_l01", "creep_p_l02", "creep_p_l03", "creep_p_l04" } },
	{ "creep_p_l01", { "creep_p_l05", "creep_p_r01", "creep_p_r02", "creep_p_r03" } },
	{ "creep_p_l01", { "creep_p_r04", "creep_p_r05", "creep_sc01", NULL } },
	{ "creep_p_r02", { "creep_p_l01", "creep_p_l02", "creep_p_l03", "creep_p_l04" } },
	{ "creep_p_r02", { "creep_p_l05", "creep_p_r01", "creep_p_r02", "creep_p_r03" } },
	{ "creep_p_r02", { "creep_p_r04", "creep_p_r05", "creep_sc01", NULL } },
};

/* the stock weapons' types (their labels: this engine's weapons' label
fields) as Halo 2's, in Halo 2's classes */
static struct
{
	char const *ce;
	char const *h2_class;
	char const *h2_type;    /* NULL: the class's "any" */
} const type_aliases[] =
{
	{ "ar", "rifle", "smg" },
	/* (a graph with no type of the weapon's holds it by its class's "any", as Halo 2
	does: a Jackal has no "csr" and this engine then refuses the weapon, every
	sniper Jackal stood unarmed) */
	{ "csr", "rifle", NULL },
	{ "cb", "rifle", NULL },
	{ "f", "flag", NULL },
	{ "b", "ball", NULL },
	{ "ft", "support", NULL },
	{ "fc", "missile", "fc" },
};

/* (the PC tier) the animations `more` leads made more variants of those
`first` leads (this engine's permutations: chosen at random, their chances
even), unless they are already */
H2_ALIGN static void variants_append(struct h2_graph *graph, short first, short more)
{
	short a, last = NONE;
	long count = 0, k;

	if (first == NONE || more == NONE || first == more)
		return;
	for (a = first; a != NONE && count < graph->animation_count; a = graph->animations[a].next, count++)
	{
		if (a == more)
			return;
		last = a;
	}
	for (a = more, k = 0; a != NONE && k < graph->animation_count; a = graph->animations[a].next, k++)
	{
		if (a == first)
			return;
	}
	if (last == NONE)
		return;
	graph->animations[last].next = more;
	count += k;
	for (a = first, k = 0; a != NONE && k < count; a = graph->animations[a].next, k++)
	{
		graph->animations[a].weight = 1.0f;
		graph->animations[a].normalized_weight = (float)(k + 1) / (float)count;
	}
}

H2_ALIGN static void slots_fill(struct h2_graph *graph, unsigned long type_any, unsigned long type,
	short *slots, long slot_count, void const *names, long name_count, short idle_graph, short idle_index)
{
	struct entry { short slot; char const *h2; short make; } const *table = names;
	long i;

	for (i = 0; i < name_count; i++)
	{
		short g, index;
		unsigned long where = type ? type : type_any;

		if (table[i].slot >= slot_count || slots[table[i].slot] != NONE)
			continue;
		/* (HALO_H2_ANIMATION_ONLY: those labels alone - tests) */
		if (HALO_ENV_ONCE("HALO_H2_ANIMATION_ONLY") && !strstr(HALO_ENV_ONCE("HALO_H2_ANIMATION_ONLY"), table[i].h2))
			continue;
		if (!where || !type_find(graph, where, table[i].h2, &g, &index))
			continue;
		slots[table[i].slot] = source_make(graph, g, index, table[i].make, idle_graph, idle_index);
		/* (Halo 2's other strikes of a melee its variants, in order - units.c
		plays the one units/bipeds.c chose; the Vita tier's as many as its
		variants) */
		if (slots[table[i].slot] != NONE && !strcmp(table[i].h2, "melee_strike_1"))
		{
			static char const *const others[] = { "melee_strike_2", "melee_strike_3" };
			long k;

			for (k = 0; k < (long)(sizeof(others) / sizeof(others[0])) && k + 1 < MAXIMUM_VARIANTS; k++)
			{
				if (type_find(graph, where, others[k], &g, &index))
					variants_append(graph, slots[table[i].slot], source_make(graph, g, index, table[i].make, idle_graph, idle_index));
			}
		}
	}
}

/* deaths and damage: "any any any" (Halo 2's first mode, "any") as the
graph's unit damage */
H2_ALIGN static void damage_fill(struct h2_graph *graph, unsigned long modes, long mode_count)
{
	long m;

	for (m = 0; m < mode_count; m++)
	{
		char mode_name[64];
		unsigned long any_class, any_type;
		long count, k;
		unsigned long entries;

		string(graph, src_read32(graph, modes + LAYOUT(graph)->mode_size * m), mode_name, sizeof(mode_name));
		if (strcmp(mode_name, "any") || !(any_class = class_named(graph, modes + LAYOUT(graph)->mode_size * m, "any")) ||
			!(any_type = type_named(graph, any_class, "any")) ||
			!src_block(graph, any_type + LAYOUT(graph)->type_damage, LAYOUT(graph)->damage_size, &count, &entries))
		{
			continue;
		}
		for (k = 0; k < count; k++)
		{
			static char const *const types[4] = { "s_ping", "h_ping", "s_kill", "h_kill" };
			char label[64];
			long damage_type, direction_count, d;
			unsigned long directions;

			string(graph, src_read32(graph, entries + LAYOUT(graph)->damage_size * k), label, sizeof(label));
			for (damage_type = 0; damage_type < 4 && strcmp(label, types[damage_type]); damage_type++)
				;
			if (damage_type == 4 || !src_block(graph, entries + LAYOUT(graph)->damage_size * k + 4, LAYOUT(graph)->direction_size, &direction_count, &directions))
				continue;
			for (d = 0; d < direction_count && d < 4; d++)
			{
				long region_count, r;
				unsigned long regions;

				if (!src_block(graph, directions + LAYOUT(graph)->direction_size * d, 4, &region_count, &regions))
					continue;
				for (r = 0; r < region_count && r < 11; r++)
				{
					unsigned char const *e = src_at(graph, regions + 4 * r, 4);
					short g = e ? (short)(read16(e) + 1) : NONE, index = e ? read16(e + 2) : NONE;

					if (index >= 0)
						graph->damage[(damage_type * 4 + d) * 11 + r] = source_make(graph, g, index, _make_base, NONE, NONE);
				}
			}
		}
	}
}

/* a seat of a Halo 2 mode (mode_labels' index, the mode's address): its
own animations and looking screen; its classes filled later
(class_fill) */
H2_ALIGN static struct ce_seat *seat_make(struct h2_graph *graph, long label, unsigned long mode, unsigned long combat)
{
	struct ce_seat *seat;
	unsigned long any_class, any_type;
	long c;

	if (graph->seat_count >= SEATS_LIMIT)
		return NULL;
	seat = &graph->seats[graph->seat_count++];
	memset(seat, 0, sizeof(*seat));
	snprintf(seat->label, sizeof(seat->label), "%s", mode_labels[label].labels[0]);
	seat->mode = mode;
	seat->mode_label = (short)label;
	for (c = 0; c < CE_SEAT_ANIMATIONS; c++)
		seat->animations[c] = NONE;
	seat->classes = calloc(MAXIMUM_CLASSES, sizeof(*seat->classes));
	if (!seat->classes)
	{
		graph->seat_count--;
		return NULL;
	}
	/* (the seat's own: the mode's "any any", else combat's) */
	any_class = class_named(graph, mode, "any");
	any_type = any_class ? type_named(graph, any_class, "any") : 0;
	slots_fill(graph, any_type, 0, seat->animations, CE_SEAT_ANIMATIONS, seat_names,
		(long)(sizeof(seat_names) / sizeof(seat_names[0])), NONE, NONE);
	/* (boarding, h2_caps_boarding: the seat's ejection - its rider thrown
	out by a boarder, units.c - in user0, which this engine never plays
	itself: units.c finds it there) */
	if (h2_caps_boarding())
	{
		/* (and a tank's hull seat's blow on the hatch and its grenade
		dropped in, user1 and user2: h2_vehicle_damage.c) */
		static struct { short slot; char const *h2; short make; } const ejection[] = { { 13, "ejection", _make_base },
			{ 14, "melee", _make_base }, { 15, "throw_grenade", _make_base } };

		slots_fill(graph, any_type, 0, seat->animations, CE_SEAT_ANIMATIONS, ejection, 3, NONE, NONE);
	}
	if (combat && combat != mode)
	{
		unsigned long combat_any = class_named(graph, combat, "any");
		unsigned long combat_type = combat_any ? type_named(graph, combat_any, "any") : 0;

		slots_fill(graph, combat_type, 0, seat->animations, 2, seat_names, 2, NONE, NONE);
	}
	/* (its looking screen: its look overlay's blend screen) */
	if (seat->animations[9] != NONE)
	{
		short g, index;
		unsigned char const *e;

		if (any_type && type_find(graph, any_type, "look", &g, &index) && g >= 0 && g < graph->graph_count &&
			(e = src_at(graph, graph->graphs[g].animations + LAYOUT(graph)->animation_size * index, LAYOUT(graph)->animation_size)) != NULL &&
			(signed char)e[LAYOUT(graph)->a_screen] >= 0)
		{
			long count;
			unsigned long screens;
			unsigned char const *b;

			if (src_block(graph, graph->graphs[g].address + LAYOUT(graph)->blend_screens, LAYOUT(graph)->blend_screen_size, &count, &screens) &&
				(signed char)e[LAYOUT(graph)->a_screen] < count && (b = src_at(graph, screens + LAYOUT(graph)->blend_screen_size * e[0x12] + 4, 24)) != NULL)
			{
				memcpy(seat->looking, b, 24);
			}
		}
	}
	return seat;
}

/* whether a mode has no weapon class but "any" (its seat's animations, idle
and aiming all there) */
H2_ALIGN static boolean mode_classless(struct h2_graph *graph, unsigned long mode_address)
{
	long count, i;
	unsigned long classes;

	if (!src_block(graph, mode_address + 4, LAYOUT(graph)->class_size, &count, &classes) || count <= 0)
		return FALSE;
	for (i = 0; i < count; i++)
	{
		char name[64];

		if (strcmp(string(graph, src_read32(graph, classes + LAYOUT(graph)->class_size * i), name, sizeof(name)), "any"))
			return FALSE;
	}
	return TRUE;
}

/* (a seat sharing the standing seat's types - crouching, a vehicle's
passenger) each type's own class actions (slots 19.., the throw, ready,
put away, idle and aim) its own mode's, not the standing seat's: Halo 2
looks them up in the unit's mode (its crouching plasma rifle idles
crouched) - the type of that name in the seat's class, else none (the
throw: the class's "any") */
H2_ALIGN static void types_own_refill(struct h2_graph *graph, struct ce_class *class, unsigned long class_address,
	unsigned long type_any)
{
	long first, t, k;
	long const count = (long)(sizeof(type_names) / sizeof(type_names[0]));

	for (first = 0; first < count && type_names[first].slot < 19; first++)
		;
	for (t = 0; t < class->type_count; t++)
	{
		struct ce_type *type = &class->types[t];
		char const *label = type->label;
		unsigned long own;

		if (!strncmp(label, "dual ", 5))
			label += 5;
		for (k = 0; k < (long)(sizeof(type_aliases) / sizeof(type_aliases[0])); k++)
		{
			if (!strcmp(type_aliases[k].ce, label))
			{
				label = type_aliases[k].h2_type;
				break;
			}
		}
		own = label && label[0] ? type_named(graph, class_address, label) : 0;
		for (k = 19; k < CE_TYPE_ANIMATIONS; k++)
			type->animations[k] = NONE;
		slots_fill(graph, type_any, own, type->animations, own ? CE_TYPE_ANIMATIONS : TYPE_OWN_FIRST, &type_names[first],
			count - first, NONE, NONE);
		slots_fill(graph, type_any, 0, type->animations, TYPE_OWN_FIRST, &type_names[first], count - first, NONE, NONE);
	}
}

/* a weapon class of a seat's mode (by name) made: its animations (its
"any" type's), its aiming screen, its types (Halo 2's, the stock weapons'
labels that are Halo 2's of this class; unarmed's the empty label) */
H2_ALIGN static void class_fill(struct h2_graph *graph, struct ce_seat *seat, char const *class_name)
{
	unsigned long class_address = class_named(graph, seat->mode, class_name), type_any, types;
	struct ce_class *class, *stand;
	long type_count, t, k;
	short idle_graph = NONE, idle_index = NONE;

	/* (a boarding mode with no classes - the Elite's: its board in the mode's
	"any" - its "unarmed" made from that) */
	if (!class_address && !strcmp(class_name, "unarmed") &&
		(strstr(mode_labels[seat->mode_label].h2, "_b_") || mode_classless(graph, seat->mode)))
		class_address = class_named(graph, seat->mode, "any");
	if (!class_address || seat->class_count >= CLASSES_LIMIT)
		return;
	for (k = 0; k < seat->class_count; k++)
	{
		if (!strcmp(seat->classes[k].label, class_name))
			return;
	}
	class = &seat->classes[seat->class_count++];
	memset(class, 0, sizeof(*class));
	snprintf(class->label, sizeof(class->label), "%s", class_name);
	for (t = 0; t < CE_CLASS_ANIMATIONS; t++)
		class->animations[t] = NONE;
	type_any = type_named(graph, class_address, "any");
	if (type_any)
		type_find(graph, type_any, "idle", &idle_graph, &idle_index);
	/* (a seat but the standing one: its own stance - idle, moving,
	airborne, aiming - and the standing seat's same class's others, made
	there: this engine's 256 animations a graph hold every weapon's class
	this way) */
	stand = NULL;
	if (strcmp(mode_labels[seat->mode_label].h2, "combat"))
	{
		for (k = 0; k < graph->seat_count && !stand; k++)
		{
			long c;

			if (graph->seats[k].alias || strcmp(mode_labels[graph->seats[k].mode_label].h2, "combat"))
				continue;
			for (c = 0; c < graph->seats[k].class_count; c++)
			{
				if (!strcmp(graph->seats[k].classes[c].label, class_name))
					stand = &graph->seats[k].classes[c];
			}
		}
	}
	if (stand)
	{
		static short const own[] = { 0, 8, 9, 10, 11, 16, 25, 26 };

		for (t = 0; t < CE_CLASS_ANIMATIONS; t++)
			class->animations[t] = SLOT_SKIPPED;
		for (t = 0; t < (long)(sizeof(own) / sizeof(own[0])); t++)
			class->animations[own[t]] = NONE;
	}
	slots_fill(graph, type_any, 0, class->animations, CE_CLASS_ANIMATIONS, class_names,
		(long)(sizeof(class_names) / sizeof(class_names[0])), idle_graph, idle_index);
	for (t = 0; t < CE_CLASS_ANIMATIONS; t++)
	{
		/* (the fixes, h2_caps_seat_fixes: a vehicle seat's aiming - 25 still,
		26 moving - is its own or none. The standing seat's aim overlay twists
		the pelvis and spine to the aim; a Ghost's rider, whose Halo 2 mode
		has none, was turned more and more as the Ghost drifted from where
		the player looked - lying sideways across it) */
		if (stand && (t == 25 || t == 26) && h2_caps_seat_fixes() &&
			strcmp(mode_labels[seat->mode_label].h2, "crouch"))
		{
			if (class->animations[t] == SLOT_SKIPPED)
				class->animations[t] = NONE;
			continue;
		}
		if (class->animations[t] == SLOT_SKIPPED || (stand && class->animations[t] == NONE))
			class->animations[t] = stand ? stand->animations[t] : NONE;
	}
	/* (its aiming screen: its aim overlay's blend screen) */
	{
		short g, index;
		unsigned char const *e;

		if (type_any && type_find(graph, type_any, "aim_still_up", &g, &index) && g >= 0 && g < graph->graph_count &&
			(e = src_at(graph, graph->graphs[g].animations + LAYOUT(graph)->animation_size * index, LAYOUT(graph)->animation_size)) != NULL &&
			(signed char)e[LAYOUT(graph)->a_screen] >= 0)
		{
			long count;
			unsigned long screens;
			unsigned char const *b;

			if (src_block(graph, graph->graphs[g].address + LAYOUT(graph)->blend_screens, LAYOUT(graph)->blend_screen_size, &count, &screens) &&
				(signed char)e[LAYOUT(graph)->a_screen] < count &&
				(b = src_at(graph, screens + LAYOUT(graph)->blend_screen_size * e[0x12] + 4, 24)) != NULL)
			{
				memcpy(class->aiming, b, 24);
			}
		}
		/* (an aim overlay with no blend screen is not aimed: Halo 2's unit
		node preprocess, 0x8bb5a0, takes the aiming and looking screens of
		the unit's aim overlays (0x550e20: NULL for a blend screen index of
		NONE) and skips the aim when it has none. This engine applied such an
		overlay through an all-zero screen - its frame 0, a corner of the
		grid: the Sentinel Enforcer's (combat:any:any:aim_still_up, 9 frames,
		no screen) twisted its torso 20 degrees right and 19 down while it
		aimed straight ahead, its left shield generator in front of its right.
		HALO_H2_AIM_UNSCREENED=1: as before) */
		else if (type_any && type_find(graph, type_any, "aim_still_up", &g, &index) && g >= 0 && g < graph->graph_count &&
			(e = src_at(graph, graph->graphs[g].animations + LAYOUT(graph)->animation_size * index, LAYOUT(graph)->animation_size)) != NULL &&
			!getenv("HALO_H2_AIM_UNSCREENED"))
		{
			class->animations[25] = NONE;
			class->animations[26] = NONE;
		}
	}
	/* (a seat's own grenade throw - Halo 2's in the mode's "any any", the
	Warthog passenger's - baked over this class's idle: the standing
	seat's would stand the rider up out of the seat, A8) */
	if (stand && idle_index != NONE)
	{
		unsigned long mode_any = class_named(graph, seat->mode, "any");
		unsigned long mode_any_type = mode_any ? type_named(graph, mode_any, "any") : 0;
		short g, index;

		if (mode_any_type && type_find(graph, mode_any_type, "throw_grenade", &g, &index))
		{
			short made = source_make(graph, g, index, _make_base, idle_graph, idle_index);

			if (made != NONE)
				class->animations[20] = made;
		}
	}
	if (stand)
	{
		/* (the standing seat's types: their reloads, firing, melee) */
		class->type_count = stand->type_count;
		memcpy(class->types, stand->types, sizeof(class->types));
		types_own_refill(graph, class, class_address, type_any);
		graph->report.classes++;
		if (getenv("HALO_H2_DEBUG"))
			platform_log("h2 animation: seat %s class %s (the standing one's beside): %ld animations so far; idle %d", seat->label,
				class_name, graph->animation_count, (int)class->animations[0]);
		return;
	}
	if (!src_block(graph, class_address + 4, LAYOUT(graph)->type_size, &type_count, &types))
		type_count = 0;
	for (t = 0; t < type_count && class->type_count < TYPES_LIMIT; t++)
	{
		char type_name[64];
		struct ce_type *type;

		string(graph, src_read32(graph, types + LAYOUT(graph)->type_size * t), type_name, sizeof(type_name));
		if (!strcmp(type_name, "any"))
			continue;
		type = &class->types[class->type_count++];
		snprintf(type->label, sizeof(type->label), "%s", type_name);
		for (k = 0; k < CE_TYPE_ANIMATIONS; k++)
			type->animations[k] = NONE;
		slots_fill(graph, type_any, types + LAYOUT(graph)->type_size * t, type->animations, CE_TYPE_ANIMATIONS, type_names,
			(long)(sizeof(type_names) / sizeof(type_names[0])), idle_graph, idle_index);
		slots_fill(graph, type_any, 0, type->animations, TYPE_OWN_FIRST, type_names,
			(long)(sizeof(type_names) / sizeof(type_names[0])), idle_graph, idle_index);
	}
	for (k = 0; k < (long)(sizeof(type_aliases) / sizeof(type_aliases[0])) && class->type_count < TYPES_LIMIT; k++)
	{
		struct ce_type *type;
		unsigned long source = 0;
		long u;
		boolean have = FALSE;

		if (strcmp(type_aliases[k].h2_class, class_name))
			continue;
		for (u = 0; u < class->type_count; u++)
			have |= !strcmp(class->types[u].label, type_aliases[k].ce);
		if (have)
			continue;
		if (type_aliases[k].h2_type)
			source = type_named(graph, class_address, type_aliases[k].h2_type);
		type = &class->types[class->type_count++];
		snprintf(type->label, sizeof(type->label), "%s", type_aliases[k].ce);
		for (u = 0; u < CE_TYPE_ANIMATIONS; u++)
			type->animations[u] = NONE;
		slots_fill(graph, type_any, source, type->animations, source ? CE_TYPE_ANIMATIONS : TYPE_OWN_FIRST, type_names,
			(long)(sizeof(type_names) / sizeof(type_names[0])), idle_graph, idle_index);
		slots_fill(graph, type_any, 0, type->animations, TYPE_OWN_FIRST, type_names,
			(long)(sizeof(type_names) / sizeof(type_names[0])), idle_graph, idle_index);
	}
	/* (a melee of the class's: its types' first one when the class has
	none - Halo 2's rifles strike as their weapon does, this engine's
	class strikes once) */
	for (t = 0; t < type_count && class->animations[39] == NONE; t++)
	{
		short g, index;
		char type_name[64];

		string(graph, src_read32(graph, types + LAYOUT(graph)->type_size * t), type_name, sizeof(type_name));
		if (strcmp(type_name, "any") && type_find(graph, types + LAYOUT(graph)->type_size * t, "melee_strike_1", &g, &index))
		{
			class->animations[39] = source_make(graph, g, index, _make_base, idle_graph, idle_index);
			if (class->animations[42] == NONE)
				class->animations[42] = class->animations[39];
		}
	}
	/* (no type: the class's own, its label empty - what an unarmed unit's
	weapon label finds - or the class's name) */
	if ((!strcmp(class_name, "unarmed") || class->type_count == 0) && class->type_count < TYPES_LIMIT)
	{
		struct ce_type *type = &class->types[class->type_count++];
		long u;

		snprintf(type->label, sizeof(type->label), "%s", !strcmp(class_name, "unarmed") ? "" : class_name);
		for (u = 0; u < CE_TYPE_ANIMATIONS; u++)
			type->animations[u] = NONE;
		slots_fill(graph, type_any, 0, type->animations, TYPE_OWN_FIRST, type_names,
			(long)(sizeof(type_names) / sizeof(type_names[0])), idle_graph, idle_index);
		/* (the Hunter: its arm's cannon the stand-in rocket launcher's
		label too, "rl" - h2_level.c's ai_lookup_weapon gives it that for
		Halo 2's hunter_particle_cannon) */
		if (!strcmp(class_name, "unarmed") && graph->graph_count > 0 && class->type_count < TYPES_LIMIT &&
			strstr(src_tag_name(graph, graph->graphs[0].tag), "\\hunter\\"))
		{
			class->types[class->type_count] = *type;
			snprintf(class->types[class->type_count].label, sizeof(class->types[0].label), "%s", "rl");
			class->type_count++;
		}
		/* (a Sentinel - aggressor, constructor, Enforcer: its gun built
		into its body, Halo 2's "any any" its aiming and firing - the guns'
		label, "sen", h2_weapons.c's Sentinel weapons, AI's only) */
		if (!strcmp(class_name, "unarmed") && graph->graph_count > 0 && class->type_count < TYPES_LIMIT &&
			strstr(src_tag_name(graph, graph->graphs[0].tag), "\\sentinel_"))
		{
			class->types[class->type_count] = *type;
			snprintf(class->types[class->type_count].label, sizeof(class->types[0].label), "%s", "sen");
			class->type_count++;
		}
	}
	/* (the PC tier: the dual class's types labelled "dual <type>", which a
	unit holding a left weapon looks for - units.c unit_dual_wield_label -
	so that they are not the single weapons' of the same names, found
	first; both tiers since the Vita's graphs were lifted) */
	if (!strcmp(class_name, "dual"))
	{
		for (t = 0; t < class->type_count; t++)
		{
			char label[sizeof(class->types[t].label)];

			snprintf(label, sizeof(label), "dual %s", class->types[t].label);
			memcpy(class->types[t].label, label, sizeof(label));
		}
		/* (the SMG is the assault rifle's stand-in, its label the stand-in's
		"ar": the type_aliases' rifle row) */
		for (t = 0; t < class->type_count && class->type_count < TYPES_LIMIT; t++)
		{
			if (!strcmp(class->types[t].label, "dual smg"))
			{
				class->types[class->type_count] = class->types[t];
				snprintf(class->types[class->type_count].label, sizeof(class->types[0].label), "dual ar");
				class->type_count++;
				break;
			}
		}
	}
	graph->report.classes++;
	if (getenv("HALO_H2_DEBUG"))
	{
		char labels[512];
		size_t used = 0;

		labels[0] = 0;
		for (t = 0; t < class->type_count && used + 40 < sizeof(labels); t++)
			used += (size_t)snprintf(labels + used, sizeof(labels) - used, " '%s'", class->types[t].label);
		platform_log("h2 animation: seat %s class %s: %ld animations so far; types%s", seat->label, class_name,
			graph->animation_count, labels);
	}
}

/* what is made first while this engine's 256 animations a graph last:
the stance seats' classes of the weapons there are, the stock vehicles'
seats, then the rest (Halo 2's other weapons, dual wielding, the other
vehicles' seats) */
static struct
{
	char const *mode;
	char const *classes;    /* space separated; "*" every one of the mode's */
} const class_priorities[] =
{
	{ "combat", "unarmed rifle pistol missile flag ball" },
	{ "crouch", "unarmed rifle pistol missile flag ball" },
	{ "patrol", "unarmed rifle pistol missile" },
	{ "flee", "unarmed rifle pistol missile" },
	{ "warthog_d", "*" },
	{ "warthog_g", "*" },
	{ "warthog_p", "unarmed rifle pistol missile" },
	{ "ghost_d", "*" },
	{ "combat", "sword support" },
	{ "crouch", "sword support" },
	{ "warthog_p", "flag ball sword support" },
	{ "combat", "dual" },
	{ "crouch", "dual" },
	{ NULL, "*" },
};

H2_ALIGN static void graph_tables(struct h2_graph *graph)
{
	long mode_count, m, i, p, s;
	unsigned long modes;
	unsigned long combat = 0;
	unsigned long mode_addresses[sizeof(mode_labels) / sizeof(mode_labels[0])];

	for (i = 0; i < CE_DAMAGE_ANIMATIONS; i++)
		graph->damage[i] = NONE;
	/* (HALO_H2_DEBUG=3: every Halo 2 animation of the graph and those it
	inherits, by name - what a graph made leaves out) */
	if (getenv("HALO_H2_DEBUG") && atoi(getenv("HALO_H2_DEBUG")) >= 3)
	{
		long g;

		for (g = 0; g < graph->graph_count; g++)
		{
			for (i = 0; i < graph->graphs[g].animation_count; i++)
			{
				unsigned char const *e = src_at(graph, graph->graphs[g].animations + LAYOUT(graph)->animation_size * i, LAYOUT(graph)->animation_size);
				char name[64];

				if (e)
					platform_log("h2 animation: all %ld %ld '%s' type %d frames %d screen %d", g, i,
						string(graph, read32(e), name, sizeof(name)), (int)e[LAYOUT(graph)->a_type], (int)read16(e + LAYOUT(graph)->a_frames),
						(int)(signed char)e[LAYOUT(graph)->a_screen]);
			}
		}
	}
	if (!src_block(graph, graph->graphs[0].address + LAYOUT(graph)->modes, LAYOUT(graph)->mode_size, &mode_count, &modes))
		return;
	damage_fill(graph, modes, mode_count);
	if (getenv("HALO_H2_DEBUG"))
		platform_log("h2 animation: deaths and damage: %ld animations", graph->animation_count);
	for (i = 0; i < (long)(sizeof(mode_labels) / sizeof(mode_labels[0])); i++)
	{
		mode_addresses[i] = 0;
		for (m = 0; m < mode_count; m++)
		{
			char mode_name[64];

			if (!strcmp(string(graph, src_read32(graph, modes + LAYOUT(graph)->mode_size * m), mode_name, sizeof(mode_name)), mode_labels[i].h2))
				mode_addresses[i] = modes + LAYOUT(graph)->mode_size * m;
		}
		/* (a flier's standing seat is its flight mode - a Drone's: its combat
		and patrol are for when it perches, which this engine's flying actors do
		not. Only a biped that flies: an Elite's graph has a flight mode too, its
		jetpack's, and its stand seat made of it played the jetpack's idles for
		every move - Elites glided, hovering in the jetpack's pose) */
		if (!strcmp(mode_labels[i].h2, "combat") && graph->flies)
		{
			for (m = 0; m < mode_count; m++)
			{
				char mode_name[64];

				if (!strcmp(string(graph, src_read32(graph, modes + LAYOUT(graph)->mode_size * m), mode_name, sizeof(mode_name)), "flight"))
					mode_addresses[i] = modes + LAYOUT(graph)->mode_size * m;
			}
		}
		if (!strcmp(mode_labels[i].h2, "combat"))
			combat = mode_addresses[i];
		/* (boarding's modes: the PC tier's) */
		if (strstr(mode_labels[i].h2, "_b_") && !h2_caps_boarding())
			mode_addresses[i] = 0;
	}
	/* the essential seats (the stances, the stock vehicles') */
	for (i = 0; i < ESSENTIAL_MODES && i < (long)(sizeof(mode_labels) / sizeof(mode_labels[0])); i++)
	{
		if (mode_addresses[i])
			seat_make(graph, i, mode_addresses[i], combat);
	}
	if (getenv("HALO_H2_DEBUG"))
		platform_log("h2 animation: the seats' own: %ld animations so far", graph->animation_count);
	/* their classes by priority, while there is room */
	for (p = 0; p < (long)(sizeof(class_priorities) / sizeof(class_priorities[0])); p++)
	{
		/* (the last: every other seat and class there is room for) */
		if (!class_priorities[p].mode)
		{
			for (i = ESSENTIAL_MODES; i < (long)(sizeof(mode_labels) / sizeof(mode_labels[0])); i++)
			{
				if (mode_addresses[i] && graph->animation_count < MAXIMUM_CE_ANIMATIONS - 8)
					seat_make(graph, i, mode_addresses[i], combat);
				else if (mode_addresses[i])
					graph->report.missing++;
			}
		}
		for (s = 0; s < graph->seat_count; s++)
		{
			struct ce_seat *seat = &graph->seats[s];
			char wanted[128], *name, *rest;
			long class_count, c;
			unsigned long classes;

			if (class_priorities[p].mode && strcmp(mode_labels[seat->mode_label].h2, class_priorities[p].mode))
				continue;
			if (!strcmp(class_priorities[p].classes, "*"))
			{
				if (!src_block(graph, seat->mode + 4, LAYOUT(graph)->class_size, &class_count, &classes))
					continue;
				for (c = 0; c < class_count; c++)
				{
					char class_name[64];

					string(graph, src_read32(graph, classes + LAYOUT(graph)->class_size * c), class_name, sizeof(class_name));
					if (strcmp(class_name, "any") && graph->animation_count < MAXIMUM_CE_ANIMATIONS - 4)
						class_fill(graph, seat, class_name);
				}
				continue;
			}
			snprintf(wanted, sizeof(wanted), "%s", class_priorities[p].classes);
			for (name = strtok_r(wanted, " ", &rest); name; name = strtok_r(NULL, " ", &rest))
			{
				if (graph->animation_count < MAXIMUM_CE_ANIMATIONS - 4)
					class_fill(graph, seat, name);
				else
					graph->report.missing++;
			}
		}
	}
	/* (a seat with no class - the Elite's boarding modes (boarding,
	h2_caps_boarding), the multiplayer turrets', the creep's: Halo 2 has
	their idle, firing and aiming in the mode's "any any" - its "unarmed"
	from that) */
	for (s = 0; s < graph->seat_count; s++)
	{
		if (!graph->seats[s].alias && graph->seats[s].class_count == 0 &&
			(strstr(mode_labels[graph->seats[s].mode_label].h2, "_b_") || mode_classless(graph, graph->seats[s].mode)))
		{
			class_fill(graph, &graph->seats[s], "unarmed");
		}
	}
	/* (the fixes, h2_caps_seat_fixes: a vehicle seat's hovering and flying
	animations - this engine's units play them while their vehicle hovers
	or flies, the Ghost's and Banshee's riders - its first class's idle,
	which Halo 2 plays there) */
	for (s = 0; h2_caps_seat_fixes() && s < graph->seat_count; s++)
	{
		struct ce_seat *seat = &graph->seats[s];
		static short const slots[] = { 23, 24, 25, 26, 29 };
		short idle = NONE;
		long c, k;

		if (seat->alias || !strcmp(mode_labels[seat->mode_label].h2, "combat") ||
			!strcmp(mode_labels[seat->mode_label].h2, "crouch"))
		{
			continue;
		}
		for (c = 0; c < seat->class_count && idle == NONE; c++)
			idle = seat->classes[c].animations[0];
		for (k = 0; idle != NONE && k < (long)(sizeof(slots) / sizeof(slots[0])); k++)
		{
			if (seat->animations[slots[k]] == NONE)
				seat->animations[slots[k]] = idle;
		}
	}
	/* (the same seats by the stock vehicles' labels) */
	for (s = 0, i = graph->seat_count; s < i && graph->seat_count < SEATS_LIMIT; s++)
	{
		int l;

		for (l = 1; l < 4 && mode_labels[graph->seats[s].mode_label].labels[l] && graph->seat_count < SEATS_LIMIT; l++)
		{
			struct ce_seat *alias;
			long k;

			/* (a seat of that label its own already: patrol's alert) */
			for (k = 0; k < i; k++)
			{
				if (!strcmp(graph->seats[k].label, mode_labels[graph->seats[s].mode_label].labels[l]))
					break;
			}
			if (k < i)
				continue;
			alias = &graph->seats[graph->seat_count++];

			*alias = graph->seats[s];
			snprintf(alias->label, sizeof(alias->label), "%s", mode_labels[graph->seats[s].mode_label].labels[l]);
			alias->alias = TRUE;
			alias->alias_of = (short)s;
		}
	}
	for (s = 0; getenv("HALO_H2_DEBUG") && s < graph->seat_count; s++)
		platform_log("h2 animation: seat %s: %d classes%s", graph->seats[s].label, (int)graph->seats[s].class_count,
			graph->seats[s].alias ? " (another's)" : "");
}

H2_ALIGN static short ce_animation_new(struct h2_graph *graph)
{
	struct ce_animation *grown;

	if (graph->animation_count >= MAXIMUM_CE_ANIMATIONS)
		return NONE;
	grown = realloc(graph->animations, (graph->animation_count + 1) * sizeof(*grown));
	if (!grown)
		return NONE;
	graph->animations = grown;
	memset(&grown[graph->animation_count], 0, sizeof(*grown));
	return (short)graph->animation_count++;
}

/* one animation made (no variants) */
H2_ALIGN static short source_make_one(struct h2_graph *graph, short graph_index, short index, short make,
	short bake_graph, short bake_index)
{
	struct decoded d, base;
	struct ce_animation *a;
	short result;
	long f, n;
	short type;
	unsigned char *sets;
	char name[64];

	if (!decode(graph, graph_index, index, &d))
		return NONE;
	string(graph, d.name_id, name, sizeof(name));
	/* (a seat's acceleration overlays are kept as Halo 2 has them, three
	frames - the most one way, at rest, the most the other way: this
	engine's own layout, units.c's unit_seat_update setting a seat's
	acceleration to 0.5 at rest, 0..1 - so the middle frame is played when
	the vehicle is still; made the at-rest frame and the most, a still
	rider was held halfway to the most, hunched forward) */
	memset(&base, 0, sizeof(base));
	type = d.type;
	if ((make == _make_base && d.type != ANIMATION_BASE) || (make == _make_replacement && d.type == ANIMATION_OVERLAY))
	{
		/* baked over the idle's first frame (or the model's pose) */
		if (bake_index == NONE || !decode(graph, bake_graph, bake_index, &base) || base.type != ANIMATION_BASE)
		{
			decoded_free(&base);
			memset(&base, 0, sizeof(base));
		}
		for (f = 0; f < d.frame_count; f++)
		{
			for (n = 0; n < d.node_count; n++)
			{
				struct h2_animation_pose *p = &d.poses[f * d.node_count + n];
				struct h2_animation_pose under;

				if (base.poses)
					under = base.poses[n];
				else
				{
					under = graph->node_defaults[n];
					quaternion_conjugate(under.rotation, under.rotation);
				}
				if (d.type == ANIMATION_OVERLAY)
				{
					/* (Halo 2: frame = base x change) */
					float q[4];
					int k;

					quaternion_multiply(under.rotation, p->rotation, q);
					memcpy(p->rotation, q, 16);
					for (k = 0; k < 3; k++)
						p->translation[k] += under.translation[k];
					p->scale *= under.scale;
				}
				else if (!(d.sets[n] & 1) || !(d.sets[n] & 2) || !(d.sets[n] & 4))
				{
					if (!(d.sets[n] & 1))
						memcpy(p->rotation, under.rotation, 16);
					if (!(d.sets[n] & 2))
						memcpy(p->translation, under.translation, 12);
					if (!(d.sets[n] & 4))
						p->scale = under.scale;
				}
			}
		}
		for (n = 0; n < d.node_count; n++)
			d.sets[n] = 7;
		type = make == _make_base ? ANIMATION_BASE : ANIMATION_REPLACEMENT;
		decoded_free(&base);
	}
	else if (make == _make_base || d.type == ANIMATION_BASE)
	{
		/* (a base animation sets every node: those it did not, at the
		defaults decode gave them) */
		for (n = 0; n < d.node_count; n++)
			d.sets[n] = 7;
	}
	/* this engine's convention */
	for (f = 0; f < (long)d.frame_count * d.node_count; f++)
		quaternion_conjugate(d.poses[f].rotation, d.poses[f].rotation);
	result = ce_animation_new(graph);
	if (result == NONE)
	{
		decoded_free(&d);
		return NONE;
	}
	a = &graph->animations[result];
	string(graph, d.name_id, name, sizeof(name));
	/* (this engine's names: 31 characters) */
	snprintf(a->name, sizeof(a->name), "%.31s", name);
	if (HALO_ENV_ONCE("HALO_H2_DEBUG") && atoi(HALO_ENV_ONCE("HALO_H2_DEBUG")) >= 2)
		platform_log("h2 animation: %d '%s' (Halo 2's %s, made %d)", (int)result, a->name, name, (int)make);
	a->type = type;
	a->frame_count = d.frame_count;
	a->frame_info_type = d.frame_info_type;
	a->loop_frame = d.loop_frame >= 0 && d.loop_frame < d.frame_count ? d.loop_frame : 0;
	a->key_frame = d.key_frame;
	a->second_key_frame = d.second_key_frame;
	a->left_foot = d.left_foot;
	a->right_foot = d.right_foot;
	a->next = NONE;
	a->weight = 1.0f;
	a->normalized_weight = 1.0f;
	sets = d.sets;
	if (!animation_compress(graph, d.poses, d.frame_count, sets, &graph->animations[result]))
	{
		decoded_free(&d);
		graph->animation_count--;
		return NONE;
	}
	a = &graph->animations[result];
	if (d.frame_info_type)
	{
		long size = d.frame_info_type == 1 ? 8 : d.frame_info_type == 2 ? 12 : 16;

		a->frame_info = calloc((unsigned long)d.frame_count, (unsigned long)size);
		if (a->frame_info)
		{
			for (f = 0; f < d.frame_count; f++)
			{
				unsigned char *o = a->frame_info + size * f;

				writef(o, d.frame_info[4 * f]);
				writef(o + 4, d.frame_info[4 * f + 1]);
				if (d.frame_info_type == 2)
					writef(o + 8, d.frame_info[4 * f + 3]);
				else if (d.frame_info_type == 3)
				{
					writef(o + 8, d.frame_info[4 * f + 2]);
					writef(o + 12, d.frame_info[4 * f + 3]);
				}
			}
			a->frame_info_size = size * d.frame_count;
		}
		else
			a->frame_info_type = 0;
	}
	graph->report.bytes += a->data_size + a->frame_info_size;
	graph->report.h2_animations++;
	decoded_free(&d);
	return result;
}

H2_ALIGN static unsigned long source_hash_of(short graph_index, short index, short make, short bake_graph, short bake_index)
{
	unsigned long hash = 2166136261UL;

	hash = (hash ^ (unsigned short)graph_index) * 16777619UL;
	hash = (hash ^ (unsigned short)index) * 16777619UL;
	hash = (hash ^ (unsigned short)make) * 16777619UL;
	hash = (hash ^ (unsigned short)bake_graph) * 16777619UL;
	hash = (hash ^ (unsigned short)bake_index) * 16777619UL;
	return hash ^ (hash >> 15);
}

/* (source_make) the hash's slots for source_count + 1 sources: twice as many
as there are at least; FALSE if no room (the sources then walked) */
H2_ALIGN static boolean source_hash_room(struct h2_graph *graph)
{
	unsigned long slots = graph->source_hash ? graph->source_hash_mask + 1 : 0, wanted = 256, k;
	long i, *grown;

	while (wanted < 2 * (unsigned long)(graph->source_count + 1))
		wanted *= 2;
	if (wanted <= slots)
		return TRUE;
	grown = malloc(wanted * sizeof(long));
	if (!grown)
		return FALSE;
	for (k = 0; k < wanted; k++)
		grown[k] = NONE;
	for (i = 0; i < graph->source_count; i++)
	{
		struct source const *s = &graph->sources[i];

		k = source_hash_of(s->graph, s->index, s->make, s->bake_graph, s->bake_index) & (wanted - 1);
		while (grown[k] != NONE)
			k = (k + 1) & (wanted - 1);
		grown[k] = i;
	}
	free(graph->source_hash);
	graph->source_hash = grown;
	graph->source_hash_mask = wanted - 1;
	return TRUE;
}

H2_ALIGN static short source_make(struct h2_graph *graph, short graph_index, short index, short make,
	short bake_graph, short bake_index)
{
	long i;
	short first = NONE, previous = NONE, variant = index, count = 0;
	struct source *grown;
	boolean hashed;

	if (graph_index < 0 || graph_index >= graph->graph_count || index < 0)
		return NONE;
	/* (made already the same way: by the hash - the same answer as the walk,
	no two sources made the same way being kept) */
	hashed = source_hash_room(graph);
	if (hashed)
	{
		unsigned long k = source_hash_of(graph_index, index, make, bake_graph, bake_index) & graph->source_hash_mask;

		for (; graph->source_hash[k] != NONE; k = (k + 1) & graph->source_hash_mask)
		{
			struct source const *s = &graph->sources[graph->source_hash[k]];

			if (s->graph == graph_index && s->index == index && s->make == make && s->bake_graph == bake_graph &&
				s->bake_index == bake_index)
			{
				return s->ce_index;
			}
		}
	}
	else
	{
		for (i = 0; i < graph->source_count; i++)
		{
			struct source const *s = &graph->sources[i];

			if (s->graph == graph_index && s->index == index && s->make == make && s->bake_graph == bake_graph &&
				s->bake_index == bake_index)
			{
				return s->ce_index;
			}
		}
	}
	/* it and its variants (Halo 2's next variant siblings) */
	while (variant >= 0 && count < MAXIMUM_VARIANTS)
	{
		short made = source_make_one(graph, graph_index, variant, make, bake_graph, bake_index);
		unsigned char const *e;

		if (made == NONE)
			break;
		if (first == NONE)
			first = made;
		if (previous != NONE)
			graph->animations[previous].next = made;
		previous = made;
		count++;
		e = src_at(graph, graph->graphs[graph_index].animations + LAYOUT(graph)->animation_size * variant, LAYOUT(graph)->animation_size);
		variant = e ? read16(e + LAYOUT(graph)->a_next) : NONE;
		if (variant == index)
			break;
	}
	/* (their chances even: this engine's running sums) */
	if (first != NONE && count > 1)
	{
		short a = first;
		long k = 0;

		while (a != NONE && k < count)
		{
			graph->animations[a].weight = 1.0f;
			graph->animations[a].normalized_weight = (float)(k + 1) / (float)count;
			a = graph->animations[a].next;
			k++;
		}
	}
	/* (room for it: twice the sources each time, not one more) */
	grown = graph->sources;
	if (graph->source_count >= graph->source_capacity)
	{
		long capacity = graph->source_capacity ? 2 * graph->source_capacity : 64;

		grown = realloc(graph->sources, (size_t)capacity * sizeof(*grown));
		if (grown)
		{
			graph->sources = grown;
			graph->source_capacity = capacity;
		}
	}
	/* (the hash made again: what it held may be stale, the sources moved -
	it holds their indices, not addresses) */
	if (grown && hashed && !source_hash_room(graph))
		hashed = FALSE;
	if (grown)
	{
		grown[graph->source_count].graph = graph_index;
		grown[graph->source_count].index = index;
		grown[graph->source_count].make = make;
		grown[graph->source_count].bake_graph = bake_graph;
		grown[graph->source_count].bake_index = bake_index;
		grown[graph->source_count].ce_index = first;
		if (hashed)
		{
			unsigned long k = source_hash_of(graph_index, index, make, bake_graph, bake_index) & graph->source_hash_mask;

			while (graph->source_hash[k] != NONE)
				k = (k + 1) & graph->source_hash_mask;
			graph->source_hash[k] = graph->source_count;
		}
		else
		{
			/* (no hash to keep: the walk from here on) */
			free(graph->source_hash);
			graph->source_hash = NULL;
			graph->source_hash_mask = 0;
		}
		graph->source_count++;
	}
	if (first == NONE)
		graph->report.missing++;
	return first;
}

/* ---------- public code */

H2_ALIGN struct h2_graph *h2_graph_make(
	struct h2_cache *cache,
	long jmad_tag,
	char const (*node_names)[32],
	struct h2_animation_pose const *node_defaults,
	short const *node_parents,
	short node_count,
	int flies,
	char *why,
	long why_size)
{
	struct h2_graph_source source;

	h2_source_make(&source, cache);
	return h2_graph_make_from(&source, jmad_tag, node_names, node_defaults, node_parents, node_count, flies, why, why_size);
}

H2_ALIGN struct h2_graph *h2_graph_make_from(
	struct h2_graph_source const *source,
	long jmad_tag,
	char const (*node_names)[32],
	struct h2_animation_pose const *node_defaults,
	short const *node_parents,
	short node_count,
	int flies,
	char *why,
	long why_size)
{
	struct h2_graph *graph = calloc(1, sizeof(*graph));
	long count, i;
	unsigned long list, nodes;

	if (why_size > 0)
		why[0] = 0;
	if (!graph || node_count < 1 || node_count > H2_ANIMATION_MAXIMUM_NODES || !source || !source->layout)
	{
		free(graph);
		snprintf(why, (size_t)why_size, "no room");
		return NULL;
	}
	graph->source = *source;
	graph->cache = source->layout == &h2_layout ? source->context : NULL;
	graph->node_count = node_count;
	graph->node_names = calloc((unsigned long)node_count, 32);
	graph->node_defaults = calloc((unsigned long)node_count, sizeof(*graph->node_defaults));
	graph->parents = calloc((unsigned long)node_count, sizeof(short));
	graph->first_children = calloc((unsigned long)node_count, sizeof(short));
	graph->next_siblings = calloc((unsigned long)node_count, sizeof(short));
	if (!graph->node_names || !graph->node_defaults || !graph->parents || !graph->first_children || !graph->next_siblings)
	{
		h2_graph_free(graph);
		snprintf(why, (size_t)why_size, "out of memory");
		return NULL;
	}
	memcpy(graph->node_names, node_names, (unsigned long)node_count * 32);
	memcpy(graph->node_defaults, node_defaults, (unsigned long)node_count * sizeof(*node_defaults));
	if (!jmad_read(graph, jmad_tag, &graph->graphs[0]))
	{
		h2_graph_free(graph);
		snprintf(why, (size_t)why_size, "its animation graph cannot be read");
		return NULL;
	}
	graph->graph_count = 1;
	/* the nodes' links: the model's (its order is the graph's) */
	for (i = 0; i < node_count; i++)
	{
		short m;

		graph->parents[i] = node_parents[i];
		graph->first_children[i] = graph->next_siblings[i] = NONE;
		for (m = (short)(i + 1); m < node_count && node_parents[i] != NONE; m++)
		{
			if (node_parents[m] == node_parents[i])
			{
				graph->next_siblings[i] = m;
				break;
			}
		}
		for (m = 0; m < node_count; m++)
		{
			if (node_parents[m] == i)
			{
				graph->first_children[i] = m;
				break;
			}
		}
	}
	/* the graphs it inherits (their nodes the model's by name) */
	if (LAYOUT(graph)->inheritance &&
		src_block(graph, graph->graphs[0].address + LAYOUT(graph)->inheritance, LAYOUT(graph)->inheritance_size, &count, &list))
	{
		for (i = 0; i < count && graph->graph_count < 1 + MAXIMUM_INHERITED; i++)
		{
			long inherited = src_reference(graph, list + LAYOUT(graph)->inheritance_size * i);

			if (jmad_read(graph, inherited, &graph->graphs[graph->graph_count]))
				graph->graph_count++;
			else
				break;
		}
	}
	graph->node_list_checksum = 0;
	graph->flies = flies;
	graph_tables(graph);
	graph->report.animations = graph->animation_count;
	graph->report.seats = graph->seat_count;
	if (!graph->animation_count)
	{
		h2_graph_free(graph);
		snprintf(why, (size_t)why_size, "none of its animations could be made");
		return NULL;
	}
	if (getenv("HALO_H2_DEBUG"))
	{
		platform_log("h2 animation: %ld animations (%ld of Halo 2's), %ld KB, %ld keys, %d seats, %ld classes, %ld missing; codecs %ld %ld %ld %ld %ld %ld %ld %ld",
			graph->report.animations, graph->report.h2_animations, graph->report.bytes / 1024, graph->report.keys,
			(int)graph->report.seats, graph->report.classes, graph->report.missing, graph->report.codec_counts[1],
			graph->report.codec_counts[2], graph->report.codec_counts[3], graph->report.codec_counts[4],
			graph->report.codec_counts[5], graph->report.codec_counts[6], graph->report.codec_counts[7],
			graph->report.codec_counts[8]);
	}
	return graph;
}

/* (h2_biped.c's graphs made ahead) the map a graph reads from later: the
gather's own open, for one made over a view of it on another thread */
H2_ALIGN void h2_graph_cache_set(
	struct h2_graph *graph,
	struct h2_cache *cache)
{
	if (graph && graph->source.layout == &h2_layout)
	{
		graph->cache = cache;
		graph->source.context = cache;
	}
}

H2_ALIGN void h2_graph_free(
	struct h2_graph *graph)
{
	long i;

	if (!graph)
		return;
	for (i = 0; i < graph->animation_count; i++)
	{
		free(graph->animations[i].data);
		free(graph->animations[i].frame_info);
	}
	for (i = 0; i < graph->seat_count; i++)
	{
		if (!graph->seats[i].alias)
			free(graph->seats[i].classes);
	}
	free(graph->animations);
	free(graph->sources);
	free(graph->source_hash);
	free(graph->node_names);
	free(graph->node_defaults);
	free(graph->parents);
	free(graph->first_children);
	free(graph->next_siblings);
	free(graph);
}

/* the animation index blocks (shorts) at field */
H2_ALIGN static void index_block(struct h2_biped_host const *host, unsigned long field, short const *indices, long count)
{
	long used = count, i;
	unsigned long elements;

	/* (trailing NONEs left out) */
	while (used > 0 && indices[used - 1] == NONE)
		used--;
	elements = host->block(host->context, field, used, 2);
	if (host->failed(host->context))
		return;
	for (i = 0; i < used; i++)
		write16(host->at(host->context, elements + 2 * i), indices[i]);
}

H2_ALIGN unsigned long h2_graph_write(
	struct h2_graph *graph,
	struct h2_biped_host const *host)
{
	void *c = host->context;
	unsigned long root = host->alloc(c, 0x80), seats, nodes, animations;
	long i;
	short *parents;
	unsigned long *seat_offsets = calloc(MAXIMUM_SEATS, sizeof(unsigned long));

	if (!seat_offsets || host->failed(c))
	{
		free(seat_offsets);
		return 0;
	}
	/* nodes */
	nodes = host->block(c, root + 0x68, graph->node_count, CE_NODE_SIZE);
	for (i = 0; i < graph->node_count && !host->failed(c); i++)
	{
		unsigned char *o = host->at(c, nodes + CE_NODE_SIZE * i);

		memcpy(o, graph->node_names[i], 31);
		write16(o + 0x20, graph->next_siblings[i]);
		write16(o + 0x22, graph->first_children[i]);
		write16(o + 0x24, graph->parents[i]);
	}
	/* seats (an alias: the same blocks under another label) */
	seats = host->block(c, root + 0x0C, graph->seat_count, CE_SEAT_SIZE);
	for (i = 0; i < graph->seat_count && !host->failed(c); i++)
	{
		struct ce_seat const *seat = &graph->seats[i];
		unsigned long s = seats + CE_SEAT_SIZE * i;
		long k;

		/* (an alias written whole: blocks are never shared) */
		seat_offsets[i] = s;
		memcpy(host->at(c, s), seat->label, 31);
		memcpy(host->at(c, s + 0x20), seat->looking, 24);
		index_block(host, s + 0x40, seat->animations, CE_SEAT_ANIMATIONS);
		host->block(c, s + 0x4C, 0, CE_IK_SIZE);
		{
			unsigned long classes = host->block(c, s + 0x58, seat->class_count, CE_CLASS_SIZE);

			for (k = 0; k < seat->class_count && !host->failed(c); k++)
			{
				struct ce_class const *class = &seat->classes[k];
				unsigned long w = classes + CE_CLASS_SIZE * k, types;
				long t;

				memcpy(host->at(c, w), class->label, 31);
				if (strcmp(class->label, "unarmed"))
					memcpy(host->at(c, w + 0x40), "right hand", 11);
				memcpy(host->at(c, w + 0x60), class->aiming, 24);
				index_block(host, w + 0x98, class->animations, CE_CLASS_ANIMATIONS);
				host->block(c, w + 0xA4, 0, CE_IK_SIZE);
				types = host->block(c, w + 0xB0, class->type_count, CE_TYPE_SIZE);
				for (t = 0; t < class->type_count && !host->failed(c); t++)
				{
					memcpy(host->at(c, types + CE_TYPE_SIZE * t), class->types[t].label, 31);
					index_block(host, types + CE_TYPE_SIZE * t + 0x30, class->types[t].animations, CE_TYPE_ANIMATIONS);
				}
			}
		}
	}
	/* unit damage */
	{
		unsigned long damage = host->block(c, root + 0x3C, CE_DAMAGE_ANIMATIONS, 2);

		for (i = 0; i < CE_DAMAGE_ANIMATIONS && !host->failed(c); i++)
			write16(host->at(c, damage + 2 * i), graph->damage[i]);
	}
	host->block(c, root + 0x00, 0, 0x14);
	host->block(c, root + 0x18, 0, 0x14);
	host->block(c, root + 0x24, 0, 0x14);
	host->block(c, root + 0x30, 0, 0x60);
	host->block(c, root + 0x48, 0, 0x1C);
	host->block(c, root + 0x54, 0, 0x14);
	writef(host->at(c, root + 0x60), 0.06f);
	/* the animations */
	animations = host->block(c, root + 0x74, graph->animation_count, CE_ANIMATION_SIZE);
	/* (each one's runtime parent: the first of its permutations - Halo 2's
	variants, linked by next - which model_animations.c's animation_advance
	starts again from when an animation that does not loop ends; the tool
	writes it: none here sent such an animation's unit to no animation at
	all, its model's bind pose - a rider stood up out of the Ghost's seat
	once its idle had played) */
	parents = calloc((unsigned long)graph->animation_count + 1, sizeof(*parents));
	for (i = 0; parents && i < graph->animation_count; i++)
		parents[i] = (short)i;
	for (i = 0; parents && i < graph->animation_count; i++)
	{
		short next = graph->animations[i].next;

		/* (variants are made after their first: a link forward alone) */
		if (next > i && next < graph->animation_count)
			parents[next] = parents[i];
	}
	for (i = 0; i < graph->animation_count && !host->failed(c); i++)
	{
		struct ce_animation const *a = &graph->animations[i];
		unsigned long at = animations + CE_ANIMATION_SIZE * i;
		unsigned char *o = host->at(c, at);
		int k;

		memcpy(o, a->name, 31);
		write16(o + 0x20, a->type);
		write16(o + 0x22, a->frame_count);
		write16(o + 0x24, a->frame_size);
		write16(o + 0x26, a->frame_info_type);
		write32(o + 0x28, graph->node_list_checksum);
		write16(o + 0x2C, graph->node_count);
		write16(o + 0x2E, a->loop_frame);
		writef(o + 0x30, a->weight);
		write16(o + 0x34, a->key_frame);
		write16(o + 0x36, a->second_key_frame);
		write16(o + 0x38, a->next);
		write16(o + 0x3A, 1);                  /* compressed */
		write16(o + 0x3C, -1);                 /* no sound */
		write16(o + 0x3E, -1);
		o[0x40] = (unsigned char)(a->left_foot >= 0 && a->left_foot < 255 ? a->left_foot : 0xFF);
		o[0x41] = (unsigned char)(a->right_foot >= 0 && a->right_foot < 255 ? a->right_foot : 0xFF);
		write16(o + 0x42, parents ? parents[i] : (short)i);
		writef(o + 0x44, a->normalized_weight);
		for (k = 0; k < 2; k++)
		{
			write32(o + 0x5C + 4 * k, a->flags[2 + k]);    /* translation */
			write32(o + 0x6C + 4 * k, a->flags[k]);        /* rotation */
			write32(o + 0x7C + 4 * k, a->flags[4 + k]);    /* scale */
		}
		write32(o + 0x88, 0);                  /* its compressed data first */
		if (a->frame_info_size)
			host->data(c, at + 0x48, a->frame_info, (unsigned long)a->frame_info_size);
		else
			host->data(c, at + 0x48, NULL, 0);
		host->data(c, at + 0x8C, NULL, 0);
		host->data(c, at + 0xA0, a->data, (unsigned long)a->data_size);
	}
	free(seat_offsets);
	free(parents);
	return host->failed(c) ? 0 : root;
}

H2_ALIGN void h2_graph_report(
	struct h2_graph const *graph,
	struct h2_graph_report *report)
{
	*report = graph->report;
}

H2_ALIGN boolean h2_animation_decode_raw(
	struct h2_cache *cache,
	long jmad_tag,
	long animation_index,
	struct h2_animation_pose **poses,
	unsigned char **node_sets,
	short *node_count,
	short *frame_count,
	short *type)
{
	struct h2_graph graph;
	struct decoded d;
	long count, i;
	unsigned long nodes;
	struct h2_cache_tag t;
	static char names[H2_ANIMATION_MAXIMUM_NODES][32];
	static struct h2_animation_pose defaults[H2_ANIMATION_MAXIMUM_NODES];

	memset(&graph, 0, sizeof(graph));
	graph.cache = cache;
	h2_source_make(&graph.source, cache);
	if (!h2_cache_tag_get(cache, jmad_tag, &t) || !src_block(&graph, t.address + h2_layout.nodes, h2_layout.node_size, &count, &nodes) ||
		count < 1 || count > H2_ANIMATION_MAXIMUM_NODES)
	{
		return FALSE;
	}
	/* (its own nodes, the rest pose under them) */
	for (i = 0; i < count; i++)
	{
		string(&graph, src_read32(&graph, nodes + h2_layout.node_size * i), names[i], 32);
		memset(&defaults[i], 0, sizeof(defaults[i]));
		defaults[i].rotation[3] = 1.0f;
		defaults[i].scale = 1.0f;
	}
	graph.node_count = (short)count;
	graph.node_names = names;
	graph.node_defaults = defaults;
	if (!jmad_read(&graph, jmad_tag, &graph.graphs[0]))
		return FALSE;
	graph.graph_count = 1;
	if (!decode(&graph, 0, animation_index, &d))
		return FALSE;
	*poses = d.poses;
	*node_sets = d.sets;
	*node_count = d.node_count;
	*frame_count = d.frame_count;
	*type = d.type;
	free(d.frame_info);
	return TRUE;
}

/* ---------- an animation's source kept, its frames decoded a window at
a time (a cutscene's tracks, h2_cinematics.c) */

struct h2_animation_source
{
	unsigned char *data;        /* Halo 2's compressed animation, a copy */
	unsigned long size, dd, comp, sf, af;
	short frame_count, node_count, type;
	short map[H2_ANIMATION_MAXIMUM_NODES];
};

H2_ALIGN struct h2_animation_source *h2_animation_source_open(struct h2_cache *cache, long jmad_tag, long animation_index)
{
	struct h2_animation_source *s;
	struct h2_cache_tag t;
	unsigned char const *e, *d;
	long count, animation_count, i;
	unsigned long nodes, animations, size, address, mv, dd, comp, sf, af;
	short frame_count, type;

	if (!h2_cache_tag_get(cache, jmad_tag, &t) || !h2_cache_block(cache, t.address + h2_layout.nodes, h2_layout.node_size, &count, &nodes) ||
		count < 1 || count > H2_ANIMATION_MAXIMUM_NODES ||
		!h2_cache_block(cache, t.address + h2_layout.animations, h2_layout.animation_size, &animation_count, &animations) ||
		animation_index < 0 || animation_index >= animation_count ||
		!(e = h2_cache_at(cache, animations + h2_layout.animation_size * animation_index, h2_layout.animation_size)))
	{
		return NULL;
	}
	type = e[0x10];
	frame_count = read16(e + 0x14);
	size = read32(e + 0x28);
	address = read32(e + 0x2C);
	sf = e[0x30];
	af = e[0x31];
	mv = (unsigned short)read16(e + 0x32);
	dd = (unsigned short)read16(e + 0x36);
	comp = read32(e + 0x3C);
	if (frame_count < 1 || frame_count > MAXIMUM_FRAMES || type > 2 || size > MAXIMUM_ANIMATION_BYTES ||
		dd + comp + sf + af + mv > size || (sf && sf != 36 && sf != 24 && sf != 12) || (af != 36 && af != 24 && af != 12) ||
		!(d = h2_cache_at(cache, address, size)))
	{
		return NULL;
	}
	s = calloc(1, sizeof(*s));
	if (!s || !(s->data = malloc(size ? size : 1)))
	{
		free(s);
		return NULL;
	}
	memcpy(s->data, d, size);
	s->size = size, s->dd = dd, s->comp = comp, s->sf = sf, s->af = af;
	s->frame_count = frame_count;
	s->node_count = (short)count;
	s->type = type;
	for (i = 0; i < count; i++)
		s->map[i] = (short)i;
	return s;
}

H2_ALIGN struct h2_animation_source *h2_animation_source_copy(struct h2_animation_source const *from)
{
	struct h2_animation_source *s = from ? malloc(sizeof(*s)) : NULL;

	if (!s)
		return NULL;
	*s = *from;
	s->data = malloc(from->size ? from->size : 1);
	if (!s->data)
	{
		free(s);
		return NULL;
	}
	memcpy(s->data, from->data, from->size);
	return s;
}

H2_ALIGN void h2_animation_source_close(struct h2_animation_source *s)
{
	if (s)
		free(s->data);
	free(s);
}

H2_ALIGN void h2_animation_source_figures(struct h2_animation_source const *s, short *node_count, short *frame_count, short *type,
	unsigned long *bytes)
{
	*node_count = s->node_count;
	*frame_count = s->frame_count;
	*type = s->type;
	*bytes = s->size;
}

H2_ALIGN boolean h2_animation_source_same(struct h2_animation_source const *a, struct h2_animation_source const *b)
{
	return a && b && a->size == b->size && a->frame_count == b->frame_count && a->node_count == b->node_count &&
		!memcmp(a->data, b->data, a->size);
}

/* frames [first, first + count) of every node into poses (count by
node_count, Halo 2's convention: the nodes it does not set at identity),
the nodes it sets into sets (node_count + 1 bytes) */
H2_ALIGN boolean h2_animation_source_decode(struct h2_animation_source const *s, short first, short count,
	struct h2_animation_pose *poses, unsigned char *sets)
{
	struct decoded out;
	unsigned char const *d = s->data;
	long f, n;

	if (first < 0 || count < 1 || first + count > s->frame_count)
		return FALSE;
	memset(&out, 0, sizeof(out));
	out.frame_count = s->frame_count;
	out.node_count = s->node_count;
	out.type = s->type;
	out.poses = poses;
	out.sets = sets;
	out.window_first = first;
	out.window_count = count;
	memset(sets, 0, (size_t)s->node_count + 1);
	for (f = 0; f < count; f++)
	{
		for (n = 0; n < s->node_count; n++)
		{
			struct h2_animation_pose *p = &poses[f * s->node_count + n];

			p->rotation[0] = p->rotation[1] = p->rotation[2] = 0.0f;
			p->rotation[3] = 1.0f;
			p->translation[0] = p->translation[1] = p->translation[2] = 0.0f;
			p->scale = 1.0f;
		}
	}
	return (!s->dd || codec_decode(&out, d, s->dd, d + s->dd + s->comp, s->sf ? s->sf / 3 : s->af / 3, s->node_count, s->map, NULL)) &&
		codec_decode(&out, d + s->dd, s->comp, d + s->dd + s->comp + s->sf, s->af / 3, s->node_count, s->map, NULL);
}

/* (keep the sampler for tests and debugging builds) */
void h2_animation_compressed_sample_unused(void);
void h2_animation_compressed_sample_unused(void)
{
	(void)animation_compressed_sample;
}
