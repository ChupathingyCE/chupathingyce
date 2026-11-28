/*
OPENSAUCE_POSTPROCESS_TAGS.H

OpenSauce's post-processing tags (shader_postprocess_generic,
effect_postprocess_generic, effect_postprocess_collection and
shader_postprocess_globals: shpg, efpg, efpc, sppg), as the tag validator
checks them (port/linux/game/tag_schema_opensauce.c) and post-processing
reads them (port/linux/game/opensauce_postprocess.c). Their layouts, sizes
and block maxima are OpenSauce's (Kornner Studios, GPL-3.0-or-later): its
shader_postprocess_*_definitions.hpp and effect_postprocess_*_definitions.hpp,
which assert their sizes, read as facts.
*/

#ifndef OPENSAUCE_POSTPROCESS_TAGS_H
#define OPENSAUCE_POSTPROCESS_TAGS_H

#include "tag_files/tag_groups.h"

/* ---------- structures */

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

/* (OpenSauce's sizes) */
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

#endif
