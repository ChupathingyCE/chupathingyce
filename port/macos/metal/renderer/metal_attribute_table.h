/* Vertex-pulling ABI shared with nv2a_msl.c. The table contains offsets and
values, not Metal object identities: buffer renaming must still bind and mark
resources on every draw. Plain C so transition tests need no Metal device. */
#ifndef METAL_ATTRIBUTE_TABLE_H
#define METAL_ATTRIBUTE_TABLE_H

#include "gpu.h"
#include <assert.h>
#include <string.h> /* IWYU pragma: keep -- memcpy/memcmp in inline functions. */

struct metal_attribute
{
	uint32_t format, stream, offset, stride;
};

struct metal_attribute_table
{
	struct metal_attribute entries[GPU_ATTRIBUTE_COUNT];
	float constants[GPU_ATTRIBUTE_COUNT][4];
};

_Static_assert(GPU_ATTRIBUTE_COUNT == 16, "vertex-pulling shader ABI has 16 attributes");
_Static_assert(sizeof(struct metal_attribute_table) == 512, "AttributeTable MSL layout");

static inline uint32_t metal_attribute_size(uint8_t format)
{
	if (format >= GPU_ATTRIBUTE_FLOAT1 && format <= GPU_ATTRIBUTE_FLOAT4)
		return 4u * (format - GPU_ATTRIBUTE_FLOAT1 + 1);
	if (format >= GPU_ATTRIBUTE_SHORT1 && format <= GPU_ATTRIBUTE_SHORT4)
		return 2u * (format - GPU_ATTRIBUTE_SHORT1 + 1);
	if (format >= GPU_ATTRIBUTE_NORMSHORT1 && format <= GPU_ATTRIBUTE_NORMSHORT4)
		return 2u * (format - GPU_ATTRIBUTE_NORMSHORT1 + 1);
	if (format >= GPU_ATTRIBUTE_UBYTE1 && format <= GPU_ATTRIBUTE_UBYTE4)
		return format - GPU_ATTRIBUTE_UBYTE1 + 1u;
	return 4;
}

/* Update only changed effective entries. records is a bit per live buffer,
resolved afresh by draw_attributes_decide, not a declaration-only identity.
Unused constants are zeroed on transition for deterministic table bytes. */
static inline int metal_attribute_table_update(struct metal_attribute_table *table,
	const struct gpu_draw *draw, uint32_t records)
{
	assert(table != NULL);
	assert(draw != NULL);
	assert((records >> GPU_ATTRIBUTE_COUNT) == 0);
	int changed = 0;
	static const float zero[4];

	for (unsigned index = 0; index < GPU_ATTRIBUTE_COUNT; index++)
	{
		const struct gpu_vertex_attribute *attribute = &draw->attributes[index];
		struct metal_attribute entry = { attribute->format, GPU_STREAM_CONSTANT, 0, 0 };
		const float *constant = draw->constant_values[index];

		if (records & (1u << index))
		{
			assert(attribute->stream < GPU_ATTRIBUTE_COUNT);
			const struct gpu_vertex_stream *stream = &draw->streams[attribute->stream];

			entry.stream = attribute->stream;
			entry.offset = stream->offset + attribute->offset;
			entry.stride = stream->stride ? stream->stride : metal_attribute_size(attribute->format);
			constant = zero;
		}
		if (memcmp(&table->entries[index], &entry, sizeof(entry)))
		{
			table->entries[index] = entry;
			changed = 1;
		}
		if (memcmp(table->constants[index], constant, sizeof(table->constants[index])))
		{
			memcpy(table->constants[index], constant, sizeof(table->constants[index]));
			changed = 1;
		}
	}
	return changed;
}

/* Last layout and effective table, excluding geometry bytes and buffer IDs.
Live buffer records are resolved separately, so destruction/reuse cannot
preserve stale presence. The two lists partition all 16 slots; storage is
bounded and owns no native resources. */
struct metal_attribute_cache
{
	struct metal_attribute_table table;
	struct gpu_vertex_attribute attributes[GPU_ATTRIBUTE_COUNT];
	uint32_t records;
	uint8_t streamed[GPU_ATTRIBUTE_COUNT], streamed_count;
	uint8_t constant_slots[GPU_ATTRIBUTE_COUNT], constant_count;
	int valid;
};

static inline int metal_attribute_table_prepare(struct metal_attribute_cache *cache,
	const struct gpu_draw *draw, uint32_t records)
{
	assert(cache != NULL);
	assert(draw != NULL);
	assert((records >> GPU_ATTRIBUTE_COUNT) == 0);
	int changed = 0;

	if (!cache->valid || cache->records != records ||
		memcmp(cache->attributes, draw->attributes, sizeof(cache->attributes)))
	{
		/* A layout/presence transition rebuilds the format plan, not geometry. */
		memcpy(cache->attributes, draw->attributes, sizeof(cache->attributes));
		cache->records = records;
		cache->valid = 1;
		cache->streamed_count = 0;
		cache->constant_count = 0;
		for (unsigned index = 0; index < GPU_ATTRIBUTE_COUNT; index++)
		{
			if (records & (1u << index))
				cache->streamed[cache->streamed_count++] = (uint8_t)index;
			else
				cache->constant_slots[cache->constant_count++] = (uint8_t)index;
		}
		assert(cache->streamed_count + cache->constant_count == GPU_ATTRIBUTE_COUNT);
		return metal_attribute_table_update(&cache->table, draw, records);
	}
	/* Only effective inputs matter: streamed constants and unused streams
	cannot change table bytes. Buffer identity is handled by bind_attributes,
	not this value-only cache. Each list has at most 16 entries. */
	for (unsigned item = 0; item < cache->constant_count; item++)
	{
		unsigned index = cache->constant_slots[item];
		if (memcmp(cache->table.constants[index], draw->constant_values[index],
			sizeof(cache->table.constants[index])))
		{
			memcpy(cache->table.constants[index], draw->constant_values[index],
				sizeof(cache->table.constants[index]));
			changed = 1;
		}
	}
	for (unsigned range_item = 0; range_item < cache->streamed_count; range_item++)
	{
		unsigned index = cache->streamed[range_item];
		const struct gpu_vertex_attribute *attribute = &cache->attributes[index];
		const struct gpu_vertex_stream *stream = &draw->streams[attribute->stream];
		struct metal_attribute *entry = &cache->table.entries[index];
		uint32_t offset = stream->offset + attribute->offset;
		uint32_t stride = stream->stride ? stream->stride : metal_attribute_size(attribute->format);

		if (entry->offset != offset || entry->stride != stride)
		{
			entry->offset = offset;
			entry->stride = stride;
			changed = 1;
		}
	}
	return changed;
}

#endif
