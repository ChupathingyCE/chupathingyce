/*
OPENSAUCE_BSP_SETS.H

OpenSauce's structure BSP lightmap sets and sky sets
(port/linux/game/opensauce_bsp_sets.c): a map with an OpenSauce header may
list, for some of its BSPs, other lightmaps and other skies its scripts
switch to by name (structure_bsp_set_lightmap_set, structure_bsp_set_sky_set:
dawn, a darkened room, a sunset). Nothing here does anything on any other
map, and the calls from the game's own code are empty where there are no
OpenSauce maps.
*/

#ifndef OPENSAUCE_BSP_SETS_H
#define OPENSAUCE_BSP_SETS_H

#if defined(HALO_CUSTOM_EDITION) && defined(HALO_MAP_LIST) && !defined(HALO_XBOX_CONSOLE)

#define OPENSAUCE_BSP_SETS

/* the map's tags checked (opensauce_map.c): its scenario's BSP modifiers read */
void opensauce_bsp_sets_tags_loaded(
	void *tag_instances,
	long tag_count,
	unsigned long scenario_tag_index);

/* the map unloaded: no sets, none chosen */
void opensauce_bsp_sets_map_unloaded(
	void);

/* a BSP loaded and current (scenario.c): the sets its scripts chose put in
place for it */
void opensauce_bsp_sets_structure_bsp_loaded(
	short structure_bsp_index);

/* a game state loaded (a checkpoint's revert, a saved game): the current
BSP's sets put back as they were saved */
void opensauce_bsp_sets_game_state_loaded(
	void);

/* the sky a scenario sky index draws (scenario.c): the current BSP's sky
set's, where it has one, else definition_index */
long opensauce_bsp_sets_sky(
	short sky_index,
	long definition_index);

/* the scripts' (opensauce_scripts.c): a BSP's set chosen by name; FALSE if
the BSP has no set of that name */
boolean opensauce_bsp_sets_set_lightmap_set(
	short structure_bsp_index,
	char const *name);
boolean opensauce_bsp_sets_set_sky_set(
	short structure_bsp_index,
	char const *name);

#else

#define opensauce_bsp_sets_structure_bsp_loaded(structure_bsp_index) ((void)(structure_bsp_index))
static __inline void opensauce_bsp_sets_game_state_loaded(void) { }
#define opensauce_bsp_sets_sky(sky_index, definition_index) ((void)(sky_index), (definition_index))

#endif

#endif
