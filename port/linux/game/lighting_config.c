#ifndef HALO_CONSOLE_NO_LIGHT_EFFECTS /* (a console draws no Light effects: halo_port_features.h) */
/* display.light_effects: the Light effects' level, and the parts of them each
level turns on. "auto" is "high" where the machine can draw it, "low" on an older Intel HD
part and "off" on a software renderer or the older drivers' OpenGL 3.3 path. OpenGL ES, Android, the consoles and the server never draw
them. "low" keeps the projectile and muzzle-flash lights (with their walls),
"high" adds the emitters and the held weapons' glow. The parts' names are
chester's from GulchCE, where each was a setting of its own.

The level is read again only when a setting changes (config_changes): a light
or a draw asks for its parts many times a frame. */
#include "lighting_config.h"
#include <string.h>

int config_boolean(const char *name);
const char *config_string(const char *name);
unsigned long config_changes(void);

/* the renderer draws in software or on the older drivers' OpenGL 3.3 path
(d3d8_gl.c): "auto" is off there; on an older Intel HD part it is "low" */
int xgpu_software_renderer, xgpu_weak_renderer;

enum
{
	_level_off,
	_level_low,
	_level_high,
};

static const struct
{
	const char *name;
	int level;
} parts[] = {
	{ "display.plasma_lighting", _level_low },
	{ "display.muzzle_lighting", _level_low },
	{ "display.plasma_occlusion", _level_low },
	{ "display.ammo_counter_lighting", _level_high },
	{ "display.needler_lighting", _level_high },
	{ "display.jackal_shield_lighting", _level_high },
	{ "display.covenant_shield_lighting", _level_high },
	{ "display.fuel_rod_lighting", _level_high },
	{ "display.weapon_glow", _level_high },
	{ "display.plasma_prong_lighting", _level_high },
	{ "display.vehicle_lighting", _level_high },
	{ "display.world_lighting", _level_high },
};

static int level_read(void)
{
#if defined(HALO_GL_ES) || defined(HALO_ANDROID) || defined(HALO_CONSOLE) || defined(HALO_XBOX_CONSOLE) || \
	defined(HALO_CONSOLE_GL_LAYER) || defined(HALO_SERVER)
	return _level_off;
#else
	const char *level = config_string("display.light_effects");

	if (!strcmp(level, "high"))
		return _level_high;
	if (!strcmp(level, "low"))
		return _level_low;
	if (!strcmp(level, "off"))
		return _level_off;
	/* (auto, or a word the game doesn't know) */
	return xgpu_software_renderer ? _level_off : xgpu_weak_renderer ? _level_low : _level_high;
#endif
}

int lighting_config_level(void)
{
	static unsigned long changes;
	static int ready, level, software;

	if (!ready || changes != config_changes() || software != xgpu_software_renderer * 2 + xgpu_weak_renderer)
	{
		changes = config_changes();
		software = xgpu_software_renderer * 2 + xgpu_weak_renderer;
		level = level_read();
		ready = 1;
	}
	return level;
}

int lighting_config_boolean(const char *name)
{
	int level = lighting_config_level();
	unsigned int i;

	for (i = 0; i < sizeof(parts) / sizeof(parts[0]); i++)
	{
		if (!strcmp(name, parts[i].name))
			return level != _level_off && level >= parts[i].level;
	}
	return config_boolean(name);
}
#endif
