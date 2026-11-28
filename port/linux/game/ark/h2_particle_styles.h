/*
H2_PARTICLE_STYLES.H

(port) Halo 2's particles' looks over their lives, which this engine's
particle tags have no fields for: their size, alpha and colour sampled at
H2_PARTICLE_STYLE_SAMPLES points of their lives, kept in the particle tag's
reserved bytes (a 'h2fx' mark at 0x6C - no Halo CE tag has one, so every CE
particle draws exactly as before). Written by the Halo 2 level builder
(h2_effects.c), read by the renderer (render_particles.c) for each particle
it draws. Only drawing: nothing the game state, the network or a save
sees.

The quality tier (HALO_H2_EFFECTS: "full", "capped", "off"; full on every
platform, the Vita too - capped is a measurement's only): capped draws a Halo 2 particle no bigger than
H2_PARTICLE_STYLE_CAPPED_SCREEN of the screen's height across (the
overdraw of a big smoke cloud close up) and the level builder caps each
emitter's count and its sprite sheets' size (h2_effects.c).
*/

#ifndef __H2_PARTICLE_STYLES_H
#define __H2_PARTICLE_STYLES_H
#pragma once

/* ---------- constants */

enum
{
	H2_PARTICLE_STYLE_SAMPLES = 8,

	H2_PARTICLE_STYLE_SCALE = 1 << 0,     /* its size over its life */
	H2_PARTICLE_STYLE_ALPHA = 1 << 1,     /* its alpha over its life */
	H2_PARTICLE_STYLE_COLOR = 1 << 2,     /* its colour over its life */
	H2_PARTICLE_STYLE_MOTION = 1 << 3,    /* Halo 2's particle physics: gravity and drag (not the point physics') */
	H2_PARTICLE_STYLE_TINT = 1 << 4,      /* Halo 2's additive tint: "white stays white" - its colour drawn toward white by the weight */
	H2_PARTICLE_STYLE_VERTICAL = 1 << 5,  /* its bitmap authored vertically: drawn along its direction by its height (an oriented one) */

	H2_PARTICLE_STYLE_TIER_FULL = 0,
	H2_PARTICLE_STYLE_TIER_CAPPED,

	/* an effect's particles entry made from a Halo 2 emitter (h2_effects.c)
	carries the emitter's emission shape at its unused06a and this mark at
	its unused078[0]: effects.c places the particle by the shape */
	H2_EFFECT_SHAPE_MARK = ('h' << 24) | ('2' << 16) | ('s' << 8) | 'h',
	H2_EFFECT_ACCELERATION_MARK = ('h' << 24) | ('2' << 16) | ('a' << 8) | 'c',   /* (an event's unused018: h2_effects.c, effects.c) */
	H2_EMISSION_SHAPE_SPRAYER = 0,
	H2_EMISSION_SHAPE_DISC = 1,
	H2_EMISSION_SHAPE_GLOBE = 2,
	H2_EMISSION_SHAPE_IMPLODE = 3,
	H2_EMISSION_SHAPE_TUBE = 4,
	H2_EMISSION_SHAPE_HALO = 5,
	H2_EMISSION_SHAPE_IMPACT_CONTOUR = 6,
	H2_EMISSION_SHAPE_IMPACT_AREA = 7,
	H2_EMISSION_SHAPE_DEBRIS = 8,
	H2_EMISSION_SHAPE_LINE = 9,
	H2_EMISSION_SHAPE_CUSTOM_POINTS = 10,
};

/* (capped: the largest a Halo 2 particle is drawn, a fraction of the
screen's height) */
#define H2_PARTICLE_STYLE_CAPPED_SCREEN 0.6f

/* ---------- structures */

struct h2_particle_style
{
	unsigned long flags;
	float scale[H2_PARTICLE_STYLE_SAMPLES];     /* of its radius, 0..1 */
	float alpha[H2_PARTICLE_STYLE_SAMPLES];     /* 0..1 */
	float color[H2_PARTICLE_STYLE_SAMPLES][3];  /* 0..1, times its tint */
	float gravity;                              /* (motion) of the world's: 1 falls, -0.3 rises */
	float drag;                                 /* (motion) its velocity's loss a second */
	float tint_white;                           /* (tint) how far its colour goes toward white, 0..1 */
	float sort_bias;                            /* Halo 2's particle system's sort bias (-10..10: negative nearer), world units */
};

/* a Halo 2 particle's attached particle system (its prt3's "attached
particle systems": the rocket's fallout sparks' flint, debris' smoke
trails), made a particle tag of its own by h2_effects.c and kept in the
parent's tag: particles.c emits it from each live particle at rate a tick,
flying from the particle along its velocity within cone at velocity[0..1]
world units a second, radius[0..1] across */
struct h2_particle_child
{
	long definition_index;      /* the child's particle tag, NONE */
	float rate;                 /* children a tick (fractions accumulate) */
	float velocity[2];
	float cone;                 /* radians, the half angle */
	float radius[2];
};

/* ---------- prototypes */

/* the child written into a parent's tag (after its style); read back:
whether it has one, and its values */
void h2_particle_style_child_write(void *definition, struct h2_particle_child const *child);
int h2_particle_style_child(void const *definition, struct h2_particle_child *child);
/* (perf/ce-floor-2) a child was ever written into a definition (a made Halo
2 level's): until then no definition has one - particles.c asks this before
its per-particle child test (a lookup, a definition and this call each) */
extern int h2_particle_children_written;
/* (perf/ce-floor-2) a style (its mark) was ever written into a definition:
until then none has one - particles.c asks this before its per-particle
motion call */
extern int h2_particle_styles_written;

/* a Halo 2 particle model's mesh (its tag's reserved34 points at it, the
level builder's h2_effects.c): drawn for the particle in place of its
sprite (render_sprite.c's build_sprite_mesh). After the header: its models
(index start and count, a short each: a particle draws one), its vertices
(x, y, z, u, v: floats; u and v of its sprite's bounds), its indices
(shorts, a triangle list) */
struct h2_particle_mesh
{
	unsigned short model_count;
	unsigned short vertex_count;
	unsigned short index_count;
	unsigned short pad;
};

/* the particle tag's mesh, NULL none (a sprite) */
struct h2_particle_mesh const *h2_particle_style_mesh(void const *definition);

/* the style packed into a particle tag's reserved bytes (definition: a
struct particle_definition, 0x164 bytes) */
void h2_particle_style_write(void *definition, struct h2_particle_style const *style);

/* whether the particle tag has a style; then its scale, fade and colour
for a particle at age_fraction (0..1) of its life: scale and fade
multiplied, colour (in, the particle's) multiplied. pixels: its drawn
diameter in pixels (the cap's), screen_height the view's */
int h2_particle_style_apply(void const *definition, float age_fraction, float *scale, float *fade,
	float color_rgb[3], float pixels, float screen_height);

/* whether a Halo 2 particle's sprite is authored vertically: an oriented
one (parallel or perpendicular to its direction) is turned a quarter, its
height along the direction (render_particles.c) */
int h2_particle_style_vertical(void const *definition);

/* a Halo 2 particle's sort bias (its particle system's: render_particles.c
draws its group as if that much farther from the camera, a negative one
nearer - an explosion's fire over its own smoke); 0 none or not Halo 2's */
float h2_particle_style_sort_bias(void const *definition);

/* (particles.c, a free particle's physics) a Halo 2 particle's own
movement: its velocity (world units a second) changed for dt seconds;
FALSE for a particle without (this engine's point physics alone) */
int h2_particle_style_motion(void const *definition, float velocity[3], float dt);

/* the quality tier (HALO_H2_EFFECTS; the platform's) */
short h2_particle_style_tier(void);

/* (HALO_H2_EFFECTS_SIZE, tests) Halo 2's sizes times this, 1 */
float h2_particle_style_size_scale(void);

#endif /* __H2_PARTICLE_STYLES_H */
