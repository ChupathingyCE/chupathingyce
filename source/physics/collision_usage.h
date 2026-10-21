/*
COLLISION_USAGE.H

header included in hcex build.
*/

#ifndef __COLLISION_USAGE_H
#define __COLLISION_USAGE_H
#pragma once

#include "cseries/cseries.h"
#include <xtl.h>

/* ---------- constants */

enum
{
	_collision_user_unknown,
	_collision_user_ai_look,
	_collision_user_ai_lineofsight,
	_collision_user_ai_comms,
	_collision_user_ai_lineoffire,
	_collision_user_ai_melee,
	_collision_user_aim_assist,
	_collision_user_bipeds,
	_collision_user_melee,
	_collision_user_decals,
	_collision_user_area_damage,
	_collision_user_items,
	_collision_user_observer,
	_collision_user_point_physics,
	_collision_user_projectiles,
	_collision_user_structure_lighting,
	_collision_user_sounds,
	_collision_user_vehicles,
	_collision_user_limp_body_physics,
	_collision_user_objects,
	_collision_user_ui,
	_collision_user_debugging,
	NUMBER_OF_COLLISION_USER_TYPES,
};

enum
{
	_collision_function_vector_structure,
	_collision_function_vector_objects,
	_collision_function_vector_bounds_object,
	_collision_function_vector_intersect_model,
	_collision_function_vector_intersect_bsp_object,
	_collision_function_vector_intersect_bsp_structure,
	_collision_function_sphere_intersect_bsp_object,
	_collision_function_sphere_intersect_bsp_structure,
	NUMBER_OF_COLLISION_FUNCTION_TYPES,
};

enum
{
	MAXIMUM_COLLISION_USER_STACK_DEPTH = 32,
};

/* ---------- macros */

/* ---------- structures */

/* ---------- prototypes/COLLISION_USAGE.C */

void collision_log_initialize(
	void);
void collision_log_enable(
	boolean enable);
void collision_log_render(
	void);
void collision_log_start_time(
	LARGE_INTEGER *start_time);
void collision_log_end_time(
	short collision_function,
	__int64 end_time);
void collision_log_usage(
	short collision_function);
void collision_log_display(
	char *buffer);
void collision_log_begin_period(
	short time_period);
void collision_log_continue_period(
	short time_period);
void collision_log_end_period(
	void);

#ifdef HALO_CONSOLE_NO_DEBUG_TOOLS
/* port: the collision log counts every collision test by its kind and its
user and times each with two reads of the performance counter, and each
frame clears its record three times and copies it three times, all for the
debug display that collision_log_render draws. A console's build without
its developer tools (halo_xbox_prefix.h) has no such display, so the
counting and timing are nothing here: in a busy multiplayer match the tests
run in the thousands a tick. The game's results do not depend on any of
it. The users' stack (global_current_collision_users) stays as it is. */
#define collision_log_start_time(start_time) ((void)0)
#define collision_log_end_time(collision_function, start_time) ((void)0)
#define collision_log_usage(collision_function) ((void)0)
#define collision_log_begin_period(time_period) ((void)0)
#define collision_log_continue_period(time_period) ((void)0)
#define collision_log_end_period() ((void)0)
#endif

/* ---------- globals */

extern short global_current_collision_user_depth;
extern short global_current_collision_users[MAXIMUM_COLLISION_USER_STACK_DEPTH];

/* ---------- public code */

#endif // __COLLISION_USAGE_H
