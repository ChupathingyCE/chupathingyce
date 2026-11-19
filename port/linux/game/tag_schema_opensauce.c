/*
TAG_SCHEMA_OPENSAUCE.C

OpenSauce's tag layout: what the validator (tag_validate.c) checks the tags
of a map with an OpenSauce header by (port/linux/game/opensauce_map.c), in
place of Halo PC's (tag_schema_pc.c). It is Halo PC's, with OpenSauce's own
groups first and its memory upgrades: a tag cache 1.5 times Custom
Edition's, and the scripts' syntax for 28501 nodes rather than 19001.

The groups are OpenSauce's (Kornner Studios, GPL-3.0-or-later): their
layouts, sizes, block maxima and enums are from its tag definitions
(the XML files in OpenSauce's shared/Halo1/CheApe folder, and the
structures in shared/Include/YeloLib/Halo1, which assert their sizes), read as facts. Each
structure here is the game's own, with its size checked against
OpenSauce's. As in every schema, only the fields that hold a pointer, a
count, an index, an enum, a tag or a runtime value are listed.

What the game reads of them: project_yellow and project_yellow_globals
(opensauce_map.c). The actor variant transforms and the post-processing
groups are checked as their tags load and not used yet.
*/

#ifdef HALO_CUSTOM_EDITION

/* ---------- headers */

#include "cseries.h"
#include "tag_schema.h"
#include "cache/predicted_resources.h"
#include "opensauce.h"

#include <string.h>

/* ---------- constants */

enum
{
	/* project_yellow_shared.xml and project_yellow.xml */
	MAXIMUM_OPENSAUCE_SCRIPTED_WIDGETS = 128,
	MAXIMUM_OPENSAUCE_SCRIPT_FUNCTIONS = 464,
	MAXIMUM_OPENSAUCE_SCRIPT_GLOBALS = 880,
	MAXIMUM_OPENSAUCE_SCRIPT_PARAMETERS = 32,
	NUMBER_OF_OPENSAUCE_BUILD_STAGES = 6,
	/* project_yellow_globals.xml (its own block is unbounded: 32 is
	Custom Edition's most players) */
	MAXIMUM_OPENSAUCE_PLAYER_UNITS = 32,

	/* actor_variant_transform_*.xml, unit_transform_definition.hpp */
	MAXIMUM_TRANSFORM_COLLECTION_ENTRIES = 32,
	MAXIMUM_TRANSFORM_COLLECTION_TRANSFORMS = 32,
	MAXIMUM_TRANSFORM_IN_TARGETS = 16,
	MAXIMUM_TRANSFORM_OUT_INSTIGATORS = 16,
	MAXIMUM_TRANSFORM_OUT_ATTACHMENTS = 16,
	MAXIMUM_TRANSFORM_KEYFRAME_ACTIONS = 9,
	NUMBER_OF_TRANSFORM_KEYFRAMES = 3,
	NUMBER_OF_TRANSFORM_RIDER_HANDLINGS = 3,
	NUMBER_OF_TRANSFORM_EFFECT_TARGETS = 2,
	NUMBER_OF_TRANSFORM_TEAM_HANDLINGS = 3,
	NUMBER_OF_TRANSFORM_STATE_HANDLINGS = 3,
	NUMBER_OF_TRANSFORM_SQUAD_HANDLINGS = 3,
	NUMBER_OF_TRANSFORM_VITALITY_HANDLINGS = 4,
	NUMBER_OF_TRANSFORM_DAMAGE_TYPES = 3,
	/* (game_allegiance_team_enum, actor_default_state_enum) */
	NUMBER_OF_TRANSFORM_TEAMS = 10,
	NUMBER_OF_TRANSFORM_DEFAULT_STATES = 12,

	/* shader_postprocess.xml, effect_postprocess*.xml */
	MAXIMUM_POSTPROCESS_SHADER_BINARY_SIZE = 0x8000,
	MAXIMUM_POSTPROCESS_TECHNIQUES = 3,
	MAXIMUM_POSTPROCESS_PASSES = 32,
	MAXIMUM_POSTPROCESS_PREDICTED_RESOURCES = 1024,
	MAXIMUM_POSTPROCESS_PARAMETERS = 16384,
	MAXIMUM_POSTPROCESS_VALUES = 16,
	MAXIMUM_POSTPROCESS_EFFECT_SHADERS = 12,
	MAXIMUM_POSTPROCESS_EFFECT_PARAMETERS = 32,
	MAXIMUM_POSTPROCESS_COLLECTION_SHADERS = 128,
	MAXIMUM_POSTPROCESS_COLLECTION_EFFECTS = 32,
	MAXIMUM_POSTPROCESS_COLLECTION_INSTANCES = 32,
	MAXIMUM_POSTPROCESS_SCRIPT_VARIABLES = 32,
	MAXIMUM_POSTPROCESS_ACTIVATION_CONTROLS = 8,
	NUMBER_OF_POSTPROCESS_RENDER_STAGES = 6,
	NUMBER_OF_POSTPROCESS_ACTIVATION_STATES = 4,
	NUMBER_OF_POSTPROCESS_ACTIVATION_OPERATIONS = 2,
	MAXIMUM_POSTPROCESS_BLOOM_GLOBALS = 1,

	/* (cache_files.c's CE_TAG_CACHE_SIZE, as OpenSauce's memory upgrades
	make it) */
	OPENSAUCE_SCHEMA_TAG_CACHE_SIZE = OPENSAUCE_TAG_CACHE_SIZE,
};

/* ---------- structures: project_yellow (yelo) and its globals (gelo) */

struct opensauce_build_info
{
	word pad;
	short build_stage;
	unsigned long revision;
	unsigned long timestamp[2];
	byte uuid[16];
	byte pad2[16];
};

/* a widget scripts open by name (display_scripted_ui_widget) */
struct opensauce_scripted_widget
{
	char name[32];
	struct tag_reference definition;
	unsigned long pad;
	byte pad2[24];
};

struct opensauce_script_parameter
{
	short type;
};

/* a script function or global OpenSauce adds, by name and its number */
struct opensauce_script_function
{
	char name[64];
	short index;
	short return_type;
	struct tag_block parameters;
};

struct opensauce_script_global
{
	char name[64];
	short index;
	short type;
};

struct opensauce_scripting
{
	struct tag_block new_functions;
	struct tag_block new_globals;
};

struct opensauce_project_yellow
{
	short version;
	word flags;
	struct tag_reference yelo_globals;
	struct tag_reference game_globals;
	struct tag_reference explicit_references;
	struct tag_block build_info;
	byte pad[40];
	struct tag_block scripted_widgets;
	byte pad2[16];
	real gravity_scale;
	real player_speed_scale;
	byte pad3[20];
	unsigned long networking_flags;
	byte pad4[20];
	unsigned long gameplay_flags;
	byte pad5[20];
	struct tag_block user_scripting;
	byte pad6[92];
};

struct opensauce_player_unit
{
	char name[32];
	struct tag_reference definition;
	byte pad[32];
};

struct opensauce_project_yellow_globals
{
	short version;
	word flags;
	unsigned long base_address;
	char mod_name[32];
	struct tag_reference explicit_references;
	byte pad[32];
	byte preprocess[12];
	byte pad2[52];
	struct tag_block scripted_widgets;
	struct tag_block player_units;
	byte pad3[20];
	struct tag_block yelo_scripting;
	byte pad4[80];
};

/* ---------- structures: actor variant transforms (avtc, avti, avto) */

struct opensauce_transform_keyframe_action
{
	short keyframe;
	short rider_handling;
	short target;
	word pad;
	struct tag_reference damage_effect;
	struct tag_reference effect;
	char effect_marker[32];
};

struct opensauce_transform_in_target
{
	char target_name[32];
	word flags;
	word pad;
	real selection_chance[4];
	struct tag_reference actor_variant;
	short encounter_squad_handling;
	word pad2;
	short team_handling;
	short team_override;
	short initial_state_handling;
	short initial_state_override;
	short return_state_handling;
	short return_state_override;
	word pad3[2];
	char transform_in_animation[32];
	struct tag_block keyframe_actions;
	unsigned long pad4;
	short vitality_inheritance;
	short vitality_override;
	real shield_override;
	real health_override;
	byte pad5[24];
};

struct opensauce_transform_in
{
	struct tag_block targets;
	byte pad[24];
};

struct opensauce_transform_out_instigator
{
	struct tag_reference unit;
	short damage_type;
	word pad;
	byte pad2[12];
};

struct opensauce_transform_out_attachment
{
	struct tag_reference object_type;
	char object_marker[32];
	char destination_marker[32];
	short destination_marker_count;
	word pad;
	short team_handling;
	short team_override;
	real attachment_scale[2];
	byte pad2[24];
};

struct opensauce_transform_out
{
	word flags;
	word pad;
	word criteria_flags;
	word pad2;
	word actor_action;
	word actor_state;
	real shield_range[2];
	real health_range[2];
	byte pad3[24];
	struct tag_block instigators;
	char transform_out_animation[32];
	struct tag_block keyframe_actions;
	word attachment_flags;
	word pad4;
	struct tag_block attachments;
	byte pad5[24];
};

struct opensauce_transform_collection_transform
{
	word flags;
	word pad;
	char transform_name[32];
	real selection_chance[4];
	struct tag_reference transform_out;
	/* (runtime: OpenSauce's pointer to the tag's definition) */
	unsigned long transform_out_definition;
	struct tag_reference transform_in;
	unsigned long transform_in_definition;
	byte pad2[24];
};

struct opensauce_transform_collection_entry
{
	struct tag_reference actor_variant;
	struct tag_block transforms;
	byte pad[24];
};

struct opensauce_transform_collection
{
	struct tag_block actor_variant_transforms;
	byte pad[24];
};

/* ---------- structures: post-processing (shpg, efpg, efpc, sppg) */

struct opensauce_postprocess_pass
{
	char name[32];
	word flags;
	short render_chain;
	byte pad[12];
};

struct opensauce_postprocess_technique
{
	char name[32];
	word shader_model;
	word pad;
	byte pad2[16];
	struct tag_block passes;
};

/* a value's fields (s_shader_postprocess_value_base) */
#define OPENSAUCE_POSTPROCESS_VALUE_FIELDS \
	char value_name[32]; \
	short value_type; \
	short value_count; \
	/* (runtime: the effect's handle of it) */ \
	unsigned long value_handle; \
	byte value[32]; \
	short runtime_value; \
	word runtime_flags; \
	short animation_function; \
	byte animation_flags; \
	byte pad_value; \
	real animation_duration; \
	real animation_rate;

struct opensauce_postprocess_value
{
	OPENSAUCE_POSTPROCESS_VALUE_FIELDS
};

struct opensauce_postprocess_bitmap
{
	OPENSAUCE_POSTPROCESS_VALUE_FIELDS
	byte pad[12];
	struct tag_reference bitmap;
};

struct opensauce_postprocess_parameter
{
	OPENSAUCE_POSTPROCESS_VALUE_FIELDS
	struct tag_reference bitmap;
	/* (runtime: the bitmap's data, or an external texture) */
	unsigned long runtime[2];
	word flags;
	word pad;
};

struct opensauce_shader_postprocess_generic
{
	word pad;
	word flags;
	byte pad2[20];
	struct tag_data shader_code_binary;
	/* (runtime: the effect's handles of its standard variables) */
	unsigned long handles[11];
	byte pad3[20];
	struct tag_block techniques;
	struct tag_block predicted_resources;
	/* (runtime: the effect, its handle and its technique) */
	word runtime_flags;
	word pad4;
	unsigned long runtime_effect;
	unsigned long runtime_handle;
	unsigned long runtime_technique;
	byte pad5[16];
	/* (shader_postprocess_generic's own) */
	word pad6[2];
	struct tag_reference base_shader;
	struct tag_block parameters;
	struct tag_block bitmaps;
	struct tag_block bools;
	struct tag_block integers;
	struct tag_block floats;
	struct tag_block float2s;
	struct tag_block float3s;
	struct tag_block float4s;
	struct tag_block colors;
	byte pad7[36];
};

struct opensauce_postprocess_shader_reference
{
	struct tag_reference shader;
};

struct opensauce_postprocess_shader_index
{
	long shader;
};

struct opensauce_postprocess_exposed_parameter
{
	char exposed_name[32];
	char parameter_name[32];
	long shader_index;
	byte runtime[12];
};

struct opensauce_effect_postprocess_generic
{
	/* (effect_postprocess's) */
	byte pad[12];
	short tessellation[2];
	real x_bounds[2];
	real y_bounds[2];
	byte pad2[12];
	word runtime_flags;
	word pad3;
	byte pad4[12];
	/* (effect_postprocess_generic's own) */
	struct tag_block shaders;
	struct tag_block shader_indices;
	struct tag_block exposed_parameters;
};

struct opensauce_postprocess_script_variable
{
	char script_variable_name[32];
	char exposed_parameter_name[32];
	byte runtime[16];
};

struct opensauce_postprocess_collection_effect
{
	struct tag_reference effect;
	char name[32];
	struct tag_block script_variables;
	byte pad[12];
};

struct opensauce_postprocess_activation_control
{
	short state;
	word flags;
};

struct opensauce_postprocess_effect_instance
{
	char name[32];
	short effect_index;
	short render_stage;
	short tessellation[2];
	real x_bounds[2];
	real y_bounds[2];
	byte pad[12];
	short activation_operation;
	byte pad2[6];
	struct tag_block activation_controls;
	byte pad3[12];
	byte pad4[16];
};

struct opensauce_effect_postprocess_collection
{
	byte pad[12];
	struct tag_block shaders;
	struct tag_block effects;
	struct tag_block effect_instances;
};

struct opensauce_postprocess_bloom
{
	word flags;
	word pad;
	real size;
	real exposure;
	real mix_amount;
	real minimum_color[3];
	real maximum_color[3];
};

struct opensauce_shader_postprocess_globals
{
	word pad[2];
	struct tag_block bloom_globals;
	byte pad2[228];
};

/* (OpenSauce's sizes: its structures' asserts and its tag definitions) */
typedef char verify_opensauce_build_info_size[sizeof(struct opensauce_build_info) == 0x30 ? 1 : -1];
typedef char verify_opensauce_scripted_widget_size[sizeof(struct opensauce_scripted_widget) == 0x4C ? 1 : -1];
typedef char verify_opensauce_script_function_size[sizeof(struct opensauce_script_function) == 0x50 ? 1 : -1];
typedef char verify_opensauce_script_global_size[sizeof(struct opensauce_script_global) == 0x44 ? 1 : -1];
typedef char verify_opensauce_scripting_size[sizeof(struct opensauce_scripting) == 0x18 ? 1 : -1];
typedef char verify_opensauce_project_yellow_size[sizeof(struct opensauce_project_yellow) == 0x138 ? 1 : -1];
typedef char verify_opensauce_player_unit_size[sizeof(struct opensauce_player_unit) == 0x50 ? 1 : -1];
typedef char verify_opensauce_project_yellow_globals_size[
	sizeof(struct opensauce_project_yellow_globals) == 0x120 ? 1 : -1];
typedef char verify_opensauce_keyframe_action_size[sizeof(struct opensauce_transform_keyframe_action) == 0x48 ? 1 : -1];
typedef char verify_opensauce_transform_in_target_size[sizeof(struct opensauce_transform_in_target) == 0xAC ? 1 : -1];
typedef char verify_opensauce_transform_in_size[sizeof(struct opensauce_transform_in) == 0x24 ? 1 : -1];
typedef char verify_opensauce_instigator_size[sizeof(struct opensauce_transform_out_instigator) == 0x20 ? 1 : -1];
typedef char verify_opensauce_attachment_size[sizeof(struct opensauce_transform_out_attachment) == 0x78 ? 1 : -1];
typedef char verify_opensauce_transform_out_size[sizeof(struct opensauce_transform_out) == 0x94 ? 1 : -1];
typedef char verify_opensauce_collection_transform_size[
	sizeof(struct opensauce_transform_collection_transform) == 0x74 ? 1 : -1];
typedef char verify_opensauce_collection_entry_size[sizeof(struct opensauce_transform_collection_entry) == 0x34 ? 1 : -1];
typedef char verify_opensauce_collection_size[sizeof(struct opensauce_transform_collection) == 0x24 ? 1 : -1];
typedef char verify_opensauce_pass_size[sizeof(struct opensauce_postprocess_pass) == 0x30 ? 1 : -1];
typedef char verify_opensauce_technique_size[sizeof(struct opensauce_postprocess_technique) == 0x40 ? 1 : -1];
typedef char verify_opensauce_value_size[sizeof(struct opensauce_postprocess_value) == 0x58 ? 1 : -1];
typedef char verify_opensauce_bitmap_size[sizeof(struct opensauce_postprocess_bitmap) == 0x74 ? 1 : -1];
typedef char verify_opensauce_parameter_size[sizeof(struct opensauce_postprocess_parameter) == 0x74 ? 1 : -1];
typedef char verify_opensauce_shpg_size[sizeof(struct opensauce_shader_postprocess_generic) == 0x148 ? 1 : -1];
typedef char verify_opensauce_exposed_size[sizeof(struct opensauce_postprocess_exposed_parameter) == 0x50 ? 1 : -1];
typedef char verify_opensauce_efpg_size[sizeof(struct opensauce_effect_postprocess_generic) == 0x60 ? 1 : -1];
typedef char verify_opensauce_script_variable_size[sizeof(struct opensauce_postprocess_script_variable) == 0x50 ? 1 : -1];
typedef char verify_opensauce_collection_effect_size[
	sizeof(struct opensauce_postprocess_collection_effect) == 0x48 ? 1 : -1];
typedef char verify_opensauce_effect_instance_size[sizeof(struct opensauce_postprocess_effect_instance) == 0x74 ? 1 : -1];
typedef char verify_opensauce_efpc_size[sizeof(struct opensauce_effect_postprocess_collection) == 0x30 ? 1 : -1];
typedef char verify_opensauce_bloom_size[sizeof(struct opensauce_postprocess_bloom) == 0x28 ? 1 : -1];
typedef char verify_opensauce_sppg_size[sizeof(struct opensauce_shader_postprocess_globals) == 0xF4 ? 1 : -1];

/* ---------- definitions: project_yellow and its globals */

static struct tag_schema_field const opensauce_build_info_fields[] =
{
	TAG_SCHEMA_ENUM(struct opensauce_build_info, build_stage, NUMBER_OF_OPENSAUCE_BUILD_STAGES, 0),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_build_info_schema =
	TAG_SCHEMA_DEFINITION(opensauce_build_info, struct opensauce_build_info, opensauce_build_info_fields);

static struct tag_schema_field const opensauce_scripted_widget_fields[] =
{
	TAG_SCHEMA_STRING(struct opensauce_scripted_widget, name),
	TAG_SCHEMA_REFERENCE(struct opensauce_scripted_widget, definition, TAG_SCHEMA_GROUPS('DeLa')),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_scripted_widget_schema =
	TAG_SCHEMA_DEFINITION(opensauce_scripted_widget, struct opensauce_scripted_widget,
		opensauce_scripted_widget_fields);

static struct tag_schema_field const opensauce_script_parameter_fields[] =
{
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_script_parameter_schema =
	TAG_SCHEMA_DEFINITION(opensauce_script_parameter, struct opensauce_script_parameter,
		opensauce_script_parameter_fields);

static struct tag_schema_field const opensauce_script_function_fields[] =
{
	TAG_SCHEMA_STRING(struct opensauce_script_function, name),
	TAG_SCHEMA_BLOCK(struct opensauce_script_function, parameters, opensauce_script_parameter_schema,
		MAXIMUM_OPENSAUCE_SCRIPT_PARAMETERS),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_script_function_schema =
	TAG_SCHEMA_DEFINITION(opensauce_script_function, struct opensauce_script_function,
		opensauce_script_function_fields);

static struct tag_schema_field const opensauce_script_global_fields[] =
{
	TAG_SCHEMA_STRING(struct opensauce_script_global, name),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_script_global_schema =
	TAG_SCHEMA_DEFINITION(opensauce_script_global, struct opensauce_script_global, opensauce_script_global_fields);

static struct tag_schema_field const opensauce_scripting_fields[] =
{
	TAG_SCHEMA_BLOCK(struct opensauce_scripting, new_functions, opensauce_script_function_schema,
		MAXIMUM_OPENSAUCE_SCRIPT_FUNCTIONS),
	TAG_SCHEMA_BLOCK(struct opensauce_scripting, new_globals, opensauce_script_global_schema,
		MAXIMUM_OPENSAUCE_SCRIPT_GLOBALS),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_scripting_schema =
	TAG_SCHEMA_DEFINITION(opensauce_scripting, struct opensauce_scripting, opensauce_scripting_fields);

static struct tag_schema_field const opensauce_project_yellow_fields[] =
{
	TAG_SCHEMA_REFERENCE(struct opensauce_project_yellow, yelo_globals, TAG_SCHEMA_GROUPS('gelo')),
	TAG_SCHEMA_REFERENCE(struct opensauce_project_yellow, game_globals, TAG_SCHEMA_GROUPS('matg')),
	TAG_SCHEMA_REFERENCE(struct opensauce_project_yellow, explicit_references, TAG_SCHEMA_GROUPS('tagc')),
	TAG_SCHEMA_BLOCK(struct opensauce_project_yellow, build_info, opensauce_build_info_schema, 1),
	TAG_SCHEMA_BLOCK(struct opensauce_project_yellow, scripted_widgets, opensauce_scripted_widget_schema,
		MAXIMUM_OPENSAUCE_SCRIPTED_WIDGETS),
	TAG_SCHEMA_BLOCK(struct opensauce_project_yellow, user_scripting, opensauce_scripting_schema, 1),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_project_yellow_schema =
	TAG_SCHEMA_DEFINITION(opensauce_project_yellow, struct opensauce_project_yellow,
		opensauce_project_yellow_fields);

static struct tag_schema_field const opensauce_player_unit_fields[] =
{
	TAG_SCHEMA_STRING(struct opensauce_player_unit, name),
	TAG_SCHEMA_REFERENCE(struct opensauce_player_unit, definition, TAG_SCHEMA_GROUPS('unit')),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_player_unit_schema =
	TAG_SCHEMA_DEFINITION(opensauce_player_unit, struct opensauce_player_unit, opensauce_player_unit_fields);

static struct tag_schema_field const opensauce_project_yellow_globals_fields[] =
{
	TAG_SCHEMA_STRING(struct opensauce_project_yellow_globals, mod_name),
	TAG_SCHEMA_REFERENCE(struct opensauce_project_yellow_globals, explicit_references, TAG_SCHEMA_GROUPS('tagc')),
	TAG_SCHEMA_BLOCK(struct opensauce_project_yellow_globals, scripted_widgets, opensauce_scripted_widget_schema,
		MAXIMUM_OPENSAUCE_SCRIPTED_WIDGETS),
	TAG_SCHEMA_BLOCK(struct opensauce_project_yellow_globals, player_units, opensauce_player_unit_schema,
		MAXIMUM_OPENSAUCE_PLAYER_UNITS),
	TAG_SCHEMA_BLOCK(struct opensauce_project_yellow_globals, yelo_scripting, opensauce_scripting_schema, 1),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_project_yellow_globals_schema =
	TAG_SCHEMA_DEFINITION(opensauce_project_yellow_globals, struct opensauce_project_yellow_globals,
		opensauce_project_yellow_globals_fields);

/* ---------- definitions: actor variant transforms */

static struct tag_schema_field const opensauce_keyframe_action_fields[] =
{
	TAG_SCHEMA_ENUM(struct opensauce_transform_keyframe_action, keyframe, NUMBER_OF_TRANSFORM_KEYFRAMES, 0),
	TAG_SCHEMA_ENUM(struct opensauce_transform_keyframe_action, rider_handling, NUMBER_OF_TRANSFORM_RIDER_HANDLINGS, 0),
	TAG_SCHEMA_ENUM(struct opensauce_transform_keyframe_action, target, NUMBER_OF_TRANSFORM_EFFECT_TARGETS, 0),
	TAG_SCHEMA_REFERENCE(struct opensauce_transform_keyframe_action, damage_effect, TAG_SCHEMA_GROUPS('jpt!')),
	TAG_SCHEMA_REFERENCE(struct opensauce_transform_keyframe_action, effect, TAG_SCHEMA_GROUPS('effe')),
	TAG_SCHEMA_STRING(struct opensauce_transform_keyframe_action, effect_marker),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_keyframe_action_schema =
	TAG_SCHEMA_DEFINITION(opensauce_transform_keyframe_action, struct opensauce_transform_keyframe_action,
		opensauce_keyframe_action_fields);

static struct tag_schema_field const opensauce_transform_in_target_fields[] =
{
	TAG_SCHEMA_STRING(struct opensauce_transform_in_target, target_name),
	TAG_SCHEMA_REFERENCE(struct opensauce_transform_in_target, actor_variant, TAG_SCHEMA_GROUPS('actv')),
	TAG_SCHEMA_ENUM(struct opensauce_transform_in_target, encounter_squad_handling,
		NUMBER_OF_TRANSFORM_SQUAD_HANDLINGS, 0),
	TAG_SCHEMA_ENUM(struct opensauce_transform_in_target, team_handling, NUMBER_OF_TRANSFORM_TEAM_HANDLINGS, 0),
	TAG_SCHEMA_ENUM(struct opensauce_transform_in_target, team_override, NUMBER_OF_TRANSFORM_TEAMS, 0),
	TAG_SCHEMA_ENUM(struct opensauce_transform_in_target, initial_state_handling,
		NUMBER_OF_TRANSFORM_STATE_HANDLINGS, 0),
	TAG_SCHEMA_ENUM(struct opensauce_transform_in_target, initial_state_override,
		NUMBER_OF_TRANSFORM_DEFAULT_STATES, 0),
	TAG_SCHEMA_ENUM(struct opensauce_transform_in_target, return_state_handling,
		NUMBER_OF_TRANSFORM_STATE_HANDLINGS, 0),
	TAG_SCHEMA_ENUM(struct opensauce_transform_in_target, return_state_override,
		NUMBER_OF_TRANSFORM_DEFAULT_STATES, 0),
	TAG_SCHEMA_STRING(struct opensauce_transform_in_target, transform_in_animation),
	TAG_SCHEMA_BLOCK(struct opensauce_transform_in_target, keyframe_actions, opensauce_keyframe_action_schema,
		MAXIMUM_TRANSFORM_KEYFRAME_ACTIONS),
	TAG_SCHEMA_ENUM(struct opensauce_transform_in_target, vitality_inheritance,
		NUMBER_OF_TRANSFORM_VITALITY_HANDLINGS, 0),
	TAG_SCHEMA_ENUM(struct opensauce_transform_in_target, vitality_override,
		NUMBER_OF_TRANSFORM_VITALITY_HANDLINGS, 0),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_transform_in_target_schema =
	TAG_SCHEMA_DEFINITION(opensauce_transform_in_target, struct opensauce_transform_in_target,
		opensauce_transform_in_target_fields);

static struct tag_schema_field const opensauce_transform_in_fields[] =
{
	TAG_SCHEMA_BLOCK(struct opensauce_transform_in, targets, opensauce_transform_in_target_schema,
		MAXIMUM_TRANSFORM_IN_TARGETS),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_transform_in_schema =
	TAG_SCHEMA_DEFINITION(opensauce_transform_in, struct opensauce_transform_in, opensauce_transform_in_fields);

static struct tag_schema_field const opensauce_instigator_fields[] =
{
	TAG_SCHEMA_REFERENCE(struct opensauce_transform_out_instigator, unit, TAG_SCHEMA_GROUPS('unit')),
	TAG_SCHEMA_ENUM(struct opensauce_transform_out_instigator, damage_type, NUMBER_OF_TRANSFORM_DAMAGE_TYPES, 0),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_instigator_schema =
	TAG_SCHEMA_DEFINITION(opensauce_transform_out_instigator, struct opensauce_transform_out_instigator,
		opensauce_instigator_fields);

static struct tag_schema_field const opensauce_attachment_fields[] =
{
	TAG_SCHEMA_REFERENCE(struct opensauce_transform_out_attachment, object_type, TAG_SCHEMA_GROUPS('obje')),
	TAG_SCHEMA_STRING(struct opensauce_transform_out_attachment, object_marker),
	TAG_SCHEMA_STRING(struct opensauce_transform_out_attachment, destination_marker),
	TAG_SCHEMA_ENUM(struct opensauce_transform_out_attachment, team_handling, NUMBER_OF_TRANSFORM_TEAM_HANDLINGS, 0),
	TAG_SCHEMA_ENUM(struct opensauce_transform_out_attachment, team_override, NUMBER_OF_TRANSFORM_TEAMS, 0),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_attachment_schema =
	TAG_SCHEMA_DEFINITION(opensauce_transform_out_attachment, struct opensauce_transform_out_attachment,
		opensauce_attachment_fields);

static struct tag_schema_field const opensauce_transform_out_fields[] =
{
	TAG_SCHEMA_BLOCK(struct opensauce_transform_out, instigators, opensauce_instigator_schema,
		MAXIMUM_TRANSFORM_OUT_INSTIGATORS),
	TAG_SCHEMA_STRING(struct opensauce_transform_out, transform_out_animation),
	TAG_SCHEMA_BLOCK(struct opensauce_transform_out, keyframe_actions, opensauce_keyframe_action_schema,
		MAXIMUM_TRANSFORM_KEYFRAME_ACTIONS),
	TAG_SCHEMA_BLOCK(struct opensauce_transform_out, attachments, opensauce_attachment_schema,
		MAXIMUM_TRANSFORM_OUT_ATTACHMENTS),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_transform_out_schema =
	TAG_SCHEMA_DEFINITION(opensauce_transform_out, struct opensauce_transform_out, opensauce_transform_out_fields);

static struct tag_schema_field const opensauce_collection_transform_fields[] =
{
	TAG_SCHEMA_STRING(struct opensauce_transform_collection_transform, transform_name),
	TAG_SCHEMA_REFERENCE(struct opensauce_transform_collection_transform, transform_out, TAG_SCHEMA_GROUPS('avto')),
	TAG_SCHEMA_RESET(struct opensauce_transform_collection_transform, transform_out_definition, 0),
	TAG_SCHEMA_REFERENCE(struct opensauce_transform_collection_transform, transform_in, TAG_SCHEMA_GROUPS('avti')),
	TAG_SCHEMA_RESET(struct opensauce_transform_collection_transform, transform_in_definition, 0),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_collection_transform_schema =
	TAG_SCHEMA_DEFINITION(opensauce_transform_collection_transform, struct opensauce_transform_collection_transform,
		opensauce_collection_transform_fields);

static struct tag_schema_field const opensauce_collection_entry_fields[] =
{
	TAG_SCHEMA_REFERENCE(struct opensauce_transform_collection_entry, actor_variant, TAG_SCHEMA_GROUPS('actv')),
	TAG_SCHEMA_BLOCK(struct opensauce_transform_collection_entry, transforms, opensauce_collection_transform_schema,
		MAXIMUM_TRANSFORM_COLLECTION_TRANSFORMS),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_collection_entry_schema =
	TAG_SCHEMA_DEFINITION(opensauce_transform_collection_entry, struct opensauce_transform_collection_entry,
		opensauce_collection_entry_fields);

static struct tag_schema_field const opensauce_transform_collection_fields[] =
{
	TAG_SCHEMA_BLOCK(struct opensauce_transform_collection, actor_variant_transforms,
		opensauce_collection_entry_schema, MAXIMUM_TRANSFORM_COLLECTION_ENTRIES),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_transform_collection_schema =
	TAG_SCHEMA_DEFINITION(opensauce_transform_collection, struct opensauce_transform_collection,
		opensauce_transform_collection_fields);

/* ---------- definitions: post-processing */

static struct tag_schema_field const opensauce_pass_fields[] =
{
	TAG_SCHEMA_STRING(struct opensauce_postprocess_pass, name),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_pass_schema =
	TAG_SCHEMA_DEFINITION(opensauce_postprocess_pass, struct opensauce_postprocess_pass, opensauce_pass_fields);

static struct tag_schema_field const opensauce_technique_fields[] =
{
	TAG_SCHEMA_STRING(struct opensauce_postprocess_technique, name),
	TAG_SCHEMA_BLOCK(struct opensauce_postprocess_technique, passes, opensauce_pass_schema, MAXIMUM_POSTPROCESS_PASSES),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_technique_schema =
	TAG_SCHEMA_DEFINITION(opensauce_postprocess_technique, struct opensauce_postprocess_technique,
		opensauce_technique_fields);

static struct tag_schema_field const opensauce_predicted_resource_fields[] =
{
	TAG_SCHEMA_ENUM(struct predicted_resource, type, _predicted_resource_sound + 1, 0),
	TAG_SCHEMA_TAG_INDEX(struct predicted_resource, tag_index, TAG_SCHEMA_GROUPS('bitm', 'snd!')),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_predicted_resource_schema =
	TAG_SCHEMA_DEFINITION(predicted_resource, struct predicted_resource, opensauce_predicted_resource_fields);

static struct tag_schema_field const opensauce_value_fields[] =
{
	TAG_SCHEMA_STRING(struct opensauce_postprocess_value, value_name),
	TAG_SCHEMA_RESET(struct opensauce_postprocess_value, value_handle, 0),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_value_schema =
	TAG_SCHEMA_DEFINITION(opensauce_postprocess_value, struct opensauce_postprocess_value, opensauce_value_fields);

static struct tag_schema_field const opensauce_bitmap_fields[] =
{
	TAG_SCHEMA_STRING(struct opensauce_postprocess_bitmap, value_name),
	TAG_SCHEMA_RESET(struct opensauce_postprocess_bitmap, value_handle, 0),
	TAG_SCHEMA_REFERENCE(struct opensauce_postprocess_bitmap, bitmap, TAG_SCHEMA_GROUPS('bitm')),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_bitmap_schema =
	TAG_SCHEMA_DEFINITION(opensauce_postprocess_bitmap, struct opensauce_postprocess_bitmap, opensauce_bitmap_fields);

static struct tag_schema_field const opensauce_parameter_fields[] =
{
	TAG_SCHEMA_STRING(struct opensauce_postprocess_parameter, value_name),
	TAG_SCHEMA_RESET(struct opensauce_postprocess_parameter, value_handle, 0),
	TAG_SCHEMA_REFERENCE(struct opensauce_postprocess_parameter, bitmap, TAG_SCHEMA_GROUPS('bitm')),
	TAG_SCHEMA_ZERO(struct opensauce_postprocess_parameter, runtime),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_parameter_schema =
	TAG_SCHEMA_DEFINITION(opensauce_postprocess_parameter, struct opensauce_postprocess_parameter,
		opensauce_parameter_fields);

static struct tag_schema_field const opensauce_shader_postprocess_generic_fields[] =
{
	TAG_SCHEMA_DATA(struct opensauce_shader_postprocess_generic, shader_code_binary,
		MAXIMUM_POSTPROCESS_SHADER_BINARY_SIZE),
	TAG_SCHEMA_ZERO(struct opensauce_shader_postprocess_generic, handles),
	TAG_SCHEMA_BLOCK(struct opensauce_shader_postprocess_generic, techniques, opensauce_technique_schema,
		MAXIMUM_POSTPROCESS_TECHNIQUES),
	TAG_SCHEMA_BLOCK(struct opensauce_shader_postprocess_generic, predicted_resources,
		opensauce_predicted_resource_schema, MAXIMUM_POSTPROCESS_PREDICTED_RESOURCES),
	TAG_SCHEMA_RESET(struct opensauce_shader_postprocess_generic, runtime_flags, 0),
	TAG_SCHEMA_RESET(struct opensauce_shader_postprocess_generic, runtime_effect, 0),
	TAG_SCHEMA_RESET(struct opensauce_shader_postprocess_generic, runtime_handle, 0),
	TAG_SCHEMA_RESET(struct opensauce_shader_postprocess_generic, runtime_technique, 0),
	TAG_SCHEMA_REFERENCE(struct opensauce_shader_postprocess_generic, base_shader, TAG_SCHEMA_GROUPS('shpg')),
	TAG_SCHEMA_BLOCK(struct opensauce_shader_postprocess_generic, parameters, opensauce_parameter_schema,
		MAXIMUM_POSTPROCESS_PARAMETERS),
	TAG_SCHEMA_BLOCK(struct opensauce_shader_postprocess_generic, bitmaps, opensauce_bitmap_schema,
		MAXIMUM_POSTPROCESS_VALUES),
	TAG_SCHEMA_BLOCK(struct opensauce_shader_postprocess_generic, bools, opensauce_value_schema,
		MAXIMUM_POSTPROCESS_VALUES),
	TAG_SCHEMA_BLOCK(struct opensauce_shader_postprocess_generic, integers, opensauce_value_schema,
		MAXIMUM_POSTPROCESS_VALUES),
	TAG_SCHEMA_BLOCK(struct opensauce_shader_postprocess_generic, floats, opensauce_value_schema,
		MAXIMUM_POSTPROCESS_VALUES),
	TAG_SCHEMA_BLOCK(struct opensauce_shader_postprocess_generic, float2s, opensauce_value_schema,
		MAXIMUM_POSTPROCESS_VALUES),
	TAG_SCHEMA_BLOCK(struct opensauce_shader_postprocess_generic, float3s, opensauce_value_schema,
		MAXIMUM_POSTPROCESS_VALUES),
	TAG_SCHEMA_BLOCK(struct opensauce_shader_postprocess_generic, float4s, opensauce_value_schema,
		MAXIMUM_POSTPROCESS_VALUES),
	TAG_SCHEMA_BLOCK(struct opensauce_shader_postprocess_generic, colors, opensauce_value_schema,
		MAXIMUM_POSTPROCESS_VALUES),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_shader_postprocess_generic_schema =
	TAG_SCHEMA_DEFINITION(opensauce_shader_postprocess_generic, struct opensauce_shader_postprocess_generic,
		opensauce_shader_postprocess_generic_fields);

static struct tag_schema_field const opensauce_shader_reference_fields[] =
{
	TAG_SCHEMA_REFERENCE(struct opensauce_postprocess_shader_reference, shader, TAG_SCHEMA_GROUPS('shpg')),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_shader_reference_schema =
	TAG_SCHEMA_DEFINITION(opensauce_postprocess_shader_reference, struct opensauce_postprocess_shader_reference,
		opensauce_shader_reference_fields);

static struct tag_schema_field const opensauce_shader_index_fields[] =
{
	TAG_SCHEMA_BLOCK_INDEX(struct opensauce_postprocess_shader_index, shader, TAG_SCHEMA_ROOT,
		offsetof(struct opensauce_effect_postprocess_generic, shaders), FLAG(_tag_schema_none_bit)),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_shader_index_schema =
	TAG_SCHEMA_DEFINITION(opensauce_postprocess_shader_index, struct opensauce_postprocess_shader_index,
		opensauce_shader_index_fields);

static struct tag_schema_field const opensauce_exposed_parameter_fields[] =
{
	TAG_SCHEMA_STRING(struct opensauce_postprocess_exposed_parameter, exposed_name),
	TAG_SCHEMA_STRING(struct opensauce_postprocess_exposed_parameter, parameter_name),
	TAG_SCHEMA_BLOCK_INDEX(struct opensauce_postprocess_exposed_parameter, shader_index, TAG_SCHEMA_ROOT,
		offsetof(struct opensauce_effect_postprocess_generic, shader_indices), FLAG(_tag_schema_none_bit)),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_exposed_parameter_schema =
	TAG_SCHEMA_DEFINITION(opensauce_postprocess_exposed_parameter, struct opensauce_postprocess_exposed_parameter,
		opensauce_exposed_parameter_fields);

static struct tag_schema_field const opensauce_effect_postprocess_generic_fields[] =
{
	TAG_SCHEMA_RESET(struct opensauce_effect_postprocess_generic, runtime_flags, 0),
	TAG_SCHEMA_BLOCK(struct opensauce_effect_postprocess_generic, shaders, opensauce_shader_reference_schema,
		MAXIMUM_POSTPROCESS_EFFECT_SHADERS),
	TAG_SCHEMA_BLOCK(struct opensauce_effect_postprocess_generic, shader_indices, opensauce_shader_index_schema,
		MAXIMUM_POSTPROCESS_EFFECT_SHADERS),
	TAG_SCHEMA_BLOCK(struct opensauce_effect_postprocess_generic, exposed_parameters,
		opensauce_exposed_parameter_schema, MAXIMUM_POSTPROCESS_EFFECT_PARAMETERS),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_effect_postprocess_generic_schema =
	TAG_SCHEMA_DEFINITION(opensauce_effect_postprocess_generic, struct opensauce_effect_postprocess_generic,
		opensauce_effect_postprocess_generic_fields);

static struct tag_schema_field const opensauce_script_variable_fields[] =
{
	TAG_SCHEMA_STRING(struct opensauce_postprocess_script_variable, script_variable_name),
	TAG_SCHEMA_STRING(struct opensauce_postprocess_script_variable, exposed_parameter_name),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_script_variable_schema =
	TAG_SCHEMA_DEFINITION(opensauce_postprocess_script_variable, struct opensauce_postprocess_script_variable,
		opensauce_script_variable_fields);

static struct tag_schema_field const opensauce_collection_effect_fields[] =
{
	TAG_SCHEMA_REFERENCE(struct opensauce_postprocess_collection_effect, effect, TAG_SCHEMA_GROUPS('efpg')),
	TAG_SCHEMA_STRING(struct opensauce_postprocess_collection_effect, name),
	TAG_SCHEMA_BLOCK(struct opensauce_postprocess_collection_effect, script_variables,
		opensauce_script_variable_schema, MAXIMUM_POSTPROCESS_SCRIPT_VARIABLES),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_collection_effect_schema =
	TAG_SCHEMA_DEFINITION(opensauce_postprocess_collection_effect, struct opensauce_postprocess_collection_effect,
		opensauce_collection_effect_fields);

static struct tag_schema_field const opensauce_activation_control_fields[] =
{
	TAG_SCHEMA_ENUM(struct opensauce_postprocess_activation_control, state, NUMBER_OF_POSTPROCESS_ACTIVATION_STATES, 0),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_activation_control_schema =
	TAG_SCHEMA_DEFINITION(opensauce_postprocess_activation_control, struct opensauce_postprocess_activation_control,
		opensauce_activation_control_fields);

static struct tag_schema_field const opensauce_effect_instance_fields[] =
{
	TAG_SCHEMA_STRING(struct opensauce_postprocess_effect_instance, name),
	TAG_SCHEMA_BLOCK_INDEX(struct opensauce_postprocess_effect_instance, effect_index, TAG_SCHEMA_ROOT,
		offsetof(struct opensauce_effect_postprocess_collection, effects), FLAG(_tag_schema_none_bit)),
	TAG_SCHEMA_ENUM(struct opensauce_postprocess_effect_instance, render_stage, NUMBER_OF_POSTPROCESS_RENDER_STAGES, 0),
	TAG_SCHEMA_ENUM(struct opensauce_postprocess_effect_instance, activation_operation,
		NUMBER_OF_POSTPROCESS_ACTIVATION_OPERATIONS, 0),
	TAG_SCHEMA_BLOCK(struct opensauce_postprocess_effect_instance, activation_controls,
		opensauce_activation_control_schema, MAXIMUM_POSTPROCESS_ACTIVATION_CONTROLS),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_effect_instance_schema =
	TAG_SCHEMA_DEFINITION(opensauce_postprocess_effect_instance, struct opensauce_postprocess_effect_instance,
		opensauce_effect_instance_fields);

static struct tag_schema_field const opensauce_effect_postprocess_collection_fields[] =
{
	TAG_SCHEMA_BLOCK(struct opensauce_effect_postprocess_collection, shaders, opensauce_shader_reference_schema,
		MAXIMUM_POSTPROCESS_COLLECTION_SHADERS),
	TAG_SCHEMA_BLOCK(struct opensauce_effect_postprocess_collection, effects, opensauce_collection_effect_schema,
		MAXIMUM_POSTPROCESS_COLLECTION_EFFECTS),
	TAG_SCHEMA_BLOCK(struct opensauce_effect_postprocess_collection, effect_instances,
		opensauce_effect_instance_schema, MAXIMUM_POSTPROCESS_COLLECTION_INSTANCES),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_effect_postprocess_collection_schema =
	TAG_SCHEMA_DEFINITION(opensauce_effect_postprocess_collection, struct opensauce_effect_postprocess_collection,
		opensauce_effect_postprocess_collection_fields);

static struct tag_schema_field const opensauce_bloom_fields[] =
{
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_bloom_schema =
	TAG_SCHEMA_DEFINITION(opensauce_postprocess_bloom, struct opensauce_postprocess_bloom, opensauce_bloom_fields);

static struct tag_schema_field const opensauce_shader_postprocess_globals_fields[] =
{
	TAG_SCHEMA_BLOCK(struct opensauce_shader_postprocess_globals, bloom_globals, opensauce_bloom_schema,
		MAXIMUM_POSTPROCESS_BLOOM_GLOBALS),
	TAG_SCHEMA_END
};

static struct tag_schema_definition const opensauce_shader_postprocess_globals_schema =
	TAG_SCHEMA_DEFINITION(opensauce_shader_postprocess_globals, struct opensauce_shader_postprocess_globals,
		opensauce_shader_postprocess_globals_fields);

/* ---------- globals */

/* (first in its layout: OpenSauce's own groups; shpg and efpg are the only
kinds of shpp and efpp there are) */
static struct tag_schema_group const tag_schema_opensauce_groups[] =
{
	{ 'yelo', { NONE, NONE }, &opensauce_project_yellow_schema },
	{ 'gelo', { NONE, NONE }, &opensauce_project_yellow_globals_schema },
	{ 'avtc', { NONE, NONE }, &opensauce_transform_collection_schema },
	{ 'avti', { NONE, NONE }, &opensauce_transform_in_schema },
	{ 'avto', { NONE, NONE }, &opensauce_transform_out_schema },
	{ 'shpg', { 'shpp', NONE }, &opensauce_shader_postprocess_generic_schema },
	{ 'efpg', { 'efpp', NONE }, &opensauce_effect_postprocess_generic_schema },
	{ 'efpc', { NONE, NONE }, &opensauce_effect_postprocess_collection_schema },
	{ 'sppg', { NONE, NONE }, &opensauce_shader_postprocess_globals_schema },
	{ 0 }
};

static struct tag_schema_group const *const tag_schema_opensauce_group_lists[] =
{
	tag_schema_opensauce_groups,
	tag_schema_pc_groups,
	tag_schema_object_groups,
	tag_schema_model_groups,
	tag_schema_collision_groups,
	tag_schema_render_groups,
	tag_schema_effect_groups,
	tag_schema_scenario_groups,
	NULL
};

/* (Halo PC's: tag_schema_pc.c) */
static unsigned long const tag_schema_opensauce_group_aliases[][2] =
{
	{ 'mode', 'mod2' },
	{ 0, 0 }
};

/* the scenario's object placements (a block of each kind, by its field's and
its elements' names in tag_schema_scenario.c): the game places as many as a
block has (object_types.c's object_types_place_all, up to its short counter),
and nothing is sized by them; OpenSauce's tools made more than the Xbox's
(SPV3's a10 has 186 bipeds) */
static char const *const tag_schema_opensauce_placements[][2] =
{
	{ "scenery", "scenery" },
	{ "bipeds", "biped" },
	{ "equipment", "equipment" },
	{ "weapons", "weapon" },
	{ "machines", "machine" },
	{ "controls", "control" },
	{ "light_fixtures", "light_fixture" },
	{ "sound_scenery", "sound_scenery" },
};

/* OpenSauce's maps' maxima where they are not Halo PC's: the scenario's
scripts' syntax is for 28501 nodes (scenario_check_scripts checks it as such
in this layout), and its object placements are as many as there are; any
other field keeps the one the checks take (maximum) */
static long tag_schema_opensauce_field_maximum(
	struct tag_schema_field const *field,
	long maximum)
{
	if (!field->name)
		return maximum;
	if (field->type == _tag_schema_data && !strcmp(field->name, "hs_syntax_data"))
		return OPENSAUCE_HS_SYNTAX_DATA_SIZE;
	if (field->type == _tag_schema_block && field->definition)
	{
		struct tag_schema_definition const *element = field->definition;
		short index;

		for (index = 0; index < (short)NUMBEROF(tag_schema_opensauce_placements); index++)
		{
			if (!strcmp(field->name, tag_schema_opensauce_placements[index][0]) && element->name &&
				!strcmp(element->name, tag_schema_opensauce_placements[index][1]))
			{
				return SHORT_MAX;
			}
		}
	}
	return maximum;
}

struct tag_schema_layout const tag_schema_opensauce_layout =
{
	"OpenSauce",
	tag_schema_opensauce_group_lists,
	tag_schema_opensauce_group_aliases,
	OPENSAUCE_SCHEMA_TAG_CACHE_SIZE,
	FLAG(_tag_schema_layout_pc_header_bit) |
		FLAG(_tag_schema_layout_tool_maxima_kept_bit) |
		FLAG(_tag_schema_layout_file_data_elsewhere_bit) |
		FLAG(_tag_schema_layout_uncompressed_bsp_vertices_bit) |
		FLAG(_tag_schema_layout_overlaps_walked_bit) |
		FLAG(_tag_schema_layout_pc_shader_types_bit) |
		FLAG(_tag_schema_layout_opensauce_bit),
	tag_schema_opensauce_field_maximum
};

#endif
