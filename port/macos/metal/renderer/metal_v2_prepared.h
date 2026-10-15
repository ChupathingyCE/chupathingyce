/* Mac V2 frontend preparation. No native objects, guest memory or allocations.
The device owns one zero-initialized cache, used only on its rendering thread.
Reset before device initialization or shader-handle destruction/reuse. Outputs
are copies: HUD, depth-clamp and shader specialization edits stay draw-local.

Every call observes the supplied live Xbox words, never setter dirty bits.
Render/stage pointers must span D3DRS_MAX/D3DTSS_MAX words respectively and
remain valid for the call only; no pointers into those arrays are retained.
Texture contents, targets, overrides, uniforms and geometry remain the caller's
responsibility. Shader handles are borrowed until reset, not owned here. */
#ifndef METAL_V2_PREPARED_H
#define METAL_V2_PREPARED_H

#include "xgpu.h"
#include "draw_profile.h"

/* Fixed storage exposed for allocation, not for caller inspection/mutation. */
struct metal_v2_prepared
{
	DWORD depth_inputs[11], blend_inputs[5], raster_inputs[7];
	struct gpu_depth_stencil_state depth;
	struct gpu_blend_state blend;
	struct gpu_raster_state raster;
	BOOL depth_valid, blend_valid, raster_valid;
	struct
	{
		DWORD inputs[13];
		struct gpu_sampler_state state;
		BOOL valid;
	} samplers[GPU_STAGE_COUNT];
	struct nv2a_pixel_shader_key fragment_key;
	gpu_shader fragment;
};

void metal_v2_prepared_reset(struct metal_v2_prepared *cache);
/* Dynamic stencil reference/blend color are read on hits too. */
void metal_v2_prepared_depth(struct metal_v2_prepared *cache, struct draw_profile *profile,
	const DWORD *render, BOOL has_depth, struct gpu_depth_stencil_state *out);
void metal_v2_prepared_blend(struct metal_v2_prepared *cache, struct draw_profile *profile,
	const DWORD *render, struct gpu_blend_state *out);
void metal_v2_prepared_raster(struct metal_v2_prepared *cache, struct draw_profile *profile,
	const DWORD *render, BOOL clip_y_flip, struct gpu_raster_state *out);
/* Anisotropy is an explicit normalized policy input in [1,16]. */
void metal_v2_prepared_sampler(struct metal_v2_prepared *cache, struct draw_profile *profile,
	unsigned stage, const DWORD *state, BOOL mipmapped, BOOL hires, uint32_t anisotropy,
	struct gpu_sampler_state *out);
/* Resolver is synchronous and called only on misses. Zero means failure and
is never reused. Do not recursively prepare draws from the resolver. */
gpu_shader metal_v2_prepared_fragment(struct metal_v2_prepared *cache, struct draw_profile *profile,
	const struct nv2a_pixel_shader_key *key,
	gpu_shader (*resolve)(const struct nv2a_pixel_shader_key *));

#endif
