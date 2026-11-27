/*
OPENSAUCE_TRANSFORMS.H

OpenSauce's actor variant transforms (port/linux/game/opensauce_transforms.c):
on a map with an OpenSauce header and an actor_variant_transform_collection
tag, an actor changes into another one mid-fight, as the map's tags say
(a Marine taken over by the Flood, an Elite drawing a sword when it is
hurt). Nothing here does anything on any other map, and the calls from the
game's own code are empty where there are no OpenSauce maps.

The host decides: a transform starts, and the new actor is made, only where
the game's actors think (a local game, or a network game's host); a client
plays what its host sends.
*/

#ifndef OPENSAUCE_TRANSFORMS_H
#define OPENSAUCE_TRANSFORMS_H

struct damage_data;

#if defined(HALO_CUSTOM_EDITION) && defined(HALO_MAP_LIST) && !defined(HALO_XBOX_CONSOLE)

#define OPENSAUCE_TRANSFORMS

/* the map's tags checked (opensauce_map.c): its transform collection read */
void opensauce_transforms_tags_loaded(
	void *tag_instances,
	long tag_count);

/* the map unloaded: no collection, no transform under way */
void opensauce_transforms_map_unloaded(
	void);

/* a game state loaded (a checkpoint's revert, a saved game): a transform
under way when it was saved is ended where it stands, its actor let go */
void opensauce_transforms_game_state_loaded(
	void);

/* each tick, before the objects update (game.c): the transforms under way
step on, and an actor whose state or action calls for one starts it */
void opensauce_transforms_update(
	void);

/* a unit was damaged (damage.c, after unit_damage_aftermath): an actor whose
vitality, or whose attacker, calls for a transform starts it */
void opensauce_transforms_unit_damaged(
	long unit_index,
	struct damage_data const *damage);

/* a unit's custom animation reached a key frame, or its end (units.c): a
transform's animation's key frame actions, and its stage's end */
void opensauce_transforms_unit_animation_event(
	long unit_index,
	boolean ended);

/* the scripts' (opensauce_scripts.c): ai_transform_actor and the rest, as
OpenSauce's. A name empty or NULL picks at random */
boolean opensauce_transforms_transform_actor(
	long unit_index,
	char const *transform_name,
	char const *target_name);
boolean opensauce_transforms_transform_actors(
	long object_list_index,
	long actor_variant_index,
	char const *transform_name,
	char const *target_name);
boolean opensauce_transforms_actor_is_transforming(
	long unit_index);

#else

static __inline void opensauce_transforms_game_state_loaded(void) { }
static __inline void opensauce_transforms_update(void) { }
static __inline void opensauce_transforms_unit_damaged(long unit_index, struct damage_data const *damage)
{
	(void)unit_index;
	(void)damage;
}
static __inline void opensauce_transforms_unit_animation_event(long unit_index, boolean ended)
{
	(void)unit_index;
	(void)ended;
}

#endif

#endif
