/*
OPENSAUCE_POSTPROCESS.C

OpenSauce's post-processing for its maps (opensauce_postprocess.h): what a
map's effect_postprocess_collection holds and how its effects are drawn,
OpenSauce's "internal" system for a map's own effects, and its bloom from
shader_postprocess_globals. The renderer draws them
(port/linux/src/opensauce_postprocess_gl.inl); this is the part that is the
same whatever draws:
- the collection's shaders (a shader_postprocess_generic that names a base
  shader draws with the base's compiled effect and its own parameters), its
  effects (each a list of shader instances, with parameters it exposes) and
  its effect instances (an effect at a render stage over a quad);
- an instance draws while its active flag (pp_set_effect_instance_active,
  and its "initially active" control) and its activation controls (in a
  cutscene, zoomed, in a vehicle; all of them, or any) say so, faded by its
  fade (pp_set_effect_instance_fade) over the scene as it was;
- a parameter's value each frame: its upper bound, or between its bounds by
  its animation function, or the value scripts give it through the
  collection's script variables (pp_set_effect_shader_variable_*), reached
  over the time they give;
- the bloom's values, reached over the time scripts give
  (pp_bloom_set_maximum_color, pp_bloom_set_exposure).
How each of those behaves is OpenSauce's (Kornner Studios,
GPL-3.0-or-later): its Rasterizer/PostProcessing sources (c_system_internal,
c_effect_postprocess, c_effect_instance_generic, c_parameter_instance,
shader_postprocess_runtime.inl's value rules, its interpolators, and
c_system_bloom), read as facts, including what it does that looks
backwards (a boolean parameter that is not animated is set to the opposite
of its "enabled", and an animated one is its lower value past halfway), as
the maps were made against it. The code is this game's.

Only a map with an OpenSauce header has any of this, and only one with a
collection or globals tag; every other map's stages return at once.
*/

#include "cseries.h"
#include "errors.h"
#include "opensauce.h"
#include "opensauce_postprocess.h"

#ifdef OPENSAUCE_POSTPROCESS

#include "opensauce_postprocess_tags.h"
#include "bitmaps/bitmap_group_lookup.h"
#include "cache/texture_cache.h"
#include "cutscene/cinematics.h"
#include "game/players.h"
#include "math/periodic_functions.h"
#include "memory/data.h"
#include "objects/objects.h"
#include "render/render_cameras.h"
#include "tag_files/tag_groups.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------- constants */

enum
{
	MAXIMUM_PP_SHADERS = 128,
	MAXIMUM_PP_EFFECTS = 32,
	MAXIMUM_PP_INSTANCES = 32,
	MAXIMUM_PP_EFFECT_SHADERS = 12,
	MAXIMUM_PP_SHADER_PARAMETERS = 64,
	MAXIMUM_PP_SCRIPT_VARIABLES = 32,
	MAXIMUM_PP_PASSES = 32,

	/* a technique's shader models (s_technique_definition) */
	_pp_technique_sm_1_0_bit = 0,
	_pp_technique_sm_2_0_bit,
	_pp_technique_sm_3_0_bit,

	/* an instance's activation controls (effect_activation_state) */
	_pp_activation_initially_active = 0,
	_pp_activation_in_cutscene,
	_pp_activation_player_zoomed,
	_pp_activation_player_in_vehicle,
	_pp_activation_invert_bit = 0,

	/* how a parameter's value is had (c_parameter_instance) */
	_pp_parameter_static = 0,
	_pp_parameter_animated,
	_pp_parameter_texture,

	/* a parameter's animation (periodic_function, and its flags) */
	_pp_periodic_one = 0,
	_pp_periodic_noise = 8,
	_pp_animation_inverted_bit = 0,
	_pp_animation_multichannel_noise_bit,
	_pp_animation_ignore_alpha_bit,

	/* the bloom's flags (s_shader_postprocess_globals_bloom) */
	_pp_bloom_enabled_bit = 0,
	_pp_bloom_after_hud_bit,

	TAG_INSTANCE_SIZE = 0x20,
};

/* ---------- structures */

/* a value linearly reached over a time (c_interp_linear) */
struct pp_linear
{
	real value;
	real time;
};

struct pp_parameter
{
	struct opensauce_postprocess_parameter const *tag;
	short process;
	boolean overridden;
	/* an animated one's values (its interpolator's, each component) and
	when it last moved */
	real animation[4];
	real update_time;
	/* an overridden one's start and target (as the type's words) and how
	far between them it is */
	unsigned long start[4], target[4];
	struct pp_linear interpolation;
};

/* a shader of the collection's (its own tag, and the root of its bases,
whose effect it draws with) */
struct pp_shader
{
	long tag_index;
	struct opensauce_shader_postprocess_generic const *tag;
	struct opensauce_shader_postprocess_generic const *root;
	struct opensauce_postprocess_technique const *technique;
	long effect;
	boolean loaded, valid;
};

struct pp_shader_instance
{
	short shader;
	boolean active;
	struct pp_parameter parameters[MAXIMUM_PP_SHADER_PARAMETERS];
	short parameter_count;
};

struct pp_effect
{
	struct opensauce_effect_postprocess_generic const *tag;
	struct opensauce_postprocess_collection_effect const *collection_effect;
	struct pp_shader_instance instances[MAXIMUM_PP_EFFECT_SHADERS];
	short instance_count;
	/* each script variable's parameter (NULL where it names none) */
	struct pp_parameter *script_variables[MAXIMUM_PP_SCRIPT_VARIABLES];
	short script_variable_count;
	boolean valid;
};

struct pp_instance
{
	struct opensauce_postprocess_effect_instance const *tag;
	short effect;
	boolean active;
	/* (its first draw, and a draw that failed, said once a map) */
	boolean logged_drawn, logged_failed;
	real fade_start, fade_end, fade_current;
	struct pp_linear fade;
};

/* the bloom's values (s_shader_postprocess_bloom_definition): size,
exposure, mix amount, minimum and maximum colors */
struct pp_bloom_values
{
	real size, exposure, mix_amount;
	real minimum[3], maximum[3];
};

/* ---------- globals */

int config_boolean(const char *name);
unsigned long system_milliseconds(void);

static struct
{
	boolean loaded;
	boolean has_collection, has_bloom;
	boolean prepared;
	/* (said once a map) */
	boolean logged_unavailable, logged_drawing;
	struct opensauce_effect_postprocess_collection const *collection;
	struct pp_shader shaders[MAXIMUM_PP_SHADERS];
	short shader_count;
	struct pp_effect effects[MAXIMUM_PP_EFFECTS];
	short effect_count;
	struct pp_instance instances[MAXIMUM_PP_INSTANCES];
	short instance_count;
	/* the bloom's */
	unsigned short bloom_flags;
	struct pp_bloom_values bloom_current, bloom_target;
	struct pp_linear bloom_exposure_interpolation, bloom_color_interpolation;
	/* the frame's */
	unsigned long last_milliseconds;
	boolean timing;
	real elapsed;
	struct opensauce_pp_frame frame;
	boolean frame_valid;
	float last_clip_from_world[4][4];
	boolean last_clip_valid;
	long frame_serial;
} pp;

/* ---------- private code */

static void pp_log(char const *format, ...)
{
	char text[512];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	error(_error_silent, "OpenSauce post-processing: %s", text);
}

static void *pp_tag_root(struct tag_block const *block_owner_unused, unsigned long address)
{
	(void)block_owner_unused;
	return address ? xbox_pointer(address) : NULL;
}

static void *pp_block_element(struct tag_block const *block, long index, unsigned long size)
{
	if (index < 0 || index >= block->count || !block->address)
		return NULL;
	return (byte *)xbox_pointer((unsigned long)block->address) + index * size;
}

/* a tag's root by its handle, if it is one of group */
static void *pp_tag_get(void *tag_instances, long tag_count, long tag_index, unsigned long group_tag)
{
	byte const *instance;
	unsigned long instance_group, instance_index, address;

	if (tag_index == NONE || (tag_index & 0xFFFF) >= tag_count)
		return NULL;
	instance = (byte const *)tag_instances + (tag_index & 0xFFFF) * TAG_INSTANCE_SIZE;
	memcpy(&instance_group, instance, sizeof(instance_group));
	memcpy(&instance_index, instance + 0x0C, sizeof(instance_index));
	memcpy(&address, instance + 0x14, sizeof(address));
	if (instance_group != group_tag || (long)instance_index != tag_index)
		return NULL;
	return pp_tag_root(NULL, address);
}

static char const *pp_tag_name(void *tag_instances, long tag_count, long tag_index)
{
	unsigned long name;

	if (tag_index == NONE || (tag_index & 0xFFFF) >= tag_count)
		return "";
	memcpy(&name, (byte const *)tag_instances + (tag_index & 0xFFFF) * TAG_INSTANCE_SIZE + 0x10, sizeof(name));
	return name ? (char const *)xbox_pointer(name) : "";
}

static void pp_linear_begin(struct pp_linear *linear, real time)
{
	linear->time = time;
	linear->value = time == 0.0f ? 1.0f : 0.0f;
}

static void pp_linear_update(struct pp_linear *linear, real delta)
{
	if (linear->time == 0.0f)
		linear->value = 1.0f;
	else
		linear->value += delta / linear->time;
	if (linear->value > 1.0f)
		linear->value = 1.0f;
}

/* a parameter's words: its type's, and how many (bools and ints 1, floats
their count, colors 4) */
static short pp_parameter_words(struct opensauce_postprocess_parameter const *tag)
{
	switch (tag->value_type)
	{
	case _opensauce_pp_value_boolean:
	case _opensauce_pp_value_integer:
		return 1;
	case _opensauce_pp_value_float:
		return tag->value_count >= 1 && tag->value_count <= 4 ? tag->value_count : 1;
	case _opensauce_pp_value_argb_color:
		return 4;
	}
	return 0;
}

/* a parameter's lower and upper bounds as words (s_shader_postprocess_
value_union; a boolean's are its enabled and the opposite) */
static void pp_parameter_bounds(struct opensauce_postprocess_parameter const *tag, unsigned long lower[4],
	unsigned long upper[4])
{
	byte const *value = tag->value;

	memset(lower, 0, sizeof(unsigned long) * 4);
	memset(upper, 0, sizeof(unsigned long) * 4);
	switch (tag->value_type)
	{
	case _opensauce_pp_value_boolean:
		lower[0] = value[0] != 0;
		upper[0] = value[0] == 0;
		break;
	case _opensauce_pp_value_integer:
		memcpy(&lower[0], value, 4);
		memcpy(&upper[0], value + 4, 4);
		break;
	case _opensauce_pp_value_float:
	case _opensauce_pp_value_argb_color:
		memcpy(lower, value, 4 * (size_t)pp_parameter_words(tag));
		memcpy(upper, value + 16, 4 * (size_t)pp_parameter_words(tag));
		break;
	}
}

static real pp_word_real(unsigned long word)
{
	real value;

	memcpy(&value, &word, sizeof(value));
	return value;
}

static unsigned long pp_real_word(real value)
{
	unsigned long word;

	memcpy(&word, &value, sizeof(word));
	return word;
}

/* lower to upper by interpolation (each component's), into out: a
boolean's lower past halfway (Interpolation.hpp's rule), an integer
truncated */
static void pp_values_between(short type, short words, unsigned long const lower[4], unsigned long const upper[4],
	real const interpolation[4], unsigned long out[4])
{
	short index;

	for (index = 0; index < words; index++)
	{
		switch (type)
		{
		case _opensauce_pp_value_boolean:
			out[index] = interpolation[index] > 0.5f ? lower[index] : upper[index];
			break;
		case _opensauce_pp_value_integer:
			out[index] = (unsigned long)(long)((real)((long)upper[index] - (long)lower[index]) * interpolation[index] +
				(real)(long)lower[index]);
			break;
		default:
			out[index] = pp_real_word((pp_word_real(upper[index]) - pp_word_real(lower[index])) * interpolation[index] +
				pp_word_real(lower[index]));
			break;
		}
	}
}

static void pp_parameter_setup(struct pp_parameter *parameter, struct opensauce_postprocess_parameter const *tag)
{
	memset(parameter, 0, sizeof(*parameter));
	parameter->tag = tag;
	if (tag->value_type == _opensauce_pp_value_texture)
		parameter->process = _pp_parameter_texture;
	else if (tag->animation_function == _pp_periodic_one)
		parameter->process = _pp_parameter_static;
	else
		parameter->process = _pp_parameter_animated;
}

/* a parameter that scripts set (SetupParameterOverride): its start and
target its default (the upper bound; a boolean's enabled), reached
linearly */
static void pp_parameter_override(struct pp_parameter *parameter)
{
	unsigned long lower[4], upper[4];
	struct opensauce_postprocess_parameter const *tag = parameter->tag;

	if (parameter->overridden || tag->value_type == _opensauce_pp_value_texture ||
		tag->value_type == _opensauce_pp_value_matrix)
	{
		return;
	}
	pp_parameter_bounds(tag, lower, upper);
	if (tag->value_type == _opensauce_pp_value_boolean)
		upper[0] = lower[0];
	memcpy(parameter->start, upper, sizeof(parameter->start));
	memcpy(parameter->target, upper, sizeof(parameter->target));
	parameter->overridden = TRUE;
	parameter->process = _pp_parameter_animated;
	parameter->interpolation.value = 0.0f;
	parameter->interpolation.time = 0.0f;
}

static void pp_parameter_update(struct pp_parameter *parameter, real delta)
{
	struct opensauce_postprocess_parameter const *tag = parameter->tag;
	real input;
	short index, count;

	if (parameter->process != _pp_parameter_animated)
		return;
	if (parameter->overridden)
	{
		pp_linear_update(&parameter->interpolation, delta);
		return;
	}
	/* (c_interp_function_pp_anim: updated at its rate, if it has one) */
	if (tag->animation_rate != 0.0f)
	{
		boolean update = FALSE;

		parameter->update_time += delta;
		while (parameter->update_time > tag->animation_rate)
		{
			update = TRUE;
			parameter->update_time -= tag->animation_rate;
		}
		if (!update)
			return;
	}
	input = tag->animation_duration == 0.0f ? 0.0f : pp.elapsed / tag->animation_duration;
	count = tag->value_type == _opensauce_pp_value_argb_color ? 4 : pp_parameter_words(tag);
	for (index = 0; index < count; index++)
	{
		if (tag->animation_function == _pp_periodic_noise &&
			TEST_FLAG(tag->animation_flags, _pp_animation_multichannel_noise_bit))
		{
			input += (real)(7 * index) * input;
		}
		parameter->animation[index] = periodic_function_evaluate(tag->animation_function, input);
		if (TEST_FLAG(tag->animation_flags, _pp_animation_inverted_bit))
			parameter->animation[index] = 1.0f - parameter->animation[index];
	}
	if (TEST_FLAG(tag->animation_flags, _pp_animation_ignore_alpha_bit))
		parameter->animation[0] = 1.0f;
}

/* a parameter's value now, as its type's words */
static void pp_parameter_value(struct pp_parameter const *parameter, unsigned long out[4])
{
	struct opensauce_postprocess_parameter const *tag = parameter->tag;
	unsigned long lower[4], upper[4];
	short words = pp_parameter_words(tag);

	memset(out, 0, sizeof(unsigned long) * 4);
	if (parameter->overridden)
	{
		real interpolation[4];
		short index;

		for (index = 0; index < 4; index++)
			interpolation[index] = parameter->interpolation.value;
		pp_values_between(tag->value_type, words, parameter->start, parameter->target, interpolation, out);
		return;
	}
	pp_parameter_bounds(tag, lower, upper);
	if (parameter->process == _pp_parameter_animated)
		pp_values_between(tag->value_type, words, lower, upper, parameter->animation, out);
	else
		memcpy(out, upper, sizeof(unsigned long) * 4);
}

/* a parameter given to its shader's effect */
static void pp_parameter_apply(struct pp_parameter const *parameter, long effect)
{
	struct opensauce_postprocess_parameter const *tag = parameter->tag;
	unsigned long value[4];

	if (parameter->process == _pp_parameter_texture)
	{
		struct bitmap_data *bitmap = NULL;
		unsigned short bitmap_index;

		memcpy(&bitmap_index, tag->value, sizeof(bitmap_index));
		if (tag->bitmap.index != NONE)
			bitmap = bitmap_group_try_and_get_bitmap(tag->bitmap.index, (short)bitmap_index);
		halo_opensauce_pp_parameter_texture(effect, tag->value_name,
			bitmap ? _texture_cache_bitmap_get_hardware_format(bitmap, TRUE, TRUE) : NULL);
		return;
	}
	pp_parameter_value(parameter, value);
	if (tag->value_type == _opensauce_pp_value_argb_color)
	{
		/* (ARGB in the tag, as Direct3D's color: red, green, blue, alpha) */
		unsigned long rgba[4];

		rgba[0] = value[1];
		rgba[1] = value[2];
		rgba[2] = value[3];
		rgba[3] = value[0];
		halo_opensauce_pp_parameter_set(effect, tag->value_name, _opensauce_pp_value_float, rgba, 4);
		return;
	}
	halo_opensauce_pp_parameter_set(effect, tag->value_name, tag->value_type, value, pp_parameter_words(tag));
}

/* the shader instance's parameter by name */
static struct pp_parameter *pp_instance_parameter(struct pp_shader_instance *instance, char const *name)
{
	short index;

	for (index = 0; index < instance->parameter_count; index++)
	{
		if (!strncmp(instance->parameters[index].tag->value_name, name, 32))
			return &instance->parameters[index];
	}
	return NULL;
}

/* the collection's shader with a tag handle */
static short pp_shader_find(long tag_index)
{
	short index;

	for (index = 0; index < pp.shader_count; index++)
	{
		if (pp.shaders[index].tag_index == tag_index)
			return index;
	}
	return NONE;
}

/* the technique a shader draws by: shader model 3's, else 2's, else 1's
(c_shader_postprocess::GetActiveTechnique on a shader model 3 device) */
static struct opensauce_postprocess_technique const *pp_technique_choose(
	struct opensauce_shader_postprocess_generic const *root)
{
	short bit;

	for (bit = _pp_technique_sm_3_0_bit; bit >= _pp_technique_sm_1_0_bit; bit--)
	{
		long index;

		for (index = 0; index < root->techniques.count; index++)
		{
			struct opensauce_postprocess_technique const *technique =
				pp_block_element(&root->techniques, index, sizeof(*technique));

			if (technique && TEST_FLAG(technique->shader_model, bit))
				return technique;
		}
	}
	return NULL;
}

/* the shaders' effects loaded, once a map, as the renderer can (their
first draw): a shader is valid if its effect loads with its technique and
has every parameter its tag sets (c_shader_generic::ValidateImpl) */
static void pp_prepare(void)
{
	short index;

	if (pp.prepared)
		return;
	pp.prepared = TRUE;
	for (index = 0; index < pp.shader_count; index++)
	{
		struct pp_shader *shader = &pp.shaders[index];
		short other;

		shader->effect = NONE;
		if (!shader->root || !shader->technique)
			continue;
		/* (a base shared by several: loaded once) */
		for (other = 0; other < index; other++)
		{
			if (pp.shaders[other].root == shader->root && pp.shaders[other].loaded)
			{
				shader->effect = pp.shaders[other].effect;
				break;
			}
		}
		if (other == index && shader->root->shader_code_binary.size > 0)
		{
			shader->effect = halo_opensauce_pp_effect_load(
				xbox_pointer((unsigned long)shader->root->shader_code_binary.address),
				(unsigned long)shader->root->shader_code_binary.size, "", shader->technique->name);
		}
		shader->loaded = TRUE;
		shader->valid = shader->effect != NONE;
		/* (every parameter its tag sets the effect must have) */
		if (shader->valid && shader->tag)
		{
			long parameter_index;

			for (parameter_index = 0; parameter_index < shader->tag->parameters.count; parameter_index++)
			{
				struct opensauce_postprocess_parameter const *parameter =
					pp_block_element(&shader->tag->parameters, parameter_index, sizeof(*parameter));

				if (parameter && !halo_opensauce_pp_parameter_exists(shader->effect, parameter->value_name))
				{
					pp_log("shader %d's parameter '%.32s' is not in its effect: OpenSauce would not draw it", (int)index,
						parameter->value_name);
					shader->valid = FALSE;
					break;
				}
			}
		}
	}
	for (index = 0; index < pp.effect_count; index++)
	{
		struct pp_effect *effect = &pp.effects[index];
		short instance_index;

		effect->valid = effect->instance_count > 0;
		for (instance_index = 0; instance_index < effect->instance_count; instance_index++)
		{
			struct pp_shader_instance *instance = &effect->instances[instance_index];

			if (instance->shader == NONE || !pp.shaders[instance->shader].valid)
				effect->valid = FALSE;
		}
		if (!effect->valid)
		{
			pp_log("effect %d ('%.32s') can't be drawn: it has no shaders, or one did not load (said above)",
				(int)index, effect->collection_effect ? effect->collection_effect->name : "");
		}
	}
}

/* whether the local player is in a vehicle */
static boolean pp_player_in_vehicle(void)
{
	long player_index = player_data ? local_player_get_player_index(0) : NONE;
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	struct object_datum *unit = player && player->unit_index != NONE ? object_try_and_get(player->unit_index) : NULL;

	return unit && unit->object.parent_object_index != NONE;
}

/* an activation control's value (c_effect_instance_generic::
GetActivationValue) */
static boolean pp_activation_value(struct opensauce_postprocess_activation_control const *control)
{
	boolean value = TRUE;

	switch (control->state)
	{
	case _pp_activation_in_cutscene:
		value = cinematic_in_progress();
		break;
	case _pp_activation_player_zoomed:
		value = player_data && local_player_get_player_index(0) != NONE && player_control_get_zoom_level(0) != NONE;
		break;
	case _pp_activation_player_in_vehicle:
		value = pp_player_in_vehicle();
		break;
	}
	return TEST_FLAG(control->flags, _pp_activation_invert_bit) ? !value : value;
}

/* whether an instance draws (c_effect_instance_generic::IsActive): its
active flag, then its controls (none: active; the "initially active" one
only at the start) */
static boolean pp_instance_active(struct pp_instance const *instance)
{
	struct opensauce_postprocess_effect_instance const *tag = instance->tag;
	boolean active = instance->active;
	long index;

	if (!instance->active)
		return FALSE;
	if (tag->activation_controls.count == 0)
		return TRUE;
	for (index = 0; index < tag->activation_controls.count; index++)
	{
		struct opensauce_postprocess_activation_control const *control =
			pp_block_element(&tag->activation_controls, index, sizeof(*control));

		if (!control || control->state == _pp_activation_initially_active)
			continue;
		if (tag->activation_operation)
			active |= pp_activation_value(control);
		else
		{
			if (!pp_activation_value(control))
				return FALSE;
			active = TRUE;
		}
	}
	return active;
}

static boolean pp_instance_initially_active(struct opensauce_postprocess_effect_instance const *tag)
{
	long index;

	for (index = 0; index < tag->activation_controls.count; index++)
	{
		struct opensauce_postprocess_activation_control const *control =
			pp_block_element(&tag->activation_controls, index, sizeof(*control));

		if (control && control->state == _pp_activation_initially_active)
			return pp_activation_value(control);
	}
	return TRUE;
}

/* the frame's time and every value's update (c_system_internal::Update) */
static void pp_update(void)
{
	unsigned long now = system_milliseconds();
	real delta = pp.timing ? (real)(now - pp.last_milliseconds) / 1000.0f : 0.0f;
	short index;

	pp.timing = TRUE;
	pp.last_milliseconds = now;
	if (delta < 0.0f || delta > 1.0f)
		delta = 0.0f;
	pp.elapsed += delta;
	for (index = 0; index < pp.effect_count; index++)
	{
		struct pp_effect *effect = &pp.effects[index];
		short instance_index, parameter_index;

		for (instance_index = 0; instance_index < effect->instance_count; instance_index++)
		{
			struct pp_shader_instance *instance = &effect->instances[instance_index];

			for (parameter_index = 0; parameter_index < instance->parameter_count; parameter_index++)
				pp_parameter_update(&instance->parameters[parameter_index], delta);
		}
	}
	for (index = 0; index < pp.instance_count; index++)
	{
		struct pp_instance *instance = &pp.instances[index];

		pp_linear_update(&instance->fade, delta);
		instance->fade_current = instance->fade_start + (instance->fade_end - instance->fade_start) *
			instance->fade.value;
	}
	pp_linear_update(&pp.bloom_exposure_interpolation, delta);
	pp_linear_update(&pp.bloom_color_interpolation, delta);
}

/* an instance's effect drawn (c_effect_postprocess::Render) */
static boolean pp_instance_draw(struct pp_instance const *instance)
{
	struct pp_effect *effect;
	struct opensauce_postprocess_effect_instance const *tag = instance->tag;
	boolean drew = FALSE, failed = FALSE;
	short index;

	if (instance->effect < 0 || instance->effect >= pp.effect_count)
		return FALSE;
	effect = &pp.effects[instance->effect];
	if (!effect->valid || instance->fade_current == 0.0f)
		return FALSE;
	halo_opensauce_pp_effect_begin();
	for (index = 0; index < effect->instance_count && !failed; index++)
	{
		struct pp_shader_instance *shader_instance = &effect->instances[index];
		struct pp_shader const *shader = &pp.shaders[shader_instance->shader];
		struct opensauce_pp_pass passes[MAXIMUM_PP_PASSES];
		short tessellation[2];
		short pass_count = 0, parameter_index;
		long pass_index;

		if (!shader_instance->active)
			continue;
		for (parameter_index = 0; parameter_index < shader_instance->parameter_count; parameter_index++)
			pp_parameter_apply(&shader_instance->parameters[parameter_index], shader->effect);
		for (pass_index = 0; pass_index < shader->technique->passes.count && pass_count < MAXIMUM_PP_PASSES; pass_index++)
		{
			struct opensauce_postprocess_pass const *pass =
				pp_block_element(&shader->technique->passes, pass_index, sizeof(*pass));

			if (!pass)
				break;
			passes[pass_count].flags = pass->flags;
			passes[pass_count].render_chain = pass->render_chain;
			pass_count++;
		}
		/* (OpenSauce's quads: 1 to 64 a side, bounds in [0, 1]) */
		tessellation[0] = tag->tessellation[0] < 1 ? 1 : tag->tessellation[0] > 64 ? 64 : tag->tessellation[0];
		tessellation[1] = tag->tessellation[1] < 1 ? 1 : tag->tessellation[1] > 64 ? 64 : tag->tessellation[1];
		if (halo_opensauce_pp_shader_draw(shader->effect, passes, pass_count, tessellation, tag->x_bounds,
			tag->y_bounds))
		{
			drew = TRUE;
		}
		else
			failed = TRUE;
	}
	halo_opensauce_pp_effect_end(drew && !failed, instance->fade_current);
	return drew && !failed;
}

static void pp_bloom_values_now(struct pp_bloom_values *values)
{
	real exposure = pp.bloom_exposure_interpolation.value, color = pp.bloom_color_interpolation.value;
	short index;

	*values = pp.bloom_current;
	values->exposure = pp.bloom_current.exposure + (pp.bloom_target.exposure - pp.bloom_current.exposure) * exposure;
	for (index = 0; index < 3; index++)
	{
		values->maximum[index] = pp.bloom_current.maximum[index] +
			(pp.bloom_target.maximum[index] - pp.bloom_current.maximum[index]) * color;
	}
}

/* the frame's matrices for the G-buffer: the world to clip transform the
game's vertex shaders use (rasterizer_set_frustum_z's) */
static void pp_frame_matrices(struct render_frustum const *frustum)
{
	short column, row;

	for (column = 0; column < 4; column++)
	{
		for (row = 0; row < 4; row++)
		{
			real value = 0.0f;

			if (row < 3)
			{
				value = frustum->world_to_view.n[row][0] * frustum->projection_matrix[0][column] +
					frustum->world_to_view.n[row][1] * frustum->projection_matrix[1][column] +
					frustum->world_to_view.n[row][2] * frustum->projection_matrix[2][column];
			}
			else
			{
				value = frustum->world_to_view.n[3][0] * frustum->projection_matrix[0][column] +
					frustum->world_to_view.n[3][1] * frustum->projection_matrix[1][column] +
					frustum->world_to_view.n[3][2] * frustum->projection_matrix[2][column] +
					frustum->projection_matrix[3][column];
			}
			pp.frame.clip_from_world[column][row] = value;
		}
	}
	pp.frame.projection_scale[0] = frustum->projection_matrix[0][0];
	pp.frame.projection_scale[1] = frustum->projection_matrix[1][1];
	pp.frame.near_clip = frustum->z_near;
	pp.frame.far_clip = frustum->z_far;
}

/* ---------- public code */

int opensauce_postprocess_active(void)
{
	return pp.loaded && opensauce_map_active();
}

void opensauce_postprocess_tags_loaded(void *tag_instances, long tag_count)
{
	long tag_index;
	struct opensauce_shader_postprocess_globals const *globals = NULL;

	opensauce_postprocess_map_unloaded();
	if (!opensauce_map_active() || !tag_instances || tag_count <= 0)
		return;
	/* (the first of each group, as OpenSauce takes it: there can be one) */
	for (tag_index = 0; tag_index < tag_count; tag_index++)
	{
		byte const *instance = (byte const *)tag_instances + tag_index * TAG_INSTANCE_SIZE;
		unsigned long group, handle;

		memcpy(&group, instance, sizeof(group));
		memcpy(&handle, instance + 0x0C, sizeof(handle));
		if (group == 'efpc' && !pp.collection)
			pp.collection = pp_tag_get(tag_instances, tag_count, (long)handle, 'efpc');
		else if (group == 'sppg' && !globals)
			globals = pp_tag_get(tag_instances, tag_count, (long)handle, 'sppg');
	}
	if (globals && globals->bloom_globals.count > 0)
	{
		struct opensauce_postprocess_bloom const *bloom = pp_block_element(&globals->bloom_globals, 0, sizeof(*bloom));

		if (bloom)
		{
			pp.has_bloom = TRUE;
			pp.bloom_flags = bloom->flags;
			pp.bloom_current.size = bloom->size;
			pp.bloom_current.exposure = bloom->exposure;
			pp.bloom_current.mix_amount = bloom->mix_amount;
			memcpy(pp.bloom_current.minimum, bloom->minimum_color, sizeof(pp.bloom_current.minimum));
			memcpy(pp.bloom_current.maximum, bloom->maximum_color, sizeof(pp.bloom_current.maximum));
			pp.bloom_target = pp.bloom_current;
			pp_linear_begin(&pp.bloom_exposure_interpolation, 0.0f);
			pp_linear_begin(&pp.bloom_color_interpolation, 0.0f);
		}
	}
	if (pp.collection)
	{
		struct opensauce_effect_postprocess_collection const *collection = pp.collection;
		long index;

		pp.has_collection = TRUE;
		/* the shaders, each with the root of its bases */
		for (index = 0; index < collection->shaders.count && pp.shader_count < MAXIMUM_PP_SHADERS; index++)
		{
			struct opensauce_postprocess_shader_reference const *reference =
				pp_block_element(&collection->shaders, index, sizeof(*reference));
			struct pp_shader *shader = &pp.shaders[pp.shader_count];
			short depth;

			memset(shader, 0, sizeof(*shader));
			shader->tag_index = reference ? reference->shader.index : NONE;
			shader->tag = reference ? pp_tag_get(tag_instances, tag_count, shader->tag_index, 'shpg') : NULL;
			shader->effect = NONE;
			shader->root = shader->tag;
			for (depth = 0; shader->root && shader->root->base_shader.index != NONE && depth < 8; depth++)
				shader->root = pp_tag_get(tag_instances, tag_count, shader->root->base_shader.index, 'shpg');
			shader->technique = shader->root ? pp_technique_choose(shader->root) : NULL;
			if (!shader->tag || !shader->root || !shader->technique)
			{
				pp_log("shader '%s' has no shader_postprocess_generic, base or technique to draw with",
					pp_tag_name(tag_instances, tag_count, shader->tag_index));
			}
			pp.shader_count++;
		}
		/* the effects and their shader instances */
		for (index = 0; index < collection->effects.count && pp.effect_count < MAXIMUM_PP_EFFECTS; index++)
		{
			struct opensauce_postprocess_collection_effect const *collection_effect =
				pp_block_element(&collection->effects, index, sizeof(*collection_effect));
			struct pp_effect *effect = &pp.effects[pp.effect_count++];
			long shader_index, variable_index;

			memset(effect, 0, sizeof(*effect));
			effect->collection_effect = collection_effect;
			effect->tag = collection_effect ?
				pp_tag_get(tag_instances, tag_count, collection_effect->effect.index, 'efpg') : NULL;
			if (!effect->tag)
				continue;
			for (shader_index = 0; shader_index < effect->tag->shader_indices.count &&
				effect->instance_count < MAXIMUM_PP_EFFECT_SHADERS; shader_index++)
			{
				struct opensauce_postprocess_shader_index const *shader_reference_index =
					pp_block_element(&effect->tag->shader_indices, shader_index, sizeof(*shader_reference_index));
				struct opensauce_postprocess_shader_reference const *shader_reference = shader_reference_index ?
					pp_block_element(&effect->tag->shaders, shader_reference_index->shader, sizeof(*shader_reference)) :
					NULL;
				struct pp_shader_instance *instance = &effect->instances[effect->instance_count++];
				long parameter_index;

				memset(instance, 0, sizeof(*instance));
				instance->active = TRUE;
				instance->shader = shader_reference ? pp_shader_find(shader_reference->shader.index) : NONE;
				if (instance->shader == NONE || !pp.shaders[instance->shader].tag)
				{
					instance->shader = NONE;
					continue;
				}
				for (parameter_index = 0; parameter_index < pp.shaders[instance->shader].tag->parameters.count &&
					instance->parameter_count < MAXIMUM_PP_SHADER_PARAMETERS; parameter_index++)
				{
					struct opensauce_postprocess_parameter const *parameter = pp_block_element(
						&pp.shaders[instance->shader].tag->parameters, parameter_index, sizeof(*parameter));

					if (parameter)
						pp_parameter_setup(&instance->parameters[instance->parameter_count++], parameter);
				}
			}
			/* the parameters it exposes that scripts may set: theirs by
			their exposed names */
			for (variable_index = 0; variable_index < collection_effect->script_variables.count &&
				variable_index < MAXIMUM_PP_SCRIPT_VARIABLES; variable_index++)
			{
				struct opensauce_postprocess_script_variable const *variable =
					pp_block_element(&collection_effect->script_variables, variable_index, sizeof(*variable));
				long exposed_index;

				effect->script_variable_count = (short)(variable_index + 1);
				if (!variable)
					continue;
				for (exposed_index = 0; exposed_index < effect->tag->exposed_parameters.count; exposed_index++)
				{
					struct opensauce_postprocess_exposed_parameter const *exposed =
						pp_block_element(&effect->tag->exposed_parameters, exposed_index, sizeof(*exposed));

					if (exposed && !strncmp(exposed->exposed_name, variable->exposed_parameter_name, 32) &&
						exposed->shader_index >= 0 && exposed->shader_index < effect->instance_count)
					{
						struct pp_parameter *parameter = pp_instance_parameter(&effect->instances[exposed->shader_index],
							exposed->parameter_name);

						if (parameter)
						{
							pp_parameter_override(parameter);
							effect->script_variables[variable_index] = parameter;
						}
						break;
					}
				}
			}
		}
		/* the instances */
		for (index = 0; index < collection->effect_instances.count && pp.instance_count < MAXIMUM_PP_INSTANCES; index++)
		{
			struct opensauce_postprocess_effect_instance const *tag =
				pp_block_element(&collection->effect_instances, index, sizeof(*tag));
			struct pp_instance *instance = &pp.instances[pp.instance_count++];

			memset(instance, 0, sizeof(*instance));
			instance->tag = tag;
			instance->effect = tag ? tag->effect_index : NONE;
			instance->active = tag ? pp_instance_initially_active(tag) : FALSE;
			instance->fade_start = instance->fade_end = instance->fade_current = 1.0f;
			pp_linear_begin(&instance->fade, 0.0f);
		}
	}
	pp.loaded = pp.has_collection || pp.has_bloom;
	if (pp.loaded)
	{
		pp_log("%d shaders, %d effects, %d effect instances%s; drawn %s", (int)pp.shader_count, (int)pp.effect_count,
			(int)pp.instance_count, pp.has_bloom ? ", and bloom" : "",
			config_boolean("display.opensauce_post_processing") ? "by the OpenGL or bgfx renderer" :
			"never (display.opensauce_post_processing is off)");
	}
}

void opensauce_postprocess_map_unloaded(void)
{
	if (pp.prepared)
		halo_opensauce_pp_unload();
	memset(&pp, 0, sizeof(pp));
}

void opensauce_postprocess_render_stage(int stage, struct render_frustum const *frustum, short x0, short y0, short x1,
	short y1)
{
	int stages[3], stage_count = 0, index;

	if (!pp.loaded || !opensauce_map_active())
		return;
	if (!config_boolean("display.opensauce_post_processing"))
		return;
	if (!halo_opensauce_pp_available())
	{
		if (!pp.logged_unavailable)
		{
			pp.logged_unavailable = TRUE;
			pp_log("not drawn: only the OpenGL and bgfx renderers draw it");
		}
		return;
	}
	if (stage == _opensauce_pp_stage_pre_alpha_blended)
	{
		/* (the frame's first stage: its time, and the 3D view's matrices) */
		pp_prepare();
		pp_update();
		if (frustum)
		{
			memcpy(pp.frame.previous_clip_from_world, pp.last_clip_valid ? pp.last_clip_from_world :
				pp.frame.clip_from_world, sizeof(pp.frame.previous_clip_from_world));
			pp_frame_matrices(frustum);
			if (!pp.last_clip_valid)
				memcpy(pp.frame.previous_clip_from_world, pp.frame.clip_from_world, sizeof(pp.frame.clip_from_world));
			pp.frame.previous_valid = pp.last_clip_valid;
			memcpy(pp.last_clip_from_world, pp.frame.clip_from_world, sizeof(pp.last_clip_from_world));
			pp.last_clip_valid = TRUE;
			pp.frame_valid = TRUE;
		}
		pp.frame.frame_serial++;
		stages[stage_count++] = _opensauce_pp_stage_pre_blur;
		stages[stage_count++] = _opensauce_pp_stage_blur;
	}
	if (!pp.frame_valid || !pp.prepared)
		return;
	stages[stage_count++] = stage;
	pp.frame.x0 = x0;
	pp.frame.y0 = y0;
	pp.frame.x1 = x1;
	pp.frame.y1 = y1;
	/* (OpenSauce's HUD scale: 4:3 within the screen) */
	{
		real width = (real)(x1 - x0), height = (real)(y1 - y0);
		real ratio = height > 0.0f ? width / height : 4.0f / 3.0f;

		pp.frame.hud_scale[0] = ratio > 4.0f / 3.0f ? (4.0f / 3.0f) / ratio : 1.0f;
		pp.frame.hud_scale[1] = ratio > 4.0f / 3.0f ? 1.0f : ratio / (4.0f / 3.0f);
	}
	for (index = 0; index < stage_count; index++)
	{
		int current = stages[index];
		short instance_index;
		boolean begun = FALSE;

		for (instance_index = 0; instance_index < pp.instance_count; instance_index++)
		{
			struct pp_instance const *instance = &pp.instances[instance_index];

			if (!instance->tag || instance->tag->render_stage != current || !pp_instance_active(instance))
				continue;
			if (!begun)
			{
				if (!halo_opensauce_pp_stage_begin(&pp.frame, current))
					break;
				begun = TRUE;
			}
			if (pp_instance_draw(instance))
			{
				if (!instance->logged_drawn)
				{
					pp.instances[instance_index].logged_drawn = TRUE;
					pp_log("instance %d ('%.32s') drawn at stage %d", (int)instance_index, instance->tag->name, current);
				}
			}
			else if (!instance->logged_failed && instance->fade_current != 0.0f)
			{
				pp.instances[instance_index].logged_failed = TRUE;
				pp_log("instance %d ('%.32s') could not be drawn", (int)instance_index, instance->tag->name);
			}
		}
		/* the bloom: before the HUD, or after it */
		if (pp.has_bloom && current == (TEST_FLAG(pp.bloom_flags, _pp_bloom_after_hud_bit) ?
			_opensauce_pp_stage_pre_ui : _opensauce_pp_stage_pre_hud))
		{
			struct pp_bloom_values values;

			if (!begun && halo_opensauce_pp_stage_begin(&pp.frame, current))
				begun = TRUE;
			if (begun)
			{
				pp_bloom_values_now(&values);
				halo_opensauce_pp_bloom(values.size, values.exposure, values.mix_amount, values.minimum, values.maximum);
			}
		}
		if (begun)
		{
			halo_opensauce_pp_stage_end();
			if (!pp.logged_drawing)
			{
				pp.logged_drawing = TRUE;
				pp_log("drawing");
			}
		}
	}
}

void opensauce_postprocess_set_instance_active(short instance, int active)
{
	if (instance >= 0 && instance < pp.instance_count)
		pp.instances[instance].active = active != 0;
}

void opensauce_postprocess_set_instance_fade(short instance, float start, float end, float time)
{
	struct pp_instance *effect_instance;

	if (instance < 0 || instance >= pp.instance_count)
		return;
	effect_instance = &pp.instances[instance];
	effect_instance->fade_start = start;
	effect_instance->fade_end = end;
	effect_instance->fade_current = start;
	pp_linear_begin(&effect_instance->fade, time);
	if (time == 0.0f)
		effect_instance->fade_current = end;
}

float opensauce_postprocess_instance_current_fade(short instance)
{
	return instance >= 0 && instance < pp.instance_count ? pp.instances[instance].fade_current : 0.0f;
}

/* a script variable set (c_effect_internal::SetScriptedVariable): its
parameter from where it is now to the value, over the time. The effect's
index is checked against the instances' count, as OpenSauce checks it */
static void pp_variable_set(short effect, short variable, unsigned long const value[4], float time)
{
	struct pp_parameter *parameter;
	real interpolation[4];
	unsigned long current[4];
	short index;

	if (effect < 0 || effect >= pp.instance_count || effect >= pp.effect_count)
		return;
	if (variable < 0 || variable >= pp.effects[effect].script_variable_count)
		return;
	parameter = pp.effects[effect].script_variables[variable];
	if (!parameter || !parameter->overridden)
		return;
	for (index = 0; index < 4; index++)
		interpolation[index] = parameter->interpolation.value;
	pp_values_between(parameter->tag->value_type, 4, parameter->start, parameter->target, interpolation, current);
	memcpy(parameter->start, current, sizeof(parameter->start));
	memcpy(parameter->target, value, sizeof(parameter->target));
	pp_linear_begin(&parameter->interpolation, time);
}

void opensauce_postprocess_set_variable_integer(short effect, short variable, long value, float time)
{
	unsigned long words[4] = { 0, 0, 0, 0 };

	words[0] = (unsigned long)value;
	if (effect >= 0 && effect < pp.effect_count && variable >= 0 && variable < pp.effects[effect].script_variable_count &&
		pp.effects[effect].script_variables[variable] &&
		pp.effects[effect].script_variables[variable]->tag->value_type != _opensauce_pp_value_integer)
	{
		return;
	}
	pp_variable_set(effect, variable, words, time);
}

void opensauce_postprocess_set_variable_real(short effect, short variable, float const values[4], float time)
{
	unsigned long words[4];
	short index;

	if (effect >= 0 && effect < pp.effect_count && variable >= 0 && variable < pp.effects[effect].script_variable_count &&
		pp.effects[effect].script_variables[variable])
	{
		short type = pp.effects[effect].script_variables[variable]->tag->value_type;

		if (type != _opensauce_pp_value_float && type != _opensauce_pp_value_argb_color)
			return;
	}
	for (index = 0; index < 4; index++)
		words[index] = pp_real_word(values[index]);
	pp_variable_set(effect, variable, words, time);
}

void opensauce_postprocess_bloom_set_maximum_color(float const color[3], float time)
{
	struct pp_bloom_values now;

	pp_bloom_values_now(&now);
	memcpy(pp.bloom_current.maximum, now.maximum, sizeof(now.maximum));
	memcpy(pp.bloom_target.maximum, color, sizeof(pp.bloom_target.maximum));
	pp_linear_begin(&pp.bloom_color_interpolation, time);
}

void opensauce_postprocess_bloom_set_exposure(float exposure, float time)
{
	struct pp_bloom_values now;

	pp_bloom_values_now(&now);
	pp.bloom_current.exposure = now.exposure;
	pp.bloom_target.exposure = exposure;
	pp_linear_begin(&pp.bloom_exposure_interpolation, time);
}

#endif
