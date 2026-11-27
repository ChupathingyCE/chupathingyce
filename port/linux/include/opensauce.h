/*
OPENSAUCE.H

OpenSauce's maps (Custom Edition caches built with OpenSauce's tools, by
Kornner Studios), in a module of their own (port/linux/game/opensauce_map.c):
the OpenSauce header a map carries, whether the map being loaded has one,
its tags' layout (port/linux/game/tag_schema_opensauce.c) and what of its
project_yellow and project_yellow_globals tags the game honors. Nothing here
is used for a map without an OpenSauce header.
*/

#ifndef OPENSAUCE_H
#define OPENSAUCE_H

#ifndef HALO_CUSTOM_EDITION
/* (no OpenSauce maps without Custom Edition's: as it is) */
#define opensauce_map_drawn_camouflage(amount) (amount)
#define opensauce_map_sound_gain(music) ((void)(music), 1.0f)
#endif

/* ---------- constants */

enum
{
	/* where it is in a cache's header, and its signature there */
	OPENSAUCE_HEADER_OFFSET = 0x70,
	OPENSAUCE_HEADER_SIGNATURE = 'yelo',

	/* its flags (cache_files_structures_yelo.hpp) */
	_opensauce_header_uses_memory_upgrades_bit = 0,
	_opensauce_header_uses_mod_data_files_bit,
	_opensauce_header_is_protected_bit,
	_opensauce_header_uses_game_state_upgrades_bit,
	_opensauce_header_has_compression_params_bit,
	NUMBER_OF_OPENSAUCE_HEADER_FLAGS,

	/* OpenSauce's memory upgrades (blam_memory_upgrades.hpp): 1.5 times Custom
	Edition's tag cache, tag count and scripts' syntax nodes */
	OPENSAUCE_TAG_CACHE_SIZE = 0x02280000,
	OPENSAUCE_MAXIMUM_TAG_COUNT = 7680,
	OPENSAUCE_MAXIMUM_HS_SYNTAX_NODES = 28501,
	OPENSAUCE_HS_SYNTAX_DATA_SIZE = 0x38 + OPENSAUCE_MAXIMUM_HS_SYNTAX_NODES * 0x14,

	/* its grenade types (game_globals_definitions.hpp's upgrade): Halo's
	two and two more */
	OPENSAUCE_NUMBER_OF_GRENADE_TYPES = 4,
};

/* ---------- structures */

/* the header, as read (s_cache_header_yelo: the fields the game looks at) */
struct opensauce_header
{
	short version;
	unsigned short flags;
	unsigned char project_yellow_version;
	unsigned char project_yellow_globals_version;
	float memory_upgrade_amount;
	char mod_name[32];
};

/* ---------- prototypes/OPENSAUCE_MAP.C */

/* whether a cache's header (its 0x800 bytes) has OpenSauce's, read into
header if so */
boolean opensauce_header_read(
	void const *cache_header,
	struct opensauce_header *header);

/* a flag's name, "uses_memory_upgrades" and so on */
char const *opensauce_header_flag_name(
	short bit);

/* a Custom Edition map's header, as it starts to load (cache_files.c): from
then until it is unloaded, the map is OpenSauce's if it has the header */
void opensauce_map_opened(
	void const *cache_header,
	char const *map_name);

/* whether the map loaded (or loading) has an OpenSauce header */
boolean opensauce_map_active(
	void);

/* the loaded map's header, NULL if it has none */
struct opensauce_header const *opensauce_map_header(
	void);

/* its tags checked against OpenSauce's layout (cache_files.c): its
project_yellow and project_yellow_globals read and what the game honors of
them put in place, the rest logged once */
void opensauce_map_tags_loaded(
	void *tag_instances,
	long tag_count,
	unsigned long scenario_tag_index);

/* gravity set to the game's own times scale, as OpenSauce's
physics_set_gravity does: put back when the map unloads */
void opensauce_map_set_gravity_scale(
	float scale);

/* a unit's active camouflage as it is drawn (and fades its lights): an
OpenSauce map's scripts set it past 1 (SPV3's camo_power 2, to hold it on
longer as it fades), which Halo PC's engine draws as fully on; drawn so here,
the unit keeping its value. Any other map's as it is */
#ifdef HALO_CUSTOM_EDITION
float opensauce_map_drawn_camouflage(
	float amount);
#endif

/* the ui_widget_definition a map's scripts open by name
(display_scripted_ui_widget): from its project_yellow's scripted widgets, or
its project_yellow_globals', as OpenSauce finds it; NONE if neither has one
of that name with a definition, or the map isn't OpenSauce's */
long opensauce_map_scripted_widget(
	char const *name);

/* whether the loaded map is OpenSauce's and its globals list more than
Halo's two grenade types (OpenSauce allows four): their counts are kept by
the extra grenade types' layer (mcc_grenades.c) */
#ifdef HALO_CUSTOM_EDITION
boolean opensauce_map_four_grenades(
	void);
#endif

#ifdef HALO_CUSTOM_EDITION
/* Halo PC's sound_set_music_gain and sound_set_effects_gain, which an
OpenSauce map's scripts call: the music's or the other sounds' gain, 0 to 1,
for this map (sound_manager.c takes it with the player's own volume). 1 on
any other map, and again at each map */
void opensauce_map_set_sound_gain(
	boolean music,
	float gain);
float opensauce_map_sound_gain(
	boolean music);
#endif

/* the map unloaded: whatever it changed put back */
void opensauce_map_unloaded(
	void);

#endif
