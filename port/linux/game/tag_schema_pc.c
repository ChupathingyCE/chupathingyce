/*
TAG_SCHEMA_PC.C

Halo PC's tag layout (Custom Edition's maps, cache version 609, and
HaloMD's, Halo PC retail's): what the validator (tag_validate.c) checks a
Halo PC map's tags by as they load (cache_files.c), before the port's
conversions (port/linux/game/ce_*.c) read them.

Halo PC's tags are laid out as the Xbox's in every group the Xbox's maps
have, but for a model's parts (a gbxmodel's, with its local nodes) and
where its loader keeps what the Xbox's tools put in a map otherwise (a
structure bsp's vertices uncompressed, a bitmap's pixels and a sound's
samples in the resource maps): so its layout is its own groups first, then
the Xbox's (tag_schema_groups.c), and flags for the rest
(tag_schema_layout_flags). Compared field by field with Invader's Halo PC
tag definitions (src/tag/hek/definition, by SnowyMouse and Aerocatia,
GPL-3), which every walked block of the Xbox's schemas matches in offset
and size.

OpenSauce's groups and extensions are not Halo PC's: they are a layout of
their own, for later (its own file, its groups before these).
*/

#ifdef HALO_CUSTOM_EDITION

/* ---------- headers */

#include "cseries.h"
#include "tag_schema.h"

/* ---------- constants */

enum
{
	/* (cache_files.c's CE_TAG_CACHE_SIZE) */
	PC_TAG_CACHE_SIZE = 0x01700000,

	/* Invader's tag_collection: 200 tags */
	MAXIMUM_TAG_COLLECTION_TAGS = 200,
	/* input_device_defaults: its device kinds, and its data's most bytes
	(MCC's validator's, mcc_tag_validate.c) */
	NUMBER_OF_INPUT_DEVICE_KINDS = 3,
	MAXIMUM_INPUT_DEVICE_IDENTIFIER_SIZE = 16,
	MAXIMUM_INPUT_DEVICE_PROFILE_SIZE = 41984,
};

/* ---------- structures */

/* a tag collection (tagc): tags of any group, which the game never reads */
struct tag_collection_entry
{
	struct tag_reference reference;
};

struct tag_collection
{
	struct tag_block tags;
};

/* input device defaults (devc), which the game never reads */
struct input_device_defaults
{
	short device_kind;
	word flags;
	struct tag_data identifier;
	struct tag_data profile;
};

/* network game preferences (ngpr), which the game never reads */
struct preferences_network_game
{
	char name[32];
	real primary_color[3];
	real secondary_color[3];
	struct tag_reference pattern;
	short pattern_bitmap_index;
	word pad;
	struct tag_reference decal;
	short decal_bitmap_index;
	word pad2;
	byte unused[800];
};

typedef char verify_tag_collection_entry_size[sizeof(struct tag_collection_entry) == 0x10 ? 1 : -1];
typedef char verify_tag_collection_size[sizeof(struct tag_collection) == 0xc ? 1 : -1];
typedef char verify_input_device_defaults_size[sizeof(struct input_device_defaults) == 0x2c ? 1 : -1];
typedef char verify_preferences_network_game_size[sizeof(struct preferences_network_game) == 0x380 ? 1 : -1];

/* ---------- definitions */

static struct tag_schema_field const tag_collection_entry_fields[] =
{
	TAG_SCHEMA_REFERENCE(struct tag_collection_entry, reference, NULL),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const tag_collection_entry_schema =
	TAG_SCHEMA_DEFINITION(tag_collection_entry, struct tag_collection_entry, tag_collection_entry_fields);

static struct tag_schema_field const tag_collection_fields[] =
{
	TAG_SCHEMA_TOOL_BLOCK(struct tag_collection, tags, tag_collection_entry_schema, MAXIMUM_TAG_COLLECTION_TAGS),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const tag_collection_schema =
	TAG_SCHEMA_DEFINITION(tag_collection, struct tag_collection, tag_collection_fields);

static struct tag_schema_field const input_device_defaults_fields[] =
{
	TAG_SCHEMA_ENUM(struct input_device_defaults, device_kind, NUMBER_OF_INPUT_DEVICE_KINDS, 0),
	TAG_SCHEMA_DATA(struct input_device_defaults, identifier, MAXIMUM_INPUT_DEVICE_IDENTIFIER_SIZE),
	TAG_SCHEMA_DATA(struct input_device_defaults, profile, MAXIMUM_INPUT_DEVICE_PROFILE_SIZE),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const input_device_defaults_schema =
	TAG_SCHEMA_DEFINITION(input_device_defaults, struct input_device_defaults, input_device_defaults_fields);

static struct tag_schema_field const preferences_network_game_fields[] =
{
	TAG_SCHEMA_STRING(struct preferences_network_game, name),
	TAG_SCHEMA_REFERENCE(struct preferences_network_game, pattern, TAG_SCHEMA_GROUPS('bitm')),
	TAG_SCHEMA_REFERENCE(struct preferences_network_game, decal, TAG_SCHEMA_GROUPS('bitm')),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const preferences_network_game_schema =
	TAG_SCHEMA_DEFINITION(preferences_network_game, struct preferences_network_game,
		preferences_network_game_fields);

/* ---------- globals */

/* (first in its layout: a group here is Halo PC's, whatever the Xbox's of
the same group tag) */
struct tag_schema_group const tag_schema_pc_groups[] =
{
	{ 'mod2', { NONE, NONE }, &tag_schema_gbxmodel_schema },
	{ 'tagc', { NONE, NONE }, &tag_collection_schema },
	{ 'devc', { NONE, NONE }, &input_device_defaults_schema },
	{ 'ngpr', { NONE, NONE }, &preferences_network_game_schema },
	{ 0 }
};

static struct tag_schema_group const *const tag_schema_pc_group_lists[] =
{
	tag_schema_pc_groups,
	tag_schema_object_groups,
	tag_schema_model_groups,
	tag_schema_collision_groups,
	tag_schema_render_groups,
	tag_schema_effect_groups,
	tag_schema_scenario_groups,
	NULL
};

/* (a gbxmodel is where a model is asked for: the port makes it one as the
map loads, ce_models.c) */
static unsigned long const tag_schema_pc_group_aliases[][2] =
{
	{ 'mode', 'mod2' },
	{ 0, 0 }
};

/* (both Custom Edition's and HaloMD's: retail's tools left some blocks
overlapping others, and Custom Edition's maps are played as HaloMD plays
them, so an overlapping block is walked as its own where the walk writes
nothing into it, and made none otherwise) */
struct tag_schema_layout const tag_schema_pc_layout =
{
	"Halo PC",
	tag_schema_pc_group_lists,
	tag_schema_pc_group_aliases,
	PC_TAG_CACHE_SIZE,
	FLAG(_tag_schema_layout_pc_header_bit) |
		FLAG(_tag_schema_layout_tool_maxima_kept_bit) |
		FLAG(_tag_schema_layout_file_data_elsewhere_bit) |
		FLAG(_tag_schema_layout_uncompressed_bsp_vertices_bit) |
		FLAG(_tag_schema_layout_overlaps_walked_bit) |
		FLAG(_tag_schema_layout_pc_shader_types_bit)
};

#endif
