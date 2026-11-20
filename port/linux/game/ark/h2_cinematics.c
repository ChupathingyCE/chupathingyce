/*
H2_CINEMATICS.C

port: a Halo 2 level's cinematics played by its scripts (h2_scripts.c):
the camera's animations (camera_set_animation_relative), the objects'
(custom_animation_relative, custom_animation(_loop), scenery_animation_start
(_relative, _loop)), read from the player's own map as they are asked for
(h2_animation_decode_raw) and played a frame a tick.

A cinematic camera's graph (objects\characters\cinematic_camera\...) has one
node, the camera: its place each frame is the camera's, relative to the
anchor the script names (a cutscene flag: its position and facing). An
object's animation moves the object as its graph's root node moves,
relative to its anchor (an object: its place when the animation starts);
the object is moved whole (this engine's objects keep their own animation:
a character's limbs are not posed yet). The camera's field of view is the
script's (camera_set_field_of_view), 60 degrees by default.

The axes: a node's rotation (Halo 2's quaternion) turns its x axis to the
camera's forward and its z axis to its up.
*/

/* ---------- headers */

#include "cseries.h"
#include "h2_scripts.h"
#include "h2_cache.h"
#include "h2_animation.h"
#include "h2_build_heap.h"
#include "h2_biped.h"
#include "h2_level.h"
#include "game/game.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "game/players.h"
#include "objects/object_definitions.h"
#include "models/model_definitions.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"
#include "devices/devices.h"
#include "physics/collisions.h"
#include "h2_render_nodes.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stddef.h>
#ifdef HALO_LINUX
#include "render_epoch.h"

#endif

/* (perf/ce-floor-3) TRUE once a face, an emotion or an object animation was
ever taken (the counts h2_cinematics_node_matrices tests are only raised by
those three, below): objects.c calls it only from then on */
boolean h2_cinematics_ever;

#undef malloc
#undef free
#undef realloc
#undef calloc

#define H2_ALIGN __attribute__((force_align_arg_pointer))

void platform_log(char const *format, ...);
long game_time_get(void);

/* ---------- constants */

enum
{
	JMAD_NODES = 0x0C,
	JMAD_NODE_SIZE = 0x20,
	JMAD_ANIMATIONS = 0x2C,
	JMAD_ANIMATION_SIZE = 0x60,
	MAXIMUM_OBJECT_ANIMATIONS = 96,
	CUTSCENE_FLAG_SIZE = 0x5C,
};

/* ---------- structures */

/* a place: a point, its axes and its scale (an animation's node scale -
x01's capital ship 0.019 of its model, the matte paintings a fifth: the
space shots' sets are models shrunk about the camera) */
struct place
{
	real_point3d position;
	real_vector3d forward;
	real_vector3d left;
	real_vector3d up;
	/* (its children's places scaled by it - Cortana's hologram in 01b's
	x02 too; 0 is 1) */
	real scale;
};

/* an animation's root node, a place a frame (relative to its anchor); and
every node's pose a frame (Halo 2's, relative to its parent), the nodes'
names and what the animation sets of each (bit 0 rotation, 1 translation) */
/* (the nodes' poses are decoded a window of WINDOW_FRAMES frames at a time
from the animation's compressed data, kept in source: decoded whole they
took frames x nodes x 32 bytes a track - 1.4 MB for a character of 55
nodes over 780 frames, 17 MB for Cairo's ceremony - and the Vita's C heap
refused them: "cannot be played", the Arbiter gone from his trial, the
Grunts, Marines, Miranda and Cortana standing in their rest pose) */
enum { WINDOW_FRAMES = 64 };
struct track
{
	/* the root's place at every frame; a graph of nodes fills them a
	window at a time as the frames are asked for (frames_done a bit a
	window; NULL: all filled at the decode) */
	struct place *frames;
	unsigned char *frames_done;
	short frame_count;
	struct h2_animation_pose *poses;        /* the window: window_count frames from window_first */
	short window_first, window_count;
	struct h2_animation_pose *poses0;       /* frame 0's */
	struct h2_animation_source *source;
	unsigned char *sets;
	short node_count;
	/* (a frame's scale not 1: the object's scale is the animation's) */
	boolean scaled;
	char (*node_names)[32];
};

struct object_animation
{
	long object_index;
	/* (a test's: the root kept where its first frame is) */
	boolean in_place;
	/* (the object's model's nodes: the animation's node of each, NONE) */
	short *node_map;
	short node_map_count;
	struct track track;
	struct place anchor;
	long start_time;
	boolean loop;
	/* (a unit in a cutscene: spared the scene's blasts until its track ends) */
	boolean spared;
	/* (a device's position track - device_set_position_track: 03a's and
	03b's Scarabs walking -: its frame the device's position, not the time;
	never let go while the device is there) */
	boolean device_driven;
	/* (the track before it, blended out over the track's interpolation
	seconds: each model node's local pose then - halo2-decompiled
	devices.cpp 0x107ed0, function_ba350) */
	long blend_start, blend_ticks;
	float (*blend_rotation)[4];
	float (*blend_translation)[3];
	short blend_count;
	/* (a device's overlay track - device_set_overlay_track: 03b's Scarab's
	main gun opening, firing, closing -: Halo 2's channel 1, its poses
	deltas over the track's - rotations multiplied, translations added
	(halo2-decompiled unknown_279d80.cpp, blend method 2) -, its frame the
	overlay's own position, device_animate_overlay's) */
	struct track overlay;
	short *overlay_map;
	real overlay_from, overlay_to;
	long overlay_start, overlay_ticks;
	/* (an object attached to another - 03b's Scarab pilots, hooked to its
	seats while they idle in their custom animation -: the parent's node
	last tick, its anchor carried as that node moves) */
	long carrier_index;
	short carrier_node;
	real_matrix4x3 carrier_matrix;
	/* (its anchor where the object was - custom_animation(_loop), not
	relative to a cutscene's flag or object: only such an anchor is carried
	by a parent or the deck under it; a cutscene's riders are animated in
	the scene's own frame with their vehicle, and carrying them too would
	move them twice) */
	boolean anchored_here;
};

/* ---------- globals */

static struct
{
	boolean camera_active;
	struct track camera;
	struct place camera_anchor;
	long camera_start;
	char camera_name[48];
	struct
	{
		real separation, near_bounds[2], far_bounds[2], near_ticks, far_ticks;
		long near_start, far_start;
	} dof;
	long crossfade_start;
	real crossfade_ticks, crossfade_exponent;
	real field_of_view, fov_from;
	long fov_start;
	short fov_ticks;
	struct object_animation objects[MAXIMUM_OBJECT_ANIMATIONS];
	long object_count;
	long decoded, failed, objects_missing;
	boolean test_in_place;
} h2_cine;

/* ---------- the faces: Halo 2's lip-sync

A line of dialogue (sound_impulse_start on a character) moves its speaker's
face as Halo 2's does: the sound's extra info (the 'snd!' tag's at 0xE: an
index into the sound gestalt's block 14, 0x14C an element - eight bytes,
then a geometry block info, 0x24, a language each: the first English) is a
block of sounds_en.dat ('blkh', its section, then its resources: the
encoded data, type 1; the dialogue info, type 0 at 8 - the mouth data's
offset and length, the lip-sync data's offset and length). The lip-sync
data: its phonemes' count (23), a word, a scale (and its inverse); each
phoneme's keys' count and first; the keys, a time and a weight each (signed
words over the scale: seconds, about -1..1). Each phoneme is a frame of the speaker's
graph's "phonemes" overlay; its weight now, along its keys, scales that
frame's change to each face node (the overlay's rule: the node's own,
times the change; its translation, plus the change's) */

enum
{
	FACE_MAXIMUM = 16,
	FACE_PHONEMES = 32,
	FACE_KEYS = 1024,
	FACE_EXTRA_INFOS = 14,
	FACE_EXTRA_INFO_SIZE = 0x14C,
};

struct face
{
	long object_index;
	long start_time;
	float seconds;
	short phoneme_count;
	short key_first[FACE_PHONEMES], key_count[FACE_PHONEMES];
	short key_total;
	float key_time[FACE_KEYS], key_weight[FACE_KEYS];
	/* the phonemes overlay: its frames' changes, its nodes' names */
	struct h2_animation_pose *poses;
	short node_count, frame_count;
	char (*node_names)[32];
	/* (the object's model's nodes: the overlay's node of each, NONE) */
	short *node_map;
	short node_map_count;
};

static struct
{
	struct face faces[FACE_MAXIMUM];
	long count;
	long started, failed;
} h2_faces;

/* ---------- the faces: Halo 2's emotions

unit_set_emotional_state (unit, emotion, weight, ticks): the character's
graph's "emotions" overlay, two frames an emotion - the emotion's
(Halo 2's emotion types: none, asleep(?), amorous, happy, inquisitive,
repulsed, disappointed, shocked, scared, arrogant, annoyed, angry, pensive,
pain - the AI dialogue globals' speaker emotion values and the engine's
string ids, in that order) less one, times two -, its first frame's change
(its second's while the unit speaks a line: the pair differ in the jaw)
to each of the overlay's nodes, the weight reached over the ticks */

static char const *const emotion_names[] =
{
	"none", "asleep", "amorous", "happy", "inquisitive", "repulsed", "disappointed", "shocked", "scared", "arrogant",
	"annoyed", "angry", "pensive", "pain",
};

struct emotion
{
	long object_index;
	short frame;
	float weight_from, weight_to;
	long start_time, ticks;
	struct h2_animation_pose *poses;
	short node_count, frame_count;
	short *node_map;
	short node_map_count;
};

static struct
{
	struct emotion emotions[FACE_MAXIMUM];
	long count;
} h2_emotions;

/* ---------- private code */

/* (debug switches read once: getenv walks the environment, and these were
asked once a pose - 17000 times for one cutscene animation's decode, every
node of every animated object a tick) */
static boolean env_switch(char const *name, signed char *cached)
{
	if (*cached < 0)
		*cached = getenv(name) != NULL;
	return *cached != 0;
}

static signed char env_conjugate = -1, env_no_scale = -1, env_test_defaults = -1;
#define ENV_CONJUGATE() env_switch("HALO_H2_CINEMATIC_CONJUGATE", &env_conjugate)
#define ENV_NO_SCALE() env_switch("HALO_H2_CINEMATIC_NO_SCALE", &env_no_scale)
#define ENV_TEST_DEFAULTS() env_switch("HALO_H2_CINEMATIC_TEST_DEFAULTS", &env_test_defaults)

static void place_from_quaternion(float const q[4], float const t[3], struct place *place)
{
	float x = q[0], y = q[1], z = q[2], w = q[3];
	float n = x * x + y * y + z * z + w * w;

	if (ENV_CONJUGATE())
		x = -x, y = -y, z = -z;
	if (n > 0.0f)
	{
		float s = 1.0f / sqrtf(n);

		x *= s, y *= s, z *= s, w *= s;
	}
	else
		w = 1.0f;
	/* (the rotation's columns: its x, y and z axes) */
	place->forward.i = 1.0f - 2.0f * (y * y + z * z);
	place->forward.j = 2.0f * (x * y + w * z);
	place->forward.k = 2.0f * (x * z - w * y);
	place->left.i = 2.0f * (x * y - w * z);
	place->left.j = 1.0f - 2.0f * (x * x + z * z);
	place->left.k = 2.0f * (y * z + w * x);
	place->up.i = 2.0f * (x * z + w * y);
	place->up.j = 2.0f * (y * z - w * x);
	place->up.k = 1.0f - 2.0f * (x * x + y * y);
	place->position.x = t[0], place->position.y = t[1], place->position.z = t[2];
	place->scale = 1.0f;
}

/* child (relative to parent) made absolute */
static void place_compose(struct place const *parent, struct place const *child, struct place *out)
{
	struct place r;

	real parent_scale = parent->scale > 0.0f ? parent->scale : 1.0f;
	real child_scale = child->scale > 0.0f ? child->scale : 1.0f;

#define AXIS(v) \
	{ parent->forward.i * (v).i + parent->left.i * (v).j + parent->up.i * (v).k, \
	  parent->forward.j * (v).i + parent->left.j * (v).j + parent->up.j * (v).k, \
	  parent->forward.k * (v).i + parent->left.k * (v).j + parent->up.k * (v).k }
	{
		real_vector3d f = child->forward, l = child->left, u = child->up;
		real_vector3d p;

		p.i = child->position.x * parent_scale, p.j = child->position.y * parent_scale, p.k = child->position.z * parent_scale;
		{
			real fi[3] = AXIS(f), li[3] = AXIS(l), ui[3] = AXIS(u), pi[3] = AXIS(p);

			r.forward.i = fi[0], r.forward.j = fi[1], r.forward.k = fi[2];
			r.left.i = li[0], r.left.j = li[1], r.left.k = li[2];
			r.up.i = ui[0], r.up.j = ui[1], r.up.k = ui[2];
			r.position.x = parent->position.x + pi[0];
			r.position.y = parent->position.y + pi[1];
			r.position.z = parent->position.z + pi[2];
		}
	}
#undef AXIS
	r.scale = parent_scale * child_scale;
	*out = r;
}

/* a cutscene flag's place (its facing: yaw and pitch) */
static boolean flag_place(short flag_index, struct place *place)
{
	struct scenario *scenario = global_scenario_get();
	unsigned char const *flag;
	float yaw, pitch;

	if (flag_index < 0 || flag_index >= scenario->cutscene_flags.count)
		return FALSE;
	flag = (unsigned char const *)scenario->cutscene_flags.address + CUTSCENE_FLAG_SIZE * flag_index;
	memcpy(&place->position, flag + 0x24, sizeof(place->position));
	memcpy(&yaw, flag + 0x30, 4);
	memcpy(&pitch, flag + 0x34, 4);
	place->forward.i = cosf(yaw) * cosf(pitch), place->forward.j = sinf(yaw) * cosf(pitch), place->forward.k = sinf(pitch);
	place->left.i = -sinf(yaw), place->left.j = cosf(yaw), place->left.k = 0.0f;
	place->up.i = -cosf(yaw) * sinf(pitch), place->up.j = -sinf(yaw) * sinf(pitch), place->up.k = cosf(pitch);
	place->scale = 1.0f;
	return TRUE;
}

/* an object's place */
static boolean object_place(long object_index, struct place *place)
{
	struct object_datum *object = object_index != NONE ? object_try_and_get(object_index) : NULL;

	if (!object)
		return FALSE;
	object_get_origin(object_index, &place->position);
	place->forward = object->object.forward;
	place->up = object->object.up;
	place->left.i = place->up.j * place->forward.k - place->up.k * place->forward.j;
	place->left.j = place->up.k * place->forward.i - place->up.i * place->forward.k;
	place->left.k = place->up.i * place->forward.j - place->up.j * place->forward.i;
	place->scale = 1.0f;
	return TRUE;
}

static void track_free(struct track *track)
{
	free(track->frames);
	free(track->frames_done);
	free(track->poses);
	free(track->poses0);
	h2_animation_source_close(track->source);
	free(track->sets);
	free(track->node_names);
	memset(track, 0, sizeof(*track));
}

/* a window of WINDOW_FRAMES poses from an aligned first frame decoded into
the track's window (under window_lock), and - a graph of nodes whose root
places are filled as asked for - the window's root places made from them,
once (frames_done), as the decode's first pass made them: the same codec
call over the same aligned window, frame by frame the same places */
static boolean window_load(struct track *track, long first)
{
	short count = (short)MIN(WINDOW_FRAMES, track->frame_count - first);

	if (!track->source || !track->poses || !h2_animation_source_decode(track->source, (short)first, count, track->poses, track->sets))
		return FALSE;
	track->window_first = (short)first;
	track->window_count = count;
	if (track->frames_done && !(track->frames_done[first / WINDOW_FRAMES / 8] & (1 << ((first / WINDOW_FRAMES) & 7))))
	{
		long f, n;

		for (f = 0; f < count; f++)
		{
			struct h2_animation_pose const *pose = &track->poses[(long)track->node_count * f];

			place_from_quaternion(pose->rotation, pose->translation, &track->frames[first + f]);
			if (pose->scale > 0.0001f && pose->scale < 10000.0f && fabsf(pose->scale - 1.0f) > 0.0001f && !ENV_NO_SCALE())
			{
				track->frames[first + f].scale = pose->scale;
				track->scaled = TRUE;
			}
			for (n = 0; n < track->node_count && !track->scaled && !ENV_NO_SCALE(); n++)
			{
				if ((track->sets[n] & 4) && fabsf(pose[n].scale - 1.0f) > 0.0001f)
					track->scaled = TRUE;
			}
		}
		track->frames_done[first / WINDOW_FRAMES / 8] |= (unsigned char)(1 << ((first / WINDOW_FRAMES) & 7));
	}
	return TRUE;
}

/* the pose of node k at a frame, copied to out: the window moved there
when the frame is outside it (an aligned window of WINDOW_FRAMES).
(Under a lock: the node matrices are computed on the tick's thread and,
for a drawn frame between ticks, the game's) */
static pthread_mutex_t window_lock = PTHREAD_MUTEX_INITIALIZER;

static void track_pose(struct track *track, long frame, long k, struct h2_animation_pose *out)
{
	if (frame < 0)
		frame = 0;
	if (frame >= track->frame_count)
		frame = track->frame_count - 1;
	pthread_mutex_lock(&window_lock);
	if (frame < track->window_first || frame >= track->window_first + track->window_count)
	{
		if (!window_load(track, frame - frame % WINDOW_FRAMES))
		{
			*out = track->poses0[k];
			pthread_mutex_unlock(&window_lock);
			return;
		}
	}
	*out = track->poses[(frame - track->window_first) * track->node_count + k];
	pthread_mutex_unlock(&window_lock);
}

/* the root's place at a frame (its window of places filled first when it
is not yet) */
static struct place const *track_place(struct track *track, long frame)
{
	if (frame < 0)
		frame = 0;
	if (frame >= track->frame_count)
		frame = track->frame_count - 1;
	if (track->frames_done && !(track->frames_done[frame / WINDOW_FRAMES / 8] & (1 << ((frame / WINDOW_FRAMES) & 7))))
	{
		pthread_mutex_lock(&window_lock);
		window_load(track, frame - frame % WINDOW_FRAMES);
		pthread_mutex_unlock(&window_lock);
	}
	return &track->frames[frame];
}

/* every window's root places filled (the kept copies' check compares them all) */
static void track_fill_all(struct track *track)
{
	long first;

	for (first = 0; track->frames_done && first < track->frame_count; first += WINDOW_FRAMES)
		track_place(track, first);
}

/* (port) the tracks decoded lately, kept: a looping animation a
cutscene's Marines start again and again (marine_fist_loop 53 nodes x 177
frames, 158-167 ms on the Vita each time) and the same animation on two
objects decode once. Copies of the decode, by the level's cache, the
graph's handle and the name, the latest used kept within KEPT_BYTES;
emptied with the level (h2_cinematics_dispose) */
enum { KEPT_TRACKS = 16, KEPT_BYTES = 2 << 20 };
static struct
{
	struct
	{
		void const *cache;
		long graph_handle;
		char name[64];
		struct track track;
		unsigned long bytes, used;
	} tracks[KEPT_TRACKS];
	unsigned long bytes, clock, hits;
} kept;

static unsigned long track_bytes(struct track const *track)
{
	unsigned long bytes = sizeof(struct place) * (unsigned long)track->frame_count;

	if (track->poses)
	{
		short nodes, frames, type;
		unsigned long data = 0;

		if (track->source)
			h2_animation_source_figures(track->source, &nodes, &frames, &type, &data);
		bytes += sizeof(struct h2_animation_pose) * (unsigned long)track->node_count * (WINDOW_FRAMES + 1) + data +
			(unsigned long)track->node_count + 1 + 32 * (unsigned long)track->node_count;
	}
	return bytes;
}

/* a copy of from in to, whole or not at all */
static boolean track_copy(struct track *to, struct track const *from)
{
	memset(to, 0, sizeof(*to));
	to->frame_count = from->frame_count;
	to->node_count = from->node_count;
	to->scaled = from->scaled;
	to->frames = malloc(sizeof(struct place) * (size_t)from->frame_count);
	if (to->frames)
		memcpy(to->frames, from->frames, sizeof(struct place) * (size_t)from->frame_count);
	if (from->frames_done)
	{
		size_t bits = ((size_t)from->frame_count / WINDOW_FRAMES + 8) / 8;

		to->frames_done = malloc(bits);
		if (!to->frames_done)
		{
			track_free(to);
			return FALSE;
		}
		memcpy(to->frames_done, from->frames_done, bits);
	}
	if (from->poses)
	{
		size_t window = sizeof(struct h2_animation_pose) * (size_t)from->node_count * WINDOW_FRAMES;

		to->poses = malloc(window);
		to->poses0 = malloc(sizeof(struct h2_animation_pose) * (size_t)from->node_count);
		to->source = h2_animation_source_copy(from->source);
		to->sets = malloc((size_t)from->node_count + 1);
		to->node_names = malloc(32 * (size_t)from->node_count);
		if (to->poses && from->window_count > 0)
		{
			memcpy(to->poses, from->poses, sizeof(struct h2_animation_pose) * (size_t)from->node_count * (size_t)from->window_count);
			to->window_first = from->window_first, to->window_count = from->window_count;
		}
		if (to->poses0)
			memcpy(to->poses0, from->poses0, sizeof(struct h2_animation_pose) * (size_t)from->node_count);
		if (to->sets)
			memcpy(to->sets, from->sets, (size_t)from->node_count + 1);
		if (to->node_names)
			memcpy(to->node_names, from->node_names, 32 * (size_t)from->node_count);
	}
	if (!to->frames || (from->poses && (!to->poses || !to->poses0 || !to->source || !to->sets || !to->node_names)))
	{
		track_free(to);
		return FALSE;
	}
	return TRUE;
}

static void kept_empty(void)
{
	long i;

	for (i = 0; i < KEPT_TRACKS; i++)
		track_free(&kept.tracks[i].track);
	memset(&kept, 0, sizeof(kept));
}

static boolean kept_find(void const *cache, long graph_handle, char const *name, struct track *track)
{
	long i;

	for (i = 0; i < KEPT_TRACKS; i++)
	{
		if (kept.tracks[i].track.frames && kept.tracks[i].cache == cache && kept.tracks[i].graph_handle == graph_handle &&
			!strcmp(kept.tracks[i].name, name))
		{
			if (!track_copy(track, &kept.tracks[i].track))
				return FALSE;
			kept.tracks[i].used = ++kept.clock;
			kept.hits++;
			return TRUE;
		}
	}
	return FALSE;
}

static void kept_add(void const *cache, long graph_handle, char const *name, struct track const *track)
{
	unsigned long bytes = track_bytes(track);
	long i, slot;

	if (bytes > KEPT_BYTES / 2 || strlen(name) >= sizeof(kept.tracks[0].name))
		return;
	for (;;)
	{
		long oldest = NONE, empty = NONE;

		for (i = 0; i < KEPT_TRACKS; i++)
		{
			if (!kept.tracks[i].track.frames)
			{
				if (empty == NONE)
					empty = i;
			}
			else if (oldest == NONE || kept.tracks[i].used < kept.tracks[oldest].used)
				oldest = i;
		}
		if (empty != NONE && kept.bytes + bytes <= KEPT_BYTES)
		{
			slot = empty;
			break;
		}
		if (oldest == NONE)
			return;
		kept.bytes -= kept.tracks[oldest].bytes;
		track_free(&kept.tracks[oldest].track);
	}
	if (!track_copy(&kept.tracks[slot].track, track))
		return;
	kept.tracks[slot].cache = cache;
	kept.tracks[slot].graph_handle = graph_handle;
	strcpy(kept.tracks[slot].name, name);
	kept.tracks[slot].bytes = bytes;
	kept.tracks[slot].used = ++kept.clock;
	kept.bytes += bytes;
}

/* a graph's (Halo 2's handle) animation named name, its root node's
places; FALSE if it is not there */
unsigned long long vita_host_time_us(void) __attribute__((weak));

/* (a decode of over 50 ms logged: the search by name and the decode timed,
"h2 cinematics: slow decode") */
static unsigned long long decode_now(void)
{
	return vita_host_time_us ? vita_host_time_us() : 0;
}

/* ---------- the graphs' animation names, read once a level

Finding an animation by name read every name of its graph through the
cache (h2_cache_string: the string table's chunk inflated again after the
cache's trim between actors): 15-30 ms an actor on the Vita (johnson_01
"found 0 of 32 by name in 29.6 ms"). The names of a graph are read once
and kept for the level (a few dozen graphs, ~4 KB each) */

enum { NAME_GRAPHS = 32, NAME_LENGTH = 128 };

static struct
{
	struct
	{
		void const *cache;
		long jmad;
		long count;
		char (*names)[NAME_LENGTH];
		unsigned long used;
	} graphs[NAME_GRAPHS];
	unsigned long clock;
} name_index;

static void name_index_empty(void)
{
	long i;

	for (i = 0; i < NAME_GRAPHS; i++)
		free(name_index.graphs[i].names);
	memset(&name_index, 0, sizeof(name_index));
}

/* the animation of the name in the graph (its animations block at
animations, count of them), NONE */
static long animation_by_name(struct h2_cache *cache, long jmad, unsigned long animations, long count, char const *name)
{
	long g, slot = NONE, i;

	for (g = 0; g < NAME_GRAPHS && slot == NONE; g++)
	{
		/* (the graph's tag index and its count alone: the level's map is
		opened afresh at a BSP switch during play - h2_cache.h - and a
		graph's names are the map's, not the handle's) */
		if (name_index.graphs[g].names && name_index.graphs[g].jmad == jmad && name_index.graphs[g].count == count)
			slot = g;
	}
	if (slot == NONE)
	{
		long oldest = 0;
		char (*names)[NAME_LENGTH] = count > 0 ? calloc((size_t)count, NAME_LENGTH) : NULL;

		if (!names)
		{
			/* (no room: the search as before) */
			for (i = 0; i < count; i++)
			{
				char text[NAME_LENGTH];

				if (h2_cache_string(cache, h2_cache_read32(cache, animations + JMAD_ANIMATION_SIZE * i), text, sizeof(text)) && !strcmp(text, name))
					return i;
			}
			return NONE;
		}
		for (i = 0; i < count; i++)
			if (!h2_cache_string(cache, h2_cache_read32(cache, animations + JMAD_ANIMATION_SIZE * i), names[i], NAME_LENGTH))
				names[i][0] = 0;
		for (g = 0; g < NAME_GRAPHS; g++)
		{
			if (!name_index.graphs[g].names)
			{
				oldest = g;
				break;
			}
			if (name_index.graphs[g].used < name_index.graphs[oldest].used)
				oldest = g;
		}
		free(name_index.graphs[oldest].names);
		name_index.graphs[oldest].names = names;
		name_index.graphs[oldest].cache = cache;
		name_index.graphs[oldest].jmad = jmad;
		name_index.graphs[oldest].count = count;
		slot = oldest;
	}
	name_index.graphs[slot].used = ++name_index.clock;
	for (i = 0; i < count; i++)
		if (!strcmp(name_index.graphs[slot].names[i], name))
			return i;
	return NONE;
}

H2_ALIGN static boolean track_decode_fresh(long graph_handle, char const *name, struct track *track)
{
	struct h2_cache *cache = h2_scripts_cache();
	struct h2_cache_tag tag;
	long jmad, count, i, animation = NONE, first;
	unsigned long animations, bytes = 0;
	struct h2_animation_source *source;
	short node_count = 0, frame_count = 0, type = 0;
	unsigned long long started = decode_now(), searched = 0, opened = 0;
	static int slow_logged;
	char const *why = NULL;

	memset(track, 0, sizeof(*track));
	if (!cache || !name || (jmad = h2_cache_tag_from_handle(cache, (unsigned long)graph_handle)) == NONE ||
		!h2_cache_tag_get(cache, jmad, &tag) || !h2_cache_block(cache, tag.address + JMAD_ANIMATIONS, JMAD_ANIMATION_SIZE, &count, &animations))
	{
		h2_cine.failed++;
		return FALSE;
	}
	animation = animation_by_name(cache, jmad, animations, count, name);
	searched = decode_now();
	source = animation != NONE ? h2_animation_source_open(cache, jmad, animation) : NULL;
	if (source)
		h2_animation_source_figures(source, &node_count, &frame_count, &type, &bytes);
	if (!source || node_count < 1 || frame_count < 1)
	{
		platform_log("h2 cinematics: %s of %s cannot be played (%s)", name, h2_cache_tag_name(cache, jmad),
			animation == NONE ? "no animation of the name" : !source ? "its data could not be read or kept" : "no nodes or frames");
		h2_animation_source_close(source);
		h2_cine.failed++;
		return FALSE;
	}
	opened = decode_now();
	track->source = source;
	track->node_count = node_count;
	track->frames = calloc((size_t)frame_count, sizeof(struct place));
	track->poses = malloc(sizeof(struct h2_animation_pose) * (size_t)node_count * WINDOW_FRAMES);
	track->poses0 = malloc(sizeof(struct h2_animation_pose) * (size_t)node_count);
	track->sets = calloc((size_t)node_count + 1, 1);
	track->node_names = malloc(32 * (size_t)node_count);
	/* (a graph of nodes: the first window's root places now, the rest as
	the frames are asked for - window_load, track_place -: a scene's dozen
	actors took 10-59 ms each here on the Vita for places the scene reads
	a window at a time anyway; a root alone - a camera's, a scenery's -
	is filled whole, its source let go below) */
	if (node_count > 1 && !(track->frames_done = calloc(((size_t)frame_count / WINDOW_FRAMES + 8) / 8, 1)))
		why = "no memory for its frames";
	if (!track->frames || !track->poses || !track->poses0 || !track->sets || !track->node_names)
		why = "no memory for its frames";
	/* (every frame once, a window at a time: the root's places, frame 0's
	poses, whether any node is scaled; the last window stays - a graph of
	nodes: its first window alone) */
	for (first = 0; !why && first < frame_count && (first == 0 || !track->frames_done); first += WINDOW_FRAMES)
	{
		short window = (short)MIN(WINDOW_FRAMES, frame_count - first);
		long f, n;

		if (!h2_animation_source_decode(source, (short)first, window, track->poses, track->sets))
		{
			why = "its data is not of a form this reads";
			break;
		}
		track->window_first = (short)first;
		track->window_count = window;
		if (first == 0)
			memcpy(track->poses0, track->poses, sizeof(struct h2_animation_pose) * (size_t)node_count);
		for (f = 0; f < window; f++)
		{
			struct h2_animation_pose const *pose = &track->poses[(long)node_count * f];

			place_from_quaternion(pose->rotation, pose->translation, &track->frames[first + f]);
			/* (its root's scale, HALO_H2_CINEMATIC_NO_SCALE: none) */
			if (pose->scale > 0.0001f && pose->scale < 10000.0f && fabsf(pose->scale - 1.0f) > 0.0001f &&
				!ENV_NO_SCALE())
			{
				track->frames[first + f].scale = pose->scale;
				track->scaled = TRUE;
			}
			/* (any node scaled: its matrices' scales the animation's) */
			for (n = 0; n < node_count && !track->scaled && !ENV_NO_SCALE(); n++)
			{
				if ((track->sets[n] & 4) && fabsf(pose[n].scale - 1.0f) > 0.0001f)
					track->scaled = TRUE;
			}
		}
	}
	if (!why && track->frames_done)
		track->frames_done[0] |= 1;
	if (why)
	{
		platform_log("h2 cinematics: %s of %s cannot be played (%s)", name, h2_cache_tag_name(cache, jmad), why);
		track_free(track);
		h2_cine.failed++;
		return FALSE;
	}
	track->frame_count = frame_count;
	/* (a graph of nodes: the nodes' names; a root alone - a scenery's, a
	camera's - keeps its places only) */
	if (node_count > 1)
	{
		unsigned long nodes;
		long node_block_count;

		memset(track->node_names, 0, 32 * (size_t)node_count);
		if (h2_cache_block(cache, tag.address + JMAD_NODES, JMAD_NODE_SIZE, &node_block_count, &nodes))
		{
			for (i = 0; i < node_count && i < node_block_count; i++)
				h2_cache_string(cache, h2_cache_read32(cache, nodes + JMAD_NODE_SIZE * i), track->node_names[i], 32);
		}
	}
	else
	{
		free(track->poses), track->poses = NULL;
		free(track->poses0), track->poses0 = NULL;
		free(track->node_names), track->node_names = NULL;
		h2_animation_source_close(track->source), track->source = NULL;
		track->window_first = track->window_count = 0;
	}
	h2_cine.decoded++;
	if (started && decode_now() - started > 50000 && slow_logged++ < 32)
		platform_log("h2 cinematics: slow decode %s: %.1f ms (found %ld of %ld by name in %.1f ms, %d nodes x %d frames, %lu KB of Halo 2's data read in %.1f ms, decoded once in windows of %d in %.1f ms)",
			name, (decode_now() - started) / 1000.0, animation, count, (searched - started) / 1000.0, (int)node_count, (int)frame_count,
			bytes >> 10, (opened - searched) / 1000.0, (int)WINDOW_FRAMES, (decode_now() - opened) / 1000.0);
	return TRUE;
}

/* (the kept copy when there is one: HALO_H2_CINEMATIC_KEPT_CHECK=1 decodes
it again and compares, byte for byte) */
H2_ALIGN static boolean track_decode(long graph_handle, char const *name, struct track *track)
{
	struct h2_cache *cache = h2_scripts_cache();
	static signed char check = -1;

	if (check < 0)
		check = getenv("HALO_H2_CINEMATIC_KEPT_CHECK") != NULL;
	if (cache && name && kept_find(cache, graph_handle, name, track))
	{
		h2_cine.decoded++;
		if (check)
		{
			struct track fresh;
			boolean same;

			track_fill_all(track);
			if (track_decode_fresh(graph_handle, name, &fresh))
				track_fill_all(&fresh);
			else
				memset(&fresh, 0, sizeof(fresh));
			same = fresh.frames && fresh.frame_count == track->frame_count &&
				fresh.node_count == track->node_count && fresh.scaled == track->scaled &&
				!memcmp(fresh.frames, track->frames, sizeof(struct place) * (size_t)track->frame_count) &&
				!fresh.poses == !track->poses &&
				(!fresh.poses || (!memcmp(fresh.poses0, track->poses0, sizeof(struct h2_animation_pose) * (size_t)track->node_count) &&
					h2_animation_source_same(fresh.source, track->source) && !memcmp(fresh.sets, track->sets, (size_t)track->node_count) &&
					!memcmp(fresh.node_names, track->node_names, 32 * (size_t)track->node_count)));

			platform_log("h2 cinematics: kept %s: %s the decode", name, same ? "the same as" : "NOT THE SAME AS");
			track_free(&fresh);
		}
		return TRUE;
	}
	if (!track_decode_fresh(graph_handle, name, track))
		return FALSE;
	if (cache)
		kept_add(cache, graph_handle, name, track);
	return TRUE;
}

static long track_frame(struct track const *track, long start, boolean loop)
{
	long frame = game_time_get() - start;

	if (frame < 0)
		frame = 0;
	if (loop && track->frame_count > 0)
		return frame % track->frame_count;
	return frame < track->frame_count ? frame : track->frame_count - 1;
}

/* an animated object's frame now: a device's position track's at its
position (0 the first frame, 1 the last), else the time's */
static long animation_frame(struct object_animation const *a)
{
	if (a->device_driven && a->track.frame_count > 0)
	{
		real position = object_try_and_get_and_verify_type(a->object_index, _object_mask_device) ?
			device_get_position(a->object_index) : 0.0f;
		long frame;

		if (!(position > 0.0f))
			position = 0.0f;
		if (position > 1.0f)
			position = 1.0f;
		frame = (long)(position * (real)(a->track.frame_count - 1) + 0.5f);
		return MIN(frame, (long)a->track.frame_count - 1);
	}
	return track_frame(&a->track, a->start_time, a->loop);
}

/* (a device's position track: the frame its position falls on, and how far
toward the next - Halo 2 plays a device's animation at its position, between
frames: 06b's Key, a 31-frame track 1131 units long, moved 37.7 units at once
every 8 seconds when its frame was rounded, and the players on it - the
machine riders follow a move of a unit at most - were left behind and fell
through the city) */
static long animation_frame_between(struct object_animation const *a, real *toward)
{
	real position, at;
	long frame;

	*toward = 0.0f;
	if (!a->device_driven || a->track.frame_count < 2 || HALO_ENV_ONCE("HALO_H2_DEVICE_FRAME_ROUNDED"))
		return animation_frame(a);
	position = object_try_and_get_and_verify_type(a->object_index, _object_mask_device) ?
		device_get_position(a->object_index) : 0.0f;
	if (!(position > 0.0f))
		position = 0.0f;
	if (position > 1.0f)
		position = 1.0f;
	at = position * (real)(a->track.frame_count - 1);
	frame = (long)at;
	if (frame >= a->track.frame_count - 1)
		return a->track.frame_count - 1;
	/* (the next frame in the decoded window only: across a window's edge the
	two would load in turn, node by node - the pose holds there a frame) */
	if ((frame + 1) / WINDOW_FRAMES != frame / WINDOW_FRAMES)
		return frame;
	*toward = at - (real)frame;
	return frame;
}

/* the pose of node k between frame and the next, toward of the way */
static void track_pose_between(struct track *track, long frame, real toward, long k, struct h2_animation_pose *out)
{
	struct h2_animation_pose next;
	float dot, sign;
	short c;

	track_pose(track, frame, k, out);
	if (!(toward > 0.0001f))
		return;
	track_pose(track, frame + 1, k, &next);
	dot = out->rotation[0] * next.rotation[0] + out->rotation[1] * next.rotation[1] + out->rotation[2] * next.rotation[2] +
		out->rotation[3] * next.rotation[3];
	sign = dot < 0.0f ? -1.0f : 1.0f;
	for (c = 0; c < 4; c++)
		out->rotation[c] = out->rotation[c] * (1.0f - toward) + next.rotation[c] * sign * toward;
	{
		float length = sqrtf(out->rotation[0] * out->rotation[0] + out->rotation[1] * out->rotation[1] +
			out->rotation[2] * out->rotation[2] + out->rotation[3] * out->rotation[3]);

		if (length > 0.0001f)
			for (c = 0; c < 4; c++)
				out->rotation[c] /= length;
	}
	for (c = 0; c < 3; c++)
		out->translation[c] = out->translation[c] * (1.0f - toward) + next.translation[c] * toward;
	out->scale = out->scale * (1.0f - toward) + next.scale * toward;
}

/* a device's overlay's frame now: its position, moving from where it was to
where device_animate_overlay sent it over its seconds */
static long overlay_frame(struct object_animation const *a)
{
	real position = a->overlay_to, t;

	if (a->overlay_ticks > 0)
	{
		t = (real)(game_time_get() - a->overlay_start) / (real)a->overlay_ticks;
		if (t < 0.0f)
			t = 0.0f;
		if (t < 1.0f)
			position = a->overlay_from + (a->overlay_to - a->overlay_from) * t;
	}
	if (!(position > 0.0f))
		position = 0.0f;
	if (position > 1.0f)
		position = 1.0f;
	return a->overlay.frame_count > 0 ? MIN((long)(position * (real)(a->overlay.frame_count - 1) + 0.5f), (long)a->overlay.frame_count - 1) : 0;
}


/* ---------- private code: the faces */

H2_ALIGN static void face_free(struct face *face)
{
	/* (h2_animation_decode_raw's: the build's heap's if a build was on where
	it was decoded - its free knows) */
	h2_build_heap_free(face->poses);
	free(face->node_names);
	free(face->node_map);
	memset(face, 0, sizeof(*face));
	face->object_index = NONE;
}

/* a face's slot let go, the ones after it moved down (a line that could not
start on a face it had - an existing slot, not the last -: the last slot was
dropped, its face's memory with it, and the failed one kept) */
H2_ALIGN static void face_drop(struct face *face)
{
	long index = (long)(face - h2_faces.faces);

	if (index < 0 || index >= h2_faces.count)
		return;
	face_free(face);
	memmove(&h2_faces.faces[index], &h2_faces.faces[index + 1], sizeof(h2_faces.faces[0]) * (size_t)(h2_faces.count - index - 1));
	h2_faces.count--;
}

H2_ALIGN static unsigned long face_u32(unsigned char const *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

H2_ALIGN static unsigned short face_u16(unsigned char const *p)
{
	return (unsigned short)(p[0] | (p[1] << 8));
}

/* the lip-sync data parsed into the face's keys; FALSE if it is not one */
H2_ALIGN static boolean face_lipsync_read(struct face *face, unsigned char const *data, unsigned long size)
{
	float scale;
	short i, count;
	unsigned long keys_at;

	if (size < 16)
		return FALSE;
	count = (short)face_u16(data);
	memcpy(&scale, data + 8, 4);
	if (count < 1 || count > FACE_PHONEMES || !(scale > 1.0f && scale < 1.0e7f) || 16UL + 4UL * (unsigned long)count > size)
		return FALSE;
	keys_at = 16 + 4 * (unsigned long)count;
	face->phoneme_count = count;
	face->key_total = 0;
	for (i = 0; i < count; i++)
	{
		unsigned short n = face_u16(data + 16 + 4 * i), first = face_u16(data + 16 + 4 * i + 2);
		short k;

		face->key_first[i] = face->key_total;
		face->key_count[i] = 0;
		for (k = 0; k < (short)n && face->key_total < FACE_KEYS; k++)
		{
			unsigned long at = keys_at + 4UL * (first + (unsigned long)k);

			if (at + 4 > size)
				break;
			/* (signed: a key a little before the line's start, a weight below none) */
			face->key_time[face->key_total] = (short)face_u16(data + at) / scale;
			face->key_weight[face->key_total] = (short)face_u16(data + at + 2) / scale;
			face->key_total++;
			face->key_count[i]++;
		}
	}
	return TRUE;
}

/* a phoneme's weight at a time: along its keys, linearly */
H2_ALIGN static float face_weight(struct face const *face, short phoneme, float seconds)
{
	short first = face->key_first[phoneme], count = face->key_count[phoneme], k;

	if (count < 1)
		return 0.0f;
	if (seconds <= face->key_time[first])
		return count == 1 ? face->key_weight[first] : 0.0f;
	for (k = 1; k < count; k++)
	{
		float t0 = face->key_time[first + k - 1], t1 = face->key_time[first + k];

		if (seconds <= t1)
		{
			float a = t1 > t0 ? (seconds - t0) / (t1 - t0) : 1.0f;

			return face->key_weight[first + k - 1] + a * (face->key_weight[first + k] - face->key_weight[first + k - 1]);
		}
	}
	return 0.0f;
}

/* the speaker's Halo 2 graph (its biped's hlmt's), NONE: its made biped's
name (h2\characters\<kind>_mp\...) the kind's */
H2_ALIGN static long face_graph(struct h2_cache *cache, long object_index)
{
	struct object_datum *object = object_get(object_index);
	char const *name = tag_get_name(object->definition_index);
	short b;

	if (strncmp(name, "h2\\characters\\", 14))
		return NONE;
	for (b = 0; h2_bipeds_source_name(b); b++)
	{
		char kind[64];
		long tag, hlmt;
		struct h2_cache_tag t;

		snprintf(kind, sizeof(kind), "h2\\characters\\%s_mp\\", h2_bipeds_source_name(b));
		if (strncmp(name, kind, strlen(kind)))
			continue;
		tag = h2_bipeds_made_from(cache, b);
		if (tag == NONE || !h2_cache_tag_get(cache, tag, &t) || (hlmt = h2_cache_reference(cache, t.address + 0x34)) == NONE ||
			!h2_cache_tag_get(cache, hlmt, &t))
		{
			return NONE;
		}
		return h2_cache_reference(cache, t.address + 0x10);
	}
	return NONE;
}

/* the sound's lip-sync read (its English extra info, from sounds_en.dat)
into the face; FALSE if it has none */
/* (perf/cairo-tick, HALO_H2_FACE_MEMO) what face_sound_read looked up the
same way every time, kept: the cache's sound gestalt (found by walking every
tag), sounds_en.dat open (an open of a file on the Vita's memory card every
line), and the blocks it read from it by place (the lines of one sound -
its permutations, its next speakers - share one block). Same bytes, same
answers; the dev Vita's Cairo fight spent 2-10 ms a tick in these reads */
static struct
{
	struct h2_cache *cache;
	unsigned long generation;
	long gestalt;
	FILE *file;
	char path[512];
} face_kept = { NULL, 0, NONE, NULL, "" };

#define FACE_BLOCK_ENTRIES 48
#define FACE_BLOCK_BYTES (384UL * 1024UL)

static struct face_block
{
	unsigned long generation, offset, size, last_used;
	unsigned char *data;
	char path[512];
} face_blocks[FACE_BLOCK_ENTRIES];
static unsigned long face_block_clock, face_block_bytes, face_block_hits, face_block_checked, face_block_different;

static long face_gestalt(struct h2_cache *cache)
{
	long i, gestalt = NONE;
	struct h2_cache_tag t;

	if (face_memo_setting() && face_kept.cache == cache && face_kept.generation == halo_map_generation)
		return face_kept.gestalt;
	for (i = 0; i < h2_cache_tag_count(cache) && gestalt == NONE; i++)
	{
		if (h2_cache_tag_get(cache, i, &t) && t.group_tag == 'ugh!')
			gestalt = i;
	}
	face_kept.cache = cache;
	face_kept.generation = halo_map_generation;
	face_kept.gestalt = gestalt;
	return gestalt;
}

/* size bytes at offset of the file at path, in a block of the caller's
(malloc; free it): kept ones copied, else read (FALSE: the open or the read
failed, *opened saying whether the file opened) */
static unsigned char *face_file_read(char const *path, unsigned long offset, unsigned long size, boolean *opened)
{
	int setting = face_memo_setting();
	unsigned char *raw;
	FILE *file;
	long i;
	struct face_block *oldest = NULL, *found = NULL;

	*opened = FALSE;
	if (setting)
	{
		face_block_clock++;
		for (i = 0; i < FACE_BLOCK_ENTRIES; i++)
		{
			struct face_block *b = &face_blocks[i];

			if (b->data && b->generation == halo_map_generation && b->offset == offset && b->size == size && !strcmp(b->path, path))
			{
				face_block_hits++;
				if (setting == 2)
				{
					/* (the check: read again below, compared) */
					found = b;
					break;
				}
				raw = (unsigned char *)malloc(size);
				if (!raw)
					break;
				memcpy(raw, b->data, size);
				b->last_used = face_block_clock;
				*opened = TRUE;
				return raw;
			}
		}
	}
	if (setting && face_kept.file && !strcmp(face_kept.path, path))
	{
		file = face_kept.file;
	}
	else
	{
		file = fopen(path, "rb");
		if (setting && file && strlen(path) < sizeof(face_kept.path))
		{
			if (face_kept.file)
				fclose(face_kept.file);
			face_kept.file = file;
			strcpy(face_kept.path, path);
		}
	}
	raw = file ? (unsigned char *)malloc(size) : NULL;
	*opened = file != NULL;
	if (raw && !(fseek(file, (long)offset, SEEK_SET) == 0 && fread(raw, 1, size, file) == size))
	{
		/* (as before: a short read reads as no block) */
		memset(raw, 0, 4);
	}
	if (file && file != face_kept.file)
		fclose(file);
	if (found)
	{
		face_block_checked++;
		if (!raw || memcmp(raw, found->data, size))
			face_block_different++;
		return raw;
	}
	if (raw && setting && size <= FACE_BLOCK_BYTES / 4 && strlen(path) < sizeof(face_blocks[0].path) && !memcmp(raw, "hklb", 4))
	{
		/* (kept: room made from the least used) */
		while (face_block_bytes + size > FACE_BLOCK_BYTES || !oldest)
		{
			oldest = NULL;
			for (i = 0; i < FACE_BLOCK_ENTRIES; i++)
				if (!face_blocks[i].data || !oldest || (oldest->data && face_blocks[i].last_used < oldest->last_used))
					oldest = &face_blocks[i];
			if (!oldest->data || face_block_bytes + size <= FACE_BLOCK_BYTES)
				break;
			face_block_bytes -= oldest->size;
			free(oldest->data);
			oldest->data = NULL;
		}
		if (oldest->data)
		{
			face_block_bytes -= oldest->size;
			free(oldest->data);
			oldest->data = NULL;
		}
		oldest->data = (unsigned char *)malloc(size);
		if (oldest->data)
		{
			memcpy(oldest->data, raw, size);
			oldest->generation = halo_map_generation;
			oldest->offset = offset;
			oldest->size = size;
			oldest->last_used = face_block_clock;
			strcpy(oldest->path, path);
			face_block_bytes += size;
		}
	}
	return raw;
}

H2_ALIGN static boolean face_sound_read(struct h2_cache *cache, char const *sound_name, long permutation, struct face *face)
{
	long sound = h2_cache_tag_find(cache, 'snd!', sound_name), gestalt = NONE, i, count, resource_count;
	struct h2_cache_tag t;
	unsigned long infos, record, offset, size, section_size, resources, data;
	unsigned short extra;
	unsigned char *raw = NULL;
	char path[512];
	boolean read = FALSE;

	/* (the scripts' names leave "sound\\" off) */
	if (sound == NONE)
	{
		char full[256];

		snprintf(full, sizeof(full), "sound\\%s", sound_name);
		sound = h2_cache_tag_find(cache, 'snd!', full);
	}
	if (sound == NONE || !h2_cache_tag_get(cache, sound, &t) || t.size < 0x14)
	{
		if (HALO_ENV_ONCE("HALO_H2_FACE_LOG"))
			platform_log("h2 faces: %s: no sound tag", sound_name);
		return FALSE;
	}
	extra = (unsigned short)(h2_cache_read32(cache, t.address + 0xC) >> 16);
	face->seconds = (float)h2_cache_read32(cache, t.address + 0x10) / 1000.0f;
	if (extra == 0xFFFF)
		return FALSE;
	gestalt = face_gestalt(cache);
	if (gestalt == NONE || !h2_cache_tag_get(cache, gestalt, &t) ||
		!h2_cache_block(cache, t.address + 8 * FACE_EXTRA_INFOS, FACE_EXTRA_INFO_SIZE, &count, &infos) || extra >= count)
	{
		return FALSE;
	}
	/* (its English record: the first) */
	record = infos + FACE_EXTRA_INFO_SIZE * (unsigned long)extra + 8;
	offset = h2_cache_read32(cache, record) & 0x3FFFFFFFUL;
	size = h2_cache_read32(cache, record + 4);
	section_size = h2_cache_read32(cache, record + 8);
	if (!size || size > 1024 * 1024 || 8 + section_size > size || !h2_cache_block(cache, record + 0x10, 16, &resource_count, &resources))
		return FALSE;
	snprintf(path, sizeof(path), "%ssounds_en.dat", h2_resource_directory(h2_level_maps_directory(), "sounds_en.dat"));
	{
		boolean opened;

		raw = face_file_read(path, offset, size, &opened);
	}
	if (raw && !memcmp(raw, "hklb", 4))
	{
		unsigned long encoded = 0, encoded_size = 0, lipsync = 0, lipsync_size = 0;

		data = 8 + section_size;
		for (i = 0; i < resource_count; i++)
		{
			unsigned char const *r = (unsigned char const *)h2_cache_at(cache, resources + 16 * (unsigned long)i, 16);
			unsigned long rsize = r ? face_u32(r + 8) : 0, roffset = r ? face_u32(r + 12) : 0;

			if (!r || roffset > size - data || rsize > size - data - roffset)
				continue;
			if (r[0] == 1)
				encoded = data + roffset, encoded_size = rsize;
			else if (r[0] == 0 && face_u16(r + 2) == 8 && rsize >= 16 * (unsigned long)(permutation + 1))
			{
				/* (a dialogue info a permutation: a combat line's variants) */
				lipsync = face_u32(raw + data + roffset + 16 * (unsigned long)permutation + 8);
				lipsync_size = face_u32(raw + data + roffset + 16 * (unsigned long)permutation + 12);
			}
		}
		if (encoded && lipsync_size && lipsync <= encoded_size && lipsync_size <= encoded_size - lipsync)
			read = face_lipsync_read(face, raw + encoded + lipsync, lipsync_size);
		if (HALO_ENV_ONCE("HALO_H2_FACE_LOG"))
			platform_log("h2 faces: %s: extra info %u, %s at %lx (%lu bytes): lip-sync %lu bytes at %lu, %s", sound_name, (unsigned)extra, path,
				offset, size, lipsync_size, lipsync, read ? "read" : "not read");
	}
	else if (HALO_ENV_ONCE("HALO_H2_FACE_LOG"))
		platform_log("h2 faces: %s: extra info %u: no block at %lx of %s", sound_name, (unsigned)extra, offset, path);
	free(raw);
	return read;
}

/* (perf/cairo-tick) HALO_H2_FACE_MEMO: a line's lip-sync as face_sound_read
found it, kept for the line's next time. face_sound_read reads the line's
sound tag, walks the cache's every tag for the sound gestalt, and reads up
to a megabyte of sounds_en.dat from the memory card - on the tick, for
every line a speaker starts: on the Vita's Cairo grunt fight that was
10-15 ms a tick (unit_dialogue_update, tick-detail). Its answer depends
only on the level's cache (the map, as loaded), the name and the
permutation, and the face it writes into comes cleared (face_free, just
before): kept is the answer and every field it set - the same face
afterwards. 0 off, 1 on (default), 2 check: read again and compared (the
"h2 faces memo" line, HALO_TICK_PROFILE) */
#define FACE_MEMO_ENTRIES 96

static struct face_memo
{
	struct h2_cache *cache;
	unsigned long generation;
	long permutation;
	unsigned long last_used;
	boolean used, read;
	float seconds;
	short phoneme_count, key_total;
	short key_first[FACE_PHONEMES], key_count[FACE_PHONEMES];
	float *keys; /* key_time then key_weight, key_total each */
	char name[200];
} face_memo[FACE_MEMO_ENTRIES];
static unsigned long face_memo_clock, face_memo_hits, face_memo_misses, face_memo_checked, face_memo_different;
static struct face face_memo_check_face;
/* (face_decode_kept's, below) */
static unsigned long face_decode_clock, face_decode_hits, face_decode_misses, face_decode_checked, face_decode_different;

static int face_memo_setting(void)
{
	static int setting = -1;

	if (setting < 0)
	{
		const char *value = getenv("HALO_H2_FACE_MEMO");

		setting = value ? atoi(value) : 1;
		if (setting < 0 || setting > 2)
			setting = 1;
	}
	return setting;
}

/* (a face face_free left) the memo's entry laid into it */
static void face_memo_apply(struct face_memo const *entry, struct face *face)
{
	face->seconds = entry->seconds;
	face->phoneme_count = entry->phoneme_count;
	face->key_total = entry->key_total;
	memcpy(face->key_first, entry->key_first, sizeof(face->key_first));
	memcpy(face->key_count, entry->key_count, sizeof(face->key_count));
	if (entry->key_total > 0)
	{
		memcpy(face->key_time, entry->keys, sizeof(float) * (size_t)entry->key_total);
		memcpy(face->key_weight, entry->keys + entry->key_total, sizeof(float) * (size_t)entry->key_total);
	}
}

/* the fields face_sound_read sets, equal? (the rest of both cleared) */
static boolean face_memo_same(struct face const *a, struct face const *b)
{
	return !memcmp(&a->seconds, &b->seconds, sizeof(a->seconds)) && a->phoneme_count == b->phoneme_count &&
		a->key_total == b->key_total && !memcmp(a->key_first, b->key_first, sizeof(a->key_first)) &&
		!memcmp(a->key_count, b->key_count, sizeof(a->key_count)) &&
		!memcmp(a->key_time, b->key_time, sizeof(a->key_time)) && !memcmp(a->key_weight, b->key_weight, sizeof(a->key_weight));
}

H2_ALIGN static boolean face_sound_read_kept(struct h2_cache *cache, char const *sound_name, long permutation, struct face *face)
{
	int setting = face_memo_setting();
	struct face_memo *entry = NULL, *oldest = NULL;
	long i;
	boolean read;

	if (!setting || strlen(sound_name) >= sizeof(face_memo[0].name))
		return face_sound_read(cache, sound_name, permutation, face);
	face_memo_clock++;
	{
		static int report = -1;

		if (report < 0)
			report = getenv("HALO_TICK_PROFILE") && atoi(getenv("HALO_TICK_PROFILE")) != 0;
		if (report && face_memo_clock % 16 == 0)
			platform_log("h2 faces memo: %lu lines: %lu from the memo, %lu read; check: %lu compared, %lu different | phonemes "
				"decoded: %lu from the memo, %lu decoded; check: %lu compared, %lu different | sound blocks: %lu kept answered, "
				"%lu KB kept; check: %lu compared, %lu different", face_memo_clock, face_memo_hits,
				face_memo_misses, face_memo_checked, face_memo_different, face_decode_hits, face_decode_misses, face_decode_checked,
				face_decode_different, face_block_hits, face_block_bytes / 1024, face_block_checked, face_block_different);
	}
	for (i = 0; i < FACE_MEMO_ENTRIES; i++)
	{
		struct face_memo *m = &face_memo[i];

		if (m->used && m->cache == cache && m->generation == halo_map_generation && m->permutation == permutation &&
			!strcmp(m->name, sound_name))
		{
			entry = m;
			break;
		}
		if (!oldest || !m->used || (oldest->used && m->last_used < oldest->last_used))
			oldest = m;
	}
	if (entry)
	{
		entry->last_used = face_memo_clock;
		face_memo_hits++;
		if (setting == 2)
		{
			/* (the check: read as before into a cleared face, compared) */
			struct face *fresh = &face_memo_check_face;

			memset(fresh, 0, sizeof(*fresh));
			memcpy(fresh, face, offsetof(struct face, seconds));
			read = face_sound_read(cache, sound_name, permutation, fresh);
			face_memo_apply(entry, face);
			face_memo_checked++;
			if (read != entry->read || !face_memo_same(fresh, face))
				face_memo_different++;
			*face = *fresh;
			return read;
		}
		face_memo_apply(entry, face);
		return entry->read;
	}
	face_memo_misses++;
	read = face_sound_read(cache, sound_name, permutation, face);
	if (oldest)
	{
		float *keys = face->key_total > 0 ? (float *)malloc(sizeof(float) * 2 * (size_t)face->key_total) : NULL;

		if (face->key_total > 0 && !keys)
			return read;
		free(oldest->keys);
		oldest->keys = keys;
		oldest->cache = cache;
		oldest->generation = halo_map_generation;
		oldest->permutation = permutation;
		oldest->last_used = face_memo_clock;
		oldest->used = TRUE;
		oldest->read = read;
		oldest->seconds = face->seconds;
		oldest->phoneme_count = face->phoneme_count;
		oldest->key_total = face->key_total;
		memcpy(oldest->key_first, face->key_first, sizeof(oldest->key_first));
		memcpy(oldest->key_count, face->key_count, sizeof(oldest->key_count));
		if (keys)
		{
			memcpy(keys, face->key_time, sizeof(float) * (size_t)face->key_total);
			memcpy(keys + face->key_total, face->key_weight, sizeof(float) * (size_t)face->key_total);
		}
		strcpy(oldest->name, sound_name);
	}
	return read;
}

/* (perf/cairo-tick) the same switch: a speaker's phonemes overlay as
h2_animation_decode_raw made it - the graph read whole and the animation
decoded, on the tick, for every line - kept per (level, graph, animation):
a level's speakers are a dozen graphs. The decode depends on those alone;
a kept one is handed out as copies of its poses and node sets, the same
bytes, in blocks of the caller's to free as before */
#define FACE_DECODE_ENTRIES 16

static struct face_decode_memo
{
	struct h2_cache *cache;
	unsigned long generation, last_used;
	long jmad, animation;
	boolean used, decoded;
	struct h2_animation_pose *poses;
	unsigned char *sets;
	short node_count, frame_count, type;
} face_decode_memo[FACE_DECODE_ENTRIES];

H2_ALIGN static boolean face_decode_kept(struct h2_cache *cache, long jmad, long animation, struct h2_animation_pose **poses,
	unsigned char **sets, short *node_count, short *frame_count, short *type)
{
	int setting = face_memo_setting();
	struct face_decode_memo *entry = NULL, *oldest = NULL;
	unsigned long pose_bytes;
	long i;
	boolean decoded;

	if (!setting)
		return h2_animation_decode_raw(cache, jmad, animation, poses, sets, node_count, frame_count, type);
	face_decode_clock++;
	for (i = 0; i < FACE_DECODE_ENTRIES; i++)
	{
		struct face_decode_memo *m = &face_decode_memo[i];

		if (m->used && m->cache == cache && m->generation == halo_map_generation && m->jmad == jmad && m->animation == animation)
		{
			entry = m;
			break;
		}
		if (!oldest || !m->used || (oldest->used && m->last_used < oldest->last_used))
			oldest = m;
	}
	if (entry && entry->decoded)
	{
		struct h2_animation_pose *copy;
		unsigned char *copy_sets;

		pose_bytes = sizeof(*copy) * (unsigned long)entry->frame_count * (unsigned long)entry->node_count;
		copy = (struct h2_animation_pose *)malloc(pose_bytes ? pose_bytes : 1);
		copy_sets = (unsigned char *)malloc((unsigned long)entry->node_count + 1);
		if (copy && copy_sets)
		{
			entry->last_used = face_decode_clock;
			face_decode_hits++;
			memcpy(copy, entry->poses, pose_bytes);
			memcpy(copy_sets, entry->sets, (unsigned long)entry->node_count + 1);
			if (setting == 2)
			{
				/* (the check: decoded again, compared) */
				struct h2_animation_pose *fresh = NULL;
				unsigned char *fresh_sets = NULL;
				short n = 0, f = 0, t = 0;
				boolean again = h2_animation_decode_raw(cache, jmad, animation, &fresh, &fresh_sets, &n, &f, &t);

				face_decode_checked++;
				if (!again || n != entry->node_count || f != entry->frame_count || t != entry->type ||
					memcmp(fresh, copy, pose_bytes) || memcmp(fresh_sets, copy_sets, (unsigned long)n + 1))
				{
					face_decode_different++;
				}
				h2_build_heap_free(fresh);
				h2_build_heap_free(fresh_sets);
			}
			*poses = copy;
			*sets = copy_sets;
			*node_count = entry->node_count;
			*frame_count = entry->frame_count;
			*type = entry->type;
			return TRUE;
		}
		free(copy);
		free(copy_sets);
	}
	face_decode_misses++;
	decoded = h2_animation_decode_raw(cache, jmad, animation, poses, sets, node_count, frame_count, type);
	/* (kept: a decode that worked, copied) */
	if (decoded && oldest && *node_count >= 0 && *frame_count >= 0)
	{
		struct h2_animation_pose *keep;
		unsigned char *keep_sets;

		pose_bytes = sizeof(*keep) * (unsigned long)*frame_count * (unsigned long)*node_count;
		keep = (struct h2_animation_pose *)malloc(pose_bytes ? pose_bytes : 1);
		keep_sets = (unsigned char *)malloc((unsigned long)*node_count + 1);
		if (keep && keep_sets)
		{
			free(oldest->poses);
			free(oldest->sets);
			memcpy(keep, *poses, pose_bytes);
			memcpy(keep_sets, *sets, (unsigned long)*node_count + 1);
			oldest->poses = keep;
			oldest->sets = keep_sets;
			oldest->cache = cache;
			oldest->generation = halo_map_generation;
			oldest->jmad = jmad;
			oldest->animation = animation;
			oldest->last_used = face_decode_clock;
			oldest->used = TRUE;
			oldest->decoded = TRUE;
			oldest->node_count = *node_count;
			oldest->frame_count = *frame_count;
			oldest->type = *type;
		}
		else
		{
			free(keep);
			free(keep_sets);
		}
	}
	return decoded;
}

/* (h2_scripts.c: sound_impulse_start on an object) the line's lip-sync
played on its speaker's face, when it has one and the speaker a graph of
phonemes; FALSE otherwise */
boolean h2_cinematics_face_start(long object_index, char const *sound_name)
{
	struct h2_cache *cache = h2_scripts_cache();
	struct face *face = NULL;
	long jmad, i, count, phonemes = NONE, node_count_tag, permutation = 0;
	char base_name[256];
	unsigned long animations, nodes;
	struct h2_cache_tag tag;
	unsigned char *sets = NULL;
	short type = 0;

	if (!cache || !sound_name || object_index == NONE || !object_try_and_get_and_verify_type(object_index, _object_mask_unit) ||
		getenv("HALO_H2_NO_FACES"))
	{
		return FALSE;
	}
	/* (a voice's line, name#permutation - h2_sound.c's) */
	snprintf(base_name, sizeof(base_name), "%s", sound_name);
	if (strchr(base_name, '#'))
	{
		permutation = atol(strchr(base_name, '#') + 1);
		*strchr(base_name, '#') = '\0';
		sound_name = base_name;
	}
	for (i = 0; i < h2_faces.count && !face; i++)
	{
		if (h2_faces.faces[i].object_index == object_index)
			face = &h2_faces.faces[i];
	}
	if (!face)
	{
		if (h2_faces.count >= FACE_MAXIMUM)
		{
			/* (the oldest let go) */
			face_free(&h2_faces.faces[0]);
			memmove(&h2_faces.faces[0], &h2_faces.faces[1], sizeof(h2_faces.faces[0]) * (FACE_MAXIMUM - 1));
			h2_faces.count--;
		}
		h2_cinematics_ever = TRUE; /* (perf/ce-floor-3: objects.c asks h2_cinematics_node_matrices only from now on) */
		face = &h2_faces.faces[h2_faces.count++];
		memset(face, 0, sizeof(*face));
	}
	face_free(face);
	face->object_index = object_index;
	face->start_time = game_time_get();
	jmad = face_graph(cache, object_index);
	if (HALO_ENV_ONCE("HALO_H2_FACE_LOG"))
		platform_log("h2 faces: %s on %08lx (%s): graph %s", sound_name, (unsigned long)object_index,
			tag_get_name(object_get(object_index)->definition_index), jmad != NONE ? h2_cache_tag_name(cache, jmad) : "none");
	{
		/* (perf/cairo-tick, HALO_TICK_PROFILE=3) the face's parts timed (tick_detail.c) */
		extern unsigned long long halo_tick_detail_begin(void);
extern int halo_tick_detail_state; /* (perf/ce-floor) 0: no detail timing - no call */
		extern void halo_tick_detail_end(const char *name, unsigned long long started);
		unsigned long long detail_at = (halo_tick_detail_state ? halo_tick_detail_begin() : 0ULL);
		boolean read = jmad != NONE && face_sound_read_kept(cache, sound_name, permutation, face);

		(halo_tick_detail_state ? halo_tick_detail_end("face:sound_read", detail_at) : (void)0);
		if (!read)
			jmad = NONE;
	}
	if (jmad == NONE || !h2_cache_tag_get(cache, jmad, &tag) ||
		!h2_cache_block(cache, tag.address + JMAD_ANIMATIONS, JMAD_ANIMATION_SIZE, &count, &animations) ||
		!h2_cache_block(cache, tag.address + JMAD_NODES, JMAD_NODE_SIZE, &node_count_tag, &nodes))
	{
		h2_faces.failed++;
		face_drop(face);
		return FALSE;
	}
	for (i = 0; i < count && phonemes == NONE; i++)
	{
		char text[64];

		if (h2_cache_string(cache, h2_cache_read32(cache, animations + JMAD_ANIMATION_SIZE * i), text, sizeof(text)) &&
			!strcmp(text, "phonemes"))
		{
			phonemes = i;
		}
	}
	{
		extern unsigned long long halo_tick_detail_begin(void);
extern int halo_tick_detail_state; /* (perf/ce-floor) 0: no detail timing - no call */
		extern void halo_tick_detail_end(const char *name, unsigned long long started);
		unsigned long long detail_at = (halo_tick_detail_state ? halo_tick_detail_begin() : 0ULL);
		boolean decoded = phonemes != NONE && face_decode_kept(cache, jmad, phonemes, &face->poses, &sets, &face->node_count,
			&face->frame_count, &type);

		(halo_tick_detail_state ? halo_tick_detail_end("face:decode", detail_at) : (void)0);
		if (!decoded)
			phonemes = NONE;
	}
	if (phonemes == NONE || face->node_count < 1)
	{
		h2_build_heap_free(sets);
		h2_faces.failed++;
		face_drop(face);
		return FALSE;
	}
	h2_build_heap_free(sets);
	face->node_names = malloc(32 * (size_t)face->node_count);
	for (i = 0; face->node_names && i < face->node_count; i++)
	{
		if (i >= node_count_tag || !h2_cache_string(cache, h2_cache_read32(cache, nodes + JMAD_NODE_SIZE * i), face->node_names[i], 32))
			face->node_names[i][0] = 0;
	}
	/* (the object's model's nodes in the overlay, by name) */
	{
		struct object_definition *definition = object_definition_get(object_get(object_index)->definition_index);
		struct model *model = definition->object.model.index != NONE ? model_definition_get(definition->object.model.index) : NULL;
		short n, k;

		face->node_map_count = model ? (short)model->nodes.count : 0;
		face->node_map = face->node_map_count ? malloc(sizeof(short) * (size_t)face->node_map_count) : NULL;
		for (n = 0; face->node_map && n < face->node_map_count; n++)
		{
			struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, n, struct model_node);

			face->node_map[n] = NONE;
			/* (the face's alone: the overlay's other nodes are its pose) */
			if (strncmp(node->name, "f_", 2) && strcmp(node->name, "jaw") && strncmp(node->name, "eye", 3))
				continue;
			for (k = 0; face->node_names && k < face->node_count; k++)
			{
				if (!strncmp(node->name, face->node_names[k], 31))
				{
					face->node_map[n] = k;
					break;
				}
			}
		}
	}
	h2_faces.started++;
	if (HALO_ENV_ONCE("HALO_H2_FACE_LOG"))
	{
		short n, mapped = 0;

		for (n = 0; n < face->node_map_count; n++)
			mapped += face->node_map[n] != NONE;
		platform_log("h2 faces: %d face nodes of the model's %d in the overlay", mapped, face->node_map_count);
	}
	if (HALO_ENV_ONCE("HALO_H2_FACE_LOG"))
		platform_log("h2 faces: tick %ld: %s on %08lx: %d phonemes, %d keys, %.2f s; %s's phonemes (%d frames)", (long)game_time_get(),
			sound_name, (unsigned long)object_index, face->phoneme_count, face->key_total, face->seconds, h2_cache_tag_name(cache, jmad),
			face->frame_count);
	return TRUE;
}

/* the face of an object, NULL */
H2_ALIGN static struct face const *face_of(long object_index)
{
	long i;

	for (i = 0; i < h2_faces.count; i++)
	{
		if (h2_faces.faces[i].object_index == object_index)
			return &h2_faces.faces[i];
	}
	return NULL;
}

/* (a node's local pose, Halo 2's) the face's phonemes now on it */
H2_ALIGN static void face_pose(struct face const *face, short model_node, float rotation[4], float translation[3])
{
	float seconds;
	short p, k;

	if (!face || model_node < 0 || model_node >= face->node_map_count || (k = face->node_map[model_node]) == NONE)
		return;
	seconds = (float)(game_time_get() - face->start_time) / 30.0f;
	if (seconds < 0.0f || seconds > face->seconds + 0.5f)
		return;
	if (HALO_ENV_ONCE("HALO_H2_FACE_LOG") && game_time_get() % 15 == 0 && !strncmp(face->node_names[k], "f_l_lip_upper", 13))
	{
		float total = 0.0f;

		for (p = 0; p < face->phoneme_count; p++)
			total += face_weight(face, p, seconds);
		platform_log("h2 faces: %08lx at %.2f s: phonemes' weights %.2f", (unsigned long)face->object_index, seconds, total);
	}
	for (p = 0; p < face->phoneme_count && p < face->frame_count; p++)
	{
		float w = face_weight(face, p, seconds), q[4], r[4], c;
		struct h2_animation_pose const *pose;
		int i;

		if (fabsf(w) <= 0.001f)
			continue;
		pose = &face->poses[(long)face->node_count * p + k];
		/* (the change, w of the way from none: a normalized lerp) */
		c = pose->rotation[3] < 0.0f ? -1.0f : 1.0f;
		for (i = 0; i < 3; i++)
			q[i] = w * c * pose->rotation[i];
		q[3] = 1.0f - w + w * c * pose->rotation[3];
		{
			float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);

			if (n > 0.0f)
				for (i = 0; i < 4; i++)
					q[i] /= n;
		}
		/* (the node's own, times the change) */
		r[0] = rotation[3] * q[0] + rotation[0] * q[3] + rotation[1] * q[2] - rotation[2] * q[1];
		r[1] = rotation[3] * q[1] - rotation[0] * q[2] + rotation[1] * q[3] + rotation[2] * q[0];
		r[2] = rotation[3] * q[2] + rotation[0] * q[1] - rotation[1] * q[0] + rotation[2] * q[3];
		r[3] = rotation[3] * q[3] - rotation[0] * q[0] - rotation[1] * q[1] - rotation[2] * q[2];
		memcpy(rotation, r, sizeof(r));
		for (i = 0; i < 3; i++)
			translation[i] += w * pose->translation[i];
	}
}


/* ---------- private code: the emotions */

H2_ALIGN static void emotion_free(struct emotion *e)
{
	h2_build_heap_free(e->poses);
	free(e->node_map);
	memset(e, 0, sizeof(*e));
	e->object_index = NONE;
}

/* an emotion's slot let go, the ones after it moved down (face_drop's) */
H2_ALIGN static void emotion_drop(struct emotion *e)
{
	long index = (long)(e - h2_emotions.emotions);

	if (index < 0 || index >= h2_emotions.count)
		return;
	emotion_free(e);
	memmove(&h2_emotions.emotions[index], &h2_emotions.emotions[index + 1],
		sizeof(h2_emotions.emotions[0]) * (size_t)(h2_emotions.count - index - 1));
	h2_emotions.count--;
}

H2_ALIGN static struct emotion const *emotion_of(long object_index)
{
	long i;

	for (i = 0; i < h2_emotions.count; i++)
	{
		if (h2_emotions.emotions[i].object_index == object_index)
			return &h2_emotions.emotions[i];
	}
	return NULL;
}

/* its weight now */
H2_ALIGN static float emotion_weight(struct emotion const *e)
{
	long t = game_time_get() - e->start_time;

	if (e->ticks <= 0 || t >= e->ticks)
		return e->weight_to;
	return e->weight_from + (e->weight_to - e->weight_from) * (float)t / (float)e->ticks;
}

/* (a node's local pose, Halo 2's) the emotion now on it */
H2_ALIGN static void emotion_pose(struct emotion const *e, short model_node, float rotation[4], float translation[3])
{
	struct h2_animation_pose const *pose;
	float w, q[4], r[4], c, n;
	short k;
	int i;

	if (!e || model_node < 0 || model_node >= e->node_map_count || (k = e->node_map[model_node]) == NONE || e->frame < 0 ||
		e->frame >= e->frame_count || fabsf(w = emotion_weight(e)) <= 0.001f)
	{
		return;
	}
	/* (speaking - a line's lip-sync playing on it -: the pair's second
	frame, the emotion's talking face) */
	{
		struct face const *face = h2_faces.count ? face_of(e->object_index) : NULL;
		short frame = e->frame;

		if (face && !HALO_ENV_ONCE("HALO_H2_EMOTION_FIRST_FRAME"))
		{
			float seconds = (float)(game_time_get() - face->start_time) / 30.0f;

			if (seconds >= 0.0f && seconds <= face->seconds && frame + 1 < e->frame_count)
				frame++;
		}
		pose = &e->poses[(long)e->node_count * frame + k];
	}
	c = pose->rotation[3] < 0.0f ? -1.0f : 1.0f;
	for (i = 0; i < 3; i++)
		q[i] = w * c * pose->rotation[i];
	q[3] = 1.0f - w + w * c * pose->rotation[3];
	n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	if (n > 0.0f)
		for (i = 0; i < 4; i++)
			q[i] /= n;
	r[0] = rotation[3] * q[0] + rotation[0] * q[3] + rotation[1] * q[2] - rotation[2] * q[1];
	r[1] = rotation[3] * q[1] - rotation[0] * q[2] + rotation[1] * q[3] + rotation[2] * q[0];
	r[2] = rotation[3] * q[2] + rotation[0] * q[1] - rotation[1] * q[0] + rotation[2] * q[3];
	r[3] = rotation[3] * q[3] - rotation[0] * q[0] - rotation[1] * q[1] - rotation[2] * q[2];
	memcpy(rotation, r, sizeof(r));
	for (i = 0; i < 3; i++)
		translation[i] += w * pose->translation[i];
}

/* (unit_set_emotional_state) the unit's emotion: by its name, reached over
the ticks; FALSE if the name or its graph's emotions are not known */
boolean h2_cinematics_emotion(long object_index, char const *name, real weight, long ticks)
{
	struct h2_cache *cache = h2_scripts_cache();
	struct emotion *e = NULL;
	short type = NONE, i;
	long jmad, count, animation = NONE, node_count_tag, k;
	unsigned long animations, nodes;
	struct h2_cache_tag tag;
	unsigned char *sets = NULL;
	short decoded_type = 0;
	float from = 0.0f;

	if (!cache || !name || object_index == NONE || !object_try_and_get_and_verify_type(object_index, _object_mask_unit) ||
		getenv("HALO_H2_NO_EMOTIONS"))
	{
		return FALSE;
	}
	for (i = 0; i < (short)NUMBEROF(emotion_names) && type == NONE; i++)
	{
		if (!strcmp(name, emotion_names[i]))
			type = i;
	}
	for (k = 0; k < h2_emotions.count && !e; k++)
	{
		if (h2_emotions.emotions[k].object_index == object_index)
			e = &h2_emotions.emotions[k];
	}
	if (e)
	{
		from = emotion_weight(e);
		/* (another emotion: from none) */
		if (type == NONE || e->frame != 2 * (type - 1))
			from = 0.0f;
	}
	if (type == NONE)
	{
		if (HALO_ENV_ONCE("HALO_H2_FACE_LOG"))
			platform_log("h2 faces: emotion %s: not known", name);
		return FALSE;
	}
	if (!e)
	{
		if (h2_emotions.count >= FACE_MAXIMUM)
		{
			emotion_free(&h2_emotions.emotions[0]);
			memmove(&h2_emotions.emotions[0], &h2_emotions.emotions[1], sizeof(h2_emotions.emotions[0]) * (FACE_MAXIMUM - 1));
			h2_emotions.count--;
		}
		h2_cinematics_ever = TRUE; /* (perf/ce-floor-3: objects.c asks h2_cinematics_node_matrices only from now on) */
		e = &h2_emotions.emotions[h2_emotions.count++];
		memset(e, 0, sizeof(*e));
		e->object_index = NONE;
	}
	/* (none: the emotion it had, going away) */
	if (type == 0)
	{
		e->weight_from = from;
		e->weight_to = 0.0f;
		e->start_time = game_time_get();
		e->ticks = ticks;
		return TRUE;
	}
	emotion_free(e);
	e->object_index = object_index;
	e->frame = (short)(2 * (type - 1));
	e->weight_from = from;
	e->weight_to = weight;
	e->start_time = game_time_get();
	e->ticks = ticks;
	jmad = face_graph(cache, object_index);
	if (jmad == NONE || !h2_cache_tag_get(cache, jmad, &tag) ||
		!h2_cache_block(cache, tag.address + JMAD_ANIMATIONS, JMAD_ANIMATION_SIZE, &count, &animations) ||
		!h2_cache_block(cache, tag.address + JMAD_NODES, JMAD_NODE_SIZE, &node_count_tag, &nodes))
	{
		emotion_drop(e);
		return FALSE;
	}
	for (k = 0; k < count && animation == NONE; k++)
	{
		char text[64];

		if (h2_cache_string(cache, h2_cache_read32(cache, animations + JMAD_ANIMATION_SIZE * k), text, sizeof(text)) &&
			!strcmp(text, "emotions"))
		{
			animation = k;
		}
	}
	if (animation == NONE || !h2_animation_decode_raw(cache, jmad, animation, &e->poses, &sets, &e->node_count, &e->frame_count,
		&decoded_type) || e->node_count < 1)
	{
		h2_build_heap_free(sets);
		emotion_drop(e);
		return FALSE;
	}
	/* (the object's model's nodes in the overlay, by name: those it moves) */
	{
		struct object_definition *definition = object_definition_get(object_get(object_index)->definition_index);
		struct model *model = definition->object.model.index != NONE ? model_definition_get(definition->object.model.index) : NULL;
		short n;

		e->node_map_count = model ? (short)model->nodes.count : 0;
		e->node_map = e->node_map_count ? malloc(sizeof(short) * (size_t)e->node_map_count) : NULL;
		for (n = 0; e->node_map && n < e->node_map_count; n++)
		{
			struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, n, struct model_node);

			e->node_map[n] = NONE;
			for (k = 0; k < e->node_count && k < node_count_tag; k++)
			{
				char text[32];

				if (h2_cache_string(cache, h2_cache_read32(cache, nodes + JMAD_NODE_SIZE * k), text, sizeof(text)) &&
					!strncmp(node->name, text, 31) && (sets[k] & 3))
				{
					e->node_map[n] = (short)k;
					break;
				}
			}
		}
	}
	h2_build_heap_free(sets);
	if (HALO_ENV_ONCE("HALO_H2_FACE_LOG"))
		platform_log("h2 faces: tick %ld: %08lx emotion %s (frame %d of %d) to %.2f over %ld ticks", (long)game_time_get(),
			(unsigned long)object_index, name, (int)e->frame, (int)e->frame_count, weight, ticks);
	return TRUE;
}

/* ---------- public code */

void h2_cinematics_dispose(void)
{
	long i;

	kept_empty();
	name_index_empty();

	track_free(&h2_cine.camera);
	for (i = 0; i < h2_cine.object_count; i++)
	{
		track_free(&h2_cine.objects[i].track);
		free(h2_cine.objects[i].node_map);
	}
	memset(&h2_cine, 0, sizeof(h2_cine));
	h2_texture_camera_off();
	for (i = 0; i < h2_faces.count; i++)
		face_free(&h2_faces.faces[i]);
	memset(&h2_faces, 0, sizeof(h2_faces));
	for (i = 0; i < h2_emotions.count; i++)
		emotion_free(&h2_emotions.emotions[i]);
	memset(&h2_emotions, 0, sizeof(h2_emotions));
}

/* Halo 2's texture camera (hs_doc: "sets the render texture camera to a
given object marker", "turns off the render texture camera"): one, set on
an object's marker with a field of view - the In Amber Clad bridge's
forward view and the HEV pod's screen in 05a's x05, 03b's x03, 05b's
intra1, 06a's x06, 07a's intro, 08b's intra2. Its view each frame is the
marker's (forward along the marker's forward, up its up) as the object
moves; gone with its object. Off again at a checkpoint's revert and a new
map */
static struct
{
	boolean on;
	long object_index;
	char marker[64];
	real field_of_view;
} h2_texture_camera;

void h2_texture_camera_set(long object_index, char const *marker, real field_of_view_degrees)
{
	h2_texture_camera.on = object_index != NONE;
	h2_texture_camera.object_index = object_index;
	snprintf(h2_texture_camera.marker, sizeof(h2_texture_camera.marker), "%s", marker ? marker : "");
	h2_texture_camera.field_of_view = field_of_view_degrees > 1.0f && field_of_view_degrees < 179.0f ? field_of_view_degrees : 50.0f;
	platform_log("h2 cinematics: texture camera on %08lx marker '%s' field of view %.1f (tick %ld)", (unsigned long)object_index,
		h2_texture_camera.marker, h2_texture_camera.field_of_view, game_time_get());
}

void h2_texture_camera_off(void)
{
	if (h2_texture_camera.on)
		platform_log("h2 cinematics: texture camera off (tick %ld)", game_time_get());
	h2_texture_camera.on = FALSE;
}

boolean h2_texture_camera_view(real_point3d *position, real_vector3d *forward, real_vector3d *up, real *field_of_view_degrees)
{
	struct object_marker marker;

	if (!h2_texture_camera.on || h2_texture_camera.object_index == NONE || !object_try_and_get(h2_texture_camera.object_index))
		return FALSE;
	if (object_get_marker_by_name(h2_texture_camera.object_index, h2_texture_camera.marker, &marker, 1) < 1)
	{
		/* (no such marker: the object's origin and facing) */
		struct object_datum *object = object_get(h2_texture_camera.object_index);

		object_get_origin(h2_texture_camera.object_index, position);
		*forward = object->object.forward;
		*up = object->object.up;
	}
	else
	{
		*position = marker.matrix.position;
		*forward = marker.matrix.forward;
		*up = marker.matrix.up;
	}
	if (normalize3d(forward) == 0.0f || normalize3d(up) == 0.0f)
		return FALSE;
	*field_of_view_degrees = h2_texture_camera.field_of_view;
	return TRUE;
}

/* (a checkpoint reverted to) the animations stopped */
void h2_cinematics_reset(void)
{
	long i;

	h2_texture_camera.on = FALSE;

	for (i = 0; i < h2_cine.object_count; i++)
	{
		track_free(&h2_cine.objects[i].track);
		free(h2_cine.objects[i].node_map);
		free(h2_cine.objects[i].blend_rotation);
		free(h2_cine.objects[i].blend_translation);
		track_free(&h2_cine.objects[i].overlay);
		free(h2_cine.objects[i].overlay_map);
	}
	h2_cine.object_count = 0;
	h2_cine.camera_active = FALSE;
	for (i = 0; i < h2_faces.count; i++)
		face_free(&h2_faces.faces[i]);
	h2_faces.count = 0;
	for (i = 0; i < h2_emotions.count; i++)
		emotion_free(&h2_emotions.emotions[i]);
	h2_emotions.count = 0;
}

/* camera_set_animation_relative (graph, animation, unit, flag): the camera
played relative to the flag (or the unit, or the world); its frames */
long h2_cinematics_camera_animation(long graph_handle, char const *name, long unit_index, short flag_index)
{
	struct track track;

	if (!track_decode(graph_handle, name, &track))
		return 0;
	track_free(&h2_cine.camera);
	h2_cine.camera = track;
	if (!flag_place(flag_index, &h2_cine.camera_anchor) && !object_place(unit_index, &h2_cine.camera_anchor))
	{
		memset(&h2_cine.camera_anchor, 0, sizeof(h2_cine.camera_anchor));
		h2_cine.camera_anchor.forward.i = h2_cine.camera_anchor.left.j = h2_cine.camera_anchor.up.k = 1.0f;
	}
	h2_cine.camera_start = game_time_get();
	h2_cine.camera_active = TRUE;
	snprintf(h2_cine.camera_name, sizeof(h2_cine.camera_name), "%s", name);
	if (HALO_ENV_ONCE("HALO_H2_CINEMATIC_LOG"))
		platform_log("h2 cinematics: camera %s: anchor %.2f %.2f %.2f fwd %.2f %.2f %.2f, first frame %.2f %.2f %.2f fwd %.2f %.2f %.2f",
			name, h2_cine.camera_anchor.position.x, h2_cine.camera_anchor.position.y, h2_cine.camera_anchor.position.z,
			h2_cine.camera_anchor.forward.i, h2_cine.camera_anchor.forward.j, h2_cine.camera_anchor.forward.k,
			track.frames[0].position.x, track.frames[0].position.y, track.frames[0].position.z,
			track.frames[0].forward.i, track.frames[0].forward.j, track.frames[0].forward.k);
	return track.frame_count;
}

void h2_cinematics_camera_stop(void)
{
	h2_cine.camera_active = FALSE;
}

/* the field of view now, in degrees: along its change */
static real field_of_view_now(void)
{
	long elapsed = game_time_get() - h2_cine.fov_start;

	if (h2_cine.fov_ticks <= 0 || elapsed >= h2_cine.fov_ticks || elapsed < 0)
		return h2_cine.field_of_view;
	return h2_cine.fov_from + (h2_cine.field_of_view - h2_cine.fov_from) * (real)elapsed / (real)h2_cine.fov_ticks;
}

/* camera_set_field_of_view (degrees, ticks): reached over the ticks (03a's
36 to 60 over 14, hs_doc's <short>) */
void h2_cinematics_field_of_view(real degrees, short ticks)
{
	h2_cine.fov_from = field_of_view_now();
	if (h2_cine.fov_from <= 1.0f)
		h2_cine.fov_from = 60.0f;
	h2_cine.field_of_view = degrees;
	h2_cine.fov_ticks = ticks;
	h2_cine.fov_start = game_time_get();
}

/* cinematic_screen_effect_set_depth_of_field (separation, near lower, upper,
time, far lower, upper, time): each blur from its lower to its upper bound
over its time; a time below 0 leaves it (MCC halo2.dll 0x180967f10, hs_doc) */
void h2_cinematics_depth_of_field_set(real separation, real near_lower, real near_upper, real near_time, real far_lower, real far_upper,
	real far_time)
{
	long now = game_time_get();

	h2_cine.dof.separation = separation > 0.0f ? separation : 0.0f;
	if (near_time >= 0.0f)
	{
		h2_cine.dof.near_bounds[0] = near_lower, h2_cine.dof.near_bounds[1] = near_upper;
		h2_cine.dof.near_start = now, h2_cine.dof.near_ticks = near_time * 30.0f;
	}
	if (far_time >= 0.0f)
	{
		h2_cine.dof.far_bounds[0] = far_lower, h2_cine.dof.far_bounds[1] = far_upper;
		h2_cine.dof.far_start = now, h2_cine.dof.far_ticks = far_time * 30.0f;
	}
}

/* (cinematic_screen_effect_start true, _stop) no blur */
void h2_cinematics_depth_of_field_clear(void)
{
	memset(&h2_cine.dof, 0, sizeof(h2_cine.dof));
}

static real dof_amount(real const bounds[2], long start, real ticks)
{
	real t = ticks > 0.0f ? (real)(game_time_get() - start) / ticks : 1.0f;

	if (t < 0.0f)
		t = 0.0f;
	if (t > 1.0f)
		t = 1.0f;
	return bounds[0] + (bounds[1] - bounds[0]) * t;
}

/* (h2_bloom.c) the blur now; FALSE: none */
boolean h2_cinematics_depth_of_field(float *separation, float *near_amount, float *far_amount)
{
	if (getenv("HALO_H2_NO_DOF"))
		return FALSE;
	*separation = h2_cine.dof.separation;
	*near_amount = dof_amount(h2_cine.dof.near_bounds, h2_cine.dof.near_start, h2_cine.dof.near_ticks);
	*far_amount = dof_amount(h2_cine.dof.far_bounds, h2_cine.dof.far_start, h2_cine.dof.far_ticks);
	return *near_amount > 0.0f || *far_amount > 0.0f;
}

/* cinematic_screen_effect_set_crossfade (seconds), _crossfade2 (seconds,
exponent): the last frame fades out over the seconds (MCC halo2.dll
0x180967e70) */
void h2_cinematics_crossfade_start(real seconds, real exponent)
{
	h2_cine.crossfade_start = game_time_get();
	h2_cine.crossfade_ticks = seconds * 30.0f;
	h2_cine.crossfade_exponent = exponent;
}

/* (h2_bloom.c) the old frame's opacity now; 0: keep this frame; below 0: nothing
to do */
float h2_cinematics_crossfade(void)
{
	if (h2_cine.crossfade_ticks > 0.0f)
	{
		real t = (real)(game_time_get() - h2_cine.crossfade_start) / h2_cine.crossfade_ticks;

		if (t < 1.0f)
		{
			real opacity = 1.0f - (t > 0.0f ? t : 0.0f);

			if (h2_cine.crossfade_exponent > 0.0f && h2_cine.crossfade_exponent != 1.0f)
				opacity = powf(opacity, h2_cine.crossfade_exponent);
			return opacity > 0.004f ? opacity : 0.004f;
		}
		h2_cine.crossfade_ticks = 0.0f;
	}
	return h2_cine.camera_active ? 0.0f : -1.0f;
}

/* camera_time: the ticks the camera's animation has left; NONE when none
plays */
long h2_cinematics_camera_time(void)
{
	if (!h2_cine.camera_active)
		return NONE;
	return MAX(0, h2_cine.camera.frame_count - 1 - (game_time_get() - h2_cine.camera_start));
}

/* (camera_scripting.c) the camera's place now, while an animation plays */
boolean h2_cinematics_camera_frame(real_point3d *position, real_vector3d *forward, real_vector3d *up, real *field_of_view)
{
	struct place place;
	real degrees;

	if (!h2_cine.camera_active || !h2_cine.camera.frame_count)
		return FALSE;
	place_compose(&h2_cine.camera_anchor, track_place(&h2_cine.camera, track_frame(&h2_cine.camera, h2_cine.camera_start, FALSE)), &place);
	*position = place.position;
	*forward = place.forward;
	*up = place.up;
	degrees = field_of_view_now();
	if (degrees <= 1.0f)
		degrees = 60.0f;
	if (degrees > 89.0f)
		degrees = 89.0f;
	/* (this engine's wide view adds side strips to 4:3's, Hor+; MCC's scene
	keeps 4:3's width across the wide screen: its Chief and pilots 4/3 the
	size of ours, footage mcc-h2-campaign-20261025 at 03a's scenes. While a
	cinematic runs every Halo 2 view spans its field of view across the whole
	screen's width, as the Xbox's (field_of_view.c
	field_of_view_level_aspect_scale, the screen's own shape); with that off
	(HALO_H2_FOV_HOR_PLUS=1) this camera alone keeps the tangent 640/848 of
	the script's as before. HALO_H2_CINEMATIC_FOV_WIDE: ours) */
	*field_of_view = degrees * 3.14159265f / 180.0f;
	{
		boolean field_of_view_level_aspect_enabled(void);
		char const *display = HALO_ENV_ONCE("HALO_DISPLAY_WIDTH");

		if (!field_of_view_level_aspect_enabled() &&
			!HALO_ENV_ONCE("HALO_H2_CINEMATIC_FOV_WIDE") && !(display && atol(display) == 640))
			*field_of_view = 2.0f * atanf(tanf(*field_of_view * 0.5f) * (640.0f / 848.0f));
	}
	return valid_real_vector3d_axes2(forward, up) &&
		fabsf(position->x) < 5000.0f && fabsf(position->y) < 5000.0f && fabsf(position->z) < 5000.0f;
}

/* a model's node's name and an animation's the same: a vehicle's made model
has its names' '_' as ' ' (h2_vehicles.c), its animations Halo 2's - only the
one word names matched, so a Pelican's doors, gear, engines and flaps, a
Banshee's wings and a Wraith's turret never moved in a cutscene */
static boolean node_names_match(char const *model, char const *animation)
{
	short i;

	for (i = 0; i < 31; i++)
	{
		char a = model[i] == ' ' ? '_' : model[i], b = animation[i] == ' ' ? '_' : animation[i];

		if (a != b)
			return FALSE;
		if (!a)
			return TRUE;
	}
	return TRUE;
}

/* an object animated (custom_animation_relative, scenery_animation_start
and their kin): relative to anchor_object, or the flag, or where it is
now; its frames (0: none) */
long h2_cinematics_object_animation(long object_index, long graph_handle, char const *name, long anchor_object, short anchor_flag,
	boolean loop)
{
	struct object_animation *animation = NULL;
	long i;

	if (object_index == NONE || !object_try_and_get(object_index))
	{
		if (h2_cine.objects_missing++ < 16)
			platform_log("h2 cinematics: %s: its object is not there", name ? name : "?");
		return 0;
	}
	for (i = 0; i < h2_cine.object_count && !animation; i++)
	{
		if (h2_cine.objects[i].object_index == object_index)
			animation = &h2_cine.objects[i];
	}
	if (!animation)
	{
		if (h2_cine.object_count >= MAXIMUM_OBJECT_ANIMATIONS)
			return 0;
		h2_cinematics_ever = TRUE; /* (perf/ce-floor-3: objects.c asks h2_cinematics_node_matrices only from now on) */
		animation = &h2_cine.objects[h2_cine.object_count++];
		memset(animation, 0, sizeof(*animation));
	}
	track_free(&animation->track);
	free(animation->node_map);
	animation->node_map = NULL;
	animation->node_map_count = 0;
	animation->object_index = object_index;
	/* (h2_cinematics_device_track sets its own after this) */
	animation->device_driven = FALSE;
	animation->blend_ticks = 0;
	/* (its anchor carried by a parent from the next tick: a new anchor) */
	animation->carrier_index = NONE;
	animation->anchored_here = FALSE;
	if (!track_decode(graph_handle, name, &animation->track))
	{
		*animation = h2_cine.objects[--h2_cine.object_count];
		return 0;
	}
	if (anchor_flag == H2_CINEMATICS_CAMERA_ANCHOR && !object_try_and_get(anchor_object) && h2_cine.camera_active &&
		!getenv("HALO_H2_NO_CAMERA_ANCHOR"))
	{
		animation->anchor = h2_cine.camera_anchor;
		if (h2_cine.objects_missing++ < 16)
			platform_log("h2 cinematics: %s: its anchor object is not there, the camera's anchor instead", name ? name : "?");
	}
	else if (!object_place(anchor_object, &animation->anchor) && !flag_place(anchor_flag, &animation->anchor))
	{
		/* (where it is now, its first frame there) */
		struct place here, first = animation->track.frames[0];

		object_place(object_index, &here);
		animation->anchored_here = TRUE;
		/* (the anchor: here less the first frame's offset, its axes kept) */
		animation->anchor = here;
		animation->anchor.position.x -= here.forward.i * first.position.x + here.left.i * first.position.y + here.up.i * first.position.z;
		animation->anchor.position.y -= here.forward.j * first.position.x + here.left.j * first.position.y + here.up.j * first.position.z;
		animation->anchor.position.z -= here.forward.k * first.position.x + here.left.k * first.position.y + here.up.k * first.position.z;
	}
	animation->start_time = game_time_get();
	animation->loop = loop;
	animation->in_place = h2_cine.test_in_place;
	if (h2_cine.test_in_place)
		animation->anchor.position.z += 0.65f;
	/* (its model's nodes found in the animation by name) */
	if (animation->track.node_count > 1)
	{
		struct object_datum *object = object_get(object_index);
		struct object_definition *definition = object_definition_get(object->definition_index);

		if (definition->object.model.index != NONE)
		{
			struct model *model = model_definition_get(definition->object.model.index);
			short n, k, found = 0;

			animation->node_map = malloc(sizeof(short) * (size_t)(model->nodes.count > 0 ? model->nodes.count : 1));
			for (n = 0; animation->node_map && n < model->nodes.count; n++)
			{
				struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, n, struct model_node);

				animation->node_map[n] = NONE;
				for (k = 0; k < animation->track.node_count; k++)
				{
					if (node_names_match(node->name, animation->track.node_names[k]))
					{
						animation->node_map[n] = k;
						found++;
						break;
					}
				}
			}
			/* (the roots named apart - the In Amber Clad bridge's model
			"iac_bridge", its animations "bridge" -: root to root, the rest
			by name. Its seat_back stood still and hid Miranda in x05) */
			if (animation->node_map && model->nodes.count > 0 && animation->node_map[0] == NONE && found > 0 &&
				!getenv("HALO_H2_NO_ROOT_MATCH"))
			{
				boolean taken = FALSE;

				for (n = 1; n < model->nodes.count; n++)
					taken |= animation->node_map[n] == 0;
				if (!taken)
				{
					animation->node_map[0] = 0;
					found++;
				}
			}
			animation->node_map_count = animation->node_map ? model->nodes.count : 0;
			/* (a model the animation names none of the nodes of - this
			engine's stand-in for it - is moved whole) */
			if (found < 2)
			{
				free(animation->node_map);
				animation->node_map = NULL;
				animation->node_map_count = 0;
			}
			if (HALO_ENV_ONCE("HALO_H2_CINEMATIC_LOG"))
				platform_log("h2 cinematics: %s: %d of its model's %d nodes in the animation's %d", name, found, (int)model->nodes.count,
					(int)animation->track.node_count);
			if (HALO_ENV_ONCE("HALO_H2_CINEMATIC_LOG") && found < model->nodes.count && model->nodes.count <= 4)
			{
				for (n = 0; n < model->nodes.count; n++)
					platform_log("h2 cinematics: %s: model node %d '%.31s'", name, n,
						TAG_BLOCK_GET_ELEMENT(&model->nodes, n, struct model_node)->name);
				for (k = 0; k < animation->track.node_count && k < 4; k++)
					platform_log("h2 cinematics: %s: animation node %d '%.31s'", name, k, animation->track.node_names[k]);
			}
			if (HALO_ENV_ONCE("HALO_H2_CINEMATIC_LOG") && animation->node_map)
			{
				for (n = 0; n < model->nodes.count && n < 8; n++)
				{
					struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, n, struct model_node);
					short m = animation->node_map[n];
					struct h2_animation_pose const *pose = m != NONE ? &animation->track.poses0[m] : NULL;

					platform_log("h2 cinematics: node %d %s parent %d: model %.3f %.3f %.3f q %.3f %.3f %.3f %.3f | animation %.3f %.3f %.3f q %.3f %.3f %.3f %.3f sets %d",
						n, node->name, node->parent_node_index, node->default_translation.x, node->default_translation.y, node->default_translation.z,
						node->default_rotation.v.i, node->default_rotation.v.j, node->default_rotation.v.k, node->default_rotation.w,
						pose ? pose->translation[0] : 0.0f, pose ? pose->translation[1] : 0.0f, pose ? pose->translation[2] : 0.0f,
						pose ? pose->rotation[0] : 0.0f, pose ? pose->rotation[1] : 0.0f, pose ? pose->rotation[2] : 0.0f, pose ? pose->rotation[3] : 0.0f,
						m != NONE ? animation->track.sets[m] : -1);
				}
			}
		}
	}
	if (HALO_ENV_ONCE("HALO_H2_CINEMATIC_LOG"))
	{
		struct place here;

		object_place(object_index, &here);
		platform_log("h2 cinematics: object %s: at %.2f %.2f %.2f; anchor %.2f %.2f %.2f fwd %.2f %.2f %.2f, first frame %.2f %.2f %.2f",
			name, here.position.x, here.position.y, here.position.z, animation->anchor.position.x, animation->anchor.position.y,
			animation->anchor.position.z, animation->anchor.forward.i, animation->anchor.forward.j, animation->anchor.forward.k,
			animation->track.frames[0].position.x, animation->track.frames[0].position.y, animation->track.frames[0].position.z);
	}
	return animation->track.frame_count;
}

/* an object's animation, if one plays on it */
static struct object_animation *object_animation_find(long object_index)
{
	long i;

	for (i = 0; i < h2_cine.object_count; i++)
	{
		if (h2_cine.objects[i].object_index == object_index)
			return &h2_cine.objects[i];
	}
	return NULL;
}

/* a place's inverse (its axes orthonormal; its scale 1) */
static void place_inverse(struct place const *p, struct place *out)
{
	struct place r;

	r.forward.i = p->forward.i, r.forward.j = p->left.i, r.forward.k = p->up.i;
	r.left.i = p->forward.j, r.left.j = p->left.j, r.left.k = p->up.j;
	r.up.i = p->forward.k, r.up.j = p->left.k, r.up.k = p->up.k;
	r.position.x = -(r.forward.i * p->position.x + r.left.i * p->position.y + r.up.i * p->position.z);
	r.position.y = -(r.forward.j * p->position.x + r.left.j * p->position.y + r.up.j * p->position.z);
	r.position.z = -(r.forward.k * p->position.x + r.left.k * p->position.y + r.up.k * p->position.z);
	r.scale = 1.0f;
	*out = r;
}

/* a device's Halo 2 animation graph's handle (NONE): its machine's tag -
this engine's "h2\objects\<name>[ <variant>]" is Halo 2's machine of that
name - its model's graph */
static long device_graph_handle(long device_index)
{
	static struct { long definition_index, handle; } known[16];
	static short known_count;
	static void const *known_cache;
	struct h2_cache *cache = h2_scripts_cache();
	char const *name = tag_get_name(object_get(device_index)->definition_index), *leaf;
	char short_name[64];
	long definition_index = object_get(device_index)->definition_index, i, count, handle = NONE;
	short k;

	if (!cache)
		return NONE;
	if (known_cache != cache)
		known_cache = cache, known_count = 0;
	for (k = 0; k < known_count; k++)
	{
		if (known[k].definition_index == definition_index)
			return known[k].handle;
	}
	if (strncmp(name, "h2\\objects\\", 11))
		return NONE;
	snprintf(short_name, sizeof(short_name), "%s", name + 11);
	if (strchr(short_name, ' '))
		*strchr(short_name, ' ') = 0;
	count = h2_cache_tag_count(cache);
	for (i = 0; i < count && handle == NONE; i++)
	{
		struct h2_cache_tag t, model, graph;
		long hlmt, jmad;

		if (!h2_cache_tag_get(cache, i, &t) || t.group_tag != 'mach')
			continue;
		leaf = strrchr(h2_cache_tag_name(cache, i), '\\');
		leaf = leaf ? leaf + 1 : h2_cache_tag_name(cache, i);
		if (strcmp(leaf, short_name))
			continue;
		if ((hlmt = h2_cache_reference(cache, t.address + 0x34)) == NONE || !h2_cache_tag_get(cache, hlmt, &model) ||
			(jmad = h2_cache_reference(cache, model.address + 0x10)) == NONE || !h2_cache_tag_get(cache, jmad, &graph))
		{
			continue;
		}
		handle = (long)graph.handle;
	}
	if (known_count < (short)NUMBEROF(known))
	{
		known[known_count].definition_index = definition_index;
		known[known_count++].handle = handle;
	}
	return handle;
}

/* (a device's overlay set before its first track - 03b's
scarab_gun_open_down_instant, then finale_enter -: kept until the track
plays, then laid over it) */
static struct
{
	long device_index;
	char name[64];
	real to, from;
	long start, ticks;
} h2_pending_overlays[8];

static int pending_overlay_find(long device_index, boolean make)
{
	int k, free_slot = -1;

	for (k = 0; k < (int)NUMBEROF(h2_pending_overlays); k++)
	{
		if (h2_pending_overlays[k].name[0] && h2_pending_overlays[k].device_index == device_index)
			return k;
		if (!h2_pending_overlays[k].name[0] && free_slot < 0)
			free_slot = k;
	}
	if (!make)
		return -1;
	if (free_slot < 0)
		free_slot = 0;
	memset(&h2_pending_overlays[free_slot], 0, sizeof(h2_pending_overlays[free_slot]));
	h2_pending_overlays[free_slot].device_index = device_index;
	return free_slot;
}

/* (device_set_position_track: halo2-decompiled devices.cpp 0x107ed0) a
device's track - an animation of its graph by name - played at its
position from now on: the last track's root motion so far kept (the
device moved by it, as Halo 2 moves the object by its root's way from
frame 0), the last track's pose blended out over the interpolation's
seconds. FALSE: the device has no graph or no such animation (Halo 2
then plays "default" - this engine's device keeps its pose) */
boolean h2_cinematics_device_track(long device_index, char const *name, real interpolation_seconds)
{
	struct object_animation *a = object_animation_find(device_index), *b;
	struct place anchor;
	float (*rotation)[4] = NULL;
	float (*translation)[3] = NULL;
	short blend_count = 0;
	long handle;

	if (device_index == NONE || !object_try_and_get_and_verify_type(device_index, _object_mask_device) || !name)
		return FALSE;
	handle = device_graph_handle(device_index);
	if (handle == NONE)
	{
		if (h2_cine.objects_missing++ < 16)
			platform_log("h2 cinematics: device track %s: its device has no Halo 2 graph", name);
		return FALSE;
	}
	/* (where it stands: the last track's root motion from its first frame
	to now added to that track's anchor; the first track's, the device's
	own place) */
	/* (the device's own place is its transform, Halo 2's: the root's
	motion poses its nodes above it, and a track's end moves it there. So
	a script's teleport - 03b's object_teleport scarab - and a revert to a
	checkpoint - whose game state holds the device where it stood, not
	this file's tracks - leave it right: the anchor is where it is) */
	if (a && a->device_driven && a->track.frame_count > 0)
		object_place(device_index, &a->anchor);
	if (a && a->device_driven && a->track.frame_count > 0)
	{
		struct place inverse, moved;
		long frame = animation_frame(a);

		place_inverse(track_place(&a->track, 0), &inverse);
		place_compose(&a->anchor, track_place(&a->track, frame), &moved);
		place_compose(&moved, &inverse, &anchor);
		/* (the last track's local poses now, its nodes by the model's) */
		if (a->node_map && interpolation_seconds > 0.0f)
		{
			struct object_datum *object = object_get(device_index);
			struct model *model = object_definition_get(object->definition_index)->object.model.index != NONE ?
				model_definition_get(object_definition_get(object->definition_index)->object.model.index) : NULL;
			short n, count = model ? (short)MIN(model->nodes.count, a->node_map_count) : 0;

			rotation = count > 0 ? malloc(sizeof(*rotation) * count) : NULL;
			translation = count > 0 ? malloc(sizeof(*translation) * count) : NULL;
			for (n = 0; rotation && translation && n < count; n++)
			{
				struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, n, struct model_node);
				short k = a->node_map[n];

				rotation[n][0] = -node->default_rotation.v.i, rotation[n][1] = -node->default_rotation.v.j;
				rotation[n][2] = -node->default_rotation.v.k, rotation[n][3] = node->default_rotation.w;
				translation[n][0] = node->default_translation.x, translation[n][1] = node->default_translation.y;
				translation[n][2] = node->default_translation.z;
				if (k != NONE && k < a->track.node_count)
				{
					/* (through the decode window: poses holds WINDOW_FRAMES
					frames from window_first, not the whole track - indexed
					by the frame it read past its end, 03b's crash in
					device_set_position_track, found by tools/h2run.sh) */
					struct h2_animation_pose pose;

					track_pose(&a->track, frame, k, &pose);
					if (a->track.sets[k] & 1)
						memcpy(rotation[n], pose.rotation, sizeof(rotation[n]));
					if (a->track.sets[k] & 2)
						memcpy(translation[n], pose.translation, sizeof(translation[n]));
				}
			}
			if (rotation && translation)
				blend_count = count;
		}
	}
	else if (!object_place(device_index, &anchor))
		return FALSE;
	if (!h2_cinematics_object_animation(device_index, handle, name, NONE, NONE, FALSE) ||
		!(b = object_animation_find(device_index)))
	{
		free(rotation);
		free(translation);
		if (h2_cine.objects_missing++ < 16)
			platform_log("h2 cinematics: device track %s: not in its graph", name);
		return FALSE;
	}
	b->anchor = anchor;
	b->device_driven = TRUE;
	/* (the device moved by the last track's motion: there now) */
	object_set_position(device_index, &anchor.position, &anchor.forward, &anchor.up);
	free(b->blend_rotation);
	free(b->blend_translation);
	b->blend_rotation = rotation;
	b->blend_translation = translation;
	b->blend_count = blend_count;
	b->blend_start = game_time_get();
	b->blend_ticks = blend_count ? (long)(interpolation_seconds * 30.0f + 0.5f) : 0;
	{
		int k = pending_overlay_find(device_index, FALSE);

		if (k >= 0)
		{
			char pending[64];

			snprintf(pending, sizeof(pending), "%s", h2_pending_overlays[k].name);
			h2_pending_overlays[k].name[0] = 0;
			if (h2_cinematics_device_overlay(device_index, pending))
			{
				b = object_animation_find(device_index);
				b->overlay_from = h2_pending_overlays[k].from;
				b->overlay_to = h2_pending_overlays[k].to;
				b->overlay_start = h2_pending_overlays[k].start;
				b->overlay_ticks = h2_pending_overlays[k].ticks;
			}
		}
	}
	if (getenv("HALO_H2_DEVICE_LOG"))
	{
		struct object_datum *o = object_get(device_index);
		real_matrix4x3 const *m = object_get_node_matrices(device_index);
		short count = object_definition_get(o->definition_index)->object.model.index != NONE ?
			(short)model_definition_get(object_definition_get(o->definition_index)->object.model.index)->nodes.count : 1;

		platform_log("h2 cinematics: device %08lx posed at the switch: cluster %d leaf %ld, middle %.2f %.2f %.2f r %.2f, node 0 %.2f %.2f %.2f, node %d %.2f %.2f %.2f up %.2f %.2f %.2f",
			(unsigned long)device_index, (int)o->object.location.cluster_index, (long)o->object.location.leaf_index,
			o->object.bounding_sphere_center.x, o->object.bounding_sphere_center.y, o->object.bounding_sphere_center.z, o->object.bounding_sphere_radius,
			m[0].position.x, m[0].position.y, m[0].position.z, count - 1, m[count - 1].position.x, m[count - 1].position.y, m[count - 1].position.z,
			m[count - 1].up.i, m[count - 1].up.j, m[count - 1].up.k);
	}
	if (getenv("HALO_H2_DEVICE_LOG"))
		platform_log("h2 cinematics: device %08lx track %s: %d frames, anchor %.2f %.2f %.2f fwd %.2f %.2f %.2f, blend %ld ticks",
			(unsigned long)device_index, name, (int)b->track.frame_count, anchor.position.x, anchor.position.y, anchor.position.z,
			anchor.forward.i, anchor.forward.j, anchor.forward.k, b->blend_ticks);
	return TRUE;
}

/* (device_set_overlay_track: halo2-decompiled devices.cpp 0x108530) a
device's overlay by name, at position 0 - its nodes the model's by name;
FALSE: no such overlay, or the device plays no track to lay it over */
boolean h2_cinematics_device_overlay(long device_index, char const *name)
{
	struct object_animation *a = object_animation_find(device_index);
	struct object_datum *object;
	struct model *model;
	long handle;
	short n, k;

	/* (no track yet: kept for the first) */
	if ((!a || !a->device_driven) && name && object_try_and_get(device_index))
	{
		int k = pending_overlay_find(device_index, TRUE);

		snprintf(h2_pending_overlays[k].name, sizeof(h2_pending_overlays[k].name), "%s", name);
		if (getenv("HALO_H2_DEVICE_LOG"))
			platform_log("h2 cinematics: device %08lx overlay %s kept for its first track", (unsigned long)device_index, name);
		return TRUE;
	}
	if (!a || !a->device_driven || !name || !(object = object_try_and_get(device_index)) ||
		object_definition_get(object->definition_index)->object.model.index == NONE ||
		(handle = device_graph_handle(device_index)) == NONE)
	{
		return FALSE;
	}
	track_free(&a->overlay);
	free(a->overlay_map);
	a->overlay_map = NULL;
	a->overlay_from = a->overlay_to = 0.0f;
	a->overlay_ticks = 0;
	if (!track_decode(handle, name, &a->overlay) || a->overlay.node_count < 1)
	{
		track_free(&a->overlay);
		return FALSE;
	}
	model = model_definition_get(object_definition_get(object->definition_index)->object.model.index);
	a->overlay_map = malloc(sizeof(short) * (size_t)(model->nodes.count > 0 ? model->nodes.count : 1));
	for (n = 0; a->overlay_map && n < model->nodes.count; n++)
	{
		struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, n, struct model_node);

		a->overlay_map[n] = NONE;
		for (k = 0; k < a->overlay.node_count; k++)
		{
			if (node_names_match(node->name, a->overlay.node_names[k]))
			{
				a->overlay_map[n] = k;
				break;
			}
		}
	}
	if (getenv("HALO_H2_DEVICE_LOG"))
		platform_log("h2 cinematics: device %08lx overlay %s: %d frames", (unsigned long)device_index, name, (int)a->overlay.frame_count);
	return TRUE;
}

/* (device_animate_overlay device position seconds ...) the overlay's
position moved there over the seconds */
void h2_cinematics_device_overlay_animate(long device_index, real position, real seconds)
{
	struct object_animation *a = object_animation_find(device_index);

	if (!a || !a->device_driven)
	{
		int k = pending_overlay_find(device_index, FALSE);

		if (k >= 0)
		{
			h2_pending_overlays[k].from = h2_pending_overlays[k].to;
			h2_pending_overlays[k].to = position;
			h2_pending_overlays[k].start = game_time_get();
			h2_pending_overlays[k].ticks = (long)(seconds * 30.0f + 0.5f);
		}
		return;
	}
	if (!a->overlay.frame_count)
		return;
	{
		real now = a->overlay_to;

		if (a->overlay_ticks > 0)
		{
			real t = (real)(game_time_get() - a->overlay_start) / (real)a->overlay_ticks;

			if (t >= 0.0f && t < 1.0f)
				now = a->overlay_from + (a->overlay_to - a->overlay_from) * t;
		}
		a->overlay_from = now;
	}
	a->overlay_to = position;
	a->overlay_start = game_time_get();
	a->overlay_ticks = (long)(seconds * 30.0f + 0.5f);
}

/* the ticks an object's animation has left (NONE: none plays) */
long h2_cinematics_object_time(long object_index)
{
	long i;

	for (i = 0; i < h2_cine.object_count; i++)
	{
		struct object_animation const *a = &h2_cine.objects[i];

		if (a->object_index == object_index)
			return a->loop ? 0 : MAX(0, a->track.frame_count - 1 - (game_time_get() - a->start_time));
	}
	return NONE;
}

/* the cluster a running cutscene's camera is in, NONE (players.c's PVS) */
short h2_cinematics_camera_cluster(void)
{
	static long time = NONE;
	static short cluster = NONE;

	if (!h2_cine.camera_active || !h2_cine.camera.frame_count)
		return NONE;
	if (time != game_time_get())
	{
		real_point3d position;
		real_vector3d forward, up;
		real field_of_view;
		struct location location;

		time = game_time_get();
		cluster = NONE;
		if (h2_cinematics_camera_frame(&position, &forward, &up, &field_of_view))
		{
			scenario_location_from_point(&location, &position);
			cluster = location.cluster_index;
		}
	}
	return cluster;
}

/* an animated object let go */
H2_ALIGN static void object_animation_release(struct object_animation *a)
{
	struct object_datum *object = a->spared ? object_try_and_get(a->object_index) : NULL;

	if (object)
		SET_FLAG(object->object.damage_flags, _object_cannot_take_damage_bit, FALSE);
	track_free(&a->track);
	free(a->node_map);
	free(a->blend_rotation);
	free(a->blend_translation);
	track_free(&a->overlay);
	free(a->overlay_map);
	*a = h2_cine.objects[--h2_cine.object_count];
}

/* an object's animation stopped (unit_stop_custom_animation, a deleted
object) */
void h2_cinematics_object_stop(long object_index)
{
	long i;

	for (i = 0; i < h2_cine.object_count; i++)
	{
		if (h2_cine.objects[i].object_index == object_index)
		{
			object_animation_release(&h2_cine.objects[i]);
			return;
		}
	}
}

/* (h2_scripts_update, each tick) the animated objects moved; those done
or gone let go */
/* a place carried as a frame moved from `from` to `to` (the place's point and
axes in from's space put in to's) */
static void place_carry(struct place *place, real_matrix4x3 const *from, real_matrix4x3 const *to)
{
	real_point3d local;
	real_vector3d forward, up;

	matrix4x3_inverse_transform_point(from, &place->position, &local);
	matrix4x3_transform_point(to, &local, &place->position);
	matrix4x3_inverse_transform_normal(from, &place->forward, &forward);
	matrix4x3_transform_normal(to, &forward, &place->forward);
	matrix4x3_inverse_transform_normal(from, &place->up, &up);
	matrix4x3_transform_normal(to, &up, &place->up);
	normalize3d(&place->forward);
	normalize3d(&place->up);
	place->left.i = place->up.j * place->forward.k - place->up.k * place->forward.j;
	place->left.j = place->up.k * place->forward.i - place->up.i * place->forward.k;
	place->left.k = place->up.i * place->forward.j - place->up.j * place->forward.i;
}

/* (each tick, before its place) an animation anchored where its object was
(custom_animation(_loop)) carried by what carries the object: an attached
object's parent's node (03b's Scarab pilots hooked to its seats), or the
machine's node under a unit standing on one (the pilots let go, still in
their idle, on the walking Scarab's cockpit floor - Halo 2 plays a unit's
custom animation where it is, its ground moving it): the node's move since
last tick */
static void object_animation_follow_carrier(struct object_animation *a)
{
	struct object_datum *object = object_try_and_get(a->object_index);
	long carrier = NONE;
	short node = NONE;
	real_matrix4x3 const *now;

	if (!object || !a->anchored_here)
	{
		a->carrier_index = NONE;
		return;
	}
	if (object->object.parent_object_index != NONE)
	{
		if (object_try_and_get(object->object.parent_object_index))
		{
			carrier = object->object.parent_object_index;
			node = object->object.parent_node_index;
		}
	}
	else if (TEST_FLAG(_object_mask_unit, object->object.type))
	{
		real_point3d start = object->object.position;
		real_vector3d down = { 0.0f, 0.0f, -0.8f };
		struct collision_result collision;

		start.z += 0.3f;
		if (collision_test_vector(FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_machines_bit) |
				FLAG(_collision_test_front_facing_surfaces_bit), &start, &down, a->object_index, &collision) &&
			collision.type == _collision_result_object && collision.node_index >= 0 &&
			object_try_and_get_and_verify_type(collision.object_index, _object_mask_machine))
		{
			carrier = collision.object_index;
			node = collision.node_index;
		}
	}
	if (carrier == NONE)
	{
		a->carrier_index = NONE;
		return;
	}
	now = object_get_node_matrix(carrier, node);
	if (a->carrier_index == carrier && a->carrier_node == node)
		place_carry(&a->anchor, &a->carrier_matrix, now);
	a->carrier_index = carrier;
	a->carrier_node = node;
	a->carrier_matrix = *now;
}

void h2_cinematics_update(void)
{
	long i;

	/* (a test: HALO_H2_CINEMATIC_SHOTS=n, a shot each n frames of a cutscene's
	camera, named for the animation and the frame) */
	if (h2_cine.camera_active && h2_cine.camera.frame_count)
	{
		static int shots = -1;
		extern char halo_screenshot_name[64];
		long frame = game_time_get() - h2_cine.camera_start;

		if (shots < 0)
			shots = getenv("HALO_H2_CINEMATIC_SHOTS") ? atoi(getenv("HALO_H2_CINEMATIC_SHOTS")) : 0;
		if (shots > 0 && frame % shots == 0 && frame < h2_cine.camera.frame_count)
			snprintf(halo_screenshot_name, sizeof(halo_screenshot_name), "%s_%04ld", h2_cine.camera_name, frame);
	}

	/* (a test: HALO_H2_CINEMATIC_TEST="<graph tag>|<animation>" plays it,
	looped, on the first player's unit where it stands) */
	{
		static long tested_time = NONE;
		char const *test = HALO_ENV_ONCE("HALO_H2_CINEMATIC_TEST");
		struct h2_cache *cache = h2_scripts_cache();

		/* (again after a checkpoint is reverted to) */
		if (tested_time != NONE && game_time_get() < tested_time)
			tested_time = NONE;
		if (test && cache && tested_time == NONE)
		{
			long player_index = data_next_index(player_data, NONE);
			long unit = player_index != NONE ? player_get(player_index)->unit_index : NONE;
			char graph[256], *bar;

			snprintf(graph, sizeof(graph), "%s", test);
			bar = strchr(graph, '|');
			if (unit != NONE && bar && game_time_get() > (getenv("HALO_H2_CINEMATIC_TEST_TICK") ? atol(getenv("HALO_H2_CINEMATIC_TEST_TICK")) : 150))
			{
				struct h2_cache_tag tag;
				long jmad;

				*bar = 0;
				jmad = h2_cache_tag_find(cache, 'jmad', graph);
				tested_time = game_time_get();
				/* (HALO_H2_CINEMATIC_TEST_BIPED=marine: on one of the level's
				Halo 2 bipeds made there, three units in front of the player) */
				if (getenv("HALO_H2_CINEMATIC_TEST_BIPED"))
				{
					char biped_name[128];
					long definition;

					snprintf(biped_name, sizeof(biped_name), "h2\\characters\\%s_mp\\%s_mp", getenv("HALO_H2_CINEMATIC_TEST_BIPED"),
						getenv("HALO_H2_CINEMATIC_TEST_BIPED"));
					definition = tag_loaded('bipd', biped_name);
					if (definition != NONE)
					{
						struct object_placement_data data;
						struct object_datum *player_unit = object_get(unit);

						object_placement_data_new(&data, definition, NONE);
						object_get_origin(unit, &data.position);
						data.position.x += (getenv("HALO_H2_CINEMATIC_TEST_DISTANCE") ? (real)atof(getenv("HALO_H2_CINEMATIC_TEST_DISTANCE")) : 3.0f) * player_unit->object.forward.i;
						data.position.y += (getenv("HALO_H2_CINEMATIC_TEST_DISTANCE") ? (real)atof(getenv("HALO_H2_CINEMATIC_TEST_DISTANCE")) : 3.0f) * player_unit->object.forward.j;
						data.forward.i = -player_unit->object.forward.i, data.forward.j = -player_unit->object.forward.j, data.forward.k = 0.0f;
						unit = object_new(&data);
						platform_log("h2 cinematics: test biped %s made: %08lX", biped_name, (unsigned long)unit);
					}
					else
						platform_log("h2 cinematics: test biped %s: not in the level", biped_name);
				}
				h2_cine.test_in_place = getenv("HALO_H2_CINEMATIC_TEST_IN_PLACE") != NULL;
				if (unit != NONE && jmad != NONE && h2_cache_tag_get(cache, jmad, &tag))
					platform_log("h2 cinematics: test %s %s: %ld frames", graph, bar + 1,
						h2_cinematics_object_animation(unit, (long)tag.handle, bar + 1, NONE, NONE, TRUE));
			}
		}
	}

	/* (perf/h2-tick-gap, HALO_TICK_PROFILE) every 300 ticks: the animated
	objects, how many are looped / device-driven / active (objects_update
	recomputes an active object's node matrices after this), the
	set_positions a tick, and objects.c's node matrix computes a tick with
	the repeats within a tick */
	{
		extern unsigned long object_node_compute_census[2];
		static int census_on = -1;
		static unsigned long ticks, positions, looped, driven, active, animated;

		if (census_on < 0)
			census_on = getenv("HALO_TICK_PROFILE") && atoi(getenv("HALO_TICK_PROFILE"));
		if (census_on > 0)
		{
			for (i = 0; i < h2_cine.object_count; i++)
			{
				struct object_animation *a = &h2_cine.objects[i];
				struct object_header_datum *header = object_try_and_get(a->object_index) ? object_header_get(a->object_index) : NULL;

				animated++;
				looped += a->loop != 0;
				driven += a->device_driven != 0;
				active += header && TEST_FLAG(header->flags, _object_header_active_bit);
				positions += !a->device_driven;
			}
			if (++ticks >= 300)
			{
				platform_log("h2 cinematics census (a tick): %.1f animated objects (%.1f looped, %.1f device-driven, %.1f active), "
					"%.1f set_positions | node matrix computes %.1f, %.1f of them an object's second this tick",
					animated / 300.0, looped / 300.0, driven / 300.0, active / 300.0, positions / 300.0,
					object_node_compute_census[0] / 300.0, object_node_compute_census[1] / 300.0);
				ticks = positions = looped = driven = active = animated = 0;
				object_node_compute_census[0] = object_node_compute_census[1] = 0;
			}
		}
	}
	for (i = 0; i < h2_cine.object_count;)
	{
		struct object_animation *a = &h2_cine.objects[i];
		struct place place;
		long elapsed = game_time_get() - a->start_time;

		if (!object_try_and_get(a->object_index) || (!a->loop && !a->device_driven && elapsed > a->track.frame_count + 30))
		{
			object_animation_release(a);
			continue;
		}
		/* (nothing in a cutscene's scripts hurts its actors but what plays
		around them: 05a's Jackal, in a track to the shot's end, died to a
		pod's blast at its frame 90 and lay there through his close-up) */
		if (h2_cine.camera_active && !a->spared && object_try_and_get_and_verify_type(a->object_index, _object_mask_unit) &&
			!TEST_FLAG(object_get(a->object_index)->object.damage_flags, _object_cannot_take_damage_bit))
		{
			SET_FLAG(object_get(a->object_index)->object.damage_flags, _object_cannot_take_damage_bit, TRUE);
			a->spared = TRUE;
		}
		/* (a device's track: the device stays at its own place, the
		anchor - moved by a teleport or a revert, it is the anchor -; its
		nodes are posed above it, h2_cinematics_node_matrices) */
		if (a->device_driven)
		{
			object_place(a->object_index, &a->anchor);
			i++;
			continue;
		}
		object_animation_follow_carrier(a);
		place_compose(&a->anchor, track_place(&a->track, a->in_place ? 0 : animation_frame(a)), &place);
		if (valid_real_vector3d_axes2(&place.forward, &place.up) && fabsf(place.position.x) < 5000.0f &&
			fabsf(place.position.y) < 5000.0f && fabsf(place.position.z) < 5000.0f)
		{
			/* (an object attached to another - 05a's Regret hologram on its
			projector - keeps its place in its parent node's space: the world
			place written there was transformed again, thousands of units
			off, and halted object_compute_node_matrices) */
			struct object_datum *object = object_get(a->object_index);

			if (object->object.parent_object_index != NONE)
			{
				real_matrix4x3 inverse;
				struct place world = place;

				matrix4x3_inverse(object_get_node_matrix(object->object.parent_object_index, object->object.parent_node_index),
					&inverse);
				matrix4x3_transform_point(&inverse, &world.position, &place.position);
				matrix4x3_transform_normal(&inverse, &world.forward, &place.forward);
				matrix4x3_transform_normal(&inverse, &world.up, &place.up);
				/* (made orthonormal again: the parent's matrix's rounding and
				scale left them a little off, and the node matrices' check is
				strict) */
				normalize3d(&place.forward);
				{
					real along = dot_product3d(&place.up, &place.forward);

					place.up.i -= along * place.forward.i;
					place.up.j -= along * place.forward.j;
					place.up.k -= along * place.forward.k;
				}
				normalize3d(&place.up);
			}
			/* (a vehicle's velocity its animation's motion: its gravity's, run
			up under a script holding it, went to what it let go - the
			Pelican's Scorpion left at 10 wu/s, objects_detach) */
			if (object->object.parent_object_index == NONE && object->object.type == _object_type_vehicle)
			{
				vector_from_points3d(&object->object.position, &place.position, &object->object.translational_velocity);
				set_real_vector3d(&object->object.angular_velocity, 0.0f, 0.0f, 0.0f);
			}
			object_set_position(a->object_index, &place.position, &place.forward, &place.up);
			/* (its scale the animation's: Halo 2's cinematic sets are models
			shrunk by their animations - x01's ships and matte paintings;
			drawn whole size, the camera stood inside them) */
			if (a->track.scaled)
				object_get(a->object_index)->object.scale = place.scale;
		}
		i++;
	}
}

/* (objects.c, object_compute_node_matrices) an animated object's nodes
posed as its animation poses them at the frame now: each the animation's
node of its name (its rotation and translation where the animation sets
them, else the model's default), under its parent, the root under the
anchor */
/* (a speaker not in a cutscene) its face's nodes' local poses changed by
the phonemes now, from the world matrices its animation made: each node's
own (its place under its parent) taken out, the change put in, it and its
children placed again under their parents - parents first, as the model
orders them */
H2_ALIGN static void face_pose_matrices(struct face const *face, struct emotion const *emotion, long object_index, real_matrix4x3 *matrices,
	short matrix_count)
{
	struct object_datum *object = object_try_and_get(object_index);
	struct model *model;
	real_matrix4x3 old[MAXIMUM_NODES_PER_MODEL];
	boolean moved[MAXIMUM_NODES_PER_MODEL];
	short count, i;

	if (!object || object_definition_get(object->definition_index)->object.model.index == NONE)
		return;
	model = model_definition_get(object_definition_get(object->definition_index)->object.model.index);
	count = (short)MIN(model->nodes.count, MIN(matrix_count, MAXIMUM_NODES_PER_MODEL));
	memcpy(old, matrices, sizeof(real_matrix4x3) * (size_t)count);
	memset(moved, 0, sizeof(moved));
	for (i = 0; i < count; i++)
	{
		short parent = TAG_BLOCK_GET_ELEMENT(&model->nodes, i, struct model_node)->parent_node_index;
		boolean face_node = (face && i < face->node_map_count && face->node_map[i] != NONE) ||
			(emotion && i < emotion->node_map_count && emotion->node_map[i] != NONE);
		struct place p_old, p_new, world, local, change;
		float rotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f }, translation[3] = { 0.0f, 0.0f, 0.0f };

		if (parent < 0 || parent >= i || (!face_node && !moved[parent]))
			continue;
		/* (its place under its parent, as it was) */
		p_old.position = old[parent].position, p_old.forward = old[parent].forward, p_old.left = old[parent].left, p_old.up = old[parent].up;
		p_old.scale = old[parent].scale;
		world.position = old[i].position, world.forward = old[i].forward, world.left = old[i].left, world.up = old[i].up, world.scale = 1.0f;
		{
			real s = p_old.scale > 0.0f ? p_old.scale : 1.0f;
			real_vector3d d;

			d.i = world.position.x - p_old.position.x, d.j = world.position.y - p_old.position.y, d.k = world.position.z - p_old.position.z;
			local.position.x = dot_product3d(&d, &p_old.forward) / s;
			local.position.y = dot_product3d(&d, &p_old.left) / s;
			local.position.z = dot_product3d(&d, &p_old.up) / s;
#define LOCAL_AXIS(out, v) (out).i = dot_product3d(&(v), &p_old.forward), (out).j = dot_product3d(&(v), &p_old.left), (out).k = dot_product3d(&(v), &p_old.up)
			LOCAL_AXIS(local.forward, world.forward);
			LOCAL_AXIS(local.left, world.left);
			LOCAL_AXIS(local.up, world.up);
#undef LOCAL_AXIS
			local.scale = old[i].scale > 0.0f && p_old.scale > 0.0f ? old[i].scale / p_old.scale : 1.0f;
		}
		/* (the change: its rotation in the node's own axes, its
		translation in the parent's) */
		if (face_node)
		{
			emotion_pose(emotion, i, rotation, translation);
			face_pose(face, i, rotation, translation);
			place_from_quaternion(rotation, translation, &change);
			change.position.x = change.position.y = change.position.z = 0.0f;
			{
				struct place turned;

				place_compose(&local, &change, &turned);
				turned.position = local.position;
				turned.scale = local.scale;
				local = turned;
			}
			local.position.x += translation[0], local.position.y += translation[1], local.position.z += translation[2];
		}
		p_new.position = matrices[parent].position, p_new.forward = matrices[parent].forward, p_new.left = matrices[parent].left;
		p_new.up = matrices[parent].up, p_new.scale = matrices[parent].scale;
		place_compose(&p_new, &local, &world);
		if (!valid_real_vector3d_axes2(&world.forward, &world.up))
			continue;
		matrices[i].forward = world.forward, matrices[i].left = world.left, matrices[i].up = world.up;
		matrices[i].position = world.position;
		matrices[i].scale = world.scale > 0.0f ? world.scale : 1.0f;
		moved[i] = TRUE;
	}
}

/* (render_nodes: perf/h2-render-nodes - the compute whose Halo 2 render-only
matrices the tick left to the render, h2_render_nodes.c; NULL: none) an
object this poses has them made first, here: they are posed over */
static void h2_cinematics_node_matrices_pose(long object_index, real_matrix4x3 *matrices, short matrix_count,
	struct h2_render_nodes_compute *render_nodes);

/* (perf/ce-floor-2) every object's node matrices ask (objects.c): with no
cutscene animating anything and no face or emotion loaded - a Halo CE
level, always - the pose below finds nothing and returns; that answer
first, without the call into its 7 KB (I-cache) and its large frame */
void h2_cinematics_node_matrices(long object_index, real_matrix4x3 *matrices, short matrix_count,
	struct h2_render_nodes_compute *render_nodes)
{
	if (!h2_cine.object_count && !h2_faces.count && !h2_emotions.count)
		return;
	h2_cinematics_node_matrices_pose(object_index, matrices, matrix_count, render_nodes);
}

__attribute__((noinline)) static void h2_cinematics_node_matrices_pose(long object_index, real_matrix4x3 *matrices, short matrix_count,
	struct h2_render_nodes_compute *render_nodes)
{
	struct object_animation *a = NULL;
	struct face const *face;
	struct emotion const *emotion;
	struct object_datum *object;
	struct model *model;
	struct place world[MAXIMUM_NODES_PER_MODEL];
	boolean done[MAXIMUM_NODES_PER_MODEL];
	long i, frame, pass;
	real toward = 0.0f;
	short count;

	for (i = 0; i < h2_cine.object_count && !a; i++)
	{
		if (h2_cine.objects[i].object_index == object_index && h2_cine.objects[i].node_map)
			a = &h2_cine.objects[i];
	}
	/* (no cutscene's animation: a face speaking a line in play - an AI's
	voice - posed over the matrices its own animation made) */
	if (!a)
	{
		face = h2_faces.count ? face_of(object_index) : NULL;
		emotion = h2_emotions.count ? emotion_of(object_index) : NULL;
		if (face || emotion)
		{
			if (render_nodes)
				h2_render_nodes_make_whole(object_index, render_nodes, matrices);
			face_pose_matrices(face, emotion, object_index, matrices, matrix_count);
		}
		return;
	}
	if (render_nodes)
		h2_render_nodes_make_whole(object_index, render_nodes, matrices);
	if (!(object = object_try_and_get(object_index)) ||
		object_definition_get(object->definition_index)->object.model.index == NONE)
	{
		return;
	}
	model = model_definition_get(object_definition_get(object->definition_index)->object.model.index);
	face = face_of(object_index);
	emotion = emotion_of(object_index);
	count = (short)MIN(MIN(model->nodes.count, a->node_map_count), MIN(matrix_count, MAXIMUM_NODES_PER_MODEL));
	frame = animation_frame_between(a, &toward);
	memset(done, 0, sizeof(done));
	/* (parents first: a pass for each depth, the models' parents before
	their children as a rule) */
	for (pass = 0; pass < count; pass++)
	{
		boolean all = TRUE;

		for (i = 0; i < count; i++)
		{
			struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, i, struct model_node);
			short k = a->node_map[i], parent = node->parent_node_index;
			float rotation[4], translation[3], scale = 1.0f;
			struct place local;

			if (done[i])
				continue;
			if (parent >= 0 && parent < count && !done[parent])
			{
				all = FALSE;
				continue;
			}
			/* (the model's defaults are this engine's quaternions: Halo 2's
			conjugated) */
			rotation[0] = -node->default_rotation.v.i, rotation[1] = -node->default_rotation.v.j;
			rotation[2] = -node->default_rotation.v.k, rotation[3] = node->default_rotation.w;
			translation[0] = node->default_translation.x, translation[1] = node->default_translation.y;
			translation[2] = node->default_translation.z;
			/* (the root, when the animation has no node of its name: the
			animation's root) */
			if (k == NONE && (parent < 0 || parent >= count))
				k = 0;
			if (ENV_TEST_DEFAULTS() && parent >= 0 && parent < count)
				k = NONE;
			if (k != NONE && k < a->track.node_count)
			{
				struct h2_animation_pose posed, *pose = &posed;

				track_pose_between(&a->track, frame, toward, k, &posed);
				if (a->track.sets[k] & 1)
					memcpy(rotation, pose->rotation, sizeof(rotation));
				if (a->track.sets[k] & 2)
					memcpy(translation, (a->in_place && k == 0 ? &a->track.poses0[k] : pose)->translation, sizeof(translation));
				if ((a->track.sets[k] & 4) && pose->scale > 0.001f && pose->scale < 100.0f && !ENV_NO_SCALE())
					scale = pose->scale;
			}
			/* (a device's new position track: its nodes but the root
			blended from the last track's pose over the interpolation) */
			if (a->blend_ticks > 0 && i > 0 && i < a->blend_count && a->blend_rotation)
			{
				real t = (real)(game_time_get() - a->blend_start) / (real)a->blend_ticks;

				if (t < 1.0f)
				{
					float const *from = a->blend_rotation[i];
					float sign = from[0] * rotation[0] + from[1] * rotation[1] + from[2] * rotation[2] + from[3] * rotation[3] < 0.0f ? -1.0f : 1.0f;
					short c;

					if (t < 0.0f)
						t = 0.0f;
					for (c = 0; c < 4; c++)
						rotation[c] = from[c] * sign * (1.0f - t) + rotation[c] * t;
					for (c = 0; c < 3; c++)
						translation[c] = a->blend_translation[i][c] * (1.0f - t) + translation[c] * t;
				}
			}
			/* (a device's overlay: its delta over the pose) */
			if (a->overlay_map && a->overlay.frame_count > 0 && i < a->node_map_count && a->overlay_map[i] != NONE &&
				a->overlay_map[i] < a->overlay.node_count)
			{
				short ko = a->overlay_map[i];
				/* (through the decode window, as the track's own poses: the
				overlay's poses hold its window alone, and its frame read
				straight from them was past the window's end - garbage, NaN
				nodes: 03b's Scarab's legs at its arrival, idle under
				fire_down, the halt "cc0.red" in the model's fog) */
				struct h2_animation_pose delta_pose, *delta = &delta_pose;

				track_pose(&a->overlay, overlay_frame(a), ko, &delta_pose);

				if (a->overlay.sets[ko] & 1)
				{
					/* (Halo 2's base x delta; this engine's quaternions
					are Halo 2's conjugated: delta x base here) */
					float const *d = delta->rotation;
					float r[4];

					r[0] = d[3] * rotation[0] + d[0] * rotation[3] + d[1] * rotation[2] - d[2] * rotation[1];
					r[1] = d[3] * rotation[1] - d[0] * rotation[2] + d[1] * rotation[3] + d[2] * rotation[0];
					r[2] = d[3] * rotation[2] + d[0] * rotation[1] - d[1] * rotation[0] + d[2] * rotation[3];
					r[3] = d[3] * rotation[3] - d[0] * rotation[0] - d[1] * rotation[1] - d[2] * rotation[2];
					memcpy(rotation, r, sizeof(r));
				}
				if (a->overlay.sets[ko] & 2)
				{
					translation[0] += delta->translation[0];
					translation[1] += delta->translation[1];
					translation[2] += delta->translation[2];
				}
			}
			emotion_pose(emotion, (short)i, rotation, translation);
			face_pose(face, (short)i, rotation, translation);
			place_from_quaternion(rotation, translation, &local);
			local.scale = scale;
			place_compose(parent >= 0 && parent < count ? &world[parent] : &a->anchor, &local, &world[i]);
			done[i] = TRUE;
		}
		if (all)
			break;
	}
	if (HALO_ENV_ONCE("HALO_H2_CINEMATIC_LOG") && frame % 30 == 0 && count > 0)
	{
		float change = 0.0f;
		long n, c;

		for (n = 0; n < a->track.node_count; n++)
			for (c = 0; c < 4; c++)
			{
				struct h2_animation_pose posed;

				track_pose(&a->track, frame, n, &posed);
				change += fabsf(posed.rotation[c] - a->track.poses0[n].rotation[c]);
			}
		platform_log("h2 cinematics: frame %ld rotation change from frame 0: %.3f (sets %d %d %d)", frame, change,
			a->track.sets[0], a->track.sets[1], a->track.sets[a->track.node_count / 2]);
	}
	if (HALO_ENV_ONCE("HALO_H2_CINEMATIC_LOG") && frame % 30 == 0 && count > 0)
	{
		real_point3d origin;

		object_get_origin(object_index, &origin);
		platform_log("h2 cinematics: posed %08lX (vitality %.2f damage flags %04X flags %08lX) frame %ld: node 0 %.2f %.2f %.2f (%s), node %d %.2f %.2f %.2f; object at %.2f %.2f %.2f",
			(unsigned long)object_index, object->object.body_vitality, (unsigned)object->object.damage_flags, (unsigned long)object->object.flags,
			frame, world[0].position.x, world[0].position.y, world[0].position.z,
			TAG_BLOCK_GET_ELEMENT(&model->nodes, 0, struct model_node)->name, count - 1, world[count - 1].position.x,
			world[count - 1].position.y, world[count - 1].position.z, origin.x, origin.y, origin.z);
	}
	if (HALO_ENV_ONCE("HALO_H2_CINEMATIC_LOG") && frame % 60 == 1)
	{
		for (i = 0; i < count && i < 6; i++)
		{
			/* (this engine's own, relative to its node 0; ours, relative to ours) */
			real_matrix4x3 const *m0 = &matrices[0], *m = &matrices[i];
			real dx = m->position.x - m0->position.x, dy = m->position.y - m0->position.y, dz = m->position.z - m0->position.z;
			real ex = world[i].position.x - world[0].position.x, ey = world[i].position.y - world[0].position.y, ez = world[i].position.z - world[0].position.z;

			platform_log("h2 cinematics: compare node %ld: engine fwd %.2f %.2f %.2f up %.2f %.2f %.2f off %.3f %.3f %.3f | ours fwd %.2f %.2f %.2f up %.2f %.2f %.2f off %.3f %.3f %.3f",
				i, m->forward.i, m->forward.j, m->forward.k, m->up.i, m->up.j, m->up.k, dx, dy, dz,
				world[i].forward.i, world[i].forward.j, world[i].forward.k, world[i].up.i, world[i].up.j, world[i].up.k, ex, ey, ez);
		}
	}
	for (i = 0; i < count; i++)
	{
		if (!done[i] || !valid_real_vector3d_axes2(&world[i].forward, &world[i].up))
			continue;
		matrices[i].scale = world[i].scale > 0.0f ? world[i].scale : 1.0f;
		matrices[i].forward = world[i].forward;
		matrices[i].left = world[i].left;
		matrices[i].up = world[i].up;
		matrices[i].position = world[i].position;
	}
}

/* the counts: animations decoded, not found */
void h2_cinematics_counts(long *decoded, long *failed, long *objects_missing)
{
	*decoded = h2_cine.decoded;
	*failed = h2_cine.failed;
	*objects_missing = h2_cine.objects_missing;
}
