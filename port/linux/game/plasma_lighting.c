#ifndef HALO_CONSOLE_NO_LIGHT_EFFECTS /* (a console draws no Light effects: halo_port_features.h) */
/* The Light effects (display.light_effects): authored projectile lights lit
   for the frame drawn, without editing tags or advancing projectile simulation.
   rocket_lighting.c owns additional flight lights where rockets have none.
   This file is also the other modules' way in from object_lights.c.
   By chester (GulchCE), brought to Chupathingy with his permission. */
#include "cseries.h"
#include "objects/objects.h"
#include "tag_files/tag_files.h"
#include "plasma_lighting.h"
#include "authored_emitter_lighting.h"
#include "plasma_model_shader.h"
#include "plasma_occlusion.h"
#include "rocket_lighting.h"
#include "covenant_lighting.h"
#include "covenant_emitter_lighting.h"
#include "muzzle_lighting.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_lights.h"
#include "physics/collisions.h"
#include <string.h>

#include "lighting_config.h"

enum { PLASMA_LIGHTING_MAXIMUM = 128 };
static struct
{
	long light_index;
	long object_index;
	long rasterizer_index;
	int was_dynamic, category;
	float radius_scale, saved_radius;
} plasma_lighting_attachments[PLASMA_LIGHTING_MAXIMUM];
static int plasma_lighting_count;
static unsigned int	plasma_model_mask;

/* port: how much of a Light effects light reaches a biped (a player, an AI):
   the game's own lighting keeps them as readable as it made them. A light
   is copied for the bipeds, at UNIT_LIGHT_SCALE of its color and no further
   than its own radius was before the Light effects. Nor does one reach a
   biped through the level's walls (plasma_lighting_unit_light). */
#define UNIT_LIGHT_SCALE 0.35f
enum { UNIT_LIGHTS = 32 };
static struct
{
	long rasterizer_index, copy_index;
} unit_lights[UNIT_LIGHTS];
static int unit_light_count;

static float plasma_lighting_radius_scale(long rasterizer_index);
static void unit_light_submit(long rasterizer_index);

void plasma_model_lighting_reset(void)
{
	plasma_model_mask = 0;
	plasma_occlusion_model_slot(NONE, 0);
	plasma_occlusion_model_slot(NONE, 1);
}

void plasma_model_lighting_slot(long light_index, short slot)
{
	if (slot < 0 || slot >= 2)
		return;
	plasma_model_mask &= ~(1U << slot);
	plasma_occlusion_model_slot(light_index, slot);
	if (plasma_lighting_selected(light_index))
		plasma_model_mask |= 1U << slot;
}

unsigned int plasma_model_lighting_mask(void)
{
	return (lighting_config_boolean("display.plasma_lighting") || lighting_config_boolean("display.muzzle_lighting") ||
		covenant_emitter_lighting_enabled() || authored_emitter_lighting_enabled()) ? plasma_model_mask : 0;
}

void plasma_lighting_prepare(long light_index, long object_index, long definition_index)
{
	struct object_datum *object;
	const char *name;
	int index, category;
	float covenant_scale, authored_scale = 1.0f;

	if ((!lighting_config_boolean("display.plasma_lighting") && !authored_emitter_lighting_enabled()) || object_index == NONE)
		return;
	if (covenant_lighting_owns(light_index, object_index))
		return; /* Owned glows have their own radius and frame cleanup. */
	object = object_try_and_get(object_index);
	if (!object)
		return;
	category = authored_emitter_lighting_profile(object_index, definition_index, &authored_scale);
	if (!category && (!lighting_config_boolean("display.plasma_lighting") || object->object.type != _object_type_projectile))
		return;
	name = tag_get_name(object->definition_index);
	covenant_scale = covenant_lighting_scale(name);
	/* Exact tag policies keep unrelated custom projectiles stock. */
	/* The pistol overcharge uses a tag in the rifle directory. */
	if (!category && (!name || (!covenant_scale && strcmp(name, "weapons\\plasma rifle\\bolt") &&
		strcmp(name, "weapons\\plasma pistol\\bolt") &&
		strcmp(name, "weapons\\plasma rifle\\charged bolt") &&
		strcmp(name, "weapons\\rocket launcher\\rocket"))))
		return;
	for (index = 0; index < plasma_lighting_count; index++)
		if (plasma_lighting_attachments[index].light_index == light_index &&
			plasma_lighting_attachments[index].object_index == object_index)
			return; /* shared by split-screen views */
	if (plasma_lighting_count == PLASMA_LIGHTING_MAXIMUM)
		return; /* excess lights retain stock behavior */
	if (!light_port_attachment_radius(light_index, object_index,
		&plasma_lighting_attachments[plasma_lighting_count].saved_radius, FALSE))
		return;
	plasma_lighting_attachments[plasma_lighting_count].light_index = light_index;
	plasma_lighting_attachments[plasma_lighting_count].was_dynamic = light_port_dynamic_set(light_index, object_index, TRUE);
	plasma_lighting_attachments[plasma_lighting_count].rasterizer_index = NONE;
	plasma_lighting_attachments[plasma_lighting_count].object_index = object_index;
	/* Wider soft falloff increases useful illumination without clipping colour.
	   Pistol shots start with smaller authored radii than rifle bolts. */
	plasma_lighting_attachments[plasma_lighting_count].radius_scale =
		category ? authored_scale : covenant_scale ? covenant_scale :
		!strcmp(name, "weapons\\plasma pistol\\bolt") ? 2.25f :
		!strcmp(name, "weapons\\plasma rifle\\charged bolt") ? 2.75f : 1.75f;
	plasma_lighting_attachments[plasma_lighting_count].category = category;
	plasma_lighting_count++;
	if (!light_port_refresh_attachment(light_index, object_index))
	{
		/* Disconnected engine attachments stay out of render selection. */
		plasma_lighting_count--;
		light_port_dynamic_set(light_index, object_index,
			plasma_lighting_attachments[plasma_lighting_count].was_dynamic);
	}
}

float plasma_lighting_radius(long light_index, long object_index, float radius)
{
	int index;
	if (!lighting_config_boolean("display.plasma_lighting") && !authored_emitter_lighting_enabled())
		return radius;
	for (index = 0; index < plasma_lighting_count; index++)
		if (plasma_lighting_attachments[index].light_index == light_index &&
			plasma_lighting_attachments[index].object_index == object_index &&
			authored_emitter_lighting_category_enabled(plasma_lighting_attachments[index].category))
			return radius * plasma_lighting_attachments[index].radius_scale;
	return radius;
}

void plasma_lighting_frame_end(void)
{
	int index;

	muzzle_lighting_frame_end();
	covenant_emitter_lighting_frame_end();
	covenant_lighting_frame_end();
	rocket_lighting_frame_end();
	plasma_falloff_end(); /* Also close a pass if drawing ended early. */
	/* Called after render interpolation ends, even if the option was disabled.
	   Restore positions AND cluster membership before the next simulation tick. */
	for (index = 0; index < plasma_lighting_count; index++)
	{
		plasma_lighting_attachments[index].radius_scale = 1.0f;
		light_port_attachment_radius(plasma_lighting_attachments[index].light_index,
			plasma_lighting_attachments[index].object_index,
			&plasma_lighting_attachments[index].saved_radius, TRUE);
		light_port_dynamic_set(plasma_lighting_attachments[index].light_index,
			plasma_lighting_attachments[index].object_index, plasma_lighting_attachments[index].was_dynamic);
		light_port_refresh_attachment(plasma_lighting_attachments[index].light_index,
			plasma_lighting_attachments[index].object_index);
	}
	plasma_lighting_reset();
	if (!lighting_config_boolean("display.plasma_occlusion") && !lighting_config_boolean("display.muzzle_lighting") &&
		!covenant_emitter_lighting_enabled() && !authored_emitter_lighting_enabled())
		plasma_occlusion_reset();
	if (!lighting_config_boolean("display.plasma_lighting") && !lighting_config_boolean("display.muzzle_lighting") &&
		!covenant_emitter_lighting_enabled() && !authored_emitter_lighting_enabled())
		plasma_falloff_dispose();
}

void plasma_lighting_reset(void)
{
	/* No tag pointers or object identities survive unloading or light recovery. */
	plasma_lighting_count = 0;
	plasma_model_lighting_reset();
}

void plasma_lighting_window_begin(void)
{
	int index;
	plasma_occlusion_window();
	unit_light_count = 0;
	muzzle_lighting_window_begin();
	covenant_emitter_lighting_window_begin();
	covenant_lighting_window_begin();
	rocket_lighting_window_begin();
	plasma_model_lighting_reset();
	for (index = 0; index < plasma_lighting_count; index++)
		plasma_lighting_attachments[index].rasterizer_index = NONE;
}

void plasma_lighting_submitted(long light_index, long rasterizer_index)
{
	int index;
	muzzle_lighting_submitted(light_index, rasterizer_index);
	covenant_emitter_lighting_submitted(light_index, rasterizer_index);
	covenant_lighting_submitted(light_index, rasterizer_index);
	rocket_lighting_submitted(light_index, rasterizer_index);
	for (index = 0; index < plasma_lighting_count; index++)
		if (plasma_lighting_attachments[index].light_index == light_index)
		{
			plasma_lighting_attachments[index].rasterizer_index = rasterizer_index;
			plasma_occlusion_submit(light_index, rasterizer_index);
		}
	unit_light_submit(rasterizer_index);
}

int plasma_lighting_selected(long rasterizer_index)
{
	int index;
	if (muzzle_lighting_selected(rasterizer_index) || covenant_emitter_lighting_selected(rasterizer_index))
		return TRUE;
	if (rasterizer_index == NONE || (!lighting_config_boolean("display.plasma_lighting") && !authored_emitter_lighting_enabled()))
		return FALSE;
	if (covenant_lighting_selected(rasterizer_index) || rocket_lighting_selected(rasterizer_index))
		return TRUE;
	for (index = 0; index < plasma_lighting_count; index++)
		if (plasma_lighting_attachments[index].rasterizer_index == rasterizer_index &&
			authored_emitter_lighting_category_enabled(plasma_lighting_attachments[index].category))
			return TRUE;
	return FALSE;
}

int plasma_lighting_prong_cone(long rasterizer, float axis[3], float origin[3], float *radius)
{
	int i;
	struct rasterizer_light_submit_parameters *light;
	if (!lighting_config_boolean("display.plasma_prong_lighting") || rasterizer < 0 || rasterizer >= rasterizer_lights.light_count)
		return FALSE;
	for (i = 0; i < plasma_lighting_count; i++)
		if (plasma_lighting_attachments[i].category == 4 && plasma_lighting_attachments[i].rasterizer_index == rasterizer)
		{
			light = &rasterizer_lights.lights[rasterizer];
			axis[0] = light->forward.i; axis[1] = light->forward.j; axis[2] = light->forward.k;
			origin[0] = light->position.x; origin[1] = light->position.y; origin[2] = light->position.z;
			*radius = light->radius;
			return 3;
		}
	return FALSE;
}

/* Headlights include their own vehicle in the collision field, even when
   the general emitter occlusion option is off. Keep the authored beam cone. */
int plasma_lighting_vehicle_occlusion(long light_index)
{
	int index;
	if (light_index == NONE || !lighting_config_boolean("display.vehicle_lighting"))
		return FALSE;
	for (index = 0; index < plasma_lighting_count; index++)
		if (plasma_lighting_attachments[index].light_index == light_index &&
			plasma_lighting_attachments[index].category == 2)
			return TRUE;
	return FALSE;
}

/* ---------- Chupathingy's way in (object_lights.c, main.c) */

/* the glows, where they are drawn this frame: before the lights are found */
void plasma_lighting_update(void)
{
	muzzle_lighting_update();
	covenant_lighting_update();
	covenant_emitter_lighting_update();
	rocket_lighting_update();
}

/* every light the Light effects hold is let go: the lights array is made
   again (a new map, or lights_port_recover) */
void plasma_lighting_forget(void)
{
	muzzle_lighting_forget();
	covenant_lighting_forget();
	covenant_emitter_lighting_forget();
	rocket_lighting_forget();
	plasma_lighting_reset();
	plasma_falloff_dispose();
}

/* how much further than before an attached light reaches this frame */
static float plasma_lighting_radius_scale(long rasterizer_index)
{
	int index;
	for (index = 0; index < plasma_lighting_count; index++)
		if (plasma_lighting_attachments[index].rasterizer_index == rasterizer_index)
			return plasma_lighting_attachments[index].radius_scale > 0.0f ?
				plasma_lighting_attachments[index].radius_scale : 1.0f;
	return 1.0f;
}

/* a Light effects light's copy for the bipeds (UNIT_LIGHT_SCALE), submitted
   beside it */
static void unit_light_submit(long rasterizer_index)
{
	struct rasterizer_light_submit_parameters copy;
	if (rasterizer_index < 0 || rasterizer_index >= rasterizer_lights.light_count ||
		unit_light_count >= UNIT_LIGHTS || !plasma_lighting_selected(rasterizer_index))
		return;
	copy = rasterizer_lights.lights[rasterizer_index];
	copy.color.red *= UNIT_LIGHT_SCALE;
	copy.color.green *= UNIT_LIGHT_SCALE;
	copy.color.blue *= UNIT_LIGHT_SCALE;
	copy.radius /= plasma_lighting_radius_scale(rasterizer_index);
	unit_lights[unit_light_count].rasterizer_index = rasterizer_index;
	unit_lights[unit_light_count].copy_index = rasterizer_light_submit(&copy);
	unit_light_count++;
}

/* the rasterizer light a biped centered there is lit by in this one's place:
   itself for the game's own lights, the copy for a Light effects light, or
   NONE where the level's walls are between them */
long plasma_lighting_unit_light(long rasterizer_index, real_point3d const *center)
{
	int index;
	for (index = 0; index < unit_light_count; index++)
	{
		struct collision_result collision;
		real_point3d const *position;
		real_vector3d vector;
		if (unit_lights[index].rasterizer_index != rasterizer_index)
			continue;
		position = &rasterizer_lights.lights[rasterizer_index].position;
		vector.i = center->x - position->x;
		vector.j = center->y - position->y;
		vector.k = center->z - position->z;
		if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
			position, &vector, NONE, &collision) &&
			collision.t < 0.95f)
			return NONE;
		return unit_lights[index].copy_index;
	}
	/* (a Light effects light with no copy, past UNIT_LIGHTS: none at all) */
	return plasma_lighting_selected(rasterizer_index) ? NONE : rasterizer_index;
}
#endif
