/* h2_weather_plate.h: a Halo 2 level's weather plates, the one draw between
source/effects/weather_particle_systems.c and the PC's GL (d3d8_gl.c). NULL on
the Vita: no plates there */
#ifndef H2_WEATHER_PLATE_H
#define H2_WEATHER_PLATE_H

struct halo_weather_plate_parameters
{
	float map_offset[3][2];     /* (u, v at the view's top left) */
	float map_scale[3];         /* (repeats across the view, each axis) */
	float opacity[3];
	float tint[3];
	float blend_near, blend_far;
	float z_near, z_far;
	int viewport[4];            /* (x, y, width, height in the frame) */
	int debug;
};

extern void (*halo_weather_plate_pass)(struct halo_weather_plate_parameters const *parameters);

#endif
