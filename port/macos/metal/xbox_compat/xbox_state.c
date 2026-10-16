/* Bounded frontend state preparation. Native object ownership stays in the
backend; resource/content validation stays in xbox_device.c. */
#include "xbox_state.h"
#include "metal_report.h"
#include <assert.h>
#include <string.h>

_Static_assert(sizeof(struct xbox_state_cache) <= 8192, "prepared cache has an 8 KiB budget");
_Static_assert(GPU_STAGE_COUNT == 4, "pixel key has four Xbox stages");

/* Each conversion names the values it knows. Its fallback is what an unset
(zero) or default state means; any other value is one this renderer does not
translate, reported (metal_report.h) and drawn with the fallback. */

static uint8_t gpu_compare(DWORD function, const char *state)
{
	switch (function)
	{
	case D3DCMP_LESS: return GPU_COMPARE_LESS;
	case D3DCMP_EQUAL: return GPU_COMPARE_EQUAL;
	case D3DCMP_LESSEQUAL: return GPU_COMPARE_LESS_EQUAL;
	case D3DCMP_GREATER: return GPU_COMPARE_GREATER;
	case D3DCMP_NOTEQUAL: return GPU_COMPARE_NOT_EQUAL;
	case D3DCMP_GREATEREQUAL: return GPU_COMPARE_GREATER_EQUAL;
	case D3DCMP_ALWAYS: return GPU_COMPARE_ALWAYS;
	case 0:
	case D3DCMP_NEVER: return GPU_COMPARE_NEVER;
	default:
		METAL_REPORT(METAL_REPORT_UNSUPPORTED_STATE, state, 0, function, "drawn as NEVER", NULL);
		return GPU_COMPARE_NEVER;
	}
}

static uint8_t gpu_stencil_operation(DWORD operation, const char *state)
{
	switch (operation)
	{
	case D3DSTENCILOP_KEEP: return GPU_STENCIL_KEEP;
	case D3DSTENCILOP_REPLACE: return GPU_STENCIL_REPLACE;
	case D3DSTENCILOP_INCRSAT: return GPU_STENCIL_INCREMENT_CLAMP;
	case D3DSTENCILOP_DECRSAT: return GPU_STENCIL_DECREMENT_CLAMP;
	case D3DSTENCILOP_INVERT: return GPU_STENCIL_INVERT;
	case D3DSTENCILOP_INCR: return GPU_STENCIL_INCREMENT_WRAP;
	case D3DSTENCILOP_DECR: return GPU_STENCIL_DECREMENT_WRAP;
	case D3DSTENCILOP_ZERO: return GPU_STENCIL_ZERO;
	default:
		METAL_REPORT(METAL_REPORT_UNSUPPORTED_STATE, state, 0, operation, "drawn as ZERO", NULL);
		return GPU_STENCIL_ZERO;
	}
}

static uint8_t gpu_blend_factor(DWORD factor, const char *state)
{
	switch (factor)
	{
	case D3DBLEND_ONE: return GPU_BLEND_ONE;
	case D3DBLEND_SRCCOLOR: return GPU_BLEND_SOURCE_COLOR;
	case D3DBLEND_INVSRCCOLOR: return GPU_BLEND_ONE_MINUS_SOURCE_COLOR;
	case D3DBLEND_SRCALPHA: return GPU_BLEND_SOURCE_ALPHA;
	case D3DBLEND_INVSRCALPHA: return GPU_BLEND_ONE_MINUS_SOURCE_ALPHA;
	case D3DBLEND_DESTALPHA: return GPU_BLEND_DESTINATION_ALPHA;
	case D3DBLEND_INVDESTALPHA: return GPU_BLEND_ONE_MINUS_DESTINATION_ALPHA;
	case D3DBLEND_DESTCOLOR: return GPU_BLEND_DESTINATION_COLOR;
	case D3DBLEND_INVDESTCOLOR: return GPU_BLEND_ONE_MINUS_DESTINATION_COLOR;
	case D3DBLEND_SRCALPHASAT: return GPU_BLEND_SOURCE_ALPHA_SATURATE;
	case D3DBLEND_CONSTANTCOLOR: return GPU_BLEND_CONSTANT_COLOR;
	case D3DBLEND_INVCONSTANTCOLOR: return GPU_BLEND_ONE_MINUS_CONSTANT_COLOR;
	case D3DBLEND_CONSTANTALPHA: return GPU_BLEND_CONSTANT_ALPHA;
	case D3DBLEND_INVCONSTANTALPHA: return GPU_BLEND_ONE_MINUS_CONSTANT_ALPHA;
	case D3DBLEND_ZERO: return GPU_BLEND_ZERO;
	default:
		METAL_REPORT(METAL_REPORT_UNSUPPORTED_STATE, state, 0, factor, "drawn as ZERO", NULL);
		return GPU_BLEND_ZERO;
	}
}

static uint8_t gpu_blend_operation(DWORD operation)
{
	switch (operation)
	{
	case D3DBLENDOP_SUBTRACT: return GPU_BLEND_OP_SUBTRACT;
	case D3DBLENDOP_REVSUBTRACT:
	case D3DBLENDOP_REVSUBTRACTSIGNED: return GPU_BLEND_OP_REVERSE_SUBTRACT;
	case D3DBLENDOP_MIN: return GPU_BLEND_OP_MIN;
	case D3DBLENDOP_MAX: return GPU_BLEND_OP_MAX;
	/* ADDSIGNED is the NV2A's signed add; its bias is dropped */
	case 0:
	case D3DBLENDOP_ADD:
	case D3DBLENDOP_ADDSIGNED: return GPU_BLEND_OP_ADD;
	default:
		METAL_REPORT(METAL_REPORT_UNSUPPORTED_STATE, "D3DRS_BLENDOP", 0, operation, "drawn as ADD", NULL);
		return GPU_BLEND_OP_ADD;
	}
}

static uint8_t gpu_filter(DWORD filter, const char *state, unsigned stage)
{
	switch (filter)
	{
	case D3DTEXF_POINT: return GPU_FILTER_POINT;
	case D3DTEXF_LINEAR: return GPU_FILTER_LINEAR;
	case D3DTEXF_ANISOTROPIC: return GPU_FILTER_ANISOTROPIC;
	case D3DTEXF_QUINCUNX: return GPU_FILTER_QUINCUNX;
	case D3DTEXF_GAUSSIANCUBIC: return GPU_FILTER_GAUSSIAN_CUBIC;
	case D3DTEXF_NONE: return GPU_FILTER_NONE;
	default:
		METAL_REPORT(METAL_REPORT_UNSUPPORTED_STATE, state, stage, filter, "sampled without filtering", NULL);
		return GPU_FILTER_NONE;
	}
}

static uint8_t gpu_address(DWORD mode, const char *state, unsigned stage)
{
	switch (mode)
	{
	case D3DTADDRESS_MIRROR: return GPU_ADDRESS_MIRROR;
	case D3DTADDRESS_CLAMP: return GPU_ADDRESS_CLAMP;
	case D3DTADDRESS_BORDER: return GPU_ADDRESS_BORDER;
	case D3DTADDRESS_CLAMPTOEDGE: return GPU_ADDRESS_CLAMP_TO_EDGE;
	case 0:
	case D3DTADDRESS_WRAP: return GPU_ADDRESS_WRAP;
	default:
		METAL_REPORT(METAL_REPORT_UNSUPPORTED_STATE, state, stage, mode, "drawn with WRAP", NULL);
		return GPU_ADDRESS_WRAP;
	}
}

static float state_float(DWORD bits)
{
	float value;
	_Static_assert(sizeof(value) == sizeof(bits), "Xbox float state is 32 bits");
	memcpy(&value, &bits, sizeof(value));
	return value;
}

void xbox_state_reset(struct xbox_state_cache *cache)
{
	assert(cache != NULL);
	memset(cache, 0, sizeof(*cache));
}

void xbox_state_depth(struct xbox_state_cache *cache, struct draw_profile *profile,
	const DWORD *rs, BOOL has_depth, struct gpu_depth_stencil_state *out)
{
	assert(cache != NULL);
	assert(profile != NULL);
	assert(rs != NULL);
	assert(out != NULL);
	DWORD inputs[] = { has_depth, rs[D3DRS_ZENABLE], rs[D3DRS_ZWRITEENABLE], rs[D3DRS_ZFUNC],
		rs[D3DRS_STENCILENABLE], rs[D3DRS_STENCILFUNC], rs[D3DRS_STENCILFAIL],
		rs[D3DRS_STENCILZFAIL], rs[D3DRS_STENCILPASS],
		rs[D3DRS_STENCILMASK], rs[D3DRS_STENCILWRITEMASK] };
	_Static_assert(sizeof(inputs) == sizeof(cache->depth_inputs), "complete depth snapshot");
	if (!draw_profile_reuse(profile, DRAW_CACHE_DEPTH, cache->depth_valid &&
		!memcmp(cache->depth_inputs, inputs, sizeof(inputs))))
	{
		struct gpu_depth_stencil_state *depth = &cache->depth;
		memset(depth, 0, sizeof(*depth));
		depth->depth_test = has_depth && rs[D3DRS_ZENABLE];
		depth->depth_write = depth->depth_test && rs[D3DRS_ZWRITEENABLE];
		depth->depth_function = gpu_compare(rs[D3DRS_ZFUNC], "D3DRS_ZFUNC");
		depth->stencil_test = has_depth && rs[D3DRS_STENCILENABLE];
		depth->stencil_function = gpu_compare(rs[D3DRS_STENCILFUNC], "D3DRS_STENCILFUNC");
		depth->stencil_fail = gpu_stencil_operation(rs[D3DRS_STENCILFAIL], "D3DRS_STENCILFAIL");
		depth->stencil_depth_fail = gpu_stencil_operation(rs[D3DRS_STENCILZFAIL], "D3DRS_STENCILZFAIL");
		depth->stencil_pass = gpu_stencil_operation(rs[D3DRS_STENCILPASS], "D3DRS_STENCILPASS");
		depth->stencil_read_mask = (uint32_t)rs[D3DRS_STENCILMASK];
		depth->stencil_write_mask = (uint32_t)rs[D3DRS_STENCILWRITEMASK];
		memcpy(cache->depth_inputs, inputs, sizeof(inputs));
		cache->depth_valid = TRUE;
	}
	*out = cache->depth;
	out->stencil_reference = (uint32_t)rs[D3DRS_STENCILREF];
}

void xbox_state_blend(struct xbox_state_cache *cache, struct draw_profile *profile,
	const DWORD *rs, struct gpu_blend_state *out)
{
	assert(cache != NULL);
	assert(profile != NULL);
	assert(rs != NULL);
	assert(out != NULL);
	DWORD write = rs[D3DRS_COLORWRITEENABLE];
	DWORD inputs[] = { rs[D3DRS_ALPHABLENDENABLE], rs[D3DRS_SRCBLEND], rs[D3DRS_DESTBLEND],
		rs[D3DRS_BLENDOP], write };
	_Static_assert(sizeof(inputs) == sizeof(cache->blend_inputs), "complete blend snapshot");
	if (!draw_profile_reuse(profile, DRAW_CACHE_BLEND, cache->blend_valid &&
		!memcmp(cache->blend_inputs, inputs, sizeof(inputs))))
	{
		struct gpu_blend_state *blend = &cache->blend;
		memset(blend, 0, sizeof(*blend));
		blend->enable = rs[D3DRS_ALPHABLENDENABLE] != 0;
		blend->source = gpu_blend_factor(rs[D3DRS_SRCBLEND], "D3DRS_SRCBLEND");
		blend->destination = gpu_blend_factor(rs[D3DRS_DESTBLEND], "D3DRS_DESTBLEND");
		blend->operation = gpu_blend_operation(rs[D3DRS_BLENDOP]);
		blend->color_write_mask = (uint8_t)(((write & D3DCOLORWRITEENABLE_RED) ? 1 : 0) |
			((write & D3DCOLORWRITEENABLE_GREEN) ? 2 : 0) |
			((write & D3DCOLORWRITEENABLE_BLUE) ? 4 : 0) |
			((write & D3DCOLORWRITEENABLE_ALPHA) ? 8 : 0));
		memcpy(cache->blend_inputs, inputs, sizeof(inputs));
		cache->blend_valid = TRUE;
	}
	*out = cache->blend;
	out->color = (uint32_t)rs[D3DRS_BLENDCOLOR];
}

void xbox_state_raster(struct xbox_state_cache *cache, struct draw_profile *profile,
	const DWORD *rs, BOOL clip_y_flip, struct gpu_raster_state *out)
{
	assert(cache != NULL);
	assert(profile != NULL);
	assert(rs != NULL);
	assert(out != NULL);
	DWORD inputs[] = { rs[D3DRS_CULLMODE], rs[D3DRS_FRONTFACE], rs[D3DRS_FILLMODE],
		rs[D3DRS_SOLIDOFFSETENABLE], rs[D3DRS_POLYGONOFFSETZSLOPESCALE],
		rs[D3DRS_POLYGONOFFSETZOFFSET], clip_y_flip };
	_Static_assert(sizeof(inputs) == sizeof(cache->raster_inputs), "complete raster snapshot");
	if (!draw_profile_reuse(profile, DRAW_CACHE_RASTER, cache->raster_valid &&
		!memcmp(cache->raster_inputs, inputs, sizeof(inputs))))
	{
		struct gpu_raster_state *raster = &cache->raster;
		memset(raster, 0, sizeof(*raster));
		/* CULLMODE names discarded winding; clip-y reversal also flips it. */
		raster->cull_mode = rs[D3DRS_CULLMODE] == D3DCULL_NONE ? GPU_CULL_NONE :
			rs[D3DRS_CULLMODE] == rs[D3DRS_FRONTFACE] ? GPU_CULL_FRONT : GPU_CULL_BACK;
		raster->front_face = (rs[D3DRS_FRONTFACE] == D3DFRONT_CCW) != (clip_y_flip != 0) ?
			GPU_FRONT_COUNTER_CLOCKWISE : GPU_FRONT_CLOCKWISE;
		raster->fill_mode = rs[D3DRS_FILLMODE] == D3DFILL_WIREFRAME ? GPU_FILL_LINE :
			rs[D3DRS_FILLMODE] == D3DFILL_POINT ? GPU_FILL_POINT : GPU_FILL_SOLID;
		raster->depth_bias_enable = rs[D3DRS_SOLIDOFFSETENABLE] != 0;
		raster->depth_bias_slope = state_float(rs[D3DRS_POLYGONOFFSETZSLOPESCALE]);
		raster->depth_bias_constant = state_float(rs[D3DRS_POLYGONOFFSETZOFFSET]);
		memcpy(cache->raster_inputs, inputs, sizeof(inputs));
		cache->raster_valid = TRUE;
	}
	*out = cache->raster;
}

void xbox_state_sampler(struct xbox_state_cache *cache, struct draw_profile *profile,
	unsigned stage, const DWORD *state, BOOL mipmapped, BOOL hires, uint32_t anisotropy,
	struct gpu_sampler_state *out)
{
	assert(cache != NULL);
	assert(profile != NULL);
	assert(state != NULL);
	assert(out != NULL);
	assert(stage < GPU_STAGE_COUNT);
	assert(anisotropy >= 1);
	assert(anisotropy <= 16);
	DWORD inputs[] = { state[D3DTSS_MINFILTER], state[D3DTSS_MIPFILTER],
		state[D3DTSS_MAGFILTER], state[D3DTSS_ADDRESSU], state[D3DTSS_ADDRESSV],
		state[D3DTSS_ADDRESSW], state[D3DTSS_MAXMIPLEVEL], state[D3DTSS_MAXANISOTROPY],
		state[D3DTSS_MIPMAPLODBIAS], state[D3DTSS_BORDERCOLOR], mipmapped, hires, anisotropy };
	_Static_assert(sizeof(inputs) == sizeof(cache->samplers[0].inputs), "complete sampler snapshot");
	if (!draw_profile_reuse(profile, DRAW_CACHE_SAMPLER, cache->samplers[stage].valid &&
		!memcmp(cache->samplers[stage].inputs, inputs, sizeof(inputs))))
	{
		struct gpu_sampler_state *sampler = &cache->samplers[stage].state;
		memset(sampler, 0, sizeof(*sampler));
		sampler->min_filter = hires ? GPU_FILTER_LINEAR : gpu_filter(state[D3DTSS_MINFILTER], "D3DTSS_MINFILTER", stage);
		sampler->mip_filter = hires ? GPU_FILTER_LINEAR : mipmapped ?
			gpu_filter(state[D3DTSS_MIPFILTER], "D3DTSS_MIPFILTER", stage) : GPU_FILTER_NONE;
		sampler->mag_filter = hires ? GPU_FILTER_LINEAR : gpu_filter(state[D3DTSS_MAGFILTER], "D3DTSS_MAGFILTER", stage);
		sampler->address_u = gpu_address(state[D3DTSS_ADDRESSU], "D3DTSS_ADDRESSU", stage);
		sampler->address_v = gpu_address(state[D3DTSS_ADDRESSV], "D3DTSS_ADDRESSV", stage);
		sampler->address_w = gpu_address(state[D3DTSS_ADDRESSW], "D3DTSS_ADDRESSW", stage);
		sampler->max_mip_level = hires ? 0 : (uint32_t)state[D3DTSS_MAXMIPLEVEL];
		sampler->max_anisotropy = (uint32_t)state[D3DTSS_MAXANISOTROPY];
		sampler->lod_bias = hires ? 0.0f : state_float(state[D3DTSS_MIPMAPLODBIAS]);
		sampler->border_color = (uint32_t)state[D3DTSS_BORDERCOLOR];
		if (anisotropy > 1 && sampler->min_filter == GPU_FILTER_LINEAR &&
			sampler->mip_filter != GPU_FILTER_NONE)
		{
			sampler->min_filter = GPU_FILTER_ANISOTROPIC;
			sampler->max_anisotropy = anisotropy;
		}
		memcpy(cache->samplers[stage].inputs, inputs, sizeof(inputs));
		cache->samplers[stage].valid = TRUE;
	}
	*out = cache->samplers[stage].state;
}

gpu_shader xbox_state_fragment(struct xbox_state_cache *cache, struct draw_profile *profile,
	const struct nv2a_pixel_shader_key *key,
	gpu_shader (*resolve)(const struct nv2a_pixel_shader_key *))
{
	assert(cache != NULL);
	assert(profile != NULL);
	assert(key != NULL);
	assert(resolve != NULL);
	if (!draw_profile_reuse(profile, DRAW_CACHE_FRAGMENT, cache->fragment &&
		!memcmp(&cache->fragment_key, key, sizeof(*key))))
	{
		cache->fragment = resolve(key);
		cache->fragment_key = *key;
	}
	return cache->fragment;
}
