/*
H3_ANIMATION.H

Halo 3's animation graphs (model_animation_graph, 'jmad') made this engine's
for the player's biped, read from the player's own MCC Halo 3 maps
(h3_animation.c). Halo 3's graphs are Halo 2's design - nodes, blend
screens, animations, modes of weapon classes of weapon types with actions and
overlays, deaths and damage, variants - laid out with MCC's 12-byte blocks at
other offsets, their animations' data in resource pages (this map's or
shared.map's) instead of the tag; the codec blocks are Halo 2's exactly. So
a Halo 3 graph is read through h2_animation.h's source (h2_graph_make_from)
and everything after the read is h2_animation.c's: the tables, the baking of
overlays into base animations, the variants, this engine's compression and
the tag written into the level.

Layouts: read against the owner's zanzibar.map (a local audit: the table and what verified
each offset); the field names are
Assembly's Halo3MCC plugins' (a reference, no code).

Switch: HALO_H3_ANIMATION=1 (off by default; PC for now: the maps are read
whole into memory, the Vita's streaming is a later milestone) makes the
Spartan biped's graph of a Halo 2 or Halo 3 level from Halo 3's
masterchief graph; HALO_H3_ANIMATION_MAP=<name> names the Halo 3 map read
(else the first of H3_SPARTAN_MAPS found).
*/

#ifndef __H3_ANIMATION_H
#define __H3_ANIMATION_H

#include "cseries.h"
#include "h2_animation.h"

struct h3_cache;

/* a Halo 3 graph opened as an h2_animation.h source (h3_weapons.c: the
first-person graphs): its animations' data gathered from the pages in one
pass, its blocks read through the source's calls with the Halo 3 layout
(source->layout) */
struct h3_animation_source;

/* ---------- prototypes/H3_ANIMATION.C */

/* the jmad tag at jmad_tag of an open cache (the caller's: it stays open)
as a graph source, source filled in; NULL and why if it cannot be read */
struct h3_animation_source *h3_animation_source_open(
	struct h3_cache *cache,
	long jmad_tag,
	struct h2_graph_source *source,
	char *why,
	long why_size);

void h3_animation_source_close(
	struct h3_animation_source *opened);

/* whether a biped of a level (h2_biped.c's names: "spartan"...) plays a Halo
3 graph on this machine */
boolean h3_animation_wanted(
	char const *biped_name);

/* the biped's graph made from Halo 3's (h2_graph_make's arguments: the
model's nodes, their names, defaults in this engine's convention and
parents): NULL and why if it cannot be */
struct h2_graph *h3_animation_graph_make(
	char const *biped_name,
	char const (*node_names)[32],
	struct h2_animation_pose const *node_defaults,
	short const *node_parents,
	short node_count,
	char *why,
	long why_size);

/* the same from an open Halo 3 map where it holds the graph (a Halo 3
level's own, h3_level.c: borrowed, not closed), else as above */
struct h2_graph *h3_animation_graph_make_from(
	struct h3_cache *cache,
	char const *biped_name,
	char const (*node_names)[32],
	struct h2_animation_pose const *node_defaults,
	short const *node_parents,
	short node_count,
	char *why,
	long why_size);

#endif
