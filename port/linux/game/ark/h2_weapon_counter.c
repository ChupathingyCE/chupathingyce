/*
H2_WEAPON_COUNTER.C

port: a Halo 2 weapon's on-gun counter (the battle rifle's two digits).
Halo 2 draws each digit with an illum_detail shader whose bitmap image is
picked by the weapon's rounds (its animated parameter: input
primary_ammunition_tens or _ones). The converter (h2_weapons.c,
h2w_digit_shader) makes twenty shaders, h2\shaders\digits tens 0..9 and
digits ones 0..9, the model's parts drawn with the 0s; models.c asks here
for the one to draw instead: the tens' or ones' digit of the rounds in the
weapon's magazine. Each digit's tag is found by its name: a tag's index is
its salt (high half) and its place (low half), and the salt differs from tag
to tag, so the 0's index plus the digit is no tag (cache_files.c's "i don't
think ... is a tag index" halt, Oct 23). HALO_H2_NO_WEAPON_COUNTERS: none
(the converter's).
*/

#include "cseries.h"
#include "items/weapons.h"
#include "objects/objects.h"
#include "devices/devices.h"
#include "cache/cache_files.h"
#include "scenario/scenario.h"
#include "tag_files/tag_groups.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (render_epoch.c: changes with every map and game state replaced; the
shader images' and the digits' tags looked up for that one) */
extern unsigned long halo_map_generation;
static unsigned long h2si_generation, h2wc_generation;

void platform_log(const char *format, ...);

static struct
{
	long scenario;
	long digits[2][10];  /* the tens' and ones' shader tags 0..9; NONE */
} h2wc = { NONE };

/* a Halo 2 shader's other images, picked by its object's input
(h2_level.c shader_image_variants: the shader "NAME image N INPUT LOW HIGH"
draws image N where the input is in LOW..HIGH; NAME, its first image, where
it is in none) - the Zanzibar gate keypad's screen by the control's
position. Found by name each new map: the model shaders' ("... model") and
the environment ones' */
enum
{
	H2_SHADER_IMAGES = 128,
};

static struct
{
	long scenario;
	long count;
	struct
	{
		long base, shader;
		short input;     /* 0 position, 1 power, 2 random constant, 3 the vehicle's model variant (its number: index + 1) */
		float low, high;
	} images[H2_SHADER_IMAGES];
} h2si = { NONE };

static void h2_shader_images_find(void)
{
	struct tag_iterator iterator;
	long tag_index;

	h2si.scenario = global_scenario_index;
	h2si_generation = halo_map_generation;
	h2si.count = 0;
	tag_iterator_new(&iterator, 'shdr');
	while ((tag_index = tag_iterator_next(&iterator)) != NONE && h2si.count < H2_SHADER_IMAGES)
	{
		char const *name = tag_get_name(tag_index);
		char const *mark = name ? strstr(name, " image ") : NULL;
		char input[32], base_name[256];
		int image, used = 0;
		float low, high;
		struct tag_iterator bases;
		long base;

		if (!mark || strncmp(name, "h2\\", 3) || sscanf(mark + 7, "%d %31s %f %f%n", &image, input, &low, &high, &used) != 4 ||
			image < 1 || (strcmp(input, "position") && strcmp(input, "power") && strcmp(input, "random_constant") && strcmp(input, "variant")) ||
			mark - name >= (long)sizeof(base_name) - 8)
		{
			continue;
		}
		/* (its first image: the name without " image ...", a model shader's " model" kept) */
		snprintf(base_name, sizeof(base_name), "%.*s%s", (int)(mark - name), name, mark + 7 + used);
		tag_iterator_new(&bases, 'shdr');
		while ((base = tag_iterator_next(&bases)) != NONE && strcmp(tag_get_name(base), base_name))
			;
		if (base == NONE)
			continue;
		h2si.images[h2si.count].base = base;
		h2si.images[h2si.count].shader = tag_index;
		h2si.images[h2si.count].input = (short)(!strcmp(input, "position") ? 0 : !strcmp(input, "power") ? 1 : !strcmp(input, "variant") ? 3 : 2);
		h2si.images[h2si.count].low = low;
		h2si.images[h2si.count].high = high;
		h2si.count++;
	}
}

/* an object's "random constant" (Halo 2's object function input, MCC
halo2.dll 0x1808d36c3): its datum index through a linear congruential step,
the high half over 65535 - 0..1, the same each frame and on every machine
that numbers the object alike */
static real h2_object_random_constant(long object_index)
{
	unsigned long step = (unsigned long)object_index * 0x19660DUL + 0x3C6EF35FUL;

	return (real)((step & 0xFFFFFFFFUL) >> 16) / 65535.0f;
}

/* the shader to draw a part of shader with: its image the object's input picks */
static long h2_shader_image_shader(long object_index, long shader)
{
	long k;
	boolean any = FALSE;

	/* (the map's generation too: consecutive maps' scenarios can have the same tag index) */
	if (h2si.scenario != global_scenario_index || h2si_generation != halo_map_generation)
		h2_shader_images_find();
	for (k = 0; k < h2si.count && !any; k++)
		any = h2si.images[k].base == shader;
	if (!any || object_index == NONE || !object_try_and_get(object_index))
		return shader;
	for (k = 0; k < h2si.count; k++)
	{
		real value;

		if (h2si.images[k].base != shader)
			continue;
		if (h2si.images[k].input == 2)
			value = h2_object_random_constant(object_index);
		else if (h2si.images[k].input == 3)
			value = (real)object_get(object_index)->object.variant_number;
		else if (!object_try_and_get_and_verify_type(object_index, _object_mask_device))
			continue;
		else
			value = h2si.images[k].input ? device_get_power(object_index) : device_get_position(object_index);
		/* (HALO_H2_SHADER_IMAGE_LOG: the input as it changes) */
		{
			static int log = -1;
			static real logged = -2.0f;

			if (log < 0)
				log = getenv("HALO_H2_SHADER_IMAGE_LOG") != NULL;
			if (log && value != logged)
			{
				logged = value;
				platform_log("h2 shader image: object %08lx %s %.3f", (unsigned long)object_index,
					h2si.images[k].input == 3 ? "variant" : h2si.images[k].input == 2 ? "random constant" : h2si.images[k].input ? "power" : "position", value);
			}
		}
		if (value >= h2si.images[k].low - 0.0005f && value <= h2si.images[k].high + 0.0005f)
			return h2si.images[k].shader;
	}
	return shader;
}

/* a vehicle's shader drawn as the level's hologram character when the
vehicle is attached to it (h2_level.c hologram_twin: the shader "NAME
hologram CHARACTER"; Regret's gravity throne in 00a) - found by name each
new map */
enum
{
	H2_HOLOGRAM_TWINS = 64,
};

static struct
{
	long scenario;
	long count;
	struct
	{
		long base, twin;
		char owner[64];
	} twins[H2_HOLOGRAM_TWINS];
} h2ht = { NONE };

static void h2_hologram_twins_find(void)
{
	struct tag_iterator iterator;
	long tag_index;

	h2ht.scenario = global_scenario_index;
	h2ht.count = 0;
	tag_iterator_new(&iterator, 'shdr');
	while ((tag_index = tag_iterator_next(&iterator)) != NONE && h2ht.count < H2_HOLOGRAM_TWINS)
	{
		char const *name = tag_get_name(tag_index);
		char const *mark = name ? strstr(name, " hologram ") : NULL;
		char base_name[256];
		struct tag_iterator bases;
		long base;

		if (!mark || strncmp(name, "h2\\", 3) || mark - name >= (long)sizeof(base_name) || !mark[10])
			continue;
		/* (its base: the shader, and the model shader made of it - "NAME
		model", which a model's parts draw with) */
		snprintf(base_name, sizeof(base_name), "%.*s", (int)(mark - name), name);
		tag_iterator_new(&bases, 'shdr');
		while ((base = tag_iterator_next(&bases)) != NONE && h2ht.count < H2_HOLOGRAM_TWINS)
		{
			char const *base_tag_name = tag_get_name(base);
			size_t length = strlen(base_name);

			if (strncmp(base_tag_name, base_name, length) || (base_tag_name[length] && strcmp(base_tag_name + length, " model")))
				continue;
			h2ht.twins[h2ht.count].base = base;
			h2ht.twins[h2ht.count].twin = tag_index;
			snprintf(h2ht.twins[h2ht.count].owner, sizeof(h2ht.twins[0].owner), "%s", mark + 10);
			if (getenv("HALO_H2_HOLOGRAM_LOG"))
				platform_log("h2 hologram: %s drawn as %s on %s", base_tag_name, name, mark + 10);
			h2ht.count++;
		}
	}
}

static long h2_hologram_shader(long object_index, long shader)
{
	struct object_datum *object, *root;
	char const *root_name, *slash;
	long k, root_index, steps;

	if (h2ht.scenario != global_scenario_index)
		h2_hologram_twins_find();
	if (!h2ht.count || object_index == NONE || !(object = object_try_and_get(object_index)) ||
		object->object.parent_object_index == NONE)
	{
		return shader;
	}
	for (k = 0; k < h2ht.count && h2ht.twins[k].base != shader; k++)
		;
	if (k == h2ht.count)
		return shader;
	/* (its outermost parent) */
	root_index = object_index;
	root = object;
	for (steps = 0; root->object.parent_object_index != NONE && steps < 16; steps++)
	{
		struct object_datum *parent = object_try_and_get(root->object.parent_object_index);

		if (!parent)
			break;
		root_index = root->object.parent_object_index;
		root = parent;
	}
	(void)root_index;
	root_name = root->definition_index != NONE ? tag_get_name(root->definition_index) : NULL;
	if (!root_name)
		return shader;
	slash = strrchr(root_name, '\\');
	root_name = slash ? slash + 1 : root_name;
	for (; k < h2ht.count; k++)
	{
		if (h2ht.twins[k].base == shader && !strncmp(root_name, h2ht.twins[k].owner, strlen(h2ht.twins[k].owner)))
			return h2ht.twins[k].twin;
	}
	return shader;
}

/* (models.c) the shader tag to draw a part of shader tag shader with, the
object (the weapon, first or third person) its own */
long h2_weapon_counter_shader(long object_index, long shader)
{
	struct object_datum *object;
	long rounds, place, digit, which;

	/* (a vehicle attached to the level's hologram character: drawn as the
	hologram, h2_hologram_shader) */
	if (shader != NONE)
	{
		long twin = h2_hologram_shader(object_index, shader);

		if (twin != shader)
			return twin;
	}
	/* (a shader whose image the object picks: h2_shader_image_shader) */
	{
		long picked = shader != NONE ? h2_shader_image_shader(object_index, shader) : shader;

		if (picked != shader)
			return picked;
	}

	if (h2wc.scenario != global_scenario_index || h2wc_generation != halo_map_generation)
	{
		char name[64];

		h2wc.scenario = global_scenario_index;
		h2wc_generation = halo_map_generation;
		for (which = 0; which < 2; which++)
			for (digit = 0; digit < 10; digit++)
			{
				snprintf(name, sizeof(name), "h2\\shaders\\digits %s %ld", which ? "ones" : "tens", digit);
				h2wc.digits[which][digit] = tag_loaded('schi', name);
			}
	}
	if (shader == NONE)
		return shader;
	if (shader == h2wc.digits[0][0])
		which = 0;
	else if (shader == h2wc.digits[1][0])
		which = 1;
	else
		return shader;
	if (object_index == NONE || !(object = object_try_and_get(object_index)) || object->object.type != _object_type_weapon)
		return shader;
	rounds = ((struct weapon_datum *)object)->weapon.magazines[0].rounds_loaded;
	rounds = rounds < 0 ? 0 : rounds > 99 ? 99 : rounds;
	place = which == 0 ? rounds / 10 : rounds % 10;
	return h2wc.digits[which][place] != NONE ? h2wc.digits[which][place] : shader;
}
