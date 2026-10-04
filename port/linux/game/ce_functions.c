/*
CE_FUNCTIONS.C

Custom Edition maps' periodic and transition functions (cache_files.c,
Custom Edition maps). Halo PC has the Xbox's functions, twelve periodic ones
(one, zero, cosine, ..., spark: periodic_functions.c) and six transition
ones (linear to cosine), but some community maps' tags name others: the
lights of Hornets Nest's assault rifle muzzle flash and Foundation's
Spartan laser name periodic function 256, a 1 (zero) written in the other
byte order by the tool that made them. Halo PC draws them all the same; the
Xbox's code looks such a function up past the end of its tables (and halts
on it where asserts are on, as when the assault rifle fires).

When the map's tags load, every function field the game evaluates that names
none is made a valid one: the function its bytes name the other way round if
that is one, else the first (one, or linear), as a zeroed field names. The
fields are those of objects' functions, damage effects' and continuous
damage effects' camera shakes and transitions, transparent chicago shaders'
maps' animations, model and environment shaders' animations and
self-illumination, lights' gels and lens flares' reflections (the game's
own definitions: object_definitions.h, damage_effect_definitions.h,
shader_texture_animation.h, rasterizer_xbox_models.c,
rasterizer_xbox_environment.c, rasterizer_lights.c, shaders.c). Any other
the game evaluates is evaluated as the first (periodic_functions.c).

Every block and field read lies in the map's tag cache, or is passed over.
*/

#ifdef HALO_CUSTOM_EDITION

#include "cseries.h"
#include "cseries_windows.h"
#include "errors.h"

/* ---------- constants */

enum
{
	CE_TAG_INSTANCE_SIZE = 0x20,
	/* (cache_files.c's: where a Custom Edition map's tags are) */
	CE_TAG_CACHE_BASE = 0x40440000,
	CE_TAG_CACHE_SIZE = 0x01700000,

	/* (periodic_functions.c's NUMBER_OF_PERIODIC_FUNCTIONS and
	NUMBER_OF_TRANSITION_FUNCTIONS) */
	CE_PERIODIC_FUNCTION_COUNT = 12,
	CE_TRANSITION_FUNCTION_COUNT = 6,

	/* a tag block: its count and its elements' address */
	TAG_BLOCK_SIZE = 0x0c,
	TAG_BLOCK_ADDRESS_OFFSET = 0x04,

	/* an object's functions (object_function_definition): their periodic
	function, wobble function and transition */
	OBJECT_FUNCTIONS_OFFSET = 0x158,
	OBJECT_FUNCTION_SIZE = 0x168,
	OBJECT_FUNCTION_TYPE_OFFSET = 0x0a,
	OBJECT_FUNCTION_WOBBLE_TYPE_OFFSET = 0x0e,
	OBJECT_FUNCTION_TRANSITION_OFFSET = 0x1e,

	/* a damage effect (damage_effect_definition): its screen flash's and
	vibrations' fades, its camera impulse's and shake's transitions, its
	camera shake's periodic function */
	DAMAGE_EFFECT_SCREEN_FLASH_FADE_OFFSET = 0x38,
	DAMAGE_EFFECT_LOW_VIBRATE_FADE_OFFSET = 0x64,
	DAMAGE_EFFECT_HIGH_VIBRATE_FADE_OFFSET = 0x78,
	DAMAGE_EFFECT_CAMERA_IMPULSE_TRANSITION_OFFSET = 0x9c,
	DAMAGE_EFFECT_CAMERA_SHAKE_TRANSITION_OFFSET = 0xd0,
	DAMAGE_EFFECT_CAMERA_SHAKE_FUNCTION_OFFSET = 0xe8,
	DAMAGE_EFFECT_READ_SIZE = 0xf8,
	/* a continuous damage effect's camera shake's periodic function */
	CONTINUOUS_DAMAGE_EFFECT_CAMERA_SHAKE_FUNCTION_OFFSET = 0x58,
	CONTINUOUS_DAMAGE_EFFECT_READ_SIZE = 0x60,

	/* a texture animation (shader_texture_animation): its u, v and
	rotation functions */
	TEXTURE_ANIMATION_U_FUNCTION_OFFSET = 0x02,
	TEXTURE_ANIMATION_V_FUNCTION_OFFSET = 0x12,
	TEXTURE_ANIMATION_R_FUNCTION_OFFSET = 0x22,
	TEXTURE_ANIMATION_SIZE = 0x38,

	/* a transparent chicago shader's maps (shader_transparent_chicago_map),
	each one's texture animation; an extended one's four-stage and
	two-stage maps */
	CHICAGO_MAPS_OFFSET = 0x54,
	CHICAGO_EXTENDED_TWO_STAGE_MAPS_OFFSET = 0x60,
	CHICAGO_MAP_SIZE = 0xdc,
	CHICAGO_MAP_ANIMATION_OFFSET = 0xa4,
	CHICAGO_READ_SIZE = 0x6c,

	/* a model shader (shader_model_definition): its self-illumination
	animation function and texture animation */
	MODEL_SHADER_SELF_ILLUMINATION_FUNCTION_OFFSET = 0x72,
	MODEL_SHADER_ANIMATION_OFFSET = 0xfc,
	MODEL_SHADER_READ_SIZE = 0x190,

	/* an environment shader (shader_environment_definition): its diffuse
	u and v animation functions, its self-illumination's primary,
	secondary and plasma animation functions */
	ENVIRONMENT_SHADER_U_FUNCTION_OFFSET = 0x150,
	ENVIRONMENT_SHADER_V_FUNCTION_OFFSET = 0x15c,
	ENVIRONMENT_SHADER_PRIMARY_FUNCTION_OFFSET = 0x1b4,
	ENVIRONMENT_SHADER_SECONDARY_FUNCTION_OFFSET = 0x1f0,
	ENVIRONMENT_SHADER_PLASMA_FUNCTION_OFFSET = 0x22c,
	ENVIRONMENT_SHADER_READ_SIZE = 0x344,

	/* a light (point_light_definition): its gel's yaw, roll and pitch
	functions */
	LIGHT_GEL_YAW_FUNCTION_OFFSET = 0x8e,
	LIGHT_GEL_ROLL_FUNCTION_OFFSET = 0x96,
	LIGHT_GEL_PITCH_FUNCTION_OFFSET = 0x9e,
	LIGHT_READ_SIZE = 0xa8,

	/* a lens flare's reflections (lens_flare_reflection), each one's
	animation function */
	LENS_FLARE_REFLECTIONS_OFFSET = 0xc4,
	LENS_FLARE_REFLECTION_SIZE = 0x80,
	LENS_FLARE_REFLECTION_FUNCTION_OFFSET = 0x72,
	LENS_FLARE_READ_SIZE = 0xf0,

	/* (each *_READ_SIZE: as much of such a tag as is read) */

	/* (the most elements of a block looked at, more than any holds) */
	CE_FUNCTIONS_MAXIMUM_ELEMENTS = 0x1000,
	/* (the tags named in the log) */
	CE_FUNCTIONS_MAXIMUM_LOGGED = 8,
};

/* ---------- structures */

/* (cache_files.c's) */
struct ce_tag_instance
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	unsigned long tag_index;
	unsigned long name;
	unsigned long base_address;
	unsigned long indexed;
	unsigned long unused;
};

/* ---------- globals */

static struct
{
	struct ce_tag_instance const *instance;
	long periodic, transition;
	long logged;
} ce_functions;

/* ---------- private code */

/* size bytes at an Xbox address in the map's tag cache, or NULL */
static byte *ce_functions_pointer(
	unsigned long address,
	unsigned long size)
{
	if (address < CE_TAG_CACHE_BASE || address - CE_TAG_CACHE_BASE > CE_TAG_CACHE_SIZE ||
		size > CE_TAG_CACHE_SIZE - (address - CE_TAG_CACHE_BASE))
	{
		return NULL;
	}
	return xbox_pointer(address);
}

/* a function field naming one of count functions: if it names none, the one
its bytes name the other way round, or the first */
static void ce_function_field(
	byte *field,
	short count,
	long *fixed)
{
	short value = *(short *)field;
	short swapped = (short)(((unsigned short)value >> 8) | (((unsigned short)value & 0xff) << 8));

	if (value >= 0 && value < count)
		return;
	*(short *)field = swapped >= 0 && swapped < count ? swapped : 0;
	(*fixed)++;
	if (ce_functions.logged < CE_FUNCTIONS_MAXIMUM_LOGGED)
	{
		char const *name = (char const *)ce_functions_pointer(ce_functions.instance->name, 1);

		error(_error_silent, "Custom Edition maps: %s names %s function %d, made %d",
			name ? name : "a tag", count == CE_PERIODIC_FUNCTION_COUNT ? "periodic" : "transition", value,
			*(short *)field);
		ce_functions.logged++;
	}
}

static void ce_periodic_function(
	byte *field)
{
	ce_function_field(field, CE_PERIODIC_FUNCTION_COUNT, &ce_functions.periodic);
}

static void ce_transition_function(
	byte *field)
{
	ce_function_field(field, CE_TRANSITION_FUNCTION_COUNT, &ce_functions.transition);
}

/* a block's elements (element_size bytes each), or NULL */
static byte *ce_functions_block(
	byte const *block,
	unsigned long element_size,
	long *count)
{
	*count = *(long const *)block;
	if (*count <= 0 || *count > CE_FUNCTIONS_MAXIMUM_ELEMENTS)
		return NULL;
	return ce_functions_pointer(*(unsigned long const *)(block + TAG_BLOCK_ADDRESS_OFFSET),
		(unsigned long)*count * element_size);
}

static void ce_texture_animation(
	byte *animation)
{
	ce_periodic_function(animation + TEXTURE_ANIMATION_U_FUNCTION_OFFSET);
	ce_periodic_function(animation + TEXTURE_ANIMATION_V_FUNCTION_OFFSET);
	ce_periodic_function(animation + TEXTURE_ANIMATION_R_FUNCTION_OFFSET);
}

static void ce_object_functions(
	byte *object)
{
	long count, index;
	byte *functions = ce_functions_block(object + OBJECT_FUNCTIONS_OFFSET, OBJECT_FUNCTION_SIZE, &count);

	for (index = 0; functions && index < count; index++)
	{
		byte *function = functions + index * OBJECT_FUNCTION_SIZE;

		ce_periodic_function(function + OBJECT_FUNCTION_TYPE_OFFSET);
		ce_periodic_function(function + OBJECT_FUNCTION_WOBBLE_TYPE_OFFSET);
		ce_transition_function(function + OBJECT_FUNCTION_TRANSITION_OFFSET);
	}
}

static void ce_chicago_maps(
	byte const *block)
{
	long count, index;
	byte *maps = ce_functions_block(block, CHICAGO_MAP_SIZE, &count);

	for (index = 0; maps && index < count; index++)
		ce_texture_animation(maps + index * CHICAGO_MAP_SIZE + CHICAGO_MAP_ANIMATION_OFFSET);
}

static void ce_lens_flare_reflections(
	byte *lens_flare)
{
	long count, index;
	byte *reflections = ce_functions_block(lens_flare + LENS_FLARE_REFLECTIONS_OFFSET, LENS_FLARE_REFLECTION_SIZE,
		&count);

	for (index = 0; reflections && index < count; index++)
		ce_periodic_function(reflections + index * LENS_FLARE_REFLECTION_SIZE + LENS_FLARE_REFLECTION_FUNCTION_OFFSET);
}

/* ---------- public code */

/* a Custom Edition map's tags loaded (cache_files.c), before its extended
chicago shaders are made chicago ones (ce_models.c): every function field
the game evaluates made one there is */
void ce_functions_tags_loaded(
	void *tag_instances,
	long tag_count)
{
	long index;

	csmemset(&ce_functions, 0, sizeof(ce_functions));
	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance const *instance = (struct ce_tag_instance const *)((byte *)tag_instances +
			index * CE_TAG_INSTANCE_SIZE);
		unsigned long group_tag = instance->group_tag;
		byte *tag;

		ce_functions.instance = instance;
		if (instance->indexed)
			continue;
		if (group_tag == 'obje' || instance->parent_group_tags[0] == 'obje' ||
			instance->parent_group_tags[1] == 'obje')
		{
			tag = ce_functions_pointer(instance->base_address, OBJECT_FUNCTIONS_OFFSET + TAG_BLOCK_SIZE);
			if (tag)
				ce_object_functions(tag);
		}
		else if (group_tag == 'jpt!')
		{
			tag = ce_functions_pointer(instance->base_address, DAMAGE_EFFECT_READ_SIZE);
			if (tag)
			{
				ce_transition_function(tag + DAMAGE_EFFECT_SCREEN_FLASH_FADE_OFFSET);
				ce_transition_function(tag + DAMAGE_EFFECT_LOW_VIBRATE_FADE_OFFSET);
				ce_transition_function(tag + DAMAGE_EFFECT_HIGH_VIBRATE_FADE_OFFSET);
				ce_transition_function(tag + DAMAGE_EFFECT_CAMERA_IMPULSE_TRANSITION_OFFSET);
				ce_transition_function(tag + DAMAGE_EFFECT_CAMERA_SHAKE_TRANSITION_OFFSET);
				ce_periodic_function(tag + DAMAGE_EFFECT_CAMERA_SHAKE_FUNCTION_OFFSET);
			}
		}
		else if (group_tag == 'cdmg')
		{
			tag = ce_functions_pointer(instance->base_address, CONTINUOUS_DAMAGE_EFFECT_READ_SIZE);
			if (tag)
				ce_periodic_function(tag + CONTINUOUS_DAMAGE_EFFECT_CAMERA_SHAKE_FUNCTION_OFFSET);
		}
		else if (group_tag == 'schi' || group_tag == 'scex')
		{
			tag = ce_functions_pointer(instance->base_address, CHICAGO_READ_SIZE);
			if (tag)
			{
				ce_chicago_maps(tag + CHICAGO_MAPS_OFFSET);
				if (group_tag == 'scex')
					ce_chicago_maps(tag + CHICAGO_EXTENDED_TWO_STAGE_MAPS_OFFSET);
			}
		}
		else if (group_tag == 'soso')
		{
			tag = ce_functions_pointer(instance->base_address, MODEL_SHADER_READ_SIZE);
			if (tag)
			{
				ce_periodic_function(tag + MODEL_SHADER_SELF_ILLUMINATION_FUNCTION_OFFSET);
				ce_texture_animation(tag + MODEL_SHADER_ANIMATION_OFFSET);
			}
		}
		else if (group_tag == 'senv')
		{
			tag = ce_functions_pointer(instance->base_address, ENVIRONMENT_SHADER_READ_SIZE);
			if (tag)
			{
				ce_periodic_function(tag + ENVIRONMENT_SHADER_U_FUNCTION_OFFSET);
				ce_periodic_function(tag + ENVIRONMENT_SHADER_V_FUNCTION_OFFSET);
				ce_periodic_function(tag + ENVIRONMENT_SHADER_PRIMARY_FUNCTION_OFFSET);
				ce_periodic_function(tag + ENVIRONMENT_SHADER_SECONDARY_FUNCTION_OFFSET);
				ce_periodic_function(tag + ENVIRONMENT_SHADER_PLASMA_FUNCTION_OFFSET);
			}
		}
		else if (group_tag == 'ligh')
		{
			tag = ce_functions_pointer(instance->base_address, LIGHT_READ_SIZE);
			if (tag)
			{
				ce_periodic_function(tag + LIGHT_GEL_YAW_FUNCTION_OFFSET);
				ce_periodic_function(tag + LIGHT_GEL_ROLL_FUNCTION_OFFSET);
				ce_periodic_function(tag + LIGHT_GEL_PITCH_FUNCTION_OFFSET);
			}
		}
		else if (group_tag == 'lens')
		{
			tag = ce_functions_pointer(instance->base_address, LENS_FLARE_READ_SIZE);
			if (tag)
				ce_lens_flare_reflections(tag);
		}
	}
	error(_error_silent, "Custom Edition maps: %ld periodic and %ld transition functions there are none of made valid",
		ce_functions.periodic, ce_functions.transition);
}

#endif
