/*
METAL_STATE_CACHE.H

What the open render command encoder holds, so the Metal backend
(gpu_metal.m) can skip an encoder call that sets a slot to the value it
already has. A render command encoder keeps its state from draw to draw, so
skipping such a call changes nothing a draw sees; it saves the driver's work
(and, under --metal-validation, the validation layer's) for each of the 36
calls a draw would otherwise make again.

A slot holds the bytes last set (an object's address and offset, or a value)
and whether they are known. The backend forgets everything when an encoder
begins or ends (metal_state_reset), and the slots the clear quad sets behind
its back after a clear (metal_state_clear_quad). Changing the pipeline doesn't
forget the bindings, because Metal keeps them across pipelines.

Comparing objects by address is safe while the encoder is open: the command
buffer retains what is bound to it (retainedReferences), so no new object can
take a bound one's address before the encoder ends and the cache is reset.

Plain C, for port/ios/tests/metal_state_probe.c.
*/
#ifndef METAL_STATE_CACHE_H
#define METAL_STATE_CACHE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum
{
	METAL_STATE_PIPELINE,
	METAL_STATE_DEPTH_STENCIL,
	METAL_STATE_STENCIL_REFERENCE,
	METAL_STATE_VIEWPORT,
	METAL_STATE_SCISSOR,
	METAL_STATE_CULL_MODE,
	METAL_STATE_WINDING,
	METAL_STATE_DEPTH_BIAS,
	METAL_STATE_BLEND_COLOR,
	/* the fragment textures and samplers of the 4 stages */
	METAL_STATE_TEXTURE,
	METAL_STATE_SAMPLER = METAL_STATE_TEXTURE + 4,
	/* the constants: vertex buffers 0 and 1 and fragment buffer 0 */
	METAL_STATE_VERTEX_BUFFER = METAL_STATE_SAMPLER + 4,
	METAL_STATE_FRAGMENT_BUFFER = METAL_STATE_VERTEX_BUFFER + 2,
	/* the 16 attributes' vertex buffers (VERTEX_STREAM_BINDING onward) */
	METAL_STATE_ATTRIBUTE_BUFFER,
	METAL_STATE_SLOTS = METAL_STATE_ATTRIBUTE_BUFFER + 16,
};

/* the calls a draw makes with nothing skipped: a slot each, the attribute
table and the draw (and, opening a visibility test, a visibility mode) */
enum { METAL_STATE_DRAW_CALLS = METAL_STATE_SLOTS + 2 };

/* the largest value: a viewport's 6 doubles */
#define METAL_STATE_VALUE_BYTES 48

struct metal_state_cache
{
	int enabled;
	uint64_t known; /* a bit per slot */
	unsigned char values[METAL_STATE_SLOTS][METAL_STATE_VALUE_BYTES];
	/* since the last metal_state_take_counts: the draws, and their encoder
	calls made and skipped */
	unsigned long draws, issued, skipped;
};

_Static_assert(METAL_STATE_SLOTS <= 64, "a bit per slot");

static inline void metal_state_initialize(struct metal_state_cache *cache, int enabled)
{
	memset(cache, 0, sizeof(*cache));
	cache->enabled = enabled;
}

/* a new or ended encoder: nothing is known */
static inline void metal_state_reset(struct metal_state_cache *cache)
{
	cache->known = 0;
}

static inline void metal_state_forget(struct metal_state_cache *cache, int slot)
{
	cache->known &= ~((uint64_t)1 << slot);
}

/* whether to make the call that sets slot to value (size bytes); 0 when the
encoder already holds it */
static inline int metal_state_value(struct metal_state_cache *cache, int slot, const void *value, size_t size)
{
	uint64_t bit = (uint64_t)1 << slot;

	if (cache->enabled && (cache->known & bit) && !memcmp(cache->values[slot], value, size))
	{
		cache->skipped++;
		return 0;
	}
	memcpy(cache->values[slot], value, size);
	cache->known |= bit;
	cache->issued++;
	return 1;
}

/* the same for an object (an id's address) bound at an offset */
static inline int metal_state_object(struct metal_state_cache *cache, int slot, const void *object, uint64_t offset)
{
	struct { const void *object; uint64_t offset; } value;

	memset(&value, 0, sizeof(value));
	value.object = object;
	value.offset = offset;
	return metal_state_value(cache, slot, &value, sizeof(value));
}

/* a call made every time (setVertexBytes, the draw itself), for the counts */
static inline void metal_state_always(struct metal_state_cache *cache)
{
	cache->issued++;
}

static inline void metal_state_draw(struct metal_state_cache *cache)
{
	cache->draws++;
}

/* what gpu_metal_clear's quad sets without the cache */
static inline void metal_state_clear_quad(struct metal_state_cache *cache)
{
	static const int slots[] = { METAL_STATE_PIPELINE, METAL_STATE_DEPTH_STENCIL, METAL_STATE_STENCIL_REFERENCE,
		METAL_STATE_CULL_MODE, METAL_STATE_DEPTH_BIAS, METAL_STATE_VIEWPORT, METAL_STATE_SCISSOR,
		METAL_STATE_VERTEX_BUFFER, METAL_STATE_FRAGMENT_BUFFER };
	size_t index;

	for (index = 0; index < sizeof(slots) / sizeof(slots[0]); index++)
		metal_state_forget(cache, slots[index]);
}

/* starts the counts again, after a report */
static inline void metal_state_take_counts(struct metal_state_cache *cache)
{
	cache->draws = cache->issued = cache->skipped = 0;
}

#endif
