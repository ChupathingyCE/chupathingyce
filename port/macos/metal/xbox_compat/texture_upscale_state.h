/* Upscaled textures, per level (spec 2026-10-04 "Cache and versioning", "Settings"): a level loads upscaled textures
only if the switch was on and nothing asked for originals when it loaded; a skipped or low-storage level loads none,
not even cached ones, so it never shows upscaled and original textures side by side. Header-only for a host probe. */
#pragma once

struct texture_upscale_state
{
	int switch_on, original_requested, level_enabled;
};

/* at every map load: the switch's value now, and whether piece 3 asked for originals (a skip, low storage, a failed
   write) before this load; the level keeps what this decides until the next map load */
static inline void texture_upscale_state_map_loaded(struct texture_upscale_state *s, int switch_on)
{
	s->switch_on = switch_on;
	s->level_enabled = switch_on && !s->original_requested;
	s->original_requested = 0;
}

static inline void texture_upscale_state_request_original(struct texture_upscale_state *s)
{
	s->original_requested = 1;
}

static inline int texture_upscale_state_enabled(const struct texture_upscale_state *s)
{
	return s->level_enabled;
}
