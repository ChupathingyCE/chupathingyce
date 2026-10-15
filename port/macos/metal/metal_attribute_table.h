/* Vertex-pulling ABI shared with nv2a_msl.c. The table contains offsets and
values, not Metal object identities: buffer renaming must still bind and mark
resources on every draw. Plain C so transition tests need no Metal device. */
#ifndef METAL_ATTRIBUTE_TABLE_H
#define METAL_ATTRIBUTE_TABLE_H

#include "renderer/gpu.h"
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

/* Last effective packet inputs, excluding geometry bytes. Live buffer
records are resolved separately, so destruction/reuse cannot preserve a stale
streamed-versus-constant interpretation. Bounded storage, no retained objects. */
struct metal_attribute_cache
{
	struct metal_attribute_table table;
	struct gpu_vertex_attribute attributes[GPU_ATTRIBUTE_COUNT];
	struct gpu_vertex_stream streams[GPU_ATTRIBUTE_COUNT];
	float constants[GPU_ATTRIBUTE_COUNT][4];
	uint32_t records;
	uint8_t streamed[GPU_ATTRIBUTE_COUNT], streamed_count;
	int valid;
};

static inline int metal_attribute_table_prepare(struct metal_attribute_cache *cache,
	const struct gpu_draw *draw, uint32_t records)
{
	int changed = 0;

	if (!cache->valid || cache->records != records ||
		memcmp(cache->attributes, draw->attributes, sizeof(cache->attributes)))
	{
		/* A layout/presence transition rebuilds the format plan, not geometry. */
		memcpy(cache->attributes, draw->attributes, sizeof(cache->attributes));
		memcpy(cache->streams, draw->streams, sizeof(cache->streams));
		memcpy(cache->constants, draw->constant_values, sizeof(cache->constants));
		cache->records = records;
		cache->valid = 1;
		cache->streamed_count = 0;
		for (unsigned index = 0; index < GPU_ATTRIBUTE_COUNT; index++)
			if (records & (1u << index))
				cache->streamed[cache->streamed_count++] = (uint8_t)index;
		return metal_attribute_table_update(&cache->table, draw, records);
	}
	if (memcmp(cache->constants, draw->constant_values, sizeof(cache->constants)))
	{
		memcpy(cache->constants, draw->constant_values, sizeof(cache->constants));
		for (unsigned index = 0; index < GPU_ATTRIBUTE_COUNT; index++)
		{
			if (!(records & (1u << index)) && memcmp(cache->table.constants[index],
				draw->constant_values[index], sizeof(cache->table.constants[index])))
			{
				memcpy(cache->table.constants[index], draw->constant_values[index], sizeof(cache->table.constants[index]));
				changed = 1;
			}
		}
	}
	if (memcmp(cache->streams, draw->streams, sizeof(cache->streams)))
	{
		memcpy(cache->streams, draw->streams, sizeof(cache->streams));
		/* Dynamic ranges only touch the streamed slots; unchanged constants
		and formats must not be walked/reconstructed again. */
		for (unsigned item = 0; item < cache->streamed_count; item++)
		{
			unsigned index = cache->streamed[item];
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
	}
	return changed;
}

#endif
