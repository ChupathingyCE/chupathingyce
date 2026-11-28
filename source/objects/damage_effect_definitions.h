/*
DAMAGE_EFFECT_DEFINITIONS.H

header included in hcex build.
*/

#ifndef __DAMAGE_EFFECT_DEFINITIONS_H
#define __DAMAGE_EFFECT_DEFINITIONS_H
#pragma once

/* ---------- headers */

#include "real_math.h"

/* ---------- constants */

enum
{
	DAMAGE_EFFECT_DEFINITION_TAG = 'jpt!',
	DAMAGE_EFFECT_DEFINITION_VERSION = 6,
	CONTINUOUS_DAMAGE_EFFECT_DEFINITION_TAG = 'cdmg',
	CONTINUOUS_DAMAGE_EFFECT_DEFINITION_VERSION = 1,
};

enum
{
	_vibrate_frequency_low = 0,
	_vibrate_frequency_high,
	NUMBER_OF_vibrate_FREQUENCIES,

	_vibrate_frequency_left = 0,
	_vibrate_frequency_right,
};

/* ---------- macros */

#define damage_effect_definition_get(index) ((struct damage_effect_definition *)tag_get(DAMAGE_EFFECT_DEFINITION_TAG, index))
#define continuous_damage_effect_definition_get(index) ((struct continuous_damage_effect_definition *)tag_get(CONTINUOUS_DAMAGE_EFFECT_DEFINITION_TAG, (index)))

/* ---------- structures */

struct damage_definition
{
	short side_effect;
	short category;
	unsigned long flags;
	real area_of_effect_core_radius;
	real damage_minimum;
	real damage_lower_bound;
	real damage_upper_bound;
	real vehicle_passthrough_penalty;
	real active_camouflage_damage;
	real stun;
	real maximum_stun;
	real stun_time;
	unsigned long stun_unused[1];
	real instantaneous_acceleration;
	real instantaneous_acceleration_zero_scale_factor;
	unsigned long instantaneous_acceleration_unused[1];
	real material_modifiers[MAXIMUM_NUMBER_OF_MATERIAL_TYPES];
};

struct continuous_camera_shake_definition
{
	real random_translation_magnitude;
	real random_rotation_magnitude;
	long unused_motion_scales[3];
	short periodic_function;
	word pad1;
	real periodic_period;
	real periodic_weight;
	real zero_scale_factor;
	long unused[7];
};

struct camera_impulse_definition
{
	real temporary_duration;
	short temporary_transition;
	word pad;
	real temporary_rotation;
	real temporary_translation;
	real temporary_jitter_lower_bound;
	real temporary_jitter_upper_bound;
	real temporary_zero_scale_factor;
	unsigned long temporary_unused[1];
	real permanent_angle;
	real permanent_zero_scale_factor;
	unsigned long permanent_unused[3];
};

struct camera_shake_definition
{
	real duration;
	short falloff_transition_function;
	word pad0;
	real random_translation_magnitude;
	real random_rotation_magnitude;
	long unused_motion_scales[3];
	short periodic_function;
	word pad1;
	real periodic_period;
	real periodic_weight;
	real zero_scale_factor;
	long unused[7];
};

struct screen_flash_definition
{
	short type;
	short priority;
	unsigned long unused1[3];
	real duration;
	short fade_function;
	word pad;
	unsigned long unused2[2];
	real max_intensity;
	real zero_scale_factor;
	real_argb_color screen_flash_color;
};

struct continuous_vibrate_definition
{
	real frequencies[2];
	unsigned long unused[6];
};

struct continuous_damage_effect_definition
{
	real falloff_radius;
	real cutoff_radius;
	real cutoff_scale;
	unsigned long area_of_effect_unused[6];
	struct continuous_vibrate_definition vibrate;
	struct continuous_camera_shake_definition camera_shake;
	unsigned long unused[40];
	struct damage_definition damage;
};

struct vibrate_frequency_definition
{
	real frequency;
	real duration;
	short fade_function;
	word pad;
	unsigned long unused[2];
};

struct vibrate_definition
{
	struct vibrate_frequency_definition vibrate_frequencies[NUMBER_OF_vibrate_FREQUENCIES];
	real zero_scale_factor;
	unsigned long unused[4];
};

struct damage_breaking_effect_definition
{
	real forward_velocity;
	real forward_radius;
	real forward_exponent;
	long forward_unused[3];
	real outward_velocity;
	real outward_radius;
	real outward_exponent;
	long outward_unused[3];
};

struct damage_effect_definition
{
	real falloff_radius;
	real cutoff_radius;
	real cutoff_scale;
	unsigned long flags;
	unsigned long area_of_effect_unused[5];
	struct screen_flash_definition screen_flash;
	struct vibrate_definition vibrate;
	struct camera_impulse_definition camera_impulse;
	struct camera_shake_definition camera_shake;
	struct tag_reference sound;
	unsigned long unused1[28];
	struct damage_breaking_effect_definition breaking_effect;
	struct damage_definition damage;
};

/* (port) a converted Halo 2 damage effect's screen flashes (h2_weapons.c):
Halo 2's player responses, its unshielded one in screen_flash and its
shielded one here, in unused1, marked - player_effects.c flashes the
shielded one while the player's shields hold (Halo 2's short cyan flash;
its red one for health). Zero in every Halo CE damage effect */
#define H2_SHIELDED_SCREEN_FLASH_MAGIC 0x68327366UL   /* 'h2sf' */

struct h2_shielded_screen_flash
{
	unsigned long magic;
	struct screen_flash_definition screen_flash;
};

/* (port) a converted Halo 2 damage effect's shake radius (h2_weapons.c:
Halo 2's "shake radius", the decompilation's player radius): players past
the damage's cutoff radius but within it are shaken at no damage, their
shake scaled by 1 - distance / shake radius (damage.c's
area_of_effect_cause_damage). Zero in every Halo CE damage effect */
#define H2_AREA_SHAKE_MAGIC 0x68327368UL   /* 'h2sh' */

struct h2_area_shake
{
	unsigned long magic;
	real shake_radius;
};

/* (port) a converted Halo 2 damage effect's multipliers on the armor classes
of Halo 2's characters (h2_damage.c: a class is a pair of armors of the
damage table, the global materials of the Hunter, Brute, Grunt...), after its
shielded screen flash in unused1: this magic, then each class's multiplier of
the damage table in 1/1024ths. A collision material or shield of a Halo 2
character names its class (damage_resistance_material.pad, damage_resistance.pad0:
class + 1) and takes the multiplier in place of its material type's.
Zero in every Halo CE damage effect */
#define H2_CLASS_MODIFIERS_MAGIC 0x68326D63UL   /* 'h2mc' */
#define H2_MAXIMUM_ARMOR_CLASSES 24

struct h2_class_modifiers
{
	unsigned long magic;
	unsigned short multiplier[H2_MAXIMUM_ARMOR_CLASSES];
};

/* the byte offset of struct h2_class_modifiers in a damage effect definition */
#define H2_CLASS_MODIFIERS_OFFSET (offsetof(struct damage_effect_definition, unused1) + sizeof(struct h2_shielded_screen_flash))

typedef char h2_class_modifiers_size_assert[
	sizeof(struct h2_shielded_screen_flash) + sizeof(struct h2_class_modifiers) <= sizeof(((struct damage_effect_definition *)0)->unused1) ? 1 : -1];

typedef char h2_area_shake_size_assert[
	sizeof(struct h2_area_shake) <= sizeof(((struct damage_effect_definition *)0)->area_of_effect_unused) ? 1 : -1];

typedef char h2_shielded_screen_flash_size_assert[
	sizeof(struct h2_shielded_screen_flash) <= sizeof(((struct damage_effect_definition *)0)->unused1) ? 1 : -1];

/* ---------- prototypes */

/* ---------- globals */

/* ---------- public code */

#endif // __DAMAGE_EFFECT_DEFINITIONS_H
