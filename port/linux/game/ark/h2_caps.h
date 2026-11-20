/*
H2_CAPS.H

The limits the Halo 2 / Halo 3 converters (h2_*.c) apply to what they
make, by platform tier (owner, Oct 23 2026: "lift any caps on PC, then
we'll optimize for the Vita").

- the Vita tier (the Vita, the Linux builds that stand for it - gxm-null,
  the Vita-on-the-net and dedicated server builds - and Android, whose
  phones have the Vita's kind of memory): until Oct 24 2026 the values the
  converters had before this file (256 animations a graph, the Xbox's:
  the player graphs then had no dual wielding, sword or boarding, and
  Halo 2's overlays left the vehicle seats with no class at all); now
  lifted as the PC's but for an animation's variants (3: Halo 2's three
  melee strikes) and the animations a graph (1024), its level marked as
  the PC tier's is so its tags are checked with the tools' maximums;
- the PC tier (the desktop builds: Linux, Windows): the converters' own
  caps lifted to what the engine itself takes (a short animation index, a
  Custom Edition graph's most).

What a cap changes is only what the converter writes into this machine's
own level file (t:\h2\<map>.map, made from the player's MCC maps): never
the game state's layout, a saved game's, the netcode's or the map-share
protocol's, nor the multiplayer cache slot (a converted multiplayer level
still has to fit it: 64 MB on the Vita tier, 128 MB on the PC tier's
desktop builds - halo_port_capacity.h). The tier is recorded in the level's header, so a level made by the
other tier is made again (h2_level_cache_path).

A PC-tier level differs from a Vita-tier one (more animations in the
player graphs): two machines of different tiers must not play one Halo 2
level together. They cannot today (a Halo 2 level is not joinable over
the network yet); when it is, the join's map check has to compare the tier
(h2_caps_level_word, in the level's header at 0x71C).

HALO_H2_CAPS=vita on a PC build gives the Vita tier (to check the Vita's
output on a desktop); HALO_H2_CAPS=pc on a Vita-tier build does nothing.
*/

#ifndef __H2_CAPS_H
#define __H2_CAPS_H

/* ---------- constants */

/* (compile time) whether this build may use the PC tier */
#if defined(HALO_VITA) || defined(HALO_GXM_NULL) || defined(HALO_NET_AS_VITA) || defined(HALO_DEDICATED_SERVER) || \
	defined(__ANDROID__)
#define H2_CAPS_PC_BUILD 0
#else
#define H2_CAPS_PC_BUILD 1
/* whether a map file's header (its 0x800 bytes) is a Halo 2 level this
machine made at the PC tier: its tags are checked with the engine's own
block maximums (the Custom Edition tools', as a Custom Edition map's are)
rather than the Xbox's (cache_files.c, tag_validate_allow_tool_maximums):
the Xbox's 256 animations a graph cut the PC tier's graphs */
int h2_caps_level_lifted(void const *header);

#endif

enum
{
	_h2_caps_tier_vita = 0,
	_h2_caps_tier_pc = 1,
};

/* the most of each cap this build's tiers use (array sizes); each tier's
value is the function below. A Vita-tier build's are the Vita's own, so
its converters' structures (and so its heap, whose addresses a level file
shows in its tag references' unused name fields) are as before. */
#if H2_CAPS_PC_BUILD
enum
{
	/* animations a converted animation graph (h2_animation.c): the engine
	indexes them with a short, a Custom Edition graph holds up to SHORT_MAX
	(tag_schema_models.c); Vita 256 (the Xbox graph's maximum) */
	H2_CAPS_MAXIMUM_GRAPH_ANIMATIONS = 2048,
	/* an animation's variants made (Halo 2's next-variant siblings, this
	engine's permutations); Vita 2 */
	H2_CAPS_MAXIMUM_ANIMATION_VARIANTS = 6,
	/* unit seats a graph (Halo 2's modes and the stock vehicles' labels for
	them); Vita 32 */
	H2_CAPS_MAXIMUM_GRAPH_SEATS = 64,
	/* weapon classes a seat; Vita 12 */
	H2_CAPS_MAXIMUM_SEAT_CLASSES = 16,
	/* weapon types a class (Halo 2's, with the stock weapons' labels beside
	them); Vita 16 */
	H2_CAPS_MAXIMUM_CLASS_TYPES = 32,
};
#else
enum
{
	/* (Oct 24 2026: the Vita's graphs lifted past the Xbox's 256 - their
	dual wielding, sword, vehicle and boarding seats and Halo 2's overlays
	need ~420 with three variants - h2_caps.c) */
	H2_CAPS_MAXIMUM_GRAPH_ANIMATIONS = 1024,
	H2_CAPS_MAXIMUM_ANIMATION_VARIANTS = 3,
	H2_CAPS_MAXIMUM_GRAPH_SEATS = 64,
	H2_CAPS_MAXIMUM_SEAT_CLASSES = 16,
	H2_CAPS_MAXIMUM_CLASS_TYPES = 32,
};
/* whether a map file's header (its 0x800 bytes) is a Halo 2 level this
machine made at the PC tier: its tags are checked with the engine's own
block maximums (the Custom Edition tools', as a Custom Edition map's are)
rather than the Xbox's (cache_files.c, tag_validate_allow_tool_maximums):
the Xbox's 256 animations a graph cut the PC tier's graphs */
int h2_caps_level_lifted(void const *header);

#endif

/* ---------- prototypes/H2_CAPS.C */

/* this machine's tier (_h2_caps_tier_*) */
int h2_caps_tier(void);

/* each cap at this machine's tier */
long h2_caps_graph_animations(void);
long h2_caps_animation_variants(void);
long h2_caps_graph_seats(void);
long h2_caps_seat_classes(void);
long h2_caps_class_types(void);

/* (not a cap) whether the vehicle seats' fixes are made (h2_animation.c):
a seat's hovering and flying animations its idle (a Ghost's rider had
none: the bind pose, standing on it). (The acceleration overlays are
Halo 2's three frames again: units.c plays the middle one at rest) */
int h2_caps_seat_fixes(void);

/* (not a cap) whether Halo 2's boarding is made: the graphs' boarding modes
(warthog_b_d...: their enter, board and the seats' ejection animations) and
the vehicles' boarding seats at Halo 2's boarding markers (h2_vehicles.c,
units.c's unit_seat_boarding_target), the Warthog's passenger seen in third
person as Halo 2 has it */
int h2_caps_boarding(void);

/* what the level's header records of the tier (a level made by the other
tier is made again): 0 for the Vita tier, which every level made before
this file also has there */
unsigned long h2_caps_level_word(void);

/* whether a map file's header (its 0x800 bytes) is a Halo 2 level this
machine made at the PC tier: its tags are checked with the engine's own
block maximums (the Custom Edition tools', as a Custom Edition map's are)
rather than the Xbox's (cache_files.c, tag_validate_allow_tool_maximums):
the Xbox's 256 animations a graph cut the PC tier's graphs */
int h2_caps_level_lifted(void const *header);

#endif
