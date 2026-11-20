/*
H3_OBJECT_LIGHT.H

A Halo 3 level's objects lit by Halo 3's own light probes (h3_object_light.c):
the scenario lightmap's scenery probes and air probes, and the level's
lightprobe texture under an object. Desktop only: stubs in the Vita and
gxm-null builds.
*/

#ifndef __H3_OBJECT_LIGHT_H
#define __H3_OBJECT_LIGHT_H

struct render_lighting;

enum
{
	_h3_object_light_probe_scenery,
	_h3_object_light_probe_air,
	_h3_object_light_probe_machine,
	_h3_object_light_probe_instance   /* (an instance's per-vertex lighting's mean: four coefficients and the dominant light's intensity) */
};

/* (h3_level.c, a level's make) the probes of the level made: forgotten, then
each added - its place (a scenery probe's: its placement's), its dominant
light's direction and intensity and its nine coefficients a channel
(terms[coefficient][channel]), linear light - and the exposure they are
drawn at (the lightmap's bake exposure) */
void h3_object_light_begin(void);
void h3_object_light_probe_add(int kind, float const position[3], float const direction[3], float const intensity[3],
	float const terms[9][3]);
void h3_object_light_end(float exposure, short lightprobe_atlas);

/* (h3_level.c) the level's lightprobe texture's coefficients at a lightmap
coordinate of its atlas page: FALSE none kept */
boolean h3_level_lightprobe_at(float u, float v, float sh[4][3], float intensity[3]);

/* (object_lights.c, an object's static lighting worked out on a Halo 3
level) its ambient and distant lights from Halo 3's probes: sampled - the
ray under it met a lightmapped surface, of the lightmap bitmap and
coordinate given. FALSE: left as it was */
boolean h3_object_light(long object_index, struct render_lighting *lighting, boolean sampled, short lightmap_bitmap,
	float u, float v);

#endif
