/* Phase 0 of docs/metal-v2.md: CPU time per draw, by stage, behind
debug.gpu_stats. Disabled it costs one predictable branch per stage.
Stages nest: DRAW_PROFILE_CALL includes the others. Times are inclusive wall
clock, so include any preemption; use a sampling profiler to attribute inside
a stage. */
#ifndef DRAW_PROFILE_H
#define DRAW_PROFILE_H

#include <stdint.h>
#include <time.h>

enum draw_profile_stage
{
	DRAW_PROFILE_CALL,    /* the whole D3DDevice_Draw* call */
	DRAW_PROFILE_PREPARE, /* prepare_draw */
	DRAW_PROFILE_STREAMS, /* setup_streams */
	DRAW_PROFILE_SUBMIT,  /* gpu_draw: dispatch + gpu_metal_draw */
	/* inside prepare_draw: */
	DRAW_PROFILE_STAGES_FILL, /* stages_fill */
	DRAW_PROFILE_KEY,         /* the pixel shader key but for stages_fill */
	DRAW_PROFILE_SHADERS,     /* vertex_shader_get + fragment_shader_get */
	DRAW_PROFILE_UNIFORMS,    /* the uniform inputs, compared and converted */
	DRAW_PROFILE_TARGETS,     /* draw_targets */
	DRAW_PROFILE_RASTER,      /* raster_state_fill */
	/* inside setup_streams: */
	DRAW_PROFILE_MIRROR,         /* mirror_range, with its refresh */
	DRAW_PROFILE_MIRROR_REFRESH, /* mirror_refresh alone: protect + upload of stale pages */
	DRAW_PROFILE_STREAM_UPLOAD,  /* the streaming fallback's copy (stream_upload*) */
	DRAW_PROFILE_FRONTEND_LAST = DRAW_PROFILE_STREAM_UPLOAD,
	/* inside gpu_metal_draw (gpu_metal.m keeps its own struct draw_profile) */
	DRAW_PROFILE_PIPELINE,    /* draw_attributes_decide + draw_pipeline */
	DRAW_PROFILE_PASS,        /* pass_begin and target_size */
	DRAW_PROFILE_DEPTH_STATE, /* depth_state */
	DRAW_PROFILE_DYNAMIC,     /* visibility, pipeline set and the dynamic encoder state */
	DRAW_PROFILE_BIND_STAGES,
	DRAW_PROFILE_BIND_CONSTANTS,
	DRAW_PROFILE_BIND_ATTRIBUTES,
	DRAW_PROFILE_ENCODE,      /* the Metal draw call itself */
	DRAW_PROFILE_STAGES
};

struct draw_profile
{
	int enabled;
	uint64_t nanoseconds[DRAW_PROFILE_STAGES];
	uint64_t worst[DRAW_PROFILE_STAGES];
	unsigned long calls[DRAW_PROFILE_STAGES];
	/* setVertexBytes of the attribute table: made, and skipped when unchanged */
	unsigned long attribute_table_binds;
};

static inline uint64_t draw_profile_now(const struct draw_profile *profile)
{
	struct timespec now;

	if (!profile->enabled)
		return 0;
	clock_gettime(CLOCK_MONOTONIC_RAW, &now);
	return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static inline void draw_profile_add(struct draw_profile *profile, enum draw_profile_stage stage, uint64_t started)
{
	uint64_t elapsed;

	if (!profile->enabled)
		return;
	elapsed = draw_profile_now(profile) - started;
	profile->nanoseconds[stage] += elapsed;
	profile->calls[stage]++;
	if (elapsed > profile->worst[stage])
		profile->worst[stage] = elapsed;
}

/* adds the time since *started to stage and restarts the clock, for consecutive stages */
static inline void draw_profile_lap(struct draw_profile *profile, enum draw_profile_stage stage, uint64_t *started)
{
	uint64_t now;

	if (!profile->enabled)
		return;
	now = draw_profile_now(profile);
	profile->nanoseconds[stage] += now - *started;
	profile->calls[stage]++;
	if (now - *started > profile->worst[stage])
		profile->worst[stage] = now - *started;
	*started = now;
}

#endif
