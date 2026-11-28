/*
H2_SHADOWS.H

Objects' shadows on a converted Halo 2 level (h2_level.c builds the level).

This engine's own shadows (the Xbox's): each object big enough on screen is
drawn from its shadow direction into a small texture (render_objects.c
render_object_shadow_begin, rasterizer_xbox_shadows.c), which is then laid
over the level's surfaces under it (structure_render_shadow), darkening
them by the object's shadow colour - its lighting's: the lightmap's light
under it, its incident direction (the sun's side, in sun) the shadow's.

On a Halo 2 level none were drawn, for two reasons:
- the surfaces under a small object were looked for through the collision
  leaves' surface references, and a converted level's leaves have none
  (structure_visibility.c now looks through the clusters there);
- Halo 2's object shaders are converted as model shaders that are not
  alpha tested, whose shadow draw (rasterizer_xbox_shadows.c) reads no
  texture, its alpha 0: the alpha test the shadow texture keeps dropped
  every pixel (here the test is off for them).

Tiers (HALO_H2_SHADOWS, config.toml's graphics.h2_shadows; live):
- off: no objects' shadows on a Halo 2 level;
- cheap: this engine's shadows as the Xbox draws them (the shadow's
  direction the lightmap's incident one, at least 45 degrees up, softened
  once): the Vita's, when it takes them;
- full (the desktop's default): as Halo 2 casts them - from the level's
  sun (its direction found once a level, the lightmap's incident
  directions averaged where they are most directional), as long as the
  sun's height makes them, softer and darker in sun, fading in shade.
The Vita, its null build and Android default to off.
*/

#ifndef __H2_SHADOWS_H
#define __H2_SHADOWS_H

#include "cseries.h"

enum
{
	_h2_shadows_off,
	_h2_shadows_cheap,
	_h2_shadows_full,
	NUMBER_OF_H2_SHADOWS_TIERS
};

/* the tier the setting asks for (whatever the level) */
short h2_shadows_tier(void);

/* whether the level loaded is a converted Halo 2 level */
boolean h2_shadows_level(void);

/* the tier in force: off on a Halo CE map (its shadows its own) */
short h2_shadows_active(void);

/* (render_objects.c, full tier) an object's shadow: its direction (unit,
down) and its colour, given its lighting's (shadow_vector, shadow_color:
the lightmap's light under it); FALSE: left as they are */
boolean h2_shadows_object(float shadow_vector[3], float shadow_color[3]);

/* (h2_level.c, as a level is built) the level's sun from its Halo 2 map:
its scenario's first sky's first light's direction (yaw and pitch, in
radians: Zanzibar's 168 and 36 degrees); FALSE if it has none */
struct h2_cache;
boolean h2_shadows_sun_from_cache(struct h2_cache *cache, float *yaw, float *pitch);
boolean h2_shadows_sun_get(real_vector3d *toward);

/* the level cache's header words the sun is kept in (h2_level.c writes
them; a level made before they were is given them from its map once, by
h2_shadows_level_resolved) */
enum
{
	H2_SHADOWS_HEADER_SUN = 0x720,  /* 'sun ' (yaw, pitch follow) or 'none' */
	H2_SHADOWS_HEADER_YAW = 0x724,
	H2_SHADOWS_HEADER_PITCH = 0x728,
	/* (0x730: the tag cache room the level needs, 0x734 its sceneries' model
	detail - h2_level.c) */
};

/* (h2_level.c, as a level is resolved for loading - a level file or one with
none) the level's sun, read from its MCC map's sky (level_path, the level
file, is not read: a level with no file passes NULL) */
void h2_shadows_level_resolved(char const *maps_directory, char const *name, char const *level_path);

/* (rasterizer_xbox_shadows.c, full tier) the softening's width, a scale
of its own (HALO_H2_SHADOW_SOFTNESS, 1 the default) */
float h2_shadows_softness(void);

/* (full tier) how much darker than this engine's a shadow is: its colour's
darkening scaled (HALO_H2_SHADOW_DARKNESS, 1.35 the default) */
float h2_shadows_darkness(void);


#endif
