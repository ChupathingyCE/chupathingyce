/*
H2_AI.H

port: Halo 2's AI on this engine's (a Halo 2 campaign level, h2_level.c).

Halo 2 keeps its AI as squads (each a character, a weapon, its starting
locations, an initial order), squad groups (a tree the scripts name), zones
(firing positions in areas), orders (areas to fight from, endings that move
a squad on to its next order when triggers' conditions hold) and character
tags (a unit, perception, weapons, firing patterns, grenades, vitality...,
each block inherited from the parent character when the character has none).

This engine's AI is Halo's: encounters of squads, firing positions in
lettered groups, actor variants of actors. h2_ai.c makes the one from the
other while a level is built (the builder half) and plays Halo 2's script
functions on it while the level runs (the runtime half):

- each Halo 2 squad is an encounter of one squad (encounter k is squad k:
  an ai index of Halo 2's naming a squad is this engine's naming that
  encounter), each squad group an encounter of no squads after them (its
  name, for the console);
- each character an actor (actr), each character and weapon a squad or its
  starting locations use an actor variant (actv) - its unit the character's
  biped (h2_biped.c's campaign bipeds), its weapon the level's weapon for
  Halo 2's (h2_level.c's stand-ins);
- a squad's firing positions are those of the areas of the orders it may be
  given (its initial order, its groups', the orders their endings lead to
  and the orders the scripts give it), a letter an order; the order's index
  is kept in the firing position (its unused word);
- the runtime reads the squads, groups, orders and triggers again from the
  player's map when the level loads (as h2_rules.c does) and runs the
  orders' endings, the AI script functions (ai_place, ai_living_count, ...)
  and command scripts (cs_run_command_script and the cs_ functions).

Upstream behaviour: halo2-upstream-work's squads.cpp / unknown_272b70.cpp
(the ai index: its type in the top two bits - squad, squad group, actor,
starting location), the functions' semantics from their documentation
(hs_doc.txt, H2EK, private) and field names from the H2EK's tag exports
(private). No Havok code, no Bungie data kept here.
*/

#ifndef __H2_AI_H
#define __H2_AI_H

#include "cseries.h"

struct h2_cache;
struct h2_biped_host;

/* ---------- the builder */

struct h2_ai;

/* the level's AI gathered (NULL: none - a multiplayer level, no squads, or
HALO_H2_NO_AI) */
struct h2_ai *h2_ai_gather(
	struct h2_cache *cache);

void h2_ai_free(
	struct h2_ai *ai);

/* its tags (actors and actor variants, after the level's) and each one's
group and name */
long h2_ai_tag_count(
	struct h2_ai const *ai);
boolean h2_ai_tag_describe(
	struct h2_ai const *ai,
	long index,
	unsigned long *group,
	char *name,
	long name_size);

/* how the builder finds the units and weapons the AI uses: the tag index of
the biped made for a Halo 2 biped of h2_biped.c's (h2_biped.h's enum; NONE:
not made), and of the level's weapon for a Halo 2 weapon tag (NONE: none) */
struct h2_ai_lookup
{
	void *context;
	long (*biped_tag)(void *context, short biped);
	long (*weapon_tag)(void *context, long h2_weapon_tag);
	/* a tag's id as the table holds it (the globals' pack's are its own) */
	unsigned long (*tag_id)(void *context, long index);
};

/* its tags written (the first of them first_tag in the table) and the
scenario's AI blocks (actor palette, encounters) */
boolean h2_ai_write(
	struct h2_ai *ai,
	struct h2_biped_host const *host,
	struct h2_ai_lookup const *lookup,
	long first_tag,
	unsigned long scenario_root);

/* the biped of h2_biped.h's made for a Halo 2 biped tag's name (one of
the campaign's characters' units), NONE if none */
short h2_ai_biped_for_unit_name(
	char const *name);

/* ---------- the runtime */

/* (h2_scripts.c) a function of Halo 2's this file plays: its evaluate,
return type and parameters (as h2_scripts.c's own natives) */
struct h2_scripts_native;
struct h2_scripts_native const *h2_ai_natives(
	long *count);

/* (h2_scripts.c) the value of one of Halo 2's engine globals the AI keeps
as a constant (ai_combat_status_*): FALSE if it is not one */
boolean h2_ai_external_global_value(
	char const *name,
	long *value);

/* the level loaded (map_name: the cache file's name): its squads, groups,
orders and triggers read from the player's map; NONE of it on a level that
is not a Halo 2 campaign level */
void h2_ai_map_loaded(
	char const *map_name);

/* a tick of the game (the orders' endings, the command scripts) */
short h2_ai_vehicle_load_cargo(
	long vehicle_index,
	char const *seat_name,
	long object_list);
short h2_ai_vehicle_unload_cargo(
	long vehicle_index,
	char const *seat_name);
void h2_ai_game_state_reverted(
	void);
void h2_ai_update(
	void);

/* (objects.c) an object made, its regions' permutations chosen: a
campaign character's model variant (its placement's variant number, the
variant's index + 1; else one of its defaults) */
void h2_ai_object_variant(
	long object_index,
	short variant_number);

/* (damage.c) whether the scripts keep an object from dying (ai_cannot_die,
object_cannot_die): hurt to nothing, not killed */
boolean h2_ai_object_cannot_die(
	long object_index);

/* (source/ai) a vocalization a Halo 2 behaviour makes that this engine's dialogue has no type for ("cower", the
retreat's cower each 2-3 s) */
void h2_ai_behaviour_voice(
	long unit_index,
	char const *vocalization);

/* (h2_dialogue.c) whether the runtime has a level; an actor that may speak
its combat lines (a command script's only if cs_enable_dialogue says so),
by its actor or its unit */
boolean h2_ai_running(
	void);
boolean h2_ai_actor_may_speak(
	long actor_index);
boolean h2_ai_actor_may_speak_unit(
	long unit_index);
/* (h2_dialogue.c) a campaign character's unit: its Halo 2 biped's tag path
and the model variant it shows ("" none) */
boolean h2_ai_object_character(
	long object_index,
	char *biped_path,
	long path_size,
	char *variant,
	long variant_size);
/* (h2_dialogue.c) whether an actor's squad follows the player (its order),
and whether it is its squad's leader (the leader type among them - an Elite
with Grunts -, else its first living actor) */
boolean h2_ai_actor_follows_player(
	long actor_index);
boolean h2_ai_actor_is_leader(
	long actor_index);
/* (h2_dialogue.c) the combat status an actor's order forces on it (0 none,
1 asleep, 2 idle, 3 alert, 4 combat) */
short h2_ai_actor_forced_combat_status(
	long actor_index);
void h2_ai_dispose_from_old_map(
	void);

/* (the dialogue) a Halo 2 vocalization's gesture played by its speaker: the dialogue globals' vocalization
"animation impulse" (1 shakefist, 2 cheer, 5 taunt, 7 point, 8 hold, 9 wave, 10 advance, 11 fallback); TRUE: it plays */
boolean h2_ai_vocalization_animation(
	long unit_index,
	short animation_impulse);

#endif
