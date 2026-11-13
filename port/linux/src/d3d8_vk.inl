/*
D3D8_VK.INL

The Xbox Direct3D 8 device drawn with Vulkan, in the Android app
(display.renderer = "vulkan"; port/android/VULKAN.md). Part of d3d8_gl.c,
included at its end as the bgfx backend is (d3d8_bgfx.inl), and sharing the
device's state: what the game sets through the XDK's inline functions and the
entry points above is read at each draw. OpenGL ES is never initialized while
it runs (gl_ready stays FALSE), so the OpenGL path's entry points do nothing;
when Vulkan cannot start, the game draws with OpenGL ES as before
(platform_video_reopen_for_gl).

The renderer is thelinkin3000's, from PR #152 (the guest build of the app,
where the game ran as a 32-bit image): this file is the Vulkan half of his
d3d8_vk.c, which built the frame as a command stream for the host's backend
(port/android/host/host_vk*.c, taken as it was). The stream stays, written
over a frame and handed over at its end, or sooner when it fills; here the
hand-over is a call into the same library. The game's textures are kept as
his were, images the backend makes, decoded by xbox_textures.c's helpers as
the bgfx backend decodes them; the high-res HUD, the text's atlas and the
menus' art are images of the same kind (xgpu_vulkan_texture_create).

Not here yet: the Guide and the other overlays (ui_overlay.c draws with
OpenGL or bgfx), the anti-aliasing passes and the capture of screenshots and
clips, which are the OpenGL renderer's; and the shaders are the generators'
of PR #152 (nv2a_vsh_vk.c, nv2a_psh_vk.c), without what nv2a_vsh.c and
nv2a_psh.c gained since (per-pixel lighting, the Light effects).
*/

#include <SDL3/SDL.h>
#include <unistd.h>

#include "host_vk_api.h"
#include "hud_hires.h"
#include "vk_commands.h"
#include "vk_shaders.h"

SDL_Window *platform_sdl_window(void);
#include "menu_files.h"
#include "text_hires.h"

BOOL xgpu_vulkan;

/* what this backend keeps beside the device's state */
static struct
{
	BOOL ready;
	/* the frame number, for the textures' idle counts */
	unsigned long frame;
	/* the game's window (host_vk_window) */
	void *window;
	/* the start note (vk_initialize): its path while this start's is written, cleared once frames are drawn */
	char note_path[512];
} vk;
/* ---------- the command stream (vk_commands.h)

Commands are written here over a frame and handed to the host half
(port/android/host/host_vk*.c) at its end, or sooner if the stream fills. The
host reads them during the call and nothing later. */

#define STREAM_SIZE (4 * 1024 * 1024)

static uint32_t vk_stream[STREAM_SIZE / 4];
static unsigned long stream_used;

static void stream_flush(void)
{
	if (stream_used)
		host_vk_submit(vk_stream, (uint32_t)stream_used);
	stream_used = 0;
}

/* room for a command of size bytes, its header filled in (vk_device.h) */
void *vk_stream_command(uint32_t type, unsigned long size)
{
	struct vk_command_header *header;

	size = (size + 3) & ~3UL;
	if (stream_used + size > STREAM_SIZE)
		stream_flush();
	header = (struct vk_command_header *)((unsigned char *)vk_stream + stream_used);
	header->type = type;
	header->size = (uint32_t)size;
	stream_used += size;
	return header;
}

#define stream_command vk_stream_command

/* The data a draw reads is copied here, into the stream, when the draw is made, and
nowhere later: the game rewrites its buffers between draws of a frame. Returns the
frame's running number of the data (from 1; reset at Present), which commands name,
with an offset, to read it. Data that does not fit in what is left of the stream is
split into parts, across hand-overs: the host places them contiguously. */
static uint32_t data_id;

/* what is left in the stream for the payload of a data record, a multiple of 4 */
static unsigned long data_room(void)
{
	unsigned long left = STREAM_SIZE - stream_used;

	return left > sizeof(struct vk_command_data) ? (left - sizeof(struct vk_command_data)) & ~3UL : 0;
}

uint32_t vk_data_put(const void *bytes, unsigned long size)
{
	const unsigned char *at = bytes;
	unsigned long done = 0;
	uint32_t id = ++data_id;

	do
	{
		unsigned long part = size - done, room = data_room();
		struct vk_command_data *command;

		/* a part of a few bytes is not worth a record at the end of the stream */
		if (room < 64 && room < part)
		{
			stream_flush();
			room = data_room();
		}
		if (part > room)
			part = room;
		command = stream_command(VK_COMMAND_DATA, sizeof(*command) + part);
		command->id = id;
		command->part_offset = (uint32_t)done;
		command->total_size = (uint32_t)size;
		command->part_size = (uint32_t)part;
		if (part)
			memcpy(command->payload, at + done, part);
		if (part & 3)
			memset((unsigned char *)command->payload + part, 0, 4 - (part & 3));
		done += part;
	} while (done < size);
	return id;
}

/* the pixels per unit of the bound targets: what the screen's targets are drawn at,
as render_target_get in d3d8_gl.c works it out (a target the size of the screen gets
the screen's scale), and what clears are scaled by (target_pixel there) */
static float vk_target_scale[2] = { 1.0f, 1.0f };

static void vk_target_scale_of(unsigned long width, unsigned long height, float scale[2])
{
	scale[0] = scale[1] = 1.0f;
	if (width == (unsigned long)halo_screen_width() && height == SCREEN_HEIGHT)
	{
		scale[0] = screen_scale[0];
		scale[1] = screen_scale[1];
	}
}

static long vk_target_pixel(float coordinate, int axis)
{
	return (long)floorf(coordinate * vk_target_scale[axis] + 0.5f);
}

/* a surface as the host knows it; NONE for none. A surface that is not a depth format
is not a depth buffer (d3d8_gl.c leaves it unbound) */
static void surface_describe(const D3DSurface *surface, BOOL depth_only, struct vk_surface *out, float scale_out[2])
{
	unsigned long width, height;
	float scale[2];
	BOOL depth;

	memset(out, 0, sizeof(*out));
	scale_out[0] = scale_out[1] = 1.0f;
	if (!surface || !surface->Data)
		return;
	surface_dimensions(surface, &width, &height, &depth);
	if (depth_only && !depth)
		return;
	vk_target_scale_of(width, height, scale);
	out->data = surface->Data;
	out->width = (uint32_t)width;
	out->height = (uint32_t)height;
	out->pixel_width = (uint32_t)(width * scale[0] + 0.5f);
	out->pixel_height = (uint32_t)(height * scale[1] + 0.5f);
	out->kind = depth ? VK_SURFACE_DEPTH : VK_SURFACE_COLOR;
	scale_out[0] = scale[0];
	scale_out[1] = scale[1];
}

static void rendered_note(const D3DSurface *surface);

/* the targets the host has bound, as last told it; cleared when a frame starts there anew */
static struct vk_command_targets targets_told;
static BOOL targets_known;

/* tells the host the current targets, if they are not what it has, and sets the scale
viewports and clears are in; FALSE if there is nothing to draw into (bind_targets in
d3d8_gl.c) */
static BOOL targets_bind(BOOL *has_depth)
{
	struct vk_command_targets targets;
	float color_scale[2], depth_scale[2];

	memset(&targets, 0, sizeof(targets));
	surface_describe(device.render_target, FALSE, &targets.color, color_scale);
	surface_describe(device.depth_stencil, TRUE, &targets.depth, depth_scale);
	if (targets.color.kind == VK_SURFACE_NONE && targets.depth.kind == VK_SURFACE_NONE)
		return FALSE;
	if (targets.color.kind != VK_SURFACE_NONE)
	{
		vk_target_scale[0] = color_scale[0];
		vk_target_scale[1] = color_scale[1];
	}
	else
	{
		vk_target_scale[0] = depth_scale[0];
		vk_target_scale[1] = depth_scale[1];
	}
	if (!targets_known || memcmp(&targets.color, &targets_told.color, sizeof(targets.color)) ||
		memcmp(&targets.depth, &targets_told.depth, sizeof(targets.depth)))
	{
		struct vk_command_targets *command = stream_command(VK_COMMAND_TARGETS, sizeof(*command));

		command->color = targets.color;
		command->depth = targets.depth;
		targets_told = targets;
		targets_known = TRUE;
		if (targets.color.kind != VK_SURFACE_NONE)
			rendered_note(device.render_target);
	}
	*has_depth = targets.depth.kind != VK_SURFACE_NONE;
	return TRUE;
}

/* the colour surfaces the game has bound as targets, by address, and when each was last bound: a texture whose data is one samples
the target, not the game's memory (where the GPU never writes). As d3d8_gl.c's xgpu_render_target_find: the one bound last. */
#define RENDERED_MAXIMUM 256

static struct
{
	unsigned long data, width, height, clock;
} rendered[RENDERED_MAXIMUM];
static unsigned rendered_count;
static unsigned long rendered_clock;

static void rendered_note(const D3DSurface *surface)
{
	unsigned long width, height, index;
	BOOL depth;

	if (!surface || !surface->Data)
		return;
	surface_dimensions(surface, &width, &height, &depth);
	if (depth)
		return;
	for (index = 0; index < rendered_count; index++)
	{
		if (rendered[index].data == surface->Data && rendered[index].width == width && rendered[index].height == height)
		{
			rendered[index].clock = ++rendered_clock;
			return;
		}
	}
	if (rendered_count == RENDERED_MAXIMUM)
		return;
	rendered[rendered_count].data = surface->Data;
	rendered[rendered_count].width = width;
	rendered[rendered_count].height = height;
	rendered[rendered_count++].clock = ++rendered_clock;
}

/* the target last bound at a texture's data, or NULL */
static const void *rendered_find(unsigned long data, unsigned long *width, unsigned long *height)
{
	unsigned long index, best = RENDERED_MAXIMUM;

	for (index = 0; index < rendered_count; index++)
	{
		if (rendered[index].data == data && (best == RENDERED_MAXIMUM || rendered[index].clock > rendered[best].clock))
			best = index;
	}
	if (best == RENDERED_MAXIMUM)
		return NULL;
	*width = rendered[best].width;
	*height = rendered[best].height;
	return &rendered[best];
}


/* ---------- debug.vk_self_test: the data a draw reads is copied at the draw (phase 3)

A buffer of three vertices in the game's memory, a red triangle that covers the target,
is put; the same buffer is rewritten in place (green) and put again; a third, larger
than the stream, blue, with its vertices across the boundary of its first part, is put
in parts. Only then does the host draw each: red, green and blue say that the copies
were made at the puts, and not at the hand-over, and that parts land contiguously.
The host says what it saw in its log. */

static void test_vertices(struct vk_test_vertex *vertices, float red, float green, float blue)
{
	static const float corners[3][2] = { { -1.0f, -1.0f }, { 3.0f, -1.0f }, { -1.0f, 3.0f } };
	int index;

	for (index = 0; index < 3; index++)
	{
		vertices[index].position[0] = corners[index][0];
		vertices[index].position[1] = corners[index][1];
		vertices[index].position[2] = 0.5f;
		vertices[index].color[0] = red;
		vertices[index].color[1] = green;
		vertices[index].color[2] = blue;
		vertices[index].color[3] = 1.0f;
	}
}

static void test_draw(uint32_t id, uint32_t offset, uint32_t red, uint32_t green, uint32_t blue, uint32_t last)
{
	struct vk_command_test_draw *command = stream_command(VK_COMMAND_TEST_DRAW, sizeof(*command));

	command->data.id = id;
	command->data.offset = offset;
	command->expected[0] = red;
	command->expected[1] = green;
	command->expected[2] = blue;
	command->last = last;
}

static void data_self_test(void)
{
	struct vk_test_vertex vertices[3];
	const unsigned long total = STREAM_SIZE + 4096;
	unsigned char *large = malloc(total);
	uint32_t red, green, blue, offset;

	if (!large)
		return;
	test_vertices(vertices, 1.0f, 0.0f, 0.0f);
	red = vk_data_put(vertices, sizeof(vertices));
	test_vertices(vertices, 0.0f, 1.0f, 0.0f);
	green = vk_data_put(vertices, sizeof(vertices));
	/* the first part ends where the stream does: the vertices start 40 bytes before that, and end in the second */
	if (data_room() < 64)
		stream_flush();
	offset = (uint32_t)data_room() - 40;
	memset(large, 0xa5, total);
	test_vertices((struct vk_test_vertex *)(large + offset), 0.0f, 0.0f, 1.0f);
	blue = vk_data_put(large, total);
	free(large);
	test_draw(red, 0, 255, 0, 0, 0);
	test_draw(green, 0, 0, 255, 0, 0);
	test_draw(blue, offset, 0, 0, 255, 1);
	stream_flush();
	data_id = 0;
}

/* the shader service: a trivial pair of shaders that compile, and one that does not (the host says so once and keeps its GLSL
in vk_cache/failed/): each is sent, then asked for until it is made or has failed */
static void shader_self_test_one(const char *name, uint32_t stage, uint64_t hash, const char *glsl, BOOL expect_made)
{
	uint32_t status = VK_SHADER_STATUS_UNKNOWN, handle = 0;
	int attempt;

	host_vk_shader_compile(stage, hash, glsl, (uint32_t)strlen(glsl));
	for (attempt = 0; attempt < 500; attempt++)
	{
		handle = host_vk_shader_find(stage, hash, &status);
		if (handle || status == VK_SHADER_STATUS_FAILED)
			break;
		usleep(10000);
	}
	platform_log("shader service self-test: %s %s (handle %u, status %u, after %d tries)",
		name, (handle != 0) == expect_made && (handle != 0 || status == VK_SHADER_STATUS_FAILED) ? "ok" : "FAILED",
		(unsigned)handle, (unsigned)status, attempt + 1);
}

static void shader_self_test(void)
{
	shader_self_test_one("a vertex shader that compiles", VK_SHADER_STAGE_VERTEX, 0x5e1f7e57deadbeefull,
		"#version 450\nvoid main()\n{\n\tgl_Position = vec4(0.0);\n}\n", TRUE);
	shader_self_test_one("a pixel shader that compiles", VK_SHADER_STAGE_PIXEL, 0x5e1f7e57deadbef0ull,
		"#version 450\nlayout(location = 0) out vec4 c;\nvoid main()\n{\n\tc = vec4(1.0);\n}\n", TRUE);
	shader_self_test_one("a pixel shader that does not", VK_SHADER_STAGE_PIXEL, 0x5e1f7e57deadbef1ull,
		"#version 450\nvoid main()\n{\n\tthis is not glsl;\n}\n", FALSE);
}
/* ---------- the renderer's name (the "renderer=" line of the log: Direct3D_CreateDevice), with the driver archive the
backend loaded when it is not the phone's own driver */

static const char *vk_renderer_name(void)
{
	static char description[80];

	if (!description[0])
	{
		char driver[56];

		driver[0] = 0;
		host_vk_driver_name(driver, sizeof(driver));
		if (driver[0])
			snprintf(description, sizeof(description), "vulkan (%s)", driver);
		else
			snprintf(description, sizeof(description), "vulkan");
	}
	return description;
}

/* ---------- visibility (occlusion) tests (phase 6, step 6)

The draws between Begin and End are the test's: the host counts the samples they pass (an occlusion query, host_vk_visibility.c)
and keeps the count, in the game's pixels, for the slot End names. The result is asked for through an import that never waits (the
latest count of the slot: from this test, or from an earlier one while the GPU is behind), as d3d8_gl.c's query buffer path gives. */

static BOOL vk_test_active;
/* the number of the latest test ended on each slot (D3DDevice_EndVisibilityTest's) */
static unsigned long vk_test_number[VISIBILITY_TEST_SLOTS];

static void vk_visibility_begin(void)
{
	if (!vk.ready || vk_test_active)
		return;
	vk_test_active = TRUE;
	stream_command(VK_COMMAND_VISIBILITY_BEGIN, sizeof(struct vk_command_header));
}

/* number: the test's (D3DDevice_EndVisibilityTest); FALSE when no test was running */
static BOOL vk_visibility_end(DWORD index, unsigned long number)
{
	struct vk_command_visibility_end *command;

	if (!vk.ready || !vk_test_active)
		return FALSE;
	vk_test_active = FALSE;
	index %= VISIBILITY_TEST_SLOTS;
	if (!index)
		index = 1;
	command = stream_command(VK_COMMAND_VISIBILITY_END, sizeof(*command));
	command->index = (uint32_t)index;
	/* the target's pixels to a game pixel: the count is of the game's pixels (visibility_unscaled in d3d8_gl.c), which the game
	divides by its own test's area (lens flares, rasterizer_lights.c), a split-screen window's or the screen's alike */
	command->area = vk_target_scale[0] * vk_target_scale[1];
	vk_test_number[index] = number;
	return TRUE;
}

/* the slot's latest count the backend knows (host_vk_visibility.c: this test's once the GPU is through its frame, the slot's
earlier count until then), and the number of the latest test ended on the slot */
static UINT vk_visibility_result(DWORD index, unsigned long *number)
{
	index %= VISIBILITY_TEST_SLOTS;
	if (!index)
		index = 1;
	*number = vk_test_number[index];
	if (!vk.ready)
		return 0;
	return host_vk_visibility((uint32_t)index);
}

/* ---------- the shaders a draw would use (phase 4)

Nothing is drawn yet (phase 6), but at each draw the device makes the pixel shader key as d3d8_gl.c's prepare_draw
does, without binding anything, and notes it and the vertex shader for the dump (debug.gpu_dump_shaders), which
tools/vk_shader_check.py and the probe's `shaders` step take as their corpus. */

typedef char pixel_shader_key_size_assert[sizeof(struct nv2a_pixel_shader_key) % 4 == 0 ? 1 : -1];

/* the key as prepare_draw makes it (count_samples stays 0: the visibility tests are occlusion queries) */
static void pixel_key_make(struct nv2a_pixel_shader_key *key)
{
	int stage;

	memset(key, 0, sizeof(*key));
	memcpy(key->combiner_state, D3D__RenderState, sizeof(key->combiner_state));
	/* constants are uniforms, not part of the program */
	memset(&key->combiner_state[D3DRS_PSCONSTANT0_0], 0, 16 * sizeof(DWORD));
	key->combiner_state[D3DRS_PSFINALCOMBINERCONSTANT0] = 0;
	key->combiner_state[D3DRS_PSFINALCOMBINERCONSTANT1] = 0;
	key->texture_modes = D3D__RenderState[D3DRS_PSTEXTUREMODES];
	/* (the stages' sampler types and the meter's coverage are textures_make's) */
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		key->alpha_kill[stage] = D3D__TextureState[stage][D3DTSS_ALPHAKILL] == D3DTALPHAKILL_ENABLE;
		key->color_sign[stage] = (unsigned char)((D3D__TextureState[stage][D3DTSS_COLORSIGN] >> 28) & 0xf);
	}
	key->alpha_test_function = D3D__RenderState[D3DRS_ALPHATESTENABLE] ? D3D__RenderState[D3DRS_ALPHAFUNC] : 0;
	key->fog_enable = D3D__RenderState[D3DRS_FOGENABLE] != 0;
	key->fog_table_mode = (unsigned char)D3D__RenderState[D3DRS_FOGTABLEMODE];
}
/* ---------- the shader service's handles (phase 5)

The host never sees a key: a shader is asked for by its hash (vk_shaders.h's identity), and for one the host does not
have, the GLSL is generated here and sent, to be compiled on the host's thread and kept on the device. A draw's shader
is the handle the host gave, or 0 while it is queued or compiling (the draw would be skipped). */

struct shader_entry
{
	uint64_t hash;
	uint32_t handle;
	unsigned char sent, failed;
	struct shader_entry *next;
};

#define SHADER_BUCKETS 1024

static struct shader_entry *shader_entries[2][SHADER_BUCKETS];
static BOOL shader_import_logged;

/* the handle of a shader, asking the host (and sending its GLSL when the host does not know it); 0 if it is not ready */
static uint32_t shader_handle(uint32_t stage, uint64_t hash, const struct vertex_shader_object *program,
	unsigned long mask, const struct nv2a_pixel_shader_key *key)
{
	struct shader_entry **bucket = &shader_entries[stage][hash % SHADER_BUCKETS];
	struct shader_entry *entry;
	uint32_t status = VK_SHADER_STATUS_UNKNOWN, handle;

	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->hash == hash)
			break;
	}
	if (!entry)
	{
		entry = calloc(1, sizeof(*entry));
		if (!entry)
			return 0;
		entry->hash = hash;
		entry->next = *bucket;
		*bucket = entry;
	}
	if (entry->handle)
		return entry->handle;
	if (entry->failed)
		return 0;
	if (!shader_import_logged)
	{
		shader_import_logged = TRUE;
		platform_log("vk shaders: the first host_vk_shader_find: stage %u, hash %08x%08x (the host logs its own)",
			(unsigned)stage, (unsigned)(hash >> 32), (unsigned)hash);
	}
	handle = host_vk_shader_find(stage, hash, &status);
	if (handle)
	{
		entry->handle = handle;
		return handle;
	}
	if (status == VK_SHADER_STATUS_FAILED)
	{
		entry->failed = 1;
		return 0;
	}
	if (status == VK_SHADER_STATUS_UNKNOWN && entry->sent < 3)
	{
		char *glsl = stage == VK_SHADER_STAGE_VERTEX ?
			nv2a_vk_vertex_shader_to_glsl((const uint32_t *)program->instructions, program->instruction_count, mask) :
			nv2a_vk_pixel_shader_to_glsl(key);

		if (glsl)
		{
			host_vk_shader_compile(stage, hash, glsl, (uint32_t)strlen(glsl));
			free(glsl);
			entry->sent++;
		}
	}
	return 0;
}

/* the shaders a draw needs, as d3d8_gl.c's prepare_draw chooses them, asked for from the backend (and sent when it lacks
them). FALSE if there is no program to draw with. The handles are 0 while a shader is queued or compiling (the draw is
skipped). key is the pixel shader's, made already (pixel_key_make, then textures_make). */
static BOOL draw_shaders(BOOL immediate, const struct nv2a_pixel_shader_key *key, unsigned long *mask_out, uint32_t *vertex,
	uint32_t *pixel)
{
	struct vertex_shader_object *program = current_program();
	unsigned long mask;
	uint32_t generator = VK_SHADER_GENERATOR_VERSION;
	uint64_t hash;

	*vertex = *pixel = 0;
	if (!program || !device.vertex_shader || !program->instructions)
		return FALSE;
	mask = immediate ? 0 : device.vertex_shader->packed_mask;
	*mask_out = mask;
	if (!vk.ready)
		return TRUE;
	/* FNV-1a 64 of the instruction count and words (vk_shaders.h): what the shader cache knows the program by */
	if (!program->vk_program_hash)
	{
		uint32_t count = (uint32_t)program->instruction_count;

		program->vk_program_hash = vk_hash_mix(vk_hash_init(), &count, sizeof(count));
		program->vk_program_hash = vk_hash_mix(program->vk_program_hash, program->instructions, count * 4 * sizeof(DWORD));
	}
	{
		uint32_t mask32 = (uint32_t)mask;

		hash = vk_hash_mix(vk_hash_init(), &generator, sizeof(generator));
		hash = vk_hash_mix(hash, &program->vk_program_hash, sizeof(program->vk_program_hash));
		hash = vk_hash_mix(hash, &mask32, sizeof(mask32));
	}
	*vertex = shader_handle(VK_SHADER_STAGE_VERTEX, hash, program, mask, NULL);
	hash = vk_hash_mix(vk_hash_init(), &generator, sizeof(generator));
	hash = vk_hash_mix(hash, key, sizeof(*key));
	*pixel = shader_handle(VK_SHADER_STAGE_PIXEL, hash, NULL, 0, key);
	return TRUE;
}

/* debug.gpu_stats: the draws of the last 60 frames, and what skipped the rest */
static struct
{
	unsigned long draws, immediate_draws, presents;
	unsigned long skipped_no_program, skipped_no_target, skipped_shader, skipped_texture, skipped_other;
} vk_stats;
static BOOL vk_statistics_on, vk_statistics_read;

/* The Xbox's enumerants are OpenGL's (D3DBLEND_*, D3DCMP_*, the stencil operations, D3DBLENDOP_*): Vulkan's numbers for them */

static uint32_t compare_op(DWORD value)
{
	/* GL_NEVER to GL_ALWAYS are 0x200 to 0x207, in Vulkan's order; 0 is never (d3d8_gl.c) */
	return value >= 0x200 && value <= 0x207 ? value - 0x200 : 0;
}

static uint32_t stencil_op(DWORD value)
{
	switch (value)
	{
	case 0: return 1; /* ZERO */
	case D3DSTENCILOP_REPLACE: return 2;
	case D3DSTENCILOP_INCRSAT: return 3;
	case D3DSTENCILOP_DECRSAT: return 4;
	case D3DSTENCILOP_INVERT: return 5;
	case D3DSTENCILOP_INCR: return 6;
	case D3DSTENCILOP_DECR: return 7;
	default: return 0; /* KEEP */
	}
}

static uint32_t blend_factor(DWORD value)
{
	switch (value)
	{
	case D3DBLEND_ZERO: return 0;
	case D3DBLEND_ONE: return 1;
	case D3DBLEND_SRCCOLOR: return 2;
	case D3DBLEND_INVSRCCOLOR: return 3;
	case D3DBLEND_DESTCOLOR: return 4;
	case D3DBLEND_INVDESTCOLOR: return 5;
	case D3DBLEND_SRCALPHA: return 6;
	case D3DBLEND_INVSRCALPHA: return 7;
	case D3DBLEND_DESTALPHA: return 8;
	case D3DBLEND_INVDESTALPHA: return 9;
	case D3DBLEND_CONSTANTCOLOR: return 10;
	case D3DBLEND_INVCONSTANTCOLOR: return 11;
	case D3DBLEND_CONSTANTALPHA: return 12;
	case D3DBLEND_INVCONSTANTALPHA: return 13;
	case D3DBLEND_SRCALPHASAT: return 14;
	default: return 1;
	}
}

static uint32_t blend_operation(DWORD value)
{
	switch (value)
	{
	case D3DBLENDOP_SUBTRACT: return 1;
	case D3DBLENDOP_REVSUBTRACT:
	case D3DBLENDOP_REVSUBTRACTSIGNED: return 2;
	case D3DBLENDOP_MIN: return 3;
	case D3DBLENDOP_MAX: return 4;
	default: return 0;
	}
}

/* primitive_mode in d3d8_gl.c, as Vulkan's topology: quads and line loops are drawn as indices (draw_make) */
static uint32_t topology_of(D3DPRIMITIVETYPE type)
{
	switch (type)
	{
	case D3DPT_POINTLIST: return 0;
	case D3DPT_LINELIST: return 1;
	case D3DPT_LINELOOP:
	case D3DPT_LINESTRIP: return 2;
	case D3DPT_TRIANGLESTRIP:
	case D3DPT_QUADSTRIP: return 4;
	case D3DPT_TRIANGLEFAN:
	case D3DPT_POLYGON: return 5;
	default: return 3;
	}
}

/* apply_raster_state in d3d8_gl.c, as the pipeline's state and the draw's dynamic state */
static void raster_state_make(struct vk_command_draw *draw, BOOL has_color, BOOL has_depth)
{
	struct vk_pipeline_state *state = &draw->state;
	DWORD *rs = D3D__RenderState;
	DWORD write = rs[D3DRS_COLORWRITEENABLE];
	BOOL depth_test = has_depth && rs[D3DRS_ZENABLE];
	long x0 = vk_target_pixel((float)device.viewport.X, 0), y0 = vk_target_pixel((float)device.viewport.Y, 1);
	long x1 = vk_target_pixel((float)(device.viewport.X + device.viewport.Width), 0);
	long y1 = vk_target_pixel((float)(device.viewport.Y + device.viewport.Height), 1);

	draw->viewport[0] = (float)x0;
	draw->viewport[1] = (float)y0;
	draw->viewport[2] = (float)(x1 - x0);
	draw->viewport[3] = (float)(y1 - y0);
	draw->viewport[4] = device.viewport.MinZ;
	draw->viewport[5] = device.viewport.MaxZ;
	/* the game never issues a scissor rectangle, and the NV2A scissor register defaults to the viewport, so fragment
	clipping follows the viewport: this is what keeps a split-screen window's geometry from bleeding across the divider */
	draw->scissor[0] = (int32_t)x0;
	draw->scissor[1] = (int32_t)y0;
	draw->scissor[2] = (int32_t)(x1 - x0);
	draw->scissor[3] = (int32_t)(y1 - y0);

	state->color_format = has_color ? 1 : 0;
	state->depth_format = has_depth ? 1 : 0;
	state->color_write_mask = ((write & D3DCOLORWRITEENABLE_RED) ? 1 : 0) | ((write & D3DCOLORWRITEENABLE_GREEN) ? 2 : 0) |
		((write & D3DCOLORWRITEENABLE_BLUE) ? 4 : 0) | ((write & D3DCOLORWRITEENABLE_ALPHA) ? 8 : 0);
	state->polygon_mode = rs[D3DRS_FILLMODE] == D3DFILL_WIREFRAME ? 1 : rs[D3DRS_FILLMODE] == D3DFILL_POINT ? 2 : 0;
	state->depth_test = depth_test;
	if (depth_test)
	{
		state->depth_write = rs[D3DRS_ZWRITEENABLE] != 0;
		state->depth_compare_op = compare_op(rs[D3DRS_ZFUNC]);
	}
	if (has_depth && rs[D3DRS_STENCILENABLE])
	{
		struct vk_stencil_face_state face;

		face.compare_op = compare_op(rs[D3DRS_STENCILFUNC]);
		face.fail_op = stencil_op(rs[D3DRS_STENCILFAIL]);
		face.depth_fail_op = stencil_op(rs[D3DRS_STENCILZFAIL]);
		face.pass_op = stencil_op(rs[D3DRS_STENCILPASS]);
		state->stencil_test = 1;
		state->stencil_front = face;
		state->stencil_back = face;
		draw->stencil_compare_mask = rs[D3DRS_STENCILMASK] & 0xff;
		draw->stencil_write_mask = rs[D3DRS_STENCILWRITEMASK] & 0xff;
		draw->stencil_reference = rs[D3DRS_STENCILREF] & 0xff;
	}
	if (rs[D3DRS_ALPHABLENDENABLE])
	{
		state->blend_enable = 1;
		state->source_color_factor = state->source_alpha_factor = blend_factor(rs[D3DRS_SRCBLEND]);
		state->destination_color_factor = state->destination_alpha_factor = blend_factor(rs[D3DRS_DESTBLEND]);
		state->color_op = state->alpha_op = blend_operation(rs[D3DRS_BLENDOP]);
		color_to_vec4(rs[D3DRS_BLENDCOLOR], draw->blend_constants);
	}
	/* the cull mode names the winding to discard, FRONTFACE the front winding: d3d8_gl.c's desktop branch (the Android one
	answers its y flip in the shaders, which these do not make: the flip is the viewport's) */
	if (rs[D3DRS_CULLMODE] != D3DCULL_NONE)
	{
		state->cull_mode = rs[D3DRS_CULLMODE] == rs[D3DRS_FRONTFACE] ? 1 : 2; /* front : back */
		state->front_face = rs[D3DRS_FRONTFACE] == D3DFRONT_CCW ? 0 : 1; /* counter-clockwise : clockwise */
	}
	/* D3DRS_ZBIAS is expressed in these states (D3DDevice_SetRenderState_ZBias): a polygon offset's units and slope factor */
	if (rs[D3DRS_SOLIDOFFSETENABLE])
	{
		state->depth_bias = 1;
		draw->depth_bias_slope = dword_to_float(rs[D3DRS_POLYGONOFFSETZSLOPESCALE]);
		draw->depth_bias_constant = dword_to_float(rs[D3DRS_POLYGONOFFSETZOFFSET]);
	}
}
/* ---------- uniforms: the three blocks, put when they differ from what this frame last put */

static struct
{
	BOOL constants_valid, parameters_valid;
	unsigned long constants_serial;
	struct vk_data_ref constants_ref, parameters_ref;
	struct vk_vertex_parameters parameters;
	/* the pixel parameters (a few distinct ones a frame at most) */
	BOOL pixel_valid;
	struct vk_pixel_parameters pixel;
	struct vk_data_ref pixel_ref;
	/* the attributes' current values (SetVertexData), as the unfed binding holds them */
	BOOL attributes_valid;
	float attributes[XGPU_VERTEX_ATTRIBUTE_COUNT][4];
	struct vk_data_ref attributes_ref;
} puts_this_frame;

/* the data ids are good for a frame (Present resets them): so is what was put */
static void puts_reset(void)
{
	memset(&puts_this_frame, 0, sizeof(puts_this_frame));
}

static struct vk_data_ref data_ref(uint32_t id)
{
	struct vk_data_ref ref;

	ref.id = id;
	ref.offset = 0;
	return ref;
}

static void pixel_parameters_make(struct vk_pixel_parameters *p, const float texture_scale[4][4])
{
	int stage;

	memset(p, 0, sizeof(*p));
	for (stage = 0; stage < 8; stage++)
	{
		color_to_vec4(D3D__RenderState[D3DRS_PSCONSTANT0_0 + stage], p->ps_c0[stage]);
		color_to_vec4(D3D__RenderState[D3DRS_PSCONSTANT1_0 + stage], p->ps_c1[stage]);
	}
	color_to_vec4(D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0], p->ps_final_c0);
	color_to_vec4(D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1], p->ps_final_c1);
	color_to_vec4(D3D__RenderState[D3DRS_FOGCOLOR], p->fog_color);
	p->fog_parameters[0] = dword_to_float(D3D__RenderState[D3DRS_FOGSTART]);
	p->fog_parameters[1] = dword_to_float(D3D__RenderState[D3DRS_FOGEND]);
	p->fog_parameters[2] = dword_to_float(D3D__RenderState[D3DRS_FOGDENSITY]);
	p->alpha_reference[0] = (float)(D3D__RenderState[D3DRS_ALPHAREF] & 0xff);
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		DWORD *state = D3D__TextureState[stage];

		p->bump_matrix[stage][0] = dword_to_float(state[D3DTSS_BUMPENVMAT00]);
		p->bump_matrix[stage][1] = dword_to_float(state[D3DTSS_BUMPENVMAT01]);
		p->bump_matrix[stage][2] = dword_to_float(state[D3DTSS_BUMPENVMAT10]);
		p->bump_matrix[stage][3] = dword_to_float(state[D3DTSS_BUMPENVMAT11]);
		p->bump_luminance[stage][0] = dword_to_float(state[D3DTSS_BUMPENVLSCALE]);
		p->bump_luminance[stage][1] = dword_to_float(state[D3DTSS_BUMPENVLOFFSET]);
	}
	memcpy(p->texture_scale, texture_scale, sizeof(p->texture_scale));
}

static void uniforms_make(struct vk_command_draw *draw, const float texture_scale[4][4])
{
	struct vk_vertex_parameters parameters;
	struct vk_pixel_parameters pixel;

	if (!puts_this_frame.constants_valid || puts_this_frame.constants_serial != constants_serial)
	{
		puts_this_frame.constants_ref = data_ref(vk_data_put(device.constants, sizeof(device.constants)));
		puts_this_frame.constants_serial = constants_serial;
		puts_this_frame.constants_valid = TRUE;
	}
	draw->vertex_constants = puts_this_frame.constants_ref;

	memcpy(parameters.viewport_scale, device.viewport_scale, sizeof(parameters.viewport_scale));
	memcpy(parameters.viewport_offset, device.viewport_offset, sizeof(parameters.viewport_offset));
	parameters.point_and_screen[0] = D3D__RenderState[D3DRS_POINTSIZE] ? dword_to_float(D3D__RenderState[D3DRS_POINTSIZE]) : 1.0f;
	parameters.point_and_screen[1] = (float)UI_OFFSET;
	parameters.point_and_screen[2] = parameters.point_and_screen[3] = 0.0f;
	if (!puts_this_frame.parameters_valid || memcmp(&puts_this_frame.parameters, &parameters, sizeof(parameters)))
	{
		puts_this_frame.parameters = parameters;
		puts_this_frame.parameters_ref = data_ref(vk_data_put(&parameters, sizeof(parameters)));
		puts_this_frame.parameters_valid = TRUE;
	}
	draw->vertex_parameters = puts_this_frame.parameters_ref;

	pixel_parameters_make(&pixel, texture_scale);
	if (!puts_this_frame.pixel_valid || memcmp(&puts_this_frame.pixel, &pixel, sizeof(pixel)))
	{
		puts_this_frame.pixel = pixel;
		puts_this_frame.pixel_ref = data_ref(vk_data_put(&pixel, sizeof(pixel)));
		puts_this_frame.pixel_valid = TRUE;
	}
	draw->pixel_parameters = puts_this_frame.pixel_ref;
}
/* ---------- vertex input */

/* Vulkan's format for a vertex element, as attribute_format in d3d8_gl.c maps it to GL (its desktop branch: D3DCOLOR is
B8G8R8A8_UNORM, which reads the Xbox's byte order as it is) */
static uint32_t element_format(unsigned long type)
{
	switch (type)
	{
	case D3DVSDT_FLOAT1: return VK_VERTEX_FORMAT_R32_SFLOAT;
	case D3DVSDT_FLOAT2: return VK_VERTEX_FORMAT_R32G32_SFLOAT;
	case D3DVSDT_FLOAT3:
	case D3DVSDT_FLOAT2H: return VK_VERTEX_FORMAT_R32G32B32_SFLOAT;
	case D3DVSDT_FLOAT4: return VK_VERTEX_FORMAT_R32G32B32A32_SFLOAT;
	case D3DVSDT_D3DCOLOR: return VK_VERTEX_FORMAT_B8G8R8A8_UNORM;
	case D3DVSDT_SHORT1: return VK_VERTEX_FORMAT_R16_SSCALED;
	case D3DVSDT_SHORT2: return VK_VERTEX_FORMAT_R16G16_SSCALED;
	case D3DVSDT_SHORT3: return VK_VERTEX_FORMAT_R16G16B16_SSCALED;
	case D3DVSDT_SHORT4: return VK_VERTEX_FORMAT_R16G16B16A16_SSCALED;
	case D3DVSDT_NORMSHORT1: return VK_VERTEX_FORMAT_R16_SNORM;
	case D3DVSDT_NORMSHORT2: return VK_VERTEX_FORMAT_R16G16_SNORM;
	case D3DVSDT_NORMSHORT3: return VK_VERTEX_FORMAT_R16G16B16_SNORM;
	case D3DVSDT_NORMSHORT4: return VK_VERTEX_FORMAT_R16G16B16A16_SNORM;
	case D3DVSDT_NORMPACKED3: return VK_VERTEX_FORMAT_R32_UINT;
	case D3DVSDT_PBYTE1: return VK_VERTEX_FORMAT_R8_UNORM;
	case D3DVSDT_PBYTE2: return VK_VERTEX_FORMAT_R8G8_UNORM;
	case D3DVSDT_PBYTE3: return VK_VERTEX_FORMAT_R8G8B8_UNORM;
	case D3DVSDT_PBYTE4: return VK_VERTEX_FORMAT_R8G8B8A8_UNORM;
	default: return VK_VERTEX_FORMAT_R32G32B32A32_SFLOAT;
	}
}

/* Vulkan requires few formats as vertex attributes (the 32-bit ones; the rest are optional): the host says, once for each
format, and the guest expands what is missing into four floats. Said once in the log. */
static BOOL format_supported(uint32_t format)
{
	static unsigned char known[100]; /* 0 not asked, 1 yes, 2 no */

	if (format >= 98)
		return TRUE;
	if (!known[format])
	{
		known[format] = host_vk_format_supported(format) ? 1 : 2;
		if (known[format] == 2)
			platform_log("vk: vertex format %u is not readable by the device as a vertex attribute; those attributes are "
				"expanded to four floats", (unsigned)format);
	}
	return known[format] == 1;
}

/* an element's value as up to four floats (the rest 0, 0, 0, 1 as a vertex input fills them) */
static void element_decode(unsigned long type, const unsigned char *source, float out[4])
{
	const float *f = (const float *)source;
	const short *s = (const short *)source;
	unsigned long index, count = 0;

	out[0] = out[1] = out[2] = 0.0f;
	out[3] = 1.0f;
	switch (type)
	{
	case D3DVSDT_FLOAT1: count = 1; break;
	case D3DVSDT_FLOAT2: count = 2; break;
	case D3DVSDT_FLOAT3:
	case D3DVSDT_FLOAT2H: count = 3; break;
	case D3DVSDT_FLOAT4: count = 4; break;
	default: break;
	}
	if (count)
	{
		for (index = 0; index < count; index++)
			out[index] = f[index];
		return;
	}
	switch (type)
	{
	case D3DVSDT_D3DCOLOR:
		out[0] = source[2] / 255.0f;
		out[1] = source[1] / 255.0f;
		out[2] = source[0] / 255.0f;
		out[3] = source[3] / 255.0f;
		return;
	case D3DVSDT_SHORT1: case D3DVSDT_SHORT2: case D3DVSDT_SHORT3: case D3DVSDT_SHORT4:
		count = type >> 4; /* the type's high nibble is its component count */
		for (index = 0; index < count; index++)
			out[index] = (float)s[index];
		return;
	case D3DVSDT_NORMSHORT1: case D3DVSDT_NORMSHORT2: case D3DVSDT_NORMSHORT3: case D3DVSDT_NORMSHORT4:
		count = type >> 4;
		for (index = 0; index < count; index++)
		{
			float value = s[index] / 32767.0f;

			out[index] = value < -1.0f ? -1.0f : value;
		}
		return;
	case D3DVSDT_PBYTE1: case D3DVSDT_PBYTE2: case D3DVSDT_PBYTE3: case D3DVSDT_PBYTE4:
		count = type >> 4;
		for (index = 0; index < count; index++)
			out[index] = source[index] / 255.0f;
		return;
	default:
		return;
	}
}

/* the stream's elements, with the formats the device cannot read as attributes put expanded to four floats: a buffer of its
own, made at the draw from the game's bytes, every element of the stream in it (a NORMPACKED3 one keeps its 32 bits) */
static uint32_t stream_expanded_put(const struct vertex_shader_object *declaration, unsigned long stream,
	const unsigned char *base, unsigned long stride, unsigned long vertices, struct vk_pipeline_state *state,
	unsigned long binding)
{
	unsigned long indexes[XGPU_VERTEX_ATTRIBUTE_COUNT], elements = 0, index, vertex;
	unsigned long out_stride, rows = stride ? vertices : 1;
	unsigned char *buffer;
	uint32_t id;

	for (index = 0; index < declaration->element_count; index++)
	{
		const struct vertex_element *element = &declaration->elements[index];

		if (element->stream == stream && element->type != D3DVSDT_NONE)
			indexes[elements++] = index;
	}
	out_stride = elements * 16;
	buffer = calloc(rows, out_stride);
	if (!buffer)
		return 0;
	for (vertex = 0; vertex < rows; vertex++)
	{
		for (index = 0; index < elements; index++)
		{
			const struct vertex_element *element = &declaration->elements[indexes[index]];
			unsigned char *at = buffer + vertex * out_stride + index * 16;
			const unsigned char *source = base + vertex * stride + element->offset;

			if (element->type == D3DVSDT_NORMPACKED3)
				memcpy(at, source, 4);
			else
				element_decode(element->type, source, (float *)at);
		}
	}
	for (index = 0; index < elements; index++)
	{
		const struct vertex_element *element = &declaration->elements[indexes[index]];

		state->attributes[element->reg].format = element->type == D3DVSDT_NORMPACKED3 ? VK_VERTEX_FORMAT_R32_UINT :
			VK_VERTEX_FORMAT_R32G32B32A32_SFLOAT;
		state->attributes[element->reg].binding = (uint32_t)binding;
		state->attributes[element->reg].offset = (uint32_t)(index * 16);
	}
	state->bindings[binding].stride = stride ? (uint32_t)out_stride : 0;
	id = vk_data_put(buffer, rows * out_stride);
	free(buffer);
	return id;
}

/* the vertices [first, first + count) of every stream the declaration reads, put, and the pipeline's bindings and attributes
for them (setup_streams in d3d8_gl.c); the attributes the declaration does not feed read the current values SetVertexData
set, from one more binding of stride 0, put again whenever they change. FALSE if the draw cannot be made. */
static BOOL vertex_input_make(struct vk_command_draw *draw, unsigned long first, unsigned long count)
{
	const struct vertex_shader_object *declaration = device.vertex_shader;
	struct vk_pipeline_state *state = &draw->state;
	int binding_of[16];
	BOOL fed[XGPU_VERTEX_ATTRIBUTE_COUNT] = { FALSE };
	unsigned long index, bindings = 0, stream;

	for (index = 0; index < 16; index++)
		binding_of[index] = -1;
	for (index = 0; index < declaration->element_count; index++)
	{
		const struct vertex_element *element = &declaration->elements[index];

		if (!device.streams[element->stream].data || element->type == D3DVSDT_NONE || binding_of[element->stream] >= 0)
			continue;
		if (bindings >= VK_PIPELINE_VERTEX_BINDINGS - 1)
			return FALSE;
		binding_of[element->stream] = (int)bindings++;
	}
	for (stream = 0; stream < 16; stream++)
	{
		unsigned long stride = device.streams[stream].stride, bytes;
		const unsigned char *base;
		BOOL expand = FALSE;
		unsigned long binding;

		if (binding_of[stream] < 0)
			continue;
		binding = (unsigned long)binding_of[stream];
		bytes = stride ? stride * count : 64;
		base = (const unsigned char *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[stream].data) + first * stride;
		for (index = 0; index < declaration->element_count; index++)
		{
			const struct vertex_element *element = &declaration->elements[index];

			if (element->stream == stream && element->type != D3DVSDT_NONE &&
				!format_supported(element_format(element->type)))
				expand = TRUE;
		}
		if (expand)
		{
			uint32_t id = stream_expanded_put(declaration, stream, base, stride, count, state, binding);

			if (!id)
				return FALSE;
			draw->vertex_buffers[binding] = data_ref(id);
		}
		else
		{
			draw->vertex_buffers[binding] = data_ref(vk_data_put(base, bytes));
			state->bindings[binding].stride = (uint32_t)stride;
			for (index = 0; index < declaration->element_count; index++)
			{
				const struct vertex_element *element = &declaration->elements[index];

				if (element->stream != stream || element->type == D3DVSDT_NONE)
					continue;
				state->attributes[element->reg].format = element_format(element->type);
				state->attributes[element->reg].binding = (uint32_t)binding;
				state->attributes[element->reg].offset = element->offset;
			}
		}
		for (index = 0; index < declaration->element_count; index++)
		{
			const struct vertex_element *element = &declaration->elements[index];

			if (element->stream == stream && element->type != D3DVSDT_NONE)
				fed[element->reg] = TRUE;
		}
	}
	/* the unfed attributes' current values: all sixteen in one binding of stride 0 (a packed attribute reads zero) */
	{
		float values[XGPU_VERTEX_ATTRIBUTE_COUNT][4];

		memcpy(values, device.attributes, sizeof(values));
		for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
		{
			if (declaration->packed_mask & (1UL << index))
				memset(values[index], 0, sizeof(values[index]));
		}
		if (!puts_this_frame.attributes_valid || memcmp(puts_this_frame.attributes, values, sizeof(values)))
		{
			memcpy(puts_this_frame.attributes, values, sizeof(values));
			puts_this_frame.attributes_ref = data_ref(vk_data_put(values, sizeof(values)));
			puts_this_frame.attributes_valid = TRUE;
		}
		draw->vertex_buffers[bindings] = puts_this_frame.attributes_ref;
		state->bindings[bindings].stride = 0;
		for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
		{
			if (fed[index])
				continue;
			state->attributes[index].format = declaration->packed_mask & (1UL << index) ? VK_VERTEX_FORMAT_R32_UINT :
				VK_VERTEX_FORMAT_R32G32B32A32_SFLOAT;
			state->attributes[index].binding = (uint32_t)bindings;
			state->attributes[index].offset = (uint32_t)(index * 16);
		}
		bindings++;
	}
	state->binding_count = (uint32_t)bindings;
	return TRUE;
}

/* the largest and smallest index of an index list */
static void vk_index_extent(const WORD *indices, unsigned long count, unsigned long *minimum, unsigned long *maximum)
{
	unsigned long index, low = 0xffff, high = 0;

	for (index = 0; index < count; index++)
	{
		if (indices[index] < low)
			low = indices[index];
		if (indices[index] > high)
			high = indices[index];
	}
	*minimum = low;
	*maximum = high;
}

/* quads become two triangles each */
static WORD *vk_quad_indices(const WORD *indices, unsigned long count, unsigned long *out_count)
{
	unsigned long quads = count / 4;
	WORD *result = malloc(quads * 6 * sizeof(WORD) + 2);
	unsigned long quad;

	for (quad = 0; quad < quads; quad++)
	{
		WORD v0 = indices ? indices[quad * 4] : (WORD)(quad * 4);
		WORD v1 = indices ? indices[quad * 4 + 1] : (WORD)(quad * 4 + 1);
		WORD v2 = indices ? indices[quad * 4 + 2] : (WORD)(quad * 4 + 2);
		WORD v3 = indices ? indices[quad * 4 + 3] : (WORD)(quad * 4 + 3);

		result[quad * 6 + 0] = v0;
		result[quad * 6 + 1] = v1;
		result[quad * 6 + 2] = v2;
		result[quad * 6 + 3] = v0;
		result[quad * 6 + 4] = v2;
		result[quad * 6 + 5] = v3;
	}
	*out_count = quads * 6;
	return result;
}

/* a line loop is a line strip with its first vertex again at its end: Vulkan has no loop */
static WORD *loop_indices(const WORD *indices, unsigned long count, unsigned long *out_count)
{
	WORD *result = malloc((count + 1) * sizeof(WORD) + 2);
	unsigned long index;

	for (index = 0; index < count; index++)
		result[index] = indices ? indices[index] : (WORD)index;
	result[count] = result[0];
	*out_count = count + 1;
	return result;
}
/* ---------- textures: bind_textures in d3d8_gl.c */

static unsigned long stage_texture_mode_of(int stage)
{
	return stage_texture_mode(stage);
}

static BOOL vk_texture_get(const DWORD *resource, const D3DCOLOR *palette, struct xgpu_texture_description *description,
	uint32_t *id, BOOL *mipmapped, long *override);
static int vk_mcc_texture_get(const DWORD *resource, const D3DCOLOR *palette, struct xgpu_texture_description *description,
	uint32_t *id, BOOL *mipmapped, long *override);

/* the Vulkan address mode for a Direct3D one (address_mode in d3d8_gl.c) */
static uint32_t vk_address_mode(DWORD mode)
{
	switch (mode)
	{
	case D3DTADDRESS_MIRROR: return 1; /* mirrored repeat */
	case D3DTADDRESS_CLAMP:
	case D3DTADDRESS_CLAMPTOEDGE: return 2; /* clamp to edge */
	case D3DTADDRESS_BORDER: return 3; /* clamp to border */
	default: return 0; /* repeat */
	}
}

/* configure_sampler in d3d8_gl.c, as a state the host keeps a VkSampler for. hires: a high-res HUD texture (hud_hires.h),
drawn smaller than it is, so filtered and from its mip levels whatever the game asks */
static void sampler_state_make(int stage, BOOL mipmapped, BOOL hires, struct vk_sampler_state *out)
{
	DWORD *state = D3D__TextureState[stage];
	DWORD min_filter = hires ? D3DTEXF_LINEAR : state[D3DTSS_MINFILTER];
	DWORD mip_filter = hires ? D3DTEXF_LINEAR : mipmapped ? state[D3DTSS_MIPFILTER] : D3DTEXF_NONE;
	DWORD mag_filter = hires ? D3DTEXF_LINEAR : state[D3DTSS_MAGFILTER];
	DWORD maximum_mip_level = hires ? 0 : state[D3DTSS_MAXMIPLEVEL];
	DWORD lod_bias = hires ? 0 : state[D3DTSS_MIPMAPLODBIAS];

	memset(out, 0, sizeof(*out));
	out->min_filter = min_filter == D3DTEXF_POINT ? 0 : 1;
	out->mag_filter = mag_filter == D3DTEXF_POINT ? 0 : 1;
	out->mip_mode = mip_filter == D3DTEXF_NONE ? 0 : mip_filter == D3DTEXF_POINT ? 1 : 2;
	out->address[0] = vk_address_mode(state[D3DTSS_ADDRESSU]);
	out->address[1] = vk_address_mode(state[D3DTSS_ADDRESSV]);
	out->address[2] = vk_address_mode(state[D3DTSS_ADDRESSW]);
	out->border_color = state[D3DTSS_BORDERCOLOR];
	/* Vulkan's sampler has the LOD bias (phase 4 took it out of the shaders) and the lowest level used */
	out->lod_bias = dword_to_float(lod_bias);
	out->min_lod = (float)maximum_mip_level;
	out->anisotropy = (min_filter == D3DTEXF_ANISOTROPIC && state[D3DTSS_MAXANISOTROPY] > 1) ?
		(float)state[D3DTSS_MAXANISOTROPY] : 1.0f;
}

/* the mip composite of a texture the game renders a level at a time: sent to the host once for each (data, size, levels) */
#define COMPOSITES_MAXIMUM 256

static struct
{
	unsigned long data, width, height, levels;
} composites_sent[COMPOSITES_MAXIMUM];
static unsigned composites_sent_count;

static void composite_send(unsigned long data, const struct xgpu_texture_description *description)
{
	struct vk_command_composite *command;
	unsigned long index;

	for (index = 0; index < composites_sent_count; index++)
	{
		if (composites_sent[index].data == data && composites_sent[index].width == description->width &&
			composites_sent[index].height == description->height && composites_sent[index].levels == description->levels)
			return;
	}
	if (composites_sent_count == COMPOSITES_MAXIMUM || description->levels > VK_COMPOSITE_LEVELS)
		return;
	composites_sent[composites_sent_count].data = data;
	composites_sent[composites_sent_count].width = description->width;
	composites_sent[composites_sent_count].height = description->height;
	composites_sent[composites_sent_count++].levels = description->levels;
	command = stream_command(VK_COMMAND_COMPOSITE, sizeof(*command));
	command->data = (uint32_t)data;
	command->width = (uint32_t)description->width;
	command->height = (uint32_t)description->height;
	command->levels = (uint32_t)description->levels;
	for (index = 0; index < description->levels; index++)
		command->level_data[index] = (uint32_t)(data + xgpu_texture_level_offset(description, index));
}

/* what each stage samples, and the scale a linear texture's coordinates get; the key's part of them (bind_textures in
d3d8_gl.c, bg_bind_textures in d3d8_bgfx.inl): the stages' sampler types, and stage 0's coverage_alpha and point_threshold
(a high-res HUD meter in the bitmap's place: hud_hires.h) */
static void textures_make(struct vk_command_draw *draw, struct nv2a_pixel_shader_key *key, float texture_scale[4][4])
{
	int stage;

	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		D3DBaseTexture *texture = device.textures[stage];
		unsigned long mode = stage_texture_mode_of(stage);
		struct vk_draw_texture *out = &draw->textures[stage];

		texture_scale[stage][0] = texture_scale[stage][1] = texture_scale[stage][2] = texture_scale[stage][3] = 1.0f;
		if (!texture || !texture->Data || mode == 0 || mode == 0x04 || mode == 0x05 || mode == 0x11)
		{
			key->sampler_type[stage] = mode == 0x11 ? _xgpu_sampler_2d : _xgpu_sampler_none;
			out->sampler_type = key->sampler_type[stage];
			continue;
		}
			continue;
		{
			struct xgpu_texture_description description;
			const D3DCOLOR *palette = device.palettes[stage] && device.palettes[stage]->Data ?
				(const D3DCOLOR *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.palettes[stage]->Data) : NULL;
			unsigned long target_width, target_height, levels = 1;
			BOOL mipmapped = FALSE, hires = FALSE;
			unsigned int name;
			uint32_t id = 0;

			xgpu_texture_describe(texture->Format, texture->Size, &description);
			key->sampler_type[stage] = description.cube_map ? _xgpu_sampler_cube :
				description.depth > 1 ? _xgpu_sampler_3d : _xgpu_sampler_2d;
			/* a texture whose data is a render target's samples the target (render-to-texture); linear ones have their
			coordinates scaled by the target's size */
			if (rendered_find(texture->Data, &target_width, &target_height))
			{
				levels = 1;
				key->sampler_type[stage] = _xgpu_sampler_2d;
				out->kind = VK_TEXTURE_TARGET;
				out->id = texture->Data;
				if (description.linear)
				{
					texture_scale[stage][0] = 1.0f / (float)target_width;
					texture_scale[stage][1] = 1.0f / (float)target_height;
				}
				/* the water renders a texture a mip level at a time, each a surface of its own: the levels are put together
				in a composite (mip_composite_get) */
				if (!description.linear && !description.cube_map && description.levels > 1 &&
					target_width == description.width && target_height == description.height)
				{
					levels = description.levels;
					composite_send(texture->Data, &description);
				}
				out->levels = (uint32_t)levels;
				out->width = (uint32_t)description.width;
				out->height = (uint32_t)description.height;
				out->sampler_type = key->sampler_type[stage];
				sampler_state_make(stage, levels > 1, FALSE, &out->sampler);
				continue;
			}
			/* (the high-res text's atlas for its placeholder bitmap, and a menu's art: text_hires.h, menu_files.h) */
			if ((name = text_hires_atlas_texture(texture->Data)) != 0)
			{
				id = name;
			}
			else if ((name = menu_art_texture(texture->Data, &levels)) != 0)
			{
				id = name;
				mipmapped = levels > 1;
				hires = TRUE;
			}
			else
			{
				long override = -1;
				int mcc = vk_mcc_texture_get((const DWORD *)texture, palette, &description, &id, &mipmapped, &override);

				if (mcc >= 0)
				{
					/* (an MCC map's: its kind from its own description, and a converted meter's coverage, as d3d8_gl.c's
					bind_textures) */
					key->sampler_type[stage] = description.cube_map ? _xgpu_sampler_cube :
						description.depth > 1 ? _xgpu_sampler_3d : _xgpu_sampler_2d;
					hires = description.hires;
					if (stage == 0)
					{
						key->coverage_alpha = description.hires_coverage != FALSE;
						key->point_threshold = description.hires_point_threshold != FALSE;
					}
				}
				if (mcc == 0 || (mcc < 0 &&
					!vk_texture_get((const DWORD *)texture, palette, &description, &id, &mipmapped, &override)))
					id = 0;
				else if (override >= 0)
				{
					/* (a high-res HUD texture in the bitmap's place: hud_hires.h) */
					name = hud_hires_override_texture(override, &levels);
					id = name;
					mipmapped = levels > 1;
					hires = TRUE;
					if (stage == 0)
					{
						key->coverage_alpha = hud_hires_override_coverage(override) != 0;
						key->point_threshold = hud_hires_override_point_threshold(override) != 0;
					}
				}
			}
			if (description.linear)
			{
				texture_scale[stage][0] = 1.0f / (float)description.width;
				texture_scale[stage][1] = 1.0f / (float)description.height;
			}
			out->sampler_type = key->sampler_type[stage];
			if (!id)
			{
				vk_stats.skipped_texture++;
				continue;
			}
			out->kind = VK_TEXTURE_IMAGE;
			out->id = id;
			sampler_state_make(stage, mipmapped, hires, &out->sampler);
		}
	}
	/* (only with the meter's blend: hud_hires.h, nv2a_pixel_shader_key) */
	key->coverage_alpha = key->coverage_alpha && D3D__RenderState[D3DRS_ALPHABLENDENABLE] &&
		D3D__RenderState[D3DRS_SRCBLEND] == D3DBLEND_CONSTANTCOLOR &&
		D3D__RenderState[D3DRS_DESTBLEND] == D3DBLEND_SRCALPHA;
}

/* a draw: prepare_draw and the draw calls of d3d8_gl.c, as one VK_COMMAND_DRAW. index_data is the game's indices (NULL for
a draw of consecutive vertices from start); immediate is End's draw of the vertices of Begin. */
static void draw_make(D3DPRIMITIVETYPE type, unsigned long count, BOOL immediate, const WORD *index_data, unsigned long start)
{
	struct vk_command_draw record;
	struct nv2a_pixel_shader_key key;
	float texture_scale[4][4];
	unsigned long mask, minimum = 0, maximum = 0, first, vertices, index_count = 0;
	uint32_t vertex, pixel;
	BOOL has_depth = FALSE;
	WORD *made = NULL;
	const WORD *indices = NULL;
	int stage;

	if (!vk.ready)
		return;
	if (!current_program() || !device.vertex_shader)
	{
		vk_stats.skipped_no_program++;
		return;
	}
	if (!targets_bind(&has_depth))
	{
		vk_stats.skipped_no_target++;
		return;
	}
	memset(&record, 0, sizeof(record));
	pixel_key_make(&key);
	textures_make(&record, &key, texture_scale);
	if (!draw_shaders(immediate, &key, &mask, &vertex, &pixel))
	{
		vk_stats.skipped_no_program++;
		return;
	}
	if (!vertex || !pixel)
	{
		vk_stats.skipped_shader++;
		return;
	}
	record.vertex_shader = vertex;
	record.pixel_shader = pixel;
	raster_state_make(&record, targets_told.color.kind != VK_SURFACE_NONE, has_depth);
	record.state.topology = topology_of(type);

	/* the indices: the game's (an indexed draw), or made (a quad list, a line loop) */
	if (index_data)
	{
		vk_index_extent(index_data, count, &minimum, &maximum);
		first = device.base_vertex_index + minimum;
		vertices = maximum - minimum + 1;
		indices = index_data;
		index_count = count;
	}
	else
	{
		first = start;
		vertices = count;
	}
	if (type == D3DPT_QUADLIST)
	{
		made = vk_quad_indices(index_data, count, &index_count);
		indices = made;
	}
	else if (type == D3DPT_LINELOOP)
	{
		made = loop_indices(index_data, count, &index_count);
		indices = made;
	}
	if (immediate)
	{
		record.vertex_buffers[0] = data_ref(vk_data_put(device.immediate_vertices,
			count * XGPU_VERTEX_ATTRIBUTE_COUNT * 4 * sizeof(float)));
		record.state.binding_count = 1;
		record.state.bindings[0].stride = XGPU_VERTEX_ATTRIBUTE_COUNT * 4 * sizeof(float);
		for (stage = 0; stage < XGPU_VERTEX_ATTRIBUTE_COUNT; stage++)
		{
			record.state.attributes[stage].format = VK_VERTEX_FORMAT_R32G32B32A32_SFLOAT;
			record.state.attributes[stage].binding = 0;
			record.state.attributes[stage].offset = (uint32_t)stage * 16;
		}
	}
	else if (!vertex_input_make(&record, first, vertices))
	{
		vk_stats.skipped_other++;
		free(made);
		return;
	}
	if (indices)
	{
		if (!index_count)
		{
			free(made);
			return;
		}
		record.indexed = 1;
		record.index_data = data_ref(vk_data_put(indices, index_count * sizeof(WORD)));
		record.vertex_offset = index_data ? -(int32_t)minimum : 0;
		record.count = (uint32_t)index_count;
	}
	else
	{
		record.count = (uint32_t)count;
	}
	free(made);
	uniforms_make(&record, texture_scale);
	{
		struct vk_command_draw *command = stream_command(VK_COMMAND_DRAW, sizeof(*command));

		record.header = command->header;
		*command = record;
	}
	if (immediate)
		vk_stats.immediate_draws++;
	else
		vk_stats.draws++;
}

static void vk_draw_vertices(D3DPRIMITIVETYPE type, unsigned long start, unsigned long count)
{
	if (count)
		draw_make(type, count, FALSE, NULL, start);
}

static void vk_draw_indexed(D3DPRIMITIVETYPE type, unsigned long count, const WORD *indices)
{
	if (count && indices)
		draw_make(type, count, FALSE, indices, 0);
}

/* End's draw of the vertices of Begin (device.immediate_vertices) */
static void vk_draw_immediate(D3DPRIMITIVETYPE type, unsigned long count)
{
	if (count)
		draw_make(type, count, TRUE, NULL, 0);
}

/* ---------- clearing */

static void vk_clear(DWORD count, const D3DRECT *rectangles, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
	struct vk_command_clear *command;
	uint32_t clear_flags = 0;
	BOOL has_depth = FALSE;
	DWORD index, kept = 0;

	if (!vk.ready || !targets_bind(&has_depth))
		return;
	if (flags & D3DCLEAR_TARGET)
	{
		/* the Xbox clears the channels named (D3DCLEAR_TARGET_R, _G, _B, _A):
		the fog screen clears only alpha, leaving the picture under the fog */
		clear_flags |= ((flags & D3DCLEAR_TARGET_R) ? VK_CLEAR_RED : 0) | ((flags & D3DCLEAR_TARGET_G) ? VK_CLEAR_GREEN : 0) |
			((flags & D3DCLEAR_TARGET_B) ? VK_CLEAR_BLUE : 0) | ((flags & D3DCLEAR_TARGET_A) ? VK_CLEAR_ALPHA : 0);
	}
	if (has_depth && (flags & D3DCLEAR_ZBUFFER))
		clear_flags |= VK_CLEAR_DEPTH;
	if (has_depth && (flags & D3DCLEAR_STENCIL))
		clear_flags |= VK_CLEAR_STENCIL;
	if (!clear_flags)
		return;
	command = stream_command(VK_COMMAND_CLEAR, sizeof(*command) + (count && rectangles ? count : 1) * 4 * sizeof(uint32_t));
	command->flags = clear_flags;
	color_to_vec4(color, command->color);
	command->depth = z;
	command->stencil = stencil;
	if (!count || !rectangles)
	{
		/* the NV2A clips a viewport-less clear to the viewport, which is what
		keeps a split-screen window's clear from wiping the other window */
		long x0 = vk_target_pixel((float)device.viewport.X, 0);
		long y0 = vk_target_pixel((float)device.viewport.Y, 1);

		command->rectangles[0][0] = (uint32_t)x0;
		command->rectangles[0][1] = (uint32_t)y0;
		command->rectangles[0][2] = (uint32_t)(vk_target_pixel((float)(device.viewport.X + device.viewport.Width), 0) - x0);
		command->rectangles[0][3] = (uint32_t)(vk_target_pixel((float)(device.viewport.Y + device.viewport.Height), 1) - y0);
		command->rectangle_count = 1;
		return;
	}
	for (index = 0; index < count; index++)
	{
		INT left = rectangles[index].x1 > device.viewport.X ? rectangles[index].x1 : device.viewport.X;
		INT top = rectangles[index].y1 > device.viewport.Y ? rectangles[index].y1 : device.viewport.Y;
		INT right = rectangles[index].x2 < device.viewport.X + device.viewport.Width ?
			rectangles[index].x2 : device.viewport.X + device.viewport.Width;
		INT bottom = rectangles[index].y2 < device.viewport.Y + device.viewport.Height ?
			rectangles[index].y2 : device.viewport.Y + device.viewport.Height;
		long x0, y0;

		if (left >= right || top >= bottom)
			continue;
		x0 = vk_target_pixel((float)(left + UI_OFFSET), 0);
		y0 = vk_target_pixel((float)top, 1);
		command->rectangles[kept][0] = (uint32_t)x0;
		command->rectangles[kept][1] = (uint32_t)y0;
		command->rectangles[kept][2] = (uint32_t)(vk_target_pixel((float)(right + UI_OFFSET), 0) - x0);
		command->rectangles[kept][3] = (uint32_t)(vk_target_pixel((float)bottom, 1) - y0);
		kept++;
	}
	command->rectangle_count = kept;
}
/* ---------- presentation */

static void vk_textures_frame(void);

/* the frame's end: the back buffer shown, the stream handed over, the next frame started with nothing bound and no data */
static void vk_present(void)
{
	struct vk_command_present *command;
	float scale[2];

	if (!vk.ready)
		return;
	command = stream_command(VK_COMMAND_PRESENT, sizeof(*command));
	surface_describe(&device.back_buffer, FALSE, &command->back_buffer, scale);
	stream_flush();
	targets_known = FALSE;
	data_id = 0;
	puts_reset();
	vk_textures_frame();
	vk.frame++;
	vk_stats.presents++;
	/* the start got as far as drawing: the driver's start note goes (vk_initialize) */
	if (vk.note_path[0] && vk.frame >= 30)
	{
		unlink(vk.note_path);
		vk.note_path[0] = 0;
	}
	if (!vk_statistics_read)
	{
		vk_statistics_read = TRUE;
		vk_statistics_on = config_boolean("debug.gpu_stats");
	}
	if (vk_statistics_on && vk_stats.presents >= 60)
	{
		platform_log("frame %lu: %lu draws, %lu immediate; skipped %lu no program, %lu no target, %lu shader not ready, "
			"%lu textured, %lu other", device.frame, vk_stats.draws / vk_stats.presents,
			vk_stats.immediate_draws / vk_stats.presents, vk_stats.skipped_no_program, vk_stats.skipped_no_target,
			vk_stats.skipped_shader, vk_stats.skipped_texture, vk_stats.skipped_other);
		memset(&vk_stats, 0, sizeof(vk_stats));
	}
}

static void vk_frame_account(char *text, size_t size)
{
	if (size)
		text[0] = 0;
}

static BOOL vk_active(void)
{
	return vk.ready;
}

/* the back buffer as the game sees it, and the pixels it is drawn at (view_read in d3d8_gl.c) */
static BOOL vk_back_buffer_size(int *width, int *height, int *pixel_width, int *pixel_height)
{
	struct vk_surface surface;
	float scale[2];

	if (!vk.ready)
		return FALSE;
	surface_describe(&device.back_buffer, FALSE, &surface, scale);
	if (surface.kind == VK_SURFACE_NONE)
		return FALSE;
	*width = (int)surface.width;
	*height = (int)surface.height;
	*pixel_width = (int)surface.pixel_width;
	*pixel_height = (int)surface.pixel_height;
	return TRUE;
}

/* ---------- the game's textures as images

As the bgfx backend keeps its own (d3d8_bgfx.inl, bg_texture_get): found by the texture's address, format and size (and its
palette), uploaded again when a page they were read from was written (memory_watch.c), decoded by xbox_textures.c's helpers,
BC1 to BC3 sent as they are when the device samples them. An image is let go after VK_TEXTURE_IDLE_FRAMES frames unused. The
high-res HUD bitmaps, the text's atlas and the menus' art are images of the same numbering (xgpu_vulkan_texture_create). */

#define VK_TEXTURE_BUCKETS 4096
#define VK_TEXTURE_IDLE_FRAMES 1800

struct vk_texture
{
	struct vk_texture *next;
	DWORD data, format_word, size_word;
	unsigned long palette_hash;
	unsigned long address, size, generation;
	struct xgpu_texture_description description;
	uint32_t id;
	int kind;
	BOOL created, mipmapped, failed;
	/* the frame it was last drawn with */
	unsigned long used_frame;
	/* the high-res HUD texture drawn in its place (hud_hires.h), or -1, as of its settings' changes */
	long override;
	unsigned long override_settings;
};

static struct vk_texture *vk_textures[VK_TEXTURE_BUCKETS];
/* the image numbers (the backend's table has VK_TEXTURE_LIMIT of them, 0 being no texture): handed out in order, and taken
back when an image is let go */
static uint32_t vk_id_next = 1;
static uint32_t vk_id_free[VK_TEXTURE_LIMIT];
static uint32_t vk_id_free_count;

static uint32_t vk_id_take(void)
{
	if (vk_id_free_count)
		return vk_id_free[--vk_id_free_count];
	if (vk_id_next >= VK_TEXTURE_LIMIT)
		return 0;
	return vk_id_next++;
}

static void vk_image_free(uint32_t id)
{
	struct vk_command_texture_free *command;

	if (!id)
		return;
	command = stream_command(VK_COMMAND_TEXTURE_FREE, sizeof(*command));
	command->id = id;
	vk_id_free[vk_id_free_count++] = id;
}

/* makes image id (its texels undefined until the data comes) */
static void vk_image_make(uint32_t id, uint32_t kind, uint32_t format, unsigned long width, unsigned long height,
	unsigned long depth, unsigned long levels)
{
	struct vk_command_texture *command = stream_command(VK_COMMAND_TEXTURE, sizeof(*command));

	command->id = id;
	command->kind = kind;
	command->format = format;
	command->width = (uint32_t)width;
	command->height = (uint32_t)height;
	command->depth = (uint32_t)depth;
	command->levels = (uint32_t)levels;
}

/* sends a level of a face: its texels put, and the command that names them. rows is 0 for all of the level, else the 2D BGRA
rows [top, top + rows) of it */
static void vk_image_data(uint32_t id, unsigned long level, unsigned long face, const void *texels, unsigned long bytes,
	unsigned long top, unsigned long rows)
{
	uint32_t put = vk_data_put(texels, bytes);
	struct vk_command_texture_data *command = stream_command(VK_COMMAND_TEXTURE_DATA, sizeof(*command));

	command->id = id;
	command->level = (uint32_t)level;
	command->face = (uint32_t)face;
	command->data.id = put;
	command->data.offset = 0;
	command->top = (uint32_t)top;
	command->rows = (uint32_t)rows;
}

/* whether BC1 to BC3 go to the device as they are (said once) */
static BOOL vk_bc_native(void)
{
	static int known = -1;

	if (known < 0)
	{
		known = host_vk_bc_supported() != 0;
		platform_log("vk: BC1 to BC3 textures %s", known ? "are sampled as they are" : "are decoded to BGRA");
	}
	return known;
}

static unsigned long vk_level_dimension(unsigned long size, unsigned long level)
{
	size >>= level;
	return size ? size : 1;
}

static unsigned long vk_palette_hash(const D3DCOLOR *palette)
{
	return palette ? hash_words(palette, 256 * sizeof(D3DCOLOR)) : 0;
}

/* the texture's texels sent as image entry->id: the image's description, then every level of every face. A compressed
texture is read where the game keeps it (BC) or decoded; any other is decoded to BGRA (bg_texture_upload in d3d8_bgfx.inl) */
static void vk_texture_upload(struct vk_texture *entry, const unsigned char *base, const D3DCOLOR *palette)
{
	struct xgpu_texture_description *description = &entry->description;
	int dxt = description->compressed ? xgpu_texture_dxt(description) : 0;
	unsigned long levels = description->levels ? description->levels : 1, level, face;
	BOOL volume = description->depth > 1;
	/* (a 3D image of a BC format is optional in Vulkan, where 2D and cube ones are not: a 3D one is decoded) */
	BOOL native = dxt && !volume && vk_bc_native();
	unsigned long *decoded = NULL;

	if (description->compressed && !dxt)
	{
		platform_log("vk: a compressed texture that is not DXT is not drawn");
		entry->failed = TRUE;
		return;
	}
	if (!entry->created)
	{
		entry->id = vk_id_take();
		if (!entry->id)
		{
			entry->failed = TRUE;
			return;
		}
		entry->kind = description->cube_map ? VK_IMAGE_CUBE : volume ? VK_IMAGE_3D : VK_IMAGE_2D;
		entry->created = TRUE;
		entry->mipmapped = levels > 1;
	}
	vk_image_make(entry->id, (uint32_t)entry->kind, native ? (dxt == 1 ? VK_IMAGE_FORMAT_BC1 : dxt == 3 ? VK_IMAGE_FORMAT_BC2 :
		VK_IMAGE_FORMAT_BC3) : VK_IMAGE_FORMAT_BGRA, description->width, description->height,
		volume ? description->depth : 1, levels);
	if (!native)
	{
		decoded = malloc(description->width * description->height * (volume ? description->depth : 1) * 4);
		if (!decoded)
		{
			entry->failed = TRUE;
			return;
		}
	}
	for (face = 0; face < (description->cube_map ? 6UL : 1UL); face++)
	for (level = 0; level < levels; level++)
	{
		unsigned long width = vk_level_dimension(description->width, level);
		unsigned long height = vk_level_dimension(description->height, level);
		unsigned long depth = volume ? vk_level_dimension(description->depth, level) : 1;
		const unsigned char *source = base + face * xgpu_texture_face_size(description) +
			xgpu_texture_level_offset(description, level);

		/* (Halo PC's cube map: its levels one after another, each the six faces', the second and third swapped;
		xbox_textures.c's upload) */
		if (description->pc_layout && description->cube_map)
			source = base + xgpu_texture_level_offset(description, level) * 6 +
				(face == 1 ? 2 : face == 2 ? 1 : face) * xgpu_texture_level_bytes(description, level);
		if (native)
		{
			vk_image_data(entry->id, level, face, source, xgpu_texture_level_bytes(description, level), 0, 0);
			continue;
		}
		if (!xgpu_texture_decode_level(description, level, source, palette, decoded))
			memset(decoded, 0, width * height * depth * 4);
		vk_image_data(entry->id, level, face, decoded, width * height * depth * 4, 0, 0);
	}
	free(decoded);
}

/* a stage's texture (xgpu_texture_get's), uploaded again when its pages have been written; FALSE if it cannot be drawn */
static BOOL vk_texture_get(const DWORD *resource, const D3DCOLOR *palette, struct xgpu_texture_description *description,
	uint32_t *id, BOOL *mipmapped, long *override)
{
	DWORD data = resource[1], format_word = resource[3], size_word = resource[4];
	BOOL palettized = ((format_word & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT) == 0x0b;
	unsigned long hash = vk_palette_hash(palette);
	unsigned long bucket = (hash_words(&data, sizeof(data)) ^ hash_words(&format_word, sizeof(format_word)) * 31 ^
		hash_words(&size_word, sizeof(size_word)) * 17 ^ hash) % VK_TEXTURE_BUCKETS;
	struct vk_texture *entry;
	unsigned long generation;

	*override = -1;
	for (entry = vk_textures[bucket]; entry; entry = entry->next)
	{
		if (entry->data == data && entry->format_word == format_word && entry->size_word == size_word &&
			entry->palette_hash == hash)
			break;
	}
	if (!entry)
	{
		entry = calloc(1, sizeof(*entry));
		if (!entry)
			return FALSE;
		entry->data = data;
		entry->format_word = format_word;
		entry->size_word = size_word;
		entry->palette_hash = hash;
		entry->override = -1;
		xgpu_texture_describe(format_word, size_word, &entry->description);
#ifdef HALO_64BIT
		entry->address = (unsigned int)data | PLATFORM_CONTIGUOUS_BASE; /* an Xbox address */
#else
		entry->address = (unsigned long)PLATFORM_PHYSICAL_TO_VIRTUAL(data);
#endif
		entry->size = xgpu_texture_face_size(&entry->description) * (entry->description.cube_map ? 6 : 1);
		entry->next = vk_textures[bucket];
		vk_textures[bucket] = entry;
	}
	*description = entry->description;
	if (entry->failed || !entry->size)
		return FALSE;
	generation = memory_watch_generation(entry->address, entry->size);
	if (!entry->created || generation > entry->generation || entry->override_settings != hud_hires_settings())
	{
		entry->description.pc_layout = (resource[0] & D3DCOMMON_PORT_PC_LAYOUT) != 0;
		entry->description.pc_meter = (resource[0] & D3DCOMMON_PORT_PC_METER) != 0;
		entry->description.pc_multipurpose = (resource[0] & D3DCOMMON_PORT_PC_MULTIPURPOSE) != 0;
		memory_watch_protect(entry->address, entry->size);
		entry->generation = memory_watch_generation(entry->address, entry->size);
		if (!entry->generation)
			entry->generation = 1;
		/* (which bitmap is here may have changed with the pixels, and whether it has a high-res stand-in with the
		settings) */
		entry->override = -1;
		entry->override_settings = hud_hires_settings();
		if (!palettized && !entry->description.cube_map && entry->description.depth == 1)
		{
			unsigned long levels;

			entry->override = hud_hires_override_find(entry->address, entry->description.width,
				entry->description.height, entry->description.levels > 1 ?
				xgpu_texture_level_offset(&entry->description, 1) : xgpu_texture_face_size(&entry->description));
			if (entry->override >= 0 && !hud_hires_override_texture(entry->override, &levels))
				entry->override = -1;
		}
		if (entry->override < 0)
		{
			if (!platform_is_contiguous(xbox_pointer(entry->address)) ||
				!platform_is_contiguous(xbox_pointer(entry->address + entry->size - 1)))
				return FALSE;
			/* (drawn with already this frame: a new image, so the frame's earlier draws keep the old pixels, as they
			did on the Xbox) */
			if (entry->created && entry->used_frame == vk.frame)
			{
				vk_image_free(entry->id);
				entry->id = 0;
				entry->created = FALSE;
			}
			vk_texture_upload(entry, (const unsigned char *)xbox_pointer(entry->address), palette);
		}
		*description = entry->description;
	}
	*override = entry->override;
	if (entry->override >= 0)
		return TRUE;
	if (entry->failed || !entry->created)
		return FALSE;
	entry->used_frame = vk.frame;
	*id = entry->id;
	*mipmapped = entry->mipmapped;
	return TRUE;
}

/* once a frame: the images not drawn with for VK_TEXTURE_IDLE_FRAMES are let go (the next draw uploads again) */
static void vk_textures_frame(void)
{
	unsigned long bucket;

	if (vk.frame % 120)
		return;
	for (bucket = 0; bucket < VK_TEXTURE_BUCKETS; bucket++)
	{
		struct vk_texture *entry;

		for (entry = vk_textures[bucket]; entry; entry = entry->next)
		{
			if (entry->created && vk.frame - entry->used_frame > VK_TEXTURE_IDLE_FRAMES)
			{
				vk_image_free(entry->id);
				entry->id = 0;
				entry->created = FALSE;
				entry->generation = 0;
			}
		}
	}
}

/* ---------- MCC maps' textures (mcc_texture_bridge.c): their pixels are immutable allocations outside the Xbox's memory,
behind headers whose Data names nothing, so they are found by the header itself and uploaded once (for each palette a
paletted one is drawn with), as the bgfx backend keeps them */

#if !defined(HALO_MCC) || HALO_MCC
#define VK_MCC_TEXTURE_BUCKETS 512

struct vk_mcc_texture
{
	struct vk_mcc_texture *next;
	const DWORD *resource;
	unsigned long palette_hash;
	struct vk_texture texture;
};

static struct vk_mcc_texture *vk_mcc_textures[VK_MCC_TEXTURE_BUCKETS];

static unsigned long vk_mcc_bucket(const DWORD *resource)
{
	return (unsigned long)((((uintptr_t)resource >> 4) * 2654435761UL) % VK_MCC_TEXTURE_BUCKETS);
}

/* an MCC header's texture, as vk_texture_get's (override: its high-res HUD stand-in, or -1); -1 if resource is not an MCC
texture's header */
static int vk_mcc_texture_get(const DWORD *resource, const D3DCOLOR *palette, struct xgpu_texture_description *description,
	uint32_t *id, BOOL *mipmapped, long *override)
{
	struct xgpu_texture_description source;
	const void *pixels;
	unsigned long bytes, hash;
	long hud_asset;
	struct vk_mcc_texture *entry;

	if (!xgpu_mcc_texture_source(resource, &source, &pixels, &bytes, &hud_asset))
		return -1;
	*description = source;
	*override = -1;
	if (hud_asset >= 0 && config_boolean("display.high_res_hud"))
	{
		unsigned long levels;

		if (hud_hires_override_texture(hud_asset, &levels))
		{
			*override = hud_asset;
			return TRUE;
		}
	}
	hash = source.format == 0x0b ? vk_palette_hash(palette) : 0;
	for (entry = vk_mcc_textures[vk_mcc_bucket(resource)]; entry; entry = entry->next)
	{
		if (entry->resource == resource && entry->palette_hash == hash)
			break;
	}
	if (!entry)
	{
		entry = calloc(1, sizeof(*entry));
		if (!entry)
		{
			xgpu_mcc_texture_failed(&source, "no memory for the Vulkan renderer's copy");
			return FALSE;
		}
		entry->resource = resource;
		entry->palette_hash = hash;
		entry->texture.description = source;
		entry->texture.override = -1;
		entry->next = vk_mcc_textures[vk_mcc_bucket(resource)];
		vk_mcc_textures[vk_mcc_bucket(resource)] = entry;
		vk_texture_upload(&entry->texture, (const unsigned char *)pixels, palette);
		if (entry->texture.failed || !entry->texture.created)
		{
			entry->texture.failed = TRUE;
			xgpu_mcc_texture_failed(&source, "the Vulkan renderer could not make it");
		}
	}
	if (entry->texture.failed)
		return FALSE;
	entry->texture.used_frame = vk.frame;
	*id = entry->texture.id;
	*mipmapped = entry->texture.mipmapped;
	return TRUE;
}

/* an MCC header going with its map (xgpu_mcc_texture_unbind): its copies */
static void vk_mcc_texture_forget(const DWORD *resource)
{
	struct vk_mcc_texture **link = &vk_mcc_textures[vk_mcc_bucket(resource)];

	while (*link)
	{
		struct vk_mcc_texture *entry = *link;

		if (entry->resource != resource)
		{
			link = &entry->next;
			continue;
		}
		*link = entry->next;
		if (entry->texture.created && vk.ready)
			vk_image_free(entry->texture.id);
		free(entry);
	}
}
#else
static int vk_mcc_texture_get(const DWORD *resource, const D3DCOLOR *palette, struct xgpu_texture_description *description,
	uint32_t *id, BOOL *mipmapped, long *override)
{
	(void)resource; (void)palette; (void)description; (void)id; (void)mipmapped; (void)override;
	return -1;
}

static void vk_mcc_texture_forget(const DWORD *resource)
{
	(void)resource;
}
#endif

/* ---------- textures the port's own modules make (xgpu.h): the high-res HUD's (hud_hires.c), the text's atlas
(text_hires.c) and the menus' art (menu_files.c), 8-bit RGBA with rows from the top, as the bgfx backend's
xgpu_bgfx_texture_create; the name is the image's number */

/* rows [top, top + rows) of level 0 of image name, from RGBA texels (red first) */
void xgpu_vulkan_texture_rows(unsigned int name, unsigned long top, unsigned long width, unsigned long rows,
	const unsigned char *rgba)
{
	unsigned char *bgra;
	unsigned long index;

	if (!vk.ready || !name || !rows)
		return;
	bgra = malloc(width * rows * 4);
	if (!bgra)
		return;
	for (index = 0; index < width * rows; index++)
	{
		bgra[index * 4 + 0] = rgba[index * 4 + 2];
		bgra[index * 4 + 1] = rgba[index * 4 + 1];
		bgra[index * 4 + 2] = rgba[index * 4 + 0];
		bgra[index * 4 + 3] = rgba[index * 4 + 3];
	}
	vk_image_data(name, 0, 0, bgra, width * rows * 4, top, rows);
	free(bgra);
}

/* a whole level of image id from RGBA texels */
static void vk_rgba_level_send(uint32_t id, unsigned long level, unsigned long width, unsigned long height,
	const unsigned char *rgba)
{
	unsigned char *bgra = malloc(width * height * 4);
	unsigned long index;

	if (!bgra)
		return;
	for (index = 0; index < width * height; index++)
	{
		bgra[index * 4 + 0] = rgba[index * 4 + 2];
		bgra[index * 4 + 1] = rgba[index * 4 + 1];
		bgra[index * 4 + 2] = rgba[index * 4 + 0];
		bgra[index * 4 + 3] = rgba[index * 4 + 3];
	}
	vk_image_data(id, level, 0, bgra, width * height * 4, 0, 0);
	free(bgra);
}

unsigned int xgpu_vulkan_texture_create(unsigned long width, unsigned long height, const unsigned char *rgba,
	BOOL mipmapped)
{
	unsigned long levels = 1, largest, level, level_width = width, level_height = height;
	const unsigned char *from = rgba;
	unsigned char *pixels = NULL;
	uint32_t id;

	if (!vk.ready || !width || !height)
		return 0;
	id = vk_id_take();
	if (!id)
		return 0;
	if (mipmapped)
		for (largest = width > height ? width : height; largest > 1; largest >>= 1)
			levels++;
	vk_image_make(id, VK_IMAGE_2D, VK_IMAGE_FORMAT_BGRA, width, height, 1, levels);
	if (!rgba)
	{
		/* (its texels come by rows: the text's atlas) */
		unsigned char *clear = calloc(width * height, 4);

		if (clear)
		{
			vk_image_data(id, 0, 0, clear, width * height * 4, 0, 0);
			free(clear);
		}
		return id;
	}
	for (level = 0; level < levels; level++)
	{
		unsigned long next_width = level_width > 1 ? level_width / 2 : 1;
		unsigned long next_height = level_height > 1 ? level_height / 2 : 1;
		unsigned char *next;
		unsigned long x, y, c;

		vk_rgba_level_send(id, level, level_width, level_height, from);
		if (level + 1 == levels)
			break;
		/* the next level: each texel the mean of the (up to) four above it */
		next = malloc(next_width * next_height * 4);
		if (!next)
			break;
		for (y = 0; y < next_height; y++)
		{
			unsigned long y0 = y * 2, y1 = y * 2 + 1 < level_height ? y * 2 + 1 : y * 2;

			for (x = 0; x < next_width; x++)
			{
				unsigned long x0 = x * 2, x1 = x * 2 + 1 < level_width ? x * 2 + 1 : x * 2;

				for (c = 0; c < 4; c++)
					next[(y * next_width + x) * 4 + c] = (unsigned char)((from[(y0 * level_width + x0) * 4 + c] +
						from[(y0 * level_width + x1) * 4 + c] + from[(y1 * level_width + x0) * 4 + c] +
						from[(y1 * level_width + x1) * 4 + c] + 2) / 4);
			}
		}
		free(pixels);
		pixels = next;
		from = pixels;
		level_width = next_width;
		level_height = next_height;
	}
	free(pixels);
	return id;
}

void xgpu_vulkan_texture_delete(unsigned int name)
{
	if (vk.ready && name)
		vk_image_free(name);
}

/* ---------- starting

Vulkan is brought up on the window the platform layer made without an OpenGL context (sdl_platform.c): an instance and a
physical device now (host_vk_startup), the device and the swapchain at the first hand-over. FALSE leaves the game to OpenGL
ES (platform_video_reopen_for_gl) */
static BOOL vk_initialize(void)
{
	char line[600];
	const char *driver = config_string("display.vk_driver");
	unsigned long index;

	if (vk.ready)
		return TRUE;
	vk.window = platform_sdl_window();
	host_vk_window = vk.window;
	host_vk_present_marker = config_boolean("debug.vk_present_marker");
	host_vk_self_test = config_boolean("debug.vk_self_test");
	/* A driver archive (display.vk_driver) is code the game runs, and one that crashes would crash every start: a
	note is left while a start with one is under way, and taken back once frames are drawn (vk_present). A start that
	finds the note runs on the phone's own driver and asks whether to keep it (platform_offer_system_driver). */
	if (driver && driver[0])
	{
		FILE *note;

		snprintf(vk.note_path, sizeof(vk.note_path), "%s/vk_start.txt", platform_save_root());
		note = fopen(vk.note_path, "r");
		if (note)
		{
			char last[160] = "";

			if (fgets(last, sizeof(last), note))
				last[strcspn(last, "\r\n")] = 0;
			fclose(note);
			platform_log("vulkan: the last start with the driver %s never got as far as drawing; the phone's own "
				"driver this time", last[0] ? last : driver);
			platform_offer_system_driver(last[0] ? last : driver);
			driver = "";
			vk.note_path[0] = 0;
		}
		else if ((note = fopen(vk.note_path, "w")) != NULL)
		{
			fprintf(note, "%s\n", driver);
			fclose(note);
		}
		else
			vk.note_path[0] = 0;
	}
	if (!host_vk_startup(driver, config_boolean("debug.vk_validation"), line, sizeof(line)))
	{
		platform_log("renderer: %s", line);
		/* (not a crash: the note would blame the driver for what did not start) */
		if (vk.note_path[0])
			unlink(vk.note_path);
		vk.note_path[0] = 0;
		return FALSE;
	}
	platform_log("renderer: %s", line);
	/* (the pipeline cache and the shader cache are written at the exit) */
	atexit(host_vk_exit);
	/* (gl_initialize's: the game memory writes that make textures stale, the attributes' w, and the debug settings) */
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
		device.attributes[index][3] = 1.0f;
	memory_watch_initialize();
	debug_settings.statistics = config_boolean("debug.gpu_stats");
	halo_screen_width();
	vk.ready = TRUE;
	if (config_boolean("debug.vk_self_test"))
	{
		data_self_test();
		shader_self_test();
	}
	return TRUE;
}
