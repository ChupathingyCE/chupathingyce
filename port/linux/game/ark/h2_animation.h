/*
H2_ANIMATION.H

Halo 2's animation graphs (model_animation_graph, 'jmad') made this engine's
(model_animations, 'antr') for the player models (h2_animation.c), read from
the player's own MCC maps.

How MCC's Halo 2 keeps an animation (read from the owner's shared.map and
checked frame by frame against the H2EK's source animations of the same
names - the uncompressed codecs exactly, the keyframed ones at their keys):
its tag data (its animation entry's data field, 0x28) holds a static codec
block (dd bytes, the entry's 0x36: the nodes that do not move), then an
animated codec block (the entry's 0x3C bytes), then the static nodes' flags
and the animated ones' (0x30 and 0x31 bytes: rotation, translation, scale,
64 bits each), then the movement (0x32 bytes: dx dy [dz] [dyaw] a frame).
A codec block begins with its codec, its rotated, translated and scaled
node counts, an error and a compression rate; codec 1 (static) and 3
(8-byte quaternions, float translations and scales, a node's frames
together) and 8 (the same with float quaternions) give their offsets and
strides; 4 and 6 (byte frame numbers) and 5 and 7 (word) keep keyframes - a
32-bit definition a node (12 bits its count, 20 its first key), the frame
numbers, then the values - interpolated linearly between keys. Rotations
are Halo 2's quaternions, this engine's conjugated; an overlay's frames are
the change from its base (Halo 2: frame = base x change), this engine's
the same change conjugated, applied first.

The graph: Halo 2's modes (combat, crouch, vehicles' seats), weapon classes
and weapon types (each with actions, overlays and deaths) are this engine's
unit seats, weapon classes and weapon types, each animation put where this
engine's units look for it by name (stand-in tables in h2_animation.c).
Animations are written compressed (this engine's keyframed form: 6-byte
quaternions, keys where linear interpolation misses), which keeps a graph
near the size Halo 2's is.
*/

#ifndef __H2_ANIMATION_H
#define __H2_ANIMATION_H

#include "cseries.h"

struct h2_cache;
struct h2_biped_host;

/* ---------- constants */

enum
{
	H2_ANIMATION_MAXIMUM_NODES = 96,
};

/* ---------- structures */

/* a node's place: its rotation (this engine's convention once converted),
translation and scale */
struct h2_animation_pose
{
	float rotation[4];
	float translation[3];
	float scale;
};

/* a graph made */
struct h2_graph;

/* what was made of it */
struct h2_graph_report
{
	long animations;            /* this engine's, written */
	long h2_animations;         /* Halo 2's used */
	long seats;
	long classes;
	long bytes;                 /* the animations' data */
	long keys;
	long codec_counts[9];       /* Halo 2's codecs decoded */
	long missing;               /* slots with no Halo 2 animation */
};

/* ---------- a graph's source

Where a graph's tag data is read from and how it is laid out. Halo 2's is this
file's own (over an h2_cache; h2_graph_make). Halo 3's graphs are the same
design - nodes, blend screens, animations, modes of weapon classes of weapon
types with actions and overlays, deaths and damage, variants - at other offsets
(MCC's 12-byte blocks), with the animations' data in resource pages instead of
the tag: h3_animation.c reads them through one of these (h2_graph_make_from),
and everything after the read - the tables, the baking, the variants, this
engine's compression and the tag written - is this file's, shared. */

/* a graph's offsets: where each block is in the tag and in its elements */
struct h2_graph_layout
{
	unsigned short nodes, node_size;              /* (a node: its name at 0) */
	unsigned short blend_screens, blend_screen_size;   /* (a screen: its name, then 24 bytes) */
	unsigned short animations, animation_size;
	unsigned short modes, mode_size;              /* (a mode: its name, its classes block at 4) */
	unsigned short class_size;                    /* (a class: its name, its types block at 4) */
	unsigned short type_size, type_overlays, type_damage;   /* (a type: its name, its actions block at 4) */
	unsigned short damage_size, direction_size;   /* (a damage entry: its label, its directions block at 4; a direction: its regions block at 0) */
	unsigned short inheritance, inheritance_size; /* (0: none) */
	/* an animation entry's fields */
	unsigned short a_type, a_frame_info_type, a_screen, a_frames, a_weight, a_loop, a_next, a_events;
	/* whether the graph's node names are matched to the model's loosely (case,
	'_' and ' ', a "bip01 " prefix): Halo 3's l_thigh is Halo 2's l thigh */
	boolean names_loose;
};

/* an animation's data: Halo 2's layout (the top of this file) */
struct h2_animation_data
{
	unsigned char const *bytes;
	unsigned long static_size, animated_size;
	unsigned long static_flags, animated_flags;
	unsigned long movement;
};

struct h2_graph_source
{
	void *context;
	struct h2_graph_layout const *layout;
	/* a tag's group and address (FALSE: no such tag) */
	boolean (*tag_get)(void *context, long tag, unsigned long *group_tag, unsigned long *address);
	char const *(*tag_name)(void *context, long tag);
	/* a block at address: its count and its elements' address */
	boolean (*block)(void *context, unsigned long address, unsigned long element_size, long *count, unsigned long *elements);
	/* size bytes at address, good until the next few calls (NULL: not there) */
	void const *(*at)(void *context, unsigned long address, unsigned long size);
	/* a tag reference's tag (NONE) */
	long (*reference)(void *context, unsigned long address);
	boolean (*string)(void *context, unsigned long id, char *out, long size);
	/* the animation entry's data (FALSE: not there); bytes good until the next call */
	boolean (*animation_data)(void *context, unsigned long entry_address, struct h2_animation_data *out);
};

/* ---------- prototypes/H2_ANIMATION.C */

/* the graph of a jmad tag read through a source (h2_graph_make's arguments
otherwise). The source is copied; its context must outlive the make (the
graph reads nothing after it) */
struct h2_graph *h2_graph_make_from(
	struct h2_graph_source const *source,
	long jmad_tag,
	char const (*node_names)[32],
	struct h2_animation_pose const *node_defaults,
	short const *node_parents,
	short node_count,
	int flies,
	char *why,
	long why_size);

/* an animation's data in Halo 2's and Halo 3's form (the top of this file: static
block, animated block, their flags) decoded over poses (frame_count frames of
node_count nodes, given their defaults; the graph's own order) and which of them
it set, in sets: FALSE if it is not as expected */
boolean h2_animation_blocks_decode(
	struct h2_animation_pose *poses,
	unsigned char *sets,
	short node_count,
	short frame_count,
	unsigned char const *data,
	unsigned long static_size,
	unsigned long animated_size,
	unsigned long static_flags_size,
	unsigned long animated_flags_size);

/* the graph of a jmad tag (its index) made for a model of node_count
nodes (their names, default places in this engine's convention and
parents): NULL and why if it cannot be. The graph's nodes are the model's
(Halo 2's by name; ones the model left out are not animated) */
struct h2_graph *h2_graph_make(
	struct h2_cache *cache,
	long jmad_tag,
	char const (*node_names)[32],
	struct h2_animation_pose const *node_defaults,
	short const *node_parents,
	short node_count,
	int flies,
	char *why,
	long why_size);

/* the map the graph reads from later (one made over another thread's view of
the same map handed to the open that writes it) */
void h2_graph_cache_set(
	struct h2_graph *graph,
	struct h2_cache *cache);

void h2_graph_free(
	struct h2_graph *graph);

/* written as an antr tag's root (its offset; 0 if not) */
unsigned long h2_graph_write(
	struct h2_graph *graph,
	struct h2_biped_host const *host);

void h2_graph_report(
	struct h2_graph const *graph,
	struct h2_graph_report *report);

/* one Halo 2 animation decoded (for tests: tools/h2_animation_test.c):
frame_count * node_count poses, Halo 2's convention, the nodes it sets
(bit 0 rotation, 1 translation, 2 scale), its type and frame count; FALSE
if it cannot be */
boolean h2_animation_decode_raw(
	struct h2_cache *cache,
	long jmad_tag,
	long animation_index,
	struct h2_animation_pose **poses,
	unsigned char **node_sets,
	short *node_count,
	short *frame_count,
	short *type);

/* an animation's compressed data kept (a copy of Halo 2's, a few hundred
KB at most) and decoded a window of frames at a time: a cutscene's tracks,
which decoded whole took frames x nodes x 32 bytes each (1.4 MB for a
character of 55 nodes over 780 frames, 17 MB for Cairo's ceremony) and
failed on the Vita's C heap ("cannot be played": the Arbiter, the Grunts
and Marines of 01b's cutscene stood still or were not there) */
struct h2_animation_source;
struct h2_animation_source *h2_animation_source_open(struct h2_cache *cache, long jmad_tag, long animation_index);
struct h2_animation_source *h2_animation_source_copy(struct h2_animation_source const *from);
void h2_animation_source_close(struct h2_animation_source *source);
void h2_animation_source_figures(struct h2_animation_source const *source, short *node_count, short *frame_count, short *type,
	unsigned long *bytes);
boolean h2_animation_source_same(struct h2_animation_source const *a, struct h2_animation_source const *b);
/* frames [first, first + count) of every node into poses (count x
node_count), the nodes it sets into sets (node_count + 1 bytes) */
boolean h2_animation_source_decode(struct h2_animation_source const *source, short first, short count,
	struct h2_animation_pose *poses, unsigned char *sets);

#endif
