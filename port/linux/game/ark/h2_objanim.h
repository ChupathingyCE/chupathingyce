/*
H2_OBJANIM.H

Halo 2's animated objects (device machines and controls: Zanzibar's big
wheel, windmill blades, flywheels, house gate and its keypad) made this
engine's: their machine and control definitions, their render models'
node trees, their animation graphs' device animations, their collision
models node by node, the scenario's device groups and each placement's
power and position groups (h2_objanim.c). Everything is read from the
player's own MCC maps.

h2_level.c builds each object's model geometry and shaders as before and
calls this module for the rest through a host (struct h2_objanim_host):
the tag data it lays out. Nothing of h2_level.c's own structures is seen
here, so the two files merge apart.

What Halo 2 keeps (Assembly's Halo 2 plugins, checked against the owner's
zanzibar.map; the decoding is h2_animation.c's on h2engine/player-models,
with 32-bit node flags for graphs of up to 32 nodes):
- a device (mach, ctrl): flags at 0xBC (bit 0 position loops), power and
  position speeds (position units a second: Halo 2's caches keep speeds,
  not times) and accelerations at 0xC0..0xD4, its sounds at 0xDC (open,
  close, opened, closed, depowered, repowered: 8-byte references), delay
  time 0x10C, automatic activation radius 0x118;
- a machine: type 0x11C (0 door, 1 platform, 2 gear), flags 0x11E, door
  open time 0x120, collision response 0x12C, elevator node 0x12E;
- a control: type 0x11C (toggle switch, on, off, call button), triggers
  when 0x11E, call value 0x120, on/off/deny sounds 0x128/0x130/0x138;
- the object's model (hlmt, 0x34): render model 0, collision model 8,
  animation graph 0x10;
- the graph (jmad): skeleton nodes at 0x0C (0x20 each, name first),
  animations at 0x2C (0x60 each: name, type 0x10, frame count 0x14, the
  data 0x28, static/animated flag sizes 0x30/0x31, static codec size
  0x36, animated codec size 0x3C) - a device's position animation is
  "device:position", an overlay played at position x (frames - 1);
- the collision model (coll): regions 0x1C (0x0C: permutations at 4, 0x14:
  BSPs at 4, 0x44 each: node index, then the BSP's blocks as the
  structure's), nodes 0x2C (0x0C, name first) - each BSP in its node's
  space, as this engine's collision nodes keep theirs;
- the scenario: device groups 0xA0 (0x28: name, initial value 0x20, flags
  0x24), each machine and control placement's power group 0x34, position
  group 0x36, device flags 0x38 and its own flags 0x3C.
*/

#ifndef __H2_OBJANIM_H
#define __H2_OBJANIM_H

#include "cseries.h"
#include "math/real_math.h"

struct h2_cache;

/* ---------- constants */

enum
{
	/* (part of the level cache's builder version: h2_level.c) */
	/* (7: a machine's apply collision damage scale, in its first unused word) */
	/* (8: a campaign level's scenery of nodes keeps them, for its cutscenes) */
	/* (9: a machine moved by named tracks alone - the Scarab - keeps its
	collision node by node) */
	H2_OBJANIM_BUILDER_VERSION = 9,

	/* (RASTERIZER_MAXIMUM_NODES_PER_MODEL) */
	H2_OBJANIM_MAXIMUM_NODES = 44,
	/* (a graph's node flags, 32 bits) */
	H2_OBJANIM_MAXIMUM_ANIMATED_NODES = 32,
	H2_OBJANIM_NAME_SIZE = 32,
	H2_OBJANIM_ACTION_TEXT_SIZE = 64,

	/* a device's sounds, Halo 2's order (open, close, opened, closed,
	depowered, repowered) then a control's (on, off, deny) */
	H2_OBJANIM_SOUND_OPEN = 0,
	H2_OBJANIM_SOUND_CLOSE,
	H2_OBJANIM_SOUND_OPENED,
	H2_OBJANIM_SOUND_CLOSED,
	H2_OBJANIM_SOUND_DEPOWERED,
	H2_OBJANIM_SOUND_REPOWERED,
	H2_OBJANIM_SOUND_ON,
	H2_OBJANIM_SOUND_OFF,
	H2_OBJANIM_SOUND_DENY,
	NUMBER_OF_H2_OBJANIM_SOUNDS,

	/* (runtime) what a machine or control does, the values of the device
	sounds' h2_sound.h H2_SOUND_MACHINE_* (open, opened, pressed, collide),
	then closing ones of its own */
	H2_OBJANIM_EVENT_OPEN = 1,
	H2_OBJANIM_EVENT_OPENED,
	H2_OBJANIM_EVENT_PRESSED,
	H2_OBJANIM_EVENT_COLLIDE,
	H2_OBJANIM_EVENT_CLOSE,
	H2_OBJANIM_EVENT_CLOSED,

	/* this engine's definitions' sizes (Xbox) */
	H2_OBJANIM_CE_MACHINE_SIZE = 0x324,
	H2_OBJANIM_CE_CONTROL_SIZE = 0x31C,
	H2_OBJANIM_CE_MODEL_NODE_SIZE = 0x9C,
	H2_OBJANIM_CE_PLACEMENT_SIZE = 0x40,
	H2_OBJANIM_CE_DEVICE_GROUP_SIZE = 0x34,
};

/* ---------- structures */

/* one of the objects made */
struct h2_objanim;

/* what h2_level.c lends while it writes: its tag data (offsets into it,
linked at link), a block at a field (its elements' offset), a data field,
a reference (group and tag id; 0xFFFFFFFF none), Halo 2's collision BSP
at h2 (its 8 blocks) as this engine's at field. effect_for_sound (may be
NULL) is the sounds' hook: this engine's effect tag id that plays a Halo 2
sound tag (the device sounds' agent), 0xFFFFFFFF none */
struct h2_objanim_host
{
	void *context;
	unsigned long (*alloc)(void *context, unsigned long size);
	unsigned char *(*at)(void *context, unsigned long offset);
	unsigned long (*block)(void *context, unsigned long field, long count, unsigned long element_size);
	void (*data)(void *context, unsigned long field, void const *bytes, unsigned long size);
	void (*reference)(void *context, unsigned long field, unsigned long group, unsigned long id);
	boolean (*failed)(void *context);
	boolean (*collision_convert)(void *context, unsigned long field, unsigned long h2, short material);
	unsigned long (*effect_for_sound)(void *context, long h2_sound_tag);
	unsigned long link;
};

/* a machine's or control's placement's device fields (Halo 2's, the
scenario's group indices kept: this engine's device groups are made in
Halo 2's order) */
struct h2_objanim_placement
{
	boolean device;
	short power_group;
	short position_group;
	unsigned long device_flags;
	unsigned long flags;
};

/* what was made, for the log and the tests */
struct h2_objanim_report
{
	short node_count;
	short frame_count;          /* this engine's (a looping one's last, its first again, left out) */
	short h2_frame_count;
	short codec;
	short rotated, translated, scaled;
	short collision_nodes;      /* nodes with collision */
	short collision_dropped;    /* Halo 2 BSPs left out (a node's second) */
	long animation_bytes;       /* this engine's frames and defaults */
	float position_speed;       /* position units a second */
	boolean animated, loops, control;
	short machine_type;
};

/* ---------- prototypes/H2_OBJANIM.C */

/* a Halo 2 machine's or control's definition (its tag) made: NULL (and
why) if it is neither or cannot be (its model's nodes, its graph's
position animation, its collision model, its device fields) */
extern boolean h2_objanim_scenery_nodes;

struct h2_objanim *h2_objanim_make(
	struct h2_cache *cache,
	long object_tag,
	char *why,
	long why_size);

void h2_objanim_free(
	struct h2_objanim *anim);

/* the permutation by name each region of the next objects made takes (a
placement's model variant: count 0, each region's first) */
void h2_objanim_variant(
	short count,
	char const (*regions)[32],
	char const (*permutations)[32]);

/* whether a Halo 2 crate (bloc) has parts that swing (its physics model's
hinges or ragdoll constraints): h2_objanim_make makes it a machine of its
nodes, not animated, its collision node by node (the physics props turn
its parts in the picture, ragdolls_props.c) */
boolean h2_objanim_swings(
	struct h2_cache *cache,
	long object_tag);

/* this engine's object type of it (7 machine, 8 control), its group
('mach', 'ctrl') */
short h2_objanim_object_type(
	struct h2_objanim const *anim);
unsigned long h2_objanim_group(
	struct h2_objanim const *anim);

/* its model's nodes (1 or more: a model of one node keeps h2_level.c's own
"frame" node) */
short h2_objanim_node_count(
	struct h2_objanim const *anim);

/* whether it has an animation graph (a device position animation) and a
collision model to be made (not for a machine whose graph cannot be read:
it would never move out of the way) */
boolean h2_objanim_has_graph(
	struct h2_objanim const *anim);
boolean h2_objanim_has_collision(
	struct h2_objanim const *anim);

/* its Halo 2 sound tags (H2_OBJANIM_SOUND_*: NONE none), for the sounds'
agent */
long h2_objanim_sound(
	struct h2_objanim const *anim,
	short which);

void h2_objanim_report(
	struct h2_objanim const *anim,
	struct h2_objanim_report *report);

/* (writing) its model's nodes, a model tag's root at model_root (0xB8:
the nodes block) */
void h2_objanim_write_model_nodes(
	struct h2_objanim const *anim,
	struct h2_objanim_host const *host,
	unsigned long model_root);

/* (writing) its animation graph (antr): the root's offset, 0 if none */
unsigned long h2_objanim_write_graph(
	struct h2_objanim const *anim,
	struct h2_objanim_host const *host);

/* (writing) its collision model (coll): the root's offset, 0 if none */
unsigned long h2_objanim_write_collision(
	struct h2_objanim const *anim,
	struct h2_objanim_host const *host);

/* (writing) the device's, machine's or control's fields of its object tag
at root (H2_OBJANIM_CE_MACHINE_SIZE or _CONTROL_SIZE bytes, its object
part written by h2_level.c) */
void h2_objanim_write_device(
	struct h2_objanim const *anim,
	struct h2_objanim_host const *host,
	unsigned long root);

/* a machine's or control's placement's device fields (its scenario
placement at address: the same offsets for both) */
void h2_objanim_placement_read(
	struct h2_cache *cache,
	unsigned long address,
	struct h2_objanim_placement *placement);

/* this engine's placement's device fields (a machine's or control's
placement at o: power and position group, flags) */
void h2_objanim_placement_write(
	struct h2_objanim_placement const *placement,
	boolean control,
	unsigned char *o);

/* the scenario's device groups written (this engine's scenario's device
groups field at field): how many */
long h2_objanim_write_device_groups(
	struct h2_cache *cache,
	struct h2_objanim_host const *host,
	unsigned long field);

/* ---------- (runtime) the devices' sounds

The device code (devices.c, device_controls.c) tells each machine's
start and stop and each control's press; a device made of a Halo 2 one
(its definition named h2\objects\NAME, NAME Halo 2's tag's) is passed on
to h2_objanim_sound_hook as NAME, the event and where it is. The hook is
the device sounds' (h2engine/sound: h2_sound_machine_event, the same
signature), NULL until they are merged in. */
extern boolean (*h2_objanim_sound_hook)(char const *machine, int event, real_point3d const *position);

void h2_objanim_device_event(
	long device_index,
	short event);

/* a control's prompt text (Halo 2's action string's English: this
engine's "Press [action] to ..." custom device message's part) */
boolean h2_objanim_has_action_text(
	struct h2_objanim const *anim);

/* (writing) the controls' prompt texts as a unicode string list (ustr),
in their order (those with none left out): the root's offset, 0 if not -
the scenario's custom object names; a control's placement names its
string by index + 1 */
unsigned long h2_objanim_write_action_texts(
	struct h2_objanim const *const *anims,
	long count,
	struct h2_objanim_host const *host);

#endif
