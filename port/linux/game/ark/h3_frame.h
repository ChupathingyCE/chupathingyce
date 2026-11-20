/*
H3_FRAME.H

A Halo 3 level's frame (h3_frame.c): what Halo 3 does to every frame after
its surfaces are lit - its atmosphere between the eye and each surface, its
exposure, its bloom and its tone curve -, the one pass between the game's
frame and the PC's GL (d3d8_gl.c). NULL on the Vita and the null build: no
pass there, the level drawn as before.
*/
#ifndef H3_FRAME_H
#define H3_FRAME_H

/* Halo 3's atmosphere setting as its compute_scattering reads it
(atmosphere.fx; h3_level.c's struct atmosphere) */
struct halo_h3_frame_atmosphere
{
	int on;
	float sun[3], sun_intensity[3];
	float rayleigh[3], mie[3], rayleigh_theta[3], mie_theta[3];
	float sea_level, rayleigh_height, mie_height, g, distance_bias, maximum_thickness;
};

struct halo_h3_frame_parameters
{
	int viewport[4];                /* (x, y, width, height in the frame) */
	float camera[3];                /* the eye, world */
	float corner[4][3];             /* the world vector a unit of view depth: the view's left bottom, right bottom, left top, right top */
	float z_near, z_far;
	struct halo_h3_frame_atmosphere level, sky;
	int sky_inscatter;              /* the sky's pixels drawn as the sky setting's inscatter alone (no sky model: Guardian) */
	float bake_exposure;            /* the exposure the frame's light was made at (the lightmap's) */
	float sky_bake_exposure;        /* the exposure the sky's pixels were made at */
	float exposure;                 /* the exposure this frame is shown at */
	float bloom_point, bloom_inherent, bloom_intensity;
	float bloom_colors[3][3];       /* small, medium, large */
	float tone[4];                  /* the tone curve: its white (the clamp), linear, quadratic, cubic */
	float display_power;            /* the display's encode: the frame's light to this power */
	float meter_center;             /* the exposure meter's weight toward the view's middle (0 even) */
	int debug;
};

/* the pass; measured: the frame's weighted mean log2 intensity and log2 of
its mean intensity, as shown (at parameters->exposure), before its bloom */
extern void (*halo_h3_frame_pass)(struct halo_h3_frame_parameters const *parameters, float measured[2]);
/* the frame's target holds more than white (a float target) while a Halo 3
level's frame pass is on: its sun-lit surfaces past 1 are the bloom's */
extern void (*halo_h3_frame_float)(int on);

/* (render.c, after the frame's 3D and before its interface) */
void h3_frame_render(void);
/* whether the pass draws the level's atmosphere (h3_level.c: then the made
level has no fog of this engine's) */
int h3_frame_atmosphere_enabled(void);
/* whether the pass is on at all (HALO_H3_FRAME=0: off, the frame as before) */
int h3_frame_enabled(void);

/* (h3_level.c) the made level's values for the pass */
enum { H3_FRAME_SETTINGS = 16, H3_FRAME_CLUSTERS = 512 };

struct h3_frame_level
{
	int valid;
	struct halo_h3_frame_atmosphere level, sky;
	/* every atmosphere setting of the level and the one each cluster is drawn through (-1 the level's) */
	int setting_count;
	struct halo_h3_frame_atmosphere settings[H3_FRAME_SETTINGS];
	char setting_names[H3_FRAME_SETTINGS][32];
	signed char cluster_setting[H3_FRAME_CLUSTERS];
	int sky_inscatter;
	float bake_exposure, sky_bake_exposure;
	/* the level's camera fx (cfxs) */
	float exposure_stops, exposure_minimum, exposure_maximum;   /* stops */
	float exposure_maximum_change, exposure_blend_speed;
	float exposure_target;          /* the "auto-exposure screen brightness", log2 */
	float exposure_delay;
	int exposure_flags;
	float sensitivity, anti_bloom;
	float bloom_point, bloom_inherent, bloom_intensity;
	float bloom_colors[3][3];       /* small, medium, large */
};
struct h3_frame_level *h3_frame_level(void);

#endif
