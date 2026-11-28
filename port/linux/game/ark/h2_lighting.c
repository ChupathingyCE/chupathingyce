/*
H2_LIGHTING.C

Halo 2's lighting on a converted Halo 2 level (h2_lighting.h).
*/

/* ---------- headers */

#include "cseries.h"
#include "h2_lighting.h"
#include "h2_cache.h"
#include "math/real_math.h"
#include "scenario/scenario.h"
#include "tag_files/tag_files.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HALO_LINUX
#include "render_epoch.h" /* HALO_ENV_ONCE */
#endif

/* (render_epoch.c: changes with every map and game state replaced) */
extern unsigned long halo_map_generation;

/* (the C library's allocator, as h2_level.c's: what this file allocates
h2_level.c frees) */
#undef malloc
#undef free
#undef realloc

void platform_log(char const *format, ...);

/* ---------- globals */

unsigned char halo_blend_alpha_single;

static struct
{
	char level[64];
	unsigned long build_key;
	long scenario_index;      /* the scenario last looked at (NONE: none) */
	boolean overbright;
} h2_lighting_globals = { "", 0, NONE, FALSE };

/* ---------- private code */

static float setting_real(char const *name, float fallback)
{
	char const *value = getenv(name);

	return value && *value ? (float)atof(value) : fallback;
}

/* ---------- public code */

short h2_lighting_tier(void)
{
	char const *setting = HALO_ENV_ONCE("HALO_H2_LIGHTING");

	if (setting && !strcmp(setting, "classic"))
		return _h2_lighting_tier_classic;
	if (setting && !strcmp(setting, "h2"))
		return _h2_lighting_tier_h2;
	/* (the overbright pass keeps its specular mask - its alpha multiplied
	once - on the GL renderer and on the Vita's GXM (d3d8_gxm.c
	blend_alpha_single, its null build's too); Android's GLES takes classic
	until its does and is seen) */
#if defined(HALO_ANDROID)
	return _h2_lighting_tier_classic;
#else
	return _h2_lighting_tier_h2;
#endif
}

unsigned long h2_lighting_build_key(void)
{
	/* (the tier, the atlas's texels, a version of this file's baked
	response; the exposure's own settings are a test's, not keyed. The
	classic tier's 0: the files made before this module still current) */
	if (h2_lighting_tier() == _h2_lighting_tier_classic)
		return 0;
	/* (the pages whole - h2_lighting_lightmap_shift 0, the desktop's - keyed
	too: bit 20; the halves' key as before) */
	return 0x4C000000UL | (unsigned long)(h2_lighting_lightmap_shift() == 0) << 20 |
		(unsigned long)h2_lighting_atlas_32bit() << 16 | (unsigned long)h2_lighting_tier() << 8 | 3;
}

boolean h2_lighting_atlas_32bit(void)
{
	/* (opt-in: +2 MB a Zanzibar, whose multiplayer map cache - 49,283,072
	bytes - the other Halo 2 branches fill too) */
	return h2_lighting_tier() == _h2_lighting_tier_h2 && getenv("HALO_H2_LIGHTMAP_32BIT") && atoi(getenv("HALO_H2_LIGHTMAP_32BIT"));
}

long h2_lighting_lightmap_shift(void)
{
	/* (the Vita and its null build: half each way in both tiers - Halo 2's
	pages whole are 3 atlases, 12 MB at 32 bits, past the multiplayer map
	cache (49,283,072 bytes) on Zanzibar; HALO_H2_LIGHTMAP_SHIFT=0 has them
	whole where they fit.
	h2/render-accuracy: the desktop's pages whole, as Halo 2 samples them
	(H2EK lightmap_tex_* passes: the page's texel, no other). Halved, the
	small charts of an interior's pages (Cairo's: walls a few texels across,
	packed edge to edge with lit floors and light fixtures) took their
	neighbours' light in the average and in the bilinear tap past their
	edge - Cairo q0_0 1.45x the tag test's brightness, its walls in
	triangle-shaped patches of another chart's light. The pages' texels
	are the H2EK's own, byte for byte (01b bsp 0: 400 pages, tool
	export-bitmap-tga vs MCC's). HALO_H2_LIGHTMAP_SHIFT_OLD=1 the halves) */
#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
	return 1;
#else
	return HALO_ENV_ONCE("HALO_H2_LIGHTMAP_SHIFT_OLD") ? 1 : 0;
#endif
}

boolean h2_lighting_incident_directions(void)
{
	return h2_lighting_tier() == _h2_lighting_tier_h2 && !HALO_ENV_ONCE("HALO_H2_NO_INCIDENT");
}

/* (h2_lighting_encode's settings, read again when the settings change:
it runs for every lightmap texel - a level's pages, on the build's
threads - and four getenv a texel were most of the Halo 2 tier's atlas
time on the Vita, 4.2 s of Zanzibar's on Vita3K against the classic
tier's 14 ms) */
static struct
{
	/* halo_settings_generation + 1 when the rest were read, 0 never */
	unsigned long generation;
	short tier;
	float exposure, gamma, saturation;
} h2_lighting_encoding;

static void h2_lighting_encoding_current(short *tier, float *exposure, float *gamma, float *saturation)
{
	extern volatile unsigned long halo_settings_generation;
	unsigned long generation = halo_settings_generation + 1;

	if (__atomic_load_n(&h2_lighting_encoding.generation, __ATOMIC_ACQUIRE) != generation)
	{
		/* (the build's threads may each read them at once: the same values,
		the generation stored after them) */
		h2_lighting_encoding.tier = h2_lighting_tier();
		/* (the texel holds half the light: the texture pass doubles it) */
		h2_lighting_encoding.exposure = setting_real("HALO_H2_LIGHTMAP_EXPOSURE", 2.0f) * 0.5f;
		h2_lighting_encoding.gamma = setting_real("HALO_H2_LIGHTMAP_GAMMA", 1.0f);
		h2_lighting_encoding.saturation = setting_real("HALO_H2_LIGHTMAP_SATURATION", 1.0f);
		__atomic_store_n(&h2_lighting_encoding.generation, generation, __ATOMIC_RELEASE);
	}
	*tier = h2_lighting_encoding.tier;
	*exposure = h2_lighting_encoding.exposure;
	*gamma = h2_lighting_encoding.gamma;
	*saturation = h2_lighting_encoding.saturation;
}

boolean h2_lighting_encode(float const light[3], unsigned char out[3])
{
	static int constant = -2;
	static float knee[2] = { -2.0f, 1.0f };
	float exposure, gamma, saturation, grey, value[3];
	short tier;
	int k;

	h2_lighting_encoding_current(&tier, &exposure, &gamma, &saturation);
	if (tier != _h2_lighting_tier_h2)
		return FALSE;
	if (constant == -2)
		constant = getenv("HALO_H2_LIGHTMAP_CONSTANT") ? atoi(getenv("HALO_H2_LIGHTMAP_CONSTANT")) : -1;
	if (constant >= 0)
	{
		out[0] = out[1] = out[2] = (unsigned char)(constant > 255 ? 255 : constant);
		return TRUE;
	}
	if (knee[0] == -2.0f)
	{
		knee[0] = -1.0f;
		if (getenv("HALO_H2_LIGHTMAP_KNEE"))
			sscanf(getenv("HALO_H2_LIGHTMAP_KNEE"), "%f%*[ ,]%f", &knee[0], &knee[1]);
	}
	/* (gamma 1, the default: the light itself - powf(x, 1) is x exactly -
	without three powf a texel, the classic tier's gamma table's cost on
	the Vita: page_texels_convert) */
	for (k = 0; k < 3; k++)
		value[k] = gamma == 1.0f ? (light[k] > 0.0f ? light[k] : 0.0f) : powf(light[k] > 0.0f ? light[k] : 0.0f, gamma);
	grey = 0.299f * value[0] + 0.587f * value[1] + 0.114f * value[2];
	for (k = 0; k < 3; k++)
	{
		float v = (grey + (value[k] - grey) * saturation) * exposure;

		/* (a test: HALO_H2_LIGHTMAP_KNEE="k s" - the texel above k
		compressed by s) */
		if (knee[0] >= 0.0f && v > knee[0])
			v = knee[0] + (v - knee[0]) * knee[1];
		if (v < 0.0f)
			v = 0.0f;
		if (v > 1.0f)
			v = 1.0f;
		out[k] = (unsigned char)(v * 255.0f + 0.5f);
	}

	return TRUE;
}

void h2_lighting_level_resolved(char const *name, unsigned long build_key)
{
	snprintf(h2_lighting_globals.level, sizeof(h2_lighting_globals.level), "%s", name ? name : "");
	h2_lighting_globals.build_key = build_key;
	h2_lighting_globals.scenario_index = NONE;
}

boolean h2_lighting_overbright(void)
{
	static unsigned long generation;

	/* (the map's generation too: consecutive maps' scenarios can have the same tag index) */
	if (h2_lighting_globals.scenario_index != global_scenario_index || generation != halo_map_generation)
	{
		char const *scenario = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;
		char const *leaf = scenario ? strrchr(scenario, '\\') : NULL;

		h2_lighting_globals.scenario_index = global_scenario_index;
		generation = halo_map_generation;
		/* (a converted level's scenario is h2\<name>...: h2_level.c) */
		h2_lighting_globals.overbright = scenario && !strncmp(scenario, "h2\\", 3) && leaf &&
			!strcmp(leaf + 1, h2_lighting_globals.level) &&
			(h2_lighting_globals.build_key >> 8 & 0xFF) == _h2_lighting_tier_h2;
		/* (a Halo 3 level made at load - h3_level.c, its scenario h3\<name> -
		bakes its lightmap for this pass whenever h2_lighting_h3_overbright
		says so, which a level's make and its drawing ask alike) */
		if (scenario && !strncmp(scenario, "h3\\", 3))
			h2_lighting_globals.overbright = h2_lighting_h3_overbright();
		if (scenario && !strncmp(scenario, "h2\\", 3))
			platform_log("h2 lighting: %s: %s", scenario, h2_lighting_globals.overbright ? "Halo 2 tier (overbright lightmap)" : "classic tier");
	}
	return h2_lighting_globals.overbright;
}

boolean h2_lighting_h3_overbright(void)
{
	/* (Halo 3's light goes past its albedo - its sun on the sand and the
	cliffs - which this engine's lightmap can reach only through the
	modulate-2x texture pass Halo 2's levels have; HALO_H3_OVERBRIGHT=0 a
	test: the texel clamped at the albedo, as before) */
	char const *setting = getenv("HALO_H3_OVERBRIGHT");

	return h2_lighting_tier() == _h2_lighting_tier_h2 && !(setting && !atoi(setting));
}

void h2_lighting_object_lightmap_color(float color[3])
{
	if (h2_lighting_overbright())
	{
		color[0] *= 2.0f;
		color[1] *= 2.0f;
		color[2] *= 2.0f;
	}
}

/* ---------- instance incident directions (the lightmap's geometry buckets) */

static unsigned long le32(unsigned char const *p) { return p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24; }
static short le16(unsigned char const *p) { return (short)(p[0] | p[1] << 8); }

/* a bucket's incident-direction buffer (and its vertex count): its
resource read whole, as a section's is (h2_level.c section_read: 'blkh',
the section, then the resources' data); NULL if it has none. With colors,
its baked colour buffer too (MCC's 0x2E: a word a vertex, R G B A bytes;
NULL none) */
static float *bucket_read(struct h2_cache *cache, unsigned long bucket, long *vertex_count, unsigned long **colors);

static float *bucket_incident(struct h2_cache *cache, unsigned long bucket, long *vertex_count)
{
	return bucket_read(cache, bucket, vertex_count, NULL);
}

static float *bucket_read(struct h2_cache *cache, unsigned long bucket, long *vertex_count, unsigned long **colors)
{
	unsigned char const *g = h2_cache_at(cache, bucket, 0x24);
	unsigned long reference, size, section_size, resources, data;
	long resource_count, i;
	unsigned char *raw;
	unsigned char const *descriptors = NULL;
	unsigned long descriptors_size = 0;
	unsigned char const *buffers[8] = { 0 };
	unsigned long buffer_sizes[8] = { 0 };
	float *incident = NULL;
	unsigned long color_bytes = 0;

	*vertex_count = 0;
	if (colors)
		*colors = NULL;
	if (!g)
		return NULL;
	reference = le32(g + 0x0C);
	size = le32(g + 0x10);
	section_size = le32(g + 0x14);
	if (h2_cache_is_mcc(cache) && reference != 0xFFFFFFFFUL && !(reference >> 30))
		reference |= (unsigned long)h2_cache_address_source(cache, bucket) << 30;
	if (!size || reference == 0xFFFFFFFFUL || size > 16 * 1024 * 1024 || section_size > size ||
		!h2_cache_block(cache, bucket + 0x1C, 16, &resource_count, &resources))
	{
		return NULL;
	}
	raw = malloc(size);
	if (!raw || !h2_cache_resource_read(cache, reference, raw, size) || memcmp(raw, "hklb", 4) || 8 + section_size > size)
	{
		free(raw);
		return NULL;
	}
	data = 8 + section_size;
	for (i = 0; i < resource_count; i++)
	{
		unsigned char const *r = h2_cache_at(cache, resources + 16 * i, 16);
		unsigned short primary = (unsigned short)le16(r + 2);
		short element = le16(r + 6);
		unsigned long rsize = le32(r + 8), roffset = le32(r + 12);

		if (getenv("HALO_H2_BUCKET_DEBUG"))
			fprintf(stderr, "h2 lighting: bucket %lx resource %ld type %d primary %x element %d size %lu offset %lu\n",
				bucket, i, r[0], primary, element, rsize, roffset);
		if (roffset > size - data || rsize > size - data - roffset)
			continue;
		if (r[0] == 0 && descriptors == NULL && rsize % 32 == 0)
			descriptors = raw + data + roffset, descriptors_size = rsize;
		else if (r[0] == 2 && element >= 0 && element < 8)
			buffers[element] = raw + data + roffset, buffer_sizes[element] = rsize;
	}
	for (i = 0; descriptors && i < (long)(descriptors_size / 32) && i < 8; i++)
	{
		if (getenv("HALO_H2_BUCKET_DEBUG"))
			fprintf(stderr, "h2 lighting: bucket %lx buffer %ld type %02x stride %d size %lu\n", bucket, i,
				descriptors[32 * i], descriptors[32 * i + 1], buffer_sizes[i]);
		if (descriptors[32 * i] == 0x2F && buffers[i] && !incident && (incident = malloc(buffer_sizes[i])) != NULL)
		{
			memcpy(incident, buffers[i], buffer_sizes[i]);
			*vertex_count = (long)(buffer_sizes[i] / 12);
		}
		else if (descriptors[32 * i] == 0x2E && colors && buffers[i] && !*colors && (*colors = malloc(buffer_sizes[i] + 4)) != NULL)
		{
			memcpy(*colors, buffers[i], buffer_sizes[i]);
			color_bytes = buffer_sizes[i];
		}
	}
	/* (the colours' count the directions': a bucket of colours without
	directions has none - Halo 2's buckets carry both, flags 3) */
	if (colors && *colors && (!incident || color_bytes / 4 != (unsigned long)*vertex_count))
	{
		free(*colors);
		*colors = NULL;
	}
	free(raw);

	return incident;
}

/* (h2_lighting_instance_incident's) the last bucket's directions */
static struct
{
	long busy;
	struct h2_cache *cache;
	unsigned long group, bucket;
	long instance;
	float *all;
	long vertices;
} h2_lighting_bucket_cache;

float *h2_lighting_instance_incident(struct h2_cache *cache, unsigned long lightmap_group, long instance, long vertex_count)
{
	long ref_count, bucket_count, offsets_count, bucket_vertices;
	unsigned long refs, buckets, offsets;
	unsigned char const *ref;
	short bucket_index, offset;
	float *all, *out;
	boolean cached;

	if (!h2_lighting_incident_directions() || HALO_ENV_ONCE("HALO_H2_NO_INSTANCE_INCIDENT") || !lightmap_group || vertex_count <= 0 ||
		!h2_cache_block(cache, lightmap_group + 0x50, 12, &ref_count, &refs) || instance < 0 || instance >= ref_count ||
		!h2_cache_block(cache, lightmap_group + 0x40, 0x38, &bucket_count, &buckets))
	{
		return NULL;
	}
	ref = h2_cache_at(cache, refs + 12 * instance, 12);
	bucket_index = ref ? le16(ref + 2) : -1;
	if (!ref || bucket_index < 0 || bucket_index >= bucket_count ||
		!h2_cache_block(cache, refs + 12 * instance + 4, 2, &offsets_count, &offsets) || offsets_count < 1)
	{
		return NULL;
	}
	offset = le16(h2_cache_at(cache, offsets, 2));
	/* (the bucket's directions kept while the next instances read the same
	bucket - Zanzibar's 62 instances read 11 buckets in 25 runs, each read
	the whole resource inflated again - and let go after the last instance
	that reads it; a second build at once reads its own) */
	cached = !__atomic_exchange_n(&h2_lighting_bucket_cache.busy, 1, __ATOMIC_ACQUIRE);
	if (cached && h2_lighting_bucket_cache.all && h2_lighting_bucket_cache.cache == cache &&
		h2_lighting_bucket_cache.group == lightmap_group && h2_lighting_bucket_cache.bucket == buckets + 0x38 * bucket_index &&
		h2_lighting_bucket_cache.instance < instance)
	{
		all = h2_lighting_bucket_cache.all;
		bucket_vertices = h2_lighting_bucket_cache.vertices;
	}
	else
	{
		if (cached)
		{
			free(h2_lighting_bucket_cache.all);
			h2_lighting_bucket_cache.all = NULL;
		}
		all = bucket_incident(cache, buckets + 0x38 * bucket_index, &bucket_vertices);
	}
	if (HALO_ENV_ONCE("HALO_H2_BUCKET_DEBUG"))
		fprintf(stderr, "h2 lighting: instance %ld bucket %d offset %d: %ld of the bucket's %ld vertices\n",
			instance, bucket_index, offset, vertex_count, bucket_vertices);
	out = all && offset >= 0 && offset + vertex_count <= bucket_vertices ? malloc(vertex_count * 12) : NULL;
	if (out)
		memcpy(out, all + 3 * offset, vertex_count * 12);
	if (cached)
	{
		long later;

		/* (kept only while a later instance reads this bucket) */
		for (later = instance + 1; all && later < ref_count; later++)
		{
			unsigned char const *other = h2_cache_at(cache, refs + 12 * later, 12);

			if (other && le16(other + 2) == bucket_index)
				break;
		}
		if (all && later < ref_count)
		{
			h2_lighting_bucket_cache.cache = cache;
			h2_lighting_bucket_cache.group = lightmap_group;
			h2_lighting_bucket_cache.instance = instance;
			h2_lighting_bucket_cache.bucket = buckets + 0x38 * bucket_index;
			h2_lighting_bucket_cache.all = all;
			h2_lighting_bucket_cache.vertices = bucket_vertices;
		}
		else
		{
			free(all);
			h2_lighting_bucket_cache.all = NULL;
		}
		__atomic_store_n(&h2_lighting_bucket_cache.busy, 0, __ATOMIC_RELEASE);
	}
	else
		free(all);

	return out;
}

void h2_lighting_atlas_dither_565(unsigned char *rgb, long size)
{
	/* (Floyd-Steinberg, a row at a time, each channel to its 565 levels:
	what the >> 3 / >> 2 of the 16-bit writer then keeps exactly) */
	float *error;
	long x, y;
	int k;

	if (h2_lighting_tier() != _h2_lighting_tier_h2 || h2_lighting_atlas_32bit() || getenv("HALO_H2_NO_DITHER") ||
		(error = calloc((unsigned long)(size + 2) * 2 * 3, sizeof(float))) == NULL)
	{
		return;
	}
	for (y = 0; y < size; y++)
	{
		float *row = error + (y & 1) * (size + 2) * 3, *next = error + ((y + 1) & 1) * (size + 2) * 3;

		memset(next, 0, (unsigned long)(size + 2) * 3 * sizeof(float));
		for (x = 0; x < size; x++)
		{
			unsigned char *c = rgb + 3 * (y * size + x);

			for (k = 0; k < 3; k++)
			{
				int bits = k == 1 ? 6 : 5;
				float levels = (float)((1 << bits) - 1);
				float want = (float)c[k] / 255.0f * levels + row[3 * (x + 1) + k];
				/* (floorf(want + 0.5f) without the library call - a call a
				channel on the Vita, 3 million an atlas: the truncation, one
				less below a negative fraction - the same whole number) */
				float half_up = want + 0.5f;
				long level = (long)half_up;
				float e;

				if ((float)level > half_up)
					level--;

				if (level < 0)
					level = 0;
				if (level > (long)levels)
					level = (long)levels;
				e = want - (float)level;
				/* (the 8-bit value whose top bits are the level) */
				c[k] = (unsigned char)(level << (8 - bits) | level >> (2 * bits - 8));
				row[3 * (x + 2) + k] += e * 7.0f / 16.0f;
				next[3 * x + k] += e * 3.0f / 16.0f;
				next[3 * (x + 1) + k] += e * 5.0f / 16.0f;
				next[3 * (x + 2) + k] += e * 1.0f / 16.0f;
			}
		}
	}
	free(error);
}

/* ---------- objects' light (object_lights.c build_distant_lights) */

static float h2_lighting_sky[3];
static boolean h2_lighting_sky_set;

void h2_lighting_object_sky(float const rgb[3])
{
	h2_lighting_sky_set = rgb && rgb[0] + rgb[1] + rgb[2] > 0.0f && !HALO_ENV_ONCE("HALO_H2_FIXED_SKY_LIGHT");
	if (h2_lighting_sky_set)
		memcpy(h2_lighting_sky, rgb, sizeof(h2_lighting_sky));
}

static int h2_prt_old_signs(void)
{
	static int old = -1;

	if (old < 0)
		old = getenv("HALO_H2_PRT_SH_SIGNS_OLD") != NULL;
	return old;
}

/* (the first four coefficients of each colour's spherical harmonics - L00,
L1-1 (y), L10 (z), L11 (x) - as this engine's lighting: ambient, pad, light
0 colour and direction, light 1 colour and direction) */
static void h2_lighting_sh_to_lighting(float const l[3][4], float lighting[16])
{
	float direction[3], length, luma_dc, luma_dir[3];
	int c, k;

	/* (irradiance: E0 = 0.886 L00 everywhere, and the first band's
	1.023 |L1| along its direction (x L11, y L1-1, z L10); this engine's
	ambient E0 less half of it, its light 0 the first band's along it) */
	luma_dc = 0.0f;
	for (k = 0; k < 3; k++)
		luma_dir[k] = 0.0f;
	for (c = 0; c < 3; c++)
	{
		float w = c == 0 ? 0.299f : c == 1 ? 0.587f : 0.114f;

		luma_dc += w * l[c][0];
		/* (D3DX's signs, as the PRT transfers' (round 17): L1-1 = -y,
		L10 = z, L11 = -x - the light was turned half round about z;
		HALO_H2_PRT_SH_SIGNS_OLD: as before) */
		luma_dir[0] += w * (h2_prt_old_signs() ? l[c][3] : -l[c][3]);
		luma_dir[1] += w * (h2_prt_old_signs() ? l[c][1] : -l[c][1]);
		luma_dir[2] += w * l[c][2];
	}
	length = (float)sqrt(luma_dir[0] * luma_dir[0] + luma_dir[1] * luma_dir[1] + luma_dir[2] * luma_dir[2]);
	for (k = 0; k < 3; k++)
		direction[k] = length > 1e-4f ? luma_dir[k] / length : (k == 2 ? 1.0f : 0.0f);
	for (c = 0; c < 3; c++)
	{
		float dir = 1.023f * (float)sqrt(l[c][1] * l[c][1] + l[c][2] * l[c][2] + l[c][3] * l[c][3]);
		float e0 = 0.886f * l[c][0] - 0.5f * dir;

		lighting[c] = e0 > 0.0f ? e0 : 0.0f;
		lighting[4 + c] = dir;
		lighting[10 + c] = 0.0f;
	}
	/* (the light's travel: away from its direction) */
	for (k = 0; k < 3; k++)
	{
		lighting[7 + k] = -direction[k];
		lighting[13 + k] = direction[k];
	}
}

/* (Halo 2's own default, with no samples: a unit white light straight down
projected - its irradiance about 0.28 L00 - and its first band along -z) */
static void h2_lighting_sh_default(float l[3][4])
{
	int c;

	memset(l, 0, 3 * 4 * sizeof(float));
	for (c = 0; c < 3; c++)
	{
		l[c][0] = 0.886f;
		l[c][2] = 1.023f;
	}
}

void h2_lighting_prt_environment(struct h2_cache *cache, unsigned long lightmap_group, float lighting[16])
{
	long count = 0, i;
	unsigned long samples = 0;
	float l[3][4] = { { 0 } };   /* (each colour's first four coefficients: L00, L1-1 (y), L10 (z), L11 (x)) */
	int c, k;

	if (getenv("HALO_H2_NO_PRT_LIGHT"))
		return;
	if (h2_cache_block(cache, lightmap_group + 0x38, 0xDC, &count, &samples) && count > 0)
	{
		for (i = 0; i < count; i++)
		{
			unsigned char const *e = h2_cache_at(cache, samples + 0xDC * (unsigned long)i, 0xDC);

			for (c = 0; c < 3 && e; c++)
			{
				for (k = 0; k < 4; k++)
				{
					float v;

					memcpy(&v, e + 12 + 36 * c + 4 * k, 4);
					l[c][k] += v / (float)count;
				}
			}
		}
	}
	else
		h2_lighting_sh_default(l);
	h2_lighting_sh_to_lighting(l, lighting);
	if (getenv("HALO_H2_DEBUG"))
		fprintf(stderr, "h2 lighting: PRT environment from %ld samples: ambient %.2f %.2f %.2f, light %.2f %.2f %.2f towards %.2f %.2f %.2f\n",
			count, lighting[0], lighting[1], lighting[2], lighting[4], lighting[5], lighting[6], lighting[7], lighting[8], lighting[9]);
}

/* ---------- the lighting environments by position (Halo 2's PRT objects' light)

Halo 2 lights a PRT object (prt_ templates: the Scarab's hull, legs and
turrets, vehicles' and bipeds' PRT shaders) from the lighting environments
of its structure BSP's lightmap group - sample points through the level,
each with order-2 spherical harmonics of the light there -, not from the
lightmap under it: function_23690 of the decompilation (GPL-3.0-or-later)
takes, for every render section of the model, the world position of the
section's node and asks function_22cf0 for the environment there: the
nearest sample, and the nearest sample on the far side of it (the
position's offset from the first projected on its offset to the other at
or below zero), the two blended by where the position falls between them
(function_022c30: the projection's fraction of the segment); with one
sample that one, with none a default light. The level build averaged
every sample of the group into the BSP's default lighting - 03b's city
average, dark and warm, lit the Scarab's interior black and its deck orange
wherever it walked. The samples are read here as the level is resolved
(the MCC map is open then for the global lighting tag) and kept by
structure BSP; object_lights.c asks for the object's. */

struct h2_light_sample
{
	float position[3];
	float sh[3][9];   /* (each colour's order-3 coefficients, D3DX's order and signs) */
};

static struct
{
	long bsp_count;
	long *counts;
	struct h2_light_sample **samples;
} h2_light_environments;

static void h2_lighting_environments_free(void)
{
	long k;

	for (k = 0; k < h2_light_environments.bsp_count; k++)
		free(h2_light_environments.samples[k]);
	free(h2_light_environments.samples);
	free(h2_light_environments.counts);
	memset(&h2_light_environments, 0, sizeof(h2_light_environments));
}

void h2_lighting_environments_read(struct h2_cache *cache)
{
	long structures = h2_cache_structure_count(cache), k;
	long total = 0;

	h2_lighting_environments_free();
	if (structures <= 0 || getenv("HALO_H2_NO_PRT_LIGHT") || getenv("HALO_H2_PRT_LIGHT_AVERAGE"))
		return;
	h2_light_environments.counts = calloc((size_t)structures, sizeof(long));
	h2_light_environments.samples = calloc((size_t)structures, sizeof(struct h2_light_sample *));
	if (!h2_light_environments.counts || !h2_light_environments.samples)
	{
		h2_lighting_environments_free();
		return;
	}
	h2_light_environments.bsp_count = structures;
	for (k = 0; k < structures; k++)
	{
		unsigned long sbsp = 0, lightmap = 0, group = 0, samples = 0;
		long groups = 0, count = 0, i;
		char why[128];
		struct h2_light_sample *out;

		if (!h2_cache_structure_load(cache, k, &sbsp, &lightmap, why, sizeof(why)) || !lightmap ||
			!h2_cache_block(cache, lightmap + 0x80, 0x68, &groups, &group) || groups < 1 ||
			!h2_cache_block(cache, group + 0x38, 0xDC, &count, &samples) || count <= 0)
		{
			continue;
		}
		out = calloc((size_t)count, sizeof(*out));
		if (!out)
			continue;
		for (i = 0; i < count; i++)
		{
			unsigned char const *e = h2_cache_at(cache, samples + 0xDC * (unsigned long)i, 0xDC);
			int c, j;

			if (!e)
				continue;
			memcpy(out[i].position, e, 12);
			for (c = 0; c < 3; c++)
				for (j = 0; j < 9; j++)
					memcpy(&out[i].sh[c][j], e + 12 + 36 * c + 4 * j, 4);
			if (getenv("HALO_H2_DEBUG_PRT"))
			{
				char text[400] = "";
				float v;

				for (c = 0; c < 3; c++)
				{
					for (j = 0; j < 9; j++)
					{
						memcpy(&v, e + 12 + 36 * c + 4 * j, 4);
						snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s%.3f", j ? " " : c ? " | " : "", v);
					}
				}
				platform_log("h2 lighting: bsp %ld environment %ld at %.1f %.1f %.1f: %s", k, i,
					out[i].position[0], out[i].position[1], out[i].position[2], text);
			}
		}
		h2_light_environments.counts[k] = count;
		h2_light_environments.samples[k] = out;
		total += count;
	}
	platform_log("h2 lighting: %ld lighting environments over %ld structure bsps (PRT objects' light by position)", total, structures);
}

boolean h2_lighting_prt_object_sh(short structure_bsp_index, float const position[3], float l[3][9])
{
	struct h2_light_sample const *samples, *first, *second = NULL;
	float delta[3], closest = 1048576.0f, other = 1048576.0f, fraction = 0.0f;
	long count, nearest = 0, opposite = NONE, i;
	int c, j;

	if (structure_bsp_index < 0 || structure_bsp_index >= h2_light_environments.bsp_count ||
		(count = h2_light_environments.counts[structure_bsp_index]) <= 0)
	{
		return FALSE;
	}
	samples = h2_light_environments.samples[structure_bsp_index];
	for (i = 0; i < count; i++)
	{
		float x = position[0] - samples[i].position[0], y = position[1] - samples[i].position[1], z = position[2] - samples[i].position[2];
		float distance = z * z + x * x + y * y;

		if (distance < closest)
		{
			closest = distance;
			nearest = i;
		}
	}
	first = &samples[nearest];
	for (j = 0; j < 3; j++)
		delta[j] = position[j] - first->position[j];
	for (i = 0; i < count; i++)
	{
		float x, y, z, projection;

		if (i == nearest)
			continue;
		x = position[0] - samples[i].position[0], y = position[1] - samples[i].position[1], z = position[2] - samples[i].position[2];
		projection = delta[2] * z + delta[1] * y + delta[0] * x;
		if (projection <= 0.0f)
		{
			float distance = z * z + y * y + x * x;

			if (distance < other)
			{
				other = distance;
				opposite = i;
			}
		}
	}
	if (opposite != NONE)
	{
		/* (function_022c30: where the position falls along the segment
		from the first sample to the other) */
		float segment[3], length2 = 0.0f, along = 0.0f;

		second = &samples[opposite];
		for (j = 0; j < 3; j++)
		{
			segment[j] = second->position[j] - first->position[j];
			length2 += segment[j] * segment[j];
			along += delta[j] * segment[j];
		}
		fraction = length2 > 1e-6f ? along / length2 : 0.0f;
	}
	if (fraction <= 0.0f || !second)
		memcpy(l, first->sh, 3 * 9 * sizeof(float));
	else if (fraction >= 1.0f)
		memcpy(l, second->sh, 3 * 9 * sizeof(float));
	else
	{
		for (c = 0; c < 3; c++)
			for (j = 0; j < 9; j++)
				l[c][j] = (second->sh[c][j] - first->sh[c][j]) * fraction + first->sh[c][j];
	}
	return TRUE;
}

boolean h2_lighting_prt_object(short structure_bsp_index, float const position[3], float lighting[16])
{
	float l[3][9], l4[3][4];
	int c, j;

	if (!h2_lighting_prt_object_sh(structure_bsp_index, position, l))
		return FALSE;
	for (c = 0; c < 3; c++)
		for (j = 0; j < 4; j++)
			l4[c][j] = l[c][j];
	h2_lighting_sh_to_lighting(l4, lighting);
	return TRUE;
}

/* ---------- the PRT vertices' light (Halo 2's CPCA, function_23690 / function_023ca0)

Halo 2's PRT models carry D3DX's compressed PRT: each vertex a cluster
and four PCA weights, the model a basis - each cluster its mean transfer
and four PCA vectors, each order-3 spherical harmonics (9 coefficients) a
colour. The light of the section's environment (above) is turned into the
node's frame (function_143600, D3DX's SH rotation) and dotted with every
basis vector (function_023ca0): the vertex shader then sums its weights'
dots - prt_lightmap: oD0 = (PCA_M + PCA_WEIGHTS . PCA_R/G/B) x prt scale -
and the pass draws 2 oD0 x base. The dots here, as nv2a_vsh.c's prt[]:
each cluster four vectors - the means' red, green, blue; the four PCA
vectors' red; green; blue.

The rotation of order-3 real spherical harmonics (D3DX's basis: Y1-1 = -y,
Y10 = z, Y11 = -x times 0.488603; Y2-2 = xy, Y2-1 = -yz, Y21 = -xz times
1.092548, Y20 = 0.315392 (3 z^2 - 1), Y22 = 0.546274 (x^2 - y^2)): the first
band is a vector, turned as one; the second a traceless symmetric form
n^T Q n, turned as R^T Q R; the constant band stays. */

static void sh_rotate_into_node(float const l[9], real_matrix4x3 const *node, float out[9])
{
	/* (the node's axes in the world: forward, left, up - the world to node
	turn is the transpose; its scale divided out) */
	float ax[3][3], q[3][3], t[3][3], qn[3][3], d[3], dn[3];
	float scale = node->scale > 0.0001f ? node->scale : 1.0f;
	int i, j, k;

	ax[0][0] = node->forward.i / scale, ax[0][1] = node->forward.j / scale, ax[0][2] = node->forward.k / scale;
	ax[1][0] = node->left.i / scale, ax[1][1] = node->left.j / scale, ax[1][2] = node->left.k / scale;
	ax[2][0] = node->up.i / scale, ax[2][1] = node->up.j / scale, ax[2][2] = node->up.k / scale;
	out[0] = l[0];
	/* band 1: f(n) = n . d, d = 0.4886 (-l3, -l1, l2); d' = R^T d */
	d[0] = -l[3], d[1] = -l[1], d[2] = l[2];
	for (i = 0; i < 3; i++)
		dn[i] = d[0] * ax[i][0] + d[1] * ax[i][1] + d[2] * ax[i][2];
	out[1] = -dn[1], out[2] = dn[2], out[3] = -dn[0];
	/* band 2: f(n) = n^T Q n */
	q[0][1] = q[1][0] = 1.092548f * l[4] * 0.5f;
	q[1][2] = q[2][1] = -1.092548f * l[5] * 0.5f;
	q[0][2] = q[2][0] = -1.092548f * l[7] * 0.5f;
	q[0][0] = -0.315392f * l[6] + 0.546274f * l[8];
	q[1][1] = -0.315392f * l[6] - 0.546274f * l[8];
	q[2][2] = 2.0f * 0.315392f * l[6];
	/* Q' = R^T Q R: R's columns are the axes (world = R node), so R[i][j] = ax[j][i] */
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
		{
			t[i][j] = 0.0f;
			for (k = 0; k < 3; k++)
				t[i][j] += q[i][k] * ax[j][k];   /* (Q R)[i][j] = sum_k Q[i][k] R[k][j], R[k][j] = ax[j][k] */
		}
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
		{
			qn[i][j] = 0.0f;
			for (k = 0; k < 3; k++)
				qn[i][j] += ax[i][k] * t[k][j];   /* (R^T)[i][k] = R[k][i] = ax[i][k] */
		}
	out[4] = 2.0f * qn[0][1] / 1.092548f;
	out[5] = -2.0f * qn[1][2] / 1.092548f;
	out[7] = -2.0f * qn[0][2] / 1.092548f;
	out[6] = qn[2][2] / (2.0f * 0.315392f);
	out[8] = (qn[0][0] - qn[1][1]) / (2.0f * 0.546274f);
}

long h2_lighting_prt_cluster_constants(float const sh[3][9], real_matrix4x3 const *node, float const *basis, long basis_count,
	float gain, float (*constants)[4], long maximum)
{
	long order, clusters, pca, coefficients, vector_size, cluster_size, k, v, c, j, out = 0;
	float l[3][9];

	if (!basis || basis_count < 4)
		return 0;
	order = (long)basis[0], clusters = (long)basis[1], pca = (long)basis[2];
	coefficients = order * order;
	if (order < 1 || order > 3 || clusters < 1 || pca < 0 || pca > 4)
		return 0;
	vector_size = coefficients * 3;
	cluster_size = (pca + 1) * vector_size;
	if (4 + clusters * cluster_size > basis_count)
		return 0;
	for (c = 0; c < 3; c++)
	{
		if (node)
			sh_rotate_into_node(sh[c], node, l[c]);
		else
			memcpy(l[c], sh[c], sizeof(l[c]));
	}
	for (k = 0; k < clusters && out + 4 <= maximum; k++)
	{
		float const *cluster = basis + 4 + k * cluster_size;

		memset(constants[out], 0, 4 * 4 * sizeof(float));
		for (v = 0; v <= pca; v++)
		{
			float const *vector = cluster + v * vector_size;

			for (c = 0; c < 3; c++)
			{
				float dot = 0.0f;

				for (j = 0; j < coefficients; j++)
					dot += vector[c * coefficients + j] * l[c][j];
				dot *= gain;
				if (v == 0)
					constants[out][c] = dot;
				else
					constants[out + 1 + c][v - 1] = dot;
			}
		}
		out += 4;
	}
	return out;
}

static short h2_lighting_type;
static float h2_lighting_env_brightness = 1.0f;

boolean h2_lighting_trace_on;

void h2_lighting_object_type(short type)
{
	h2_lighting_type = type;
}

boolean h2_lighting_object_reject(float const light[3])
{
	static float dark = -1.0f;

	if (dark < 0.0f)
		dark = getenv("HALO_H2_OBJECT_DARK") ? (float)atof(getenv("HALO_H2_OBJECT_DARK")) : 0.06f;
	return h2_lighting_object_halo2() && h2_lighting_type >= 6 &&
		light[0] * 0.299f + light[1] * 0.587f + light[2] * 0.114f < dark;
}

float h2_lighting_object_env(void)
{
	return h2_lighting_env_brightness;
}

boolean h2_lighting_object_halo2(void)
{
	static int halo2 = -1;

	if (halo2 < 0)
		halo2 = !getenv("HALO_H2_OBJECT_LIGHT_OLD");
	return halo2 && h2_lighting_overbright();
}

/* Halo 2's object light (its lighting record, function_d37f0 of the
decompilation, GPL-3.0-or-later; the fallback function_d4080 when the tag has
no entry for the type): built from the lightmap under the object - a ray
down - its light L (the texel, 0..1), its incident direction's length s (its
directionality) and the ground's albedo A, with the parameters of the
scenario's global lighting tag (scenario 0x338 "global lighting"; none: the
globals' default - globals\global_lighting_default in shared.map, Zanzibar's
global_lighting_multiplayer - the tag's entry whose "object affected" flags
hold the type):
  primary   pin(L + bias) * scale1(s)
  secondary (2 A' L' s^2 + primary's colour (1 - s^2)) * scale2(s)
  ambient   pin(L + bias, Min lightmap sample..) * scale0(luma) * (0.4 - 0.1 s) * 1.4
  env       value1c(2 luma): its "object env brightness", the same in sun and
            shade (a metal keeps its reflection in the dark)
Its passes (H2EK shaders\shader_passes\lightmap\lightmap_tex_detail's
LIGHTMAP DYNAMIC, read for the rule) draw 2 (primary N.L + secondary N.L' +
ambient Nz) * 2 T T' - 2 T T' the base map with its detail - and this engine's
model pass has no 2X: the colours here are the record's times 2
(HALO_H2_OBJECT_LIGHT_GAIN). */
enum { H2_GLOBAL_LIGHTING_ENTRIES = 4, H2_LIGHTING_FUNCTION_REALS = 42 };

struct h2_lighting_function
{
	unsigned char type, flags;
	float lower, upper;
	float parameters[H2_LIGHTING_FUNCTION_REALS];
};

struct h2_lighting_entry
{
	unsigned long flags;
	float bias, lower1[3], upper1[3];
	float lower2[3], upper2[3], lower_base[3], upper_base[3];
	float lower0[3], upper0[3];
	struct h2_lighting_function scale1, scale2, scale0, env;
};

static struct
{
	long count;
	struct h2_lighting_entry entries[H2_GLOBAL_LIGHTING_ENTRIES];
} h2_global_lighting;

static float pin(float value, float lower, float upper)
{
	return value < lower ? lower : value > upper ? upper : value;
}

/* a tag function's value (function_13b390, then its output bounds): the
types the global lighting tags use, the linear (4) and the linear key (5) */
static float h2_lighting_function_value(struct h2_lighting_function const *f, float input)
{
	float const *p = f->parameters;
	float value = 0.0f;

	switch (f->type)
	{
	case 0:
		value = input;
		break;
	case 4:
		value = p[4] * input + p[5];
		break;
	case 5:
		value = p[19] * pin((input - p[11]) * p[15], 0.0f, 1.0f) + p[18] * pin((input - p[10]) * p[14], 0.0f, 1.0f) +
			p[17] * pin((input - p[9]) * p[13], 0.0f, 1.0f) + p[16];
		break;
	default:
		break;
	}
	value = pin(value, 0.0f, 1.0f);
	return (f->flags & 0xF0) ? value : (f->upper - f->lower) * value + f->lower;
}

static boolean h2_lighting_function_read(struct h2_cache *cache, unsigned long address, struct h2_lighting_function *f)
{
	unsigned long size = h2_cache_read32(cache, address);
	unsigned long data = h2_cache_read32(cache, address + 4);
	unsigned char const *bytes = size >= 12 ? h2_cache_at(cache, data, size) : NULL;
	long reals;

	memset(f, 0, sizeof(*f));
	if (!bytes || (bytes[0] != 0 && bytes[0] != 4 && bytes[0] != 5))
		return FALSE;
	f->type = bytes[0];
	f->flags = bytes[1];
	memcpy(&f->lower, bytes + 4, 4);
	memcpy(&f->upper, bytes + 8, 4);
	/* (the parameters from byte 0x14, where Halo 2's evaluator reads them -
	h2_material_scalar_curve, decompilation material_queries.c: p = curve +
	0x14; linear: p+0x10 x input + p+0x14, linear key: its ramps at p+0x24,
	p+0x34, p+0x40, p+0x44 - the indices above. Until session 16 of
	h2/render-accuracy they were read from byte 12, 8 bytes early: every
	global lighting curve took the wrong words - the ambient's scale rose
	with the light (0.3 + 0.7 luma) where Halo 2's falls (1 - 0.7 luma,
	global_lighting_multiplayer's points (0,1) (1,0)), the env brightness
	held at its top, 0.6, where Halo 2's ramps 0 -> 0.6 over luma 0.2 -> 0.8.
	(Checked against the H2EK's globals\global_lighting_multiplayer: its
	linear functions' bytes from 0x14 are the points (0,0) (1,1) and then
	slope 1, offset 0 - from 12 the reading took a point for the slope.)
	HALO_H2_LIGHTING_FUNCTION_OLD=1: the old reading, which the Vita and
	its null build keep - not measured there, and they keep the doubled
	sample it was set with) */
	{
		static int old = -1;
		unsigned long start;

		if (old < 0)
		{
			char const *setting = getenv("HALO_H2_LIGHTING_FUNCTION_OLD");

#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
			old = !setting || atoi(setting) != 0;
#else
			old = setting && atoi(setting) != 0;
#endif
		}
		start = old ? 12 : 0x14;
		reals = size > start ? (long)((size - start) / 4) : 0;
		memcpy(f->parameters, bytes + start, 4 * (unsigned long)(reals > H2_LIGHTING_FUNCTION_REALS ? H2_LIGHTING_FUNCTION_REALS : reals));
	}
	return TRUE;
}

void h2_lighting_global_lighting_read(struct h2_cache *cache)
{
	struct h2_cache_tag scenario, tag;
	long index = NONE, count = 0, i;
	unsigned long entries = 0;

	h2_global_lighting.count = 0;
	if (getenv("HALO_H2_GLOBAL_LIGHTING_OFF"))
		return;
	if (h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario))
		index = h2_cache_reference(cache, scenario.address + 0x338);
	if (index == NONE)
		index = h2_cache_tag_find(cache, 'gldf', "globals\\global_lighting_default");
	if (index == NONE || !h2_cache_tag_get(cache, index, &tag) ||
		!h2_cache_block(cache, tag.address, 0x90, &count, &entries))
	{
		platform_log("h2 lighting: no global lighting tag: objects take Halo 2's fallback");
		return;
	}
	for (i = 0; i < count && h2_global_lighting.count < H2_GLOBAL_LIGHTING_ENTRIES; i++)
	{
		unsigned char const *e = h2_cache_at(cache, entries + 0x90 * (unsigned long)i, 0x90);
		struct h2_lighting_entry *out = &h2_global_lighting.entries[h2_global_lighting.count];

		if (!e)
			continue;
		out->flags = le32(e);
		memcpy(&out->bias, e + 0x04, 4);
		memcpy(out->lower1, e + 0x08, 12);
		memcpy(out->upper1, e + 0x14, 12);
		memcpy(out->lower2, e + 0x2C, 12);
		memcpy(out->upper2, e + 0x38, 12);
		memcpy(out->lower_base, e + 0x44, 12);
		memcpy(out->upper_base, e + 0x50, 12);
		memcpy(out->lower0, e + 0x68, 12);
		memcpy(out->upper0, e + 0x74, 12);
		if (h2_lighting_function_read(cache, entries + 0x90 * (unsigned long)i + 0x24, &out->scale1) &&
			h2_lighting_function_read(cache, entries + 0x90 * (unsigned long)i + 0x60, &out->scale2) &&
			h2_lighting_function_read(cache, entries + 0x90 * (unsigned long)i + 0x80, &out->scale0) &&
			h2_lighting_function_read(cache, entries + 0x90 * (unsigned long)i + 0x88, &out->env))
		{
			h2_global_lighting.count++;
		}
	}
	platform_log("h2 lighting: global lighting %s: %ld of %ld entries; min sample %.2f and %.2f",
		h2_cache_tag_name(cache, index), h2_global_lighting.count, count,
		h2_global_lighting.count > 0 ? h2_global_lighting.entries[0].lower0[0] : 0.0f,
		h2_global_lighting.count > 1 ? h2_global_lighting.entries[1].lower0[0] : 0.0f);
	for (i = 0; i < h2_global_lighting.count; i++)
	{
		struct h2_lighting_entry const *e = &h2_global_lighting.entries[i];

		platform_log("h2 lighting: entry %ld (flags %lx): primary scale %.3f..%.3f, secondary %.3f..%.3f, ambient %.3f..%.3f, env %.3f %.3f %.3f %.3f (at 0, 0.5, 1 / env at 0.2 0.5 0.8 1)",
			i, e->flags, h2_lighting_function_value(&e->scale1, 0.0f), h2_lighting_function_value(&e->scale1, 1.0f),
			h2_lighting_function_value(&e->scale2, 0.0f), h2_lighting_function_value(&e->scale2, 1.0f),
			h2_lighting_function_value(&e->scale0, 0.0f), h2_lighting_function_value(&e->scale0, 1.0f),
			h2_lighting_function_value(&e->env, 0.2f), h2_lighting_function_value(&e->env, 0.5f),
			h2_lighting_function_value(&e->env, 0.8f), h2_lighting_function_value(&e->env, 1.0f));
	}
}

static void h2_lighting_halo2_terms(float const light[3], float s, float const diffuse[3], float ambient[3], float direct[3],
	float bounce[3])
{
	static float gain = -1.0f;
	struct h2_lighting_entry const *entry = NULL;
	float l[3], luma_raw = 0.0f, luma = 0.0f, square = s * s;
	int i;

	if (gain < 0.0f)
		gain = getenv("HALO_H2_OBJECT_LIGHT_GAIN") ? (float)atof(getenv("HALO_H2_OBJECT_LIGHT_GAIN")) : 2.0f;
	/* (the record's lightmap sample: the page's texel itself - Halo 2's
	h2_draw_lighting_evaluate (decompilation, draw_queue.c) packs the
	texel h2_bitmap_lightmap_rgb reads (its palette's entry, 0..1) as the
	sample's colour, no scale -; the light handed here is that texel
	doubled (h2_lighting_object_lightmap_color, the light a surface facing
	it has), so it is halved back. Until session 15 of h2/render-accuracy
	the doubled light was the sample, set by eye to MCC frames: against the
	H2EK tag test every object drew about twice as bright - Cairo's
	crates, shields and racks real/ours 0.45-0.56, Colossus, Burial
	Mounds, Cyclotron's 0.40-0.52. HALO_H2_OBJECT_SAMPLE_DOUBLED=1: the
	old doubled sample, which the Vita and its null build keep - not
	measured there) */
	{
		static int doubled = -1;

		if (doubled < 0)
		{
			char const *setting = getenv("HALO_H2_OBJECT_SAMPLE_DOUBLED");

#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
			doubled = !setting || atoi(setting) != 0;
#else
			doubled = setting && atoi(setting) != 0;
#endif
		}
		for (i = 0; i < 3; i++)
			l[i] = pin(doubled ? light[i] : 0.5f * light[i], 0.0f, 1.0f);
	}
	for (i = 0; i < h2_global_lighting.count; i++)
	{
		if (h2_global_lighting.entries[i].flags & (1UL << (h2_lighting_type + 1) | 1UL))
			entry = &h2_global_lighting.entries[i];
	}
	if (entry)
	{
		float adjusted[3], scale0, scale1, scale2;

		for (i = 0; i < 3; i++)
			adjusted[i] = l[i] + entry->bias;
		luma_raw = 2.0f * (0.11f * l[2] + 0.59f * l[1] + 0.3f * l[0]);
		luma = 0.114f * adjusted[2] + 0.587f * adjusted[1] + 0.299f * adjusted[0];
		h2_lighting_env_brightness = h2_lighting_function_value(&entry->env, pin(luma_raw, 0.0f, 1.0f));
		scale1 = h2_lighting_function_value(&entry->scale1, s);
		scale2 = h2_lighting_function_value(&entry->scale2, s);
		scale0 = h2_lighting_function_value(&entry->scale0, luma) * (1.0f - (0.1f * s + 0.6f)) * 1.4f;
		for (i = 0; i < 3; i++)
		{
			float primary = pin(adjusted[i], entry->lower1[i], entry->upper1[i]);
			float base = pin(diffuse[i], entry->lower_base[i], entry->upper_base[i]);
			float second = pin(adjusted[i], entry->lower2[i], entry->upper2[i]);

			second = base * second * square * 2.0f + primary * (1.0f - square);
			ambient[i] = pin(adjusted[i], entry->lower0[i], entry->upper0[i]) * scale0;
			direct[i] = primary * scale1;
			bounce[i] = second * scale2;
		}
	}
	else
	{
		float k = h2_lighting_type >= 0 && h2_lighting_type <= 2 ? 1.18f : 0.75f;

		h2_lighting_env_brightness = 1.0f;
		luma = 0.114f * l[2] + 0.587f * l[1] + 0.299f * l[0];
		for (i = 0; i < 3; i++)
		{
			float albedo = pin(diffuse[i], 0.25f, 0.75f);
			float second = pin(2.0f * l[i] * albedo, 0.0f, 1.0f);

			second = (l[i] + (second - l[i]) * square) * (0.4f - 0.05f * s);
			direct[i] = l[i] * (0.4f + 0.05f * s);
			bounce[i] = second;
			ambient[i] = l[i] * (0.4f - 0.1f * s) * ((k - 0.7f) * luma + 0.7f);
		}
	}
	for (i = 0; i < 3; i++)
	{
		ambient[i] = pin(ambient[i], 0.0f, 1.0f) * gain;
		direct[i] = pin(direct[i], 0.0f, 1.0f) * gain;
		bounce[i] = pin(bounce[i], 0.0f, 1.0f) * gain;
	}
}

boolean h2_lighting_object_terms(
	float const light[3],
	float accuracy,
	float const diffuse[3],
	float ambient[3],
	float direct[3],
	float bounce[3],
	float shadow[3])
{
	static float terms[8] = { -1.0f };
	float brightness, half[3], a = accuracy < 0.0f ? 0.0f : accuracy > 1.0f ? 1.0f : accuracy;
	int k;

	if (!h2_lighting_overbright())
		return FALSE;
	if (terms[0] < 0.0f)
	{
		/* (HALO_H2_OBJECT_LIGHT="ambient direct bounce sky_r sky_g sky_b":
		the light under the object - Halo 2's 2 L - a part of it everywhere
		(ambient) and its directed part from its incident direction (the
		sun's, in sun), the ground's colour bounced up, and the level's sky
		light, the same in sun and shade - Halo 2's lighting environments,
		Zanzibar's two: incoming intensity 0.29-0.30, a little blue. Tuned
		to the owner's MCC frames: a Warthog in shade keeps about 0.55 of its
		sunlit brightness (owner/20261023063742 vs owner2/20261023065220),
		where the ground keeps 0.15) */
		char const *setting = getenv("HALO_H2_OBJECT_LIGHT");

		terms[0] = 0.2f, terms[1] = 0.5f, terms[2] = 0.3f;
		terms[3] = 0.23f, terms[4] = 0.24f, terms[5] = 0.26f;
		if (setting)
			sscanf(setting, "%f%*[ ,]%f%*[ ,]%f%*[ ,]%f%*[ ,]%f%*[ ,]%f", &terms[0], &terms[1], &terms[2], &terms[3], &terms[4], &terms[5]);
	}
	/* (a test: HALO_H2_OBJECT_LIGHT_FORCE="r g b accuracy" every object's
	light as if it stood there - a shade's on the beach's Warthog) */
	{
		static float force[4] = { -2.0f };
		static float forced[3];

		if (force[0] == -2.0f)
		{
			force[0] = -1.0f;
			if (getenv("HALO_H2_OBJECT_LIGHT_FORCE"))
				sscanf(getenv("HALO_H2_OBJECT_LIGHT_FORCE"), "%f%*[ ,]%f%*[ ,]%f%*[ ,]%f", &force[0], &force[1], &force[2], &force[3]);
		}
		if (force[0] >= 0.0f)
		{
			memcpy(forced, force, sizeof(forced));
			light = forced;
			a = force[3];
		}
	}
	brightness = (light[0] + light[1] + light[2]) / 3.0f;
	for (k = 0; k < 3; k++)
	{
		/* (the sky light: the level's sky's own outdoor ambient where it has
		one - 03a's warm brown -, scaled to the fixed one's level (Zanzibar's
		lighting environments, which its sky has no ambient for: its 0.24
		against 03a's 0.47) - HALO_H2_SKY_AMBIENT_SCALE, 0.5) */
		static float sky_scale = -1.0f;

		if (sky_scale < 0.0f)
			sky_scale = getenv("HALO_H2_SKY_AMBIENT_SCALE") ? (float)atof(getenv("HALO_H2_SKY_AMBIENT_SCALE")) : 0.5f;
		ambient[k] = light[k] * terms[0] + (h2_lighting_sky_set ? h2_lighting_sky[k] * sky_scale : terms[3 + k]);
		direct[k] = light[k] * terms[1] * a;
		bounce[k] = diffuse[k] * brightness * terms[2];
		/* (the shadow under it as the classic light's: from the texel's own
		half, not the doubled light - else black) */
		half[k] = light[k] * 0.5f;
		shadow[k] = 1.0f - half[k] * 1.3f + (1.0f - a) * 0.5f;
		if (shadow[k] < 0.03f)
			shadow[k] = 0.03f;
		if (shadow[k] > 1.0f)
			shadow[k] = 1.0f;
	}
	if (h2_lighting_object_halo2())
		h2_lighting_halo2_terms(light, a, diffuse, ambient, direct, bounce);
	if (getenv("HALO_H2_OBJECT_LIGHT_LOG2"))
	{
		static int count;

		if ((count++ & 15) == 0)
			platform_log("h2 lighting: type %d C %.2f %.2f %.2f L %.2f A %.2f %.2f %.2f: ambient %.2f %.2f %.2f primary %.2f %.2f %.2f secondary %.2f %.2f %.2f",
				h2_lighting_type, light[0], light[1], light[2], a, diffuse[0], diffuse[1], diffuse[2], ambient[0], ambient[1], ambient[2],
				direct[0], direct[1], direct[2], bounce[0], bounce[1], bounce[2]);
	}
	/* (HALO_H2_OBJECT_TRACE=<name part>: every term of the objects so named,
	object_lights.c's mark - a test's) */
	if (h2_lighting_trace_on)
		platform_log("h2 trace: terms: L %.3f %.3f %.3f s %.3f albedo %.3f %.3f %.3f -> ambient %.3f %.3f %.3f direct %.3f %.3f %.3f bounce %.3f %.3f %.3f (halo2 %d, env %.3f)",
			light[0], light[1], light[2], a, diffuse[0], diffuse[1], diffuse[2], ambient[0], ambient[1], ambient[2],
			direct[0], direct[1], direct[2], bounce[0], bounce[1], bounce[2], (int)h2_lighting_object_halo2(), h2_lighting_env_brightness);
	if (getenv("HALO_H2_OBJECT_LIGHT_LOG"))
	{
		static int count;

		if ((count++ & 15) == 0)
			platform_log("h2 lighting: object light %.2f %.2f %.2f accuracy %.2f: ambient %.2f %.2f %.2f direct %.2f bounce %.2f (sky %d %.2f %.2f %.2f)",
				light[0], light[1], light[2], a, ambient[0], ambient[1], ambient[2], direct[1], bounce[1], (int)h2_lighting_sky_set,
				h2_lighting_sky[0], h2_lighting_sky[1], h2_lighting_sky[2]);
	}
	return TRUE;
}

/* ---------- baked scenery light (h2/render-accuracy)

Halo 2 lights its placed scenery from the lightmap, not as it lights what
moves: the lightmap tag's group (0x80, 0x68 each) holds 'scenery object
info' (0x58, 0xC each: the placement's unique id at 0, its origin BSP at 4,
its type at 6 - every one 6, scenery, on Zanzibar and 03a's three BSPs:
10, 81, 48, 41 -, its source at 7, its render model's checksum at 8) and,
one for one, 'scenery object bucket refs' (0x60, 0xC each: a geometry
bucket at 2 and, for each of the render model's sections, where its
vertices begin in that bucket - a block of shorts at 4). The bucket (0x40,
0x38 each, flags 3: colour and incident direction) holds a vertex's
primary lightmap colour and incident direction, one per vertex of each
section, in the section's own vertex order.
Halo 2 draws such an object with its pervertex lightmap pass (H2EK
rasterizer\vertex_shaders_dx9\lightmap_pervertex_tex: oD1.rgb the vertex's
colour, oD1.a its incident direction's length, oT3 the direction in tangent
space; pixel_shaders_dx9 lightmap_tex_1, $lightmap_type 1: R0 = D1 +
D1 * |I|(N.I - Iz), then 2 R0 base) - the BSP's own lightmap formula with
the vertex's colour as the texel -; the dynamic lights add after it (its
lightaccum passes). This engine: the level builder keeps each object model
part's Halo 2 section and each vertex's index in it (h2_level.c, the
model's 'h2bk' words at 0x44) and each scenery placement's unique id
(its permutation's unused word at 0x44, scenery.c hands it here); a part
of such an object is drawn with its vertices' colour as its light
(rasterizer_xbox_models.c: an extra vertex stream, 2 x colour in place of
the object's ambient and distant lights, its point lights added as
before), and the object's lights - the relief and specular passes', and
the Vita's - the mean of its vertices' (lights_prepare_for_object_static).
HALO_H2_BAKED_OBJECTS=0: lit as it moves, as before. */

struct h2_baked_entry
{
	unsigned long uid;
	short bucket;
	short type;
	long section_count;
	unsigned short *section_offsets;   /* (Halo 2's shorts: read unsigned, a bucket has up to 65535 vertices) */
};

struct h2_baked_bucket
{
	long vertex_count;
	unsigned long *colors;      /* R G B A bytes a vertex */
	float *incident;            /* 3 a vertex: toward the light, its length the light's directionality */
};

struct h2_baked_bsp
{
	long entry_count;
	struct h2_baked_entry *entries;
	long bucket_count;
	struct h2_baked_bucket *buckets;
};

enum { H2_BAKED_OBJECT_SLOTS = 4096 };

static struct
{
	long bsp_count;
	struct h2_baked_bsp *bsps;
	/* the placed objects (by the object's absolute index): the object's
	index, its placement's unique id, and its mean light (summary_bsp + 1:
	the BSP it was taken for, 0 not yet) */
	struct h2_baked_placed
	{
		long object_index;
		unsigned long uid;
		long summary_bsp_plus1;
		boolean summary_good;
		float color[3], incident[3];
	} placed[H2_BAKED_OBJECT_SLOTS];
} h2_baked;

boolean h2_lighting_baked_objects(void)
{
	static int enabled = -1;

	if (enabled < 0)
	{
		char const *setting = getenv("HALO_H2_BAKED_OBJECTS");

		/* (the desktop's GL renderer: its vertex programs take the extra
		stream - nv2a_vsh.c; the Vita's and its null build's do not) */
#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
		enabled = setting && atoi(setting) != 0;
#else
		enabled = !setting || atoi(setting) != 0;
#endif
	}
	return enabled && h2_lighting_tier() == _h2_lighting_tier_h2;
}

static void h2_baked_free(void)
{
	long k, i;

	for (k = 0; k < h2_baked.bsp_count; k++)
	{
		struct h2_baked_bsp *b = &h2_baked.bsps[k];

		for (i = 0; i < b->entry_count; i++)
			free(b->entries[i].section_offsets);
		for (i = 0; i < b->bucket_count; i++)
		{
			free(b->buckets[i].colors);
			free(b->buckets[i].incident);
		}
		free(b->entries);
		free(b->buckets);
	}
	free(h2_baked.bsps);
	memset(&h2_baked, 0, sizeof(h2_baked));
}

void h2_lighting_baked_read(struct h2_cache *cache)
{
	long structures = h2_cache_structure_count(cache), k;
	long total_entries = 0, total_vertices = 0;

	h2_baked_free();
	if (structures <= 0 || !h2_lighting_baked_objects())
		return;
	h2_baked.bsps = calloc((size_t)structures, sizeof(*h2_baked.bsps));
	if (!h2_baked.bsps)
		return;
	h2_baked.bsp_count = structures;
	for (k = 0; k < structures; k++)
	{
		struct h2_baked_bsp *b = &h2_baked.bsps[k];
		unsigned long sbsp = 0, lightmap = 0, group = 0, info = 0, refs = 0, buckets = 0;
		long groups = 0, info_count = 0, ref_count = 0, bucket_count = 0, i;
		char why[128];

		if (!h2_cache_structure_load(cache, k, &sbsp, &lightmap, why, sizeof(why)) || !lightmap ||
			!h2_cache_block(cache, lightmap + 0x80, 0x68, &groups, &group) || groups < 1 ||
			!h2_cache_block(cache, group + 0x58, 0xC, &info_count, &info) || info_count <= 0 ||
			!h2_cache_block(cache, group + 0x60, 0xC, &ref_count, &refs) || ref_count != info_count ||
			!h2_cache_block(cache, group + 0x40, 0x38, &bucket_count, &buckets) || bucket_count <= 0)
		{
			continue;
		}
		b->entries = calloc((size_t)info_count, sizeof(*b->entries));
		b->buckets = calloc((size_t)bucket_count, sizeof(*b->buckets));
		if (!b->entries || !b->buckets)
		{
			free(b->entries);
			free(b->buckets);
			b->entries = NULL;
			b->buckets = NULL;
			continue;
		}
		b->bucket_count = bucket_count;
		for (i = 0; i < info_count; i++)
		{
			unsigned char const *e = h2_cache_at(cache, info + 0xC * (unsigned long)i, 0xC);
			unsigned char const *r = h2_cache_at(cache, refs + 0xC * (unsigned long)i, 0xC);
			struct h2_baked_entry *out = &b->entries[b->entry_count];
			long offsets_count = 0, s;
			unsigned long offsets = 0;
			short bucket;

			if (!e || !r)
				continue;
			bucket = le16(r + 2);
			if (bucket < 0 || bucket >= bucket_count ||
				!h2_cache_block(cache, refs + 0xC * (unsigned long)i + 4, 2, &offsets_count, &offsets) || offsets_count <= 0 ||
				!(out->section_offsets = malloc((size_t)offsets_count * sizeof(unsigned short))))
			{
				continue;
			}
			out->uid = le32(e);
			out->type = e[6];
			out->bucket = bucket;
			out->section_count = offsets_count;
			for (s = 0; s < offsets_count; s++)
			{
				unsigned char const *o = h2_cache_at(cache, offsets + 2 * (unsigned long)s, 2);

				out->section_offsets[s] = o ? (unsigned short)le16(o) : 0xFFFF;
			}
			/* (its bucket's colours and directions, read once for all its objects) */
			if (!b->buckets[bucket].incident)
			{
				b->buckets[bucket].incident = bucket_read(cache, buckets + 0x38 * (unsigned long)bucket,
					&b->buckets[bucket].vertex_count, &b->buckets[bucket].colors);
				if (!b->buckets[bucket].colors)
				{
					free(b->buckets[bucket].incident);
					b->buckets[bucket].incident = NULL;
					b->buckets[bucket].vertex_count = 0;
				}
				total_vertices += b->buckets[bucket].vertex_count;
			}
			if (getenv("HALO_H2_BAKED_DEBUG"))
			{
				struct h2_baked_bucket const *bk = &b->buckets[bucket];
				unsigned long first = bk->colors && out->section_offsets[0] < bk->vertex_count ?
					bk->colors[out->section_offsets[0]] : 0;

				platform_log("h2 baked: bsp %ld object %ld: unique id %08lx type %d source %d bucket %d (%ld vertices), %ld sections, first at %d colour %08lx",
					k, i, out->uid, (int)e[6], (int)e[7], (int)bucket, bk->vertex_count, offsets_count, (int)out->section_offsets[0], first);
			}
			b->entry_count++;
		}
		total_entries += b->entry_count;
	}
	if (total_entries)
		platform_log("h2 lighting: %ld scenery objects lit by their baked lightmap colours over %ld structure bsps (%ld bucket vertices)",
			total_entries, structures, total_vertices);
}

void h2_lighting_baked_object_placed(long object_index, unsigned long uid)
{
	long slot = object_index & 0xFFFF;

	if (object_index == NONE || slot >= H2_BAKED_OBJECT_SLOTS)
		return;
	h2_baked.placed[slot].object_index = uid ? object_index : NONE;
	h2_baked.placed[slot].uid = uid;
	h2_baked.placed[slot].summary_bsp_plus1 = 0;
}

/* an object's baked entry in a BSP and its bucket; NULL none */
static struct h2_baked_entry const *h2_baked_find(long object_index, short bsp, struct h2_baked_bucket const **bucket)
{
	long slot = object_index & 0xFFFF, i;
	struct h2_baked_bsp const *b;
	unsigned long uid;

	if (object_index == NONE || slot >= H2_BAKED_OBJECT_SLOTS || h2_baked.placed[slot].object_index != object_index ||
		bsp < 0 || bsp >= h2_baked.bsp_count || !h2_lighting_baked_objects())
	{
		return NULL;
	}
	uid = h2_baked.placed[slot].uid;
	b = &h2_baked.bsps[bsp];
	for (i = 0; i < b->entry_count; i++)
	{
		if (b->entries[i].uid == uid)
		{
			*bucket = &b->buckets[b->entries[i].bucket];
			return (*bucket)->colors ? &b->entries[i] : NULL;
		}
	}
	return NULL;
}

boolean h2_lighting_baked_object(long object_index, short bsp)
{
	struct h2_baked_bucket const *bucket;

	return h2_baked_find(object_index, bsp, &bucket) != NULL;
}

/* the model's 'h2bk' words (h2_level.c model_made_write): the signature,
the part count K and geometry count G, each geometry's parts' model part
(G x K shorts: the part's place in the geometry, -1 none), then for each
part its section (short, -1 none), a pad, its vertex count and the first of
its vertices' section indices (longs), then those indices (shorts) */
struct h2_baked_part
{
	short section;
	long vertex_count;
	unsigned short const *indices;
};

static boolean h2_baked_model_part(unsigned char const *block, long bytes, long k, struct h2_baked_part *part)
{
	unsigned long parts, geometries, at, first;

	if (!block || bytes < 8 || le32(block) != 0x6B623268UL)   /* 'h2bk' */
		return FALSE;
	parts = (unsigned short)le16(block + 4);
	geometries = (unsigned short)le16(block + 6);
	at = (8 + 2 * parts * geometries + 3) & ~3UL;
	if (k < 0 || (unsigned long)k >= parts || at + 12 * parts > (unsigned long)bytes)
		return FALSE;
	part->section = le16(block + at + 12 * k);
	part->vertex_count = (long)le32(block + at + 12 * k + 4);
	first = le32(block + at + 12 * k + 8);
	if (part->section < 0 || at + 12 * parts + 2 * (first + (unsigned long)part->vertex_count) > (unsigned long)bytes)
		return FALSE;
	part->indices = (unsigned short const *)(block + at + 12 * parts + 2 * first);
	return TRUE;
}

/* the model part drawn as a geometry's part_index-th; NONE none */
static long h2_baked_model_part_index(unsigned char const *block, long bytes, short geometry, short part_index)
{
	unsigned long parts, geometries, k;

	if (!block || bytes < 8 || le32(block) != 0x6B623268UL)
		return NONE;
	parts = (unsigned short)le16(block + 4);
	geometries = (unsigned short)le16(block + 6);
	if (geometry < 0 || (unsigned long)geometry >= geometries || 8 + 2 * parts * geometries > (unsigned long)bytes)
		return NONE;
	for (k = 0; k < parts; k++)
	{
		if (le16(block + 8 + 2 * ((unsigned long)geometry * parts + k)) == part_index)
			return (long)k;
	}
	return NONE;
}

static float h2_baked_channel(unsigned long word, int c)
{
	return (float)((word >> (8 * c)) & 0xFF) / 255.0f;
}

boolean h2_lighting_baked_part(long object_index, short bsp, void const *model_block, long model_block_bytes, short geometry,
	short part_index, long vertex_count, float (*out)[4], float gain)
{
	struct h2_baked_bucket const *bucket;
	struct h2_baked_entry const *entry = h2_baked_find(object_index, bsp, &bucket);
	struct h2_baked_part part;
	long k, v, first;

	if (!entry || vertex_count <= 0 ||
		(k = h2_baked_model_part_index(model_block, model_block_bytes, geometry, part_index)) == NONE ||
		!h2_baked_model_part(model_block, model_block_bytes, k, &part) || part.vertex_count != vertex_count ||
		part.section >= entry->section_count || (first = entry->section_offsets[part.section]) == 0xFFFF)
	{
		return FALSE;
	}
	for (v = 0; v < vertex_count; v++)
	{
		long at = first + part.indices[v];
		float const *i;

		if (at >= bucket->vertex_count)
			return FALSE;
		i = bucket->incident + 3 * at;
		out[v][0] = gain * h2_baked_channel(bucket->colors[at], 0);
		out[v][1] = gain * h2_baked_channel(bucket->colors[at], 1);
		out[v][2] = gain * h2_baked_channel(bucket->colors[at], 2);
		out[v][3] = sqrtf(i[0] * i[0] + i[1] * i[1] + i[2] * i[2]);
	}
	return TRUE;
}

boolean h2_lighting_baked_summary(long object_index, short bsp, void const *model_block, long model_block_bytes,
	float color[3], float incident[3])
{
	struct h2_baked_bucket const *bucket;
	struct h2_baked_entry const *entry = h2_baked_find(object_index, bsp, &bucket);
	struct h2_baked_placed *placed;
	unsigned char const *block = model_block;
	long k, parts;

	if (!entry)
		return FALSE;
	placed = &h2_baked.placed[object_index & 0xFFFF];
	if (placed->summary_bsp_plus1 != bsp + 1)
	{
		double sum[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
		long n = 0;

		parts = block && model_block_bytes >= 8 && le32(block) == 0x6B623268UL ? (unsigned short)le16(block + 4) : 0;
		for (k = 0; k < parts; k++)
		{
			struct h2_baked_part part;
			long v, first;

			if (!h2_baked_model_part(block, model_block_bytes, k, &part) || part.section >= entry->section_count ||
				(first = entry->section_offsets[part.section]) == 0xFFFF)
			{
				continue;
			}
			for (v = 0; v < part.vertex_count; v++)
			{
				long at = first + part.indices[v];
				int c;

				if (at >= bucket->vertex_count)
					break;
				for (c = 0; c < 3; c++)
				{
					sum[c] += h2_baked_channel(bucket->colors[at], c);
					sum[3 + c] += bucket->incident[3 * at + c];
				}
				n++;
			}
		}
		placed->summary_good = n > 0;
		for (k = 0; k < 3 && n > 0; k++)
		{
			placed->color[k] = (float)(sum[k] / n);
			placed->incident[k] = (float)(sum[3 + k] / n);
		}
		placed->summary_bsp_plus1 = bsp + 1;
		if (getenv("HALO_H2_BAKED_DEBUG"))
			platform_log("h2 baked: object %08lx (unique id %08lx) in bsp %d: %ld vertices, mean colour %.3f %.3f %.3f, incident %.2f %.2f %.2f",
				(unsigned long)object_index, entry->uid, (int)bsp, n, placed->color[0], placed->color[1], placed->color[2],
				placed->incident[0], placed->incident[1], placed->incident[2]);
	}
	if (!placed->summary_good)
		return FALSE;
	memcpy(color, placed->color, sizeof(placed->color));
	memcpy(incident, placed->incident, sizeof(placed->incident));
	return TRUE;
}

/* ---------- a light's brightness animation (h2/render-accuracy) */

float h2_light_animation_scale(void const *light_definition, float seconds)
{
	static int on = -1;
	unsigned char const *p = (unsigned char const *)light_definition + H2_LIGHT_ANIMATION_OFFSET;
	float const *d;
	float minimum, maximum, brightness, wave, fraction, value;
	unsigned long magic;

	if (on < 0)
	{
		char const *setting = getenv("HALO_H2_LIGHT_ANIMATION");

#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
		on = setting && *setting == '1';
#else
		on = !setting || *setting != '0';
#endif
	}
	if (!on || !light_definition)
		return 1.0f;
	memcpy(&magic, p, 4);
	if (magic != H2_LIGHT_ANIMATION_MAGIC || p[4] != 3)
		return 1.0f;
	memcpy(&minimum, p + 0x08, 4);
	memcpy(&maximum, p + 0x0C, 4);
	memcpy(&brightness, p + 0x30, 4);
	if (!(brightness > 0.0f))
		return 1.0f;
	d = (float const *)(p + 0x10);
	/* (Halo 2's periodic curve as h2_effects.c reads it: frequency, phase,
	then the curve's two ends - a cosine for shapes 2 and 3, the others'
	middle; its first curve, the range input none) */
	wave = p[6] == 2 || p[6] == 3 ? 0.5f - 0.5f * cosf(6.2831853f * (d[0] * seconds + d[1])) : 0.5f;
	fraction = d[2] + (d[3] - d[2]) * wave;
	value = minimum + (maximum - minimum) * fraction;
	value /= brightness;
	return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
}

/* (h2/render-accuracy) an instance Halo 2 lights per vertex - no lightmap
page of its own (its render info's page -1; lightmap_pervertex): its
bucket's baked colours (MCC 0x2E, R G B A bytes, R the low byte - a texel's
light), one for each of its vertices (malloc'd, vertex_count of them); NULL
if its bucket has none */
unsigned long *h2_lighting_instance_baked_colors(struct h2_cache *cache, unsigned long lightmap_group, long instance, long vertex_count)
{
	long ref_count, bucket_count, offsets_count, bucket_vertices;
	unsigned long refs, buckets, offsets;
	unsigned char const *ref;
	unsigned long *colors = NULL, *out;
	float *incident;
	short bucket_index, offset;

	if (!lightmap_group || vertex_count <= 0 ||
		!h2_cache_block(cache, lightmap_group + 0x50, 12, &ref_count, &refs) || instance < 0 || instance >= ref_count ||
		!h2_cache_block(cache, lightmap_group + 0x40, 0x38, &bucket_count, &buckets))
	{
		return NULL;
	}
	ref = h2_cache_at(cache, refs + 12 * instance, 12);
	bucket_index = ref ? le16(ref + 2) : -1;
	if (!ref || bucket_index < 0 || bucket_index >= bucket_count ||
		!h2_cache_block(cache, refs + 12 * instance + 4, 2, &offsets_count, &offsets) || offsets_count < 1)
	{
		return NULL;
	}
	offset = le16(h2_cache_at(cache, offsets, 2));
	incident = bucket_read(cache, buckets + 0x38 * bucket_index, &bucket_vertices, &colors);
	free(incident);
	if (!colors || offset < 0 || offset + vertex_count > bucket_vertices || (out = malloc(vertex_count * sizeof(*out))) == NULL)
	{
		free(colors);
		return NULL;
	}
	memcpy(out, colors + offset, vertex_count * sizeof(*out));
	free(colors);
	return out;
}

/* (h2_lighting_instance_baked_colors') their mean (0..1); FALSE none */
boolean h2_lighting_instance_baked_mean(struct h2_cache *cache, unsigned long lightmap_group, long instance, long vertex_count,
	float rgb[3])
{
	unsigned long *colors = h2_lighting_instance_baked_colors(cache, lightmap_group, instance, vertex_count);
	double sum[3] = { 0.0, 0.0, 0.0 };
	long v;

	if (!colors)
		return FALSE;
	for (v = 0; v < vertex_count; v++)
	{
		unsigned long c = colors[v];

		sum[0] += (double)(c & 0xFF);
		sum[1] += (double)((c >> 8) & 0xFF);
		sum[2] += (double)((c >> 16) & 0xFF);
	}
	free(colors);
	for (v = 0; v < 3; v++)
		rgb[v] = (float)(sum[v] / (255.0 * (double)vertex_count));
	return TRUE;
}
