/*
D3D8_VK.C

The Xbox Direct3D 8 device for the Android build's Vulkan renderer
(port/android/VULKAN.md, phase 1). Linked into halo_guest_vk.elf in place of
port/linux/src/d3d8_gl.c, which every other build and the Android GL ES image
keep unchanged.

It is d3d8_gl.c (commit 151bbfd0, the last to change that file) with every
OpenGL call and every host_gl_* import taken out: what drew, cleared or
uploaded keeps its state and returns. What remains is what d3d8_gl.c does that
is not OpenGL: the screen's width and scale, the state the XDK's inline
functions keep, the vertical blank and its thread and callbacks, the reserved
viewport constants, render and texture stage state, transforms, vertex shaders
and their declarations, streams, immediate mode, the surfaces and render
targets as the game sees them. It was cut from d3d8_gl.c the way the Switch's
d3d8_dk.c (commit 25c02a0a) was, which is its reference. Changes to d3d8_gl.c
are not followed automatically.

Nothing is drawn yet: the answers the game reads back that depend on the GPU
are IsBusy false, locks and waits that return at once, and visibility tests
that report 0 samples. Later phases send what d3d8_gl.c does with OpenGL to the
host's Vulkan backend (port/android/host/host_vk.c).
*/
/* ---------- the command stream (vk_commands.h)

Commands are written here over a frame and handed to the host half
(port/android/host/host_vk*.c) at its end, or sooner if the stream fills. The
host reads them during the call and nothing later. */

void host_vk_submit(unsigned int commands, unsigned int size);
/* the shader service (host_vk_shaders.c; the imports pass a 64-bit hash in one register) */
void host_vk_shader_compile(uint32_t stage, uint64_t hash, uint32_t glsl, uint32_t glsl_size);
uint32_t host_vk_shader_find(uint32_t stage, uint64_t hash, uint32_t status_out);

#define STREAM_SIZE (4 * 1024 * 1024)

static uint32_t stream[STREAM_SIZE / 4];
static unsigned long stream_used;

static void stream_flush(void)
{
	if (stream_used)
		host_vk_submit((unsigned int)(uintptr_t)stream, (unsigned int)stream_used);
	stream_used = 0;
}

/* room for a command of size bytes, its header filled in (vk_device.h) */
void *vk_stream_command(uint32_t type, unsigned long size)
{
	struct vk_command_header *header;

	size = (size + 3) & ~3UL;
	if (stream_used + size > STREAM_SIZE)
		stream_flush();
	header = (struct vk_command_header *)((unsigned char *)stream + stream_used);
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
static float target_scale[2] = { 1.0f, 1.0f };

static void target_scale_of(unsigned long width, unsigned long height, float scale[2])
{
	scale[0] = scale[1] = 1.0f;
	if (width == (unsigned long)halo_screen_width() && height == SCREEN_HEIGHT)
	{
		scale[0] = screen_scale[0];
		scale[1] = screen_scale[1];
	}
}

static long target_pixel(float coordinate, int axis)
{
	return (long)floorf(coordinate * target_scale[axis] + 0.5f);
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
	target_scale_of(width, height, scale);
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
		target_scale[0] = color_scale[0];
		target_scale[1] = color_scale[1];
	}
	else
	{
		target_scale[0] = depth_scale[0];
		target_scale[1] = depth_scale[1];
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

/* xbox_textures.c, hud_hires.c and text_hires.c call this after their own GL
calls, which under Vulkan have no context and do nothing */
void xgpu_gl_state_invalidate(void)
{
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

	host_vk_shader_compile(stage, hash, (uint32_t)(uintptr_t)glsl, (uint32_t)strlen(glsl));
	for (attempt = 0; attempt < 500; attempt++)
	{
		handle = host_vk_shader_find(stage, hash, (uint32_t)(uintptr_t)&status);
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
/* ---------- the main menu's line under the version number (menu_functions.c): this renderer, and the driver archive the host
loaded, if it is not the phone's own driver (on a second line) */

uint32_t host_vk_driver_name(uint32_t out, uint32_t size);

char const *d3d8_renderer_description(void)
{
	static char description[64];

	if (!description[0])
	{
		char driver[56];

		driver[0] = 0;
		host_vk_driver_name((uint32_t)(uintptr_t)driver, sizeof(driver));
		if (driver[0])
			snprintf(description, sizeof(description), "Vulkan\r%s", driver);
		else
			snprintf(description, sizeof(description), "Vulkan");
	}
	return description;
}
/* ---------- visibility (occlusion) tests (phase 6, step 6)

The draws between Begin and End are the test's: the host counts the samples they pass (an occlusion query, host_vk_visibility.c)
and keeps the count, in the game's pixels, for the slot End names. The result is asked for through an import that never waits (the
latest count of the slot: from this test, or from an earlier one while the GPU is behind), as d3d8_gl.c's query buffer path gives. */

#define VISIBILITY_TEST_SLOTS 4096

uint32_t host_vk_visibility(uint32_t index);

static BOOL visibility_pending[VISIBILITY_TEST_SLOTS];

void WINAPI D3DDevice_BeginVisibilityTest(void)
{
	if (!device.video_ready || device.visibility_test_active)
		return;
	device.visibility_test_active = TRUE;
	stream_command(VK_COMMAND_VISIBILITY_BEGIN, sizeof(struct vk_command_header));
}

HRESULT WINAPI D3DDevice_EndVisibilityTest(DWORD index)
{
	struct vk_command_visibility_end *command;

	if (!device.video_ready || !device.visibility_test_active)
		return S_OK;
	device.visibility_test_active = FALSE;
	index %= VISIBILITY_TEST_SLOTS;
	if (!index)
		index = 1;
	command = stream_command(VK_COMMAND_VISIBILITY_END, sizeof(*command));
	command->index = (uint32_t)index;
	/* the target's pixels to a game pixel: the count is of the game's pixels (visibility_unscaled in d3d8_gl.c), which the game
	divides by its own test's area (lens flares, rasterizer_lights.c), a split-screen window's or the screen's alike */
	command->area = target_scale[0] * target_scale[1];
	visibility_pending[index] = TRUE;
	return S_OK;
}

HRESULT WINAPI D3DDevice_GetVisibilityTestResult(DWORD index, UINT *result, ULONGLONG *time_stamp)
{
	if (time_stamp)
		*time_stamp = 0;
	index %= VISIBILITY_TEST_SLOTS;
	if (!index)
		index = 1;
	if (!device.video_ready || !visibility_pending[index])
	{
		if (result)
			*result = 0;
		return S_OK;
	}
	if (result)
		*result = host_vk_visibility((uint32_t)index);
	return S_OK;
}
/* ---------- the shaders a draw would use (phase 4)

Nothing is drawn yet (phase 6), but at each draw the device makes the pixel shader key as d3d8_gl.c's prepare_draw
does, without binding anything, and notes it and the vertex shader for the dump (debug.gpu_dump_shaders), which
tools/vk_shader_check.py and the probe's `shaders` step take as their corpus. */

/* size is a multiple of 4 (d3d8_gl.c's hash_words) */
static unsigned long hash_words(const void *data, unsigned long size)
{
	const DWORD *words = data;
	unsigned long hash = 2166136261UL;

	for (size /= 4; size; size--)
		hash = (hash ^ *words++) * 16777619UL;
	return hash;
}

typedef char pixel_shader_key_size_assert[sizeof(struct nv2a_pixel_shader_key) % 4 == 0 ? 1 : -1];

static unsigned long stage_texture_mode(int stage)
{
	return (D3D__RenderState[D3DRS_PSTEXTUREMODES] >> (5 * stage)) & 0x1f;
}

/* what bind_textures in d3d8_gl.c decides for the key: a stage with no texture, or in modes 0, 0x04 and 0x05, has no
sampler (mode 0x11 a 2D one); otherwise the sampler is the texture's own type, and stage 0's coverage_alpha is whether
its bitmap has a high-res meter (hud_hires.h) whose green is its coverage. The cache there finds the override when it
uploads the pixels; here it is asked the same way, without the GL texture (the texture's own decoding failing, which
d3d8_gl.c would see, is not seen) */
static void key_textures(struct nv2a_pixel_shader_key *key)
{
	int stage;

	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		D3DBaseTexture *texture = device.textures[stage];
		unsigned long mode = stage_texture_mode(stage);
		struct xgpu_texture_description description;
		BOOL palettized;

		if (!texture || !texture->Data || mode == 0 || mode == 0x04 || mode == 0x05 || mode == 0x11)
		{
			key->sampler_type[stage] = mode == 0x11 ? _xgpu_sampler_2d : _xgpu_sampler_none;
			continue;
		}
		/* a render target sampled as a texture is a 2D image whatever the texture says (bind_textures in d3d8_gl.c) */
		{
			unsigned long target_width, target_height;

			if (rendered_find(texture->Data, &target_width, &target_height))
			{
				key->sampler_type[stage] = _xgpu_sampler_2d;
				continue;
			}
		}
		xgpu_texture_describe(texture->Format, texture->Size, &description);
		if (stage == 0)
		{
			palettized = ((texture->Format & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT) == 0x0b;
			if (!palettized && !description.cube_map && description.depth == 1)
			{
				long override = hud_hires_override_find((unsigned long)PLATFORM_PHYSICAL_TO_VIRTUAL(texture->Data),
					description.width, description.height, description.levels > 1 ?
					xgpu_texture_level_offset(&description, 1) : xgpu_texture_face_size(&description));

				key->coverage_alpha = override >= 0 && hud_hires_override_coverage(override) != 0;
			}
		}
		key->sampler_type[stage] = description.cube_map ? _xgpu_sampler_cube :
			description.depth > 1 ? _xgpu_sampler_3d : _xgpu_sampler_2d;
	}
}

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
	key_textures(key);
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		key->alpha_kill[stage] = D3D__TextureState[stage][D3DTSS_ALPHAKILL] == D3DTALPHAKILL_ENABLE;
		key->color_sign[stage] = (unsigned char)((D3D__TextureState[stage][D3DTSS_COLORSIGN] >> 28) & 0xf);
	}
	/* (only with the meter's blend: hud_hires.h, nv2a_pixel_shader_key) */
	key->coverage_alpha = key->coverage_alpha && D3D__RenderState[D3DRS_ALPHABLENDENABLE] &&
		D3D__RenderState[D3DRS_SRCBLEND] == D3DBLEND_CONSTANTCOLOR &&
		D3D__RenderState[D3DRS_DESTBLEND] == D3DBLEND_SRCALPHA;
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
	handle = host_vk_shader_find(stage, hash, (uint32_t)(uintptr_t)&status);
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
			host_vk_shader_compile(stage, hash, (uint32_t)(uintptr_t)glsl, (uint32_t)strlen(glsl));
			free(glsl);
			entry->sent++;
		}
	}
	return 0;
}

/* the shaders a draw needs, as d3d8_gl.c's prepare_draw chooses them, asked for from the host (and sent when the host lacks
them), and noted for the dump (debug.gpu_dump_shaders). FALSE if there is no program to draw with. The handles are 0 while a
shader is queued or compiling (the draw is skipped). The key is made without binding anything. */
static BOOL draw_shaders(BOOL immediate, struct nv2a_pixel_shader_key *key, unsigned long *mask_out, uint32_t *vertex,
	uint32_t *pixel)
{
	struct vertex_shader_object *program = current_program();
	const char *folder = dump_folder();
	unsigned long mask;
	uint32_t generator = VK_SHADER_GENERATOR_VERSION;
	uint64_t hash;

	*vertex = *pixel = 0;
	if (!program || !device.vertex_shader || !program->instructions)
		return FALSE;
	pixel_key_make(key);
	mask = immediate ? 0 : device.vertex_shader->packed_mask;
	*mask_out = mask;
	if (folder)
	{
		dump_vertex_shader(folder, program, mask);
		dump_pixel_shader(folder, key);
	}
	if (!device.video_ready)
		return TRUE;
	{
		uint32_t mask32 = (uint32_t)mask;

		hash = vk_hash_mix(vk_hash_init(), &generator, sizeof(generator));
		hash = vk_hash_mix(hash, &program->program_hash, sizeof(program->program_hash));
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
} stats;
static BOOL statistics_on, statistics_read;

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
	long x0 = target_pixel((float)device.viewport.X, 0), y0 = target_pixel((float)device.viewport.Y, 1);
	long x1 = target_pixel((float)(device.viewport.X + device.viewport.Width), 0);
	long y1 = target_pixel((float)(device.viewport.Y + device.viewport.Height), 1);

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

uint32_t host_vk_format_supported(uint32_t format);

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
static void index_extent(const WORD *indices, unsigned long count, unsigned long *minimum, unsigned long *maximum)
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
static WORD *quad_indices(const WORD *indices, unsigned long count, unsigned long *out_count)
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

/* the Vulkan address mode for a Direct3D one (address_mode in d3d8_gl.c) */
static uint32_t address_mode(DWORD mode)
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
	out->address[0] = address_mode(state[D3DTSS_ADDRESSU]);
	out->address[1] = address_mode(state[D3DTSS_ADDRESSV]);
	out->address[2] = address_mode(state[D3DTSS_ADDRESSW]);
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

/* what each stage samples, and the scale a linear texture's coordinates get */
static void textures_make(struct vk_command_draw *draw, const struct nv2a_pixel_shader_key *key, float texture_scale[4][4])
{
	int stage;

	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		D3DBaseTexture *texture = device.textures[stage];
		unsigned long mode = stage_texture_mode_of(stage);
		struct vk_draw_texture *out = &draw->textures[stage];

		texture_scale[stage][0] = texture_scale[stage][1] = texture_scale[stage][2] = texture_scale[stage][3] = 1.0f;
		out->sampler_type = key->sampler_type[stage];
		if (!texture || !texture->Data || mode == 0 || mode == 0x04 || mode == 0x05 || mode == 0x11)
			continue;
		{
			struct xgpu_texture_description description;
			const D3DCOLOR *palette = device.palettes[stage] && device.palettes[stage]->Data ?
				(const D3DCOLOR *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.palettes[stage]->Data) : NULL;
			unsigned long target_width, target_height;
			int kind;
			uint32_t id;

			/* a texture whose data is a render target's samples the target (render-to-texture); linear ones have their
			coordinates scaled by the target's size */
			if (rendered_find(texture->Data, &target_width, &target_height))
			{
				unsigned long levels = 1;

				xgpu_texture_describe(texture->Format, texture->Size, &description);
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
				sampler_state_make(stage, levels > 1, FALSE, &out->sampler);
				continue;
			}
			id = vk_texture_get((const DWORD *)texture, palette, &kind, &description);
			if (!id)
				continue;
			if (description.linear)
			{
				texture_scale[stage][0] = 1.0f / (float)description.width;
				texture_scale[stage][1] = 1.0f / (float)description.height;
			}
			out->kind = VK_TEXTURE_IMAGE;
			out->id = id;
			sampler_state_make(stage, description.levels > 1, description.hires, &out->sampler);
		}
	}
}

/* a draw: prepare_draw and the draw calls of d3d8_gl.c, as one VK_COMMAND_DRAW. index_data is the game's indices (NULL for
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

	if (!device.video_ready)
		return;
	if (!draw_shaders(immediate, &key, &mask, &vertex, &pixel))
	{
		stats.skipped_no_program++;
		return;
	}
	if (!targets_bind(&has_depth))
	{
		stats.skipped_no_target++;
		return;
	}
	if (!vertex || !pixel)
	{
		stats.skipped_shader++;
		return;
	}
	memset(&record, 0, sizeof(record));
	textures_make(&record, &key, texture_scale);
	record.vertex_shader = vertex;
	record.pixel_shader = pixel;
	raster_state_make(&record, targets_told.color.kind != VK_SURFACE_NONE, has_depth);
	record.state.topology = topology_of(type);

	/* the indices: the game's (an indexed draw), or made (a quad list, a line loop) */
	if (index_data)
	{
		index_extent(index_data, count, &minimum, &maximum);
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
		made = quad_indices(index_data, count, &index_count);
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
		stats.skipped_other++;
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
		stats.immediate_draws++;
	else
		stats.draws++;
}

void WINAPI D3DDevice_DrawVertices(D3DPRIMITIVETYPE primitive_type, UINT start_vertex, UINT vertex_count)
{
	if (vertex_count)
		draw_make(primitive_type, vertex_count, FALSE, NULL, start_vertex);
}

void WINAPI D3DDevice_DrawIndexedVertices(D3DPRIMITIVETYPE primitive_type, UINT vertex_count, CONST WORD *index_data)
{
	if (vertex_count && index_data)
		draw_make(primitive_type, vertex_count, FALSE, index_data, 0);
}
void WINAPI D3DDevice_Clear(DWORD count, CONST D3DRECT *rectangles, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
	struct vk_command_clear *command;
	uint32_t clear_flags = 0;
	BOOL has_depth = FALSE;
	DWORD index, kept = 0;

	if (!device.video_ready || !targets_bind(&has_depth))
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
		long x0 = target_pixel((float)device.viewport.X, 0);
		long y0 = target_pixel((float)device.viewport.Y, 1);

		command->rectangles[0][0] = (uint32_t)x0;
		command->rectangles[0][1] = (uint32_t)y0;
		command->rectangles[0][2] = (uint32_t)(target_pixel((float)(device.viewport.X + device.viewport.Width), 0) - x0);
		command->rectangles[0][3] = (uint32_t)(target_pixel((float)(device.viewport.Y + device.viewport.Height), 1) - y0);
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
		x0 = target_pixel((float)(left + UI_OFFSET), 0);
		y0 = target_pixel((float)top, 1);
		command->rectangles[kept][0] = (uint32_t)x0;
		command->rectangles[kept][1] = (uint32_t)y0;
		command->rectangles[kept][2] = (uint32_t)(target_pixel((float)(right + UI_OFFSET), 0) - x0);
		command->rectangles[kept][3] = (uint32_t)(target_pixel((float)bottom, 1) - y0);
		kept++;
	}
	command->rectangle_count = kept;
}
/* ---------- presentation */

void WINAPI D3DDevice_Present(CONST RECT *source_rectangle, CONST RECT *destination_rectangle,
	void *unused, void *unused2)
{
	(void)source_rectangle;
	(void)destination_rectangle;
	(void)unused;
	(void)unused2;
	if (device.video_ready)
	{
		struct vk_command_present *command = stream_command(VK_COMMAND_PRESENT, sizeof(*command));
		float scale[2];

		surface_describe(&device.back_buffer, FALSE, &command->back_buffer, scale);
		stream_flush();
		/* the host starts the next frame with nothing bound, and with no data */
		targets_known = FALSE;
		data_id = 0;
		puts_reset();
		vk_texture_cache_begin_frame();
		stats.presents++;
		if (!statistics_read)
		{
			statistics_read = TRUE;
			statistics_on = config_boolean("debug.gpu_stats");
		}
		if (statistics_on && stats.presents >= 60)
		{
			platform_log("frame %lu: %lu draws, %lu immediate; skipped %lu no program, %lu no target, %lu shader not ready, "
				"%lu textured, %lu other", device.frame, stats.draws / stats.presents, stats.immediate_draws / stats.presents,
				stats.skipped_no_program, stats.skipped_no_target, stats.skipped_shader, stats.skipped_texture,
				stats.skipped_other);
			memset(&stats, 0, sizeof(stats));
		}
	}
	dump_present();
	/* the stand-in window's swap: holds the frame to the display's rate until the
	host's swapchain presents (host_vk_presenting), after which it does */
	platform_video_swap();
	device.frame++;
	platform_pump_events();

	pthread_mutex_lock(&vertical_blank_lock);
	/* the Xbox keeps at most two frames queued behind its 60 Hz display;
	with interpolation, frames come at the real display's rate instead */
	if (halo_interpolation_enabled())
	{
		flip_count++;
	}
	else
	{
		while (pending_flips >= 2)
			pthread_cond_wait(&vertical_blank_condition, &vertical_blank_lock);
		pending_flips++;
	}
	pthread_mutex_unlock(&vertical_blank_lock);
}

HRESULT WINAPI D3DDevice_PersistDisplay(void)
{
	return S_OK;
}
