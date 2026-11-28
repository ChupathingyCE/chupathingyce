/*
POSIX_MODERN_LIGHTING_RT.C

Display.ray_tracing's Vulkan side path
(modern_lighting.h), with the host's ABI (posix.h): Vulkan's handles and
sizes are 64-bit.

The game draws with OpenGL; this makes a Vulkan device on the same GPU (the
one whose deviceUUID is OpenGL's GL_DEVICE_UUID_EXT) with
VK_KHR_acceleration_structure and VK_KHR_ray_query, and shares four images
with OpenGL (VK_KHR_external_memory_fd / GL_EXT_memory_object_fd) and two
semaphores (VK_KHR_external_semaphore_fd / GL_EXT_semaphore_fd):

- OpenGL writes each pixel's position from the camera and its normal
  (modern_lighting.c), and signals the first semaphore;
- this waits on it, skins the objects that moved, refits (or builds) their
  bottom-level structures, builds the frame's top-level one, runs
  port/assets/shaders/modern_lighting/rt_trace.comp over the view, and
  signals the second;
- OpenGL waits on that, and blurs the result and multiplies the view by it.

The scene is the render geometry (modern_lighting_scene.c gathers it):
- the level: the structure BSP's materials, a geometry each, built once a
  BSP (ml_rt_level_*), the alpha-tested ones (grates, foliage) not opaque;
- the model parts: each part's vertices and triangles as the map keeps them,
  copied once when first seen (ml_rt_part);
- the objects: a bottom-level structure each, of the parts it draws, built
  from positions port/assets/shaders/modern_lighting/rt_skin.comp skins with
  the frame's node matrices (the vertex shaders' own), refit while the
  parts stay the same and the object moves, built again when they change
  (a permutation, a level of detail) and every REBUILD_UPDATES refits; an
  object that did not move costs nothing;
- the alpha-tested surfaces' textures: their alpha, an R8 image each, in a
  descriptor array the trace samples (ml_rt_texture_*);
- each geometry's record (where its triangles and vertices are, its
  texture), which the trace finds by its instance's shader binding table
  offset plus its geometry's index.
The round-1 collision scene (the level's collision BSP, the objects'
collision models, ml_rt_static_mesh / ml_rt_mesh_*) is kept for comparing
(debug.ray_tracing_geometry "collision").

Memory: large buffers (CHUNK_SIZE) cut into blocks, one kind in device
memory (the skinned positions, the structures) and one the CPU writes (the
copies of the map's vertices and triangles, the records, the instances, the
matrices: device memory the CPU can see where the GPU has it). What the GPU
may still use is freed FRAMES frames later (doomed).

Vulkan is loaded at run time (libvulkan.so.1): a machine without it, or
without ray queries, runs as before, the setting off (ml_rt_probe says
which, for the menus' NOT SUPPORTED). ml_rt_stop takes it all down again
(the setting turned off), and ml_rt_start brings it up.

bgfx's own Vulkan device is not used: bgfx (tools/bgfx_build.py) makes its
device without the ray tracing extensions and their features, keeps its
command buffers to itself and submits a whole frame at once in bgfx::frame()
on its render thread, so no pass of ours could run between the opaque
surfaces and the rest. NOTES.md (the experiment's) has what that would take.

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#if defined(__linux__) && !defined(HALO_ANDROID)

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

#include <dlfcn.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modern_lighting.h"
#include "modern_lighting_rt_spv.h"

void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

#define FRAMES 2
/* each frame's GPU timestamps: its start, the builds done, the trace started, its end */
#define STAMPS 4
#define MAXIMUM_INSTANCES 16384
#define MAXIMUM_MESHES 4096
#define MAXIMUM_TEXTURES 512
#define MAXIMUM_RECORDS 65536
#define MAXIMUM_PARTS 16384
#define MAXIMUM_OBJECT_SLOTS 4096
#define MAXIMUM_PARTS_PER_OBJECT 64
#define MAXIMUM_NODES 64
/* a frame's node matrices (3 rows of 4 floats each) */
#define MAXIMUM_FRAME_MATRICES 65536
#define CHUNK_SIZE (64ull << 20)
/* an object refit this many times is built again (its tree from its pose now) */
#define REBUILD_UPDATES 120
/* the periodic rebuilds a frame at most (the rest refit until a frame with
room): objects that came into view together are not all rebuilt in the
same frame every REBUILD_UPDATES frames, a spike on a small GPU */
#define REBUILDS_PER_FRAME 2
/* the largest texture side kept (the alpha halved until it fits) */
#define MAXIMUM_TEXTURE_SIDE 512

struct buffer
{
	VkBuffer buffer;
	VkDeviceMemory memory;
	VkDeviceSize size;
	VkDeviceAddress address;
	void *mapped;
};

/* free spans of a range (a chunk's bytes, the records) */
struct span
{
	VkDeviceSize offset, size;
};

struct spans
{
	struct span *items;
	int count, capacity;
};

struct chunk
{
	struct buffer buffer;
	struct spans free;
	int host;
};

/* a piece of a chunk */
struct block
{
	int chunk;
	VkDeviceSize offset, size;
	VkDeviceAddress address;
	void *mapped;
};

/* what the GPU may still use, freed FRAMES frames on */
struct doomed
{
	struct block block;
	VkAccelerationStructureKHR structure;
	int record_base, record_count;
	unsigned int frame;
};

/* a collision mesh (the round-1 scene) */
struct mesh
{
	const void *key;
	VkAccelerationStructureKHR structure;
	struct buffer storage;
	VkDeviceAddress address;
	int empty;
};

/* the level's render geometry: a structure of its materials */
struct level
{
	const void *key;
	VkAccelerationStructureKHR structure;
	struct block storage;
	VkDeviceAddress address;
	int record_base, record_count;
	/* (each material's vertices and triangles, kept while the level is) */
	struct block *blocks;
	int block_count;
	int ready;
};

/* a model part, as the map keeps it */
struct part
{
	const void *key;
	struct block vertices, indices;
	int vertex_count, triangle_count, kind;
};

/* an object's structure */
struct object_slot
{
	int used;
	int key;
	int part_count;
	int parts[MAXIMUM_PARTS_PER_OBJECT];
	int textures[MAXIMUM_PARTS_PER_OBJECT];
	float scales[MAXIMUM_PARTS_PER_OBJECT][2];
	int node_count;
	/* the skinned positions: each part's at its offset */
	struct block positions;
	VkDeviceSize part_offsets[MAXIMUM_PARTS_PER_OBJECT];
	VkAccelerationStructureKHR structure;
	struct block storage;
	VkDeviceAddress address;
	VkDeviceSize build_scratch, update_scratch;
	int record_base;
	/* the node matrices it was last skinned with */
	float matrices[MAXIMUM_NODES][12];
	int built, updates;
	unsigned int seen;
	/* this frame: 1 to build, 2 to refit; its matrices in the frame's ring */
	int wanted;
	/* skinned with matrices that never got to the GPU (a frame not traced) */
	int stale;
	int matrix_offset;
	unsigned int custom, mask;
};

/* a key's number (parts, textures): open addressing over pointers */
struct key_map
{
	const void **keys;
	int *values;
	int size, count;
};

/* a record (rt_trace.comp's Record) */
struct record
{
	VkDeviceAddress indices;
	VkDeviceAddress vertices;
	int kind;
	int texture;
	float scale[2];
};

typedef char record_size_assert[sizeof(struct record) == 32 ? 1 : -1];

struct texture
{
	VkImage image;
	VkDeviceMemory memory;
	VkImageView view;
};

static struct
{
	void *library;
	PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
#define VK_FUNCTION(name) PFN_vk##name name;
#define VK_FUNCTIONS(X) \
	X(CreateInstance) X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties2) \
	X(GetPhysicalDeviceFeatures2) X(GetPhysicalDeviceQueueFamilyProperties) X(GetPhysicalDeviceMemoryProperties) \
	X(EnumerateDeviceExtensionProperties) X(CreateDevice) X(DestroyDevice) X(GetDeviceProcAddr) X(GetDeviceQueue) \
	X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) \
	X(ResetCommandBuffer) X(QueueSubmit) X(QueueWaitIdle) X(CreateFence) X(DestroyFence) X(WaitForFences) \
	X(ResetFences) X(CreateSemaphore) X(DestroySemaphore) X(CreateBuffer) X(DestroyBuffer) \
	X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) X(MapMemory) \
	X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(BindImageMemory) X(CreateImageView) \
	X(DestroyImageView) X(CreateSampler) X(DestroySampler) X(CreateShaderModule) X(DestroyShaderModule) \
	X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) X(DestroyPipelineLayout) \
	X(CreateComputePipelines) X(DestroyPipeline) X(CreateDescriptorPool) X(DestroyDescriptorPool) \
	X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CmdBindPipeline) X(CmdBindDescriptorSets) \
	X(CmdPushConstants) X(CmdDispatch) X(CmdPipelineBarrier) X(CmdCopyBufferToImage) X(CreateQueryPool) \
	X(DestroyQueryPool) X(CmdResetQueryPool) X(CmdWriteTimestamp) X(GetQueryPoolResults) X(GetBufferDeviceAddress) \
	X(CreateAccelerationStructureKHR) X(DestroyAccelerationStructureKHR) X(GetAccelerationStructureBuildSizesKHR) \
	X(CmdBuildAccelerationStructuresKHR) X(GetAccelerationStructureDeviceAddressKHR) X(GetMemoryFdKHR) \
	X(GetSemaphoreFdKHR) X(DeviceWaitIdle)
	VK_FUNCTIONS(VK_FUNCTION)
#undef VK_FUNCTION
	VkInstance instance;
	VkPhysicalDevice physical;
	VkDevice device;
	VkQueue queue;
	unsigned int queue_family;
	VkPhysicalDeviceMemoryProperties memory_properties;
	unsigned int scratch_alignment;
	float timestamp_period;
	int integrated;
	char device_name[256];
	VkCommandPool pool;
	VkCommandBuffer commands[FRAMES], one_time;
	VkFence fences[FRAMES], one_time_fence;
	VkSemaphore semaphores[2];
	VkQueryPool queries;
	unsigned int frame;
	int acquired;
	int frames_submitted[FRAMES];
	float gpu_ms, gpu_build_ms, gpu_trace_ms;
	/* the trace */
	VkShaderModule trace_module, skin_module;
	VkDescriptorSetLayout set_layout;
	VkPipelineLayout pipeline_layout, skin_layout;
	VkPipeline pipeline, skin_pipeline;
	VkDescriptorPool descriptor_pool;
	VkDescriptorSet sets[FRAMES];
	/* the shared images */
	int width, height, generation;
	VkImage images[4];
	VkDeviceMemory image_memory[4];
	VkImageView views[4];
	VkDeviceSize image_sizes[4];
	int images_new;
	/* memory */
	struct chunk *chunks;
	int chunk_count;
	struct doomed *doomed;
	int doomed_count, doomed_capacity;
	struct buffer scratch;
	/* the records */
	struct buffer records;
	struct spans record_spans;
	/* the textures (the default one: alpha 1) and their descriptors' sync */
	VkSampler sampler;
	struct texture default_texture;
	struct texture textures[MAXIMUM_TEXTURES];
	int texture_count;
	struct key_map texture_map;
	int textures_synced[FRAMES], textures_written[FRAMES];
	/* the scene */
	unsigned int world;
	struct mesh level_collision;
	struct level level;
	int level_render;
	struct mesh meshes[MAXIMUM_MESHES];
	int mesh_count;
	struct part *parts;
	int part_count;
	struct key_map part_map;
	struct object_slot *slots;
	int *touched;
	int touched_count;
	struct buffer instances[FRAMES];
	struct buffer matrices[FRAMES];
	int matrix_used;
	int instance_count;
	VkAccelerationStructureKHR tlas[FRAMES];
	struct buffer tlas_storage[FRAMES], tlas_scratch[FRAMES];
	int started, failed;
	int traces;
	/* the CPU's time in the fence wait and the submit (ms, summed) */
	double fence_ms, submit_ms;
	int timed;
	/* the last trace's work: objects skinned, built, refit */
	int skinned, built, refit;
	/* this frame's periodic rebuilds so far (REBUILDS_PER_FRAME) */
	int rebuilds;
	/* (the lab's frame-by-frame record) */
	struct ml_rt_frame_stats stats;
} rt;

static VkAccelerationStructureInstanceKHR pending[MAXIMUM_INSTANCES];
/* each pending instance's object slot (-1: a collision mesh's) */
static int pending_slot[MAXIMUM_INSTANCES];

static double clock_ms(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
}

static VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment)
{
	return (value + alignment - 1) / alignment * alignment;
}

/* ---------- spans */

static int spans_insert(struct spans *spans, int at, VkDeviceSize offset, VkDeviceSize size)
{
	if (spans->count == spans->capacity)
	{
		int capacity = spans->capacity ? spans->capacity * 2 : 16;
		struct span *grown = realloc(spans->items, (size_t)capacity * sizeof(*grown));

		if (!grown)
			return 0;
		spans->items = grown;
		spans->capacity = capacity;
	}
	memmove(&spans->items[at + 1], &spans->items[at], (size_t)(spans->count - at) * sizeof(spans->items[0]));
	spans->items[at].offset = offset;
	spans->items[at].size = size;
	spans->count++;
	return 1;
}

/* the first free span that fits, aligned */
static int spans_take(struct spans *spans, VkDeviceSize size, VkDeviceSize alignment, VkDeviceSize *offset)
{
	int index;

	for (index = 0; index < spans->count; index++)
	{
		struct span *span = &spans->items[index];
		VkDeviceSize start = align_up(span->offset, alignment), end = span->offset + span->size;
		VkDeviceSize head, tail;

		if (start + size > end)
			continue;
		head = start - span->offset;
		tail = end - (start + size);
		if (head && tail)
		{
			span->size = head;
			if (!spans_insert(spans, index + 1, start + size, tail))
				return 0;
		}
		else if (head)
			span->size = head;
		else if (tail)
		{
			span->offset = start + size;
			span->size = tail;
		}
		else
		{
			memmove(&spans->items[index], &spans->items[index + 1],
				(size_t)(spans->count - index - 1) * sizeof(spans->items[0]));
			spans->count--;
		}
		*offset = start;
		return 1;
	}
	return 0;
}

static void spans_give(struct spans *spans, VkDeviceSize offset, VkDeviceSize size)
{
	int at = 0;

	if (!size)
		return;
	while (at < spans->count && spans->items[at].offset < offset)
		at++;
	if (!spans_insert(spans, at, offset, size))
		return;
	/* (joined to its neighbors) */
	if (at + 1 < spans->count && spans->items[at].offset + spans->items[at].size == spans->items[at + 1].offset)
	{
		spans->items[at].size += spans->items[at + 1].size;
		memmove(&spans->items[at + 1], &spans->items[at + 2], (size_t)(spans->count - at - 2) * sizeof(spans->items[0]));
		spans->count--;
	}
	if (at > 0 && spans->items[at - 1].offset + spans->items[at - 1].size == spans->items[at].offset)
	{
		spans->items[at - 1].size += spans->items[at].size;
		memmove(&spans->items[at], &spans->items[at + 1], (size_t)(spans->count - at - 1) * sizeof(spans->items[0]));
		spans->count--;
	}
}

/* ---------- key maps */

static unsigned int key_hash(const void *key)
{
	unsigned long long value = (unsigned long long)(size_t)key;

	value ^= value >> 33;
	value *= 0xff51afd7ed558ccdull;
	value ^= value >> 33;
	return (unsigned int)value;
}

static int key_map_find(const struct key_map *map, const void *key)
{
	unsigned int index;

	if (!map->size)
		return -1;
	for (index = key_hash(key) & (unsigned int)(map->size - 1); map->keys[index]; index = (index + 1) & (unsigned int)(map->size - 1))
	{
		if (map->keys[index] == key)
			return map->values[index];
	}
	return -1;
}

static int key_map_put(struct key_map *map, const void *key, int value)
{
	unsigned int index;

	if (!map->size)
	{
		map->size = 32768;
		map->keys = calloc((size_t)map->size, sizeof(map->keys[0]));
		map->values = calloc((size_t)map->size, sizeof(map->values[0]));
		if (!map->keys || !map->values)
		{
			free(map->keys);
			free(map->values);
			memset(map, 0, sizeof(*map));
			return 0;
		}
	}
	if (map->count >= map->size / 2)
		return 0;
	for (index = key_hash(key) & (unsigned int)(map->size - 1); map->keys[index]; index = (index + 1) & (unsigned int)(map->size - 1))
	{
		if (map->keys[index] == key)
		{
			map->values[index] = value;
			return 1;
		}
	}
	map->keys[index] = key;
	map->values[index] = value;
	map->count++;
	return 1;
}

static void key_map_clear(struct key_map *map)
{
	if (map->size)
	{
		memset(map->keys, 0, (size_t)map->size * sizeof(map->keys[0]));
		map->count = 0;
	}
}

static void key_map_free(struct key_map *map)
{
	free(map->keys);
	free(map->values);
	memset(map, 0, sizeof(*map));
}

/* ---------- memory */

static int memory_type(unsigned int bits, VkMemoryPropertyFlags wanted)
{
	unsigned int index;

	for (index = 0; index < rt.memory_properties.memoryTypeCount; index++)
	{
		if ((bits & (1u << index)) && (rt.memory_properties.memoryTypes[index].propertyFlags & wanted) == wanted)
			return (int)index;
	}
	return -1;
}

/* host: memory the CPU writes (the GPU's own where it lets the CPU see it) */
static int buffer_make(struct buffer *buffer, VkDeviceSize size, VkBufferUsageFlags usage, int host)
{
	VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	VkMemoryAllocateFlagsInfo flags = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
	VkMemoryAllocateInfo allocate = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	VkBufferDeviceAddressInfo address = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
	VkMemoryRequirements requirements;
	int type;

	memset(buffer, 0, sizeof(*buffer));
	info.size = size ? size : 16;
	info.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	if (rt.CreateBuffer(rt.device, &info, NULL, &buffer->buffer) != VK_SUCCESS)
	{
		buffer->buffer = VK_NULL_HANDLE;
		return 0;
	}
	rt.GetBufferMemoryRequirements(rt.device, buffer->buffer, &requirements);
	if (host)
	{
		type = memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (type < 0)
			type = memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
				VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}
	else
		type = memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
	allocate.pNext = &flags;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = (unsigned int)type;
	if (type < 0 || rt.AllocateMemory(rt.device, &allocate, NULL, &buffer->memory) != VK_SUCCESS)
	{
		rt.DestroyBuffer(rt.device, buffer->buffer, NULL);
		memset(buffer, 0, sizeof(*buffer));
		return 0;
	}
	rt.BindBufferMemory(rt.device, buffer->buffer, buffer->memory, 0);
	if (host && rt.MapMemory(rt.device, buffer->memory, 0, VK_WHOLE_SIZE, 0, &buffer->mapped) != VK_SUCCESS)
	{
		rt.DestroyBuffer(rt.device, buffer->buffer, NULL);
		rt.FreeMemory(rt.device, buffer->memory, NULL);
		memset(buffer, 0, sizeof(*buffer));
		return 0;
	}
	address.buffer = buffer->buffer;
	buffer->address = rt.GetBufferDeviceAddress(rt.device, &address);
	buffer->size = info.size;
	return 1;
}

static void buffer_free(struct buffer *buffer)
{
	if (buffer->buffer)
		rt.DestroyBuffer(rt.device, buffer->buffer, NULL);
	if (buffer->memory)
		rt.FreeMemory(rt.device, buffer->memory, NULL);
	memset(buffer, 0, sizeof(*buffer));
}

#define CHUNK_USAGE (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | \
	VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | \
	VK_BUFFER_USAGE_TRANSFER_DST_BIT)

/* a block of size (256-aligned: a structure's storage may start there) */
static int block_take(struct block *block, VkDeviceSize size, int host)
{
	VkDeviceSize offset;
	int index;

	memset(block, 0, sizeof(*block));
	size = align_up(size ? size : 16, 256);
	for (index = 0;; index++)
	{
		if (index == rt.chunk_count)
		{
			struct chunk *grown = realloc(rt.chunks, (size_t)(rt.chunk_count + 1) * sizeof(*grown));
			VkDeviceSize chunk_size = size > CHUNK_SIZE ? size : CHUNK_SIZE;
			double started = clock_ms();

			if (!grown)
				return 0;
			rt.chunks = grown;
			memset(&rt.chunks[index], 0, sizeof(rt.chunks[index]));
			if (!buffer_make(&rt.chunks[index].buffer, chunk_size, CHUNK_USAGE, host))
			{
				platform_log("ray tracing: no memory for a %llu MB buffer", (unsigned long long)(chunk_size >> 20));
				return 0;
			}
			rt.chunks[index].host = host;
			spans_give(&rt.chunks[index].free, 0, chunk_size);
			rt.chunk_count++;
			rt.stats.new_chunks++;
			rt.stats.stall_ms += (float)(clock_ms() - started);
		}
		if (rt.chunks[index].host != host || !spans_take(&rt.chunks[index].free, size, 256, &offset))
			continue;
		block->chunk = index;
		block->offset = offset;
		block->size = size;
		block->address = rt.chunks[index].buffer.address + offset;
		block->mapped = rt.chunks[index].buffer.mapped ? (char *)rt.chunks[index].buffer.mapped + offset : NULL;
		return 1;
	}
}

static void block_give(struct block *block)
{
	if (block->size)
		spans_give(&rt.chunks[block->chunk].free, block->offset, block->size);
	memset(block, 0, sizeof(*block));
}

static int records_take(int count)
{
	VkDeviceSize offset;

	if (count <= 0 || !spans_take(&rt.record_spans, (VkDeviceSize)count, 1, &offset))
		return -1;
	return (int)offset;
}

/* freed once no frame in flight can use it */
static void doom(struct block *block, VkAccelerationStructureKHR structure, int record_base, int record_count)
{
	struct doomed *entry;

	if (rt.doomed_count == rt.doomed_capacity)
	{
		int capacity = rt.doomed_capacity ? rt.doomed_capacity * 2 : 256;
		struct doomed *grown = realloc(rt.doomed, (size_t)capacity * sizeof(*grown));

		if (!grown)
		{
			/* (no room to wait: waited for here) */
			rt.DeviceWaitIdle(rt.device);
			if (block)
				block_give(block);
			if (structure)
				rt.DestroyAccelerationStructureKHR(rt.device, structure, NULL);
			if (record_count > 0)
				spans_give(&rt.record_spans, (VkDeviceSize)record_base, (VkDeviceSize)record_count);
			return;
		}
		rt.doomed = grown;
		rt.doomed_capacity = capacity;
	}
	entry = &rt.doomed[rt.doomed_count++];
	memset(entry, 0, sizeof(*entry));
	if (block)
	{
		entry->block = *block;
		memset(block, 0, sizeof(*block));
	}
	entry->structure = structure;
	entry->record_base = record_base;
	entry->record_count = record_count;
	entry->frame = rt.frame;
}

static void doomed_collect(int all)
{
	int index, kept = 0;

	for (index = 0; index < rt.doomed_count; index++)
	{
		struct doomed *entry = &rt.doomed[index];

		if (!all && entry->frame + FRAMES > rt.frame)
		{
			rt.doomed[kept++] = *entry;
			continue;
		}
		block_give(&entry->block);
		if (entry->structure)
			rt.DestroyAccelerationStructureKHR(rt.device, entry->structure, NULL);
		if (entry->record_count > 0)
			spans_give(&rt.record_spans, (VkDeviceSize)entry->record_base, (VkDeviceSize)entry->record_count);
	}
	rt.doomed_count = kept;
}

/* ---------- starting */

static int instance_functions(void)
{
#define LOAD(name) \
	if (!(rt.name = (PFN_vk##name)rt.GetInstanceProcAddr(rt.instance, "vk" #name))) \
	{ \
		platform_log("ray tracing: Vulkan has no vk%s", #name); \
		return 0; \
	}
	LOAD(DestroyInstance) LOAD(EnumeratePhysicalDevices) LOAD(GetPhysicalDeviceProperties2)
	LOAD(GetPhysicalDeviceFeatures2) LOAD(GetPhysicalDeviceQueueFamilyProperties)
	LOAD(GetPhysicalDeviceMemoryProperties) LOAD(EnumerateDeviceExtensionProperties) LOAD(CreateDevice)
	LOAD(GetDeviceProcAddr)
#undef LOAD
	return 1;
}

static int device_functions(void)
{
#define LOAD(name) \
	if (!(rt.name = (PFN_vk##name)rt.GetDeviceProcAddr(rt.device, "vk" #name))) \
	{ \
		platform_log("ray tracing: the device has no vk%s", #name); \
		return 0; \
	}
	LOAD(DestroyDevice) LOAD(GetDeviceQueue) LOAD(CreateCommandPool) LOAD(DestroyCommandPool)
	LOAD(AllocateCommandBuffers) LOAD(BeginCommandBuffer) LOAD(EndCommandBuffer) LOAD(ResetCommandBuffer)
	LOAD(QueueSubmit) LOAD(QueueWaitIdle) LOAD(CreateFence) LOAD(DestroyFence) LOAD(WaitForFences) LOAD(ResetFences)
	LOAD(CreateSemaphore) LOAD(DestroySemaphore) LOAD(CreateBuffer) LOAD(DestroyBuffer)
	LOAD(GetBufferMemoryRequirements) LOAD(AllocateMemory) LOAD(FreeMemory) LOAD(BindBufferMemory) LOAD(MapMemory)
	LOAD(CreateImage) LOAD(DestroyImage) LOAD(GetImageMemoryRequirements) LOAD(BindImageMemory)
	LOAD(CreateImageView) LOAD(DestroyImageView) LOAD(CreateSampler) LOAD(DestroySampler) LOAD(CreateShaderModule)
	LOAD(DestroyShaderModule) LOAD(CreateDescriptorSetLayout) LOAD(DestroyDescriptorSetLayout)
	LOAD(CreatePipelineLayout) LOAD(DestroyPipelineLayout) LOAD(CreateComputePipelines) LOAD(DestroyPipeline)
	LOAD(CreateDescriptorPool) LOAD(DestroyDescriptorPool) LOAD(AllocateDescriptorSets) LOAD(UpdateDescriptorSets)
	LOAD(CmdBindPipeline) LOAD(CmdBindDescriptorSets) LOAD(CmdPushConstants) LOAD(CmdDispatch)
	LOAD(CmdPipelineBarrier) LOAD(CmdCopyBufferToImage) LOAD(CreateQueryPool) LOAD(DestroyQueryPool)
	LOAD(CmdResetQueryPool) LOAD(CmdWriteTimestamp) LOAD(GetQueryPoolResults) LOAD(GetBufferDeviceAddress)
	LOAD(CreateAccelerationStructureKHR) LOAD(DestroyAccelerationStructureKHR)
	LOAD(GetAccelerationStructureBuildSizesKHR) LOAD(CmdBuildAccelerationStructuresKHR)
	LOAD(GetAccelerationStructureDeviceAddressKHR) LOAD(GetMemoryFdKHR) LOAD(GetSemaphoreFdKHR)
	LOAD(DeviceWaitIdle)
#undef LOAD
	return 1;
}

static const char *const device_extensions[] =
{
	VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
	VK_KHR_RAY_QUERY_EXTENSION_NAME,
	VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
	VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
	VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
};

static int device_has_extensions(VkPhysicalDevice physical)
{
	VkExtensionProperties *properties;
	unsigned int count = 0, index, found = 0, wanted;

	rt.EnumerateDeviceExtensionProperties(physical, NULL, &count, NULL);
	properties = calloc(count ? count : 1, sizeof(*properties));
	if (!properties)
		return 0;
	rt.EnumerateDeviceExtensionProperties(physical, NULL, &count, properties);
	for (wanted = 0; wanted < sizeof(device_extensions) / sizeof(device_extensions[0]); wanted++)
	{
		for (index = 0; index < count; index++)
		{
			if (!strcmp(properties[index].extensionName, device_extensions[wanted]))
			{
				found++;
				break;
			}
		}
	}
	free(properties);
	return found == sizeof(device_extensions) / sizeof(device_extensions[0]);
}

/* the library and an instance; 0 without */
static int vulkan_open(void)
{
	VkApplicationInfo application = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	VkInstanceCreateInfo instance_info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };

	/* (no Vulkan at all, until a device is looked at) */
	ml_rt_why = ML_RT_WHY_NO_VULKAN;

	rt.library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
	if (!rt.library)
	{
		platform_log("ray tracing: no libvulkan.so.1");
		return 0;
	}
	rt.GetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(rt.library, "vkGetInstanceProcAddr");
	if (!rt.GetInstanceProcAddr)
		return 0;
	rt.CreateInstance = (PFN_vkCreateInstance)rt.GetInstanceProcAddr(NULL, "vkCreateInstance");
	if (!rt.CreateInstance)
		return 0;
	application.pApplicationName = "chupathingyce modern lighting";
	application.apiVersion = VK_API_VERSION_1_2;
	instance_info.pApplicationInfo = &application;
	if (rt.CreateInstance(&instance_info, NULL, &rt.instance) != VK_SUCCESS)
	{
		rt.instance = VK_NULL_HANDLE;
		platform_log("ray tracing: no Vulkan 1.2 instance");
		return 0;
	}
	return instance_functions();
}

/* OpenGL's GPU, if it has what the trace needs (rt.physical) */
static int physical_choose(const unsigned char gl_device_uuid[16], int quiet)
{
	VkPhysicalDevice physicals[16];
	unsigned int physical_count = 16, index;

	rt.physical = VK_NULL_HANDLE;
	if (rt.EnumeratePhysicalDevices(rt.instance, &physical_count, physicals) < 0)
		return 0;
	/* (no device of OpenGL's GPU, until one is found) */
	ml_rt_why = ML_RT_WHY_OTHER_GPU;
	for (index = 0; index < physical_count; index++)
	{
		VkPhysicalDeviceIDProperties id = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
		VkPhysicalDeviceProperties2 properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		VkPhysicalDeviceVulkan12Features features12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		VkPhysicalDeviceAccelerationStructureFeaturesKHR structure_features =
			{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
		VkPhysicalDeviceRayQueryFeaturesKHR query_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
		VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		int same, extensions, usable = 0;

		properties.pNext = &id;
		rt.GetPhysicalDeviceProperties2(physicals[index], &properties);
		same = !memcmp(id.deviceUUID, gl_device_uuid, VK_UUID_SIZE);
		extensions = device_has_extensions(physicals[index]);
		if (same && extensions && properties.properties.apiVersion >= VK_API_VERSION_1_2)
		{
			features.pNext = &features12;
			features12.pNext = &structure_features;
			structure_features.pNext = &query_features;
			rt.GetPhysicalDeviceFeatures2(physicals[index], &features);
			usable = features12.bufferDeviceAddress && structure_features.accelerationStructure && query_features.rayQuery;
		}
		if (!quiet)
		{
			platform_log("ray tracing: Vulkan GPU %s: %s, %s", properties.properties.deviceName,
				same ? "OpenGL's" : "not OpenGL's", usable ? "ray queries" : extensions ? "lacks a feature" : "no ray queries");
		}
		if (same && !usable)
			ml_rt_why = ML_RT_WHY_NO_RAY_QUERIES;
		if (same && usable)
		{
			ml_rt_why = ML_RT_WHY_NONE;
			rt.physical = physicals[index];
			snprintf(rt.device_name, sizeof(rt.device_name), "%s", properties.properties.deviceName);
			rt.timestamp_period = properties.properties.limits.timestampPeriod;
			rt.integrated = properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
		}
	}
	return rt.physical != VK_NULL_HANDLE;
}

static void teardown(void);

int ml_rt_why = ML_RT_WHY_NONE;

int ml_rt_probe(const unsigned char gl_device_uuid[16])
{
	int found;

	if (rt.started && !rt.failed)
		return 1;
	if (rt.started)
		return 0;
	found = vulkan_open() && physical_choose(gl_device_uuid, 1);
	if (rt.instance && rt.DestroyInstance)
		rt.DestroyInstance(rt.instance, NULL);
	if (rt.library)
		dlclose(rt.library);
	memset(&rt, 0, sizeof(rt));
	return found;
}

static int pipelines_make(void)
{
	VkShaderModuleCreateInfo module_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	VkDescriptorSetLayoutBinding bindings[7];
	VkDescriptorSetLayoutCreateInfo set_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 112 };
	VkPushConstantRange skin_push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 40 };
	VkPipelineLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	VkComputePipelineCreateInfo pipeline_info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	VkDescriptorPoolSize sizes[4] = {
		{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, FRAMES },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4 * FRAMES },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAXIMUM_TEXTURES * FRAMES },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FRAMES },
	};
	VkDescriptorPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	VkDescriptorSetAllocateInfo set_allocate = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	VkDescriptorSetLayout layouts[FRAMES];
	int index;

	module_info.codeSize = sizeof(modern_lighting_rt_spv);
	module_info.pCode = modern_lighting_rt_spv;
	if (rt.CreateShaderModule(rt.device, &module_info, NULL, &rt.trace_module) != VK_SUCCESS)
		return 0;
	module_info.codeSize = sizeof(modern_lighting_skin_spv);
	module_info.pCode = modern_lighting_skin_spv;
	if (rt.CreateShaderModule(rt.device, &module_info, NULL, &rt.skin_module) != VK_SUCCESS)
		return 0;
	memset(bindings, 0, sizeof(bindings));
	for (index = 0; index < 7; index++)
	{
		bindings[index].binding = (unsigned int)index;
		bindings[index].descriptorType = index == 0 ? VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR :
			index == 5 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : index == 6 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER :
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		bindings[index].descriptorCount = index == 5 ? MAXIMUM_TEXTURES : 1;
		bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}
	set_info.bindingCount = 7;
	set_info.pBindings = bindings;
	if (rt.CreateDescriptorSetLayout(rt.device, &set_info, NULL, &rt.set_layout) != VK_SUCCESS)
		return 0;
	layout_info.setLayoutCount = 1;
	layout_info.pSetLayouts = &rt.set_layout;
	layout_info.pushConstantRangeCount = 1;
	layout_info.pPushConstantRanges = &push;
	if (rt.CreatePipelineLayout(rt.device, &layout_info, NULL, &rt.pipeline_layout) != VK_SUCCESS)
		return 0;
	layout_info.setLayoutCount = 0;
	layout_info.pSetLayouts = NULL;
	layout_info.pPushConstantRanges = &skin_push;
	if (rt.CreatePipelineLayout(rt.device, &layout_info, NULL, &rt.skin_layout) != VK_SUCCESS)
		return 0;
	pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipeline_info.stage.module = rt.trace_module;
	pipeline_info.stage.pName = "main";
	pipeline_info.layout = rt.pipeline_layout;
	if (rt.CreateComputePipelines(rt.device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &rt.pipeline) != VK_SUCCESS)
		return 0;
	pipeline_info.stage.module = rt.skin_module;
	pipeline_info.layout = rt.skin_layout;
	if (rt.CreateComputePipelines(rt.device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &rt.skin_pipeline) != VK_SUCCESS)
		return 0;
	pool_info.maxSets = FRAMES;
	pool_info.poolSizeCount = 4;
	pool_info.pPoolSizes = sizes;
	if (rt.CreateDescriptorPool(rt.device, &pool_info, NULL, &rt.descriptor_pool) != VK_SUCCESS)
		return 0;
	for (index = 0; index < FRAMES; index++)
		layouts[index] = rt.set_layout;
	set_allocate.descriptorPool = rt.descriptor_pool;
	set_allocate.descriptorSetCount = FRAMES;
	set_allocate.pSetLayouts = layouts;
	return rt.AllocateDescriptorSets(rt.device, &set_allocate, rt.sets) == VK_SUCCESS;
}

static void submit_one_time_begin(void)
{
	VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	rt.ResetCommandBuffer(rt.one_time, 0);
	rt.BeginCommandBuffer(rt.one_time, &begin);
}

static int submit_one_time_end(void)
{
	VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	double started = clock_ms();
	int done;

	rt.EndCommandBuffer(rt.one_time);
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &rt.one_time;
	if (rt.QueueSubmit(rt.queue, 1, &submit, rt.one_time_fence) != VK_SUCCESS)
		return 0;
	done = rt.WaitForFences(rt.device, 1, &rt.one_time_fence, VK_TRUE, ~0ull) == VK_SUCCESS;
	rt.ResetFences(rt.device, 1, &rt.one_time_fence);
	rt.stats.stall_ms += (float)(clock_ms() - started);
	return done;
}

/* ---------- the textures */

static void texture_free(struct texture *texture)
{
	if (texture->view)
		rt.DestroyImageView(rt.device, texture->view, NULL);
	if (texture->image)
		rt.DestroyImage(rt.device, texture->image, NULL);
	if (texture->memory)
		rt.FreeMemory(rt.device, texture->memory, NULL);
	memset(texture, 0, sizeof(*texture));
}

/* an R8 image of the alpha, uploaded now (and waited for) */
static int texture_make(struct texture *texture, const unsigned char *alpha, int width, int height)
{
	VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	VkMemoryAllocateInfo allocate = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	VkBufferImageCopy copy;
	VkMemoryRequirements requirements;
	struct buffer staging;
	int type;

	memset(texture, 0, sizeof(*texture));
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = VK_FORMAT_R8_UNORM;
	info.extent.width = (unsigned int)width;
	info.extent.height = (unsigned int)height;
	info.extent.depth = 1;
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (rt.CreateImage(rt.device, &info, NULL, &texture->image) != VK_SUCCESS)
	{
		texture->image = VK_NULL_HANDLE;
		return 0;
	}
	rt.GetImageMemoryRequirements(rt.device, texture->image, &requirements);
	type = memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = (unsigned int)type;
	if (type < 0 || rt.AllocateMemory(rt.device, &allocate, NULL, &texture->memory) != VK_SUCCESS)
	{
		texture->memory = VK_NULL_HANDLE;
		texture_free(texture);
		return 0;
	}
	rt.BindImageMemory(rt.device, texture->image, texture->memory, 0);
	view.image = texture->image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = VK_FORMAT_R8_UNORM;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = 1;
	view.subresourceRange.layerCount = 1;
	if (rt.CreateImageView(rt.device, &view, NULL, &texture->view) != VK_SUCCESS)
	{
		texture->view = VK_NULL_HANDLE;
		texture_free(texture);
		return 0;
	}
	if (!buffer_make(&staging, (VkDeviceSize)width * (VkDeviceSize)height, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 1))
	{
		texture_free(texture);
		return 0;
	}
	memcpy(staging.mapped, alpha, (size_t)width * (size_t)height);
	submit_one_time_begin();
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = texture->image;
	barrier.subresourceRange = view.subresourceRange;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	rt.CmdPipelineBarrier(rt.one_time, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
		NULL, 1, &barrier);
	memset(&copy, 0, sizeof(copy));
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1;
	copy.imageExtent = info.extent;
	rt.CmdCopyBufferToImage(rt.one_time, staging.buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	rt.CmdPipelineBarrier(rt.one_time, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL,
		0, NULL, 1, &barrier);
	if (!submit_one_time_end())
	{
		buffer_free(&staging);
		texture_free(texture);
		return 0;
	}
	buffer_free(&staging);
	return 1;
}

int ml_rt_texture_find(const void *key)
{
	return ml_rt_ready() ? key_map_find(&rt.texture_map, key) : -2;
}

int ml_rt_texture_add(const void *key, const unsigned char *rgba, int width, int height)
{
	unsigned char *alpha;
	int index = -2, x, y;

	if (!ml_rt_ready())
		return -2;
	if (rgba && width > 0 && height > 0 && rt.texture_count < MAXIMUM_TEXTURES &&
		(alpha = malloc((size_t)width * (size_t)height)) != NULL)
	{
		int lowest = 255;

		for (y = 0; y < height; y++)
		{
			for (x = 0; x < width; x++)
			{
				alpha[y * width + x] = rgba[((size_t)y * (size_t)width + (size_t)x) * 4 + 3];
				if (alpha[y * width + x] < lowest)
					lowest = alpha[y * width + x];
			}
		}
		/* (every texel passes the test, 127 of 255: an opaque surface, -3) */
		if (lowest > 127)
		{
			free(alpha);
			key_map_put(&rt.texture_map, key, -3);
			return -3;
		}
		/* (halved, the largest of each 2x2, until it fits: a thin leaf
		stays) */
		while (width > MAXIMUM_TEXTURE_SIDE || height > MAXIMUM_TEXTURE_SIDE)
		{
			int half_width = width > 1 ? width / 2 : 1, half_height = height > 1 ? height / 2 : 1;

			for (y = 0; y < half_height; y++)
			{
				for (x = 0; x < half_width; x++)
				{
					int x0 = x * width / half_width, y0 = y * height / half_height;
					int x1 = x0 + 1 < width ? x0 + 1 : x0, y1 = y0 + 1 < height ? y0 + 1 : y0;
					int a = alpha[y0 * width + x0], b = alpha[y0 * width + x1];
					int c = alpha[y1 * width + x0], d = alpha[y1 * width + x1];

					a = a > b ? a : b;
					c = c > d ? c : d;
					alpha[y * half_width + x] = (unsigned char)(a > c ? a : c);
				}
			}
			width = half_width;
			height = half_height;
		}
		if (texture_make(&rt.textures[rt.texture_count], alpha, width, height))
		{
			index = rt.texture_count++;
			rt.stats.new_textures++;
		}
		free(alpha);
	}
	key_map_put(&rt.texture_map, key, index);
	return index;
}

/* the frame's set: its textures' descriptors, those made since it was last
written (all of them, the rest the default, the first time) */
static void textures_sync(int frame)
{
	VkDescriptorImageInfo images[64];
	VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	int first = rt.textures_written[frame] ? rt.textures_synced[frame] : 0;
	int last = rt.textures_written[frame] ? rt.texture_count : MAXIMUM_TEXTURES, index;

	while (first < last)
	{
		int count = last - first > 64 ? 64 : last - first;

		for (index = 0; index < count; index++)
		{
			images[index].sampler = rt.sampler;
			images[index].imageView = first + index < rt.texture_count ? rt.textures[first + index].view :
				rt.default_texture.view;
			images[index].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		write.dstSet = rt.sets[frame];
		write.dstBinding = 5;
		write.dstArrayElement = (unsigned int)first;
		write.descriptorCount = (unsigned int)count;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = images;
		rt.UpdateDescriptorSets(rt.device, 1, &write, 0, NULL);
		first += count;
	}
	rt.textures_synced[frame] = rt.texture_count;
	rt.textures_written[frame] = 1;
}

/* ---------- starting and stopping */

static void teardown(void)
{
	int index;

	if (rt.device)
	{
		rt.DeviceWaitIdle(rt.device);
		for (index = 0; index < rt.mesh_count; index++)
		{
			if (rt.meshes[index].structure)
				rt.DestroyAccelerationStructureKHR(rt.device, rt.meshes[index].structure, NULL);
			buffer_free(&rt.meshes[index].storage);
		}
		if (rt.level_collision.structure)
			rt.DestroyAccelerationStructureKHR(rt.device, rt.level_collision.structure, NULL);
		buffer_free(&rt.level_collision.storage);
		if (rt.level.structure)
			rt.DestroyAccelerationStructureKHR(rt.device, rt.level.structure, NULL);
		for (index = 0; rt.slots && index < MAXIMUM_OBJECT_SLOTS; index++)
		{
			if (rt.slots[index].structure)
				rt.DestroyAccelerationStructureKHR(rt.device, rt.slots[index].structure, NULL);
		}
		doomed_collect(1);
		for (index = 0; index < FRAMES; index++)
		{
			if (rt.tlas[index])
				rt.DestroyAccelerationStructureKHR(rt.device, rt.tlas[index], NULL);
			buffer_free(&rt.tlas_storage[index]);
			buffer_free(&rt.tlas_scratch[index]);
			buffer_free(&rt.instances[index]);
			buffer_free(&rt.matrices[index]);
		}
		buffer_free(&rt.scratch);
		buffer_free(&rt.records);
		for (index = 0; index < rt.chunk_count; index++)
			buffer_free(&rt.chunks[index].buffer);
		for (index = 0; index < rt.texture_count; index++)
			texture_free(&rt.textures[index]);
		texture_free(&rt.default_texture);
		if (rt.sampler)
			rt.DestroySampler(rt.device, rt.sampler, NULL);
		for (index = 0; index < 4; index++)
		{
			if (rt.views[index])
				rt.DestroyImageView(rt.device, rt.views[index], NULL);
			if (rt.images[index])
				rt.DestroyImage(rt.device, rt.images[index], NULL);
			if (rt.image_memory[index])
				rt.FreeMemory(rt.device, rt.image_memory[index], NULL);
		}
		if (rt.pipeline)
			rt.DestroyPipeline(rt.device, rt.pipeline, NULL);
		if (rt.skin_pipeline)
			rt.DestroyPipeline(rt.device, rt.skin_pipeline, NULL);
		if (rt.pipeline_layout)
			rt.DestroyPipelineLayout(rt.device, rt.pipeline_layout, NULL);
		if (rt.skin_layout)
			rt.DestroyPipelineLayout(rt.device, rt.skin_layout, NULL);
		if (rt.descriptor_pool)
			rt.DestroyDescriptorPool(rt.device, rt.descriptor_pool, NULL);
		if (rt.set_layout)
			rt.DestroyDescriptorSetLayout(rt.device, rt.set_layout, NULL);
		if (rt.trace_module)
			rt.DestroyShaderModule(rt.device, rt.trace_module, NULL);
		if (rt.skin_module)
			rt.DestroyShaderModule(rt.device, rt.skin_module, NULL);
		if (rt.queries)
			rt.DestroyQueryPool(rt.device, rt.queries, NULL);
		for (index = 0; index < 2; index++)
		{
			if (rt.semaphores[index])
				rt.DestroySemaphore(rt.device, rt.semaphores[index], NULL);
		}
		for (index = 0; index < FRAMES; index++)
		{
			if (rt.fences[index])
				rt.DestroyFence(rt.device, rt.fences[index], NULL);
		}
		if (rt.one_time_fence)
			rt.DestroyFence(rt.device, rt.one_time_fence, NULL);
		if (rt.pool)
			rt.DestroyCommandPool(rt.device, rt.pool, NULL);
		rt.DestroyDevice(rt.device, NULL);
	}
	if (rt.instance && rt.DestroyInstance)
		rt.DestroyInstance(rt.instance, NULL);
	if (rt.library)
		dlclose(rt.library);
	for (index = 0; index < rt.chunk_count; index++)
		free(rt.chunks[index].free.items);
	free(rt.chunks);
	free(rt.doomed);
	free(rt.record_spans.items);
	free(rt.parts);
	free(rt.slots);
	free(rt.touched);
	free(rt.level.blocks);
	key_map_free(&rt.part_map);
	key_map_free(&rt.texture_map);
	memset(&rt, 0, sizeof(rt));
}

void ml_rt_stop(void)
{
	if (!rt.started)
		return;
	teardown();
	platform_log("ray tracing: the Vulkan side path is down");
}

int ml_rt_start(const unsigned char gl_device_uuid[16])
{
	VkPhysicalDeviceVulkan12Features features12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	VkPhysicalDeviceAccelerationStructureFeaturesKHR structure_features =
		{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
	VkPhysicalDeviceRayQueryFeaturesKHR query_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
	VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	VkDeviceQueueCreateInfo queue_info = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	VkDeviceCreateInfo device_info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	VkCommandBufferAllocateInfo command_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	VkQueryPoolCreateInfo query_info = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
	VkSamplerCreateInfo sampler_info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	float priority = 1.0f;
	VkCommandBuffer buffers[FRAMES + 1];
	unsigned int index;
	int non_uniform;

	if (rt.started)
		return !rt.failed;
	if (!vulkan_open() || !physical_choose(gl_device_uuid, 0))
		goto failed;
	rt.started = 1;
	{
		VkPhysicalDeviceAccelerationStructurePropertiesKHR structure_properties =
			{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
		VkPhysicalDeviceProperties2 properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		VkQueueFamilyProperties families[16];
		unsigned int family_count = 16;

		properties.pNext = &structure_properties;
		rt.GetPhysicalDeviceProperties2(rt.physical, &properties);
		rt.scratch_alignment = structure_properties.minAccelerationStructureScratchOffsetAlignment;
		if (!rt.scratch_alignment)
			rt.scratch_alignment = 256;
		rt.GetPhysicalDeviceQueueFamilyProperties(rt.physical, &family_count, families);
		for (index = 0; index < family_count; index++)
		{
			if (families[index].queueFlags & VK_QUEUE_COMPUTE_BIT)
				break;
		}
		if (index == family_count)
			goto failed;
		rt.queue_family = index;
		rt.GetPhysicalDeviceMemoryProperties(rt.physical, &rt.memory_properties);
	}
	features.pNext = &features12;
	features12.pNext = &structure_features;
	structure_features.pNext = &query_features;
	rt.GetPhysicalDeviceFeatures2(rt.physical, &features);
	non_uniform = features12.shaderSampledImageArrayNonUniformIndexing;
	memset(&features.features, 0, sizeof(features.features));
	memset((char *)&features12 + offsetof(VkPhysicalDeviceVulkan12Features, samplerMirrorClampToEdge), 0,
		sizeof(features12) - offsetof(VkPhysicalDeviceVulkan12Features, samplerMirrorClampToEdge));
	features12.bufferDeviceAddress = VK_TRUE;
	features12.shaderSampledImageArrayNonUniformIndexing = non_uniform ? VK_TRUE : VK_FALSE;
	structure_features.accelerationStructureCaptureReplay = VK_FALSE;
	structure_features.accelerationStructureIndirectBuild = VK_FALSE;
	structure_features.accelerationStructureHostCommands = VK_FALSE;
	structure_features.descriptorBindingAccelerationStructureUpdateAfterBind = VK_FALSE;
	queue_info.queueFamilyIndex = rt.queue_family;
	queue_info.queueCount = 1;
	queue_info.pQueuePriorities = &priority;
	device_info.pNext = &features;
	device_info.queueCreateInfoCount = 1;
	device_info.pQueueCreateInfos = &queue_info;
	device_info.enabledExtensionCount = sizeof(device_extensions) / sizeof(device_extensions[0]);
	device_info.ppEnabledExtensionNames = device_extensions;
	if (rt.CreateDevice(rt.physical, &device_info, NULL, &rt.device) != VK_SUCCESS)
	{
		rt.device = VK_NULL_HANDLE;
		platform_log("ray tracing: no device");
		goto failed;
	}
	if (!device_functions())
	{
		/* (what teardown needs: its own) */
		rt.DestroyDevice = (PFN_vkDestroyDevice)rt.GetDeviceProcAddr(rt.device, "vkDestroyDevice");
		if (rt.DestroyDevice)
			rt.DestroyDevice(rt.device, NULL);
		rt.device = VK_NULL_HANDLE;
		goto failed;
	}
	rt.GetDeviceQueue(rt.device, rt.queue_family, 0, &rt.queue);
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = rt.queue_family;
	if (rt.CreateCommandPool(rt.device, &pool_info, NULL, &rt.pool) != VK_SUCCESS)
	{
		rt.pool = VK_NULL_HANDLE;
		goto failed;
	}
	command_info.commandPool = rt.pool;
	command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_info.commandBufferCount = FRAMES + 1;
	if (rt.AllocateCommandBuffers(rt.device, &command_info, buffers) != VK_SUCCESS)
		goto failed;
	for (index = 0; index < FRAMES; index++)
	{
		rt.commands[index] = buffers[index];
		fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		if (rt.CreateFence(rt.device, &fence_info, NULL, &rt.fences[index]) != VK_SUCCESS)
		{
			rt.fences[index] = VK_NULL_HANDLE;
			goto failed;
		}
	}
	rt.one_time = buffers[FRAMES];
	fence_info.flags = 0;
	if (rt.CreateFence(rt.device, &fence_info, NULL, &rt.one_time_fence) != VK_SUCCESS)
	{
		rt.one_time_fence = VK_NULL_HANDLE;
		goto failed;
	}
	{
		VkExportSemaphoreCreateInfo export_info = { VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO };
		VkSemaphoreCreateInfo semaphore_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

		export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
		semaphore_info.pNext = &export_info;
		for (index = 0; index < 2; index++)
		{
			if (rt.CreateSemaphore(rt.device, &semaphore_info, NULL, &rt.semaphores[index]) != VK_SUCCESS)
			{
				rt.semaphores[index] = VK_NULL_HANDLE;
				goto failed;
			}
		}
	}
	query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
	query_info.queryCount = STAMPS * FRAMES;
	if (rt.CreateQueryPool(rt.device, &query_info, NULL, &rt.queries) != VK_SUCCESS)
	{
		rt.queries = VK_NULL_HANDLE;
		goto failed;
	}
	if (!pipelines_make())
	{
		platform_log("ray tracing: the trace's pipelines did not build");
		goto failed;
	}
	sampler_info.magFilter = sampler_info.minFilter = VK_FILTER_LINEAR;
	sampler_info.addressModeU = sampler_info.addressModeV = sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler_info.maxLod = 0.0f;
	if (rt.CreateSampler(rt.device, &sampler_info, NULL, &rt.sampler) != VK_SUCCESS)
	{
		rt.sampler = VK_NULL_HANDLE;
		goto failed;
	}
	{
		unsigned char white = 255;

		if (!texture_make(&rt.default_texture, &white, 1, 1))
			goto failed;
	}
	for (index = 0; index < FRAMES; index++)
	{
		if (!buffer_make(&rt.instances[index], sizeof(VkAccelerationStructureInstanceKHR) * MAXIMUM_INSTANCES,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, 1) ||
			!buffer_make(&rt.matrices[index], (VkDeviceSize)MAXIMUM_FRAME_MATRICES * 48, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 1))
		{
			goto failed;
		}
	}
	if (!buffer_make(&rt.records, (VkDeviceSize)MAXIMUM_RECORDS * sizeof(struct record), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 1))
		goto failed;
	/* (record 0: opaque, the collision scene's) */
	memset(rt.records.mapped, 0, sizeof(struct record));
	((struct record *)rt.records.mapped)[0].texture = -1;
	spans_give(&rt.record_spans, 1, MAXIMUM_RECORDS - 1);
	rt.parts = calloc(MAXIMUM_PARTS, sizeof(*rt.parts));
	rt.slots = calloc(MAXIMUM_OBJECT_SLOTS, sizeof(*rt.slots));
	rt.touched = calloc(MAXIMUM_OBJECT_SLOTS, sizeof(*rt.touched));
	if (!rt.parts || !rt.slots || !rt.touched)
		goto failed;
	for (index = 0; index < FRAMES; index++)
	{
		VkDescriptorBufferInfo records = { rt.records.buffer, 0, VK_WHOLE_SIZE };
		VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

		write.dstSet = rt.sets[index];
		write.dstBinding = 6;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write.pBufferInfo = &records;
		rt.UpdateDescriptorSets(rt.device, 1, &write, 0, NULL);
		textures_sync((int)index);
	}
	rt.gpu_ms = -1.0f;
	rt.failed = 0;
	platform_log("ray tracing: Vulkan side path on %s (ray queries%s)", rt.device_name,
		non_uniform ? "" : ", no non-uniform texture indexing");
	return 1;

failed:
	teardown();
	/* (started and failed: not tried again until ml_rt_stop) */
	rt.started = 1;
	rt.failed = 1;
	return 0;
}

int ml_rt_platform(void)
{
	return 1;
}

int ml_rt_integrated(void)
{
	return rt.integrated;
}

int ml_rt_ready(void)
{
	return rt.started && !rt.failed;
}

/* ---------- the shared images */

static void images_free(void)
{
	int index;

	rt.DeviceWaitIdle(rt.device);
	for (index = 0; index < 4; index++)
	{
		if (rt.views[index])
			rt.DestroyImageView(rt.device, rt.views[index], NULL);
		if (rt.images[index])
			rt.DestroyImage(rt.device, rt.images[index], NULL);
		if (rt.image_memory[index])
			rt.FreeMemory(rt.device, rt.image_memory[index], NULL);
		rt.views[index] = VK_NULL_HANDLE;
		rt.images[index] = VK_NULL_HANDLE;
		rt.image_memory[index] = VK_NULL_HANDLE;
	}
}

static int image_make(int index, VkFormat format, int *fd)
{
	VkExternalMemoryImageCreateInfo external = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
	VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	VkMemoryRequirements requirements;
	VkMemoryDedicatedAllocateInfo dedicated = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
	VkExportMemoryAllocateInfo export_info = { VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
	VkMemoryAllocateInfo allocate = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	VkMemoryGetFdInfoKHR get_fd = { VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR };
	VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	int type;

	external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
	info.pNext = &external;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = format;
	info.extent.width = (unsigned int)rt.width;
	info.extent.height = (unsigned int)rt.height;
	info.extent.depth = 1;
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (rt.CreateImage(rt.device, &info, NULL, &rt.images[index]) != VK_SUCCESS)
	{
		rt.images[index] = VK_NULL_HANDLE;
		return 0;
	}
	rt.GetImageMemoryRequirements(rt.device, rt.images[index], &requirements);
	type = memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	if (type < 0)
		return 0;
	dedicated.image = rt.images[index];
	export_info.pNext = &dedicated;
	export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
	allocate.pNext = &export_info;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = (unsigned int)type;
	if (rt.AllocateMemory(rt.device, &allocate, NULL, &rt.image_memory[index]) != VK_SUCCESS)
	{
		rt.image_memory[index] = VK_NULL_HANDLE;
		return 0;
	}
	rt.BindImageMemory(rt.device, rt.images[index], rt.image_memory[index], 0);
	rt.image_sizes[index] = requirements.size;
	get_fd.memory = rt.image_memory[index];
	get_fd.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
	if (rt.GetMemoryFdKHR(rt.device, &get_fd, fd) != VK_SUCCESS)
		return 0;
	view.image = rt.images[index];
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = format;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = 1;
	view.subresourceRange.layerCount = 1;
	if (rt.CreateImageView(rt.device, &view, NULL, &rt.views[index]) != VK_SUCCESS)
	{
		rt.views[index] = VK_NULL_HANDLE;
		return 0;
	}
	return 1;
}

int ml_rt_resize(int width, int height, struct ml_rt_shared *shared)
{
	static const VkFormat formats[4] =
		{ VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM,
			VK_FORMAT_R32G32B32A32_SFLOAT };
	int index;

	if (!ml_rt_ready())
		return 0;
	memset(shared, 0, sizeof(*shared));
	shared->memory_fd[0] = shared->memory_fd[1] = shared->memory_fd[2] = shared->memory_fd[3] = -1;
	shared->semaphore_fd[0] = shared->semaphore_fd[1] = -1;
	if (rt.width == width && rt.height == height && rt.images[0])
	{
		shared->width = width;
		shared->height = height;
		shared->generation = rt.generation;
		return 1;
	}
	if (rt.images[0])
		images_free();
	rt.width = width;
	rt.height = height;
	rt.generation++;
	for (index = 0; index < 4; index++)
	{
		if (!image_make(index, formats[index], &shared->memory_fd[index]))
		{
			platform_log("ray tracing: the shared images could not be made; off");
			for (index = 0; index < 4; index++)
			{
				if (shared->memory_fd[index] >= 0)
					close(shared->memory_fd[index]);
			}
			rt.failed = 1;
			return 0;
		}
		shared->memory_size[index] = (int)rt.image_sizes[index];
	}
	/* (in the general layout for good: OpenGL reads and writes them there) */
	{
		VkImageMemoryBarrier barriers[4];

		memset(barriers, 0, sizeof(barriers));
		for (index = 0; index < 4; index++)
		{
			barriers[index].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barriers[index].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barriers[index].newLayout = VK_IMAGE_LAYOUT_GENERAL;
			barriers[index].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barriers[index].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barriers[index].image = rt.images[index];
			barriers[index].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barriers[index].subresourceRange.levelCount = 1;
			barriers[index].subresourceRange.layerCount = 1;
			barriers[index].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		}
		submit_one_time_begin();
		rt.CmdPipelineBarrier(rt.one_time, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
			0, NULL, 0, NULL, 4, barriers);
		submit_one_time_end();
	}
	/* the semaphores, once */
	if (!rt.images_new)
	{
		VkSemaphoreGetFdInfoKHR get_fd = { VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR };

		get_fd.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
		for (index = 0; index < 2; index++)
		{
			get_fd.semaphore = rt.semaphores[index];
			if (rt.GetSemaphoreFdKHR(rt.device, &get_fd, &shared->semaphore_fd[index]) != VK_SUCCESS)
				shared->semaphore_fd[index] = -1;
		}
		rt.images_new = 1;
	}
	/* the descriptor sets: the images (the scene's each frame) */
	for (index = 0; index < FRAMES; index++)
	{
		VkDescriptorImageInfo images[4];
		VkWriteDescriptorSet writes[4];
		int image;

		memset(writes, 0, sizeof(writes));
		for (image = 0; image < 4; image++)
		{
			images[image].sampler = VK_NULL_HANDLE;
			images[image].imageView = rt.views[image];
			images[image].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			writes[image].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[image].dstSet = rt.sets[index];
			writes[image].dstBinding = (unsigned int)image + 1;
			writes[image].descriptorCount = 1;
			writes[image].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			writes[image].pImageInfo = &images[image];
		}
		rt.UpdateDescriptorSets(rt.device, 4, writes, 0, NULL);
	}
	shared->width = width;
	shared->height = height;
	shared->generation = rt.generation;
	platform_log("ray tracing: shared images %dx%d", width, height);
	return 1;
}

/* ---------- the scene: the world it is of */

/* everything of the last world (its tags' pointers are another map's now) */
static void scene_flush(void)
{
	int index;

	rt.DeviceWaitIdle(rt.device);
	doomed_collect(1);
	for (index = 0; index < rt.mesh_count; index++)
	{
		if (rt.meshes[index].structure)
			rt.DestroyAccelerationStructureKHR(rt.device, rt.meshes[index].structure, NULL);
		buffer_free(&rt.meshes[index].storage);
	}
	memset(rt.meshes, 0, sizeof(rt.meshes));
	rt.mesh_count = 0;
	if (rt.level_collision.structure)
		rt.DestroyAccelerationStructureKHR(rt.device, rt.level_collision.structure, NULL);
	buffer_free(&rt.level_collision.storage);
	memset(&rt.level_collision, 0, sizeof(rt.level_collision));
	if (rt.level.structure)
		rt.DestroyAccelerationStructureKHR(rt.device, rt.level.structure, NULL);
	block_give(&rt.level.storage);
	for (index = 0; index < rt.level.block_count; index++)
		block_give(&rt.level.blocks[index]);
	if (rt.level.record_count > 0)
		spans_give(&rt.record_spans, (VkDeviceSize)rt.level.record_base, (VkDeviceSize)rt.level.record_count);
	free(rt.level.blocks);
	memset(&rt.level, 0, sizeof(rt.level));
	for (index = 0; index < MAXIMUM_OBJECT_SLOTS; index++)
	{
		struct object_slot *slot = &rt.slots[index];

		if (slot->structure)
			rt.DestroyAccelerationStructureKHR(rt.device, slot->structure, NULL);
		block_give(&slot->storage);
		block_give(&slot->positions);
		if (slot->part_count > 0 && slot->record_base > 0)
			spans_give(&rt.record_spans, (VkDeviceSize)slot->record_base, (VkDeviceSize)slot->part_count);
		memset(slot, 0, sizeof(*slot));
	}
	for (index = 0; index < rt.part_count; index++)
	{
		block_give(&rt.parts[index].vertices);
		block_give(&rt.parts[index].indices);
	}
	rt.part_count = 0;
	key_map_clear(&rt.part_map);
	for (index = 0; index < rt.texture_count; index++)
		texture_free(&rt.textures[index]);
	rt.texture_count = 0;
	key_map_clear(&rt.texture_map);
	for (index = 0; index < FRAMES; index++)
		rt.textures_written[index] = 0;
	rt.touched_count = 0;
	rt.instance_count = 0;
}

void ml_rt_world(unsigned int world)
{
	if (!ml_rt_ready() || world == rt.world)
		return;
	if (rt.world)
		platform_log("ray tracing: a new world (%08x): the scene is made again", world);
	scene_flush();
	rt.world = world;
}

/* ---------- the scene: collision meshes (the round-1 prototype's) */

/* a bottom-level structure of triangles, built now (and waited for) */
static int mesh_build(struct mesh *mesh, const float *positions, int vertex_count, const unsigned int *indices,
	int triangle_count)
{
	struct buffer vertices, index_buffer, scratch;
	VkAccelerationStructureGeometryKHR geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	VkAccelerationStructureBuildGeometryInfoKHR build = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	VkAccelerationStructureBuildSizesInfoKHR sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	VkAccelerationStructureCreateInfoKHR create = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
	VkAccelerationStructureBuildRangeInfoKHR range = { 0 };
	const VkAccelerationStructureBuildRangeInfoKHR *ranges = &range;
	VkAccelerationStructureDeviceAddressInfoKHR address = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
	unsigned int primitives = (unsigned int)triangle_count;
	VkBufferUsageFlags input = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
	int built;

	mesh->structure = VK_NULL_HANDLE;
	mesh->empty = triangle_count <= 0 || vertex_count <= 0;
	if (mesh->empty)
		return 1;
	if (!buffer_make(&vertices, (VkDeviceSize)vertex_count * 12, input, 1))
		return 0;
	if (!buffer_make(&index_buffer, (VkDeviceSize)triangle_count * 12, input, 1))
	{
		buffer_free(&vertices);
		return 0;
	}
	memcpy(vertices.mapped, positions, (size_t)vertex_count * 12);
	memcpy(index_buffer.mapped, indices, (size_t)triangle_count * 12);
	geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	geometry.geometry.triangles.vertexData.deviceAddress = vertices.address;
	geometry.geometry.triangles.vertexStride = 12;
	geometry.geometry.triangles.maxVertex = (unsigned int)vertex_count - 1;
	geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
	geometry.geometry.triangles.indexData.deviceAddress = index_buffer.address;
	geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build.geometryCount = 1;
	build.pGeometries = &geometry;
	rt.GetAccelerationStructureBuildSizesKHR(rt.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build,
		&primitives, &sizes);
	built = buffer_make(&mesh->storage, sizes.accelerationStructureSize,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, 0);
	if (built && !buffer_make(&scratch, sizes.buildScratchSize + rt.scratch_alignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0))
	{
		buffer_free(&mesh->storage);
		built = 0;
	}
	if (built)
	{
		create.buffer = mesh->storage.buffer;
		create.size = sizes.accelerationStructureSize;
		create.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		if (rt.CreateAccelerationStructureKHR(rt.device, &create, NULL, &mesh->structure) != VK_SUCCESS)
		{
			mesh->structure = VK_NULL_HANDLE;
			buffer_free(&mesh->storage);
			built = 0;
		}
		else
		{
			build.dstAccelerationStructure = mesh->structure;
			build.scratchData.deviceAddress = align_up(scratch.address, rt.scratch_alignment);
			range.primitiveCount = primitives;
			submit_one_time_begin();
			rt.CmdBuildAccelerationStructuresKHR(rt.one_time, 1, &build, &ranges);
			submit_one_time_end();
		}
		buffer_free(&scratch);
	}
	buffer_free(&vertices);
	buffer_free(&index_buffer);
	if (!built)
		return 0;
	address.accelerationStructure = mesh->structure;
	mesh->address = rt.GetAccelerationStructureDeviceAddressKHR(rt.device, &address);
	return 1;
}

static void mesh_free(struct mesh *mesh)
{
	if (mesh->structure)
		rt.DestroyAccelerationStructureKHR(rt.device, mesh->structure, NULL);
	buffer_free(&mesh->storage);
	memset(mesh, 0, sizeof(*mesh));
}

int ml_rt_static_mesh(const void *key, const float *positions, int vertex_count, const unsigned int *indices,
	int triangle_count)
{
	if (!ml_rt_ready())
		return 0;
	rt.DeviceWaitIdle(rt.device);
	mesh_free(&rt.level_collision);
	rt.level_render = 0;
	if (!mesh_build(&rt.level_collision, positions, vertex_count, indices, triangle_count))
	{
		platform_log("ray tracing: the level's structure could not be built");
		return 0;
	}
	rt.level_collision.key = key;
	platform_log("ray tracing: the level's collision BSP, %d triangles", triangle_count);
	return 1;
}

int ml_rt_mesh_find(const void *key)
{
	int index;

	for (index = 0; index < rt.mesh_count; index++)
	{
		if (rt.meshes[index].key == key)
			return rt.meshes[index].empty ? -2 : index;
	}
	return -1;
}

int ml_rt_mesh_add(const void *key, const float *positions, int vertex_count, const unsigned int *indices,
	int triangle_count)
{
	struct mesh *mesh;

	if (!ml_rt_ready() || rt.mesh_count >= MAXIMUM_MESHES)
		return -2;
	mesh = &rt.meshes[rt.mesh_count];
	memset(mesh, 0, sizeof(*mesh));
	if (!mesh_build(mesh, positions, vertex_count, indices, triangle_count))
		mesh->empty = 1;
	mesh->key = key;
	rt.mesh_count++;
	return mesh->empty ? -2 : rt.mesh_count - 1;
}

/* ---------- the scene: the level's render geometry */

static struct
{
	VkAccelerationStructureGeometryKHR *geometries;
	unsigned int *counts;
	struct record *records;
	int count, capacity;
	unsigned long long triangles;
	int failed;
} level_build;

int ml_rt_level_begin(const void *key)
{
	int index;

	if (!ml_rt_ready())
		return 0;
	rt.DeviceWaitIdle(rt.device);
	if (rt.level.structure)
		rt.DestroyAccelerationStructureKHR(rt.device, rt.level.structure, NULL);
	block_give(&rt.level.storage);
	for (index = 0; index < rt.level.block_count; index++)
		block_give(&rt.level.blocks[index]);
	if (rt.level.record_count > 0)
		spans_give(&rt.record_spans, (VkDeviceSize)rt.level.record_base, (VkDeviceSize)rt.level.record_count);
	free(rt.level.blocks);
	memset(&rt.level, 0, sizeof(rt.level));
	rt.level.key = key;
	level_build.count = 0;
	level_build.triangles = 0;
	level_build.failed = 0;
	return 1;
}

static int level_blocks_add(struct block *block)
{
	struct block *grown = realloc(rt.level.blocks, (size_t)(rt.level.block_count + 1) * sizeof(*grown));

	if (!grown)
		return 0;
	rt.level.blocks = grown;
	rt.level.blocks[rt.level.block_count++] = *block;
	return 1;
}

void ml_rt_level_material(const void *vertices, int vertex_count, int vertex_stride, const unsigned short *triangles,
	int triangle_count, int texture, const float scale[2])
{
	struct block vertex_block, index_block;
	VkAccelerationStructureGeometryKHR *geometry;
	unsigned int *indices;
	int index, kept = 0;

	if (!ml_rt_ready() || level_build.failed || vertex_count <= 0 || triangle_count <= 0 || vertex_stride < 12)
		return;
	if (level_build.count == level_build.capacity)
	{
		int capacity = level_build.capacity ? level_build.capacity * 2 : 256;
		void *g = realloc(level_build.geometries, (size_t)capacity * sizeof(level_build.geometries[0]));
		void *c, *r;

		if (!g)
			return;
		level_build.geometries = g;
		c = realloc(level_build.counts, (size_t)capacity * sizeof(level_build.counts[0]));
		if (!c)
			return;
		level_build.counts = c;
		r = realloc(level_build.records, (size_t)capacity * sizeof(level_build.records[0]));
		if (!r)
			return;
		level_build.records = r;
		level_build.capacity = capacity;
	}
	if (!block_take(&vertex_block, (VkDeviceSize)vertex_count * (VkDeviceSize)vertex_stride, 1))
	{
		level_build.failed = 1;
		return;
	}
	if (!block_take(&index_block, (VkDeviceSize)triangle_count * 12, 1))
	{
		block_give(&vertex_block);
		level_build.failed = 1;
		return;
	}
	memcpy(vertex_block.mapped, vertices, (size_t)vertex_count * (size_t)vertex_stride);
	indices = index_block.mapped;
	for (index = 0; index < triangle_count; index++)
	{
		unsigned int a = triangles[index * 3], b = triangles[index * 3 + 1], c = triangles[index * 3 + 2];

		/* (a triangle past the vertices is left out, as the game's
		visibility leaves it out) */
		if (a >= (unsigned int)vertex_count || b >= (unsigned int)vertex_count || c >= (unsigned int)vertex_count)
			continue;
		indices[kept * 3] = a;
		indices[kept * 3 + 1] = b;
		indices[kept * 3 + 2] = c;
		kept++;
	}
	if (!kept || !level_blocks_add(&vertex_block) || !level_blocks_add(&index_block))
	{
		block_give(&vertex_block);
		block_give(&index_block);
		return;
	}
	geometry = &level_build.geometries[level_build.count];
	memset(geometry, 0, sizeof(*geometry));
	geometry->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry->geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry->geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	geometry->geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	geometry->geometry.triangles.vertexData.deviceAddress = vertex_block.address;
	geometry->geometry.triangles.vertexStride = (VkDeviceSize)vertex_stride;
	geometry->geometry.triangles.maxVertex = (unsigned int)vertex_count - 1;
	geometry->geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
	geometry->geometry.triangles.indexData.deviceAddress = index_block.address;
	geometry->flags = texture >= 0 ? 0 : VK_GEOMETRY_OPAQUE_BIT_KHR;
	level_build.counts[level_build.count] = (unsigned int)kept;
	level_build.records[level_build.count].indices = index_block.address;
	level_build.records[level_build.count].vertices = vertex_block.address;
	level_build.records[level_build.count].kind = texture >= 0 ? 3 : 0;
	level_build.records[level_build.count].texture = texture >= 0 ? texture : -1;
	level_build.records[level_build.count].scale[0] = scale ? scale[0] : 1.0f;
	level_build.records[level_build.count].scale[1] = scale ? scale[1] : 1.0f;
	level_build.count++;
	level_build.triangles += (unsigned long long)kept;
}

int ml_rt_level_end(void)
{
	VkAccelerationStructureBuildGeometryInfoKHR build = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	VkAccelerationStructureBuildSizesInfoKHR sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	VkAccelerationStructureCreateInfoKHR create = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
	VkAccelerationStructureDeviceAddressInfoKHR address = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
	VkAccelerationStructureBuildRangeInfoKHR *ranges;
	const VkAccelerationStructureBuildRangeInfoKHR *range_pointer;
	struct buffer scratch;
	int index;

	if (!ml_rt_ready())
		return 0;
	rt.level_render = 1;
	if (level_build.failed || !level_build.count)
	{
		platform_log("ray tracing: the level's render geometry: %s", level_build.failed ? "no memory" : "nothing");
		return 0;
	}
	rt.level.record_base = records_take(level_build.count);
	if (rt.level.record_base < 0)
		return 0;
	rt.level.record_count = level_build.count;
	memcpy((struct record *)rt.records.mapped + rt.level.record_base, level_build.records,
		(size_t)level_build.count * sizeof(struct record));
	build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build.geometryCount = (unsigned int)level_build.count;
	build.pGeometries = level_build.geometries;
	rt.GetAccelerationStructureBuildSizesKHR(rt.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build,
		level_build.counts, &sizes);
	if (!block_take(&rt.level.storage, sizes.accelerationStructureSize, 0))
		return 0;
	create.buffer = rt.chunks[rt.level.storage.chunk].buffer.buffer;
	create.offset = rt.level.storage.offset;
	create.size = sizes.accelerationStructureSize;
	create.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	if (rt.CreateAccelerationStructureKHR(rt.device, &create, NULL, &rt.level.structure) != VK_SUCCESS)
	{
		rt.level.structure = VK_NULL_HANDLE;
		return 0;
	}
	if (!buffer_make(&scratch, sizes.buildScratchSize + rt.scratch_alignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0))
		return 0;
	ranges = calloc((size_t)level_build.count, sizeof(*ranges));
	if (!ranges)
	{
		buffer_free(&scratch);
		return 0;
	}
	for (index = 0; index < level_build.count; index++)
		ranges[index].primitiveCount = level_build.counts[index];
	range_pointer = ranges;
	build.dstAccelerationStructure = rt.level.structure;
	build.scratchData.deviceAddress = align_up(scratch.address, rt.scratch_alignment);
	submit_one_time_begin();
	rt.CmdBuildAccelerationStructuresKHR(rt.one_time, 1, &build, &range_pointer);
	submit_one_time_end();
	free(ranges);
	buffer_free(&scratch);
	address.accelerationStructure = rt.level.structure;
	rt.level.address = rt.GetAccelerationStructureDeviceAddressKHR(rt.device, &address);
	rt.level.ready = 1;
	platform_log("ray tracing: the level's render geometry, %d materials, %llu triangles", level_build.count,
		level_build.triangles);
	return 1;
}

/* ---------- the scene: model parts and objects */

int ml_rt_part(const void *key, const void *vertices, int vertex_count, int kind, const unsigned short *indices,
	int index_count, int strip)
{
	struct part *part;
	unsigned int *triangles;
	int found, index, count = 0, stride = kind == 2 ? 68 : 32;

	if (!ml_rt_ready())
		return -2;
	found = key_map_find(&rt.part_map, key);
	if (found != -1)
		return found;
	if (rt.part_count >= MAXIMUM_PARTS || !vertices || !indices || vertex_count <= 0 || index_count < 3 ||
		(kind != 1 && kind != 2))
	{
		key_map_put(&rt.part_map, key, -2);
		return -2;
	}
	part = &rt.parts[rt.part_count];
	memset(part, 0, sizeof(*part));
	if (!block_take(&part->vertices, (VkDeviceSize)vertex_count * (VkDeviceSize)stride, 1) ||
		!block_take(&part->indices, (VkDeviceSize)(strip ? index_count - 2 : index_count / 3) * 12, 1))
	{
		block_give(&part->vertices);
		block_give(&part->indices);
		key_map_put(&rt.part_map, key, -2);
		return -2;
	}
	memcpy(part->vertices.mapped, vertices, (size_t)vertex_count * (size_t)stride);
	triangles = part->indices.mapped;
	if (strip)
	{
		/* (a strip: each three in a row a triangle, the repeated ones the
		strip's joins; the winding is not needed) */
		for (index = 0; index + 2 < index_count; index++)
		{
			unsigned int a = indices[index], b = indices[index + 1], c = indices[index + 2];

			if (a == b || b == c || a == c || a >= (unsigned int)vertex_count || b >= (unsigned int)vertex_count ||
				c >= (unsigned int)vertex_count)
			{
				continue;
			}
			triangles[count * 3] = a;
			triangles[count * 3 + 1] = b;
			triangles[count * 3 + 2] = c;
			count++;
		}
	}
	else
	{
		for (index = 0; index + 2 < index_count; index += 3)
		{
			unsigned int a = indices[index], b = indices[index + 1], c = indices[index + 2];

			if (a >= (unsigned int)vertex_count || b >= (unsigned int)vertex_count || c >= (unsigned int)vertex_count)
				continue;
			triangles[count * 3] = a;
			triangles[count * 3 + 1] = b;
			triangles[count * 3 + 2] = c;
			count++;
		}
	}
	if (!count)
	{
		block_give(&part->vertices);
		block_give(&part->indices);
		key_map_put(&rt.part_map, key, -2);
		return -2;
	}
	part->key = key;
	part->vertex_count = vertex_count;
	part->triangle_count = count;
	part->kind = kind;
	key_map_put(&rt.part_map, key, rt.part_count);
	rt.stats.new_parts++;
	return rt.part_count++;
}

/* the object's geometries, for a build or its sizes */
static int slot_geometries(const struct object_slot *slot, VkAccelerationStructureGeometryKHR *geometries,
	unsigned int *counts)
{
	int index;

	for (index = 0; index < slot->part_count; index++)
	{
		const struct part *part = &rt.parts[slot->parts[index]];
		VkAccelerationStructureGeometryKHR *geometry = &geometries[index];

		memset(geometry, 0, sizeof(*geometry));
		geometry->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		geometry->geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		geometry->geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		geometry->geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		geometry->geometry.triangles.vertexData.deviceAddress = slot->positions.address + slot->part_offsets[index];
		geometry->geometry.triangles.vertexStride = 12;
		geometry->geometry.triangles.maxVertex = (unsigned int)part->vertex_count - 1;
		geometry->geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
		geometry->geometry.triangles.indexData.deviceAddress = part->indices.address;
		geometry->flags = slot->textures[index] >= 0 ? 0 : VK_GEOMETRY_OPAQUE_BIT_KHR;
		counts[index] = (unsigned int)part->triangle_count;
	}
	return slot->part_count;
}

static void slot_free(struct object_slot *slot)
{
	if (slot->structure || slot->storage.size)
		doom(&slot->storage, slot->structure, 0, 0);
	if (slot->positions.size)
		doom(&slot->positions, VK_NULL_HANDLE, 0, 0);
	if (slot->record_base > 0 && slot->part_count > 0)
		doom(NULL, VK_NULL_HANDLE, slot->record_base, slot->part_count);
	slot->structure = VK_NULL_HANDLE;
	slot->record_base = 0;
	slot->part_count = 0;
	slot->built = 0;
}

/* a new structure for the slot's parts (not built yet) */
static int slot_make(struct object_slot *slot)
{
	VkAccelerationStructureGeometryKHR geometries[MAXIMUM_PARTS_PER_OBJECT];
	unsigned int counts[MAXIMUM_PARTS_PER_OBJECT];
	VkAccelerationStructureBuildGeometryInfoKHR build = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	VkAccelerationStructureBuildSizesInfoKHR sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	VkAccelerationStructureCreateInfoKHR create = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
	VkAccelerationStructureDeviceAddressInfoKHR address = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
	VkDeviceSize total = 0;
	struct record *records;
	int index;

	for (index = 0; index < slot->part_count; index++)
	{
		slot->part_offsets[index] = total;
		total += align_up((VkDeviceSize)rt.parts[slot->parts[index]].vertex_count * 12, 256);
	}
	if (!block_take(&slot->positions, total, 0))
		return 0;
	slot->record_base = records_take(slot->part_count);
	if (slot->record_base < 0)
	{
		slot->record_base = 0;
		return 0;
	}
	records = (struct record *)rt.records.mapped + slot->record_base;
	for (index = 0; index < slot->part_count; index++)
	{
		const struct part *part = &rt.parts[slot->parts[index]];

		records[index].indices = part->indices.address;
		records[index].vertices = part->vertices.address;
		records[index].kind = slot->textures[index] >= 0 ? part->kind : 0;
		records[index].texture = slot->textures[index];
		records[index].scale[0] = slot->scales[index][0];
		records[index].scale[1] = slot->scales[index][1];
	}
	slot_geometries(slot, geometries, counts);
	build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
		VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
	build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build.geometryCount = (unsigned int)slot->part_count;
	build.pGeometries = geometries;
	rt.GetAccelerationStructureBuildSizesKHR(rt.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, counts,
		&sizes);
	if (!block_take(&slot->storage, sizes.accelerationStructureSize, 0))
		return 0;
	create.buffer = rt.chunks[slot->storage.chunk].buffer.buffer;
	create.offset = slot->storage.offset;
	create.size = sizes.accelerationStructureSize;
	create.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	if (rt.CreateAccelerationStructureKHR(rt.device, &create, NULL, &slot->structure) != VK_SUCCESS)
	{
		slot->structure = VK_NULL_HANDLE;
		return 0;
	}
	address.accelerationStructure = slot->structure;
	slot->address = rt.GetAccelerationStructureDeviceAddressKHR(rt.device, &address);
	rt.stats.new_slots++;
	slot->build_scratch = align_up(sizes.buildScratchSize, rt.scratch_alignment);
	slot->update_scratch = align_up(sizes.updateScratchSize, rt.scratch_alignment);
	slot->built = 0;
	slot->updates = 0;
	return 1;
}

void ml_rt_object(int slot_index, int key, const struct ml_rt_part_use *uses, int use_count, const float *matrices,
	int node_count, unsigned int custom, unsigned int mask)
{
	struct object_slot *slot;
	int same, index, count = 0;
	int parts[MAXIMUM_PARTS_PER_OBJECT], textures[MAXIMUM_PARTS_PER_OBJECT];
	float scales[MAXIMUM_PARTS_PER_OBJECT][2];

	if (!ml_rt_ready() || slot_index < 0 || slot_index >= MAXIMUM_OBJECT_SLOTS || node_count <= 0 ||
		node_count > MAXIMUM_NODES || rt.instance_count >= MAXIMUM_INSTANCES - 1)
	{
		return;
	}
	for (index = 0; index < use_count && count < MAXIMUM_PARTS_PER_OBJECT; index++)
	{
		if (uses[index].part < 0 || uses[index].part >= rt.part_count)
			continue;
		parts[count] = uses[index].part;
		textures[count] = uses[index].texture;
		scales[count][0] = uses[index].scale[0];
		scales[count][1] = uses[index].scale[1];
		count++;
	}
	slot = &rt.slots[slot_index];
	same = slot->used && slot->key == key && slot->part_count == count && slot->node_count == node_count &&
		!memcmp(slot->parts, parts, (size_t)count * sizeof(int)) &&
		!memcmp(slot->textures, textures, (size_t)count * sizeof(int)) &&
		!memcmp(slot->scales, scales, (size_t)count * sizeof(scales[0]));
	if (!count)
	{
		if (slot->used)
			slot_free(slot);
		slot->used = 0;
		return;
	}
	if (!same)
	{
		slot_free(slot);
		slot->used = 1;
		slot->key = key;
		slot->part_count = count;
		slot->node_count = node_count;
		memcpy(slot->parts, parts, (size_t)count * sizeof(int));
		memcpy(slot->textures, textures, (size_t)count * sizeof(int));
		memcpy(slot->scales, scales, (size_t)count * sizeof(scales[0]));
		if (!slot_make(slot))
		{
			slot_free(slot);
			slot->used = 0;
			return;
		}
	}
	slot->seen = rt.frame;
	slot->custom = custom;
	slot->mask = mask;
	slot->wanted = 0;
	if (!slot->built)
		slot->wanted = 1;
	else if (slot->updates >= REBUILD_UPDATES && rt.rebuilds < REBUILDS_PER_FRAME)
	{
		slot->wanted = 1;
		rt.rebuilds++;
	}
	else if (slot->stale || memcmp(slot->matrices, matrices, (size_t)node_count * 12 * sizeof(float)))
		slot->wanted = 2;
	if (slot->wanted)
	{
		/* (the frame's ring full: as it was, until a frame with room) */
		if (rt.matrix_used + node_count > MAXIMUM_FRAME_MATRICES)
		{
			slot->wanted = 0;
			if (!slot->built)
				return;
		}
		else
		{
			slot->matrix_offset = rt.matrix_used;
			memcpy((float *)rt.matrices[rt.frame % FRAMES].mapped + (size_t)rt.matrix_used * 12, matrices,
				(size_t)node_count * 12 * sizeof(float));
			memcpy(slot->matrices, matrices, (size_t)node_count * 12 * sizeof(float));
			rt.matrix_used += node_count;
			slot->stale = 0;
		}
	}
	rt.touched[rt.touched_count++] = slot_index;
	{
		VkAccelerationStructureInstanceKHR *instance = &pending[rt.instance_count];

		pending_slot[rt.instance_count++] = slot_index;
		memset(instance, 0, sizeof(*instance));
		instance->transform.matrix[0][0] = 1.0f;
		instance->transform.matrix[1][1] = 1.0f;
		instance->transform.matrix[2][2] = 1.0f;
		instance->instanceCustomIndex = (custom & 0x2ffff) | ((mask & HALO_RT_MASK_UNIT) ? 0x10000u : 0u);
		instance->mask = mask & HALO_RT_MASK_SHADOWLESS ? HALO_RT_MASK_SHADOWLESS : HALO_RT_MASK_OBJECT;
		instance->instanceShaderBindingTableRecordOffset = (unsigned int)slot->record_base;
		instance->flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		instance->accelerationStructureReference = slot->address;
	}
}

/* waits for the frame's slot (once a frame: its matrices, its instances) */
static void frame_acquire(void)
{
	int frame = (int)(rt.frame % FRAMES);
	double started;

	if (rt.acquired)
		return;
	started = clock_ms();
	rt.WaitForFences(rt.device, 1, &rt.fences[frame], VK_TRUE, ~0ull);
	rt.fence_ms += clock_ms() - started;
	rt.stats.fence_ms += (float)(clock_ms() - started);
	rt.acquired = 1;
	doomed_collect(0);
}

void ml_rt_instances_begin(void)
{
	int index;

	if (!ml_rt_ready())
		return;
	frame_acquire();
	rt.instance_count = 0;
	rt.matrix_used = 0;
	rt.rebuilds = 0;
	/* (a gathering no trace followed: what it wanted is wanted again) */
	for (index = 0; index < rt.touched_count; index++)
	{
		struct object_slot *slot = &rt.slots[rt.touched[index]];

		if (slot->wanted == 2)
			slot->stale = 1;
		slot->wanted = 0;
	}
	rt.touched_count = 0;
	/* (objects not seen for a while: their structures freed) */
	if (rt.slots && (rt.frame & 63) == 0)
	{
		for (index = 0; index < MAXIMUM_OBJECT_SLOTS; index++)
		{
			if (rt.slots[index].used && rt.frame - rt.slots[index].seen > 300)
			{
				slot_free(&rt.slots[index]);
				rt.slots[index].used = 0;
			}
		}
	}
}

/* the frame's instances go to a list of their own until the trace, which
copies them to its frame's buffer */
void ml_rt_instance(int mesh, const float transform[12], unsigned int custom, unsigned int mask)
{
	VkAccelerationStructureInstanceKHR *instance;

	if (!ml_rt_ready() || mesh < 0 || mesh >= rt.mesh_count || rt.instance_count >= MAXIMUM_INSTANCES - 1)
		return;
	pending_slot[rt.instance_count] = -1;
	instance = &pending[rt.instance_count++];
	memcpy(instance->transform.matrix, transform, sizeof(float) * 12);
	instance->instanceCustomIndex = (custom & 0x2ffff) | ((mask & HALO_RT_MASK_UNIT) ? 0x10000u : 0u);
	instance->mask = mask & HALO_RT_MASK_SHADOWLESS ? HALO_RT_MASK_SHADOWLESS : HALO_RT_MASK_OBJECT;
	instance->instanceShaderBindingTableRecordOffset = 0;
	instance->flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
	instance->accelerationStructureReference = rt.meshes[mesh].address;
}

/* the frame's top-level structure: its storage made large enough */
static int tlas_fit(VkAccelerationStructureBuildGeometryInfoKHR *build)
{
	VkAccelerationStructureBuildSizesInfoKHR sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	unsigned int most = MAXIMUM_INSTANCES;
	int index;

	if (rt.tlas[0])
		return 1;
	rt.GetAccelerationStructureBuildSizesKHR(rt.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, build, &most,
		&sizes);
	for (index = 0; index < FRAMES; index++)
	{
		VkAccelerationStructureCreateInfoKHR create = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };

		if (!buffer_make(&rt.tlas_storage[index], sizes.accelerationStructureSize,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, 0) ||
			!buffer_make(&rt.tlas_scratch[index], sizes.buildScratchSize + rt.scratch_alignment,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0))
		{
			return 0;
		}
		create.buffer = rt.tlas_storage[index].buffer;
		create.size = sizes.accelerationStructureSize;
		create.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		if (rt.CreateAccelerationStructureKHR(rt.device, &create, NULL, &rt.tlas[index]) != VK_SUCCESS)
		{
			rt.tlas[index] = VK_NULL_HANDLE;
			return 0;
		}
	}
	return 1;
}

/* the objects' skinning and their structures' builds and refits, recorded */
static void objects_record(VkCommandBuffer commands)
{
	static VkAccelerationStructureBuildGeometryInfoKHR *builds;
	static VkAccelerationStructureBuildRangeInfoKHR *ranges;
	static const VkAccelerationStructureBuildRangeInfoKHR **range_pointers;
	static VkAccelerationStructureGeometryKHR *geometries;
	static int capacity, geometry_capacity;
	VkDeviceSize scratch_needed = 0, scratch_at = 0;
	VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	int index, build_count = 0, geometry_count = 0, skinned = 0, frame = (int)(rt.frame % FRAMES);

	rt.skinned = rt.built = rt.refit = 0;
	for (index = 0; index < rt.touched_count; index++)
	{
		struct object_slot *slot = &rt.slots[rt.touched[index]];

		if (!slot->wanted)
			continue;
		scratch_needed += slot->wanted == 1 ? slot->build_scratch : slot->update_scratch;
		geometry_count += slot->part_count;
		build_count++;
	}
	if (!build_count)
		return;
	if (build_count > capacity)
	{
		void *a = realloc(builds, (size_t)build_count * sizeof(*builds));
		void *b = a ? realloc(range_pointers, (size_t)build_count * sizeof(*range_pointers)) : NULL;

		if (a)
			builds = a;
		if (b)
			range_pointers = b;
		if (!a || !b)
			return;
		capacity = build_count;
	}
	if (geometry_count > geometry_capacity)
	{
		void *a = realloc(geometries, (size_t)geometry_count * sizeof(*geometries));
		void *b = a ? realloc(ranges, (size_t)geometry_count * sizeof(*ranges)) : NULL;

		if (a)
			geometries = a;
		if (b)
			ranges = b;
		if (!a || !b)
			return;
		geometry_capacity = geometry_count;
	}
	if (rt.scratch.size < scratch_needed + rt.scratch_alignment)
	{
		/* (grown: no frame may still use the old one) */
		double started = clock_ms();

		rt.DeviceWaitIdle(rt.device);
		rt.stats.stall_ms += (float)(clock_ms() - started);
		buffer_free(&rt.scratch);
		if (!buffer_make(&rt.scratch, (scratch_needed + rt.scratch_alignment) * 3 / 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0))
		{
			platform_log("ray tracing: no memory for %llu MB of build scratch",
				(unsigned long long)(scratch_needed >> 20));
			for (index = 0; index < rt.touched_count; index++)
				rt.slots[rt.touched[index]].wanted = 0;
			return;
		}
	}
	/* the skinning */
	rt.CmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, rt.skin_pipeline);
	for (index = 0; index < rt.touched_count; index++)
	{
		struct object_slot *slot = &rt.slots[rt.touched[index]];
		int part_index;

		if (!slot->wanted)
			continue;
		for (part_index = 0; part_index < slot->part_count; part_index++)
		{
			const struct part *part = &rt.parts[slot->parts[part_index]];
			struct
			{
				VkDeviceAddress source, destination, matrices;
				unsigned int count, kind, nodes, pad;
			} push;

			push.source = part->vertices.address;
			push.destination = slot->positions.address + slot->part_offsets[part_index];
			push.matrices = rt.matrices[frame].address + (VkDeviceAddress)slot->matrix_offset * 48;
			push.count = (unsigned int)part->vertex_count;
			push.kind = (unsigned int)part->kind;
			push.nodes = (unsigned int)slot->node_count;
			push.pad = 0;
			rt.CmdPushConstants(commands, rt.skin_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
			rt.CmdDispatch(commands, (push.count + 63) / 64, 1, 1);
		}
		skinned++;
	}
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
	rt.CmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, NULL, 0, NULL);
	/* the builds and refits, at once */
	build_count = 0;
	geometry_count = 0;
	for (index = 0; index < rt.touched_count; index++)
	{
		struct object_slot *slot = &rt.slots[rt.touched[index]];
		VkAccelerationStructureBuildGeometryInfoKHR *build;
		unsigned int counts[MAXIMUM_PARTS_PER_OBJECT];
		int part_index;

		if (!slot->wanted)
			continue;
		slot_geometries(slot, &geometries[geometry_count], counts);
		for (part_index = 0; part_index < slot->part_count; part_index++)
		{
			memset(&ranges[geometry_count + part_index], 0, sizeof(ranges[0]));
			ranges[geometry_count + part_index].primitiveCount = counts[part_index];
		}
		build = &builds[build_count];
		memset(build, 0, sizeof(*build));
		build->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		build->type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		build->flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
			VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
		build->mode = slot->wanted == 1 ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR :
			VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
		build->srcAccelerationStructure = slot->wanted == 1 ? VK_NULL_HANDLE : slot->structure;
		build->dstAccelerationStructure = slot->structure;
		build->geometryCount = (unsigned int)slot->part_count;
		build->pGeometries = &geometries[geometry_count];
		build->scratchData.deviceAddress = align_up(rt.scratch.address, rt.scratch_alignment) + scratch_at;
		range_pointers[build_count] = &ranges[geometry_count];
		scratch_at += slot->wanted == 1 ? slot->build_scratch : slot->update_scratch;
		geometry_count += slot->part_count;
		build_count++;
		if (slot->wanted == 1)
		{
			slot->built = 1;
			slot->updates = 0;
			rt.built++;
		}
		else
		{
			slot->updates++;
			rt.refit++;
		}
	}
	rt.CmdBuildAccelerationStructuresKHR(commands, (unsigned int)build_count, builds, range_pointers);
	rt.skinned = skinned;
	rt.stats.built += rt.built;
	rt.stats.refit += rt.refit;
	rt.stats.skinned += skinned;
}

int ml_rt_trace(const struct ml_rt_trace *trace)
{
	int frame = (int)(rt.frame % FRAMES);
	VkCommandBuffer commands = rt.commands[frame];
	VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	VkAccelerationStructureGeometryKHR geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	VkAccelerationStructureBuildGeometryInfoKHR build = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	VkAccelerationStructureBuildRangeInfoKHR range = { 0 };
	const VkAccelerationStructureBuildRangeInfoKHR *ranges = &range;
	VkAccelerationStructureInstanceKHR *instances;
	VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	VkDeviceAddress level_address;
	int level_record;
	unsigned int count = 0;
	int traced = 0, stamped = 0;
	struct
	{
		float camera[4];
		float sun[4];
		int rect[4];
		int misc[4];
		int more[4];
		int grid[4];
		float fade[4];
	} push;
	double waited;

	frame_acquire();
	rt.ResetFences(rt.device, 1, &rt.fences[frame]);
	/* (the last time this frame's slot ran: its timestamps) */
	if (rt.frames_submitted[frame])
	{
		unsigned long long stamps[STAMPS];

		if (rt.GetQueryPoolResults(rt.device, rt.queries, (unsigned int)frame * STAMPS, STAMPS, sizeof(stamps), stamps,
			sizeof(stamps[0]), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS && stamps[3] > stamps[0])
		{
			double scale = rt.timestamp_period / 1000000.0;

			rt.gpu_ms = (float)((double)(stamps[3] - stamps[0]) * scale);
			rt.gpu_build_ms = stamps[1] >= stamps[0] && stamps[1] <= stamps[3] ? (float)((double)(stamps[1] - stamps[0]) * scale) : 0.0f;
			rt.gpu_trace_ms = stamps[3] >= stamps[1] && stamps[1] >= stamps[0] ? (float)((double)(stamps[3] - stamps[1]) * scale) : 0.0f;
		}
	}
	if (!rt.textures_written[frame] || rt.textures_synced[frame] != rt.texture_count)
		textures_sync(frame);
	rt.BeginCommandBuffer(commands, &begin);
	rt.CmdResetQueryPool(commands, rt.queries, (unsigned int)frame * STAMPS, STAMPS);
	/* (after the frame before: its trace read the structures this one
	refits) */
	barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
	barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
	rt.CmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier,
		0, NULL, 0, NULL);
	rt.CmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, rt.queries, (unsigned int)frame * STAMPS);
	stamped = 1;
	level_address = rt.level_render ? rt.level.address : rt.level_collision.address;
	level_record = rt.level_render ? rt.level.record_base : 0;
	if ((rt.level_render ? rt.level.structure : rt.level_collision.structure) && rt.images[0])
	{
		objects_record(commands);
		barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
		rt.CmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, NULL, 0, NULL);
		/* the instances: the level's and the objects' */
		instances = rt.instances[frame].mapped;
		memset(&instances[0], 0, sizeof(instances[0]));
		instances[0].transform.matrix[0][0] = 1.0f;
		instances[0].transform.matrix[1][1] = 1.0f;
		instances[0].transform.matrix[2][2] = 1.0f;
		instances[0].instanceCustomIndex = 0xffff;
		instances[0].mask = HALO_RT_MASK_LEVEL;
		instances[0].instanceShaderBindingTableRecordOffset = (unsigned int)level_record;
		instances[0].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		instances[0].accelerationStructureReference = level_address;
		/* (an object whose structure was never built is left out) */
		count = 1;
		{
			int index;

			for (index = 0; index < rt.instance_count; index++)
			{
				if (pending_slot[index] >= 0 && !rt.slots[pending_slot[index]].built)
					continue;
				instances[count++] = pending[index];
			}
		}
		geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		geometry.geometry.instances.data.deviceAddress = rt.instances[frame].address;
		build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
		build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		build.geometryCount = 1;
		build.pGeometries = &geometry;
		if (tlas_fit(&build))
		{
			VkWriteDescriptorSetAccelerationStructureKHR structure =
				{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
			VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			int width = trace->corners[2] - trace->corners[0], height = trace->corners[3] - trace->corners[1];

			build.dstAccelerationStructure = rt.tlas[frame];
			build.scratchData.deviceAddress = align_up(rt.tlas_scratch[frame].address, rt.scratch_alignment);
			range.primitiveCount = count;
			rt.CmdBuildAccelerationStructuresKHR(commands, 1, &build, &ranges);
			barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
			barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
			rt.CmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
			rt.CmdWriteTimestamp(commands, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, rt.queries,
				(unsigned int)(frame * STAMPS + stamped++));
			rt.CmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, rt.queries,
				(unsigned int)(frame * STAMPS + stamped++));
			structure.accelerationStructureCount = 1;
			structure.pAccelerationStructures = &rt.tlas[frame];
			write.pNext = &structure;
			write.dstSet = rt.sets[frame];
			write.dstBinding = 0;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
			rt.UpdateDescriptorSets(rt.device, 1, &write, 0, NULL);
			memcpy(push.camera, trace->camera, sizeof(float) * 3);
			push.camera[3] = trace->ao_radius;
			memcpy(push.sun, trace->sun, sizeof(float) * 3);
			push.sun[3] = trace->sun[0] != 0.0f || trace->sun[1] != 0.0f || trace->sun[2] != 0.0f ? trace->sun_cone : 0.0f;
			memcpy(push.rect, trace->corners, sizeof(push.rect));
			push.misc[0] = trace->mode;
			push.misc[1] = trace->frame;
			push.misc[2] = trace->viewer_instance;
			push.misc[3] = trace->ao_rays > 0 ? trace->ao_rays : 4;
			push.more[0] = trace->shadow_rays;
			push.more[1] = trace->debug;
			push.more[2] = rt.level_render;
			push.more[3] = trace->blue_noise;
			push.grid[0] = trace->scale > 1 ? trace->scale : 1;
			push.grid[1] = trace->offset[0];
			push.grid[2] = trace->offset[1];
			push.grid[3] = trace->flags;
			push.fade[0] = trace->ao_fade[0];
			push.fade[1] = trace->ao_fade[1] > trace->ao_fade[0] ? trace->ao_fade[1] : trace->ao_fade[0] + 1.0f;
			push.fade[2] = push.fade[3] = 0.0f;
			rt.CmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, rt.pipeline);
			rt.CmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, rt.pipeline_layout, 0, 1, &rt.sets[frame],
				0, NULL);
			rt.CmdPushConstants(commands, rt.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
			/* (a thread a block: every pixel, or one of each 2x2) */
			width = (width + push.grid[0] - 1) / push.grid[0];
			height = (height + push.grid[0] - 1) / push.grid[0];
			rt.CmdDispatch(commands, (unsigned int)(width + 7) / 8, (unsigned int)(height + 7) / 8, 1);
			/* (on a grid of blocks, with the reflections: their own pass, a
			thread for each two of the grid's texels; it writes the
			reflections' image alone, the first the lighting's: no barrier) */
			if (push.grid[0] > 1 && trace->mode >= 2 && !(trace->flags & 1))
			{
				push.grid[3] = trace->flags | 8;
				rt.CmdPushConstants(commands, rt.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
				rt.CmdDispatch(commands, (unsigned int)((width + 1) / 2 + 7) / 8, (unsigned int)(height + 7) / 8, 1);
			}
			traced = 1;
		}
	}
	/* (the phases' stamps written even when nothing was traced) */
	while (stamped < STAMPS - 1)
		rt.CmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, rt.queries,
			(unsigned int)(frame * STAMPS + stamped++));
	rt.CmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, rt.queries,
		(unsigned int)(frame * STAMPS + STAMPS - 1));
	rt.EndCommandBuffer(commands);
	/* (always submitted, traced or not: OpenGL has signalled the first
	semaphore and waits on the second) */
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &rt.semaphores[0];
	submit.pWaitDstStageMask = &wait_stage;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &commands;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &rt.semaphores[1];
	waited = clock_ms();
	if (rt.QueueSubmit(rt.queue, 1, &submit, rt.fences[frame]) != VK_SUCCESS)
	{
		platform_log("ray tracing: a submit failed; off");
		rt.failed = 1;
		return -1;
	}
	rt.submit_ms += clock_ms() - waited;
	rt.timed++;
	rt.frames_submitted[frame] = 1;
	rt.frame++;
	rt.acquired = 0;
	rt.touched_count = 0;
	rt.traces += traced;
	rt.stats.instances = (int)count;
	return traced;
}

void ml_rt_frame_stats_take(struct ml_rt_frame_stats *stats)
{
	*stats = rt.stats;
	memset(&rt.stats, 0, sizeof(rt.stats));
}

float ml_rt_gpu_ms(void)
{
	return rt.gpu_ms;
}

void ml_rt_gpu_phases(float *build_ms, float *trace_ms)
{
	*build_ms = rt.gpu_build_ms;
	*trace_ms = rt.gpu_trace_ms;
}

void ml_rt_say(char *line, int size)
{
	VkDeviceSize used = 0, total = 0;
	int index;

	for (index = 0; index < rt.chunk_count; index++)
	{
		int span;

		total += rt.chunks[index].buffer.size;
		used += rt.chunks[index].buffer.size;
		for (span = 0; span < rt.chunks[index].free.count; span++)
			used -= rt.chunks[index].free.items[span].size;
	}
	snprintf(line, (size_t)size, "device=\"%s\" geometry=%s meshes=%d parts=%d textures=%d instances=%d level=%d "
		"traces=%d gpu_ms=%.3f fence_ms=%.3f submit_ms=%.3f skinned=%d built=%d refit=%d memory_mb=%.1f/%.1f",
		rt.device_name, rt.level_render ? "render" : "collision", rt.mesh_count, rt.part_count, rt.texture_count,
		rt.instance_count, rt.level_render ? rt.level.structure != VK_NULL_HANDLE :
		rt.level_collision.structure != VK_NULL_HANDLE, rt.traces, (double)rt.gpu_ms,
		rt.timed ? rt.fence_ms / rt.timed : 0.0, rt.timed ? rt.submit_ms / rt.timed : 0.0, rt.skinned, rt.built, rt.refit,
		(double)used / 1048576.0, (double)total / 1048576.0);
}

#else

#include "modern_lighting.h"

#include <string.h>

int ml_rt_why = ML_RT_WHY_NO_VULKAN;

int ml_rt_probe(const unsigned char gl_device_uuid[16])
{
	(void)gl_device_uuid;
	return 0;
}

int ml_rt_start(const unsigned char gl_device_uuid[16])
{
	(void)gl_device_uuid;
	return 0;
}

void ml_rt_stop(void)
{
}

int ml_rt_platform(void)
{
	return 0;
}

int ml_rt_integrated(void)
{
	return 0;
}

int ml_rt_ready(void)
{
	return 0;
}

void ml_rt_world(unsigned int world)
{
	(void)world;
}

int ml_rt_static_mesh(const void *key, const float *positions, int vertex_count, const unsigned int *indices,
	int triangle_count)
{
	(void)key;
	(void)positions;
	(void)vertex_count;
	(void)indices;
	(void)triangle_count;
	return 0;
}

int ml_rt_mesh_find(const void *key)
{
	(void)key;
	return -2;
}

int ml_rt_mesh_add(const void *key, const float *positions, int vertex_count, const unsigned int *indices,
	int triangle_count)
{
	(void)key;
	(void)positions;
	(void)vertex_count;
	(void)indices;
	(void)triangle_count;
	return -2;
}

int ml_rt_level_begin(const void *key)
{
	(void)key;
	return 0;
}

void ml_rt_level_material(const void *vertices, int vertex_count, int vertex_stride, const unsigned short *triangles,
	int triangle_count, int texture, const float scale[2])
{
	(void)vertices;
	(void)vertex_count;
	(void)vertex_stride;
	(void)triangles;
	(void)triangle_count;
	(void)texture;
	(void)scale;
}

int ml_rt_level_end(void)
{
	return 0;
}

int ml_rt_texture_find(const void *key)
{
	(void)key;
	return -2;
}

int ml_rt_texture_add(const void *key, const unsigned char *rgba, int width, int height)
{
	(void)key;
	(void)rgba;
	(void)width;
	(void)height;
	return -2;
}

int ml_rt_part(const void *key, const void *vertices, int vertex_count, int kind, const unsigned short *indices,
	int index_count, int strip)
{
	(void)key;
	(void)vertices;
	(void)vertex_count;
	(void)kind;
	(void)indices;
	(void)index_count;
	(void)strip;
	return -2;
}

void ml_rt_object(int slot_index, int key, const struct ml_rt_part_use *uses, int use_count, const float *matrices,
	int node_count, unsigned int custom, unsigned int mask)
{
	(void)slot_index;
	(void)key;
	(void)uses;
	(void)use_count;
	(void)matrices;
	(void)node_count;
	(void)custom;
	(void)mask;
}

void ml_rt_instances_begin(void)
{
}

void ml_rt_instance(int mesh, const float transform[12], unsigned int custom, unsigned int mask)
{
	(void)mesh;
	(void)transform;
	(void)custom;
	(void)mask;
}

int ml_rt_resize(int width, int height, struct ml_rt_shared *shared)
{
	(void)width;
	(void)height;
	(void)shared;
	return 0;
}

int ml_rt_trace(const struct ml_rt_trace *trace)
{
	(void)trace;
	return 0;
}

float ml_rt_gpu_ms(void)
{
	return -1.0f;
}

void ml_rt_gpu_phases(float *build_ms, float *trace_ms)
{
	*build_ms = *trace_ms = 0.0f;
}

void ml_rt_say(char *line, int size)
{
	if (size > 0)
		line[0] = 0;
}

void ml_rt_frame_stats_take(struct ml_rt_frame_stats *stats)
{
	memset(stats, 0, sizeof(*stats));
}

#endif
