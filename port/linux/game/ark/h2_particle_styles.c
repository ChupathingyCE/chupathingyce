/*
H2_PARTICLE_STYLES.C

(port) Halo 2's particles' size, alpha and colour over their lives, in
their tags' reserved bytes: h2_particle_styles.h. The layout (offsets of
the particle tag, struct particle_definition):
0x34  a particle model's mesh's address (h2_particle_mesh), 0 a sprite
0x6C  'h2fx'
0x70  flags (H2_PARTICLE_STYLE_*), a byte; the tint's weight, a byte; the
      sort bias, a signed byte (world units); a zero byte
0xA0  scale samples, a byte each (255: 1)        (reservedA0[2])
0xE0  colour samples, red, green, blue a byte    (its shader's reserved bytes before the secondary map)
0x150 alpha samples, a byte each                 (its shader's reserved bytes after the zsprite scale)
*/

#include "h2_particle_styles.h"
#include "render_epoch.h"

#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	STYLE_MARK = 0x6C,
	STYLE_FLAGS = 0x70,
	STYLE_SCALE = 0xA0,
	STYLE_COLOR = 0xE0,
	STYLE_ALPHA = 0x150,
	STYLE_GRAVITY = 0x7C,   /* (reserved7C) */
	STYLE_DRAG = 0x94,      /* (reserved94) */
	STYLE_MESH = 0x34,      /* (reserved34) a particle model's mesh's address, 0 none */
	/* an attached particle system (h2_particle_child): the child's tag
	index after the colour samples; its rate a tick after the distortion
	scale; its velocity (a twentieth of a world unit a second, 0..12.75),
	the lower bound's fraction of it, its cone (of a half turn) and its
	radius (hundredths of a world unit, 0..2.55) bytes; the radius's lower
	bound's fraction of it in the style's spare byte */
	STYLE_CHILD = 0xF8,
	STYLE_CHILD_RATE = 0x15C,
	STYLE_CHILD_BYTES = 0x160,
	STYLE_CHILD_RADIUS_RATIO = 0x73,
	STYLE_MARK_VALUE = ('h' << 24) | ('2' << 16) | ('f' << 8) | 'x',
};

/* (the desktop's d3d8_gl.c sets it: a draw's fragment program of the
caller's own - rasterizer_xbox_transparent_geometry.c's Halo 2 distortion
particles; NULL elsewhere, and they are not drawn) */
void (*halo_custom_fragment_program)(const char *source, const float *uniforms, int vec4_count) = NULL;

/* ---------- private code */

static unsigned char style_byte(float value)
{
	return (unsigned char)(value <= 0.0f ? 0 : value >= 1.0f ? 255 : (int)(value * 255.0f + 0.5f));
}

/* a sample's value at fraction (0..1) of the samples, linear between */
static float style_sample(unsigned char const *samples, int stride, float fraction, int channel)
{
	float at = fraction * (float)(H2_PARTICLE_STYLE_SAMPLES - 1);
	int k;
	float a, b;

	if (!(at > 0.0f))
		at = 0.0f;
	if (at >= (float)(H2_PARTICLE_STYLE_SAMPLES - 1))
		at = (float)(H2_PARTICLE_STYLE_SAMPLES - 1) - 0.0001f;
	k = (int)at;
	a = samples[k * stride + channel] / 255.0f;
	b = samples[(k + 1) * stride + channel] / 255.0f;
	return a + (b - a) * (at - (float)k);
}

/* ---------- public code */

/* (perf/ce-floor-2, h2_particle_styles.h) */
int h2_particle_children_written;
int h2_particle_styles_written;

void h2_particle_style_write(void *definition, struct h2_particle_style const *style)
{
	unsigned char *p = (unsigned char *)definition;
	unsigned long mark = STYLE_MARK_VALUE;
	int s, c;

	h2_particle_styles_written = 1;
	memcpy(p + STYLE_MARK, &mark, 4);
	p[STYLE_FLAGS] = (unsigned char)style->flags;
	p[STYLE_FLAGS + 1] = (style->flags & H2_PARTICLE_STYLE_TINT) ? style_byte(style->tint_white) : 0;
	{
		float bias = style->sort_bias < -127.0f ? -127.0f : style->sort_bias > 127.0f ? 127.0f : style->sort_bias;

		p[STYLE_FLAGS + 2] = (unsigned char)(signed char)(bias < 0.0f ? bias - 0.5f : bias + 0.5f);
	}
	p[STYLE_FLAGS + 3] = 0;
	for (s = 0; s < H2_PARTICLE_STYLE_SAMPLES; s++)
	{
		p[STYLE_SCALE + s] = style_byte(style->scale[s]);
		p[STYLE_ALPHA + s] = style_byte(style->alpha[s]);
		for (c = 0; c < 3; c++)
			p[STYLE_COLOR + 3 * s + c] = style_byte(style->color[s][c]);
	}
	memcpy(p + STYLE_GRAVITY, &style->gravity, 4);
	memcpy(p + STYLE_DRAG, &style->drag, 4);
}

void h2_particle_style_child_write(void *definition, struct h2_particle_child const *child)
{
	unsigned char *p = (unsigned char *)definition;
	unsigned long index = (unsigned long)child->definition_index;
	float v1 = child->velocity[1] < 0.0f ? 0.0f : child->velocity[1], r1 = child->radius[1] < 0.0f ? 0.0f : child->radius[1];

	h2_particle_children_written = 1;

	memcpy(p + STYLE_CHILD, &index, 4);
	memcpy(p + STYLE_CHILD_RATE, &child->rate, 4);
	p[STYLE_CHILD_BYTES] = (unsigned char)(v1 * 20.0f > 255.0f ? 255 : (int)(v1 * 20.0f + 0.5f));
	p[STYLE_CHILD_BYTES + 1] = style_byte(v1 > 0.0f ? child->velocity[0] / v1 : 1.0f);
	p[STYLE_CHILD_BYTES + 2] = style_byte(child->cone / 3.14159265f);
	p[STYLE_CHILD_BYTES + 3] = (unsigned char)(r1 * 100.0f > 255.0f ? 255 : (int)(r1 * 100.0f + 0.5f));
	p[STYLE_CHILD_RADIUS_RATIO] = style_byte(r1 > 0.0f ? child->radius[0] / r1 : 1.0f);
}

int h2_particle_style_child(void const *definition, struct h2_particle_child *child)
{
	unsigned char const *p = (unsigned char const *)definition;
	unsigned long mark, index;

	memcpy(&mark, p + STYLE_MARK, 4);
	if (mark != STYLE_MARK_VALUE)
		return 0;
	memcpy(&index, p + STYLE_CHILD, 4);
	if (index == 0 || index == 0xFFFFFFFFUL)
		return 0;
	child->definition_index = (long)index;
	memcpy(&child->rate, p + STYLE_CHILD_RATE, 4);
	child->velocity[1] = p[STYLE_CHILD_BYTES] / 20.0f;
	child->velocity[0] = child->velocity[1] * p[STYLE_CHILD_BYTES + 1] / 255.0f;
	child->cone = p[STYLE_CHILD_BYTES + 2] / 255.0f * 3.14159265f;
	child->radius[1] = p[STYLE_CHILD_BYTES + 3] / 100.0f;
	child->radius[0] = child->radius[1] * p[STYLE_CHILD_RADIUS_RATIO] / 255.0f;
	return child->rate > 0.0f && child->radius[1] > 0.0f;
}

int h2_particle_style_motion(void const *definition, float velocity[3], float dt)
{
	unsigned char const *p = (unsigned char const *)definition;
	unsigned long mark;
	float gravity, drag, keep;

	memcpy(&mark, p + STYLE_MARK, 4);
	if (mark != STYLE_MARK_VALUE || !(p[STYLE_FLAGS] & H2_PARTICLE_STYLE_MOTION))
		return 0;
	memcpy(&gravity, p + STYLE_GRAVITY, 4);
	memcpy(&drag, p + STYLE_DRAG, 4);
	/* (the world's gravity, this engine's: 0.0035651792 a tick squared) */
	velocity[2] -= gravity * 3.2086613f * dt;
	keep = 1.0f - drag * dt;
	if (keep < 0.0f)
		keep = 0.0f;
	if (keep < 1.0f)
	{
		velocity[0] *= keep;
		velocity[1] *= keep;
		velocity[2] *= keep;
	}
	return 1;
}

int h2_particle_style_vertical(void const *definition)
{
	unsigned char const *p = (unsigned char const *)definition;
	unsigned long mark;

	memcpy(&mark, p + STYLE_MARK, 4);
	return mark == STYLE_MARK_VALUE && (p[STYLE_FLAGS] & H2_PARTICLE_STYLE_VERTICAL) && !HALO_ENV_ONCE("HALO_H2_NO_VERTICAL");
}

float h2_particle_style_sort_bias(void const *definition)
{
	unsigned char const *p = (unsigned char const *)definition;
	unsigned long mark;
	static int disabled = -1;

	memcpy(&mark, p + STYLE_MARK, 4);
	if (mark != STYLE_MARK_VALUE)
		return 0.0f;
	/* (HALO_H2_NO_SORT_BIAS: the groups sorted by their centroids alone, as before) */
	if (disabled < 0)
		disabled = getenv("HALO_H2_NO_SORT_BIAS") != NULL;
	return disabled ? 0.0f : (float)(signed char)p[STYLE_FLAGS + 2];
}

struct h2_particle_mesh const *h2_particle_style_mesh(void const *definition)
{
	unsigned char const *p = (unsigned char const *)definition;
	unsigned long mark, mesh;

	memcpy(&mark, p + STYLE_MARK, 4);
	if (mark != STYLE_MARK_VALUE)
		return NULL;
	memcpy(&mesh, p + STYLE_MESH, 4);
	return (struct h2_particle_mesh const *)(unsigned long)mesh;
}

int h2_particle_style_apply(void const *definition, float age_fraction, float *scale, float *fade,
	float color_rgb[3], float pixels, float screen_height)
{
	unsigned char const *p = (unsigned char const *)definition;
	unsigned long mark;
	unsigned char flags;

	static int disabled = -1;

	memcpy(&mark, p + STYLE_MARK, 4);
	if (mark != STYLE_MARK_VALUE)
		return 0;
	/* (HALO_H2_NO_STYLES: as this engine draws them, tests) */
	if (disabled < 0)
		disabled = getenv("HALO_H2_NO_STYLES") != NULL;
	if (disabled)
		return 0;
	flags = p[STYLE_FLAGS];
	if (!(age_fraction >= 0.0f))
		age_fraction = 0.0f;
	if (age_fraction > 1.0f)
		age_fraction = 1.0f;
	if (flags & H2_PARTICLE_STYLE_SCALE)
		*scale *= style_sample(p + STYLE_SCALE, 1, age_fraction, 0);
	if (flags & H2_PARTICLE_STYLE_ALPHA)
		*fade *= style_sample(p + STYLE_ALPHA, 1, age_fraction, 0);
	if (flags & H2_PARTICLE_STYLE_COLOR)
	{
		int c;

		for (c = 0; c < 3; c++)
			color_rgb[c] *= style_sample(p + STYLE_COLOR, 3, age_fraction, c);
	}
	/* (Halo 2's particle_additive_tint: its pass draws the texel T times
	the colour D plus T^4 times 1 - D - a bright texel white whatever the
	colour, a dim one tinted. One colour a sprite here: D drawn toward
	white by a weight standing for T^3 over the sprite's bright part) */
	if (flags & H2_PARTICLE_STYLE_TINT)
	{
		float w = p[STYLE_FLAGS + 1] / 255.0f;
		int c;

		for (c = 0; c < 3; c++)
			color_rgb[c] += (1.0f - color_rgb[c]) * w;
	}
	/* (capped: no bigger than a share of the screen - the overdraw) */
	if (h2_particle_style_tier() == H2_PARTICLE_STYLE_TIER_CAPPED && screen_height > 0.0f && pixels > 0.0f)
	{
		float limit = H2_PARTICLE_STYLE_CAPPED_SCREEN * screen_height;
		float drawn = pixels * ((flags & H2_PARTICLE_STYLE_SCALE) ? style_sample(p + STYLE_SCALE, 1, age_fraction, 0) : 1.0f);

		if (drawn > limit)
			*scale *= limit / drawn;
	}
	return 1;
}

short h2_particle_style_tier(void)
{
	static short tier = -1;

	if (tier < 0)
	{
		char const *setting = getenv("HALO_H2_EFFECTS");

		/* (every platform full: Halo 2's particles as Halo 2 draws them -
		the Vita's capped tier (no particle over 0.6 of the screen's
		height, every emitter's count halved, no particle models,
		distortion, weather plates or debris) made the plasma rifles' and
		the plasma pistol's flashes small and thin, insider.5 tester; the
		owner's rule: accuracy before the frame rate. HALO_H2_EFFECTS=capped
		keeps the old tier for measurements) */
		tier = H2_PARTICLE_STYLE_TIER_FULL;
		if (setting && !strcmp(setting, "capped"))
			tier = H2_PARTICLE_STYLE_TIER_CAPPED;
		else if (setting && !strcmp(setting, "full"))
			tier = H2_PARTICLE_STYLE_TIER_FULL;
	}
	return tier;
}

float h2_particle_style_size_scale(void)
{
	char const *setting = HALO_ENV_ONCE("HALO_H2_EFFECTS_SIZE");
	float scale = setting ? (float)atof(setting) : 1.0f;

	return scale > 0.0f ? scale : 1.0f;
}
