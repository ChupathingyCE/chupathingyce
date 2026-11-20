/*
H2_OBJANIM.C

Halo 2's animated objects made this engine's (h2_objanim.h says what and
from where).

How each piece becomes this engine's:
- the model's nodes: Halo 2's render model's, in its order (names, parent,
  first child, next sibling, default translation, default rotation
  conjugated - this engine's convention -, distance from parent, and the
  inverse of the default transform as it is: this engine's runtime default
  inverse matrix, the same scale-forward-left-up-position layout). The
  vertices stay in the model's space (its bind pose), each its node's
  (h2_level.c: a rigid section's node, a rigid-boned or skinned vertex's
  first node through the section's node map);
- the position animation: decoded frame by frame (every codec: static 1,
  full-frame 3 and 8, keyframed 4-7 interpolated as Halo 2 does between
  keys), its nodes the model's by name, each rotation conjugated; written
  uncompressed as this engine's overlay (8-byte quaternions, float
  translations and scales), which its device code plays at position x
  (frames - 1), or x frames for a looping position (a gear: Halo 2's last
  frame, its first again, left out so position 1 wraps to frame 0 as
  Halo 2's does). Uncompressed because this engine's keyframed overlays
  take a rotation at the whole frame below (overlay_animation_apply_
  continuous): Halo 2 interpolates between frames;
- the device: Halo 2's speeds (position units a second) as this engine's
  runtime velocities a tick (/30) and accelerations a tick squared (/900);
  the transition times the inverse; machine type, flags, door open time,
  collision response and elevator node as they are (the same enums);
- the collision model: this engine's nodes are the model's; each Halo 2
  BSP goes to its node (by name), one a node (a node's largest: this
  engine's node has one BSP a permutation) in the one region and
  permutation this engine's objects made of Halo 2's have.
*/

/* ---------- headers */

#include "cseries.h"
#include "h2_objanim.h"
#include "devices/device_definitions.h"
#include "h2_cache.h"
#include "h2_sound.h"
#include "objects/objects.h"
#include "tag_files/tag_groups.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (the C library's allocator: h2_level.c's reasons) */
#undef malloc
#undef free
#undef realloc

/* (the Vita tier: what a level's build allocates is the builder's own
heap's, not the C heap's - h2_build_heap.h) */
#define H2_BUILD_HEAP_WRAP
#include "h2_build_heap.h"

#define H2_ALIGN __attribute__((force_align_arg_pointer))

void platform_log(char const *format, ...);

/* ---------- constants */

enum
{
	/* Halo 2's */
	OBJECT_BOUNDING_RADIUS = 0x04,
	OBJECT_BOUNDING_OFFSET = 0x08,
	OBJECT_MODEL = 0x34,
	OBJECT_APPLY_COLLISION_DAMAGE_SCALE = 0x6C,
	DEVICE_FLAGS = 0xBC,
	DEVICE_POWER_SPEED = 0xC0,
	DEVICE_POWER_ACCELERATION = 0xC4,
	DEVICE_POSITION_SPEED = 0xC8,
	DEVICE_POSITION_ACCELERATION = 0xCC,
	DEVICE_DEPOWERED_SPEED = 0xD0,
	DEVICE_DEPOWERED_ACCELERATION = 0xD4,
	DEVICE_SOUNDS = 0xDC,
	DEVICE_DELAY_TIME = 0x10C,
	DEVICE_ACTIVATION_RADIUS = 0x118,
	MACHINE_TYPE = 0x11C,
	MACHINE_FLAGS = 0x11E,
	MACHINE_DOOR_OPEN_TIME = 0x120,
	MACHINE_COLLISION_RESPONSE = 0x12C,
	MACHINE_ELEVATOR_NODE = 0x12E,
	CONTROL_TYPE = 0x11C,
	CONTROL_TRIGGERS_WHEN = 0x11E,
	CONTROL_CALL_VALUE = 0x120,
	CONTROL_ACTION_STRING = 0x124,
	CONTROL_SOUNDS = 0x128,
	MINIMUM_DEVICE_SIZE = 0x140,

	HLMT_RENDER_MODEL = 0x00,
	HLMT_COLLISION_MODEL = 0x08,
	HLMT_ANIMATION = 0x10,

	MODE_NODES = 0x48,
	MODE_NODE_SIZE = 0x60,

	JMAD_NODES = 0x0C,
	JMAD_NODE_SIZE = 0x20,
	JMAD_ANIMATIONS = 0x2C,
	JMAD_ANIMATION_SIZE = 0x60,

	COLL_REGIONS = 0x1C,
	COLL_REGION_SIZE = 0x0C,
	COLL_PERMUTATION_SIZE = 0x14,
	COLL_BSP_SIZE = 0x44,
	COLL_BSP_SURFACES = 0x2C,
	COLL_NODES = 0x2C,
	COLL_NODE_SIZE = 0x0C,

	/* the globals' English strings (count, data size, index and data
	references: 8-byte entries, a string id and its text's offset) */
	GLOBALS_ENGLISH_LOCALE = 0x190,
	MAXIMUM_LOCALE_STRINGS = 65536,
	MAXIMUM_LOCALE_BYTES = 4 * 1024 * 1024,

	SCENARIO_DEVICE_GROUPS = 0xA0,
	SCENARIO_DEVICE_GROUP_SIZE = 0x28,
	PLACEMENT_POWER_GROUP = 0x34,

	MAXIMUM_FRAMES = 1024,
	MAXIMUM_ANIMATION_BYTES = 256 * 1024,

	ANIMATION_BASE = 0,
	ANIMATION_OVERLAY = 1,

	H2_MACHINE_GEAR = 2,

	/* this engine's */
	CE_GRAPH_SIZE = 0x80,
	CE_GRAPH_DEVICES = 0x30,
	CE_GRAPH_NODES = 0x68,
	CE_GRAPH_ANIMATIONS = 0x74,
	CE_GRAPH_NODE_SIZE = 0x40,
	CE_DEVICE_ANIMATIONS_SIZE = 0x60,
	CE_ANIMATION_SIZE = 0xB4,
	CE_STRING_LIST_SIZE = 12,
	CE_STRING_SIZE = 20,
	CE_COLLISION_SIZE = 664,
	CE_COLLISION_MATERIAL_SIZE = 72,
	CE_COLLISION_REGION_SIZE = 84,
	CE_COLLISION_PERMUTATION_SIZE = 32,
	CE_COLLISION_NODE_SIZE = 64,
	CE_COLLISION_BSP_SIZE = 0x60,
	CE_DEVICE = 0x17C,
	CE_MACHINE = 0x290,
	CE_CONTROL = 0x290,

	TICKS_A_SECOND = 30,
};

/* ---------- structures */

struct pose
{
	float rotation[4];
	float translation[3];
	float scale;
};

struct node
{
	char name[H2_OBJANIM_NAME_SIZE];
	unsigned long id;
	short parent, first_child, next_sibling;
	float translation[3];
	float rotation[4];          /* this engine's convention */
	float distance;
	float inverse[13];
};

struct h2_objanim
{
	long tag;
	unsigned long group;        /* 'mach', 'ctrl' */
	char name[64];

	short node_count;
	struct node nodes[H2_OBJANIM_MAXIMUM_NODES];

	/* the position animation, this engine's */
	boolean has_graph;          /* (Halo 2's model names a graph) */
	boolean animated;
	/* (its graph read, but with no device position animation: a machine
	its scripts move by named tracks alone - 03b's and 08b's Scarabs,
	device_set_position_track, h2_cinematics.c) */
	boolean tracks_only;
	short frame_count, h2_frame_count, codec;
	boolean position_snaps;     /* (a looping position) no frame from its last to its first */
	/* (64 bits: an object has up to H2_OBJANIM_MAXIMUM_NODES nodes, 44 -
in 32 the shifts past bit 31 were undefined, and the frames' and defaults'
sizes and their writing disagreed for The Great Journey's 33-node
objects: the defaults' 1.0 scales written past their buffer broke the level
builder's heap; Halo CE's tag keeps two words each) */
	unsigned long long rotation_flags, translation_flags, scale_flags;
	short frame_size;
	unsigned char *frames;
	long frames_size;
	unsigned char *defaults;
	long defaults_size;

	/* the device */
	unsigned long device_flags;
	float power_speed, power_acceleration;
	float position_speed, position_acceleration;
	float depowered_speed, depowered_acceleration;
	float delay_time, activation_radius;
	short machine_type;
	unsigned short machine_flags;
	float door_open_time;
	/* (Halo 2's object bounding radius and offset: a door's sphere) */
	float bounding_radius, bounding_offset[3];
	short collision_response, elevator_node;
	/* (the object's "apply collision damage scale": Terminal's train's
	100 - what its moving collision meets dies) */
	float collision_damage_scale;
	short control_type, triggers_when;
	float call_value;
	/* (a control) its action's text, this engine's prompt's part after
	"Press [action] to": Halo 2's English, its button icons left out */
	unsigned short action_text[H2_OBJANIM_ACTION_TEXT_SIZE];
	long sounds[NUMBER_OF_H2_OBJANIM_SOUNDS];

	/* the collision: each node's Halo 2 BSP (its 8 blocks' address), 0 none */
	unsigned long node_bsp[H2_OBJANIM_MAXIMUM_NODES];
	short collision_nodes, collision_dropped;
};

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

H2_ALIGN static void h2q_normalize(float *q)
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

/* a x b (Hamilton) */
H2_ALIGN static void h2q_multiply(float const *a, float const *b, float *out)
{
	float r[4];

	r[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	r[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	r[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	r[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
	memcpy(out, r, sizeof(r));
}

/* (this engine's quaternions_interpolate_and_normalize) */
H2_ALIGN static void h2q_lerp(float const *a, float const *b, float t, float *out)
{
	float v = 1.0f - t;
	int k;

	if (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3] < 0.0f)
		t = -t;
	for (k = 0; k < 4; k++)
		out[k] = a[k] * v + b[k] * t;
	h2q_normalize(out);
}

H2_ALIGN static short compress_component(float v)
{
	v = v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v;
	return (short)(long)floorf(v * 32767.0f + 0.5f);
}

/* ---------- private code: strings */

H2_ALIGN static void string_of(struct h2_cache *cache, unsigned long id, char *out, long size)
{
	if (!h2_cache_string(cache, id, out, size))
		snprintf(out, (size_t)size, "node %lx", id & 0xFFFFFFUL);
	out[size - 1] = 0;
}

/* ---------- private code: decoding Halo 2's */

/* the nodes a flag set (bytes wide) names, in order */
H2_ALIGN static long flag_nodes(unsigned char const *flags, int bytes, long node_count, short *out)
{
	long n = 0, i;

	for (i = 0; i < node_count && i < 8 * bytes && i < 64; i++)
	{
		if (flags[i / 8] & (1 << (i % 8)))
			out[n++] = (short)i;
	}
	return n;
}

/* a codec block (size bytes at d) decoded into poses (frames x
model_nodes) for the graph's nodes the flags (rotation, translation,
scale: flag_bytes each) name, mapped to the model's by map; FALSE if it is
not one this reads - h2_animation.c's codec_decode */
H2_ALIGN static boolean codec_decode(struct pose *poses, unsigned char *sets, short frames, short model_nodes,
	unsigned char const *d, unsigned long size, unsigned char const *flags, int flag_bytes, long graph_nodes,
	short const *map, short *codec_out)
{
	short rotated[64], translated[64], scaled[64];
	long nr, nt, ns, f, i;
	unsigned char codec;

	if (size < 12)
		return size == 0;
	codec = d[0];
	nr = flag_nodes(flags, flag_bytes, graph_nodes, rotated);
	nt = flag_nodes(flags + flag_bytes, flag_bytes, graph_nodes, translated);
	ns = flag_nodes(flags + 2 * flag_bytes, flag_bytes, graph_nodes, scaled);
	if (nr != d[1] || nt != d[2] || ns != d[3])
		return FALSE;
	if (codec_out)
		*codec_out = codec;
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

			for (f = 0; f < frames && node != NONE; f++)
			{
				unsigned char const *v = d + 32 + (i * n + (codec == 1 ? 0 : f)) * quaternion_size;
				float *q = poses[f * model_nodes + node].rotation;
				int k;

				for (k = 0; k < 4; k++)
					q[k] = codec == 8 ? readf(v + 4 * k) : read16(v + 2 * k) / 32767.0f;
				h2q_normalize(q);
			}
			if (node != NONE)
				sets[node] |= 1;
		}
		for (i = 0; i < nt; i++)
		{
			short node = map[translated[i]];

			for (f = 0; f < frames && node != NONE; f++)
				memcpy(poses[f * model_nodes + node].translation, d + toff + (i * n + (codec == 1 ? 0 : f)) * 12, 12);
			if (node != NONE)
				sets[node] |= 2;
		}
		for (i = 0; i < ns; i++)
		{
			short node = map[scaled[i]];

			for (f = 0; f < frames && node != NONE; f++)
				poses[f * model_nodes + node].scale = readf(d + soff + (i * n + (codec == 1 ? 0 : f)) * 4);
			if (node != NONE)
				sets[node] |= 4;
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
				sets[node] |= (unsigned char)(1 << kind);
				for (f = 0; f < frames; f++)
				{
					long a, b;
					float t;
					struct pose *pose = &poses[f * model_nodes + node];
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
						h2q_lerp(qa, qb, t, pose->rotation);
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

/* ---------- private code: making it */

/* the model's nodes (its render model's) */
H2_ALIGN static boolean nodes_read(struct h2_objanim *anim, struct h2_cache *cache, long render_model)
{
	struct h2_cache_tag t;
	long count, i;
	unsigned long nodes;

	if (!h2_cache_tag_get(cache, render_model, &t) || !h2_cache_block(cache, t.address + MODE_NODES, MODE_NODE_SIZE, &count, &nodes) ||
		count < 1)
	{
		return FALSE;
	}
	/* (past the renderer's nodes: left off, their vertices on the root) */
	if (count > H2_OBJANIM_MAXIMUM_NODES)
		count = H2_OBJANIM_MAXIMUM_NODES;
	anim->node_count = (short)count;
	for (i = 0; i < count; i++)
	{
		unsigned char const *n = h2_cache_at(cache, nodes + MODE_NODE_SIZE * i, MODE_NODE_SIZE);
		struct node *o = &anim->nodes[i];

		if (!n)
			return FALSE;
		o->id = read32(n);
		string_of(cache, o->id, o->name, sizeof(o->name));
		/* (the node read again: the name's read may let go the piece of the
		map it lay in - under the Vita's budget, h2_cache.c's mcc_bytes) */
		if (!(n = h2_cache_at(cache, nodes + MODE_NODE_SIZE * i, MODE_NODE_SIZE)))
			return FALSE;
		o->parent = read16(n + 4);
		o->first_child = read16(n + 6);
		o->next_sibling = read16(n + 8);
		if (o->first_child >= count)
			o->first_child = -1;
		if (o->next_sibling >= count)
			o->next_sibling = -1;
		if (o->parent < -1 || o->parent >= count || o->first_child < -1 || o->next_sibling < -1)
		{
			return FALSE;
		}
		memcpy(o->translation, n + 0x0C, 12);
		memcpy(o->rotation, n + 0x18, 16);
		/* (this engine's convention: conjugated) */
		o->rotation[0] = -o->rotation[0];
		o->rotation[1] = -o->rotation[1];
		o->rotation[2] = -o->rotation[2];
		h2q_normalize(o->rotation);
		memcpy(o->inverse, n + 0x28, 52);
		o->distance = readf(n + 0x5C);
		if (getenv("HALO_H2_OBJANIM_NODES"))
			platform_log("h2 objanim nodes: %s: t %.3f %.3f %.3f q(conj) %.3f %.3f %.3f %.3f | inv s %.2f f %.3f %.3f %.3f l %.3f %.3f %.3f u %.3f %.3f %.3f p %.3f %.3f %.3f",
				o->name, o->translation[0], o->translation[1], o->translation[2], o->rotation[0], o->rotation[1], o->rotation[2],
				o->rotation[3], o->inverse[0], o->inverse[1], o->inverse[2], o->inverse[3], o->inverse[4], o->inverse[5],
				o->inverse[6], o->inverse[7], o->inverse[8], o->inverse[9], o->inverse[10], o->inverse[11], o->inverse[12]);
	}
	return TRUE;
}

/* a model node of the id, NONE none */
H2_ALIGN static short node_by_id(struct h2_objanim const *anim, unsigned long id)
{
	short i;

	for (i = 0; i < anim->node_count; i++)
	{
		if (anim->nodes[i].id == id)
			return i;
	}
	return NONE;
}

/* the graph's device position animation: "device:position", or its only
one; NONE none */
H2_ALIGN static long position_animation(struct h2_cache *cache, long count, unsigned long animations)
{
	long i;

	for (i = 0; i < count; i++)
	{
		unsigned char const *e = h2_cache_at(cache, animations + JMAD_ANIMATION_SIZE * i, JMAD_ANIMATION_SIZE);
		char name[64];

		if (e && h2_cache_string(cache, read32(e), name, sizeof(name)) && !strcmp(name, "device:position"))
			return i;
	}
	return count == 1 ? 0 : NONE;
}

/* the position animation decoded and made this engine's overlay frames */
H2_ALIGN static boolean animation_make(struct h2_objanim *anim, struct h2_cache *cache, long jmad, char *why, long why_size)
{
	struct h2_cache_tag t;
	long graph_node_count, animation_count, index, i, f, n;
	unsigned long graph_nodes, animations, size, address, sf, af, dd, comp;
	short map[64];
	unsigned char const *e, *d;
	short frames, type;
	struct pose *poses = NULL;
	unsigned char sets[H2_OBJANIM_MAXIMUM_NODES];
	boolean loops;

	if (!h2_cache_tag_get(cache, jmad, &t) ||
		!h2_cache_block(cache, t.address + JMAD_NODES, JMAD_NODE_SIZE, &graph_node_count, &graph_nodes) ||
		!h2_cache_block(cache, t.address + JMAD_ANIMATIONS, JMAD_ANIMATION_SIZE, &animation_count, &animations) ||
		graph_node_count < 1 || graph_node_count > 64)
	{
		snprintf(why, (size_t)why_size, "its animation graph cannot be read");
		return FALSE;
	}
	index = position_animation(cache, animation_count, animations);
	if (index == NONE)
	{
		snprintf(why, (size_t)why_size, "its graph has no device position animation");
		anim->tracks_only = TRUE;
		return FALSE;
	}
	/* (the graph's nodes are the model's by name) */
	for (i = 0; i < graph_node_count; i++)
	{
		unsigned char const *g = h2_cache_at(cache, graph_nodes + JMAD_NODE_SIZE * i, JMAD_NODE_SIZE);

		map[i] = g ? node_by_id(anim, read32(g)) : NONE;
	}
	e = h2_cache_at(cache, animations + JMAD_ANIMATION_SIZE * index, JMAD_ANIMATION_SIZE);
	if (!e)
		return FALSE;
	type = e[0x10];
	frames = read16(e + 0x14);
	size = read32(e + 0x28);
	address = read32(e + 0x2C);
	sf = e[0x30];
	af = e[0x31];
	dd = (unsigned short)read16(e + 0x36);
	comp = read32(e + 0x3C);
	if (frames < 1 || frames > MAXIMUM_FRAMES || type > 2 || size > MAXIMUM_ANIMATION_BYTES || dd + comp + sf + af > size ||
		(sf != 0 && sf != 12 && sf != 24) || (af != 12 && af != 24) || !(d = h2_cache_at(cache, address, size)))
	{
		snprintf(why, (size_t)why_size, "its position animation is not one this reads (frames %d, flags %lu/%lu)", frames, sf, af);
		return FALSE;
	}
	poses = calloc((unsigned long)frames * anim->node_count, sizeof(*poses));
	if (!poses)
		return FALSE;
	memset(sets, 0, sizeof(sets));
	/* (an overlay's nodes it does not set: no change; a base one's: the
	node's default, Halo 2's convention) */
	for (f = 0; f < frames; f++)
	{
		for (n = 0; n < anim->node_count; n++)
		{
			struct pose *p = &poses[f * anim->node_count + n];

			p->rotation[3] = 1.0f;
			p->scale = 1.0f;
			if (type != ANIMATION_OVERLAY)
			{
				memcpy(p->translation, anim->nodes[n].translation, 12);
				p->rotation[0] = -anim->nodes[n].rotation[0];
				p->rotation[1] = -anim->nodes[n].rotation[1];
				p->rotation[2] = -anim->nodes[n].rotation[2];
				p->rotation[3] = anim->nodes[n].rotation[3];
			}
		}
	}
	anim->codec = -1;
	if ((dd && !codec_decode(poses, sets, frames, anim->node_count, d, dd, d + dd + comp, (int)(sf / 3), graph_node_count, map, NULL)) ||
		!codec_decode(poses, sets, frames, anim->node_count, d + dd, comp, d + dd + comp + sf, (int)(af / 3), graph_node_count, map,
			&anim->codec))
	{
		free(poses);
		snprintf(why, (size_t)why_size, "its position animation's codec %d is not one this reads", d[dd]);
		return FALSE;
	}
	/* (a base animation: made the change from the node's default, as an
	overlay is - Halo 2's frame = default x change) */
	if (type != ANIMATION_OVERLAY)
	{
		for (f = 0; f < frames; f++)
		{
			for (n = 0; n < anim->node_count; n++)
			{
				struct pose *p = &poses[f * anim->node_count + n];
				float base[4], q[4];
				int k;

				base[0] = anim->nodes[n].rotation[0];
				base[1] = anim->nodes[n].rotation[1];
				base[2] = anim->nodes[n].rotation[2];
				base[3] = anim->nodes[n].rotation[3];
				h2q_multiply(base, p->rotation, q);
				memcpy(p->rotation, q, 16);
				for (k = 0; k < 3; k++)
					p->translation[k] -= anim->nodes[n].translation[k];
			}
		}
	}
	/* this engine's convention */
	for (f = 0; f < (long)frames * anim->node_count; f++)
	{
		poses[f].rotation[0] = -poses[f].rotation[0];
		poses[f].rotation[1] = -poses[f].rotation[1];
		poses[f].rotation[2] = -poses[f].rotation[2];
	}
	anim->rotation_flags = anim->translation_flags = anim->scale_flags = 0;
	for (n = 0; n < anim->node_count; n++)
	{
		if (sets[n] & 1)
			anim->rotation_flags |= 1ULL << n;
		if (sets[n] & 2)
			anim->translation_flags |= 1ULL << n;
		if (sets[n] & 4)
			anim->scale_flags |= 1ULL << n;
	}
	anim->h2_frame_count = frames;
	/* (a looping position: Halo 2's last frame its first again, left out) */
	loops = (anim->device_flags & 1) || (anim->group == 'mach' && anim->machine_type == H2_MACHINE_GEAR);
	if (loops && frames > 1)
	{
		boolean same = TRUE;

		for (n = 0; n < anim->node_count && same; n++)
		{
			struct pose const *a = &poses[n], *b = &poses[(frames - 1) * anim->node_count + n];
			float dot = a->rotation[0] * b->rotation[0] + a->rotation[1] * b->rotation[1] + a->rotation[2] * b->rotation[2] +
				a->rotation[3] * b->rotation[3];
			int k;

			if (fabsf(dot) < 0.99999f || fabsf(a->scale - b->scale) > 1e-4f)
				same = FALSE;
			for (k = 0; k < 3; k++)
			{
				if (fabsf(a->translation[k] - b->translation[k]) > 1e-4f)
					same = FALSE;
			}
		}
		if (same)
			frames--;
		/* (its last frame not its first - Ivory Tower's elevator: each
		platform rises one floor, holds, then is at its start again, the
		next one in its place -: Halo 2's position is a ratio of the frames
		less one, no frame between last and first, set_frame_ratio 0x1c6620,
		halo2-decomp src/unknown_1c62f0.cpp; played as a wrap the platform
		sank back through its rider in a third of a second) */
		else
		{
			anim->position_snaps = TRUE;
			platform_log("h2 objanim: %s: its looping position snaps from last frame to first", anim->name);
		}
	}
	anim->frame_count = frames;
	/* the frames (each node: rotation, translation, scale as its flags
	say) and the defaults (the rest) */
	anim->frame_size = 0;
	anim->defaults_size = 0;
	for (n = 0; n < anim->node_count; n++)
	{
		anim->frame_size += (anim->rotation_flags >> n & 1) ? 8 : 0;
		anim->frame_size += (anim->translation_flags >> n & 1) ? 12 : 0;
		anim->frame_size += (anim->scale_flags >> n & 1) ? 4 : 0;
		anim->defaults_size += (anim->rotation_flags >> n & 1) ? 0 : 8;
		anim->defaults_size += (anim->translation_flags >> n & 1) ? 0 : 12;
		anim->defaults_size += (anim->scale_flags >> n & 1) ? 0 : 4;
	}
	anim->frames_size = (long)anim->frame_size * frames;
	anim->frames = malloc((unsigned long)anim->frames_size + 1);
	anim->defaults = malloc((unsigned long)anim->defaults_size + 1);
	if (!anim->frames || !anim->defaults)
	{
		free(poses);
		return FALSE;
	}
	{
		unsigned char *o = anim->frames;

		for (f = 0; f < frames; f++)
		{
			for (n = 0; n < anim->node_count; n++)
			{
				struct pose const *p = &poses[f * anim->node_count + n];
				int k;

				if (anim->rotation_flags >> n & 1)
				{
					for (k = 0; k < 4; k++)
						write16(o + 2 * k, compress_component(p->rotation[k]));
					o += 8;
				}
				if (anim->translation_flags >> n & 1)
				{
					for (k = 0; k < 3; k++)
						writef(o + 4 * k, p->translation[k]);
					o += 12;
				}
				if (anim->scale_flags >> n & 1)
				{
					writef(o, p->scale);
					o += 4;
				}
			}
		}
		o = anim->defaults;
		for (n = 0; n < anim->node_count; n++)
		{
			int k;

			if (!(anim->rotation_flags >> n & 1))
			{
				for (k = 0; k < 4; k++)
					write16(o + 2 * k, compress_component(anim->nodes[n].rotation[k]));
				o += 8;
			}
			if (!(anim->translation_flags >> n & 1))
			{
				for (k = 0; k < 3; k++)
					writef(o + 4 * k, anim->nodes[n].translation[k]);
				o += 12;
			}
			if (!(anim->scale_flags >> n & 1))
			{
				writef(o, 1.0f);
				o += 4;
			}
		}
	}
	if (getenv("HALO_H2_OBJANIM_DEBUG"))
	{
		for (f = 0; f < anim->h2_frame_count; f++)
		{
			char line[1024];
			long used = 0;

			for (n = 0; n < anim->node_count && used < (long)sizeof(line) - 80; n++)
			{
				struct pose const *p = &poses[f * anim->node_count + n];

				if (!sets[n])
					continue;
				used += snprintf(line + used, sizeof(line) - (size_t)used, " %s r%.1f t(%.3f %.3f %.3f) |", anim->nodes[n].name,
					2.0 * acos(fabs(p->rotation[3]) > 1.0 ? 1.0 : fabs(p->rotation[3])) * 57.29578, p->translation[0],
					p->translation[1], p->translation[2]);
			}
			platform_log("h2 objanim: %s f%02ld:%s", anim->name, f, line);
		}
	}
	free(poses);
	anim->animated = TRUE;
	return TRUE;
}

/* (h2_objanim_variant: the permutation each region takes, by name - a
placement's model variant, h2_level.c; none, each region's first) */
static struct
{
	short count;
	char const (*regions)[32];
	char const (*permutations)[32];
} objanim_variant;

H2_ALIGN void h2_objanim_variant(
	short count,
	char const (*regions)[32],
	char const (*permutations)[32])
{
	objanim_variant.count = count;
	objanim_variant.regions = regions;
	objanim_variant.permutations = permutations;
}

/* (a collision region's - its name first - permutation the variant names,
by its index among the region's (each its name first); 0 none) */
H2_ALIGN static long variant_permutation(struct h2_cache *cache, unsigned long region, unsigned long permutations, long count)
{
	char name[64];
	short k;
	long n;

	if (!objanim_variant.count || !h2_cache_string(cache, h2_cache_read32(cache, region), name, sizeof(name)))
		return 0;
	for (k = 0; k < objanim_variant.count && strcmp(objanim_variant.regions[k], name); k++)
		;
	for (n = 0; k < objanim_variant.count && n < count; n++)
	{
		char permutation[64];

		if (h2_cache_string(cache, h2_cache_read32(cache, permutations + COLL_PERMUTATION_SIZE * (unsigned long)n), permutation,
			sizeof(permutation)) && !strcmp(permutation, objanim_variant.permutations[k]))
		{
			return n;
		}
	}
	return 0;
}

/* the collision model's BSPs: each to its node (the model's by name), a
node's largest kept */
H2_ALIGN static void collision_gather(struct h2_objanim *anim, struct h2_cache *cache, long coll)
{
	struct h2_cache_tag t;
	long region_count, node_count, r;
	unsigned long regions, nodes;
	long surfaces[H2_OBJANIM_MAXIMUM_NODES];

	memset(surfaces, 0, sizeof(surfaces));
	if (!h2_cache_tag_get(cache, coll, &t) ||
		!h2_cache_block(cache, t.address + COLL_REGIONS, COLL_REGION_SIZE, &region_count, &regions) ||
		!h2_cache_block(cache, t.address + COLL_NODES, COLL_NODE_SIZE, &node_count, &nodes))
	{
		return;
	}
	for (r = 0; r < region_count; r++)
	{
		long permutation_count, bsp_count, b;
		unsigned long permutations, bsps;

		/* (the region's first permutation: this engine's object made of a
		Halo 2 one has its model's first - or the one its placement's model
		variant names, h2_objanim_variant: 01b's broken doors) */
		if (!h2_cache_block(cache, regions + COLL_REGION_SIZE * r + 4, COLL_PERMUTATION_SIZE, &permutation_count, &permutations) ||
			permutation_count < 1)
		{
			continue;
		}
		permutations += COLL_PERMUTATION_SIZE * (unsigned long)variant_permutation(cache, regions + COLL_REGION_SIZE * r, permutations,
			permutation_count);
		if (!h2_cache_block(cache, permutations + 4, COLL_BSP_SIZE, &bsp_count, &bsps))
			continue;
		for (b = 0; b < bsp_count; b++)
		{
			unsigned char const *e = h2_cache_at(cache, bsps + COLL_BSP_SIZE * b, COLL_BSP_SIZE);
			unsigned char const *cn;
			long count;
			unsigned long ignored;
			short node, model_node;

			if (!e)
				continue;
			node = read16(e);
			if (node < 0 || node >= node_count || !(cn = h2_cache_at(cache, nodes + COLL_NODE_SIZE * node, COLL_NODE_SIZE)) ||
				(model_node = node_by_id(anim, read32(cn))) == NONE ||
				!h2_cache_block(cache, bsps + COLL_BSP_SIZE * b + COLL_BSP_SURFACES, 8, &count, &ignored))
			{
				anim->collision_dropped++;
				continue;
			}
			if (anim->node_bsp[model_node])
			{
				anim->collision_dropped++;
				if (count <= surfaces[model_node])
					continue;
			}
			else
				anim->collision_nodes++;
			anim->node_bsp[model_node] = bsps + COLL_BSP_SIZE * b + 4;
			surfaces[model_node] = count;
		}
	}
}

/* a string id's English text (the globals' locale table: shared.map's
on MCC) as this engine's prompt part: UTF-16, the private-use characters
(Halo 2's button and hold icons) and a leading "to " left out; FALSE if
there is none */
H2_ALIGN static boolean action_text(struct h2_cache *cache, unsigned long id, unsigned short *out, long size)
{
	long matg = h2_cache_tag_find(cache, 'matg', "globals\\globals");
	struct h2_cache_tag t;
	unsigned char const *g;
	unsigned long count, bytes, index_reference, data_reference, i;
	unsigned char *index = NULL, *data = NULL;
	long used = 0;
	boolean found = FALSE, hold = FALSE;

	out[0] = 0;
	if (matg == NONE || !h2_cache_tag_get(cache, matg, &t) || !(g = h2_cache_at(cache, t.address + GLOBALS_ENGLISH_LOCALE, 16)))
		return FALSE;
	count = read32(g);
	bytes = read32(g + 4);
	index_reference = read32(g + 8);
	data_reference = read32(g + 12);
	if (!count || count > MAXIMUM_LOCALE_STRINGS || !bytes || bytes > MAXIMUM_LOCALE_BYTES ||
		!(index = malloc(count * 8)) || !(data = malloc(bytes + 1)) ||
		!h2_cache_resource_read(cache, index_reference, index, count * 8) ||
		!h2_cache_resource_read(cache, data_reference, data, bytes))
	{
		free(index);
		free(data);
		return FALSE;
	}
	data[bytes] = 0;
	for (i = 0; i < count && !found; i++)
	{
		unsigned long offset = read32(index + 8 * i + 4);
		unsigned char const *c;

		if (read32(index + 8 * i) != id || offset >= bytes)
			continue;
		found = TRUE;
		/* (UTF-8 to UTF-16, the Basic Multilingual Plane's) */
		for (c = data + offset; *c && used < size - 2; )
		{
			unsigned long code;

			if (c[0] < 0x80)
				code = c[0], c += 1;
			else if ((c[0] & 0xE0) == 0xC0 && c[1])
				code = ((c[0] & 0x1FUL) << 6) | (c[1] & 0x3F), c += 2;
			else if ((c[0] & 0xF0) == 0xE0 && c[1] && c[2])
				code = ((c[0] & 0x0FUL) << 12) | ((c[1] & 0x3FUL) << 6) | (c[2] & 0x3F), c += 3;
			else
			{
				c++;
				continue;
			}
			if (code >= 0xE000 && code <= 0xF8FF)
			{
				/* (U+E47C and U+E47D: Halo 2's hold verb before the button, "Hold [E] to open
				the gate" in MCC's frames and its swap prompts; kept as a first character) */
				if (code == 0xE47C || code == 0xE47D)
					hold = TRUE;
				continue;
			}
			if (code == ' ' && used == 0)
				continue;
			out[used++] = (unsigned short)code;
		}
	}
	out[used] = 0;
	/* ("to open the gate": this engine's message says "to") */
	if (used > 3 && out[0] == 't' && out[1] == 'o' && out[2] == ' ')
		memmove(out, out + 3, (size_t)(used - 2) * sizeof(*out));
	/* (a hold: its verb's mark first, which the HUD takes off and says "Hold") */
	if (hold && used > 0)
	{
		memmove(out + 1, out, (size_t)(used + 1) * sizeof(*out));
		out[0] = 0xE47C;
	}
	free(index);
	free(data);
	return found && out[0] != 0;
}

/* ---------- public code */

/* (Halo 2's physics model: an object's model (hlmt) at 0x34, its physics
model at 0x20, the physics model's limited hinges at 0xE8 and ragdoll
constraints at 0xF0 - ragdolls_props.c reads them) */
H2_ALIGN boolean h2_objanim_swings(
	struct h2_cache *cache,
	long object_tag)
{
	struct h2_cache_tag t, model, physics;
	long count;
	unsigned long elements;

	if (getenv("HALO_H2_NO_SWINGS") || !h2_cache_tag_get(cache, object_tag, &t) ||
		!h2_cache_tag_get(cache, h2_cache_reference(cache, t.address + OBJECT_MODEL), &model) ||
		!h2_cache_tag_get(cache, h2_cache_reference(cache, model.address + 0x20), &physics))
	{
		return FALSE;
	}
	if (h2_cache_block(cache, physics.address + 0xE8, 0x84, &count, &elements) && count > 0)
		return TRUE;
	return h2_cache_block(cache, physics.address + 0xF0, 0xA0, &count, &elements) && count > 0;
}

/* a crate whose parts swing (the main crane, a hinge light, an awning):
its model's nodes and its collision node by node, a machine with no
device of its own (no power, no position, no graph): its parts are turned
only by the physics props' picture (ragdolls_props.c) */
H2_ALIGN static struct h2_objanim *objanim_make_swinging(
	struct h2_cache *cache,
	long object_tag,
	char *why,
	long why_size)
{
	struct h2_cache_tag t, model;
	struct h2_objanim *anim;
	long render_model, collision;
	int k;

	if (!h2_cache_tag_get(cache, object_tag, &t) ||
		!h2_cache_tag_get(cache, h2_cache_reference(cache, t.address + OBJECT_MODEL), &model))
	{
		snprintf(why, (size_t)why_size, "it has no model");
		return NULL;
	}
	anim = calloc(1, sizeof(*anim));
	if (!anim)
		return NULL;
	anim->tag = object_tag;
	anim->group = 'mach';
	snprintf(anim->name, sizeof(anim->name), "%s", strrchr(h2_cache_tag_name(cache, object_tag), '\\') ?
		strrchr(h2_cache_tag_name(cache, object_tag), '\\') + 1 : h2_cache_tag_name(cache, object_tag));
	for (k = 0; k < NUMBER_OF_H2_OBJANIM_SOUNDS; k++)
		anim->sounds[k] = NONE;
	anim->elevator_node = -1;
	render_model = h2_cache_reference(cache, model.address + HLMT_RENDER_MODEL);
	collision = h2_cache_reference(cache, model.address + HLMT_COLLISION_MODEL);
	if (render_model == NONE || !nodes_read(anim, cache, render_model))
	{
		snprintf(why, (size_t)why_size, "its render model's nodes cannot be read");
		h2_objanim_free(anim);
		return NULL;
	}
	if (collision != NONE)
		collision_gather(anim, cache, collision);
	if (getenv("HALO_H2_DEBUG") || getenv("HALO_H2_OBJANIM_DEBUG"))
	{
		platform_log("h2 objanim: %s (swinging crate): %d nodes, collision %d nodes (%d dropped)", anim->name,
			anim->node_count, anim->collision_nodes, anim->collision_dropped);
	}
	return anim;
}

/* a scenery whose model's graph has one looping animation (Halo 2 plays
a scenery's "idle" over and over: the multiplayer teleporters' turning
rings, objects\multi\2way_teleporter's 100 frames on its nodes bot and
top) - this engine's scenery play no animation, so it is made a gear
machine of its nodes, its position going round once each loop: the
animation at Halo 2's 30 frames a second. NULL (and why) if it has no
such graph */
H2_ALIGN static struct h2_objanim *objanim_make_idle(
	struct h2_cache *cache,
	long object_tag,
	char *why,
	long why_size)
{
	struct h2_cache_tag t, model;
	struct h2_objanim *anim;
	long render_model, collision, graph;
	int k;

	if (!h2_cache_tag_get(cache, object_tag, &t) ||
		!h2_cache_tag_get(cache, h2_cache_reference(cache, t.address + OBJECT_MODEL), &model))
	{
		snprintf(why, (size_t)why_size, "it has no model");
		return NULL;
	}
	graph = h2_cache_reference(cache, model.address + HLMT_ANIMATION);
	render_model = h2_cache_reference(cache, model.address + HLMT_RENDER_MODEL);
	collision = h2_cache_reference(cache, model.address + HLMT_COLLISION_MODEL);
	if (graph == NONE || render_model == NONE)
	{
		snprintf(why, (size_t)why_size, "a scenery with no animation graph");
		return NULL;
	}
	anim = calloc(1, sizeof(*anim));
	if (!anim)
		return NULL;
	anim->tag = object_tag;
	anim->group = 'mach';
	anim->machine_type = H2_MACHINE_GEAR;
	anim->device_flags = 1;     /* (its position loops) */
	anim->elevator_node = -1;
	snprintf(anim->name, sizeof(anim->name), "%s", strrchr(h2_cache_tag_name(cache, object_tag), '\\') ?
		strrchr(h2_cache_tag_name(cache, object_tag), '\\') + 1 : h2_cache_tag_name(cache, object_tag));
	for (k = 0; k < NUMBER_OF_H2_OBJANIM_SOUNDS; k++)
		anim->sounds[k] = NONE;
	if (!nodes_read(anim, cache, render_model) || anim->node_count < 2 || anim->node_count > H2_OBJANIM_MAXIMUM_ANIMATED_NODES)
	{
		snprintf(why, (size_t)why_size, "a scenery of one node (nothing to turn)");
		h2_objanim_free(anim);
		return NULL;
	}
	anim->has_graph = TRUE;
	if (!animation_make(anim, cache, graph, why, why_size) || !anim->animated || anim->h2_frame_count < 2)
	{
		h2_objanim_free(anim);
		return NULL;
	}
	/* (once round each loop of its frames at 30 a second) */
	anim->position_speed = (float)TICKS_A_SECOND / (float)(anim->h2_frame_count - 1);
	anim->power_speed = 1.0f;
	if (collision != NONE)
		collision_gather(anim, cache, collision);
	if (getenv("HALO_H2_DEBUG") || getenv("HALO_H2_OBJANIM_DEBUG"))
	{
		platform_log("h2 objanim: %s (scenery, its idle looped): %d nodes, %d frames, position %.3f/s, collision %d nodes",
			anim->name, anim->node_count, anim->h2_frame_count, anim->position_speed, anim->collision_nodes);
	}
	return anim;
}

/* (h2_level.c: a campaign level's build) */
boolean h2_objanim_scenery_nodes;

/* a scenery of nodes on a campaign level (hev, Scarab, tentacles, ships'
parts): no device, its nodes alone, for a cutscene's animation to pose
(h2_cinematics.c: 03a's Scarab walks by its 36 nodes). NULL if it has no
nodes to pose */
H2_ALIGN static struct h2_objanim *objanim_make_nodes(
	struct h2_cache *cache,
	long object_tag,
	char *why,
	long why_size)
{
	struct h2_cache_tag t, model;
	struct h2_objanim *anim;
	long render_model;

	if (!h2_cache_tag_get(cache, object_tag, &t) || !h2_cache_tag_get(cache, h2_cache_reference(cache, t.address + OBJECT_MODEL), &model) ||
		h2_cache_reference(cache, model.address + HLMT_ANIMATION) == NONE ||
		(render_model = h2_cache_reference(cache, model.address + HLMT_RENDER_MODEL)) == NONE)
	{
		return NULL;
	}
	anim = calloc(1, sizeof(*anim));
	if (!anim)
		return NULL;
	anim->tag = object_tag;
	anim->group = 'scen';
	anim->elevator_node = -1;
	if (!nodes_read(anim, cache, render_model) || anim->node_count < 2)
	{
		h2_objanim_free(anim);
		return NULL;
	}
	why[0] = 0;
	return anim;
}

H2_ALIGN struct h2_objanim *h2_objanim_make(
	struct h2_cache *cache,
	long object_tag,
	char *why,
	long why_size)
{
	struct h2_cache_tag t, model;
	struct h2_objanim *anim;
	unsigned char const *p;
	long render_model, collision, graph;
	int k;

	why[0] = 0;
	/* (a crate whose parts swing: a machine of its nodes, not animated) */
	if (h2_cache_tag_get(cache, object_tag, &t) && t.group_tag == 'bloc' && h2_objanim_swings(cache, object_tag))
		return objanim_make_swinging(cache, object_tag, why, why_size);
	/* (a scenery: its looping animation, if it has one) */
	if (h2_cache_tag_get(cache, object_tag, &t) && t.group_tag == 'scen')
	{
		struct h2_objanim *idle = getenv("HALO_H2_NO_SCENERY_ANIM") ? NULL : objanim_make_idle(cache, object_tag, why, why_size);

		if (idle)
			return idle;
		return h2_objanim_scenery_nodes && !getenv("HALO_H2_NO_SCENERY_NODES") ? objanim_make_nodes(cache, object_tag, why, why_size) : NULL;
	}
	if (!h2_cache_tag_get(cache, object_tag, &t) || (t.group_tag != 'mach' && t.group_tag != 'ctrl'))
	{
		snprintf(why, (size_t)why_size, "not a machine or control");
		return NULL;
	}
	if (!(p = h2_cache_at(cache, t.address, MINIMUM_DEVICE_SIZE)) ||
		!h2_cache_tag_get(cache, h2_cache_reference(cache, t.address + OBJECT_MODEL), &model))
	{
		snprintf(why, (size_t)why_size, "it has no model");
		return NULL;
	}
	anim = calloc(1, sizeof(*anim));
	if (!anim)
		return NULL;
	anim->tag = object_tag;
	anim->group = t.group_tag;
	snprintf(anim->name, sizeof(anim->name), "%s", strrchr(h2_cache_tag_name(cache, object_tag), '\\') ?
		strrchr(h2_cache_tag_name(cache, object_tag), '\\') + 1 : h2_cache_tag_name(cache, object_tag));
	/* the device */
	anim->device_flags = read32(p + DEVICE_FLAGS);
	anim->power_speed = readf(p + DEVICE_POWER_SPEED);
	anim->power_acceleration = readf(p + DEVICE_POWER_ACCELERATION);
	anim->position_speed = readf(p + DEVICE_POSITION_SPEED);
	anim->position_acceleration = readf(p + DEVICE_POSITION_ACCELERATION);
	anim->depowered_speed = readf(p + DEVICE_DEPOWERED_SPEED);
	anim->depowered_acceleration = readf(p + DEVICE_DEPOWERED_ACCELERATION);
	anim->delay_time = readf(p + DEVICE_DELAY_TIME);
	anim->activation_radius = readf(p + DEVICE_ACTIVATION_RADIUS);
	for (k = 0; k < NUMBER_OF_H2_OBJANIM_SOUNDS; k++)
		anim->sounds[k] = NONE;
	for (k = 0; k <= H2_OBJANIM_SOUND_REPOWERED; k++)
		anim->sounds[k] = h2_cache_reference(cache, t.address + DEVICE_SOUNDS + 8 * k);
	if (t.group_tag == 'mach')
	{
		anim->machine_type = read16(p + MACHINE_TYPE);
		anim->machine_flags = (unsigned short)read16(p + MACHINE_FLAGS);
		anim->door_open_time = readf(p + MACHINE_DOOR_OPEN_TIME);
		anim->bounding_radius = readf(p + OBJECT_BOUNDING_RADIUS);
		for (k = 0; k < 3; k++)
			anim->bounding_offset[k] = readf(p + OBJECT_BOUNDING_OFFSET + 4 * k);
		anim->collision_response = read16(p + MACHINE_COLLISION_RESPONSE);
		anim->elevator_node = read16(p + MACHINE_ELEVATOR_NODE);
		anim->collision_damage_scale = readf(p + OBJECT_APPLY_COLLISION_DAMAGE_SCALE);
		/* (a scale of 1 is every ordinary machine's in MCC's tags - Ivory
		Tower's elevator, Zanzibar's wheel and gates: Halo 2's contact is
		collision damage by speed times it, nothing a slow machine does. Only
		Terminal's trains, 100, kill by touch: h2_machine_riders.c) */
		if (!(anim->collision_damage_scale > 1.0f && anim->collision_damage_scale < 1e6f))
			anim->collision_damage_scale = 0.0f;
	}
	else
	{
		anim->control_type = read16(p + CONTROL_TYPE);
		anim->triggers_when = read16(p + CONTROL_TRIGGERS_WHEN);
		anim->call_value = readf(p + CONTROL_CALL_VALUE);
		for (k = 0; k < 3; k++)
			anim->sounds[H2_OBJANIM_SOUND_ON + k] = h2_cache_reference(cache, t.address + CONTROL_SOUNDS + 8 * k);
		if (action_text(cache, read32(p + CONTROL_ACTION_STRING), anim->action_text, H2_OBJANIM_ACTION_TEXT_SIZE) &&
			(getenv("HALO_H2_DEBUG") || getenv("HALO_H2_OBJANIM_DEBUG")))
		{
			char ascii[H2_OBJANIM_ACTION_TEXT_SIZE];

			for (k = 0; k < H2_OBJANIM_ACTION_TEXT_SIZE; k++)
				ascii[k] = anim->action_text[k] < 0x80 ? (char)anim->action_text[k] : '?';
			platform_log("h2 objanim: %s: its prompt \"%s\"", anim->name, ascii);
		}
	}
	/* its model's nodes, graph and collision */
	render_model = h2_cache_reference(cache, model.address + HLMT_RENDER_MODEL);
	collision = h2_cache_reference(cache, model.address + HLMT_COLLISION_MODEL);
	graph = h2_cache_reference(cache, model.address + HLMT_ANIMATION);
	if (render_model == NONE || !nodes_read(anim, cache, render_model))
	{
		snprintf(why, (size_t)why_size, "its render model's nodes cannot be read");
		h2_objanim_free(anim);
		return NULL;
	}
	anim->has_graph = graph != NONE;
	if (graph != NONE && !animation_make(anim, cache, graph, why, why_size))
		platform_log("h2 objanim: %s: %s (not animated)", anim->name, why);
	if (collision != NONE)
		collision_gather(anim, cache, collision);
	if (getenv("HALO_H2_DEBUG") || getenv("HALO_H2_OBJANIM_DEBUG"))
	{
		platform_log("h2 objanim: %s (%s): %d nodes, %s, position %.3f/s%s, collision %d nodes (%d dropped)", anim->name,
			anim->group == 'mach' ? "machine" : "control", anim->node_count, anim->animated ? "animated" : "not animated",
			anim->position_speed, anim->animated && anim->frame_count < anim->h2_frame_count ? " looping" : "",
			anim->collision_nodes, anim->collision_dropped);
	}
	return anim;
}

H2_ALIGN void h2_objanim_free(
	struct h2_objanim *anim)
{
	if (!anim)
		return;
	free(anim->frames);
	free(anim->defaults);
	free(anim);
}

H2_ALIGN short h2_objanim_object_type(
	struct h2_objanim const *anim)
{
	return anim->group == 'ctrl' ? 8 : anim->group == 'scen' ? 6 : 7;
}

H2_ALIGN unsigned long h2_objanim_group(
	struct h2_objanim const *anim)
{
	return anim->group;
}

H2_ALIGN short h2_objanim_node_count(
	struct h2_objanim const *anim)
{
	return anim->node_count;
}

H2_ALIGN boolean h2_objanim_has_graph(
	struct h2_objanim const *anim)
{
	return anim->animated;
}

H2_ALIGN boolean h2_objanim_has_collision(
	struct h2_objanim const *anim)
{
	/* (not a machine whose graph could not be read: a door that never
	opens would close its way for good - it stays walk-through. A machine
	whose graph reads but has no position animation keeps it, node by node:
	its scripts' tracks pose its nodes (h2_cinematics.c), and its collision
	moves with them - 03b's Scarab, walked on, boarded and fought on, as
	Halo 2's per-node collision BSPs ride their animated nodes) */
	return anim->collision_nodes > 0 && (anim->animated || !anim->has_graph || anim->tracks_only);
}

H2_ALIGN long h2_objanim_sound(
	struct h2_objanim const *anim,
	short which)
{
	return which >= 0 && which < NUMBER_OF_H2_OBJANIM_SOUNDS ? anim->sounds[which] : NONE;
}

H2_ALIGN void h2_objanim_report(
	struct h2_objanim const *anim,
	struct h2_objanim_report *report)
{
	short n;

	memset(report, 0, sizeof(*report));
	report->node_count = anim->node_count;
	report->frame_count = anim->frame_count;
	report->h2_frame_count = anim->h2_frame_count;
	report->codec = anim->codec;
	for (n = 0; n < anim->node_count; n++)
	{
		report->rotated += (short)(anim->rotation_flags >> n & 1);
		report->translated += (short)(anim->translation_flags >> n & 1);
		report->scaled += (short)(anim->scale_flags >> n & 1);
	}
	report->collision_nodes = anim->collision_nodes;
	report->collision_dropped = anim->collision_dropped;
	report->animation_bytes = anim->frames_size + anim->defaults_size;
	report->position_speed = anim->position_speed;
	report->animated = anim->animated;
	report->loops = anim->animated && anim->frame_count < anim->h2_frame_count;
	report->control = anim->group == 'ctrl';
	report->machine_type = anim->machine_type;
}

H2_ALIGN void h2_objanim_write_model_nodes(
	struct h2_objanim const *anim,
	struct h2_objanim_host const *host,
	unsigned long model_root)
{
	unsigned long nodes = host->block(host->context, model_root + 0xB8, anim->node_count, H2_OBJANIM_CE_MODEL_NODE_SIZE);
	short i;

	for (i = 0; i < anim->node_count && !host->failed(host->context); i++)
	{
		unsigned char *o = host->at(host->context, nodes + H2_OBJANIM_CE_MODEL_NODE_SIZE * i);
		struct node const *n = &anim->nodes[i];

		memcpy(o, n->name, 31);
		write16(o + 0x20, n->next_sibling);
		write16(o + 0x22, n->first_child);
		write16(o + 0x24, n->parent);
		memcpy(o + 0x28, n->translation, 12);
		memcpy(o + 0x34, n->rotation, 16);
		writef(o + 0x44, n->distance);
		memcpy(o + 0x68, n->inverse, 52);
	}
}

H2_ALIGN unsigned long h2_objanim_write_graph(
	struct h2_objanim const *anim,
	struct h2_objanim_host const *host)
{
	unsigned long graph, devices, nodes, animations, indices;
	unsigned char *o;
	short i;

	if (!anim->animated)
		return 0;
	graph = host->alloc(host->context, CE_GRAPH_SIZE);
	devices = host->block(host->context, graph + CE_GRAPH_DEVICES, 1, CE_DEVICE_ANIMATIONS_SIZE);
	nodes = host->block(host->context, graph + CE_GRAPH_NODES, anim->node_count, CE_GRAPH_NODE_SIZE);
	animations = host->block(host->context, graph + CE_GRAPH_ANIMATIONS, 1, CE_ANIMATION_SIZE);
	if (host->failed(host->context))
		return 0;
	/* (the device's animations: position the first, power none) */
	indices = host->block(host->context, devices + 0x54, 2, 2);
	if (host->failed(host->context))
		return 0;
	write16(host->at(host->context, indices), 0);
	write16(host->at(host->context, indices + 2), -1);
	for (i = 0; i < anim->node_count; i++)
	{
		o = host->at(host->context, nodes + CE_GRAPH_NODE_SIZE * i);
		memcpy(o, anim->nodes[i].name, 31);
		write16(o + 0x20, anim->nodes[i].next_sibling);
		write16(o + 0x22, anim->nodes[i].first_child);
		write16(o + 0x24, anim->nodes[i].parent);
	}
	o = host->at(host->context, animations);
	memcpy(o, "device position", 16);
	write16(o + 0x20, ANIMATION_OVERLAY);
	write16(o + 0x22, anim->frame_count);
	write16(o + 0x24, anim->frame_size);
	write16(o + 0x2C, anim->node_count);
	writef(o + 0x30, 1.0f);
	write16(o + 0x38, -1);              /* next animation */
	write16(o + 0x3C, -1);              /* sound */
	o[0x40] = o[0x41] = 0xFF;           /* feet */
	write16(o + 0x42, -1);
	writef(o + 0x44, 1.0f);
	write32(o + 0x5C, (unsigned long)anim->translation_flags);
	write32(o + 0x60, (unsigned long)(anim->translation_flags >> 32));
	write32(o + 0x6C, (unsigned long)anim->rotation_flags);
	write32(o + 0x70, (unsigned long)(anim->rotation_flags >> 32));
	write32(o + 0x7C, (unsigned long)anim->scale_flags);
	write32(o + 0x80, (unsigned long)(anim->scale_flags >> 32));
	host->data(host->context, animations + 0x8C, anim->defaults, (unsigned long)anim->defaults_size);
	host->data(host->context, animations + 0xA0, anim->frames, (unsigned long)anim->frames_size);
	return host->failed(host->context) ? 0 : graph;
}

H2_ALIGN unsigned long h2_objanim_write_collision(
	struct h2_objanim const *anim,
	struct h2_objanim_host const *host)
{
	static unsigned long const collision_nulls[] = { 0x70, 0x84, 0x98, 0xA8, 0xBC, 0x188, 0x198, 0x1A8 };
	unsigned long root, field, nodes;
	unsigned char *o;
	short i;
	int k;

	if (!anim->collision_nodes)
		return 0;
	/* h2_level.c's instances' collision models', with the model's nodes */
	root = host->alloc(host->context, CE_COLLISION_SIZE);
	if (host->failed(host->context))
		return 0;
	for (k = 0; k < (int)(sizeof(collision_nulls) / sizeof(collision_nulls[0])); k++)
		host->reference(host->context, root + collision_nulls[k], 0, 0xFFFFFFFFUL);
	writef(host->at(host->context, root + 0x08), 1.0f);
	field = host->block(host->context, root + 0x234, 1, CE_COLLISION_MATERIAL_SIZE);
	if (host->failed(host->context))
		return 0;
	o = host->at(host->context, field);
	memcpy(o, "default", 8);
	write16(o + 0x24, 7);
	writef(o + 0x2C, 1.0f);
	writef(o + 0x3C, 1.0f);
	field = host->block(host->context, root + 0x240, 1, CE_COLLISION_REGION_SIZE);
	if (host->failed(host->context))
		return 0;
	memcpy(host->at(host->context, field), "base", 5);
	host->reference(host->context, field + 0x38, 0, 0xFFFFFFFFUL);
	{
		unsigned long permutation = host->block(host->context, field + 0x48, 1, CE_COLLISION_PERMUTATION_SIZE);

		if (host->failed(host->context))
			return 0;
		memcpy(host->at(host->context, permutation), "__base", 7);
	}
	nodes = host->block(host->context, root + 0x28C, anim->node_count, CE_COLLISION_NODE_SIZE);
	for (i = 0; i < anim->node_count && !host->failed(host->context); i++)
	{
		struct node const *n = &anim->nodes[i];

		o = host->at(host->context, nodes + CE_COLLISION_NODE_SIZE * i);
		memcpy(o, n->name, 31);
		write16(o + 0x20, anim->node_bsp[i] ? 0 : -1);
		write16(o + 0x22, n->parent);
		write16(o + 0x24, n->next_sibling);
		write16(o + 0x26, n->first_child);
		if (anim->node_bsp[i])
		{
			unsigned long bsp = host->block(host->context, nodes + CE_COLLISION_NODE_SIZE * i + 0x34, 1, CE_COLLISION_BSP_SIZE);

			if (host->failed(host->context) || !host->collision_convert(host->context, bsp, anim->node_bsp[i], 0))
				return 0;
		}
	}
	return host->failed(host->context) ? 0 : root;
}

/* a speed (units a second) as this engine's velocity a tick, an
acceleration (units a second squared) a tick squared: none, at once */
H2_ALIGN static float tick_velocity(float speed)
{
	return speed > 0.0f ? speed / TICKS_A_SECOND : 1.0f;
}

H2_ALIGN static float tick_acceleration(float acceleration)
{
	float a = acceleration > 0.0f ? acceleration / (TICKS_A_SECOND * TICKS_A_SECOND) : 1.0f;

	return a > 1.0f ? 1.0f : a;
}

H2_ALIGN void h2_objanim_write_device(
	struct h2_objanim const *anim,
	struct h2_objanim_host const *host,
	unsigned long root)
{
	unsigned char *o = host->at(host->context, root);
	boolean loops = (anim->device_flags & 1) || (anim->group == 'mach' && anim->machine_type == H2_MACHINE_GEAR);
	int k;

	/* the device: its position loops (a gear's always: this engine's
	turns a gear by its position), the times, the runtime rates */
	write32(o + CE_DEVICE, (loops ? 1 : 0) | (anim->position_snaps ? 1UL << H2_DEVICE_POSITION_SNAPS_BIT : 0));
	writef(o + CE_DEVICE + 0x04, anim->power_speed > 0.0f ? 1.0f / anim->power_speed : 0.0f);
	writef(o + CE_DEVICE + 0x08, anim->power_acceleration > 0.0f && anim->power_speed > 0.0f ? anim->power_speed / anim->power_acceleration : 0.0f);
	writef(o + CE_DEVICE + 0x0C, anim->position_speed > 0.0f ? 1.0f / anim->position_speed : 0.0f);
	writef(o + CE_DEVICE + 0x10, anim->position_acceleration > 0.0f && anim->position_speed > 0.0f ? anim->position_speed / anim->position_acceleration : 0.0f);
	writef(o + CE_DEVICE + 0x14, anim->depowered_speed > 0.0f ? 1.0f / anim->depowered_speed : 0.0f);
	writef(o + CE_DEVICE + 0x18, anim->depowered_acceleration > 0.0f && anim->depowered_speed > 0.0f ? anim->depowered_speed / anim->depowered_acceleration : 0.0f);
	writef(o + 0x200, anim->delay_time);
	writef(o + 0x21C, anim->activation_radius);
	writef(o + 0x274, tick_acceleration(anim->power_acceleration));
	writef(o + 0x278, tick_velocity(anim->power_speed));
	writef(o + 0x27C, tick_acceleration(anim->depowered_acceleration));
	writef(o + 0x280, anim->depowered_speed > 0.0f ? anim->depowered_speed / TICKS_A_SECOND : 0.0f);
	writef(o + 0x284, tick_acceleration(anim->position_acceleration));
	writef(o + 0x288, tick_velocity(anim->position_speed));
	writef(o + 0x28C, anim->delay_time * TICKS_A_SECOND);
	/* (the sounds' hook: this engine's effects of Halo 2's sounds, the
	start, stop, depowered and repowered effects) */
	if (host->effect_for_sound)
	{
		for (k = 0; k <= H2_OBJANIM_SOUND_REPOWERED; k++)
		{
			unsigned long id = anim->sounds[k] != NONE ? host->effect_for_sound(host->context, anim->sounds[k]) : 0xFFFFFFFFUL;

			host->reference(host->context, root + 0x1A0 + 0x10 * k, 'effe', id);
		}
	}
	o = host->at(host->context, root);
	if (anim->group == 'mach')
	{
		write16(o + CE_MACHINE, anim->machine_type);
		write16(o + CE_MACHINE + 0x02, anim->machine_flags & 7);
		writef(o + CE_MACHINE + 0x04, anim->door_open_time);
		write16(o + 0x2E8, anim->collision_response);
		write16(o + 0x2EA, (anim->machine_flags & 4) && anim->elevator_node >= 0 && anim->elevator_node < anim->node_count ?
			anim->elevator_node : -1);
		write32(o + 0x320, (unsigned long)(long)(anim->door_open_time * TICKS_A_SECOND));
		/* (the first of the machine's unused words: h2_machine_riders.c's
		collision damage) */
		writef(o + CE_MACHINE + 0x08, anim->collision_damage_scale);
		/* (the next: Halo 2's own bounding sphere, radius then offset - this
		engine's object sphere is the geometry's, h2_level.c; Halo 2's door
		looks for units in the definition's, device_machines.c) */
		writef(o + CE_MACHINE + 0x0C, anim->bounding_radius);
		for (k = 0; k < 3; k++)
			writef(o + CE_MACHINE + 0x10 + 4 * k, anim->bounding_offset[k]);
	}
	else
	{
		write16(o + CE_CONTROL, anim->control_type);
		write16(o + CE_CONTROL + 0x02, anim->triggers_when);
		writef(o + CE_CONTROL + 0x04, anim->call_value);
		for (k = 0; k < 3; k++)
		{
			unsigned long id = host->effect_for_sound && anim->sounds[H2_OBJANIM_SOUND_ON + k] != NONE ?
				host->effect_for_sound(host->context, anim->sounds[H2_OBJANIM_SOUND_ON + k]) : 0xFFFFFFFFUL;

			host->reference(host->context, root + 0x2E8 + 0x10 * k, 'effe', id);
		}
	}
}

H2_ALIGN void h2_objanim_placement_read(
	struct h2_cache *cache,
	unsigned long address,
	struct h2_objanim_placement *placement)
{
	unsigned char const *p = h2_cache_at(cache, address + PLACEMENT_POWER_GROUP, 12);

	memset(placement, 0, sizeof(*placement));
	placement->power_group = placement->position_group = NONE;
	if (!p)
		return;
	placement->device = TRUE;
	placement->power_group = read16(p);
	placement->position_group = read16(p + 2);
	placement->device_flags = read32(p + 4);
	placement->flags = read32(p + 8);
}

H2_ALIGN void h2_objanim_placement_write(
	struct h2_objanim_placement const *placement,
	boolean control,
	unsigned char *o)
{
	if (!placement->device)
	{
		write16(o + 0x28, -1);
		write16(o + 0x2A, -1);
		return;
	}
	write16(o + 0x28, placement->power_group);
	write16(o + 0x2A, placement->position_group);
	/* (initially open, initially off, can change only once, position
	reversed, not usable from any side: the same bits) */
	write32(o + 0x2C, placement->device_flags & 0x1F);
	if (control)
		write16(o + 0x30, (long)(placement->flags & 1));    /* usable from both sides */
	else
	{
		/* does not operate automatically, one-sided, never appears locked,
		opened by melee attack: the same bits; Halo 2's bit 5 "does not close
		automatically" this engine's bit 4, its bit 4 "one-sided for player"
		this engine's bit 5 (devices.h). Every device here
		has a position group (a runtime one when the scenario gives none),
		so a door-type machine closed again door_open_time after it opened
		unless the bit was kept: Cairo's gun racks, opened by 1st_mission,
		shut 2 s later (owner's report: the gun holders never animate) */
		o[0x30] = (unsigned char)((placement->flags & 0x0F) | ((placement->flags >> 5) & 1) << 4 | ((placement->flags >> 4) & 1) << 5);
	}
}

H2_ALIGN long h2_objanim_write_device_groups(
	struct h2_cache *cache,
	struct h2_objanim_host const *host,
	unsigned long field)
{
	struct h2_cache_tag scenario;
	long count, i;
	unsigned long groups, out;

	if (!h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) ||
		!h2_cache_block(cache, scenario.address + SCENARIO_DEVICE_GROUPS, SCENARIO_DEVICE_GROUP_SIZE, &count, &groups) || count <= 0)
	{
		return 0;
	}
	if (count > 128)
		count = 128;
	out = host->block(host->context, field, count, H2_OBJANIM_CE_DEVICE_GROUP_SIZE);
	for (i = 0; i < count && !host->failed(host->context); i++)
	{
		unsigned char const *g = h2_cache_at(cache, groups + SCENARIO_DEVICE_GROUP_SIZE * i, SCENARIO_DEVICE_GROUP_SIZE);
		unsigned char *o = host->at(host->context, out + H2_OBJANIM_CE_DEVICE_GROUP_SIZE * i);

		if (!g)
			continue;
		memcpy(o, g, 31);
		o[31] = 0;
		writef(o + 0x20, readf(g + 0x20));
		write32(o + 0x24, read32(g + 0x24) & 1);    /* can change only once */
	}
	return count;
}

H2_ALIGN boolean h2_objanim_has_action_text(
	struct h2_objanim const *anim)
{
	return anim->group == 'ctrl' && anim->action_text[0] != 0;
}

H2_ALIGN unsigned long h2_objanim_write_action_texts(
	struct h2_objanim const *const *anims,
	long count,
	struct h2_objanim_host const *host)
{
	unsigned long root = host->alloc(host->context, CE_STRING_LIST_SIZE), strings;
	long i, n = 0;

	for (i = 0; i < count; i++)
		n += anims[i] && h2_objanim_has_action_text(anims[i]) ? 1 : 0;
	strings = host->block(host->context, root, n, CE_STRING_SIZE);
	for (i = 0, n = 0; i < count && !host->failed(host->context); i++)
	{
		unsigned char bytes[2 * H2_OBJANIM_ACTION_TEXT_SIZE];
		long length, k;

		if (!anims[i] || !h2_objanim_has_action_text(anims[i]))
			continue;
		for (length = 0; anims[i]->action_text[length]; length++)
			;
		/* (UTF-16, its terminator too) */
		for (k = 0; k <= length; k++)
			write16(bytes + 2 * k, anims[i]->action_text[k]);
		host->data(host->context, strings + CE_STRING_SIZE * n++, bytes, (unsigned long)(2 * (length + 1)));
	}
	return host->failed(host->context) ? 0 : root;
}

/* ---------- (runtime) the devices' sounds */

boolean (*h2_objanim_sound_hook)(char const *machine, int event, real_point3d const *position) = h2_sound_machine_event;

void h2_objanim_device_event(
	long device_index,
	short event)
{
	struct object_datum *object;
	char const *name;
	static char const prefix[] = "h2\\objects\\";

	static int debug = -1;
	boolean played;

	if (debug < 0)
		debug = getenv("HALO_H2_OBJANIM_DEBUG") != NULL;
	if ((!h2_objanim_sound_hook && !debug) || device_index == NONE || !(object = object_try_and_get(device_index)))
		return;
	/* (a control's own position follows its group as the machines' do: its
	sound is its press alone) */
	if (object->object.type == _object_type_control && event != H2_OBJANIM_EVENT_PRESSED)
		return;
	name = tag_get_name(object->definition_index);
	if (!name || strncmp(name, prefix, sizeof(prefix) - 1) != 0)
		return;
	/* (its middle: a machine's origin can be in the floor, outside the
	structure's clusters, where a sound is not heard) */
	played = h2_objanim_sound_hook && h2_objanim_sound_hook(name + sizeof(prefix) - 1, event, &object->object.bounding_sphere_center);
	if (debug)
		platform_log("h2 objanim: %s: event %d at %.2f %.2f %.2f: %s", name + sizeof(prefix) - 1, (int)event,
			object->object.bounding_sphere_center.x, object->object.bounding_sphere_center.y, object->object.bounding_sphere_center.z,
			!h2_objanim_sound_hook ? "no sound hook" : played ? "sound played" : "no sound");
}
