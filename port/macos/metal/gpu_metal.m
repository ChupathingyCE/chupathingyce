/*
GPU_METAL.M

Native macOS adaptation of VISR's Metal backend, enabled by --macos-metal.
It reports its shader/resource capabilities through gpu.h and returns exact
occlusion sample counts. It preserves ordering where Metal differs from GL:

- GL applies every upload in order with the draws around it; Metal runs a
  frame's commands after they are recorded. A CPU write into a buffer or
  texture that a recorded or running command buffer reads gets new storage
  instead ("renaming", buffer_write and texture_upload).
- Frames in flight are bounded by a semaphore with FRAMES slots, signaled when
  each frame's last command buffer completes; per-frame memory (streams,
  constant snapshots) has a slot per frame.
- Render passes begin at the first draw or clear for a pair of targets and end
  when the pair changes, at copies, mipmap generation, readback and Present.

Every entry point runs in an autorelease pool: the game's calling thread
need not return to a Cocoa run loop between frames.
*/

#include "renderer/gpu.h"
#include "macos_metal.h"
#include "metal_state_cache.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/QuartzCore.h>
#include <TargetConditionals.h>
#if __has_include(<MetalFX/MetalFX.h>) && !TARGET_OS_SIMULATOR
#import <MetalFX/MetalFX.h>
#define HAVE_METALFX 1
#endif
#include <math.h>
#include <os/lock.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/resource.h>

/* the platform layer's (host_gpu.c) */
void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

/* frames in flight, as gpu_gl.c's STREAM_BUFFER_RING */
#define FRAMES 3
/* the size of a transient chunk: gpu_gl.c's STREAM_BUFFER_SIZE */
#define CHUNK_SIZE (16 * 1024 * 1024)
/* constant buffer offsets: Metal's strictest alignment on any family */
#define CONSTANT_ALIGNMENT 256

/* ---------- records

Handles index tables of these; 0 is none. */

@interface MetalTexture : NSObject
{
@public
	struct gpu_texture_description description;
	id<MTLTexture> texture;          /* nil until an upload texture's first upload */
	unsigned long levels;            /* description.levels, at most what the size allows */
	uint64_t used;                   /* the last command buffer serial that read or wrote it */
}
@end
@implementation MetalTexture
@end

@interface MetalBuffer : NSObject
{
@public
	id<MTLBuffer> buffer;
	uint64_t used;
	BOOL transient;                  /* a stream chunk: never renamed */
}
@end
@implementation MetalBuffer
@end

@interface MetalShader : NSObject
{
@public
	id<MTLFunction> function;
	/* a pixel shader's text, and its compilations with exact border colors
	(exact_borders), compiled on first use: one for each EXACT_BORDERS mask,
	nil until tried, and a bit per mask tried */
	NSString *source;
	id<MTLFunction> exact[16];
	uint16_t exact_tried;
	/* a vertex shader's library, and its function specialized for each set
	of attribute kinds it is drawn with (vertex_function) */
	id<MTLLibrary> library;
	NSMutableDictionary<NSData *, id<MTLFunction>> *specialized;
}
@end
@implementation MetalShader
@end

/* a handle table: index 0 holds NSNull. objects owns the records; lookup
mirrors them as plain pointers, which table_get reads: a draw looks up 20 or
more handles, and each was a message and an autoreleased return before */
@interface MetalTable : NSObject
{
@public
	NSMutableArray *objects;
	NSMutableIndexSet *free_handles;
	__unsafe_unretained id *lookup;
	NSUInteger lookup_capacity;
}
@end
@implementation MetalTable
- (instancetype)init
{
	if ((self = [super init]))
	{
		objects = [NSMutableArray arrayWithObject:[NSNull null]];
		free_handles = [NSMutableIndexSet indexSet];
	}
	return self;
}
- (void)mirror:(NSUInteger)handle object:(id)object
{
	if (handle >= lookup_capacity)
	{
		NSUInteger capacity = lookup_capacity ? lookup_capacity : 1024;

		while (capacity <= handle)
			capacity *= 2;
		lookup = (__unsafe_unretained id *)realloc((void *)lookup, capacity * sizeof(*lookup));
		memset((void *)(lookup + lookup_capacity), 0, (capacity - lookup_capacity) * sizeof(*lookup));
		lookup_capacity = capacity;
	}
	lookup[handle] = object;
}
- (uint32_t)add:(id)object
{
	NSUInteger handle = free_handles.firstIndex;

	if (handle == NSNotFound)
	{
		[objects addObject:object];
		handle = objects.count - 1;
	}
	else
	{
		[free_handles removeIndex:handle];
		objects[handle] = object;
	}
	[self mirror:handle object:object];
	return (uint32_t)handle;
}
- (void)remove:(uint32_t)handle
{
	if (!handle || handle >= objects.count || objects[handle] == [NSNull null])
		return;
	objects[handle] = [NSNull null];
	[self mirror:handle object:nil];
	[free_handles addIndex:handle];
}
@end

/* ---------- state */

static id<MTLDevice> device;
static id<MTLCommandQueue> queue;
static CAMetalLayer *layer;
/* reversed-Z (see depth_compare_function) */
static float reversed_depth(float depth);
@class MetalTexture;
/* foveated eyes' targets (the foveation section) */
static BOOL foveation_sizes(MetalTexture *record, MTLSize *screen, MTLSize *physical);
static id<MTLTexture> foveation_resolve(MetalTexture *record, NSUInteger width, NSUInteger height);
/* Host configuration and mono services are declared in macos_metal.h. */
#if TARGET_OS_VISION
#include "host_theater.h"
#include "host_stereo_hud.h"
/* display.immersive: frames go to the theater screen while its space is open */
static BOOL theater_wanted;
/* the cut's fade through black (gpu_metal_present_stereo) */
static float stereo_cut_brightness(int view, int covered, int requested);
#endif
static MetalTable *textures, *buffers, *shaders;
static int metal_debug;
/* display.compressed_textures, where the GPU has BC formats: DXT textures
upload as they are, not decoded to BGRA8 (gpu_capabilities.s3tc) */
static BOOL compressed_textures;
/* debug.metal_specialize = false: vertex_function returns the unspecialized
function, its fallback when specializing fails */
static BOOL specialize_off;
/* debug.fixed_timestep: visibility answers wait for the GPU (visibility_result) */
static BOOL visibility_wait;

/* the open command buffer and render pass */
static id<MTLCommandBuffer> commands;
static id<MTLRenderCommandEncoder> encoder;
/* what encoder holds, to skip calls that set it again (metal_state_cache.h;
debug.metal_state_cache) */
static struct metal_state_cache state_cache;
/* the encoder's depth clip mode: 1 after setDepthClipMode:MTLDepthClipModeClamp
(only the first-person body's draws, gpu_raster_state.depth_clamp); outside
the state cache, so a frame with no such draw makes no call and counts none */
static int encoder_depth_clamp;
_Static_assert(GPU_STAGE_COUNT == 4 && GPU_ATTRIBUTE_COUNT == 16, "metal_state_cache.h's slots");
static gpu_texture pass_color, pass_depth;
static unsigned long pass_commands;
/* the open command buffer's serial; every earlier one has been committed */
static uint64_t current_serial = 1;
static _Atomic uint64_t completed_serial;
/* the latest command buffer committed, which completes after every earlier one */
static id<MTLCommandBuffer> last_committed;

/* frames */
static dispatch_semaphore_t frame_slots;
static dispatch_group_t completion_handlers;
static int frame_started;
static unsigned long frame_slot;
static unsigned long frames;
static unsigned long renames;

/* frame pacing (pacing_schedule): GPU time of completed command buffers, and
the time this frame's CPU spent waiting rather than working */
static _Atomic uint64_t pacing_gpu_nanoseconds;
/* The GPU runs consecutive frames' command buffers overlapped (the next one
starts while the last finishes), so their start-to-end spans overlap: adding
them up counted the shared time twice. pacing_gpu_nanoseconds counts only the
time some command buffer was running, from the spans' union. Completion
handlers can run on several threads, hence the lock. */
static os_unfair_lock gpu_busy_lock = OS_UNFAIR_LOCK_INIT;
static CFTimeInterval gpu_busy_until;

static void gpu_busy_add(CFTimeInterval start, CFTimeInterval end)
{
	CFTimeInterval counted = 0.0;

	os_unfair_lock_lock(&gpu_busy_lock);
	if (end > gpu_busy_until)
	{
		counted = end - (start > gpu_busy_until ? start : gpu_busy_until);
		gpu_busy_until = end;
	}
	os_unfair_lock_unlock(&gpu_busy_lock);
	if (counted > 0.0)
		atomic_fetch_add(&pacing_gpu_nanoseconds, (uint64_t)(counted * 1e9));
}
static CFTimeInterval pacing_waited;

#if TARGET_OS_VISION
/* the presenters' command buffers (host_stereo.m, host_theater.m): their GPU
time counts with the game's, so stereo's frame-time line has the whole
frame's */
void gpu_metal_count_gpu_time(id<MTLCommandBuffer> commands)
{
	[commands addCompletedHandler:^(id<MTLCommandBuffer> completed)
	{
		if (completed.GPUEndTime > completed.GPUStartTime)
			gpu_busy_add(completed.GPUStartTime, completed.GPUEndTime);
	}];
}
#endif

/* ---------- visibility tests: state (the functions are after the draws)

A test counts into entries of a ring in a shared buffer, in Metal's boolean
mode, one entry for each render pass its draws reach: a pass writes its
entries when it ends, over what they held. When a test ends, its slot and
entries join the open command buffer's list, and that buffer's completion
handler ORs each test's entries into the slot's answer, so an answer read
frames later doesn't depend on a ring entry that has since been reused. */

/* Metal's largest visibility offset on iOS is 65,528 bytes */
#define VISIBILITY_ENTRIES 8191

/* what a command buffer's completion handler resolves */
struct visibility_pending
{
	uint32_t slot, first, count;
};

static id<MTLBuffer> visibility_buffer;
/* Completion handlers can run out of order. Keep the newest exact count
   under a small lock instead of packing a boolean into the serial. */
static os_unfair_lock visibility_answer_lock = OS_UNFAIR_LOCK_INIT;
static uint64_t visibility_answer_serials[GPU_VISIBILITY_SLOTS];
static uint32_t visibility_answers[GPU_VISIBILITY_SLOTS];
/* the serial of the command buffer each ring entry was last handed to */
static uint64_t visibility_entry_serials[VISIBILITY_ENTRIES];
static struct
{
	int active;
	unsigned long next;                /* the ring's next entry */
	unsigned long first, count;        /* the active test's entries */
	int entry_open;                    /* the open pass counts into entry first + count - 1 */
	NSMutableData *pending;            /* the open command buffer's struct visibility_pending list */
	struct
	{
		unsigned long first, count;
		uint64_t serial;
	} slots[GPU_VISIBILITY_SLOTS];
} visibility;

/* ---------- transient memory: a frame slot's streams and constant snapshots,
in chunks that live until the slot's frame comes around again */

struct transient
{
	/* chunk handles in the buffer table */
	gpu_buffer chunks[64];
	unsigned long chunk_count, chunk, offset;
};

static struct transient streams[FRAMES], snapshots[FRAMES];

/* a handle's record, or nil: an array read, inlined, with no message */
static inline __attribute__((always_inline)) id table_get(MetalTable *table, uint32_t handle)
{
	return handle < table->lookup_capacity ? table->lookup[handle] : nil;
}

static inline __attribute__((always_inline)) MetalBuffer *buffer_record(gpu_buffer handle)
{
	return table_get(buffers, handle);
}

static inline __attribute__((always_inline)) MetalTexture *texture_record(gpu_texture handle)
{
	return table_get(textures, handle);
}

/* room for size bytes at alignment in the current chunk, moving to the next
(created as needed, never smaller than CHUNK_SIZE) when it doesn't fit; never
wraps, since recorded draws may still read what is behind */
static MetalBuffer *transient_room(struct transient *memory, uint32_t size, uint32_t alignment)
{
	MetalBuffer *record;

	for (;;)
	{
		if (memory->chunk < memory->chunk_count)
		{
			record = buffer_record(memory->chunks[memory->chunk]);
			memory->offset = (memory->offset + alignment - 1) & ~(unsigned long)(alignment - 1);
			if (memory->offset + size <= record->buffer.length)
				return record;
			memory->chunk++;
			memory->offset = 0;
			continue;
		}
		if (memory->chunk_count == sizeof(memory->chunks) / sizeof(memory->chunks[0]))
			host_fatal("Metal: a frame streamed more than %lu chunks", memory->chunk_count);
		record = [MetalBuffer new];
		record->buffer = [device newBufferWithLength:size > CHUNK_SIZE ? size : CHUNK_SIZE
			options:MTLResourceStorageModeShared];
		record->transient = YES;
		memory->chunks[memory->chunk_count++] = [buffers add:record];
	}
}

/* copies size bytes into transient memory; returns the offset and sets
*buffer to the chunk's handle */
static uint32_t transient_copy(struct transient *memory, const void *data, uint32_t size, uint32_t alignment,
	gpu_buffer *buffer)
{
	MetalBuffer *record = transient_room(memory, size, alignment);
	uint32_t offset = (uint32_t)memory->offset;

	memcpy((unsigned char *)record->buffer.contents + offset, data, size);
	memory->offset += size;
	*buffer = memory->chunks[memory->chunk];
	return offset;
}

static void transient_reset(struct transient *memory)
{
	memory->chunk = 0;
	memory->offset = 0;
}

/* ---------- command buffers and frames */

static BOOL busy(uint64_t used)
{
	return used > atomic_load(&completed_serial);
}

/* waits for the frame slot about to be reused, once per frame, before
anything writes into its transient memory */
static void frame_begin(void)
{
	CFTimeInterval waited;

	if (frame_started)
		return;
	waited = CACurrentMediaTime();
	dispatch_semaphore_wait(frame_slots, DISPATCH_TIME_FOREVER);
	pacing_waited += CACurrentMediaTime() - waited;
	transient_reset(&streams[frame_slot]);
	transient_reset(&snapshots[frame_slot]);
	frame_started = 1;
}

static id<MTLCommandBuffer> command_buffer(void)
{
	frame_begin();
	if (!commands)
	{
		MTLCommandBufferDescriptor *descriptor = [MTLCommandBufferDescriptor new];

		/* each encoder's execution status in a failed buffer's error, only
		with debug.gl_debug: a debugging aid, for which the driver keeps
		track of every encoder of every frame. A failure is logged either
		way (commit). */
		if (metal_debug)
			descriptor.errorOptions = MTLCommandBufferErrorOptionEncoderExecutionStatus;
		commands = [queue commandBufferWithDescriptor:descriptor];
		if (metal_debug)
			commands.label = [NSString stringWithFormat:@"frame %lu", frames];
	}
	return commands;
}

/* the open pass's depth-stencil target, whose store is decided when the pass
ends (pass_finish) */
static id<MTLTexture> pass_depth_texture;

static void pass_attachment_traffic(id<MTLTexture> texture, MTLLoadAction load, MTLStoreAction store, double bytes_per_pixel);

/* debug.gl_debug: one frame in every 600 has its render passes and blits
listed, in the order the GPU runs them, and logged at its Present, so a GPU
trace's encoders can be matched with their targets and load and store actions;
nil in every other frame */
static NSMutableString *pass_log;

static const char *load_name(MTLLoadAction load)
{
	return load == MTLLoadActionLoad ? "load" : load == MTLLoadActionClear ? "clear" : "dontcare";
}

static const char *store_name(MTLStoreAction store)
{
	return store == MTLStoreActionStore ? "store" : store == MTLStoreActionDontCare ? "dontcare" : "unknown";
}

/* ends the open pass. The depth-stencil target is stored, unless the pass
ends the frame (Present): nothing reads the screen's depth after it, and at
the display's size it's tens of megabytes of writes a frame */
static void pass_finish(BOOL frame_end)
{
	if (encoder && pass_depth_texture)
	{
		MTLStoreAction store = frame_end ? MTLStoreActionDontCare : MTLStoreActionStore;

		[encoder setDepthStoreAction:store];
		[encoder setStencilStoreAction:store];
		pass_attachment_traffic(pass_depth_texture, MTLLoadActionDontCare, store, 5.0);
		if (pass_log)
			[pass_log appendFormat:@"; depth-stencil %s", store_name(store)];
	}
	if (encoder && pass_log)
		[pass_log appendFormat:@"; %lu draws and clears", pass_commands];
	pass_depth_texture = nil;
	[encoder endEncoding];
	encoder = nil;
	metal_state_reset(&state_cache);
	encoder_depth_clamp = 0;
	visibility.entry_open = 0;
	pass_color = pass_depth = 0;
	pass_commands = 0;
}

static void pass_end(void)
{
	pass_finish(NO);
}

/* commits the open command buffer; the frame's last one (at Present) also
frees its frame slot when it completes */
static void commit(BOOL frame_end)
{
	id<MTLCommandBuffer> committed;
	uint64_t serial = current_serial;

	NSData *pending = visibility.pending;
	id<MTLBuffer> results = visibility_buffer;

	pass_end();
	committed = command_buffer();
	visibility.pending = [NSMutableData data];
	dispatch_group_enter(completion_handlers);
	[committed addCompletedHandler:^(id<MTLCommandBuffer> completed)
	{
		const struct visibility_pending *tests = pending.bytes;
		const uint64_t *entries = results.contents;
		unsigned long test, entry;

		if (completed.status == MTLCommandBufferStatusError)
			platform_log("Metal: command buffer %llu failed: %s", (unsigned long long)serial,
				completed.error.description.UTF8String);
		if (completed.GPUEndTime > completed.GPUStartTime)
			gpu_busy_add(completed.GPUStartTime, completed.GPUEndTime);
		for (test = 0; test < pending.length / sizeof(*tests); test++)
		{
			uint64_t answer = 0;
			uint32_t slot = tests[test].slot;
			for (entry = 0; entry < tests[test].count; entry++)
			{
				uint64_t count = entries[(tests[test].first + entry) % VISIBILITY_ENTRIES];
				answer += count > UINT32_MAX - answer ? UINT32_MAX - answer : count;
			}
			os_unfair_lock_lock(&visibility_answer_lock);
			if (serial >= visibility_answer_serials[slot])
			{
				visibility_answers[slot] = (uint32_t)answer;
				visibility_answer_serials[slot] = serial;
			}
			os_unfair_lock_unlock(&visibility_answer_lock);
		}
		{
			uint64_t seen = atomic_load(&completed_serial);

			while (seen < serial && !atomic_compare_exchange_weak(&completed_serial, &seen, serial))
				;
		}
		if (frame_end)
			dispatch_semaphore_signal(frame_slots);
		dispatch_group_leave(completion_handlers);
	}];
	[committed commit];
	last_committed = committed;
	commands = nil;
	current_serial++;
	if (frame_end)
	{
		frame_started = 0;
		frame_slot = (frame_slot + 1) % FRAMES;
	}
}

static void use_texture(MetalTexture *record)
{
	if (record)
		record->used = current_serial;
}

static void use_buffer(MetalBuffer *record)
{
	if (record)
		record->used = current_serial;
}

/* ---------- textures */

static unsigned long level_dimension(uint32_t size, unsigned long level)
{
	return size >> level ? size >> level : 1;
}

static MTLPixelFormat texture_format(const struct gpu_texture_description *description)
{
	if (description->format == GPU_FORMAT_DEPTH_STENCIL)
		return MTLPixelFormatDepth32Float_Stencil8;
	/* compressed_textures is set only where these exist */
	if (@available(macOS 13.0, iOS 16.4, tvOS 16.4, visionOS 1.0, *))
	{
		if (description->format == GPU_FORMAT_BC1)
			return MTLPixelFormatBC1_RGBA;
		if (description->format == GPU_FORMAT_BC2)
			return MTLPixelFormatBC2_RGBA;
		if (description->format == GPU_FORMAT_BC3)
			return MTLPixelFormatBC3_RGBA;
	}
	return MTLPixelFormatBGRA8Unorm;
}

/* a level's bytes per row and per image: 4x4 blocks of 8 bytes (BC1) or 16
(BC2, BC3) for the compressed formats (gpu_capabilities.s3tc), else 4 bytes
a texel */
static void level_layout(uint32_t format, unsigned long width, unsigned long height,
	unsigned long *row, unsigned long *image)
{
	if (format == GPU_FORMAT_BC1 || format == GPU_FORMAT_BC2 || format == GPU_FORMAT_BC3)
	{
		*row = (width + 3) / 4 * (format == GPU_FORMAT_BC1 ? 8 : 16);
		*image = *row * ((height + 3) / 4);
	}
	else
	{
		*row = width * 4;
		*image = *row * height;
	}
}

/* the storage a texture's description asks for */
static id<MTLTexture> texture_storage(MetalTexture *record)
{
	const struct gpu_texture_description *description = &record->description;
	MTLTextureDescriptor *storage = [MTLTextureDescriptor new];

	storage.textureType = description->type == GPU_TEXTURE_CUBE ? MTLTextureTypeCube :
		description->type == GPU_TEXTURE_3D ? MTLTextureType3D : MTLTextureType2D;
	storage.pixelFormat = texture_format(description);
	storage.width = description->width ? description->width : 1;
	storage.height = description->height ? description->height : 1;
	storage.depth = description->type == GPU_TEXTURE_3D && description->depth ? description->depth : 1;
	storage.mipmapLevelCount = record->levels;
	if (description->usage == GPU_USAGE_RENDER_TARGET)
	{
		storage.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
		storage.storageMode = MTLStorageModePrivate;
	}
	else
	{
		/* the GPU only reads upload textures, so the CPU's copy is always current */
		storage.usage = MTLTextureUsageShaderRead;
		storage.storageMode = MTLStorageModeShared;
	}
	return [device newTextureWithDescriptor:storage];
}

/* a new render target's contents are undefined under Metal, and zero under
GL, which shows on a frame that presents a back buffer nothing drew. The
clear runs in a command buffer of its own, committed now, so the open pass
stays open: the queue runs it before anything that uses the target. */
static void target_zero(id<MTLTexture> texture)
{
	MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
	id<MTLCommandBuffer> zero = [queue commandBuffer];

	if (texture.pixelFormat == MTLPixelFormatDepth32Float_Stencil8)
	{
		pass.depthAttachment.texture = texture;
		pass.depthAttachment.loadAction = MTLLoadActionClear;
		pass.depthAttachment.storeAction = MTLStoreActionStore;
		pass.depthAttachment.clearDepth = reversed_depth(0.0f);
		pass.stencilAttachment.texture = texture;
		pass.stencilAttachment.loadAction = MTLLoadActionClear;
		pass.stencilAttachment.storeAction = MTLStoreActionStore;
		pass.stencilAttachment.clearStencil = 0;
	}
	else
	{
		pass.colorAttachments[0].texture = texture;
		pass.colorAttachments[0].loadAction = MTLLoadActionClear;
		pass.colorAttachments[0].storeAction = MTLStoreActionStore;
		pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
	}
	[[zero renderCommandEncoderWithDescriptor:pass] endEncoding];
	[zero commit];
}

static gpu_texture gpu_metal_texture_create(const struct gpu_texture_description *description)
{
	@autoreleasepool
	{
		MetalTexture *record = [MetalTexture new];
		uint32_t largest = description->width;
		unsigned long possible = 1;

		record->description = *description;
		if (description->height > largest)
			largest = description->height;
		if (description->type == GPU_TEXTURE_3D && description->depth > largest)
			largest = description->depth;
		while (largest >> possible)
			possible++;
		/* the D3D level count comes from the format word unchecked
		(xbox_textures.c); GL ignores levels past the smallest, Metal can't
		create them */
		record->levels = description->levels < 1 ? 1 : description->levels > possible ? possible : description->levels;
		if (description->format != GPU_FORMAT_BGRA8 && description->format != GPU_FORMAT_DEPTH_STENCIL &&
			!(compressed_textures && description->usage != GPU_USAGE_RENDER_TARGET &&
				(description->format == GPU_FORMAT_BC1 || description->format == GPU_FORMAT_BC2 ||
				description->format == GPU_FORMAT_BC3)))
			platform_log("Metal: texture format %u is not supported; it samples black", description->format);
		/* render targets have storage at once; upload textures get theirs
		from their first upload, as under GL, and sample black until then */
		else if (description->usage == GPU_USAGE_RENDER_TARGET)
		{
			record->texture = texture_storage(record);
			target_zero(record->texture);
		}
		return [textures add:record];
	}
}

static void gpu_metal_texture_upload(gpu_texture texture, uint32_t face, uint32_t level, const void *data, uint32_t size)
{
	@autoreleasepool
	{
		MetalTexture *record = texture_record(texture);
		const struct gpu_texture_description *description;
		unsigned long width, height, depth, row, image;

		if (!record || record->description.format == GPU_FORMAT_DEPTH_STENCIL || level >= record->levels ||
			(record->description.format != GPU_FORMAT_BGRA8 && !compressed_textures))
			return;
		description = &record->description;
		if (!record->texture || busy(record->used))
		{
			id<MTLTexture> previous = record->texture;

			record->texture = texture_storage(record);
			/* nothing has used the new version yet: the rest of this
			refresh's levels go into it instead of renaming it again */
			record->used = 0;
			/* a refresh starts at face 0, level 0 and uploads everything;
			anything else keeps the levels it doesn't replace */
			if (previous && (face || level))
			{
				unsigned long copy_face, copy_level, faces = description->type == GPU_TEXTURE_CUBE ? 6 : 1;

				platform_log("Metal: texture %u face %u level %u uploaded while in use", texture, face, level);
				for (copy_face = 0; copy_face < faces; copy_face++)
					for (copy_level = 0; copy_level < record->levels; copy_level++)
					{
						unsigned long w = level_dimension(description->width, copy_level);
						unsigned long h = level_dimension(description->height, copy_level);
						unsigned long d = description->type == GPU_TEXTURE_3D ? level_dimension(description->depth, copy_level) : 1;
						unsigned long copy_row, copy_image;
						NSMutableData *texels;

						level_layout(description->format, w, h, &copy_row, &copy_image);
						texels = [NSMutableData dataWithLength:copy_image * d];
						[previous getBytes:texels.mutableBytes bytesPerRow:copy_row bytesPerImage:copy_image
							fromRegion:MTLRegionMake3D(0, 0, 0, w, h, d) mipmapLevel:copy_level slice:copy_face];
						[record->texture replaceRegion:MTLRegionMake3D(0, 0, 0, w, h, d) mipmapLevel:copy_level
							slice:copy_face withBytes:texels.bytes bytesPerRow:copy_row bytesPerImage:copy_image];
					}
			}
			if (previous)
				renames++;
		}
		width = level_dimension(description->width, level);
		height = level_dimension(description->height, level);
		depth = description->type == GPU_TEXTURE_3D ? level_dimension(description->depth, level) : 1;
		level_layout(description->format, width, height, &row, &image);
		if (size < image * depth)
			return;
		[record->texture replaceRegion:MTLRegionMake3D(0, 0, 0, width, height, depth) mipmapLevel:level
			slice:description->type == GPU_TEXTURE_CUBE ? face : 0 withBytes:data bytesPerRow:row
			bytesPerImage:image];
	}
}

static void gpu_metal_texture_destroy(gpu_texture texture)
{
	@autoreleasepool
	{
		/* a committed command buffer keeps what it uses. The handle can be
		reused at once, so a pass still open on it ends first: a new target
		with the same handle would otherwise continue the old one's pass */
		if (texture && (texture == pass_color || texture == pass_depth))
			pass_end();
		[textures remove:texture];
	}
}

static uint32_t gpu_metal_texture_read(gpu_texture texture, void *pixels, uint32_t size)
{
	@autoreleasepool
	{
		MetalTexture *record = texture_record(texture);
		const struct gpu_texture_description *description = record ? &record->description : NULL;
		unsigned long width, height;
		id<MTLBuffer> staging;
		id<MTLBlitCommandEncoder> blit;

		if (!record || !record->texture || description->type != GPU_TEXTURE_2D ||
			(description->format != GPU_FORMAT_BGRA8 && description->format != GPU_FORMAT_DEPTH_STENCIL) ||
			(description->format == GPU_FORMAT_DEPTH_STENCIL && description->usage != GPU_USAGE_RENDER_TARGET))
			return 0;
		width = description->width;
		height = description->height;
		/* a foveated eye's target: resolved to its screen size first, so the
		picture has the view's shape and each pixel weighs its screen area
		(the depth's empty share), then read as an ordinary one */
		{
			MTLSize screen, physical;
			id<MTLTexture> resolved;

			if (foveation_sizes(record, &screen, &physical))
			{
				width = screen.width;
				height = screen.height;
				if (size < width * height * 4)
					return 0;
				resolved = foveation_resolve(record, width, height);
				if (!resolved)
					return 0;
				staging = [device newBufferWithLength:width * height * 4 options:MTLResourceStorageModeShared];
				blit = [command_buffer() blitCommandEncoder];
				[blit copyFromTexture:resolved sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
					sourceSize:MTLSizeMake(width, height, 1) toBuffer:staging destinationOffset:0
					destinationBytesPerRow:width * 4 destinationBytesPerImage:width * height * 4];
				[blit endEncoding];
				{
					id<MTLCommandBuffer> waited = commands;

					commit(NO);
					[waited waitUntilCompleted];
				}
				memcpy(pixels, staging.contents, width * height * 4);
				if (description->format == GPU_FORMAT_DEPTH_STENCIL)
				{
					float *depth = pixels;
					unsigned long index;

					for (index = 0; index < width * height; index++)
						depth[index] = reversed_depth(depth[index]);
				}
				return 1;
			}
		}
		if (size < width * height * 4)
			return 0;
		/* a depth target's depth plane, which holds the reversed depth
		(reversed_depth): read back as the game's */
		if (description->format == GPU_FORMAT_DEPTH_STENCIL)
		{
			float *depth = pixels;
			unsigned long index;

			staging = [device newBufferWithLength:width * height * 4 options:MTLResourceStorageModeShared];
			pass_end();
			blit = [command_buffer() blitCommandEncoder];
			[blit copyFromTexture:record->texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
				sourceSize:MTLSizeMake(width, height, 1) toBuffer:staging destinationOffset:0
				destinationBytesPerRow:width * 4 destinationBytesPerImage:width * height * 4
				options:MTLBlitOptionDepthFromDepthStencil];
			[blit endEncoding];
			use_texture(record);
			{
				id<MTLCommandBuffer> waited = commands;

				commit(NO);
				[waited waitUntilCompleted];
			}
			memcpy(pixels, staging.contents, width * height * 4);
			for (index = 0; index < width * height; index++)
				depth[index] = reversed_depth(depth[index]);
			return 1;
		}
		/* BGRA8Unorm is already the BGRA byte order gpu.h reads back, and row
		0 is the top */
		if (description->usage != GPU_USAGE_RENDER_TARGET)
		{
			[record->texture getBytes:pixels bytesPerRow:width * 4 fromRegion:MTLRegionMake2D(0, 0, width, height)
				mipmapLevel:0];
			return 1;
		}
		staging = [device newBufferWithLength:width * height * 4 options:MTLResourceStorageModeShared];
		pass_end();
		blit = [command_buffer() blitCommandEncoder];
		[blit copyFromTexture:record->texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
			sourceSize:MTLSizeMake(width, height, 1) toBuffer:staging destinationOffset:0
			destinationBytesPerRow:width * 4 destinationBytesPerImage:width * height * 4];
		[blit endEncoding];
		use_texture(record);
		{
			id<MTLCommandBuffer> waited = commands;

			commit(NO);
			[waited waitUntilCompleted];
		}
		memcpy(pixels, staging.contents, width * height * 4);
		return 1;
	}
}

/* level 0 of source into level of destination (mip composites); the size is
the destination level's, as gpu_gl.c copies it */
static void gpu_metal_texture_copy_level(gpu_texture source, gpu_texture destination, uint32_t level)
{
	@autoreleasepool
	{
		MetalTexture *from = texture_record(source), *to = texture_record(destination);
		id<MTLBlitCommandEncoder> blit;
		unsigned long width, height;

		if (!from || !to || !from->texture || !to->texture || level >= to->levels)
			return;
		width = level_dimension(to->description.width, level);
		height = level_dimension(to->description.height, level);
		if (width > from->texture.width || height > from->texture.height)
			return;
		pass_end();
		blit = [command_buffer() blitCommandEncoder];
		if (metal_debug)
			blit.label = @"copy level";
		if (pass_log)
			[pass_log appendFormat:@"\n  blit: copy %lux%lu of texture %u into level %u of texture %u", width, height, source,
				level, destination];
		[blit copyFromTexture:from->texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
			sourceSize:MTLSizeMake(width, height, 1) toTexture:to->texture destinationSlice:0
			destinationLevel:level destinationOrigin:MTLOriginMake(0, 0, 0)];
		[blit endEncoding];
		use_texture(from);
		use_texture(to);
	}
}

/* the levels after base_level from base_level */
static void gpu_metal_texture_generate_mipmaps(gpu_texture texture, uint32_t base_level)
{
	@autoreleasepool
	{
		MetalTexture *record = texture_record(texture);
		id<MTLTexture> levels;
		id<MTLBlitCommandEncoder> blit;

		if (!record || !record->texture || base_level + 1 >= record->levels)
			return;
		levels = base_level ? [record->texture newTextureViewWithPixelFormat:record->texture.pixelFormat
			textureType:MTLTextureType2D levels:NSMakeRange(base_level, record->levels - base_level)
			slices:NSMakeRange(0, 1)] : record->texture;
		pass_end();
		blit = [command_buffer() blitCommandEncoder];
		if (metal_debug)
			blit.label = @"mipmaps";
		if (pass_log)
			[pass_log appendFormat:@"\n  blit: mipmaps of texture %u (%lux%lu) from level %u, %lu levels", texture,
				(unsigned long)record->texture.width, (unsigned long)record->texture.height, base_level, record->levels];
		[blit generateMipmapsForTexture:levels];
		[blit endEncoding];
		use_texture(record);
	}
}

/* ---------- buffers and streams */

static gpu_buffer gpu_metal_buffer_create(uint32_t size)
{
	@autoreleasepool
	{
		MetalBuffer *record = [MetalBuffer new];

		record->buffer = [device newBufferWithLength:size ? size : 1 options:MTLResourceStorageModeShared];
		return [buffers add:record];
	}
}

static void gpu_metal_buffer_write(gpu_buffer buffer, uint32_t offset, uint32_t size, const void *data, uint32_t flags)
{
	@autoreleasepool
	{
		MetalBuffer *record = buffer_record(buffer);

		if (!record || offset > record->buffer.length || size > record->buffer.length - offset)
			return;
		/* a range no queued draw reads (GPU_WRITE_UNUSED), or a buffer nothing
		recorded or running uses, takes the write in place; otherwise the
		buffer gets new storage holding the old contents and the write, and
		the commands already recorded keep the old storage */
		if (!(flags & GPU_WRITE_UNUSED) && busy(record->used))
		{
			id<MTLBuffer> renamed = [device newBufferWithLength:record->buffer.length
				options:MTLResourceStorageModeShared];

			memcpy(renamed.contents, record->buffer.contents, record->buffer.length);
			record->buffer = renamed;
			record->used = 0;
			renames++;
		}
		memcpy((unsigned char *)record->buffer.contents + offset, data, size);
	}
}

static void gpu_metal_stream_reserve(uint32_t vertex_bytes, uint32_t index_bytes)
{
	@autoreleasepool
	{
		/* a new chunk never discards the frame's earlier streams, so there is
		nothing to keep together; only the slot must be ours */
		frame_begin();
	}
}

static uint32_t gpu_metal_stream(uint32_t kind, const void *data, uint32_t size, gpu_buffer *buffer)
{
	@autoreleasepool
	{
		__unsafe_unretained MetalBuffer *record;
		uint32_t offset;

		frame_begin();
		/* copies size bytes and keeps the next stream 16-aligned (gpu_gl.c
		copies the rounded size, reading past the caller's data) */
		offset = transient_copy(&streams[frame_slot], data, size, 16, buffer);
		record = buffer_record(*buffer);
		use_buffer(record);
		return offset;
	}
}

/* ---------- shaders */

/* vertex shaders' and pixel shaders' (gpu_metal_initialize) */
static MTLCompileOptions *compile_options, *pixel_compile_options;

/* debug.gl_debug: the shader work done while drawing, which a frame waits
for: shader libraries compiled, vertex functions specialized and pipelines
made (each the newLibrary, newFunction or newRenderPipelineState call
alone), with their time and the longest one; logged and reset at a frame
with any (gpu_metal_present) */
enum { COMPILE_LIBRARY, COMPILE_SPECIALIZE, COMPILE_PIPELINE, COMPILE_KINDS };
static struct
{
	unsigned long count[COMPILE_KINDS];
	double seconds[COMPILE_KINDS], longest[COMPILE_KINDS];
	unsigned long total_count[COMPILE_KINDS];
	double total_seconds[COMPILE_KINDS];
	/* made while drawing, outside warming (the map's list missed them) */
	unsigned long during_play[COMPILE_KINDS];
} compile_stats;

static void compile_count(int kind, CFTimeInterval started)
{
	double seconds = CACurrentMediaTime() - started;

	compile_stats.count[kind]++;
	compile_stats.seconds[kind] += seconds;
	if (seconds > compile_stats.longest[kind])
		compile_stats.longest[kind] = seconds;
	compile_stats.total_count[kind]++;
	compile_stats.total_seconds[kind] += seconds;
}

/* compiling at map load (gpu_warm_begin to gpu_warm_end): shaders compile
in parallel, joining warm_group until they are done, and the pipelines the
map's list names wait in warm_list for warm_end, which builds them in
parallel too */
static BOOL warming;
static dispatch_group_t warm_group;
static NSMutableData *warm_list;

/* a shader's library made into its record: its function (a vertex shader's
unspecialized one) and what later specializations or variants compile from;
NO, logged, if it can't be */
static BOOL shader_finish(MetalShader *record, uint32_t stage, NSString *text, id<MTLLibrary> library, NSError *error)
{
	/* debug.gl_debug: the warnings of a shader that compiled */
	if (library && error && metal_debug)
		platform_log("the %s shader compiled with warnings:\n%s", stage == GPU_SHADER_VERTEX ? "vertex" : "pixel",
			error.localizedDescription.UTF8String);
	if (!library)
	{
		platform_log("cannot compile the %s shader:\n%s\n%s", stage == GPU_SHADER_VERTEX ? "vertex" : "pixel",
			error.localizedDescription.UTF8String, text.UTF8String);
		return NO;
	}
	/* a vertex shader's attribute kinds are function constants, which the
	unspecialized function leaves undefined: its fetches read the kinds
	from the attribute table (nv2a_msl.c) */
	error = nil;
	record->function = stage == GPU_SHADER_VERTEX ?
		[library newFunctionWithName:@"vertex_main" constantValues:[MTLFunctionConstantValues new] error:&error] :
		[library newFunctionWithName:@"fragment_main"];
	if (!record->function)
	{
		platform_log("cannot make the %s shader's function: %s", stage == GPU_SHADER_VERTEX ? "vertex" : "pixel",
			error ? error.localizedDescription.UTF8String : "no entry point");
		return NO;
	}
	if (stage != GPU_SHADER_VERTEX)
		record->source = text;
	else
		record->library = library;
	return YES;
}

/* the translators' MSL (nv2a_msl.c); 0 if it doesn't compile, which the front
end counts as a draw skipped for its program, as under GL. While warming,
the handle comes back at once and the shader compiles in the background; one
that fails then has no function, and its draws are skipped as a pipeline
that can't be made (draw_pipeline) */
static gpu_shader gpu_metal_shader_create(uint32_t stage, const char *source)
{
	@autoreleasepool
	{
		NSError *error = nil;
		NSString *text = @(source);
		MTLCompileOptions *options = stage == GPU_SHADER_VERTEX ? compile_options : pixel_compile_options;
		MetalShader *record = [MetalShader new];
		CFTimeInterval started;
		id<MTLLibrary> library;

		/* (each on a worker thread, compiled synchronously there: not with
		newLibraryWithSource's completion handler, which runs on Metal's
		compiler queue, where shader_finish's newFunctionWithName waits on
		that same queue and libdispatch traps) */
		if (warming)
		{
			dispatch_group_async(warm_group, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^
			{
				@autoreleasepool
				{
					NSError *failure = nil;
					id<MTLLibrary> compiled = [device newLibraryWithSource:text options:options error:&failure];

					shader_finish(record, stage, text, compiled, failure);
				}
			});
			return [shaders add:record];
		}
		started = CACurrentMediaTime();
		library = [device newLibraryWithSource:text options:options error:&error];
		compile_count(COMPILE_LIBRARY, started);
		compile_stats.during_play[COMPILE_LIBRARY]++;
		if (!shader_finish(record, stage, text, library, error))
			return 0;
		return [shaders add:record];
	}
}

/* a pixel shader's function for a draw whose stages in exact (a mask of
exact_borders) rebuild their border colors: the shader compiled again with
EXACT_BORDERS on the first draw that asks for that mask. nil if it doesn't
compile (logged once), and the draw then samples the plain way. */
static id<MTLFunction> pixel_function(MetalShader *pixel, unsigned exact)
{
	if (!exact)
		return pixel->function;
	if (!(pixel->exact_tried & (1u << exact)) && pixel->source)
	{
		@autoreleasepool
		{
			NSError *error = nil;
			NSString *text = [NSString stringWithFormat:@"#define EXACT_BORDERS %u\n%@", exact, pixel->source];
			/* relaxed, as the plain variant (gpu_metal_initialize) */
			id<MTLLibrary> library = [device newLibraryWithSource:text options:pixel_compile_options error:&error];

			pixel->exact_tried |= 1u << exact;
			if (library)
				pixel->exact[exact] = [library newFunctionWithName:@"fragment_main"];
			if (!pixel->exact[exact])
				platform_log("cannot compile the pixel shader with exact border colors (stages %#x): %s", exact,
					library ? "no entry point" : error.localizedDescription.UTF8String);
		}
	}
	return pixel->exact[exact];
}

/* ---------- foveation (Task 10)

A foveated eye's targets (gpu_texture_description.foveated_eye) are
allocated at the Compositor's drawable size per view and drawn through the
eye's rate map (host_stereo_rate_map), which fills their top left: every
pass that writes one binds the map, and its viewports and scissors are in
the map's screen coordinates, the eye's logical size (target_size). What
reads them maps each screen point to where the map put it: the presenter
(host_stereo.m), the game's shaders (nv2a_msl.c's foveated sampler) and
foveation_resolve here, which draws a picture at its screen shape for the
film's paths, the screenshots and the depth reads. */

/* debug.rate_map_test (the Mac's runner): the mono screen's targets, which
the front end marks as eye 1's, render through a synthetic map the size of
the screen, made here (test_map) since there's no Compositor: full rate in
the middle third each way, half outside it, except the top band at three
quarters, so a flip shows as the dense band at the bottom. Each map with its
parameter data, by screen size */
static BOOL rate_map_test;
static NSMutableDictionary<NSString *, NSArray *> *test_maps;

static NSArray *test_map(NSUInteger width, NSUInteger height)
{
	NSString *key = [NSString stringWithFormat:@"%lux%lu", (unsigned long)width, (unsigned long)height];
	NSArray *entry = test_maps[key];

	if (!entry)
	{
		/* The samples are points from edge to edge, the rate between them
		interpolated: 31 a side put samples 10 to 20 on the middle third's
		edges and inside it, so it is all at 1.0. The sides' rates are
		nudged until the middle third's offset from the screen is even each
		way: the GPU takes derivatives over 2x2 quads in physical pixels, and
		an odd offset pairs other rows or columns than the reference does,
		which changes the texture level of detail a little on detailed
		surfaces (a10's ship, by up to 22 levels) though the remap is exact */
		enum { SAMPLES = 31 };
		static const float sides[] = { 0.5f, 0.55f, 0.45f, 0.6f, 0.4f };
		static const float tops[] = { 0.75f, 0.7f, 0.8f, 0.65f, 0.85f };
		float horizontal[SAMPLES], vertical[SAMPLES];
		id<MTLRasterizationRateMap> map = nil;
		id<MTLBuffer> parameters;
		MTLSize physical;
		MTLCoordinate2D middle = { 0.0f, 0.0f };
		float side = sides[0], top = tops[0];
		int index, tried;

		if (![device supportsRasterizationRateMapWithLayerCount:1])
		{
			platform_log("Metal: debug.rate_map_test: this GPU has no rate maps");
			rate_map_test = NO;
			return nil;
		}
		for (tried = 0; tried < 25; tried++)
		{
			MTLRasterizationRateLayerDescriptor *layer;
			float screen_x = (float)(width / 2) + 0.5f, screen_y = (float)(height / 2) + 0.5f;

			side = sides[tried % 5];
			top = tops[tried / 5];
			for (index = 0; index < SAMPLES; index++)
			{
				BOOL inside = index >= 10 && index <= 20;

				horizontal[index] = inside ? 1.0f : side;
				vertical[index] = inside ? 1.0f : index < 10 ? top : 0.5f;
			}
			layer = [[MTLRasterizationRateLayerDescriptor alloc] initWithSampleCount:MTLSizeMake(SAMPLES, SAMPLES, 0)
				horizontal:horizontal vertical:vertical];
			map = [device newRasterizationRateMapWithDescriptor:[MTLRasterizationRateMapDescriptor
				rasterizationRateMapDescriptorWithScreenSize:MTLSizeMake(width, height, 0) layer:layer]];
			if (!map)
				return nil;
			middle = [map mapScreenToPhysicalCoordinates:MTLCoordinate2DMake(screen_x, screen_y) forLayer:0];
			middle.x = screen_x - middle.x;
			middle.y = screen_y - middle.y;
			if (fmodf(middle.x, 2.0f) == 0.0f && fmodf(middle.y, 2.0f) == 0.0f)
				break;
		}
		parameters = [device newBufferWithLength:map.parameterBufferSizeAndAlign.size options:MTLResourceStorageModeShared];
		[map copyParameterDataToBuffer:parameters offset:0];
		physical = [map physicalSizeForLayer:0];
		platform_log("Metal: debug.rate_map_test: a %lux%lu screen renders through a synthetic map into %lux%lu "
			"(granularity %lux%lu; the sides at %.2f, the top at %.2f, the bottom at 0.50): the middle third is "
			"1:1, %.2f, %.2f pixels from the screen's", (unsigned long)width, (unsigned long)height,
			(unsigned long)physical.width, (unsigned long)physical.height, (unsigned long)map.physicalGranularity.width,
			(unsigned long)map.physicalGranularity.height, side, top, middle.x, middle.y);
		entry = @[ map, parameters ];
		test_maps[key] = entry;
	}
	return entry;
}

/* the eye's map for a foveated target this frame, or nil */
static id<MTLRasterizationRateMap> foveation_map(MetalTexture *record)
{
	id<MTLRasterizationRateMap> map;

	if (!record || !record->description.foveated_eye)
		return nil;
	map = host_stereo_rate_map(record->description.foveated_eye - 1);
	if (!map && rate_map_test)
		map = test_map(record->description.width, record->description.height).firstObject;
	return map;
}

/* the map's parameter data, for a shader's rasterization_rate_map_decoder */
static id<MTLBuffer> foveation_parameters(MetalTexture *record)
{
	id<MTLBuffer> parameters;

	if (!record || !record->description.foveated_eye)
		return nil;
	parameters = host_stereo_rate_map_parameters(record->description.foveated_eye - 1);
	if (!parameters && rate_map_test)
		parameters = test_map(record->description.width, record->description.height).lastObject;
	return parameters;
}

/* a foveated target's screen size (the eye's logical size, which the game's
viewports cover; a test map's, the target's) and its map's physical size;
NO if it isn't foveated this frame */
static BOOL foveation_sizes(MetalTexture *record, MTLSize *screen, MTLSize *physical)
{
	id<MTLRasterizationRateMap> map = foveation_map(record);
	int screen_width, screen_height, allocated_width, allocated_height;

	if (!map)
		return NO;
	if (host_stereo_rate_map(record->description.foveated_eye - 1))
	{
		if (!host_stereo_foveated_size(&screen_width, &screen_height, &allocated_width, &allocated_height))
			return NO;
		*screen = MTLSizeMake((NSUInteger)screen_width, (NSUInteger)screen_height, 1);
	}
	else
		*screen = MTLSizeMake(record->description.width, record->description.height, 1);
	*physical = [map physicalSizeForLayer:0];
	return YES;
}

/* ---------- render passes */

/* the targets' size in the units of their viewports and scissors: a
foveated target's screen size, else its texture's */
static void target_size(gpu_texture color, gpu_texture depth, unsigned long *width, unsigned long *height)
{
	MetalTexture *record = texture_record(color ? color : depth);
	MTLSize screen, physical;

	if (foveation_sizes(record, &screen, &physical))
	{
		*width = screen.width;
		*height = screen.height;
		return;
	}
	*width = record && record->texture ? record->texture.width : 0;
	*height = record && record->texture ? record->texture.height : 0;
}

/* the color attachment of depth-only passes, by size */
static NSMutableDictionary<NSString *, id<MTLTexture>> *scratch_colors;

static id<MTLTexture> scratch_color(NSUInteger width, NSUInteger height)
{
	NSString *key = [NSString stringWithFormat:@"%lux%lu", (unsigned long)width, (unsigned long)height];
	id<MTLTexture> texture = scratch_colors[key];

	if (!texture)
	{
		MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
			width:width height:height mipmapped:NO];

		descriptor.usage = MTLTextureUsageRenderTarget;
		descriptor.storageMode = MTLStorageModeMemoryless;
		texture = [device newTextureWithDescriptor:descriptor];
		scratch_colors[key] = texture;
	}
	return texture;
}

/* opens a pass on the pair, unless it is the open one; a load action of
Clear clears what it names before anything in the pass */
/* render passes and the memory their attachments load and store, which on a
tile-based GPU is most of what a pass costs beyond its draws; logged every
600 frames (gpu_metal_present) */
static struct
{
	unsigned long passes;
	double loaded, stored;           /* bytes */
} pass_traffic;

static void pass_attachment_traffic(id<MTLTexture> texture, MTLLoadAction load, MTLStoreAction store, double bytes_per_pixel)
{
	double bytes;

	if (!texture)
		return;
	bytes = (double)texture.width * (double)texture.height * bytes_per_pixel;
	if (load == MTLLoadActionLoad)
		pass_traffic.loaded += bytes;
	if (store == MTLStoreActionStore)
		pass_traffic.stored += bytes;
}

static void pass_traffic_count(MTLRenderPassDescriptor *pass)
{
	pass_traffic.passes++;
	pass_attachment_traffic(pass.colorAttachments[0].texture, pass.colorAttachments[0].loadAction,
		pass.colorAttachments[0].storeAction, 4.0);
	pass_attachment_traffic(pass.depthAttachment.texture, pass.depthAttachment.loadAction,
		pass.depthAttachment.storeAction, 4.0);
	pass_attachment_traffic(pass.stencilAttachment.texture, pass.stencilAttachment.loadAction,
		pass.stencilAttachment.storeAction, 1.0);
}

static BOOL pass_begin(gpu_texture color, gpu_texture depth, const struct gpu_clear *clear)
{
	MetalTexture *color_record = texture_record(color), *depth_record = texture_record(depth);
	MTLRenderPassDescriptor *pass;

	if (encoder && pass_color == color && pass_depth == depth && !clear)
		return YES;
	if ((color && (!color_record || !color_record->texture)) || (depth && (!depth_record || !depth_record->texture)) ||
		(!color && !depth))
		return NO;
	pass_end();
	pass = [MTLRenderPassDescriptor renderPassDescriptor];
	pass.visibilityResultBuffer = visibility_buffer;
	/* a foveated eye's targets render through its map; both attachments are
	the eye's then, at the same allocated size */
	pass.rasterizationRateMap = foveation_map(color ? color_record : depth_record);
	if (color && depth && foveation_map(color_record) != foveation_map(depth_record))
	{
		static int logged;

		if (!logged++)
			platform_log("Metal: a pass pairs targets %u and %u, of which only one is a foveated eye's", color, depth);
	}
	if (!color)
	{
		/* a depth-only pass: the game's pixel shaders still write a color
		(and may discard), so they need an attachment to write it to, which
		never leaves the GPU's tile memory */
		pass.colorAttachments[0].texture = scratch_color(depth_record->texture.width, depth_record->texture.height);
		pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
		pass.colorAttachments[0].storeAction = MTLStoreActionDontCare;
	}
	else
	{
		pass.colorAttachments[0].texture = color_record->texture;
		pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
		pass.colorAttachments[0].storeAction = MTLStoreActionStore;
		if (clear && (clear->flags & GPU_CLEAR_COLOR))
		{
			float red = (float)((clear->color >> 16) & 0xff) / 255.0f, green = (float)((clear->color >> 8) & 0xff) / 255.0f;
			float blue = (float)(clear->color & 0xff) / 255.0f, alpha = (float)(clear->color >> 24) / 255.0f;

			pass.colorAttachments[0].loadAction = MTLLoadActionClear;
			pass.colorAttachments[0].clearColor = MTLClearColorMake(red, green, blue, alpha);
		}
		use_texture(color_record);
	}
	if (depth)
	{
		pass.depthAttachment.texture = depth_record->texture;
		pass.depthAttachment.loadAction = clear && (clear->flags & GPU_CLEAR_DEPTH) ? MTLLoadActionClear : MTLLoadActionLoad;
		pass.depthAttachment.storeAction = MTLStoreActionUnknown;
		pass.depthAttachment.clearDepth = reversed_depth(clear ? clear->depth : 1.0f);
		pass.stencilAttachment.texture = depth_record->texture;
		pass.stencilAttachment.loadAction = clear && (clear->flags & GPU_CLEAR_STENCIL) ? MTLLoadActionClear : MTLLoadActionLoad;
		pass.stencilAttachment.storeAction = MTLStoreActionUnknown;
		pass.stencilAttachment.clearStencil = clear ? clear->stencil & 0xff : 0;
		pass_depth_texture = depth_record->texture;
		use_texture(depth_record);
	}
	pass_traffic_count(pass);
	if (pass_log)
	{
		id<MTLTexture> target = pass.colorAttachments[0].texture;

		[pass_log appendFormat:@"\n  pass: color %u %lux%lu %s/%s", color, (unsigned long)target.width,
			(unsigned long)target.height, color ? load_name(pass.colorAttachments[0].loadAction) : "memoryless",
			store_name(pass.colorAttachments[0].storeAction)];
		if (depth)
			[pass_log appendFormat:@", depth %u %lux%lu %s, stencil %s", depth, (unsigned long)depth_record->texture.width,
				(unsigned long)depth_record->texture.height, load_name(pass.depthAttachment.loadAction),
				load_name(pass.stencilAttachment.loadAction)];
	}
	encoder = [command_buffer() renderCommandEncoderWithDescriptor:pass];
	metal_state_reset(&state_cache);
	encoder_depth_clamp = 0;
	if (metal_debug)
		encoder.label = [NSString stringWithFormat:@"targets %u/%u", color, depth];
	pass_color = color;
	pass_depth = depth;
	pass_commands = 0;
	return YES;
}

/* ---------- clears */

/* the pipelines that draw a clear as a triangle covering the scissor: by
color format (or none), depth-stencil format (or none) and write mask */
static NSMutableDictionary<NSNumber *, id<MTLRenderPipelineState>> *clear_pipelines;
static NSMutableDictionary<NSNumber *, id<MTLDepthStencilState>> *clear_depth_states;
/* display.upscaler = "metalfx": a back buffer smaller than its place in the
drawable is scaled up by MetalFX's spatial scaler (edge-aware, with
sharpening) into upscaled, which the present quad then draws 1:1, rather
than stretched by the quad's bilinear sampler. The scaler is made again when
either size changes. */
static BOOL metalfx_wanted;
#ifdef HAVE_METALFX
static id<MTLFXSpatialScaler> upscaler API_AVAILABLE(ios(16.0), tvos(16.0), visionos(1.0));
#endif
static id<MTLTexture> upscaled;

static id<MTLFunction> clear_vertex, clear_fragment, present_vertex, present_fragment;
static id<MTLRenderPipelineState> present_pipeline;
static id<MTLSamplerState> present_sampler;

static NSString *const utility_source =
	@"#include <metal_stdlib>\n"
	"using namespace metal;\n"
	"struct ClearOut { float4 position [[position]]; };\n"
	"vertex ClearOut clear_vertex(uint id [[vertex_id]], constant float &depth [[buffer(0)]])\n"
	"{\n"
	"\tClearOut out;\n"
	"\tout.position = float4(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0, depth, 1.0);\n"
	"\treturn out;\n"
	"}\n"
	"fragment float4 clear_fragment(constant float4 &color [[buffer(0)]]) { return color; }\n"
	"struct PresentOut { float4 position [[position]]; float2 coordinates; };\n"
	"vertex PresentOut present_vertex(uint id [[vertex_id]])\n"
	"{\n"
	"\tPresentOut out;\n"
	"\tfloat2 corner = float2(id & 1, id >> 1);\n"
	"\tout.position = float4(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);\n"
	"\tout.coordinates = corner;\n"
	"\treturn out;\n"
	"}\n"
	"fragment float4 present_fragment(PresentOut in [[stage_in]], texture2d<float> picture [[texture(0)]],\n"
	"\tsampler linear [[sampler(0)]])\n"
	"{\n"
	"\treturn float4(picture.sample(linear, in.coordinates).rgb, 1.0);\n"
	"}\n";

static id<MTLRenderPipelineState> clear_pipeline(MTLPixelFormat color, MTLPixelFormat depth, uint32_t mask)
{
	NSNumber *key = @((color << 20) | (depth << 4) | mask);
	id<MTLRenderPipelineState> pipeline = clear_pipelines[key];

	if (!pipeline)
	{
		MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
		NSError *error = nil;

		descriptor.vertexFunction = clear_vertex;
		descriptor.fragmentFunction = clear_fragment;
		descriptor.colorAttachments[0].pixelFormat = color;
		descriptor.colorAttachments[0].writeMask = (mask & GPU_CHANNEL_RED ? MTLColorWriteMaskRed : 0) |
			(mask & GPU_CHANNEL_GREEN ? MTLColorWriteMaskGreen : 0) | (mask & GPU_CHANNEL_BLUE ? MTLColorWriteMaskBlue : 0) |
			(mask & GPU_CHANNEL_ALPHA ? MTLColorWriteMaskAlpha : 0);
		descriptor.depthAttachmentPixelFormat = depth;
		descriptor.stencilAttachmentPixelFormat = depth;
		pipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
		if (!pipeline)
			host_fatal("Metal: cannot create the clear pipeline: %s", error.description.UTF8String);
		clear_pipelines[key] = pipeline;
	}
	return pipeline;
}

static id<MTLDepthStencilState> clear_depth_state(BOOL depth, BOOL stencil)
{
	NSNumber *key = @((depth ? 1 : 0) | (stencil ? 2 : 0));
	id<MTLDepthStencilState> state = clear_depth_states[key];

	if (!state)
	{
		MTLDepthStencilDescriptor *descriptor = [MTLDepthStencilDescriptor new];
		MTLStencilDescriptor *replace = [MTLStencilDescriptor new];

		descriptor.depthCompareFunction = MTLCompareFunctionAlways;
		descriptor.depthWriteEnabled = depth;
		if (stencil)
		{
			replace.stencilCompareFunction = MTLCompareFunctionAlways;
			replace.depthStencilPassOperation = MTLStencilOperationReplace;
			replace.writeMask = 0xff;
			descriptor.frontFaceStencil = replace;
			descriptor.backFaceStencil = replace;
		}
		state = [device newDepthStencilStateWithDescriptor:descriptor];
		clear_depth_states[key] = state;
	}
	return state;
}

/* rect clamped to a width x height target; NO if nothing is left */
static BOOL clamp_rect(const struct gpu_rect *rect, unsigned long width, unsigned long height, MTLScissorRect *clamped)
{
	long left = rect->x < 0 ? 0 : rect->x, top = rect->y < 0 ? 0 : rect->y;
	long right = (long)rect->x + rect->width, bottom = (long)rect->y + rect->height;

	if (right > (long)width)
		right = (long)width;
	if (bottom > (long)height)
		bottom = (long)height;
	if (right <= left || bottom <= top)
		return NO;
	*clamped = (MTLScissorRect){ (NSUInteger)left, (NSUInteger)top, (NSUInteger)(right - left), (NSUInteger)(bottom - top) };
	return YES;
}

/* each rectangle, as glClear inside glScissor does it: no viewport, depth
range, color mask (the clear's channel_mask instead) or visibility test */
static void gpu_metal_clear(const struct gpu_clear *clear, const struct gpu_rect *rectangles, uint32_t count)
{
	@autoreleasepool
	{
		uint32_t flags = clear->flags & (GPU_CLEAR_COLOR | (clear->depth_target ? GPU_CLEAR_DEPTH | GPU_CLEAR_STENCIL : 0));
		unsigned long width, height, index;
		uint32_t mask = flags & GPU_CLEAR_COLOR ? clear->channel_mask & 0xf : 0;
		MetalTexture *color = texture_record(clear->color_target), *depth = texture_record(clear->depth_target);

		float color_value[4], depth_value = reversed_depth(clear->depth);

		frame_begin();
		if (!flags || !count || (!mask && !(flags & (GPU_CLEAR_DEPTH | GPU_CLEAR_STENCIL))))
			return;
		target_size(clear->color_target, clear->depth_target, &width, &height);
		/* a whole-target clear of every channel, before anything else in the
		pass, is the pass's load action */
		if (count == 1 && rectangles[0].x <= 0 && rectangles[0].y <= 0 &&
			(long)rectangles[0].x + rectangles[0].width >= (long)width &&
			(long)rectangles[0].y + rectangles[0].height >= (long)height &&
			(!(flags & GPU_CLEAR_COLOR) || mask == 0xf) &&
			!(encoder && pass_color == clear->color_target && pass_depth == clear->depth_target && pass_commands))
		{
			struct gpu_clear whole = *clear;

			whole.flags = flags;
			pass_begin(clear->color_target, clear->depth_target, &whole);
			return;
		}
		if (!pass_begin(clear->color_target, clear->depth_target, NULL))
			return;
		color_value[0] = (float)((clear->color >> 16) & 0xff) / 255.0f;
		color_value[1] = (float)((clear->color >> 8) & 0xff) / 255.0f;
		color_value[2] = (float)(clear->color & 0xff) / 255.0f;
		color_value[3] = (float)(clear->color >> 24) / 255.0f;
		/* every pass has a BGRA8 color attachment: a depth-only pass a scratch
		one (pass_begin), which a clear leaves alone */
		[encoder setRenderPipelineState:clear_pipeline(MTLPixelFormatBGRA8Unorm,
			depth && depth->texture ? depth->texture.pixelFormat : MTLPixelFormatInvalid, color ? mask : 0)];
		[encoder setDepthStencilState:clear_depth_state((flags & GPU_CLEAR_DEPTH) != 0, (flags & GPU_CLEAR_STENCIL) != 0)];
		[encoder setStencilReferenceValue:clear->stencil & 0xff];
		[encoder setCullMode:MTLCullModeNone];
		[encoder setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
		[encoder setViewport:(MTLViewport){ 0.0, 0.0, (double)width, (double)height, 0.0, 1.0 }];
		[encoder setVertexBytes:&depth_value length:sizeof(depth_value) atIndex:0];
		[encoder setFragmentBytes:color_value length:sizeof(color_value) atIndex:0];
		/* a clear passes no samples to a visibility test, as glClear doesn't */
		if (visibility.entry_open)
			[encoder setVisibilityResultMode:MTLVisibilityResultModeDisabled offset:0];
		for (index = 0; index < count; index++)
		{
			MTLScissorRect scissor;

			if (!clamp_rect(&rectangles[index], width, height, &scissor))
				continue;
			[encoder setScissorRect:scissor];
			[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
			pass_commands++;
		}
		/* the test's next draw opens a new entry rather than counting into
		this one again in the same pass */
		visibility.entry_open = 0;
		/* the next draw sets again what the quad changed */
		metal_state_clear_quad(&state_cache);
	}
}

/* ---------- draws */

/* per-attribute vertex buffers start here (nv2a_msl.c's bindings) */
#define VERTEX_STREAM_BINDING 10

/* the vertex shader's description of each attribute (AttributeTable,
nv2a_msl.c): the GPU_ATTRIBUTE_* format; the stream (GPU_STREAM_CONSTANT or
above: the constant); its byte offset in the buffer bound for it; and the
vertex stride */
struct metal_attribute
{
	uint32_t format, stream, offset, stride;
};

struct metal_attribute_table
{
	struct metal_attribute entries[GPU_ATTRIBUTE_COUNT];
	float constants[GPU_ATTRIBUTE_COUNT][4];
};

/* what a pipeline is made of besides the shaders' text. Every descriptor
field draw_pipeline sets must be in it (a sample count for anti-aliasing,
say): the caches return a pipeline for an equal key, and nothing checks the
descriptor against the target it draws to. */
struct pipeline_key
{
	gpu_shader vertex_shader, pixel_shader;
	uint8_t blend, source, destination, operation, write_mask, alpha_source, alpha_destination;
	/* the stages that rebuild their border colors (exact_borders) */
	uint8_t exact_borders;
	uint32_t color_format, depth_format;
	/* the vertex function's specialization (attribute_kinds) */
	uint8_t attribute_kinds[GPU_ATTRIBUTE_COUNT];
};

/* debug.gl_debug's count of pipeline changes (gpu_metal_draw), logged every
600 frames, and the last draw's shaders */
static struct
{
	unsigned long changes, same_shaders;
	gpu_shader vertex_shader, pixel_shader;
} pipeline_changes;

/* a direct-mapped cache in front of pipelines, depth_states and samplers:
nearly every draw repeats a recent key, which then costs a hash and a
compare of its bytes rather than an NSData and a dictionary lookup. The
dictionaries keep every object for good, so the cache needn't retain them. */
#define FRONT_CACHE_ENTRIES 256
#define FRONT_CACHE_KEY 48
struct front_cache
{
	struct
	{
		uint8_t key[FRONT_CACHE_KEY];
		__unsafe_unretained id object;
	} entries[FRONT_CACHE_ENTRIES];
};

static struct front_cache pipeline_cache, depth_state_cache, sampler_cache;

static unsigned long front_cache_index(const void *key, size_t length)
{
	const uint8_t *bytes = key;
	uint32_t hash = 2166136261u;
	size_t index;

	/* FNV-1a over 32-bit words (and any bytes after the last whole one) */
	for (index = 0; index + 4 <= length; index += 4)
	{
		uint32_t word;

		memcpy(&word, bytes + index, sizeof(word));
		hash = (hash ^ word) * 16777619u;
	}
	for (; index < length; index++)
		hash = (hash ^ bytes[index]) * 16777619u;
	/* the multiplies carry a difference only upward, so one in the last
	word's top byte (attribute kind 15) reaches only bits 24-31: fold those
	down into the 8 bits the index keeps */
	hash ^= hash >> 16;
	return (hash ^ (hash >> 8)) & (FRONT_CACHE_ENTRIES - 1);
}

/* the object cached for key, or nil */
static id front_cache_get(struct front_cache *cache, const void *key, size_t length)
{
	unsigned long index = front_cache_index(key, length);

	return cache->entries[index].object && !memcmp(cache->entries[index].key, key, length) ?
		cache->entries[index].object : nil;
}

static void front_cache_put(struct front_cache *cache, const void *key, size_t length, id object)
{
	unsigned long index = front_cache_index(key, length);

	memcpy(cache->entries[index].key, key, length);
	cache->entries[index].object = object;
}

static NSMutableDictionary<NSData *, id> *pipelines;
static NSMutableDictionary<NSData *, id<MTLDepthStencilState>> *depth_states;
static NSMutableDictionary<NSData *, id<MTLSamplerState>> *samplers;
/* bound where a draw has nothing: black textures of each type (as GL samples
a texture with no storage), a sampler, and a vertex buffer for constant
attributes */
static id<MTLTexture> empty_textures[4];
static id<MTLSamplerState> empty_sampler;
static id<MTLBuffer> empty_buffer;
/* the frame's last constant snapshot, which draws share until a serial moves */
static struct
{
	unsigned long frame;
	uint64_t constants_serial;
	uint32_t uniforms_serial;
	gpu_buffer buffer;
	uint32_t offset;
	BOOL valid;
} snapshot;
static BOOL layout_checked;

static MTLBlendFactor blend_factor(uint8_t factor)
{
	switch (factor)
	{
	case GPU_BLEND_ONE: return MTLBlendFactorOne;
	case GPU_BLEND_SOURCE_COLOR: return MTLBlendFactorSourceColor;
	case GPU_BLEND_ONE_MINUS_SOURCE_COLOR: return MTLBlendFactorOneMinusSourceColor;
	case GPU_BLEND_SOURCE_ALPHA: return MTLBlendFactorSourceAlpha;
	case GPU_BLEND_ONE_MINUS_SOURCE_ALPHA: return MTLBlendFactorOneMinusSourceAlpha;
	case GPU_BLEND_DESTINATION_ALPHA: return MTLBlendFactorDestinationAlpha;
	case GPU_BLEND_ONE_MINUS_DESTINATION_ALPHA: return MTLBlendFactorOneMinusDestinationAlpha;
	case GPU_BLEND_DESTINATION_COLOR: return MTLBlendFactorDestinationColor;
	case GPU_BLEND_ONE_MINUS_DESTINATION_COLOR: return MTLBlendFactorOneMinusDestinationColor;
	case GPU_BLEND_SOURCE_ALPHA_SATURATE: return MTLBlendFactorSourceAlphaSaturated;
	case GPU_BLEND_CONSTANT_COLOR: return MTLBlendFactorBlendColor;
	case GPU_BLEND_ONE_MINUS_CONSTANT_COLOR: return MTLBlendFactorOneMinusBlendColor;
	case GPU_BLEND_CONSTANT_ALPHA: return MTLBlendFactorBlendAlpha;
	case GPU_BLEND_ONE_MINUS_CONSTANT_ALPHA: return MTLBlendFactorOneMinusBlendAlpha;
	default: return MTLBlendFactorZero;
	}
}

static MTLBlendOperation blend_operation(uint8_t operation)
{
	switch (operation)
	{
	case GPU_BLEND_OP_SUBTRACT: return MTLBlendOperationSubtract;
	case GPU_BLEND_OP_REVERSE_SUBTRACT: return MTLBlendOperationReverseSubtract;
	case GPU_BLEND_OP_MIN: return MTLBlendOperationMin;
	case GPU_BLEND_OP_MAX: return MTLBlendOperationMax;
	default: return MTLBlendOperationAdd;
	}
}

/* GPU_COMPARE_* is MTLCompareFunction's order */
static MTLCompareFunction compare_function(uint8_t function)
{
	return function <= GPU_COMPARE_ALWAYS ? (MTLCompareFunction)function : MTLCompareFunctionNever;
}

/* Reversed-Z: the vertex shaders write w - z (nv2a_vsh.c's MSL epilogue), so
a depth the game gives as d is stored as 1 - d, and its depth tests and
biases point the other way. With Depth32Float that keeps depth precision at a
distance, which a longer draw distance (rasterizer_far_clip_distance) needs */
static float reversed_depth(float depth)
{
	return 1.0f - depth;
}

static MTLCompareFunction depth_compare_function(uint8_t function)
{
	switch (function)
	{
	case GPU_COMPARE_LESS: return MTLCompareFunctionGreater;
	case GPU_COMPARE_LESS_EQUAL: return MTLCompareFunctionGreaterEqual;
	case GPU_COMPARE_GREATER: return MTLCompareFunctionLess;
	case GPU_COMPARE_GREATER_EQUAL: return MTLCompareFunctionLessEqual;
	default: return compare_function(function);
	}
}

static MTLStencilOperation stencil_operation(uint8_t operation)
{
	switch (operation)
	{
	case GPU_STENCIL_ZERO: return MTLStencilOperationZero;
	case GPU_STENCIL_REPLACE: return MTLStencilOperationReplace;
	case GPU_STENCIL_INCREMENT_CLAMP: return MTLStencilOperationIncrementClamp;
	case GPU_STENCIL_DECREMENT_CLAMP: return MTLStencilOperationDecrementClamp;
	case GPU_STENCIL_INVERT: return MTLStencilOperationInvert;
	case GPU_STENCIL_INCREMENT_WRAP: return MTLStencilOperationIncrementWrap;
	case GPU_STENCIL_DECREMENT_WRAP: return MTLStencilOperationDecrementWrap;
	default: return MTLStencilOperationKeep;
	}
}

static MTLColorWriteMask write_mask(uint8_t mask)
{
	return (mask & 1 ? MTLColorWriteMaskRed : 0) | (mask & 2 ? MTLColorWriteMaskGreen : 0) |
		(mask & 4 ? MTLColorWriteMaskBlue : 0) | (mask & 8 ? MTLColorWriteMaskAlpha : 0);
}

/* with debug.gl_debug, once: struct Uniforms (nv2a_msl.c) must have struct
gpu_uniforms' layout, which the backend copies as is */
static void check_uniform_layout(MTLRenderPipelineReflection *reflection)
{
	NSMutableArray<id<MTLBinding>> *bindings = [NSMutableArray arrayWithArray:reflection.vertexBindings];
	unsigned long checked = 0, wrong = 0;

	[bindings addObjectsFromArray:reflection.fragmentBindings];
	for (id<MTLBinding> binding in bindings)
	{
		MTLStructType *type;

		if (![binding.name isEqualToString:@"u"] || binding.type != MTLBindingTypeBuffer || !binding.used)
			continue;
		type = ((id<MTLBufferBinding>)binding).bufferStructType;
#define CHECK_ROW(name, glsl_type, count, uniform_stage) GPU_UNIFORM_IF_NOT_CONSTANTS_##uniform_stage( \
		{ \
			MTLStructMember *member = [type memberByName:@#name]; \
			if (!member || member.offset != offsetof(struct gpu_uniforms, name)) \
			{ \
				platform_log("uniform layout: " #name " is at %ld in MSL, %lu in C", \
					member ? (long)member.offset : -1L, (unsigned long)offsetof(struct gpu_uniforms, name)); \
				wrong++; \
			} \
			checked++; \
		})
		GPU_UNIFORMS(CHECK_ROW)
#undef CHECK_ROW
	}
	platform_log("uniform layout: %lu rows checked, %lu wrong", checked, wrong);
}

/* the pipeline for a draw, or nil if it can't be made (cached either way) */
/* a draw's attributes, decided once (gpu_metal_draw) for both the pipeline
(the vertex function's specialization) and the bindings (bind_attributes), so
the two can't disagree: each streamed attribute's buffer record, nil for a
constant (GPU_STREAM_NONE never arrives: the front end makes every unused
attribute a constant; a stream with no buffer is one too), and its kind, as
the vertex shaders' function constants take it (nv2a_msl.c): 0 for a
constant, else its GPU_ATTRIBUTE_* format */
struct draw_attributes
{
	__unsafe_unretained MetalBuffer *records[GPU_ATTRIBUTE_COUNT];
	uint8_t kinds[GPU_ATTRIBUTE_COUNT];
};

static void draw_attributes_decide(const struct gpu_draw *draw, struct draw_attributes *attributes)
{
	int index;

	for (index = 0; index < GPU_ATTRIBUTE_COUNT; index++)
	{
		const struct gpu_vertex_attribute *attribute = &draw->attributes[index];

		attributes->records[index] = attribute->stream < GPU_STREAM_CONSTANT ?
			buffer_record(draw->streams[attribute->stream].buffer) : nil;
		attributes->kinds[index] = attributes->records[index] ? attribute->format : 0;
	}
}

/* a vertex shader's function specialized for these attribute kinds, which
folds its fetches' format switches away; made with the pipeline, so once
for each pair. The unspecialized function if it can't be made (logged
once): it reads the same kinds from the attribute table. */
static id<MTLFunction> vertex_function(MetalShader *vertex, const uint8_t kinds[GPU_ATTRIBUTE_COUNT])
{
	NSData *name = [NSData dataWithBytes:kinds length:GPU_ATTRIBUTE_COUNT];
	id<MTLFunction> function;

	if (!vertex->library || specialize_off)
		return vertex->function;
	if (!vertex->specialized)
		vertex->specialized = [NSMutableDictionary dictionary];
	function = vertex->specialized[name];
	if (!function)
	{
		MTLFunctionConstantValues *values = [MTLFunctionConstantValues new];
		NSError *error = nil;
		NSUInteger index;
		CFTimeInterval started;

		for (index = 0; index < GPU_ATTRIBUTE_COUNT; index++)
		{
			uint32_t kind = kinds[index];

			[values setConstantValue:&kind type:MTLDataTypeUInt atIndex:index];
		}
		started = CACurrentMediaTime();
		function = [vertex->library newFunctionWithName:@"vertex_main" constantValues:values error:&error];
		compile_count(COMPILE_SPECIALIZE, started);
		compile_stats.during_play[COMPILE_SPECIALIZE]++;
		if (!function)
		{
			/* (once for each shader and set of kinds, which the dictionary
			then keeps with the fallback) */
			platform_log("Metal: cannot specialize vertex shader %p for its attributes, which it then reads per draw: %s",
				(__bridge void *)vertex, error.localizedDescription.UTF8String);
			function = vertex->function;
		}
		vertex->specialized[name] = function;
	}
	return function;
}

/* a pipeline's descriptor, its functions made first (vertex_function,
pixel_function), which may compile */
static MTLRenderPipelineDescriptor *pipeline_descriptor(const struct pipeline_key *key, MetalShader *vertex,
	MetalShader *pixel)
{
	MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
	MTLRenderPipelineColorAttachmentDescriptor *attachment = descriptor.colorAttachments[0];

	descriptor.vertexFunction = vertex_function(vertex, key->attribute_kinds);
	descriptor.fragmentFunction = pixel_function(pixel, key->exact_borders) ?: pixel->function;
	attachment.pixelFormat = (MTLPixelFormat)key->color_format;
	attachment.writeMask = write_mask(key->write_mask);
	if (key->blend)
	{
		attachment.blendingEnabled = YES;
		attachment.sourceRGBBlendFactor = blend_factor(key->source);
		attachment.sourceAlphaBlendFactor = blend_factor(key->alpha_source);
		attachment.destinationRGBBlendFactor = blend_factor(key->destination);
		attachment.destinationAlphaBlendFactor = blend_factor(key->alpha_destination);
		attachment.rgbBlendOperation = attachment.alphaBlendOperation = blend_operation(key->operation);
	}
	descriptor.depthAttachmentPixelFormat = (MTLPixelFormat)key->depth_format;
	descriptor.stencilAttachmentPixelFormat = (MTLPixelFormat)key->depth_format;
	return descriptor;
}

/* the pipelines this device has compiled before, kept between launches in
the app's caches folder: a pipeline found there is loaded rather than
compiled, and one that isn't is compiled and added, then written out at the
next warm_end (archive_save). The file's name holds the app's build, the
GPU and the system's version, which an archive is only good for; any other
file there is a stale one and is deleted, and one that can't be read is
replaced by an empty archive. */
static id<MTLBinaryArchive> archive;
static NSURL *archive_url;
static BOOL archive_dirty;
/* the archive's reads (pipelines loaded from it, in parallel at warm_end)
share this lock, and its writes (pipelines added, the file written) hold it
alone: Metal's headers promise nothing about using one archive from several
threads at once */
static pthread_rwlock_t archive_lock = PTHREAD_RWLOCK_INITIALIZER;

static void archive_open(void)
{
	NSFileManager *files = [NSFileManager defaultManager];
	/* (the bundle's own folder: an app outside a sandbox, the Mac app, shares
	its caches folder with every other) */
	NSURL *folder = [[[files URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask].firstObject
		URLByAppendingPathComponent:[NSBundle mainBundle].bundleIdentifier ?: @"VISR" isDirectory:YES]
		URLByAppendingPathComponent:@"metal-pipelines" isDirectory:YES];
	NSString *build = [NSBundle mainBundle].infoDictionary[@"CFBundleVersion"] ?: @"0";
	NSString *system = [NSProcessInfo processInfo].operatingSystemVersionString;
	NSMutableString *name = [NSMutableString stringWithFormat:@"%@ %@ %@", build, device.name, system];
	MTLBinaryArchiveDescriptor *descriptor = [MTLBinaryArchiveDescriptor new];
	NSError *error = nil;
	NSUInteger index;

	/* a file name: letters, digits, dots and dashes */
	for (index = 0; index < name.length; index++)
	{
		unichar c = [name characterAtIndex:index];

		if (!(isalnum(c) || c == '.' || c == '-'))
			[name replaceCharactersInRange:NSMakeRange(index, 1) withString:@"_"];
	}
	[files createDirectoryAtURL:folder withIntermediateDirectories:YES attributes:nil error:nil];
	archive_url = [folder URLByAppendingPathComponent:[name stringByAppendingString:@".binarchive"]];
	for (NSURL *old in [files contentsOfDirectoryAtURL:folder includingPropertiesForKeys:nil options:0 error:nil])
	{
		if (![old.lastPathComponent isEqualToString:archive_url.lastPathComponent])
		{
			platform_log("Metal: deleting a stale pipeline archive, %s", old.lastPathComponent.UTF8String);
			[files removeItemAtURL:old error:nil];
		}
	}
	if ([files fileExistsAtPath:archive_url.path])
	{
		descriptor.url = archive_url;
		archive = [device newBinaryArchiveWithDescriptor:descriptor error:&error];
		if (!archive)
		{
			platform_log("Metal: the pipeline archive can't be read (%s); starting a new one",
				error.localizedDescription.UTF8String);
			[files removeItemAtURL:archive_url error:nil];
		}
	}
	if (!archive)
	{
		descriptor.url = nil;
		archive = [device newBinaryArchiveWithDescriptor:descriptor error:&error];
		if (!archive)
			platform_log("Metal: no pipeline archive: %s", error.localizedDescription.UTF8String);
	}
	platform_log("Metal: pipeline archive %s", archive_url.lastPathComponent.UTF8String);
}

/* writes the archive out if pipelines were added since */
static void archive_save(void)
{
	NSError *error = nil;
	CFTimeInterval started = CACurrentMediaTime();

	if (!archive || !archive_dirty)
		return;
	pthread_rwlock_wrlock(&archive_lock);
	if ([archive serializeToURL:archive_url error:&error])
	{
		platform_log("Metal: wrote the pipeline archive in %.0f ms", (CACurrentMediaTime() - started) * 1e3);
		archive_dirty = NO;
	}
	else
	{
		/* (diagnosis, 2026-10-06: the Mac app's writes to its caches folder
		fail with "cannot create temporary file"; try the temporary folder,
		then move the file into place) */
		NSURL *temporary = [NSURL fileURLWithPath:[NSTemporaryDirectory()
			stringByAppendingPathComponent:archive_url.lastPathComponent]];
		NSError *second = nil, *moved = nil;
		char folder[1024];

		struct rlimit limit = { 0 };
		int descriptor, open_files = 0;
		char user_temporary[1024] = "?";

		getrlimit(RLIMIT_NOFILE, &limit);
		for (descriptor = 0; descriptor < (int)(limit.rlim_cur < 65536 ? limit.rlim_cur : 65536); descriptor++)
			open_files += fcntl(descriptor, F_GETFD) != -1;
		confstr(_CS_DARWIN_USER_TEMP_DIR, user_temporary, sizeof(user_temporary));
		platform_log("Metal: the pipeline archive write failed (%s); TMPDIR %s, user temporary folder %s, HOME %s, "
			"working folder %s, its folder %s, %d files open of %llu (hard limit %llu)",
			error.localizedDescription.UTF8String, getenv("TMPDIR") ?: "(unset)", user_temporary,
			getenv("HOME") ?: "(unset)", getcwd(folder, sizeof(folder)) ?: "?",
			[[NSFileManager defaultManager] isWritableFileAtPath:archive_url.URLByDeletingLastPathComponent.path] ?
			"writable" : "not writable", open_files, (unsigned long long)limit.rlim_cur, (unsigned long long)limit.rlim_max);
		[[NSFileManager defaultManager] removeItemAtURL:temporary error:nil];
		if ([archive serializeToURL:temporary error:&second])
		{
			[[NSFileManager defaultManager] removeItemAtURL:archive_url error:nil];
			if ([[NSFileManager defaultManager] moveItemAtURL:temporary toURL:archive_url error:&moved])
			{
				archive_dirty = NO;
				platform_log("Metal: wrote the pipeline archive by way of the temporary folder in %.0f ms (directly: %s)",
					(CACurrentMediaTime() - started) * 1e3, error.localizedDescription.UTF8String);
			}
			else
				platform_log("Metal: wrote the pipeline archive to the temporary folder but can't move it: %s",
					moved.localizedDescription.UTF8String);
		}
		else
			platform_log("Metal: can't write the pipeline archive %s: %s; nor to the temporary folder: %s",
				archive_url.path.UTF8String, error.description.UTF8String, second.description.UTF8String);
	}
	pthread_rwlock_unlock(&archive_lock);
}

/* the pipeline for a descriptor, or NSNull, logged, if it can't be made
(as a GL link failure: the draws are skipped): loaded from the archive when
it has it, else compiled and added to it. Safe on any thread. */
static id pipeline_make(MTLRenderPipelineDescriptor *descriptor)
{
	NSError *error = nil;
	id pipeline = nil;

	if (descriptor.vertexFunction && descriptor.fragmentFunction)
	{
		if (archive)
		{
			descriptor.binaryArchives = @[archive];
			pthread_rwlock_rdlock(&archive_lock);
			pipeline = [device newRenderPipelineStateWithDescriptor:descriptor
				options:MTLPipelineOptionFailOnBinaryArchiveMiss reflection:nil error:nil];
			pthread_rwlock_unlock(&archive_lock);
		}
		if (!pipeline)
		{
			/* (compiled without the archive: Metal would read it outside
			archive_lock, while another thread adds to it) */
			descriptor.binaryArchives = nil;
			pipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
			if (pipeline && archive)
			{
				pthread_rwlock_wrlock(&archive_lock);
				if ([archive addRenderPipelineFunctionsWithDescriptor:descriptor error:nil])
					archive_dirty = YES;
				pthread_rwlock_unlock(&archive_lock);
			}
		}
	}

	if (!pipeline)
	{
		platform_log("cannot link a shader program: %s",
			error ? error.localizedDescription.UTF8String : "a shader did not compile");
		pipeline = [NSNull null];
	}
	return pipeline;
}

/* a key's description in the front end's terms (gpu.h), and back */
static void pipeline_describe(const struct pipeline_key *key, struct gpu_pipeline_description *description)
{
	memset(description, 0, sizeof(*description));
	description->vertex_shader = key->vertex_shader;
	description->pixel_shader = key->pixel_shader;
	description->blend = key->blend;
	description->source = key->source;
	description->destination = key->destination;
	description->operation = key->operation;
	description->alpha_source = key->alpha_source;
	description->alpha_destination = key->alpha_destination;
	description->write_mask = key->write_mask;
	description->exact_borders = key->exact_borders;
	description->depth = key->depth_format != MTLPixelFormatInvalid;
	memcpy(description->attribute_kinds, key->attribute_kinds, sizeof(description->attribute_kinds));
}

static void pipeline_key_from_description(const struct gpu_pipeline_description *description, struct pipeline_key *key)
{
	memset(key, 0, sizeof(*key));
	key->vertex_shader = description->vertex_shader;
	key->pixel_shader = description->pixel_shader;
	key->blend = description->blend != 0;
	if (key->blend)
	{
		key->source = description->source;
		key->destination = description->destination;
		key->operation = description->operation;
		/* alpha's factors are color's but in the stereo HUD layer's draws
		(gpu_blend_state.alpha_separate); a list's line from before it
		carried them reads as color's (halo_shader_list_warm) */
		key->alpha_source = description->alpha_source;
		key->alpha_destination = description->alpha_destination;
	}
	key->write_mask = description->write_mask & 0xf;
	key->exact_borders = description->exact_borders & 0xf;
	/* every pass draws to BGRA8 (pass_begin's scratch color in a depth-only
	one) and depth-stencil targets are Depth32Float_Stencil8 */
	key->color_format = (uint32_t)MTLPixelFormatBGRA8Unorm;
	key->depth_format = (uint32_t)(description->depth ? MTLPixelFormatDepth32Float_Stencil8 : MTLPixelFormatInvalid);
	memcpy(key->attribute_kinds, description->attribute_kinds, sizeof(key->attribute_kinds));
}

/* pipelines built for draws outside warming, which gpu_pipeline_built_take
hands to the front end to log and record (shader_list.c), oldest first */
static NSMutableData *built_during_play;
static unsigned long built_taken;

/* the pipeline for a draw, or nil if it can't be made (cached either way) */
static id<MTLRenderPipelineState> draw_pipeline(const struct gpu_draw *draw, const struct draw_attributes *attributes,
	MetalShader *vertex, MetalShader *pixel, unsigned exact, MTLPixelFormat color, MTLPixelFormat depth)
{
	struct pipeline_key key;
	NSData *name;
	id pipeline;

	memset(&key, 0, sizeof(key));
	key.vertex_shader = draw->vertex_shader;
	key.pixel_shader = draw->pixel_shader;
	key.blend = draw->blend.enable != 0;
	/* GL sets blending's factors only while it is on (gpu_gl.c) */
	if (key.blend)
	{
		key.source = draw->blend.source;
		key.destination = draw->blend.destination;
		key.operation = draw->blend.operation;
		key.alpha_source = draw->blend.alpha_separate ? draw->blend.alpha_source : key.source;
		key.alpha_destination = draw->blend.alpha_separate ? draw->blend.alpha_destination : key.destination;
	}
	key.write_mask = draw->color_target ? draw->blend.color_write_mask & 0xf : 0;
	key.exact_borders = (uint8_t)exact;
	key.color_format = (uint32_t)color;
	key.depth_format = (uint32_t)depth;
	memcpy(key.attribute_kinds, attributes->kinds, sizeof(key.attribute_kinds));
	_Static_assert(sizeof(key) <= FRONT_CACHE_KEY, "pipeline_key fits the front cache");
	if ((pipeline = front_cache_get(&pipeline_cache, &key, sizeof(key))))
		return pipeline == [NSNull null] ? nil : pipeline;
	name = [NSData dataWithBytes:&key length:sizeof(key)];
	pipeline = pipelines[name];
	if (!pipeline)
	{
		MTLRenderPipelineDescriptor *descriptor = pipeline_descriptor(&key, vertex, pixel);
		CFTimeInterval started = CACurrentMediaTime();

		if (metal_debug && !layout_checked && descriptor.vertexFunction && descriptor.fragmentFunction)
		{
			MTLRenderPipelineReflection *reflection = nil;

			pipeline = [device newRenderPipelineStateWithDescriptor:descriptor
				options:MTLPipelineOptionBindingInfo | MTLPipelineOptionBufferTypeInfo reflection:&reflection error:nil];
			if (pipeline)
			{
				check_uniform_layout(reflection);
				layout_checked = YES;
			}
		}
		if (!pipeline)
			pipeline = pipeline_make(descriptor);
		compile_count(COMPILE_PIPELINE, started);
		compile_stats.during_play[COMPILE_PIPELINE]++;
		pipelines[name] = pipeline;
		{
			struct gpu_pipeline_description built;

			pipeline_describe(&key, &built);
			[built_during_play appendBytes:&built length:sizeof(built)];
		}
	}
	front_cache_put(&pipeline_cache, &key, sizeof(key), pipeline);
	return pipeline == [NSNull null] ? nil : pipeline;
}

static uint32_t gpu_metal_pipeline_built_take(struct gpu_pipeline_description *description)
{
	unsigned long count = built_during_play.length / sizeof(*description);

	if (built_taken >= count)
	{
		[built_during_play setLength:0];
		built_taken = 0;
		return 0;
	}
	memcpy(description, (const struct gpu_pipeline_description *)built_during_play.bytes + built_taken++,
		sizeof(*description));
	return 1;
}

/* ---------- compiling at map load (gpu.h; the front end's shader_list.c) */

static uint32_t gpu_metal_warm_list_read(const char *name, char *text, uint32_t size)
{
	@autoreleasepool
	{
		NSString *path = [[NSBundle mainBundle] pathForResource:@(name) ofType:@"txt" inDirectory:@"shader-lists"];
		NSData *list = path ? [NSData dataWithContentsOfFile:path] : nil;

		if (!list)
			return 0;
		if (list.length <= size)
			memcpy(text, list.bytes, list.length);
		return (uint32_t)list.length;
	}
}

static void gpu_metal_warm_begin(void)
{
	warming = YES;
	warm_list = [NSMutableData data];
	warm_group = dispatch_group_create();
}

static void gpu_metal_pipeline_warm(const struct gpu_pipeline_description *description)
{
	if (warming)
		[warm_list appendBytes:description length:sizeof(*description)];
}

/* waits for the shaders, then builds the listed pipelines: the vertex
functions' specializations and the pipelines themselves in parallel, the
bookkeeping (dictionaries, which aren't thread-safe) on this thread */
static void gpu_metal_warm_end(void)
{
	@autoreleasepool
	{
		const struct gpu_pipeline_description *list = warm_list.bytes;
		unsigned long count = warm_list.length / sizeof(*list), index;
		NSMutableArray *names = [NSMutableArray array], *descriptors = [NSMutableArray array];
		NSMutableSet *named = [NSMutableSet set];
		NSMutableArray *specialize = [NSMutableArray array];
		NSMutableDictionary<NSData *, id> *specialized = [NSMutableDictionary dictionary];
		CFTimeInterval started = CACurrentMediaTime(), shaders_done;
		NSMutableArray *made;

		if (!warming)
			return;
		dispatch_group_wait(warm_group, DISPATCH_TIME_FOREVER);
		warming = NO;
		shaders_done = CACurrentMediaTime();
		/* the specializations the pipelines need and don't have, made in
		parallel and filed under their shaders here */
		for (index = 0; index < count; index++)
		{
			struct pipeline_key key;
			MetalShader *vertex;
			NSMutableData *name;

			pipeline_key_from_description(&list[index], &key);
			vertex = table_get(shaders, key.vertex_shader);
			if (!vertex || !vertex->library || specialize_off)
				continue;
			name = [NSMutableData dataWithBytes:&key.vertex_shader length:sizeof(key.vertex_shader)];
			[name appendBytes:key.attribute_kinds length:sizeof(key.attribute_kinds)];
			if (vertex->specialized[[NSData dataWithBytes:key.attribute_kinds length:sizeof(key.attribute_kinds)]] ||
				specialized[name])
				continue;
			specialized[name] = [NSNull null];
			[specialize addObject:name];
		}
		{
			NSMutableArray *functions = [NSMutableArray arrayWithCapacity:specialize.count];

			for (index = 0; index < specialize.count; index++)
				[functions addObject:[NSNull null]];
			dispatch_apply(specialize.count, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^(size_t item)
			{
				NSData *name = specialize[item];
				gpu_shader handle;
				MetalShader *vertex;
				MTLFunctionConstantValues *values = [MTLFunctionConstantValues new];
				NSUInteger attribute;
				id function;

				memcpy(&handle, name.bytes, sizeof(handle));
				vertex = table_get(shaders, handle);
				for (attribute = 0; attribute < GPU_ATTRIBUTE_COUNT; attribute++)
				{
					uint32_t kind = ((const uint8_t *)name.bytes)[sizeof(handle) + attribute];

					[values setConstantValue:&kind type:MTLDataTypeUInt atIndex:attribute];
				}
				function = [vertex->library newFunctionWithName:@"vertex_main" constantValues:values error:nil];
				if (function)
				{
					@synchronized (functions)
					{
						functions[item] = function;
					}
				}
			});
			for (index = 0; index < specialize.count; index++)
			{
				NSData *name = specialize[index];
				gpu_shader handle;

				MetalShader *vertex;

				memcpy(&handle, name.bytes, sizeof(handle));
				vertex = table_get(shaders, handle);
				if (!vertex->specialized)
					vertex->specialized = [NSMutableDictionary dictionary];
				/* (one that failed gets the unspecialized function, as
				vertex_function files it, rather than compiling again there) */
				if (functions[index] == [NSNull null])
					platform_log("Metal: cannot specialize vertex shader %p for its attributes, which it then reads per draw",
						(__bridge void *)vertex);
				vertex->specialized[[name subdataWithRange:NSMakeRange(sizeof(handle), GPU_ATTRIBUTE_COUNT)]] =
					functions[index] != [NSNull null] ? functions[index] : vertex->function;
			}
		}
		/* the pixel shaders' exact-border variants the pipelines need
		(pixel_function), compiled in parallel too and filed here, so that
		pipeline_descriptor finds them made */
		{
			NSMutableArray *variants = [NSMutableArray array], *compiled = [NSMutableArray array];
			NSMutableSet *seen = [NSMutableSet set];

			for (index = 0; index < count; index++)
			{
				struct pipeline_key key;
				MetalShader *pixel;
				NSData *variant;

				pipeline_key_from_description(&list[index], &key);
				pixel = table_get(shaders, key.pixel_shader);
				if (!key.exact_borders || !pixel || !pixel->source || (pixel->exact_tried & (1u << key.exact_borders)))
					continue;
				variant = [NSData dataWithBytes:(uint32_t[2]){ key.pixel_shader, key.exact_borders } length:8];
				if ([seen containsObject:variant])
					continue;
				[seen addObject:variant];
				[variants addObject:variant];
				[compiled addObject:[NSNull null]];
			}
			dispatch_apply(variants.count, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^(size_t item)
			{
				@autoreleasepool
				{
					const uint32_t *variant = ((NSData *)variants[item]).bytes;
					MetalShader *pixel = table_get(shaders, variant[0]);
					NSString *text = [NSString stringWithFormat:@"#define EXACT_BORDERS %u\n%@", variant[1], pixel->source];
					id<MTLLibrary> library = [device newLibraryWithSource:text options:pixel_compile_options error:nil];
					id function = [library newFunctionWithName:@"fragment_main"];

					if (function)
					{
						@synchronized (compiled)
						{
							compiled[item] = function;
						}
					}
				}
			});
			for (index = 0; index < variants.count; index++)
			{
				const uint32_t *variant = ((NSData *)variants[index]).bytes;
				MetalShader *pixel = table_get(shaders, variant[0]);

				/* (one that failed is left untried, for pixel_function to compile and log) */
				if (compiled[index] == [NSNull null])
					continue;
				pixel->exact_tried |= 1u << variant[1];
				pixel->exact[variant[1]] = compiled[index];
			}
		}
		/* the descriptors, then the pipelines in parallel */
		for (index = 0; index < count; index++)
		{
			struct pipeline_key key;
			MetalShader *vertex, *pixel;
			NSData *name;

			pipeline_key_from_description(&list[index], &key);
			name = [NSData dataWithBytes:&key length:sizeof(key)];
			vertex = table_get(shaders, key.vertex_shader);
			pixel = table_get(shaders, key.pixel_shader);
			if (pipelines[name] || [named containsObject:name] || !vertex || !pixel)
				continue;
			[names addObject:name];
			[named addObject:name];
			[descriptors addObject:pipeline_descriptor(&key, vertex, pixel)];
		}
		made = [NSMutableArray arrayWithCapacity:names.count];
		for (index = 0; index < names.count; index++)
			[made addObject:[NSNull null]];
		dispatch_apply(names.count, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^(size_t item)
		{
			id pipeline = pipeline_make(descriptors[item]);

			@synchronized (made)
			{
				made[item] = pipeline;
			}
		});
		for (index = 0; index < names.count; index++)
			pipelines[names[index]] = made[index];
		warm_list = nil;
		warm_group = nil;
		archive_save();
		platform_log("Metal: warmed %lu pipelines (%lu listed, %lu specializations) in %.0f ms, after waiting %.0f ms "
			"for the shaders",
			(unsigned long)names.count, count, (unsigned long)specialize.count,
			(CACurrentMediaTime() - shaders_done) * 1e3, (shaders_done - started) * 1e3);
	}
}

static id<MTLDepthStencilState> depth_state(const struct gpu_depth_stencil_state *state)
{
	struct gpu_depth_stencil_state key;
	NSData *name;
	id<MTLDepthStencilState> result;

	/* the reference is the encoder's; tests that are off ignore the rest,
	as GL does (gpu_gl.c sets them only while on) */
	memset(&key, 0, sizeof(key));
	if (state->depth_test)
	{
		key.depth_test = 1;
		key.depth_write = state->depth_write != 0;
		key.depth_function = state->depth_function;
	}
	if (state->stencil_test)
	{
		key.stencil_test = 1;
		key.stencil_function = state->stencil_function;
		key.stencil_fail = state->stencil_fail;
		key.stencil_depth_fail = state->stencil_depth_fail;
		key.stencil_pass = state->stencil_pass;
		key.stencil_read_mask = state->stencil_read_mask & 0xff;
		key.stencil_write_mask = state->stencil_write_mask & 0xff;
	}
	_Static_assert(sizeof(key) <= FRONT_CACHE_KEY, "the depth key fits the front cache");
	if ((result = front_cache_get(&depth_state_cache, &key, sizeof(key))))
		return result;
	name = [NSData dataWithBytes:&key length:sizeof(key)];
	result = depth_states[name];
	if (!result)
	{
		MTLDepthStencilDescriptor *descriptor = [MTLDepthStencilDescriptor new];

		descriptor.depthCompareFunction = key.depth_test ? depth_compare_function(key.depth_function) : MTLCompareFunctionAlways;
		descriptor.depthWriteEnabled = key.depth_write;
		if (key.stencil_test)
		{
			MTLStencilDescriptor *stencil = [MTLStencilDescriptor new];

			stencil.stencilCompareFunction = compare_function(key.stencil_function);
			stencil.stencilFailureOperation = stencil_operation(key.stencil_fail);
			stencil.depthFailureOperation = stencil_operation(key.stencil_depth_fail);
			stencil.depthStencilPassOperation = stencil_operation(key.stencil_pass);
			stencil.readMask = key.stencil_read_mask;
			stencil.writeMask = key.stencil_write_mask;
			descriptor.frontFaceStencil = stencil;
			descriptor.backFaceStencil = stencil;
		}
		result = [device newDepthStencilStateWithDescriptor:descriptor];
		depth_states[name] = result;
	}
	front_cache_put(&depth_state_cache, &key, sizeof(key), result);
	return result;
}

/* whether the GPU has border colors (Apple7 and later, and Macs): BORDER
addressing then samples the game's border color instead of clamping to the
edge, which smeared a shadow map's edge texels into long streaks across the
ground */
static BOOL border_colors;

/* Metal's border color for a D3DCOLOR (ARGB): its own when Metal has it
(transparent black, opaque black, opaque white). Any other color (the motion
sensor's sweep has 0x46000000, the loading bar 0x05050505) is rebuilt in the
pixel shader from this sampler's transparent black border and an opaque white
one (exact_borders). */
static MTLSamplerBorderColor border_color(uint32_t color)
{
	return color == 0xffffffffu ? MTLSamplerBorderColorOpaqueWhite :
		color == 0xff000000u ? MTLSamplerBorderColorOpaqueBlack : MTLSamplerBorderColorTransparentBlack;
}

/* exact border colors, after pfista/halo-og's native Metal renderer: the
stages of a draw whose BORDER addressing has a color Metal lacks, bit n for
stage n. Each samples its texture a second time, with an opaque white border
(sampler 4 + n), and the pixel shader compiled with EXACT_BORDERS
(nv2a_msl.c) rebuilds the game's color from the two, as xemu's custom border
colors sample it. A cube texture never samples its border in Metal. Other
draws are as before. */
static unsigned exact_borders(const struct gpu_draw *draw)
{
	unsigned mask = 0;
	int stage;

	if (!border_colors)
		return 0;
	for (stage = 0; stage < GPU_STAGE_COUNT; stage++)
	{
		const struct gpu_stage *packet = &draw->stages[stage];
		const struct gpu_sampler_state *state = &packet->sampler;
		uint32_t color = state->border_color;

		if (!packet->type || packet->type == GPU_TEXTURE_CUBE ||
			color == 0 || color == 0xff000000u || color == 0xffffffffu)
			continue;
		if (state->address_u == GPU_ADDRESS_BORDER || state->address_v == GPU_ADDRESS_BORDER ||
			(packet->type == GPU_TEXTURE_3D && state->address_w == GPU_ADDRESS_BORDER))
			mask |= 1u << stage;
	}
	return mask;
}

/* a stage's sampler, mapped as gpu_gl.c maps it on OpenGL ES: every filter
but POINT is linear, ANISOTROPIC turns on anisotropy, the first level sampled
is D3D's MAXMIPLEVEL, BORDER addressing clamps to the border (border_colors)
or the edge, and the LOD bias is the shader's. white: the same with an opaque
white border, an exact border stage's second sampler. */
static id<MTLSamplerState> sampler_state(const struct gpu_sampler_state *state, BOOL white)
{
	struct
	{
		struct gpu_sampler_state state;
		uint32_t white;
	} key;
	NSData *name;
	id<MTLSamplerState> result;

	memset(&key, 0, sizeof(key));
	key.state = *state;
	key.white = white;
	_Static_assert(sizeof(key) <= FRONT_CACHE_KEY, "the sampler key fits the front cache");
	if ((result = front_cache_get(&sampler_cache, &key, sizeof(key))))
		return result;
	name = [NSData dataWithBytes:&key length:sizeof(key)];
	result = samplers[name];
	if (!result)
	{
		MTLSamplerDescriptor *descriptor = [MTLSamplerDescriptor new];
		const uint8_t addresses[3] = { state->address_u, state->address_v, state->address_w };
		MTLSamplerAddressMode modes[3];
		int axis;

		descriptor.minFilter = state->min_filter == GPU_FILTER_POINT ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
		descriptor.magFilter = state->mag_filter == GPU_FILTER_POINT ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
		descriptor.mipFilter = state->mip_filter == GPU_FILTER_NONE ? MTLSamplerMipFilterNotMipmapped :
			state->mip_filter == GPU_FILTER_POINT ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear;
		for (axis = 0; axis < 3; axis++)
			modes[axis] = addresses[axis] == GPU_ADDRESS_WRAP ? MTLSamplerAddressModeRepeat :
				addresses[axis] == GPU_ADDRESS_MIRROR ? MTLSamplerAddressModeMirrorRepeat :
				addresses[axis] == GPU_ADDRESS_BORDER && border_colors ? MTLSamplerAddressModeClampToBorderColor :
				MTLSamplerAddressModeClampToEdge;
		if (border_colors)
			descriptor.borderColor = white ? MTLSamplerBorderColorOpaqueWhite : border_color(state->border_color);
		descriptor.sAddressMode = modes[0];
		descriptor.tAddressMode = modes[1];
		descriptor.rAddressMode = modes[2];
		descriptor.lodMinClamp = (float)state->max_mip_level;
		descriptor.maxAnisotropy = state->min_filter == GPU_FILTER_ANISOTROPIC && state->max_anisotropy > 1 ?
			(state->max_anisotropy > 16 ? 16 : state->max_anisotropy) : 1;
		result = [device newSamplerStateWithDescriptor:descriptor];
		samplers[name] = result;
	}
	front_cache_put(&sampler_cache, &key, sizeof(key), result);
	return result;
}

/* the bytes of one vertex of a format, for a stride of 0, which GL reads as
tightly packed */
static uint32_t attribute_size(uint8_t format)
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

/* c[192] and struct gpu_uniforms, as the shaders read them, shared by the
frame's draws until a serial moves: a Metal draw reads everything bound, so
each snapshot is whole */
static void bind_constants(const struct gpu_constant_store *constants, const struct gpu_uniforms *uniforms)
{
	__unsafe_unretained MetalBuffer *record;

	if (!snapshot.valid || snapshot.frame != frames || snapshot.constants_serial != constants->serial ||
		snapshot.uniforms_serial != uniforms->serial)
	{
		struct transient *memory = &snapshots[frame_slot];
		unsigned char *base;

		record = transient_room(memory, sizeof(constants->c) + sizeof(*uniforms), CONSTANT_ALIGNMENT);
		snapshot.offset = (uint32_t)memory->offset;
		snapshot.buffer = memory->chunks[memory->chunk];
		base = (unsigned char *)record->buffer.contents + snapshot.offset;
		memcpy(base, constants->c, sizeof(constants->c));
		memcpy(base + sizeof(constants->c), uniforms, sizeof(*uniforms));
		memory->offset += sizeof(constants->c) + sizeof(*uniforms);
		snapshot.frame = frames;
		snapshot.constants_serial = constants->serial;
		snapshot.uniforms_serial = uniforms->serial;
		snapshot.valid = YES;
	}
	record = buffer_record(snapshot.buffer);
	use_buffer(record);
	if (metal_state_object(&state_cache, METAL_STATE_VERTEX_BUFFER, (__bridge void *)record->buffer, snapshot.offset))
		[encoder setVertexBuffer:record->buffer offset:snapshot.offset atIndex:0];
	if (metal_state_object(&state_cache, METAL_STATE_VERTEX_BUFFER + 1, (__bridge void *)record->buffer,
		snapshot.offset + sizeof(constants->c)))
		[encoder setVertexBuffer:record->buffer offset:snapshot.offset + sizeof(constants->c) atIndex:1];
	if (metal_state_object(&state_cache, METAL_STATE_FRAGMENT_BUFFER, (__bridge void *)record->buffer,
		snapshot.offset + sizeof(constants->c)))
		[encoder setFragmentBuffer:record->buffer offset:snapshot.offset + sizeof(constants->c) atIndex:0];
}

static void bind_attributes(const struct gpu_draw *draw, const struct draw_attributes *attributes)
{
	struct metal_attribute_table table;
	int index;

	memset(&table, 0, sizeof(table));
	for (index = 0; index < GPU_ATTRIBUTE_COUNT; index++)
	{
		const struct gpu_vertex_attribute *attribute = &draw->attributes[index];
		struct metal_attribute *entry = &table.entries[index];
		__unsafe_unretained MetalBuffer *record = attributes->records[index];

		entry->format = attribute->format;
		if (!record)
		{
			/* a constant (draw_attributes) */
			entry->stream = GPU_STREAM_CONSTANT;
			memcpy(table.constants[index], draw->constant_values[index], sizeof(table.constants[index]));
			if (metal_state_object(&state_cache, METAL_STATE_ATTRIBUTE_BUFFER + index, (__bridge void *)empty_buffer, 0))
				[encoder setVertexBuffer:empty_buffer offset:0 atIndex:VERTEX_STREAM_BINDING + index];
			continue;
		}
		entry->stream = attribute->stream;
		entry->offset = draw->streams[attribute->stream].offset + attribute->offset;
		entry->stride = draw->streams[attribute->stream].stride ? draw->streams[attribute->stream].stride :
			attribute_size(attribute->format);
		/* use_buffer whether or not the binding is skipped: renaming reads it */
		use_buffer(record);
		if (metal_state_object(&state_cache, METAL_STATE_ATTRIBUTE_BUFFER + index, (__bridge void *)record->buffer, 0))
			[encoder setVertexBuffer:record->buffer offset:0 atIndex:VERTEX_STREAM_BINDING + index];
	}
	[encoder setVertexBytes:&table length:sizeof(table) atIndex:2];
	metal_state_always(&state_cache);
}

/* each stage's last sampler state and its sampler: consecutive draws mostly
sample the same way, and comparing 24 bytes costs less than sampler_state's
hash of its key (4 stages a draw). The samplers dictionary keeps every
sampler for good, so these needn't retain them. */
static struct
{
	struct gpu_sampler_state state;
	__unsafe_unretained id<MTLSamplerState> sampler;
} stage_samplers[GPU_STAGE_COUNT];

static void bind_stages(const struct gpu_draw *draw, unsigned exact)
{
	int stage;
	/* a foveated eye's target read through its map (nv2a_msl.c's foveated
	sampler): the map's parameter data, and per stage its screen and
	physical sizes, then its allocated size */
	float foveation[GPU_STAGE_COUNT][2][4];
	id<MTLBuffer> foveation_map_data = nil;

	memset(foveation, 0, sizeof(foveation));
	for (stage = 0; stage < GPU_STAGE_COUNT; stage++)
	{
		const struct gpu_stage *packet = &draw->stages[stage];
		__unsafe_unretained MetalTexture *record = packet->type ? texture_record(packet->texture) : nil;
		__unsafe_unretained id<MTLTexture> texture = record ? record->texture : nil;
		__unsafe_unretained id<MTLSamplerState> sampler;
		MTLSize screen, physical;

		if (texture && foveation_sizes(record, &screen, &physical))
		{
			foveation[stage][0][0] = (float)screen.width;
			foveation[stage][0][1] = (float)screen.height;
			foveation[stage][0][2] = (float)physical.width;
			foveation[stage][0][3] = (float)physical.height;
			foveation[stage][1][0] = (float)texture.width;
			foveation[stage][1][1] = (float)texture.height;
			foveation_map_data = foveation_parameters(record);
		}

		/* a stage with no texture, or one with no storage yet, samples black,
		as GL's texture 0 or an incomplete texture does */
		if (!texture)
			texture = empty_textures[packet->type <= GPU_TEXTURE_CUBE ? packet->type : 0];
		use_texture(record);
		if (metal_state_object(&state_cache, METAL_STATE_TEXTURE + stage, (__bridge void *)texture, 0))
			[encoder setFragmentTexture:texture atIndex:stage];
		if (!packet->type)
			sampler = empty_sampler;
		else if (stage_samplers[stage].sampler &&
			!memcmp(&stage_samplers[stage].state, &packet->sampler, sizeof(packet->sampler)))
			sampler = stage_samplers[stage].sampler;
		else
		{
			sampler = sampler_state(&packet->sampler, NO);
			stage_samplers[stage].state = packet->sampler;
			stage_samplers[stage].sampler = sampler;
		}
		if (metal_state_object(&state_cache, METAL_STATE_SAMPLER + stage, (__bridge void *)sampler, 0))
			[encoder setFragmentSamplerState:sampler atIndex:stage];
	}
	/* exact border colors (exact_borders): the white samplers and the colors,
	set on every such draw, which are few (the state cache doesn't track them) */
	if (exact)
	{
		float colors[GPU_STAGE_COUNT][4];

		memset(colors, 0, sizeof(colors));
		for (stage = 0; stage < GPU_STAGE_COUNT; stage++)
		{
			uint32_t color = draw->stages[stage].sampler.border_color;

			if (!(exact & (1u << stage)))
				continue;
			[encoder setFragmentSamplerState:sampler_state(&draw->stages[stage].sampler, YES) atIndex:GPU_STAGE_COUNT + stage];
			metal_state_always(&state_cache);
			colors[stage][0] = (float)((color >> 16) & 0xff) / 255.0f;
			colors[stage][1] = (float)((color >> 8) & 0xff) / 255.0f;
			colors[stage][2] = (float)(color & 0xff) / 255.0f;
			colors[stage][3] = (float)(color >> 24) / 255.0f;
		}
		[encoder setFragmentBytes:colors length:sizeof(colors) atIndex:3];
		metal_state_always(&state_cache);
	}
	if (foveation_map_data)
	{
		/* set on every foveated draw (the state cache doesn't track them) */
		[encoder setFragmentBuffer:foveation_map_data offset:0 atIndex:1];
		metal_state_always(&state_cache);
		[encoder setFragmentBytes:foveation length:sizeof(foveation) atIndex:2];
		metal_state_always(&state_cache);
	}
}

/* fans and loops, which Metal can't draw, as 32-bit indexed lists of the
original vertices, written straight into the frame's stream memory; returns
the vertex count, 0 if there's nothing to draw */
/* room for count 32-bit indices in the frame's stream memory: its chunk's
handle and offset, and where to write them */
static uint32_t *stream_indices(uint32_t count, gpu_buffer *buffer, uint32_t *offset)
{
	struct transient *memory = &streams[frame_slot];
	MetalBuffer *record = transient_room(memory, count * (uint32_t)sizeof(uint32_t), 4);
	uint32_t *indices = (uint32_t *)((unsigned char *)record->buffer.contents + memory->offset);

	*offset = (uint32_t)memory->offset;
	*buffer = memory->chunks[memory->chunk];
	memory->offset += count * sizeof(uint32_t);
	return indices;
}

static uint32_t convert_primitive(const struct gpu_draw *draw, MTLPrimitiveType *type, gpu_buffer *buffer, uint32_t *offset)
{
	const uint16_t *source = NULL;
	uint32_t count = draw->count, converted, index, *indices;
	__unsafe_unretained MetalBuffer *record;

	if (draw->index_buffer)
	{
		__unsafe_unretained MetalBuffer *index_record = buffer_record(draw->index_buffer);

		if (!index_record || draw->index_offset + (uint64_t)count * 2 > index_record->buffer.length)
			return 0;
		source = (const uint16_t *)((const unsigned char *)index_record->buffer.contents + draw->index_offset);
	}
#define VERTEX(i) (source ? (uint32_t)source[i] : (uint32_t)(i))
	if (draw->primitive == GPU_PRIMITIVE_TRIANGLE_FAN)
	{
		if (count < 3)
			return 0;
		converted = (count - 2) * 3;
		indices = stream_indices(converted, buffer, offset);
		for (index = 0; index + 2 < count; index++)
		{
			indices[index * 3] = VERTEX(0);
			indices[index * 3 + 1] = VERTEX(index + 1);
			indices[index * 3 + 2] = VERTEX(index + 2);
		}
		*type = MTLPrimitiveTypeTriangle;
	}
	else
	{
		if (count < 2)
			return 0;
		converted = count + 1;
		indices = stream_indices(converted, buffer, offset);
		for (index = 0; index < count; index++)
			indices[index] = VERTEX(index);
		indices[count] = VERTEX(0);
		*type = MTLPrimitiveTypeLineStrip;
	}
#undef VERTEX
	record = buffer_record(*buffer);
	use_buffer(record);
	return converted;
}

static uint32_t gpu_metal_draw(const struct gpu_draw *draw, const struct gpu_constant_store *constants,
	const struct gpu_uniforms *uniforms)
{
	@autoreleasepool
	{
		/* (unretained: the tables and caches keep these for longer than the
		draw, so retaining them would only cost a retain and a release each) */
		__unsafe_unretained MetalShader *vertex = table_get(shaders, draw->vertex_shader);
		__unsafe_unretained MetalShader *pixel = table_get(shaders, draw->pixel_shader);
		__unsafe_unretained MetalTexture *depth = texture_record(draw->depth_target);
		__unsafe_unretained id<MTLRenderPipelineState> pipeline;
		unsigned long width, height;
		struct gpu_rect area;
		MTLScissorRect scissor;
		MTLPrimitiveType type;
		unsigned exact = exact_borders(draw);
		struct draw_attributes attributes;

		/* as GL's program_get: no program, no draw */
		if (!vertex || !pixel)
			return 0;
		draw_attributes_decide(draw, &attributes);
		pipeline = draw_pipeline(draw, &attributes, vertex, pixel, exact, MTLPixelFormatBGRA8Unorm,
			depth && depth->texture ? depth->texture.pixelFormat : MTLPixelFormatInvalid);
		if (!pipeline)
			return 0;
		if (!pass_begin(draw->color_target, draw->depth_target, NULL))
			return 1;
		target_size(draw->color_target, draw->depth_target, &width, &height);
		/* GL takes a scissor outside the target, Metal doesn't; an empty one
		draws nothing, but the draw happened */
		area = draw->scissor.width > 0 && draw->scissor.height > 0 ? draw->scissor :
			(struct gpu_rect){ 0, 0, (int32_t)width, (int32_t)height };
		if (!clamp_rect(&area, width, height, &scissor))
			return 1;
		if (visibility.active && !visibility.entry_open)
		{
			unsigned long entry = visibility.next;
			static int lapped;

			/* 8,191 entries in flight at once: three frames of lens flares
			are far fewer */
			if (busy(visibility_entry_serials[entry]) && !lapped)
			{
				platform_log("Metal: the visibility ring lapped a test still on the GPU; answers may be wrong");
				lapped = 1;
			}
			visibility_entry_serials[entry] = current_serial;
			if (!visibility.count)
				visibility.first = entry;
			visibility.next = (entry + 1) % VISIBILITY_ENTRIES;
			visibility.count++;
			visibility.entry_open = 1;
			((uint64_t *)visibility_buffer.contents)[entry] = 0;
			[encoder setVisibilityResultMode:MTLVisibilityResultModeCounting offset:entry * sizeof(uint64_t)];
			metal_state_always(&state_cache);
		}
		if (metal_state_object(&state_cache, METAL_STATE_PIPELINE, (__bridge void *)pipeline, 0))
		{
			[encoder setRenderPipelineState:pipeline];
			/* debug.gl_debug: how often the pipeline changes between draws of
			one pass, and how often the shaders stay the same (only blending,
			the write mask or the vertex specialization changed) */
			if (metal_debug)
			{
				pipeline_changes.changes++;
				if (pipeline_changes.vertex_shader == draw->vertex_shader &&
					pipeline_changes.pixel_shader == draw->pixel_shader)
					pipeline_changes.same_shaders++;
			}
		}
		if (metal_debug)
		{
			pipeline_changes.vertex_shader = draw->vertex_shader;
			pipeline_changes.pixel_shader = draw->pixel_shader;
		}
		{
			__unsafe_unretained id<MTLDepthStencilState> depth_stencil = depth_state(&draw->depth_stencil);
			uint32_t reference = draw->depth_stencil.stencil_reference & 0xff;
			MTLViewport viewport = { (double)draw->viewport.x, (double)draw->viewport.y,
				(double)draw->viewport.width, (double)draw->viewport.height, draw->viewport.min_z, draw->viewport.max_z };
			MTLCullMode cull = draw->raster.cull_mode == GPU_CULL_FRONT ? MTLCullModeFront :
				draw->raster.cull_mode == GPU_CULL_BACK ? MTLCullModeBack : MTLCullModeNone;
			MTLWinding winding = draw->raster.front_face == GPU_FRONT_COUNTER_CLOCKWISE ?
				MTLWindingCounterClockwise : MTLWindingClockwise;
			/* glPolygonOffset(slope, constant); filled polygons only, as on ES */
			float bias[3] = { 0.0f, 0.0f, 0.0f };
			float blend[4] = { (float)((draw->blend.color >> 16) & 0xff) / 255.0f,
				(float)((draw->blend.color >> 8) & 0xff) / 255.0f, (float)(draw->blend.color & 0xff) / 255.0f,
				(float)(draw->blend.color >> 24) / 255.0f };

			if (draw->raster.depth_bias_enable)
			{
				bias[0] = -draw->raster.depth_bias_constant;
				bias[1] = -draw->raster.depth_bias_slope;
			}
			if (metal_state_object(&state_cache, METAL_STATE_DEPTH_STENCIL, (__bridge void *)depth_stencil, 0))
				[encoder setDepthStencilState:depth_stencil];
			if (metal_state_value(&state_cache, METAL_STATE_STENCIL_REFERENCE, &reference, sizeof(reference)))
				[encoder setStencilReferenceValue:reference];
			if (metal_state_value(&state_cache, METAL_STATE_VIEWPORT, &viewport, sizeof(viewport)))
				[encoder setViewport:viewport];
			if (metal_state_value(&state_cache, METAL_STATE_SCISSOR, &scissor, sizeof(scissor)))
				[encoder setScissorRect:scissor];
			if (metal_state_value(&state_cache, METAL_STATE_CULL_MODE, &cull, sizeof(cull)))
				[encoder setCullMode:cull];
			if (metal_state_value(&state_cache, METAL_STATE_WINDING, &winding, sizeof(winding)))
				[encoder setFrontFacingWinding:winding];
			if (metal_state_value(&state_cache, METAL_STATE_DEPTH_BIAS, bias, sizeof(bias)))
				[encoder setDepthBias:bias[0] slopeScale:bias[1] clamp:bias[2]];
			if (metal_state_value(&state_cache, METAL_STATE_BLEND_COLOR, blend, sizeof(blend)))
				[encoder setBlendColorRed:blend[0] green:blend[1] blue:blend[2] alpha:blend[3]];
			if ((draw->raster.depth_clamp != 0) != encoder_depth_clamp)
			{
				encoder_depth_clamp = draw->raster.depth_clamp != 0;
				[encoder setDepthClipMode:encoder_depth_clamp ? MTLDepthClipModeClamp : MTLDepthClipModeClip];
			}
		}
		bind_stages(draw, exact);
		bind_constants(constants, uniforms);
		bind_attributes(draw, &attributes);
		pass_commands++;
		/* the draw call itself, made below unless a conversion leaves nothing */
		metal_state_always(&state_cache);
		metal_state_draw(&state_cache);
		switch (draw->primitive)
		{
		case GPU_PRIMITIVE_TRIANGLE_FAN:
		case GPU_PRIMITIVE_LINE_LOOP:
		{
			gpu_buffer indices;
			uint32_t offset, count = convert_primitive(draw, &type, &indices, &offset);

			if (count)
				[encoder drawIndexedPrimitives:type indexCount:count indexType:MTLIndexTypeUInt32
					indexBuffer:buffer_record(indices)->buffer indexBufferOffset:offset];
			return 1;
		}
		case GPU_PRIMITIVE_POINTS: type = MTLPrimitiveTypePoint; break;
		case GPU_PRIMITIVE_LINES: type = MTLPrimitiveTypeLine; break;
		case GPU_PRIMITIVE_LINE_STRIP: type = MTLPrimitiveTypeLineStrip; break;
		case GPU_PRIMITIVE_TRIANGLE_STRIP: type = MTLPrimitiveTypeTriangleStrip; break;
		default: type = MTLPrimitiveTypeTriangle; break;
		}
		if (draw->index_buffer)
		{
			__unsafe_unretained MetalBuffer *record = buffer_record(draw->index_buffer);

			if (!record)
				return 1;
			use_buffer(record);
			/* the base vertex is ignored, as on Apple's GL (base_vertex is 0) */
			[encoder drawIndexedPrimitives:type indexCount:draw->count indexType:MTLIndexTypeUInt16
				indexBuffer:record->buffer indexBufferOffset:draw->index_offset];
		}
		else
		{
			[encoder drawPrimitives:type vertexStart:0 vertexCount:draw->count];
		}
		return 1;
	}
}

/* ---------- visibility tests (the state is declared before the transient memory) */

static void gpu_metal_visibility_begin(void)
{
	visibility.active = 1;
	visibility.count = 0;
	visibility.entry_open = 0;
}

static void gpu_metal_visibility_end(uint32_t slot)
{
	if (visibility.entry_open)
		[encoder setVisibilityResultMode:MTLVisibilityResultModeDisabled offset:0];
	visibility.active = 0;
	visibility.entry_open = 0;
	visibility.slots[slot].first = visibility.first;
	visibility.slots[slot].count = visibility.count;
	visibility.slots[slot].serial = current_serial;
	if (visibility.count)
	{
		struct visibility_pending test = { slot, (uint32_t)visibility.first, (uint32_t)visibility.count };

		[visibility.pending appendBytes:&test length:sizeof(test)];
	}
}

/* The exact sample count (GPU_OCCLUSION_EXACT). With the fixed
timestep, a result the GPU hasn't written yet is waited for, committing the
open command buffer if it writes it: the game asks again until it has an
answer (lens flares), so an answer now keeps runs repeatable, as a wait under
GL does. In real time that wait makes the CPU and the GPU take turns every
frame, so the answer is the latest the GPU has written for the slot: this
test's, or while the GPU is behind, an earlier one's (a flare fades a frame
late), as desktop GL's persistent results do (gpu_gl.c) */
static uint32_t gpu_metal_visibility_result(uint32_t slot, uint32_t *samples)
{
	@autoreleasepool
	{
		*samples = 0;
		if (!visibility.slots[slot].count)
			return 1;
		if (!visibility_wait)
		{
			os_unfair_lock_lock(&visibility_answer_lock);
			*samples = visibility_answers[slot];
			os_unfair_lock_unlock(&visibility_answer_lock);
			return 1;
		}
		if (visibility.slots[slot].serial == current_serial)
			commit(NO);
		/* wait for the answer itself: waitUntilCompleted can return before
		the completion handler has run, and a later command buffer's handler
		can raise completed_serial before this one's has written the answer */
		if (dispatch_group_wait(completion_handlers, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC)))
			host_fatal("Metal: visibility completion timed out");
		os_unfair_lock_lock(&visibility_answer_lock);
		*samples = visibility_answers[slot];
		os_unfair_lock_unlock(&visibility_answer_lock);
		return 1;
	}
}

/* ---------- frame pacing

The game draws a frame whenever it can, and the interpolation blends the
world for the moment each frame is shown (render_interpolation.c). For the
motion to look even, every frame must also stay on screen equally long: a
frame shown for one refresh followed by one shown for two judders, though
both are drawn right. So each frame is presented to stay up at least a whole
number of refreshes after the one before (presentDrawable:afterMinimumDuration:;
a schedule of absolute times would have to know the compositor's latency), and
Present returns when the next frame should show, for the blend. The number of
refreshes rises at once when frames stay up longer than asked, and falls after
PACING_SETTLE frames that would fit in fewer with room to spare. With
display.frame_pacing = "tick" only numbers that give a multiple of 30 frames a
second are used (1 or 3 at 90 Hz, 1 or 2 at 60 Hz), so every game tick spans
the same number of frames; "refresh" allows any (2 at 90 Hz: 45 a second).
Pacing is off by default (display.frame_pacing = "off") until it has been
tried on a Vision Pro; frames are measured by the time between Presents
(pacing_schedule). The simulators can't hold a frame, so there it only
measures.

The refresh period comes from a display link on a thread of its own, since
the game's thread doesn't return to its run loop. Every 600 frames the log
says how many refreshes frames stayed on screen, in every mode. */

/* frames that must fit in fewer refreshes (all but PACING_OVER) before the
rate rises, and the misses (frames up longer than asked) among the last
PACING_WINDOW that lower it */
#define PACING_SETTLE 180
#define PACING_OVER 2
#define PACING_WINDOW 30
#define PACING_MISSES 3
/* a frame "fits" in n refreshes when its CPU and GPU time each take at most
this much of them */
#define PACING_HEADROOM 0.8

static os_unfair_lock pacing_lock = OS_UNFAIR_LOCK_INIT;
/* the frames a report's percentiles can cover (reports come every 600) */
#define PACING_TIMED_FRAMES 1024
static CFTimeInterval refresh_period; /* CACurrentMediaTime's clock */

static struct
{
	BOOL off, any_rate;
	long refreshes;                  /* a frame's refreshes */
	CFTimeInterval work_started;     /* when the frame being drawn began (the last Present returned) */
	uint64_t gpu_counted;            /* pacing_gpu_nanoseconds at the last Present */
	double cost;                     /* the slowest frame (CPU or GPU) since the rate last changed, in seconds */
	unsigned long over;              /* frames since then too slow for one refresh fewer */
	unsigned long since_change;
	unsigned long misses;            /* frames up longer than asked, since the window began */
	unsigned long shown[6];          /* frames that stayed up 1-4 and 5 or more refreshes, since the last report */
	CFTimeInterval last_present;     /* when Present last asked for a schedule */
	CFTimeInterval due;              /* when the next frame shows, as the schedule reckons it */
	unsigned long window;
	unsigned long reported;
	/* each frame's CPU and GPU time since the last report, for its medians
	and 95th percentiles (a loading stall would swamp an average) */
	double cpu_times[PACING_TIMED_FRAMES], gpu_times[PACING_TIMED_FRAMES];
	unsigned long timed;
} pacing;

static int compare_times(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;

	return x < y ? -1 : x > y;
}

/* the median and 95th percentile of count times, in milliseconds (sorts them) */
static void pacing_percentiles(double *times, unsigned long count, double *median, double *high)
{
	*median = *high = 0.0;
	if (!count)
		return;
	qsort(times, count, sizeof(*times), compare_times);
	*median = times[count / 2] * 1000.0;
	*high = times[count * 95 / 100] * 1000.0;
}

static void pacing_start(uint32_t flags)
{
	/* Native macOS uses CAMetalLayer display synchronization, not UIKit's
	CADisplayLink. Keep the experimental visionOS scheduling policy off. */
	(void)flags;
	pacing.off = YES;
	pacing.any_rate = NO;
	pacing.refreshes = 1;
	refresh_period = 1.0 / 60.0;
}

/* whether n refreshes a frame is allowed at this refresh rate: with "tick",
a multiple of 30 frames a second when the display runs at one */
static BOOL pacing_allowed(long n, CFTimeInterval period)
{
	double rate = 1.0 / period;
	long ticks = lround(rate / 30.0);

	if (pacing.any_rate || ticks < 1 || fabs(rate - 30.0 * (double)ticks) > 2.0)
		return n <= 4;
	return ticks % n == 0;
}

static long pacing_step(long n, int direction, CFTimeInterval period)
{
	long step;

	for (step = n + direction; step >= 1 && step <= 4; step += direction)
		if (pacing_allowed(step, period))
			return step;
	return n;
}

static void pacing_set(long refreshes, CFTimeInterval period, const char *why)
{
	platform_log("Metal: pacing %ld refresh%s a frame (%.0f frames a second): %s, slowest frame %.1f ms",
		refreshes, refreshes == 1 ? "" : "es", 1.0 / (period * (double)refreshes), why, pacing.cost * 1000.0);
	pacing.refreshes = refreshes;
	pacing.cost = 0.0;
	pacing.over = 0;
	pacing.since_change = 0;
	pacing.window = 0;
	pacing.misses = 0;
}

/* how long drawable must stay up after the frame before it (0: no minimum),
and how long from now until the next frame should show, in microseconds
(0: unknown); picks the rate for the frames after this one.

Frames are measured by the time between Presents. Present waits in
nextDrawable for a drawable the display has let go of, so once frames are
held for whole refreshes, Presents come that many refreshes apart; one that
comes later stayed up longer than asked (a miss). That works where a
drawable's presented handler never reports a time (a visionOS window, the
simulators). The next frame is due a frame's refreshes after this one, or a
frame's refreshes from now when the game ran late; only how much that lead
varies from frame to frame matters to the blend (render_interpolation.c). */
static CFTimeInterval pacing_schedule(id<CAMetalDrawable> drawable, uint32_t *next_frame_due)
{
	CFTimeInterval now = CACurrentMediaTime(), period, hold, interval;
	uint64_t gpu_total = atomic_load(&pacing_gpu_nanoseconds);
	double cpu, gpu;
	long lower, up;

	*next_frame_due = 0;
	cpu = pacing.work_started > 0.0 ? now - pacing.work_started - pacing_waited : 0.0;
	gpu = (double)(gpu_total - pacing.gpu_counted) / 1e9;
	pacing.gpu_counted = gpu_total;
	pacing_waited = 0.0;
	interval = pacing.last_present > 0.0 ? now - pacing.last_present : 0.0;
	pacing.last_present = now;
	os_unfair_lock_lock(&pacing_lock);
	period = refresh_period;
	os_unfair_lock_unlock(&pacing_lock);
	if (!drawable || period < 1.0 / 240.0 || period > 1.0 / 20.0)
		return 0.0;
	if (!pacing_allowed(pacing.refreshes, period))
		pacing.refreshes = 1;
	hold = pacing.off ? 0.0 : period * (double)pacing.refreshes;
	/* how many refreshes the frame before this one stayed up; a quarter
	second or more is a load or the background, not a frame */
	up = interval > 0.0 && interval < 0.25 ? lround(interval / period) : 0;
	if (up > 0)
	{
		pacing.shown[up > 5 ? 5 : up]++;
		if (!pacing.off && up > pacing.refreshes)
			pacing.misses++;
	}
	if (cpu > 0.0 && pacing.timed < PACING_TIMED_FRAMES)
	{
		pacing.cpu_times[pacing.timed] = cpu;
		pacing.gpu_times[pacing.timed] = gpu;
		pacing.timed++;
	}
	if (frames >= pacing.reported + 600)
	{
		double cpu_median, cpu_high, gpu_median, gpu_high;

		pacing_percentiles(pacing.cpu_times, pacing.timed, &cpu_median, &cpu_high);
		pacing_percentiles(pacing.gpu_times, pacing.timed, &gpu_median, &gpu_high);
		pacing.reported = frames;
		platform_log("Metal: frames shown for 1/2/3/4/5+ refreshes at %.1f Hz: %lu/%lu/%lu/%lu/%lu (pacing %s); "
			"frame CPU %.1f ms, GPU %.1f ms (medians; 95th %.1f, %.1f)", 1.0 / period, pacing.shown[1], pacing.shown[2], pacing.shown[3],
			pacing.shown[4], pacing.shown[5],
			pacing.off ? "off" : pacing.refreshes == 1 ? "1 refresh" : pacing.refreshes == 2 ? "2 refreshes" :
			pacing.refreshes == 3 ? "3 refreshes" : "4 refreshes",
			cpu_median, gpu_median, cpu_high, gpu_high);
		memset(pacing.shown, 0, sizeof(pacing.shown));
		pacing.timed = 0;
	}
	if (pacing.off)
		return 0.0;
	lower = pacing_step(pacing.refreshes, -1, period);
	/* a frame over a quarter second (a load, the background) says nothing
	about the rate */
	if (cpu < 0.25 && gpu < 0.25)
	{
		pacing.cost = fmax(pacing.cost, fmax(cpu, gpu));
		if (fmax(cpu, gpu) > PACING_HEADROOM * period * (double)lower)
			pacing.over++;
	}
	pacing.since_change++;
	if (++pacing.window >= PACING_WINDOW)
	{
		unsigned long misses = pacing.misses;

		pacing.misses = 0;
		pacing.window = 0;
		if (misses >= PACING_MISSES && pacing_step(pacing.refreshes, 1, period) != pacing.refreshes)
			pacing_set(pacing_step(pacing.refreshes, 1, period), period, "frames stayed up longer than asked");
	}
	if (lower != pacing.refreshes && pacing.since_change >= PACING_SETTLE && pacing.over <= PACING_OVER)
		pacing_set(lower, period, "frames fit in fewer refreshes");
	else if (pacing.since_change >= PACING_SETTLE)
	{
		/* start a new measure, so the slowest frame is a recent one */
		pacing.cost = 0.0;
		pacing.over = 0;
		pacing.since_change = 0;
	}
	pacing.due = fmax(pacing.due + hold, now + hold);
	*next_frame_due = (uint32_t)fmax(1.0, (pacing.due - now) * 1e6);
	return hold;
}

/* ---------- frames */

/* the game's KickPushBuffer: committing here would split render passes */
static void gpu_metal_flush(void)
{
}

/* the back buffer scaled to width x height by MetalFX when it's smaller and
display.upscaler asks for it, else the back buffer itself */
static id<MTLTexture> upscale(id<MTLTexture> back_buffer, long width, long height)
{
#ifdef HAVE_METALFX
	if (@available(iOS 16.0, tvOS 16.0, visionOS 1.0, *))
	{
		if (!metalfx_wanted || (long)back_buffer.width >= width || (long)back_buffer.height >= height)
			return back_buffer;
		if (!upscaler || upscaler.inputWidth != back_buffer.width || upscaler.inputHeight != back_buffer.height ||
			upscaler.outputWidth != (NSUInteger)width || upscaler.outputHeight != (NSUInteger)height)
		{
			MTLFXSpatialScalerDescriptor *descriptor = [MTLFXSpatialScalerDescriptor new];
			MTLTextureDescriptor *output;

			descriptor.inputWidth = back_buffer.width;
			descriptor.inputHeight = back_buffer.height;
			descriptor.outputWidth = (NSUInteger)width;
			descriptor.outputHeight = (NSUInteger)height;
			descriptor.colorTextureFormat = back_buffer.pixelFormat;
			descriptor.outputTextureFormat = back_buffer.pixelFormat;
			/* the game's colors are display-encoded, not linear */
			descriptor.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;
			if (![MTLFXSpatialScalerDescriptor supportsDevice:device] ||
				!(upscaler = [descriptor newSpatialScalerWithDevice:device]))
			{
				platform_log("Metal: MetalFX's spatial scaler is unavailable; the picture is scaled bilinearly");
				metalfx_wanted = NO;
				return back_buffer;
			}
			output = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:back_buffer.pixelFormat
				width:(NSUInteger)width height:(NSUInteger)height mipmapped:NO];
			output.storageMode = MTLStorageModePrivate;
			output.usage = upscaler.outputTextureUsage | MTLTextureUsageShaderRead;
			upscaled = [device newTextureWithDescriptor:output];
			platform_log("Metal: MetalFX scales %lux%lu up to %ldx%ld", (unsigned long)back_buffer.width,
				(unsigned long)back_buffer.height, width, height);
		}
		upscaler.colorTexture = back_buffer;
		upscaler.outputTexture = upscaled;
		upscaler.inputContentWidth = back_buffer.width;
		upscaler.inputContentHeight = back_buffer.height;
		[upscaler encodeToCommandBuffer:commands];
		return upscaled;
	}
#endif
	(void)width;
	(void)height;
	return back_buffer;
}

/* the back buffer letterboxed into the drawable, as gpu_gl.c's gpu_present,
without its vertical flip: GL's window shows row 0 at the bottom, Metal's
drawable at the top */
/* foveation_resolve's shaders, compiled when first needed: the decoder is
Metal 2.3's, and a GPU without rate maps never needs them */
static NSString *const resolve_source =
	@"#include <metal_stdlib>\n"
	"using namespace metal;\n"
	"struct PresentOut { float4 position [[position]]; float2 coordinates; };\n"
	"struct FoveationSizes { float2 screen; float2 physical; float2 allocated; };\n"
	/* a screen point (the normalized coordinate times the screen size, inside
	it) where the map put it, half a texel inside its physical region: the
	targets are allocated larger, and past the region they are stale */
	"static float2 foveated_at(float2 coordinates, constant rasterization_rate_map_data &map,\n"
	"\tconstant FoveationSizes &f)\n"
	"{\n"
	"\trasterization_rate_map_decoder decoder(map);\n"
	"\tfloat2 screen = clamp(coordinates * f.screen, float2(0.0), f.screen - 1.0 / 256.0);\n"
	"\tfloat2 physical = clamp(decoder.map_screen_to_physical_coordinates(screen), float2(0.5), f.physical - 0.5);\n"
	"\treturn physical / f.allocated;\n"
	"}\n"
	"fragment float4 resolve_color(PresentOut in [[stage_in]], texture2d<float> picture [[texture(0)]],\n"
	"\tsampler linear [[sampler(0)]], constant rasterization_rate_map_data &map [[buffer(0)]],\n"
	"\tconstant FoveationSizes &f [[buffer(1)]])\n"
	"{\n"
	"\treturn picture.sample(linear, foveated_at(in.coordinates, map, f));\n"
	"}\n"
	"fragment float resolve_depth(PresentOut in [[stage_in]], depth2d<float> depth [[texture(0)]],\n"
	"\tsampler nearest [[sampler(0)]], constant rasterization_rate_map_data &map [[buffer(0)]],\n"
	"\tconstant FoveationSizes &f [[buffer(1)]])\n"
	"{\n"
	"\treturn depth.sample(nearest, foveated_at(in.coordinates, map, f));\n"
	"}\n";

struct foveation_sizes_uniform
{
	float screen[2], physical[2], allocated[2];
};

static id<MTLRenderPipelineState> resolve_color_pipeline, resolve_depth_pipeline;
static id<MTLSamplerState> resolve_nearest;
/* foveation_resolve's outputs, by kind, size and eye */
static NSMutableDictionary<NSString *, id<MTLTexture>> *resolve_outputs;

/* a foveated target drawn unfoveated at width x height, at its screen
shape, into a new texture: BGRA8 for a color target, R32Float for a depth
target's depth (as stored, reversed). Encoded in the open command buffer,
after the open pass; nil if the target isn't foveated this frame */
static id<MTLTexture> foveation_resolve(MetalTexture *record, NSUInteger width, NSUInteger height)
{
	MTLSize screen, physical;
	BOOL depth = record && record->description.format == GPU_FORMAT_DEPTH_STENCIL;
	MTLTextureDescriptor *descriptor;
	MTLRenderPassDescriptor *pass;
	id<MTLRenderCommandEncoder> resolve;
	id<MTLTexture> output;
	struct foveation_sizes_uniform sizes;

	if (!record || !record->texture || !foveation_sizes(record, &screen, &physical) || !width || !height)
		return nil;
	if (!resolve_color_pipeline)
	{
		NSError *error = nil;
		id<MTLLibrary> library = [device newLibraryWithSource:resolve_source options:nil error:&error];
		MTLRenderPipelineDescriptor *pipeline = [MTLRenderPipelineDescriptor new];
		MTLSamplerDescriptor *nearest = [MTLSamplerDescriptor new];

		if (library)
		{
			pipeline.vertexFunction = present_vertex;
			pipeline.fragmentFunction = [library newFunctionWithName:@"resolve_color"];
			pipeline.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
			resolve_color_pipeline = [device newRenderPipelineStateWithDescriptor:pipeline error:&error];
			pipeline.fragmentFunction = [library newFunctionWithName:@"resolve_depth"];
			pipeline.colorAttachments[0].pixelFormat = MTLPixelFormatR32Float;
			if (resolve_color_pipeline)
				resolve_depth_pipeline = [device newRenderPipelineStateWithDescriptor:pipeline error:&error];
		}
		if (!resolve_color_pipeline || !resolve_depth_pipeline)
		{
			platform_log("Metal: the foveation resolve's shaders failed: %s", error.description.UTF8String);
			resolve_color_pipeline = resolve_depth_pipeline = nil;
			return nil;
		}
		resolve_nearest = [device newSamplerStateWithDescriptor:nearest];
	}
	/* one output per kind, size and eye, kept: the HEAD-mode menu over the
	film resolves an eye every frame, about 13 MB at quality 0.6. The two
	eyes of one frame get their own; a later frame's resolve is queued after
	the command buffers that read this one's */
	{
		NSString *key = [NSString stringWithFormat:@"%d %lux%lu %u", depth, (unsigned long)width,
			(unsigned long)height, record->description.foveated_eye];

		output = resolve_outputs[key];
		if (!output)
		{
			descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:depth ? MTLPixelFormatR32Float :
				MTLPixelFormatBGRA8Unorm width:width height:height mipmapped:NO];
			descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
			descriptor.storageMode = MTLStorageModePrivate;
			output = [device newTextureWithDescriptor:descriptor];
			/* (the sizes move while the render quality eases: a few at most) */
			if (!resolve_outputs || resolve_outputs.count >= 8)
				resolve_outputs = [NSMutableDictionary dictionary];
			resolve_outputs[key] = output;
		}
	}
	sizes = (struct foveation_sizes_uniform){ { (float)screen.width, (float)screen.height },
		{ (float)physical.width, (float)physical.height },
		{ (float)record->texture.width, (float)record->texture.height } };
	pass_end();
	pass = [MTLRenderPassDescriptor renderPassDescriptor];
	pass.colorAttachments[0].texture = output;
	pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
	pass.colorAttachments[0].storeAction = MTLStoreActionStore;
	resolve = [command_buffer() renderCommandEncoderWithDescriptor:pass];
	if (metal_debug)
		resolve.label = [NSString stringWithFormat:@"foveation resolve of %lux%lu", (unsigned long)screen.width,
			(unsigned long)screen.height];
	[resolve setRenderPipelineState:depth ? resolve_depth_pipeline : resolve_color_pipeline];
	[resolve setViewport:(MTLViewport){ 0.0, 0.0, (double)width, (double)height, 0.0, 1.0 }];
	[resolve setFragmentTexture:record->texture atIndex:0];
	[resolve setFragmentSamplerState:depth ? resolve_nearest : present_sampler atIndex:0];
	[resolve setFragmentBuffer:foveation_parameters(record) offset:0 atIndex:0];
	[resolve setFragmentBytes:&sizes length:sizeof(sizes) atIndex:1];
	[resolve drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
	[resolve endEncoding];
	use_texture(record);
	return output;
}

/* a foveated target resolved at its screen shape inside its allocated size,
for a picture drawn whole somewhere else (the film's screen, the UI's
quad); an unfoveated one as it is */
static id<MTLTexture> foveation_picture(MetalTexture *record)
{
	MTLSize screen, physical;
	NSUInteger width, height;
	id<MTLTexture> resolved;

	if (!foveation_sizes(record, &screen, &physical))
		return record->texture;
	width = record->texture.width;
	height = width * screen.height / screen.width;
	if (height > record->texture.height)
	{
		height = record->texture.height;
		width = height * screen.width / screen.height;
	}
	resolved = foveation_resolve(record, width, height);
	return resolved ? resolved : record->texture;
}

#include "metal_overlay.inc"

static uint32_t gpu_metal_present(gpu_texture back_buffer)
{
	@autoreleasepool
	{
		MetalTexture *record = texture_record(back_buffer);
		id<CAMetalDrawable> drawable;
		CFTimeInterval waited;
		uint32_t next_frame_due = 0;

		command_buffer();
		pass_finish(YES);
#if TARGET_OS_VISION
		/* head-tracked stereo without eyes this frame (the main menu, a
		load): the picture goes on the UI's quad, level in front of the head
		(host_stereo_present_ui), not the theater screen */
		if (theater_wanted && record && record->texture && host_theater_active() && host_stereo_ui_ready())
		{
			use_texture(record);
			commit(YES);
			host_stereo_present_ui(queue, record->texture, nil, stereo_cut_brightness(HOST_STEREO_VIEW_UI, 0, 0));
			frames++;
			pacing.work_started = CACurrentMediaTime();
			return 0;
		}
		/* theater mode: the picture, at the screen's size (sharp enough for its
		angle, host_theater_picture_size), goes on the screen
		in the immersive space, whose frames pace the game; the window isn't
		drawn meanwhile */
		if (theater_wanted && record && record->texture && host_theater_active())
		{
			int screen_width, screen_height;
			long window_width, window_height, width, height;
			id<MTLTexture> picture;

			host_theater_picture_size(&screen_width, &screen_height);
			window_width = screen_width;
			window_height = screen_height;
			width = window_width;
			height = window_width * (long)record->description.height / (long)record->description.width;

			if (height > window_height)
			{
				height = window_height;
				width = window_height * (long)record->description.width / (long)record->description.height;
			}
			picture = upscale(foveation_picture(record), width, height);
			use_texture(record);
			commit(YES);
			host_theater_present(queue, picture);
			frames++;
			pacing.work_started = CACurrentMediaTime();
			return 0;
		}
#endif
		waited = CACurrentMediaTime();
		drawable = [layer nextDrawable];
		pacing_waited += CACurrentMediaTime() - waited;
		/* nil in the background: the frame still commits, so its slot is
		freed */
		if (drawable && record && record->texture)
		{
			MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
			long window_width = (long)drawable.texture.width, window_height = (long)drawable.texture.height;
			long width = window_width, height = window_width * (long)record->description.height / (long)record->description.width;
			id<MTLRenderCommandEncoder> present;
			id<MTLTexture> picture;

			if (height > window_height)
			{
				height = window_height;
				width = window_height * (long)record->description.width / (long)record->description.height;
			}
			/* (debug.rate_map_test: the back buffer resolved first) */
			picture = upscale(foveation_picture(record), width, height);
			pass.colorAttachments[0].texture = drawable.texture;
			pass.colorAttachments[0].loadAction = MTLLoadActionClear;
			pass.colorAttachments[0].storeAction = MTLStoreActionStore;
			pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
			present = [commands renderCommandEncoderWithDescriptor:pass];
			if (metal_debug)
				present.label = @"present";
			if (pass_log)
				[pass_log appendFormat:@"\n  present: texture %u %lux%lu into %ldx%ld of drawable %ldx%ld %s/%s", back_buffer,
					(unsigned long)picture.width, (unsigned long)picture.height, width, height, window_width, window_height,
					load_name(pass.colorAttachments[0].loadAction), store_name(pass.colorAttachments[0].storeAction)];
			[present setRenderPipelineState:present_pipeline];
			[present setViewport:(MTLViewport){ (double)((window_width - width) / 2), (double)((window_height - height) / 2),
				(double)width, (double)height, 0.0, 1.0 }];
			[present setFragmentTexture:picture atIndex:0];
			[present setFragmentSamplerState:present_sampler atIndex:0];
			[present drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
			[present endEncoding];
			use_texture(record);
			{
				CFTimeInterval hold = pacing_schedule(drawable, &next_frame_due);

#if TARGET_OS_SIMULATOR
				(void)hold;
				[commands presentDrawable:drawable];
#else
				if (hold > 0.0)
					[commands presentDrawable:drawable afterMinimumDuration:hold];
				else
					[commands presentDrawable:drawable];
#endif
			}
		}
		drawable = nil;
		commit(YES);
		if (metal_debug && (compile_stats.count[COMPILE_LIBRARY] || compile_stats.count[COMPILE_SPECIALIZE] ||
			compile_stats.count[COMPILE_PIPELINE]))
			platform_log("Metal: frame %lu compiled %lu libraries (%.1f ms, longest %.1f), specialized %lu vertex "
				"functions (%.1f ms, longest %.1f), made %lu pipelines (%.1f ms, longest %.1f)", frames,
				compile_stats.count[COMPILE_LIBRARY], compile_stats.seconds[COMPILE_LIBRARY] * 1e3,
				compile_stats.longest[COMPILE_LIBRARY] * 1e3, compile_stats.count[COMPILE_SPECIALIZE],
				compile_stats.seconds[COMPILE_SPECIALIZE] * 1e3, compile_stats.longest[COMPILE_SPECIALIZE] * 1e3,
				compile_stats.count[COMPILE_PIPELINE], compile_stats.seconds[COMPILE_PIPELINE] * 1e3,
				compile_stats.longest[COMPILE_PIPELINE] * 1e3);
		memset(compile_stats.count, 0, sizeof(compile_stats.count));
		memset(compile_stats.seconds, 0, sizeof(compile_stats.seconds));
		memset(compile_stats.longest, 0, sizeof(compile_stats.longest));
		frames++;
		if (pass_log)
		{
			platform_log("Metal: frame %lu runs, in order:%s", frames - 1, pass_log.UTF8String);
			pass_log = nil;
		}
		if (metal_debug && frames % 600 == 300)
			pass_log = [NSMutableString string];
		if (frames % 600 == 0 && renames)
		{
			platform_log("Metal: %lu renames in the last 600 frames", renames);
			renames = 0;
		}
		if (frames % 600 == 0 && state_cache.draws)
		{
			platform_log("Metal: %.1f draws a frame make %.1f encoder calls each and skip %.1f (state cache %s)",
				(double)state_cache.draws / 600.0, (double)state_cache.issued / state_cache.draws,
				(double)state_cache.skipped / state_cache.draws, state_cache.enabled ? "on" : "off");
			if (metal_debug)
				platform_log("Metal: %.1f pipeline changes a frame, %.1f of them with the same shaders",
					(double)pipeline_changes.changes / 600.0, (double)pipeline_changes.same_shaders / 600.0);
			pipeline_changes.changes = pipeline_changes.same_shaders = 0;
			if (metal_debug)
				platform_log("Metal: so far %lu shader libraries (%.0f ms), %lu vertex specializations (%.0f ms), "
					"%lu pipelines (%.0f ms) made while drawing; outside a map's list: %lu libraries, %lu "
					"specializations, %lu pipelines", compile_stats.total_count[COMPILE_LIBRARY],
					compile_stats.total_seconds[COMPILE_LIBRARY] * 1e3, compile_stats.total_count[COMPILE_SPECIALIZE],
					compile_stats.total_seconds[COMPILE_SPECIALIZE] * 1e3, compile_stats.total_count[COMPILE_PIPELINE],
					compile_stats.total_seconds[COMPILE_PIPELINE] * 1e3, compile_stats.during_play[COMPILE_LIBRARY],
					compile_stats.during_play[COMPILE_SPECIALIZE], compile_stats.during_play[COMPILE_PIPELINE]);
			metal_state_take_counts(&state_cache);
		}
		if (frames % 600 == 0)
		{
			platform_log("Metal: a frame has %.1f render passes, which load %.0f MB and store %.0f MB",
				(double)pass_traffic.passes / 600.0, pass_traffic.loaded / 600.0 / 1e6, pass_traffic.stored / 600.0 / 1e6);
			memset(&pass_traffic, 0, sizeof(pass_traffic));
		}
		pacing.work_started = CACurrentMediaTime();
		return next_frame_due;
	}
}

/* a stereo frame: in head-tracked stereo (display.stereo = "head") each eye's
picture and depth, and the HUD, go to the Compositor's frame that
host_stereo_frame opened at the frame's begin (host_stereo.m); in stereo on
the screen (display.stereo = "screen") each eye's picture, with the HUD over
it, goes on the theater screen for its view (host_theater_present_eyes);
without a frame (the space closed, another mode) eye 0 goes through the mono
path.

A cutscene (present->cinematic, the 3D film) is on the screen in any mode,
and the script fade (present->fade) tints the space around the screen. A cut
between any two of the full view, the screen and HEAD mode's UI quad (a
cutscene starting or ending in HEAD mode, a load starting or ending, a menu
over the film) goes to black and fades back in over HOST_STEREO_CUT_SECONDS,
unless a script fade already covers it (present->cut_covered,
stereo.c's halo_stereo_cut_covered). */
#if TARGET_OS_VISION
/* what the last stereo frame showed (host_stereo_hud.h's cut, which also
covers the UI's quad), and the frames of the cut's fade still to come */
static struct host_stereo_cut stereo_cut = HOST_STEREO_CUT_INITIAL;

static const char *stereo_view_name(int view)
{
	return view == HOST_STEREO_VIEW_SCREEN ? "the screen" : view == HOST_STEREO_VIEW_UI ? "the UI's quad" : "the full view";
}

/* the brightness of a frame showing view (enum host_stereo_view): coming up
from black over HOST_STEREO_CUT_SECONDS after a cut no script fade covers;
black while requested (stereo's cut over a seat's exit glide) */
static float stereo_cut_brightness(int view, int covered, int requested)
{
	int last = stereo_cut.shown, switched;
	/* a frame stays up for the repeat count plus one refreshes of the
	Vision Pro's 90 Hz, so the fade lasts the same time at 45 */
	float frame_seconds = (float)(host_theater_frame_repeat() + 1) / 90.0f;
	float brightness = host_stereo_cut_brightness(&stereo_cut, view, covered, requested, frame_seconds,
		&switched);

	if (switched)
		platform_log("stereo: from %s to %s through black", stereo_view_name(last), stereo_view_name(view));
	return brightness;
}

/* Stereo's frame times, for the benchmarks (avp-benchmark.sh reads the
"frames shown for" line, as pacing_schedule's): the Compositor paces stereo,
so its frames are measured by their presentation times, and a frame's CPU
time is the time since the last present less the time spent waiting for the
Compositor's frame. The refresh period is the shortest interval between
presentations over the report's frames divided by the repeat count plus one
(an on-time frame's), or 90 Hz's when that falls outside 60 to 120 Hz.
Every 600 stereo frames.

Both times lag by a frame, deliberately: the GPU time is what completed
since the last count (the game's command buffers and the presenters',
gpu_metal_count_gpu_time), and completion handlers land after the next frame
has begun, as in pacing_schedule; the CPU time leaves out this frame's
presenter encode, which comes after the count, and includes the last one's.
Over a report each frame is counted once, so the medians are right. */
static struct
{
	CFTimeInterval last_presentation;
	CFTimeInterval shortest;
	uint64_t gpu_counted;
	CFTimeInterval intervals[PACING_TIMED_FRAMES];
	double cpu_times[PACING_TIMED_FRAMES], gpu_times[PACING_TIMED_FRAMES];
	unsigned long timed, frames;
} stereo_pacing;

static void stereo_pacing_count(void)
{
	CFTimeInterval now = CACurrentMediaTime();
	CFTimeInterval waited = host_theater_take_waited();
	CFTimeInterval presentation = host_theater_presentation_time();
	CFTimeInterval interval = 0.0;
	uint64_t gpu_total = atomic_load(&pacing_gpu_nanoseconds);
	double cpu = pacing.work_started > 0.0 ? now - pacing.work_started - waited : 0.0;
	double gpu = (double)(gpu_total - stereo_pacing.gpu_counted) / 1e9;

	stereo_pacing.gpu_counted = gpu_total;
	if (presentation > 0.0 && stereo_pacing.last_presentation > 0.0)
		interval = presentation - stereo_pacing.last_presentation;
	if (presentation > 0.0)
		stereo_pacing.last_presentation = presentation;
	/* a quarter second or more is a load or the background, not a frame */
	if (interval > 0.0 && interval < 0.25 && cpu > 0.0 && stereo_pacing.timed < PACING_TIMED_FRAMES)
	{
		if (stereo_pacing.shortest <= 0.0 || interval < stereo_pacing.shortest)
			stereo_pacing.shortest = interval;
		stereo_pacing.intervals[stereo_pacing.timed] = interval;
		stereo_pacing.cpu_times[stereo_pacing.timed] = cpu;
		stereo_pacing.gpu_times[stereo_pacing.timed] = gpu;
		stereo_pacing.timed++;
	}
	if (++stereo_pacing.frames % 600 == 0 && stereo_pacing.timed)
	{
		int repeat = host_theater_frame_repeat();
		CFTimeInterval period = stereo_pacing.shortest / (double)(repeat + 1);
		unsigned long shown[6] = { 0 };
		double cpu_median, cpu_high, gpu_median, gpu_high;
		unsigned long index;

		if (period < 1.0 / 120.0 || period > 1.0 / 60.0)
			period = 1.0 / 90.0;
		for (index = 0; index < stereo_pacing.timed; index++)
		{
			long up = lround(stereo_pacing.intervals[index] / period);

			shown[up < 1 ? 1 : up > 5 ? 5 : up]++;
		}
		pacing_percentiles(stereo_pacing.cpu_times, stereo_pacing.timed, &cpu_median, &cpu_high);
		pacing_percentiles(stereo_pacing.gpu_times, stereo_pacing.timed, &gpu_median, &gpu_high);
		platform_log("stereo: frames shown for 1/2/3/4/5+ refreshes at %.1f Hz: %lu/%lu/%lu/%lu/%lu (repeat count %d); "
			"frame CPU %.1f ms, GPU %.1f ms (medians; 95th %.1f, %.1f)", 1.0 / period, shown[1], shown[2], shown[3],
			shown[4], shown[5], repeat, cpu_median, gpu_median, cpu_high, gpu_high);
		stereo_pacing.timed = 0;
		stereo_pacing.shortest = 0.0;
	}
}
#endif

static uint32_t gpu_metal_present_stereo(const struct gpu_stereo_present *present)
{
#if TARGET_OS_VISION
	@autoreleasepool
	{
		MetalTexture *color[2] = { texture_record(present->eye_color[0]), texture_record(present->eye_color[1]) };
		MetalTexture *depth[2] = { texture_record(present->eye_depth[0]), texture_record(present->eye_depth[1]) };
		MetalTexture *hud = present->hud ? texture_record(present->hud) : nil;
		MetalTexture *zoom = present->zoom ? texture_record(present->zoom) : nil;
		BOOL on_screen = present->mode == HALO_STEREO_SCREEN || present->cinematic;

		if (theater_wanted && !on_screen && present->mode == HALO_STEREO_HEAD && color[0] && color[0]->texture && color[1] &&
			color[1]->texture && depth[0] && depth[0]->texture && depth[1] && depth[1]->texture && host_stereo_ready())
		{
			int eye;

			command_buffer();
			/* the eyes' depth is read after the frame, so it's stored */
			pass_end();
			for (eye = 0; eye < 2; eye++)
			{
				use_texture(color[eye]);
				use_texture(depth[eye]);
			}
			/* the HUD layer, the crosshairs' layer and the HUD groups'
			targets (host_stereo_hud.h) */
			MetalTexture *hud_records[HOST_STEREO_HUD_LAYER_COUNT] = { hud };
			__unsafe_unretained id<MTLTexture> hud_layers[HOST_STEREO_HUD_LAYER_COUNT] = { nil };
			int layer;

			hud_records[HOST_STEREO_HUD_LAYER_RETICLE] = present->reticle_layer ?
				texture_record(present->reticle_layer) : nil;
			for (layer = 0; layer < HALO_HUD_GROUP_COUNT; layer++)
				hud_records[HOST_STEREO_HUD_LAYER_GROUP + layer] = present->hud_group[layer] ?
					texture_record(present->hud_group[layer]) : nil;
			hud_records[HOST_STEREO_HUD_LAYER_UI] = present->ui ? texture_record(present->ui) : nil;
			for (layer = 0; layer < HOST_STEREO_HUD_LAYER_COUNT; layer++)
				if (hud_records[layer] && hud_records[layer]->texture)
				{
					use_texture(hud_records[layer]);
					hud_layers[layer] = hud_records[layer]->texture;
				}
			if (zoom && zoom->texture)
				use_texture(zoom);
			commit(YES);
			stereo_pacing_count();
			host_stereo_present(queue, color[0]->texture, color[1]->texture, depth[0]->texture, depth[1]->texture,
				hud_layers, present->hud_group_extent, present->hud_aspect, present->hud_ui, present->reticle,
				present->hud_tangents, zoom ? zoom->texture : nil, present->zoom_tangents, present->near_meters,
				present->far_meters, stereo_cut_brightness(HOST_STEREO_VIEW_FULL, present->cut_covered ||
				present->expanding, present->cut_requested),
				present->vignette, present->ui_dim, present->expanding, present->expansion, present->expansion_bars,
				present->fade, present->cutscene, present->cutscene_forward, present->cutscene_up,
				present->cutscene_tangents, present->cutscene_dim);
			frames++;
			pacing.work_started = CACurrentMediaTime();
			return 0;
		}
		/* HEAD mode with a menu over the film (the main menu's scripted
		scene, the pause menu in a cutscene): the left eye's picture with the
		menu on the UI's quad, as a frame without eyes, not on the screen */
		if (theater_wanted && on_screen && present->hud_ui && color[0] &&
			color[0]->texture && hud && hud->texture && host_stereo_ui_ready())
		{
			id<MTLTexture> picture;

			command_buffer();
			pass_end();
			/* (a foveated eye at its screen shape) */
			picture = foveation_picture(color[0]);
			use_texture(color[0]);
			use_texture(hud);
			commit(YES);
			host_stereo_present_ui(queue, picture, hud->texture,
				stereo_cut_brightness(HOST_STEREO_VIEW_UI, present->cut_covered, 0));
			frames++;
			pacing.work_started = CACurrentMediaTime();
			return 0;
		}
		if (theater_wanted && on_screen && color[0] && color[0]->texture && color[1] &&
			color[1]->texture && host_stereo_ready())
		{
			id<MTLTexture> left, right;

			command_buffer();
			pass_end();
			/* (foveated eyes at their screen shape) */
			left = foveation_picture(color[0]);
			right = foveation_picture(color[1]);
			use_texture(color[0]);
			use_texture(color[1]);
			if (hud && hud->texture)
				use_texture(hud);
			commit(YES);
			stereo_pacing_count();
			host_theater_present_eyes(queue, left, right, hud ? hud->texture : nil,
				present->fade, stereo_cut_brightness(HOST_STEREO_VIEW_SCREEN, present->cut_covered, 0));
			frames++;
			pacing.work_started = CACurrentMediaTime();
			return 0;
		}
		stereo_cut_brightness(HOST_STEREO_VIEW_NONE, 0, 0);
	}
#endif
	/* (the window: eye 0, or a zoomed frame's picture in its place) */
	return gpu_metal_present(present->zoom ? present->zoom : present->eye_color[0]);
}

static uint32_t gpu_metal_call_count_take(void)
{
	return 0;
}

/* ---------- initialization */

static void gpu_metal_initialize(uint32_t flags, struct gpu_capabilities *capabilities)
{
	@autoreleasepool
	{
		NSError *error = nil;
		id<MTLLibrary> utilities;
		MTLRenderPipelineDescriptor *present_descriptor = [MTLRenderPipelineDescriptor new];
		MTLSamplerDescriptor *linear = [MTLSamplerDescriptor new];

		memset(capabilities, 0, sizeof(*capabilities));
		metal_debug = (flags & GPU_INITIALIZE_DEBUG) != 0;
		visibility_wait = (flags & GPU_INITIALIZE_FIXED_TIMESTEP) != 0;
		metalfx_wanted = (flags & GPU_INITIALIZE_METALFX) != 0;
		metal_state_initialize(&state_cache, !(flags & GPU_INITIALIZE_NO_STATE_CACHE));
		specialize_off = (flags & GPU_INITIALIZE_NO_SPECIALIZE) != 0;
		built_during_play = [NSMutableData data];
		if (specialize_off)
			platform_log("Metal: vertex shaders are not specialized (debug.metal_specialize)");
#ifndef HAVE_METALFX
		if (metalfx_wanted)
			platform_log("Metal: this build has no MetalFX; the picture is scaled bilinearly");
		metalfx_wanted = NO;
#endif
		pacing_start(flags);
		layer = (__bridge CAMetalLayer *)host_sdl_metal_layer();
		device = MTLCreateSystemDefaultDevice();
		if (!layer || !device)
			host_fatal("Metal is unavailable (layer %p, device %p)", (__bridge void *)layer, (__bridge void *)device);
		queue = [device newCommandQueue];
		/* (after the device: the archive is the device's, and named by its GPU) */
		if (!(flags & GPU_INITIALIZE_NO_PIPELINE_ARCHIVE))
			archive_open();
#if TARGET_OS_VISION
		if (flags & GPU_INITIALIZE_IMMERSIVE)
		{
			theater_wanted = YES;
			host_theater_load_settings();
			host_theater_open();
		}
#endif
		/* SDL's view sets the layer's scale and drawableSize (from layoutSubviews) */
		layer.device = device;
		layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
		layer.framebufferOnly = YES;
		textures = [MetalTable new];
		buffers = [MetalTable new];
		shaders = [MetalTable new];
		frame_slots = dispatch_semaphore_create(FRAMES);
		completion_handlers = dispatch_group_create();
		clear_pipelines = [NSMutableDictionary dictionary];
		clear_depth_states = [NSMutableDictionary dictionary];
		utilities = [device newLibraryWithSource:utility_source options:nil error:&error];
		if (!utilities)
			host_fatal("Metal: cannot compile the backend's own shaders: %s", error.description.UTF8String);
		clear_vertex = [utilities newFunctionWithName:@"clear_vertex"];
		clear_fragment = [utilities newFunctionWithName:@"clear_fragment"];
		present_vertex = [utilities newFunctionWithName:@"present_vertex"];
		present_fragment = [utilities newFunctionWithName:@"present_fragment"];
		present_descriptor.vertexFunction = present_vertex;
		present_descriptor.fragmentFunction = present_fragment;
		present_descriptor.colorAttachments[0].pixelFormat = layer.pixelFormat;
		present_pipeline = [device newRenderPipelineStateWithDescriptor:present_descriptor error:&error];
		if (!present_pipeline)
			host_fatal("Metal: cannot create the present pipeline: %s", error.description.UTF8String);
		linear.minFilter = MTLSamplerMinMagFilterLinear;
		linear.magFilter = MTLSamplerMinMagFilterLinear;
		present_sampler = [device newSamplerStateWithDescriptor:linear];
		pipelines = [NSMutableDictionary dictionary];
		depth_states = [NSMutableDictionary dictionary];
		samplers = [NSMutableDictionary dictionary];
		scratch_colors = [NSMutableDictionary dictionary];
		test_maps = [NSMutableDictionary dictionary];
		{
			char value[16];

			host_config_string("debug.rate_map_test", "false", value, sizeof(value));
			rate_map_test = !strcmp(value, "true");
		}
		compile_options = [MTLCompileOptions new];
		/* vertex shaders: no fast math. The GLSL is highp, and the compiler
		mustn't reorder its arithmetic; invariance keeps a position computed in
		two passes identical, as GLSL's invariant gl_Position does */
		if (@available(macOS 15.0, iOS 18.0, tvOS 18.0, visionOS 2.0, *))
			compile_options.mathMode = MTLMathModeSafe;
		else
			compile_options.fastMathEnabled = NO;
		compile_options.preserveInvariance = YES;
		/* pixel shaders: relaxed math, which may reassociate, contract into
		fused multiply-adds and divide by reciprocals, but keeps infinities and
		NaNs. The scene's pixel shaders are bound by ALU work, and under
		exact IEEE arithmetic they took about twice the GPU time of the same
		shaders under ANGLE, which compiles with fast math. No pixel shader
		writes depth, so positions and depth tests are untouched. Before iOS
		18 the only other choice is fast math, which also drops infinities and
		NaNs, so there pixel shaders keep the vertex shaders' options. */
		pixel_compile_options = [compile_options copy];
		if (@available(macOS 15.0, iOS 18.0, tvOS 18.0, visionOS 2.0, *))
			pixel_compile_options.mathMode = MTLMathModeRelaxed;
		{
			static const uint8_t black[4] = { 0, 0, 0, 0xff };
			MTLTextureType types[4] = { MTLTextureType2D, MTLTextureType2D, MTLTextureType3D, MTLTextureTypeCube };
			int type, face;

			for (type = 0; type < 4; type++)
			{
				MTLTextureDescriptor *descriptor = [MTLTextureDescriptor new];

				descriptor.textureType = types[type];
				descriptor.pixelFormat = MTLPixelFormatBGRA8Unorm;
				descriptor.width = descriptor.height = descriptor.depth = 1;
				descriptor.storageMode = MTLStorageModeShared;
				empty_textures[type] = [device newTextureWithDescriptor:descriptor];
				for (face = 0; face < (types[type] == MTLTextureTypeCube ? 6 : 1); face++)
					[empty_textures[type] replaceRegion:MTLRegionMake3D(0, 0, 0, 1, 1, 1) mipmapLevel:0 slice:face
						withBytes:black bytesPerRow:4 bytesPerImage:4];
			}
		}
		empty_sampler = [device newSamplerStateWithDescriptor:[MTLSamplerDescriptor new]];
		empty_buffer = [device newBufferWithLength:64 options:MTLResourceStorageModeShared];
		visibility_buffer = [device newBufferWithLength:VISIBILITY_ENTRIES * sizeof(uint64_t)
			options:MTLResourceStorageModeShared];
		visibility.pending = [NSMutableData data];
		/* the class shows whether Metal's API validation wraps the device
		(MTLDebugDevice; tools/mac_run.py run --metal-validation) */
		platform_log("Metal on %s (%s)", device.name.UTF8String, NSStringFromClass([device class]).UTF8String);
		if (flags & GPU_INITIALIZE_COMPRESSED_TEXTURES)
		{
			if (@available(macOS 13.0, iOS 16.4, tvOS 16.4, visionOS 1.0, *))
				compressed_textures = device.supportsBCTextureCompression;
			platform_log("Metal: compressed textures %s", compressed_textures ? "upload as BC1-3" :
				"are decoded (this GPU has no BC formats)");
		}

		/* Native Metal capabilities. Vertex colors are swizzled by the frontend;
		visibility uses exact counts rather than the mobile boolean path. */
		capabilities->vertex_bgra = 0;
		capabilities->base_vertex = 0;
		capabilities->triangle_fans = 1;
		capabilities->line_loops = 1;
		capabilities->sampler_lod_bias = 0;
		capabilities->occlusion_mode = GPU_OCCLUSION_EXACT;
		capabilities->s3tc = compressed_textures ? 1 : 0;
		/* border colors stay inside the backend: the front end doesn't read
		border_clamp, and a GL run and a Metal run report the same line */
		border_colors = [device supportsFamily:MTLGPUFamilyApple7] || [device supportsFamily:MTLGPUFamilyMac2];
		capabilities->border_clamp = 0;
		capabilities->max_texture_size = 16384;
		capabilities->shader_language = GPU_SHADER_LANGUAGE_MSL;
		capabilities->shader_es = 0;
		capabilities->clip_y_flip = 0;
		capabilities->clip_z_remap = 0;
		GPU_CAPABILITIES_LOG(platform_log, capabilities);
	}
}

#define GPU_METAL_FUNCTION(type, name, parameters, arguments) .name = gpu_metal_##name,
#define GPU_METAL_PROCEDURE(name, parameters, arguments) .name = gpu_metal_##name,
const struct gpu_backend gpu_backend_metal = { GPU_FUNCTIONS(GPU_METAL_FUNCTION, GPU_METAL_PROCEDURE) };
