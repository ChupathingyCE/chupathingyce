/*
D3D8_BGFX.INL

A test backend: the Xbox Direct3D 8 device drawn with bgfx instead of OpenGL,
so that each platform can draw with its own API (Metal on macOS here; Vulkan
and Direct3D are bgfx's elsewhere). display.renderer = "bgfx" chooses it at
start-up, in builds configured with --renderer=bgfx. It is part of
d3d8_gl.c, included at its end, and shares the device's state: what the game
sets through the XDK's inline functions and the entry points above is read at
each draw, as the OpenGL path reads it. OpenGL is never initialized while it
runs (gl_ready stays FALSE), so the OpenGL path's entry points do nothing.

The idea of drawing the Xbox's Direct3D through bgfx comes from Theseus by
Milenko (UIX Desktop), whose desktop renderer does it for the dashboard. The
game cannot use Theseus's precompiled fixed-function shaders, though: its
vertex shaders are Xbox microcode and its pixel shaders register combiner
states, translated at run time (nv2a_vsh.c, nv2a_psh.c). Their GLSL is
rewritten for Vulkan's rules (bg_vulkan_glsl), compiled to SPIR-V with
shaderc, turned into Metal's shading language with SPIRV-Cross, and handed
to bgfx in its shader container. The Metal source is kept on disk by the
GLSL's hash, so a shader is translated once.

So far: the back buffer and its depth buffer (an offscreen frame buffer,
letterboxed into the window at Present), 2D textures (swizzled, linear,
palettized and DXT), every vertex and pixel shader, the render states bgfx
has, and clears. Not yet: render targets other than the back buffer,
cube and volume textures, visibility tests, depth bias, the overlay, the
high-res text and HUD.
*/

#include <SDL3/SDL.h>
#include <bgfx/c99/bgfx.h>
#include <shaderc/shaderc.h>
#include <spirv_cross/spirv_cross_c.h>

SDL_Window *platform_sdl_window(void);

BOOL xgpu_bgfx;

#define BG_SHADER_BUCKETS 1024
#define BG_TEXTURE_BUCKETS 4096
/* views a frame may use; the last is the present's */
#define BG_MAXIMUM_VIEWS 255
/* each staged vertex is all sixteen input registers, four floats each */
#define BG_VERTEX_BYTES (XGPU_VERTEX_ATTRIBUTE_COUNT * 16)

/* ---------- uniforms

The translators' uniforms, each a bgfx uniform of the same name. bgfx's
Metal renderer knows only vec4 (and matrix) members, so a float becomes a
vec4 named <name>_v4, whose x the shader reads (bg_vulkan_glsl). */

static struct draw_uniforms bg_uniforms;

static const struct
{
	const char *name;
	const float *source;
	unsigned short count;
	unsigned short components;
} bg_uniform_sources[] =
{
	{ "c", &device.constants[0][0], XGPU_VERTEX_CONSTANT_COUNT, 4 },
	{ "viewport_scale", bg_uniforms.viewport_scale, 1, 4 },
	{ "viewport_offset", bg_uniforms.viewport_offset, 1, 4 },
	{ "point_size_v4", &bg_uniforms.point_size, 1, 1 },
	{ "screen_offset_v4", &bg_uniforms.screen_offset, 1, 1 },
	{ "ps_c0", &bg_uniforms.ps_c0[0][0], 8, 4 },
	{ "ps_c1", &bg_uniforms.ps_c1[0][0], 8, 4 },
	{ "ps_final_c0", bg_uniforms.ps_final_c0, 1, 4 },
	{ "ps_final_c1", bg_uniforms.ps_final_c1, 1, 4 },
	{ "fog_color", bg_uniforms.fog_color, 1, 4 },
	{ "fog_parameters", bg_uniforms.fog_parameters, 1, 4 },
	{ "alpha_reference_v4", &bg_uniforms.alpha_reference, 1, 1 },
	{ "bump_matrix", &bg_uniforms.bump_matrix[0][0], 4, 4 },
	{ "bump_luminance", &bg_uniforms.bump_luminance[0][0], 4, 4 },
	{ "texture_scale", &bg_uniforms.texture_scale[0][0], 4, 4 },
	{ "texture_lod_bias", bg_uniforms.texture_lod_bias, 1, 4 },
};
#define BG_UNIFORM_COUNT (sizeof(bg_uniform_sources) / sizeof(bg_uniform_sources[0]))

/* the varyings, at the same locations in both stages */
static const char *const bg_varyings[] =
{
	"xD0", "xD1", "xB0", "xB1", "xT0", "xT1", "xT2", "xT3", "xFog", "xWorldNormal", "xWorldPosition",
};

/* the input registers v0 to v15 as bgfx's attributes, which its Metal
renderer finds by these names */
static const char *const bg_attribute_names[XGPU_VERTEX_ATTRIBUTE_COUNT] =
{
	"a_position", "a_normal", "a_tangent", "a_bitangent", "a_color0", "a_color1", "a_color2", "a_color3",
	"a_indices", "a_weight", "a_texcoord0", "a_texcoord1", "a_texcoord2", "a_texcoord3", "a_texcoord4", "a_texcoord5",
};

struct bg_shader
{
	struct bg_shader *next;
	Uint64 hash;
	bgfx_shader_handle_t shader;
	BOOL failed;
};

struct bg_fragment
{
	struct bg_fragment *next;
	unsigned long hash;
	struct nv2a_pixel_shader_key key;
	struct bg_shader *shader;
};

struct bg_program
{
	struct bg_program *next;
	struct bg_shader *vertex, *fragment;
	bgfx_program_handle_t program;
};

struct bg_texture
{
	struct bg_texture *next;
	DWORD data, format_word, size_word;
	unsigned long palette_hash;
	unsigned long address, size, generation;
	struct xgpu_texture_description description;
	bgfx_texture_handle_t texture;
	BOOL created, mipmapped, failed;
};

static struct
{
	BOOL ready;
	SDL_Window *window;
	shaderc_compiler_t compiler;
	shaderc_compile_options_t options;
	bgfx_vertex_layout_t layout;
	bgfx_uniform_handle_t uniforms[BG_UNIFORM_COUNT];
	bgfx_uniform_handle_t samplers[4];

	/* the back buffer and its depth buffer, at the screen's pixels */
	bgfx_frame_buffer_handle_t back_buffer;
	bgfx_texture_handle_t color, depth;
	unsigned long width, height;
	/* the window's pixels, as bgfx's back buffer was last reset to */
	int window_width, window_height;
	unsigned long reset_flags;

	/* the frame's views: the one being drawn, its rectangle, and whether it
	has drawn or cleared yet */
	unsigned short view;
	unsigned short view_rect[4];
	BOOL view_used;

	bgfx_texture_handle_t blank, blank_cube;
	struct bg_shader *shaders[BG_SHADER_BUCKETS];
	struct bg_fragment *fragments[BG_SHADER_BUCKETS];
	struct bg_program *programs[BG_SHADER_BUCKETS];
	struct bg_texture *textures[BG_TEXTURE_BUCKETS];
	bgfx_program_handle_t blit;

	unsigned long vsync_read_at;
	unsigned long drawn, skipped;
} bg;

static struct bg_shader bg_failed_shader;

static BOOL bg_active(void)
{
	return bg.ready;
}

static void bg_log_once(const char *what)
{
	static const char *logged[32];
	int index;

	for (index = 0; index < 32 && logged[index]; index++)
	{
		if (logged[index] == what)
			return;
	}
	if (index < 32)
		logged[index] = what;
	platform_log("bgfx: %s (not drawn yet)", what);
}

static Uint64 bg_hash(const void *data, unsigned long size, Uint64 hash)
{
	const unsigned char *bytes = data;

	while (size--)
		hash = (hash ^ *bytes++) * 1099511628211ULL;
	return hash;
}

/* ---------- GLSL for Vulkan

The translators write GLSL for OpenGL: loose uniforms, varyings matched by
name. SPIR-V wants the uniforms in a block and the varyings at locations;
bgfx wants the vertex inputs named as its attributes and the block's members
as vec4s. Each line the translators write declares one thing, so the rewrite
goes line by line. The clip-space fix for OpenGL without glClipControl comes
out: Metal's clip space is Direct3D's, as the Xbox's is. */

static void bg_replace_all(struct xgpu_text *text, const char *from, const char *to)
{
	struct xgpu_text out = { 0 };
	const char *cursor = text->buffer, *found;
	unsigned long length = (unsigned long)strlen(from);

	if (!cursor)
		return;
	while ((found = strstr(cursor, from)) != NULL)
	{
		/* (whole identifiers only) */
		char after = found[length];

		if ((after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z') || (after >= '0' && after <= '9') ||
			after == '_')
		{
			xgpu_text_append(&out, "%.*s", (int)(found - cursor + length), cursor);
		}
		else
		{
			xgpu_text_append(&out, "%.*s%s", (int)(found - cursor), cursor, to);
		}
		cursor = found + length;
	}
	xgpu_text_append(&out, "%s", cursor);
	free(text->buffer);
	*text = out;
}

static char *bg_vulkan_glsl(const char *source)
{
	struct xgpu_text block = { 0 }, defines = { 0 }, body = { 0 }, all = { 0 };
	const char *line = source;
	BOOL members = FALSE;
	unsigned long index;

	xgpu_text_append(&block, "#version 450\n");
	xgpu_text_append(&block, "layout(set = 0, binding = 8, std140) uniform Uniforms\n{\n");
	while (*line)
	{
		const char *end = strchr(line, '\n');
		unsigned long length = end ? (unsigned long)(end - line) : (unsigned long)strlen(line);
		char text[512], type[64], name[64];

		if (length >= sizeof(text))
		{
			xgpu_text_append(&body, "%.*s\n", (int)length, line);
			line += length + (end ? 1 : 0);
			continue;
		}
		memcpy(text, line, length);
		text[length] = 0;
		line += length + (end ? 1 : 0);
		if (strncmp(text, "#version", 8) == 0 || strncmp(text, "precision ", 10) == 0 ||
			strcmp(text, "invariant gl_Position;") == 0 ||
			strcmp(text, "\tgl_Position.y = -gl_Position.y;") == 0 ||
			strcmp(text, "\tgl_Position.z = 2.0 * gl_Position.z - gl_Position.w;") == 0)
			continue;
		if (sscanf(text, "uniform %63s %63[A-Za-z0-9_]", type, name) == 2)
		{
			if (strncmp(type, "sampler", 7) == 0)
			{
				/* tex0 to tex3, at their stages */
				xgpu_text_append(&body, "layout(set = 0, binding = %c) %s\n", name[3], text);
				continue;
			}
			if (strcmp(type, "float") == 0)
			{
				xgpu_text_append(&block, "\tvec4 %s_v4;\n", name);
				xgpu_text_append(&defines, "#define %s %s_v4.x\n", name, name);
			}
			else
			{
				xgpu_text_append(&block, "\t%s\n", text + 8);
			}
			members = TRUE;
			continue;
		}
		if ((strncmp(text, "in ", 3) == 0 || strncmp(text, "out ", 4) == 0) &&
			sscanf(text, "%*s %63s %63[A-Za-z0-9_];", type, name) == 2)
		{
			for (index = 0; index < sizeof(bg_varyings) / sizeof(bg_varyings[0]); index++)
			{
				if (strcmp(name, bg_varyings[index]) == 0)
					break;
			}
			if (index < sizeof(bg_varyings) / sizeof(bg_varyings[0]))
			{
				xgpu_text_append(&body, "layout(location = %lu) %s\n", index, text);
				continue;
			}
		}
		xgpu_text_append(&body, "%s\n", text);
	}
	/* (a block may not be empty) */
	if (!members)
		xgpu_text_append(&block, "\tvec4 unused_v4;\n");
	xgpu_text_append(&all, "%s};\n%s%s", block.buffer, defines.buffer ? defines.buffer : "",
		body.buffer ? body.buffer : "");
	free(block.buffer);
	free(defines.buffer);
	free(body.buffer);
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
	{
		char from[16];

		snprintf(from, sizeof(from), "v%lu_in", index);
		bg_replace_all(&all, from, bg_attribute_names[index]);
	}
	return all.buffer;
}

/* ---------- shader compilation: GLSL, SPIR-V, Metal's language, bgfx's container */

static void bg_cache_path(char *path, unsigned long size, Uint64 hash)
{
	const char *root = platform_save_root();

	if (!root || !*root)
	{
		*path = 0;
		return;
	}
	snprintf(path, size, "%s/bgfx_shaders/%016llx.metal", root, (unsigned long long)hash);
}

/* Metal's shading language for the SPIR-V: the uniform block is bgfx's
_mtl_u in buffer 0, each sampler at its stage, the entry point bgfx's */
static char *bg_msl(const shaderc_compilation_result_t result, BOOL fragment, const char **error)
{
	spvc_context context = NULL;
	spvc_parsed_ir ir;
	spvc_compiler compiler;
	spvc_compiler_options options;
	spvc_resources resources;
	const spvc_reflected_resource *list;
	size_t count, index;
	const char *source = NULL;
	char *copy = NULL;
	SpvExecutionModel model = fragment ? SpvExecutionModelFragment : SpvExecutionModelVertex;

	*error = "SPIRV-Cross";
	if (spvc_context_create(&context) != SPVC_SUCCESS)
		return NULL;
	if (spvc_context_parse_spirv(context, (const SpvId *)shaderc_result_get_bytes(result),
			shaderc_result_get_length(result) / sizeof(SpvId), &ir) != SPVC_SUCCESS ||
		spvc_context_create_compiler(context, SPVC_BACKEND_MSL, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler) !=
			SPVC_SUCCESS)
		goto done;
	spvc_compiler_create_compiler_options(compiler, &options);
	spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_MSL_VERSION, SPVC_MAKE_MSL_VERSION(2, 1, 0));
	spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_MSL_PLATFORM, SPVC_MSL_PLATFORM_MACOS);
	spvc_compiler_install_compiler_options(compiler, options);
	spvc_compiler_rename_entry_point(compiler, "main", "xlatMtlMain", model);
	spvc_compiler_create_shader_resources(compiler, &resources);
	spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, &list, &count);
	for (index = 0; index < count; index++)
	{
		spvc_msl_resource_binding binding;

		spvc_compiler_set_name(compiler, list[index].id, "_mtl_u");
		spvc_msl_resource_binding_init(&binding);
		binding.stage = model;
		binding.desc_set = spvc_compiler_get_decoration(compiler, list[index].id, SpvDecorationDescriptorSet);
		binding.binding = spvc_compiler_get_decoration(compiler, list[index].id, SpvDecorationBinding);
		binding.msl_buffer = 0;
		spvc_compiler_msl_add_resource_binding(compiler, &binding);
	}
	spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, &list, &count);
	for (index = 0; index < count; index++)
	{
		spvc_msl_resource_binding binding;
		unsigned stage = spvc_compiler_get_decoration(compiler, list[index].id, SpvDecorationBinding);

		spvc_msl_resource_binding_init(&binding);
		binding.stage = model;
		binding.desc_set = 0;
		binding.binding = stage;
		binding.msl_texture = stage;
		binding.msl_sampler = stage;
		spvc_compiler_msl_add_resource_binding(compiler, &binding);
	}
	if (spvc_compiler_compile(compiler, &source) == SPVC_SUCCESS && source)
		copy = strdup(source);
	else
		*error = spvc_context_get_last_error_string(context);
done:
	if (!copy)
		platform_log("bgfx: SPIRV-Cross: %s", spvc_context_get_last_error_string(context));
	spvc_context_destroy(context);
	return copy;
}

/* bgfx's shader container for Metal source: its uniforms are found by
reflection, so the table is empty, and both hashes are 0 so that any vertex
shader goes with any pixel shader */
static bgfx_shader_handle_t bg_container(const char *msl, BOOL fragment)
{
	unsigned long length = (unsigned long)strlen(msl);
	unsigned char *bytes = malloc(length + 32);
	unsigned long offset = 0;
	Uint32 value;
	Uint16 count = 0;
	bgfx_shader_handle_t handle;

	bytes[offset++] = fragment ? 'F' : 'V';
	bytes[offset++] = 'S';
	bytes[offset++] = 'H';
	bytes[offset++] = 11;
	value = 0;
	memcpy(bytes + offset, &value, 4), offset += 4; /* hash in */
	memcpy(bytes + offset, &value, 4), offset += 4; /* hash out */
	memcpy(bytes + offset, &count, 2), offset += 2;
	value = (Uint32)length;
	memcpy(bytes + offset, &value, 4), offset += 4;
	memcpy(bytes + offset, msl, length + 1), offset += length + 1;
	handle = bgfx_create_shader(bgfx_copy(bytes, (uint32_t)offset));
	free(bytes);
	return handle;
}

static struct bg_shader *bg_shader_failed(Uint64 hash, const char *what, const char *why)
{
	struct bg_shader *entry = calloc(1, sizeof(*entry));

	platform_log("bgfx: the %s shader %016llx fails: %s", what, (unsigned long long)hash, why);
	if (entry)
	{
		entry->hash = hash;
		entry->failed = TRUE;
		entry->next = bg.shaders[hash % BG_SHADER_BUCKETS];
		bg.shaders[hash % BG_SHADER_BUCKETS] = entry;
	}
	return &bg_failed_shader;
}

/* a shader of the translators' GLSL */
static struct bg_shader *bg_shader_get(const char *glsl, BOOL fragment, const char *what)
{
	struct bg_shader *shader;
	char *source, *msl = NULL;
	char path[1024];
	size_t msl_size = 0;
	Uint64 hash;

	source = bg_vulkan_glsl(glsl);
	if (!source)
		return &bg_failed_shader;
	hash = bg_hash(source, strlen(source), fragment ? 14695981039346656037ULL : 1469598103934665603ULL);
	for (shader = bg.shaders[hash % BG_SHADER_BUCKETS]; shader; shader = shader->next)
	{
		if (shader->hash == hash)
		{
			free(source);
			return shader->failed ? &bg_failed_shader : shader;
		}
	}
	bg_cache_path(path, sizeof(path), hash);
	if (*path)
		msl = SDL_LoadFile(path, &msl_size);
	if (!msl)
	{
		shaderc_compilation_result_t result = shaderc_compile_into_spv(bg.compiler, source, strlen(source),
			fragment ? shaderc_fragment_shader : shaderc_vertex_shader, what, "main", bg.options);
		const char *error = NULL;

		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success)
		{
			struct bg_shader *failed = bg_shader_failed(hash, what, shaderc_result_get_error_message(result));

			if (config_boolean("debug.gpu_stats"))
				platform_log("%s", source);
			shaderc_result_release(result);
			free(source);
			return failed;
		}
		msl = bg_msl(result, fragment, &error);
		shaderc_result_release(result);
		if (!msl)
		{
			free(source);
			return bg_shader_failed(hash, what, error ? error : "SPIRV-Cross");
		}
		if (*path)
		{
			char directory[1024];

			snprintf(directory, sizeof(directory), "%s/bgfx_shaders", platform_save_root());
			SDL_CreateDirectory(directory);
			SDL_SaveFile(path, msl, strlen(msl));
		}
	}
	free(source);
	shader = calloc(1, sizeof(*shader));
	shader->hash = hash;
	shader->shader = bg_container(msl, fragment);
	if (msl_size)
		SDL_free(msl);
	else
		free(msl);
	if (!BGFX_HANDLE_IS_VALID(shader->shader))
	{
		free(shader);
		return bg_shader_failed(hash, what, "bgfx refused it");
	}
	shader->next = bg.shaders[hash % BG_SHADER_BUCKETS];
	bg.shaders[hash % BG_SHADER_BUCKETS] = shader;
	return shader;
}

static struct bg_shader *bg_vertex_shader(struct vertex_shader_object *program, BOOL immediate)
{
	void **slot = &program->bg_shader[immediate ? 1 : 0];

	if (!*slot)
	{
		/* (packed normals are unpacked as they are staged: bg_vertex_element) */
		char *glsl = nv2a_vertex_shader_to_glsl(program->instructions, program->instruction_count, 0, NULL);

		*slot = bg_shader_get(glsl, FALSE, "vertex");
		free(glsl);
	}
	return *slot == &bg_failed_shader ? NULL : *slot;
}

static struct bg_shader *bg_fragment_shader(const struct nv2a_pixel_shader_key *key)
{
	unsigned long hash = (unsigned long)bg_hash(key, sizeof(*key), 14695981039346656037ULL);
	struct bg_fragment **bucket = &bg.fragments[hash % BG_SHADER_BUCKETS], *entry;
	char *glsl;

	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->hash == hash && !memcmp(&entry->key, key, sizeof(*key)))
			return entry->shader == &bg_failed_shader ? NULL : entry->shader;
	}
	glsl = nv2a_pixel_shader_to_glsl(key);
	entry = calloc(1, sizeof(*entry));
	entry->hash = hash;
	entry->key = *key;
	entry->shader = bg_shader_get(glsl, TRUE, "pixel");
	free(glsl);
	entry->next = *bucket;
	*bucket = entry;
	return entry->shader == &bg_failed_shader ? NULL : entry->shader;
}

static bgfx_program_handle_t bg_program_get(struct bg_shader *vertex, struct bg_shader *fragment)
{
	unsigned long hash = (unsigned long)(((uintptr_t)vertex >> 4) ^ ((uintptr_t)fragment >> 2) * 31);
	struct bg_program **bucket = &bg.programs[hash % BG_SHADER_BUCKETS], *entry;

	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->vertex == vertex && entry->fragment == fragment)
			return entry->program;
	}
	entry = calloc(1, sizeof(*entry));
	entry->vertex = vertex;
	entry->fragment = fragment;
	entry->program = bgfx_create_program(vertex->shader, fragment->shader, false);
	entry->next = *bucket;
	*bucket = entry;
	return entry->program;
}

/* ---------- state (Xbox render states hold OpenGL's enumerants) */

/* GL_NEVER (0x200) to GL_ALWAYS (0x207) as bgfx's test (1 less ... 8 always) */
static unsigned bg_compare(DWORD function)
{
	static const unsigned char tests[8] = { 7, 1, 3, 2, 5, 6, 4, 8 };

	return function >= 0x200 && function <= 0x207 ? tests[function - 0x200] : 8;
}

static unsigned bg_stencil_operation(DWORD operation)
{
	switch (operation)
	{
	case 0x1e00: return 1; /* keep */
	case 0x1e01: return 2; /* replace */
	case 0x1e02: return 4; /* increment, clamped */
	case 0x1e03: return 6; /* decrement, clamped */
	case 0x150a: return 7; /* invert */
	case 0x8507: return 3; /* increment, wrapped */
	case 0x8508: return 5; /* decrement, wrapped */
	default: return 0;
	}
}

static uint64_t bg_blend_factor(DWORD factor)
{
	switch (factor)
	{
	case 0: return BGFX_STATE_BLEND_ZERO;
	case 1: return BGFX_STATE_BLEND_ONE;
	case 0x300: return BGFX_STATE_BLEND_SRC_COLOR;
	case 0x301: return BGFX_STATE_BLEND_INV_SRC_COLOR;
	case 0x302: return BGFX_STATE_BLEND_SRC_ALPHA;
	case 0x303: return BGFX_STATE_BLEND_INV_SRC_ALPHA;
	case 0x304: return BGFX_STATE_BLEND_DST_ALPHA;
	case 0x305: return BGFX_STATE_BLEND_INV_DST_ALPHA;
	case 0x306: return BGFX_STATE_BLEND_DST_COLOR;
	case 0x307: return BGFX_STATE_BLEND_INV_DST_COLOR;
	case 0x308: return BGFX_STATE_BLEND_SRC_ALPHA_SAT;
	/* (bgfx has no constant alpha: the constant color stands in) */
	case 0x8001: case 0x8003: return BGFX_STATE_BLEND_FACTOR;
	case 0x8002: case 0x8004: return BGFX_STATE_BLEND_INV_FACTOR;
	default: return BGFX_STATE_BLEND_ONE;
	}
}

static uint64_t bg_blend_operation(DWORD operation)
{
	switch (operation)
	{
	case D3DBLENDOP_SUBTRACT: return BGFX_STATE_BLEND_EQUATION_SUB;
	case D3DBLENDOP_REVSUBTRACT:
	case D3DBLENDOP_REVSUBTRACTSIGNED: return BGFX_STATE_BLEND_EQUATION_REVSUB;
	case D3DBLENDOP_MIN: return BGFX_STATE_BLEND_EQUATION_MIN;
	case D3DBLENDOP_MAX: return BGFX_STATE_BLEND_EQUATION_MAX;
	default: return BGFX_STATE_BLEND_EQUATION_ADD;
	}
}

/* the draw's state from the render states (apply_raster_state's), and its
stencil; the blend constant in *rgba */
static uint64_t bg_state(BOOL has_depth, uint32_t *stencil, uint32_t *rgba)
{
	DWORD *rs = D3D__RenderState;
	DWORD write = rs[D3DRS_COLORWRITEENABLE];
	uint64_t state = 0;

	if (write & D3DCOLORWRITEENABLE_RED)
		state |= BGFX_STATE_WRITE_R;
	if (write & D3DCOLORWRITEENABLE_GREEN)
		state |= BGFX_STATE_WRITE_G;
	if (write & D3DCOLORWRITEENABLE_BLUE)
		state |= BGFX_STATE_WRITE_B;
	if (write & D3DCOLORWRITEENABLE_ALPHA)
		state |= BGFX_STATE_WRITE_A;
	if (has_depth && rs[D3DRS_ZENABLE])
	{
		state |= (uint64_t)bg_compare(rs[D3DRS_ZFUNC]) << BGFX_STATE_DEPTH_TEST_SHIFT;
		if (rs[D3DRS_ZWRITEENABLE])
			state |= BGFX_STATE_WRITE_Z;
	}
	*stencil = BGFX_STENCIL_NONE;
	if (has_depth && rs[D3DRS_STENCILENABLE])
	{
		*stencil = ((uint32_t)bg_compare(rs[D3DRS_STENCILFUNC]) << BGFX_STENCIL_TEST_SHIFT) |
			BGFX_STENCIL_FUNC_REF(rs[D3DRS_STENCILREF] & 0xff) | BGFX_STENCIL_FUNC_RMASK(rs[D3DRS_STENCILMASK] & 0xff) |
			((uint32_t)bg_stencil_operation(rs[D3DRS_STENCILFAIL]) << BGFX_STENCIL_OP_FAIL_S_SHIFT) |
			((uint32_t)bg_stencil_operation(rs[D3DRS_STENCILZFAIL]) << BGFX_STENCIL_OP_FAIL_Z_SHIFT) |
			((uint32_t)bg_stencil_operation(rs[D3DRS_STENCILPASS]) << BGFX_STENCIL_OP_PASS_Z_SHIFT);
		if ((rs[D3DRS_STENCILWRITEMASK] & 0xff) != 0xff)
			bg_log_once("stencil write masks (bgfx writes all eight bits)");
	}
	if (rs[D3DRS_ALPHABLENDENABLE])
	{
		uint64_t source = bg_blend_factor(rs[D3DRS_SRCBLEND]);
		uint64_t destination = bg_blend_factor(rs[D3DRS_DESTBLEND]);

		state |= BGFX_STATE_BLEND_FUNC(source, destination) | BGFX_STATE_BLEND_EQUATION(bg_blend_operation(rs[D3DRS_BLENDOP]));
	}
	/* (D3DCOLOR's ARGB as bgfx's RGBA) */
	*rgba = (uint32_t)((rs[D3DRS_BLENDCOLOR] << 8) | (rs[D3DRS_BLENDCOLOR] >> 24));
	/* the cull mode names the winding to discard (GL_CW, GL_CCW), in the
	target's rows from the top, as Metal's are */
	if (rs[D3DRS_CULLMODE] == 0x900)
		state |= BGFX_STATE_CULL_CW;
	else if (rs[D3DRS_CULLMODE] == 0x901)
		state |= BGFX_STATE_CULL_CCW;
	if (rs[D3DRS_SOLIDOFFSETENABLE])
		bg_log_once("depth bias (bgfx has none)");
	if (rs[D3DRS_FILLMODE] == D3DFILL_WIREFRAME)
		bg_log_once("wireframe");
	return state;
}

/* ---------- textures */

static unsigned long bg_palette_hash(const D3DCOLOR *palette)
{
	return palette ? (unsigned long)bg_hash(palette, 256 * sizeof(D3DCOLOR), 14695981039346656037ULL) | 1 : 0;
}

static unsigned long bg_level_dimension(unsigned long base, unsigned long level)
{
	unsigned long value = base >> level;

	return value ? value : 1;
}

static void bg_texture_upload(struct bg_texture *entry, const unsigned char *base, const D3DCOLOR *palette)
{
	struct xgpu_texture_description *description = &entry->description;
	int dxt = description->compressed ? xgpu_texture_dxt(description) : 0;
	unsigned long levels = description->levels ? description->levels : 1, level, full = 1, size, face, provided;

	if (description->compressed && !dxt)
	{
		bg_log_once("compressed textures other than DXT");
		entry->failed = TRUE;
		return;
	}
	/* bgfx's mipmapped textures have every level down to 1x1 (it has no
	maximum level); the levels a texture lacks are made from its last */
	for (size = description->width > description->height ? description->width : description->height; size > 1; size >>= 1)
		full++;
	provided = levels;
	if (levels > 1)
		levels = full;
	if (!entry->created)
	{
		bgfx_texture_format_t format = dxt == 1 ? BGFX_TEXTURE_FORMAT_BC1 : dxt == 3 ? BGFX_TEXTURE_FORMAT_BC2 :
			dxt == 5 ? BGFX_TEXTURE_FORMAT_BC3 : BGFX_TEXTURE_FORMAT_BGRA8;

		entry->texture = description->cube_map ?
			bgfx_create_texture_cube((uint16_t)description->width, levels > 1, 1, format, BGFX_TEXTURE_NONE, NULL, 0) :
			bgfx_create_texture_2d((uint16_t)description->width, (uint16_t)description->height, levels > 1, 1, format,
				BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE, NULL, 0);
		if (!BGFX_HANDLE_IS_VALID(entry->texture))
		{
			platform_log("bgfx: a %lux%lu texture cannot be made", description->width, description->height);
			entry->failed = TRUE;
			return;
		}
		entry->created = TRUE;
		entry->mipmapped = levels > 1;
	}
	for (face = 0; face < (description->cube_map ? 6UL : 1UL); face++)
	for (level = 0; level < (entry->mipmapped ? levels : 1); level++)
	{
		unsigned long width = bg_level_dimension(description->width, level);
		unsigned long height = bg_level_dimension(description->height, level);
		unsigned long from = level < provided ? level : provided - 1;
		const unsigned char *source = base + face * xgpu_texture_face_size(description) +
			xgpu_texture_level_offset(description, from);
		const bgfx_memory_t *memory;

		/* (Halo PC's cube map: its levels one after another, each the six
		faces', the second and third swapped; xbox_textures.c's upload) */
		if (description->pc_layout && description->cube_map)
			source = base + xgpu_texture_level_offset(description, from) * 6 +
				(face == 1 ? 2 : face == 2 ? 1 : face) * xgpu_texture_level_bytes(description, from);

		if (dxt)
		{
			/* (a missing level: the last one's first blocks, as many as it has) */
			unsigned long blocks = ((width + 3) / 4) * ((height + 3) / 4) * (dxt == 1 ? 8 : 16);
			unsigned long available = xgpu_texture_level_bytes(description, from);

			memory = bgfx_copy(source, (uint32_t)(level == from ? available : blocks < available ? blocks : available));
		}
		else if (level == from)
		{
			memory = bgfx_alloc((uint32_t)(width * height * 4));
			xgpu_texture_decode_level(description, level, source, palette, (unsigned long *)memory->data);
		}
		else
		{
			/* (a missing level: the last one, point sampled down) */
			unsigned long from_width = bg_level_dimension(description->width, from);
			unsigned long from_height = bg_level_dimension(description->height, from);
			unsigned long *decoded = malloc(from_width * from_height * 4), x, y;

			memory = bgfx_alloc((uint32_t)(width * height * 4));
			if (decoded && xgpu_texture_decode_level(description, from, source, palette, decoded))
			{
				for (y = 0; y < height; y++)
				{
					for (x = 0; x < width; x++)
						((unsigned int *)memory->data)[y * width + x] =
							(unsigned int)decoded[(y * from_height / height) * from_width + x * from_width / width];
				}
			}
			else
			{
				memset(memory->data, 0, width * height * 4);
			}
			free(decoded);
		}
		if (description->cube_map)
			bgfx_update_texture_cube(entry->texture, 0, (uint8_t)face, (uint8_t)level, 0, 0, (uint16_t)width,
				(uint16_t)height, memory, UINT16_MAX);
		else
			bgfx_update_texture_2d(entry->texture, 0, (uint8_t)level, 0, 0, (uint16_t)width, (uint16_t)height, memory,
				UINT16_MAX);
	}
}

/* a stage's texture, uploaded again when its pages have been written;
FALSE if it cannot be drawn yet */
static BOOL bg_texture_get(const DWORD *resource, const D3DCOLOR *palette, struct xgpu_texture_description *description,
	bgfx_texture_handle_t *handle, BOOL *mipmapped)
{
	DWORD data = resource[1], format_word = resource[3], size_word = resource[4];
	unsigned long hash = bg_palette_hash(palette);
	unsigned long bucket = (unsigned long)(bg_hash(&data, sizeof(data), bg_hash(&format_word, sizeof(format_word),
		bg_hash(&size_word, sizeof(size_word), 14695981039346656037ULL))) % BG_TEXTURE_BUCKETS);
	struct bg_texture *entry;
	unsigned long generation;

	for (entry = bg.textures[bucket]; entry; entry = entry->next)
	{
		if (entry->data == data && entry->format_word == format_word && entry->size_word == size_word &&
			entry->palette_hash == hash)
			break;
	}
	if (!entry)
	{
		entry = calloc(1, sizeof(*entry));
		entry->data = data;
		entry->format_word = format_word;
		entry->size_word = size_word;
		entry->palette_hash = hash;
		xgpu_texture_describe(format_word, size_word, &entry->description);
#ifdef HALO_64BIT
		entry->address = (unsigned int)data | PLATFORM_CONTIGUOUS_BASE; /* an Xbox address */
#else
		entry->address = (unsigned long)PLATFORM_PHYSICAL_TO_VIRTUAL(data);
#endif
		entry->size = xgpu_texture_face_size(&entry->description) * (entry->description.cube_map ? 6 : 1);
		if (entry->description.depth > 1)
		{
			bg_log_once("volume textures");
			entry->failed = TRUE;
		}
		entry->next = bg.textures[bucket];
		bg.textures[bucket] = entry;
	}
	*description = entry->description;
	if (entry->failed || !entry->size)
		return FALSE;
	generation = memory_watch_generation(entry->address, entry->size);
	if (!entry->generation || generation > entry->generation)
	{
		entry->description.pc_layout = (resource[0] & D3DCOMMON_PORT_PC_LAYOUT) != 0;
		entry->description.pc_meter = (resource[0] & D3DCOMMON_PORT_PC_METER) != 0;
		entry->description.pc_multipurpose = (resource[0] & D3DCOMMON_PORT_PC_MULTIPURPOSE) != 0;
		memory_watch_protect(entry->address, entry->size);
		entry->generation = memory_watch_generation(entry->address, entry->size);
		if (!entry->generation)
			entry->generation = 1;
		if (!platform_is_contiguous(xbox_pointer(entry->address)) ||
			!platform_is_contiguous(xbox_pointer(entry->address + entry->size - 1)))
			return FALSE;
		bg_texture_upload(entry, (const unsigned char *)xbox_pointer(entry->address), palette);
		*description = entry->description;
	}
	if (entry->failed)
		return FALSE;
	*handle = entry->texture;
	*mipmapped = entry->mipmapped;
	return TRUE;
}

static uint32_t bg_sampler_flags(int stage, BOOL mipmapped)
{
	DWORD *state = D3D__TextureState[stage];
	uint32_t flags = 0;
	int axis;

	if (state[D3DTSS_MINFILTER] == D3DTEXF_POINT)
		flags |= BGFX_SAMPLER_MIN_POINT;
	else if (state[D3DTSS_MINFILTER] == D3DTEXF_ANISOTROPIC && state[D3DTSS_MAXANISOTROPY] > 1)
		flags |= BGFX_SAMPLER_MIN_ANISOTROPIC;
	if (state[D3DTSS_MAGFILTER] == D3DTEXF_POINT)
		flags |= BGFX_SAMPLER_MAG_POINT;
	if (!mipmapped || state[D3DTSS_MIPFILTER] != D3DTEXF_LINEAR)
		flags |= BGFX_SAMPLER_MIP_POINT;
	for (axis = 0; axis < 3; axis++)
	{
		DWORD mode = state[axis == 0 ? D3DTSS_ADDRESSU : axis == 1 ? D3DTSS_ADDRESSV : D3DTSS_ADDRESSW];
		/* (border: the edge's, for now) */
		uint32_t address = mode == D3DTADDRESS_MIRROR ? BGFX_SAMPLER_U_MIRROR :
			mode == D3DTADDRESS_WRAP ? 0 : BGFX_SAMPLER_U_CLAMP;

		flags |= address << (axis * 2);
	}
	return flags;
}

/* the stages' textures, and the pixel shader key's part of them
(bind_textures') */
static void bg_bind_textures(struct nv2a_pixel_shader_key *key, float texture_scale[4][4])
{
	int stage;

	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		D3DBaseTexture *texture = device.textures[stage];
		unsigned long mode = stage_texture_mode(stage);
		struct xgpu_texture_description description;
		bgfx_texture_handle_t handle = bg.blank;
		BOOL mipmapped = FALSE;

		texture_scale[stage][0] = texture_scale[stage][1] = 1.0f;
		texture_scale[stage][2] = texture_scale[stage][3] = 1.0f;
		if (!texture || !texture->Data || mode == 0 || mode == 0x04 || mode == 0x05 || mode == 0x11)
		{
			key->sampler_type[stage] = mode == 0x11 ? _xgpu_sampler_2d : _xgpu_sampler_none;
			bgfx_set_texture((uint8_t)stage, bg.samplers[stage], bg.blank, UINT32_MAX);
			continue;
		}
		key->sampler_type[stage] = (((const DWORD *)texture)[3] & D3DFORMAT_CUBEMAP) ?
			_xgpu_sampler_cube : _xgpu_sampler_2d;
		if (texture->Data == device.back_buffer.Data || texture->Data == device.depth_buffer.Data)
		{
			bg_log_once("render targets as textures");
			if (key->sampler_type[stage] == _xgpu_sampler_cube)
				handle = bg.blank_cube;
		}
		else
		{
			const D3DCOLOR *palette = device.palettes[stage] && device.palettes[stage]->Data ?
				(const D3DCOLOR *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.palettes[stage]->Data) : NULL;

			if (!bg_texture_get((const DWORD *)texture, palette, &description, &handle, &mipmapped))
				handle = key->sampler_type[stage] == _xgpu_sampler_cube ? bg.blank_cube : bg.blank;
			if (description.linear)
			{
				texture_scale[stage][0] = 1.0f / (float)description.width;
				texture_scale[stage][1] = 1.0f / (float)description.height;
			}
		}
		bgfx_set_texture((uint8_t)stage, bg.samplers[stage], handle, bg_sampler_flags(stage, mipmapped));
	}
}

/* ---------- the draw's uniforms (prepare_draw's) */

static void bg_uniforms_update(float texture_scale[4][4])
{
	struct draw_uniforms *converted = &bg_uniforms;
	unsigned long index;
	int stage;

	memcpy(converted->viewport_scale, device.viewport_scale, sizeof(converted->viewport_scale));
	memcpy(converted->viewport_offset, device.viewport_offset, sizeof(converted->viewport_offset));
	memcpy(converted->texture_scale, texture_scale, sizeof(converted->texture_scale));
	converted->point_size = D3D__RenderState[D3DRS_POINTSIZE] ? dword_to_float(D3D__RenderState[D3DRS_POINTSIZE]) : 1.0f;
	for (stage = 0; stage < 8; stage++)
	{
		color_to_vec4(D3D__RenderState[D3DRS_PSCONSTANT0_0 + stage], converted->ps_c0[stage]);
		color_to_vec4(D3D__RenderState[D3DRS_PSCONSTANT1_0 + stage], converted->ps_c1[stage]);
	}
	color_to_vec4(D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0], converted->ps_final_c0);
	color_to_vec4(D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1], converted->ps_final_c1);
	color_to_vec4(D3D__RenderState[D3DRS_FOGCOLOR], converted->fog_color);
	converted->fog_parameters[0] = dword_to_float(D3D__RenderState[D3DRS_FOGSTART]);
	converted->fog_parameters[1] = dword_to_float(D3D__RenderState[D3DRS_FOGEND]);
	converted->fog_parameters[2] = dword_to_float(D3D__RenderState[D3DRS_FOGDENSITY]);
	converted->fog_parameters[3] = 0.0f;
	converted->alpha_reference = (float)(D3D__RenderState[D3DRS_ALPHAREF] & 0xff);
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		DWORD *state = D3D__TextureState[stage];

		converted->bump_matrix[stage][0] = dword_to_float(state[D3DTSS_BUMPENVMAT00]);
		converted->bump_matrix[stage][1] = dword_to_float(state[D3DTSS_BUMPENVMAT01]);
		converted->bump_matrix[stage][2] = dword_to_float(state[D3DTSS_BUMPENVMAT10]);
		converted->bump_matrix[stage][3] = dword_to_float(state[D3DTSS_BUMPENVMAT11]);
		converted->bump_luminance[stage][0] = dword_to_float(state[D3DTSS_BUMPENVLSCALE]);
		converted->bump_luminance[stage][1] = dword_to_float(state[D3DTSS_BUMPENVLOFFSET]);
		converted->bump_luminance[stage][2] = converted->bump_luminance[stage][3] = 0.0f;
		converted->texture_lod_bias[stage] = dword_to_float(state[D3DTSS_MIPMAPLODBIAS]);
	}
	converted->screen_offset = (float)UI_OFFSET;
	for (index = 0; index < BG_UNIFORM_COUNT; index++)
	{
		if (bg_uniform_sources[index].components == 1)
		{
			float value[4] = { *bg_uniform_sources[index].source, 0.0f, 0.0f, 0.0f };

			bgfx_set_uniform(bg.uniforms[index], value, 1);
		}
		else
		{
			bgfx_set_uniform(bg.uniforms[index], bg_uniform_sources[index].source, bg_uniform_sources[index].count);
		}
	}
}

/* ---------- vertices */

/* an element of a stream's vertex as four floats, missing components
(0, 0, 0, 1) as OpenGL's */
static void bg_vertex_element(const struct vertex_element *element, const unsigned char *source, float *out)
{
	unsigned long count = 0, index;

	out[0] = out[1] = out[2] = 0.0f;
	out[3] = 1.0f;
	switch (element->type)
	{
	case D3DVSDT_FLOAT1: count = 1; break;
	case D3DVSDT_FLOAT2: count = 2; break;
	case D3DVSDT_FLOAT3: case D3DVSDT_FLOAT2H: count = 3; break;
	case D3DVSDT_FLOAT4: count = 4; break;
	case D3DVSDT_NORMPACKED3:
	{
		/* (nv2a_vsh.c's unpack_normpacked3) */
		unsigned int packed;
		int x, y, z;

		memcpy(&packed, source, 4);
		x = (int)(packed << 21) >> 21;
		y = (int)(packed << 10) >> 21;
		z = (int)packed >> 22;
		out[0] = (float)x / 1023.0f;
		out[1] = (float)y / 1023.0f;
		out[2] = (float)z / 511.0f;
		return;
	}
	case D3DVSDT_D3DCOLOR:
		out[0] = source[2] / 255.0f;
		out[1] = source[1] / 255.0f;
		out[2] = source[0] / 255.0f;
		out[3] = source[3] / 255.0f;
		return;
	case D3DVSDT_SHORT1: case D3DVSDT_SHORT2: case D3DVSDT_SHORT3: case D3DVSDT_SHORT4:
	case D3DVSDT_NORMSHORT1: case D3DVSDT_NORMSHORT2: case D3DVSDT_NORMSHORT3: case D3DVSDT_NORMSHORT4:
	{
		BOOL normalized = element->type == D3DVSDT_NORMSHORT1 || element->type == D3DVSDT_NORMSHORT2 ||
			element->type == D3DVSDT_NORMSHORT3 || element->type == D3DVSDT_NORMSHORT4;

		count = element->bytes / 2;
		for (index = 0; index < count; index++)
		{
			short value;

			memcpy(&value, source + index * 2, 2);
			out[index] = normalized ? (value < -32767 ? -1.0f : value / 32767.0f) : (float)value;
		}
		return;
	}
	case D3DVSDT_PBYTE1: case D3DVSDT_PBYTE2: case D3DVSDT_PBYTE3: case D3DVSDT_PBYTE4:
		for (index = 0; index < element->bytes; index++)
			out[index] = source[index] / 255.0f;
		return;
	default:
		return;
	}
	memcpy(out, source, count * sizeof(float));
}

static uint64_t bg_topology(D3DPRIMITIVETYPE type)
{
	switch (type)
	{
	case D3DPT_POINTLIST: return BGFX_STATE_PT_POINTS;
	case D3DPT_LINELIST: return BGFX_STATE_PT_LINES;
	case D3DPT_LINELOOP:
	case D3DPT_LINESTRIP: return BGFX_STATE_PT_LINESTRIP;
	case D3DPT_TRIANGLESTRIP:
	case D3DPT_QUADSTRIP: return BGFX_STATE_PT_TRISTRIP;
	default: return 0;
	}
}

static unsigned long bg_index_count(D3DPRIMITIVETYPE type, unsigned long count)
{
	switch (type)
	{
	case D3DPT_QUADLIST: return count / 4 * 6;
	case D3DPT_TRIANGLEFAN:
	case D3DPT_POLYGON: return count >= 3 ? (count - 2) * 3 : 0;
	case D3DPT_LINELOOP: return count + 1;
	default: return count;
	}
}

static void bg_indices(D3DPRIMITIVETYPE type, const WORD *source, unsigned long count, unsigned long rebase, WORD *out)
{
	unsigned long index;

#define BG_INDEX(i) ((WORD)((source ? source[i] : (i)) - rebase))
	switch (type)
	{
	case D3DPT_QUADLIST:
		for (index = 0; index + 3 < count; index += 4)
		{
			*out++ = BG_INDEX(index);
			*out++ = BG_INDEX(index + 1);
			*out++ = BG_INDEX(index + 2);
			*out++ = BG_INDEX(index);
			*out++ = BG_INDEX(index + 2);
			*out++ = BG_INDEX(index + 3);
		}
		break;
	case D3DPT_TRIANGLEFAN:
	case D3DPT_POLYGON:
		for (index = 1; index + 1 < count; index++)
		{
			*out++ = BG_INDEX(0);
			*out++ = BG_INDEX(index);
			*out++ = BG_INDEX(index + 1);
		}
		break;
	default:
		for (index = 0; index < count; index++)
			*out++ = BG_INDEX(index);
		if (type == D3DPT_LINELOOP)
			*out++ = BG_INDEX(0);
		break;
	}
#undef BG_INDEX
}

/* ---------- views

bgfx draws a frame as views, in order: each has one target, rectangle and
clear. A view of its own starts at each clear after draws, and when the
viewport moves (the hardware viewport must be Direct3D's, since the vertex
shaders compute their positions in it). */

static void bg_view_open(const unsigned short rect[4])
{
	if (bg.view_used)
	{
		if (bg.view + 2 < BG_MAXIMUM_VIEWS)
			bg.view++;
		else
			bg_log_once("more views than a frame has");
	}
	bg.view_used = FALSE;
	memcpy(bg.view_rect, rect, sizeof(bg.view_rect));
	bgfx_set_view_frame_buffer(bg.view, bg.back_buffer);
	bgfx_set_view_mode(bg.view, BGFX_VIEW_MODE_SEQUENTIAL);
	bgfx_set_view_rect(bg.view, rect[0], rect[1], rect[2], rect[3]);
	bgfx_set_view_clear(bg.view, BGFX_CLEAR_NONE, 0, 1.0f, 0);
}

/* the rectangle of the viewport, in the back buffer's pixels */
static BOOL bg_viewport_rect(unsigned short rect[4])
{
	long x0 = scaled_pixel((float)device.viewport.X, screen_scale[0]);
	long y0 = scaled_pixel((float)device.viewport.Y, screen_scale[1]);
	long x1 = scaled_pixel((float)(device.viewport.X + device.viewport.Width), screen_scale[0]);
	long y1 = scaled_pixel((float)(device.viewport.Y + device.viewport.Height), screen_scale[1]);

	x0 = x0 < 0 ? 0 : x0;
	y0 = y0 < 0 ? 0 : y0;
	x1 = x1 > (long)bg.width ? (long)bg.width : x1;
	y1 = y1 > (long)bg.height ? (long)bg.height : y1;
	if (x1 <= x0 || y1 <= y0)
		return FALSE;
	rect[0] = (unsigned short)x0;
	rect[1] = (unsigned short)y0;
	rect[2] = (unsigned short)(x1 - x0);
	rect[3] = (unsigned short)(y1 - y0);
	return TRUE;
}

/* ---------- drawing */

/* the back buffer is the target, and whether its depth buffer is; FALSE for
any other (render targets: not yet) */
static BOOL bg_targets(BOOL *has_depth)
{
	if (!device.render_target || device.render_target->Data != device.back_buffer.Data)
		return FALSE;
	*has_depth = device.depth_stencil && device.depth_stencil->Data == device.depth_buffer.Data;
	return TRUE;
}

static void bg_draw(D3DPRIMITIVETYPE type, BOOL immediate, unsigned long first, unsigned long vertex_count,
	const WORD *indices, unsigned long index_count, unsigned long rebase)
{
	struct vertex_shader_object *program = current_program();
	struct vertex_shader_object *declaration = device.vertex_shader;
	struct nv2a_pixel_shader_key key;
	struct bg_shader *vertex_shader, *fragment_shader;
	bgfx_program_handle_t handle;
	bgfx_transient_vertex_buffer_t vertices;
	bgfx_transient_index_buffer_t generated_indices;
	float texture_scale[4][4];
	unsigned short rect[4];
	unsigned long generated, vertex, index;
	uint32_t stencil, rgba;
	uint64_t state;
	BOOL has_depth, native;
	int stage;

	if (!vertex_count || !program || !declaration || !program->instructions)
		return;
	if (!bg_targets(&has_depth))
	{
		bg.skipped++;
		bg_log_once("render targets other than the back buffer");
		return;
	}
	if (!bg_viewport_rect(rect))
		return;
	if (vertex_count > 0xffff)
	{
		bg.skipped++;
		return;
	}

	/* the pixel shader's key (prepare_draw's) */
	memset(&key, 0, sizeof(key));
	memcpy(key.combiner_state, D3D__RenderState, sizeof(key.combiner_state));
	memset(&key.combiner_state[D3DRS_PSCONSTANT0_0], 0, 16 * sizeof(DWORD));
	key.combiner_state[D3DRS_PSFINALCOMBINERCONSTANT0] = 0;
	key.combiner_state[D3DRS_PSFINALCOMBINERCONSTANT1] = 0;
	key.texture_modes = D3D__RenderState[D3DRS_PSTEXTUREMODES];
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		key.alpha_kill[stage] = D3D__TextureState[stage][D3DTSS_ALPHAKILL] == D3DTALPHAKILL_ENABLE;
		key.color_sign[stage] = (unsigned char)((D3D__TextureState[stage][D3DTSS_COLORSIGN] >> 28) & 0xf);
	}
	key.alpha_test_function = D3D__RenderState[D3DRS_ALPHATESTENABLE] ? D3D__RenderState[D3DRS_ALPHAFUNC] : 0;
	key.fog_enable = D3D__RenderState[D3DRS_FOGENABLE] != 0;
	key.fog_table_mode = (unsigned char)D3D__RenderState[D3DRS_FOGTABLEMODE];

	/* (the textures first: their sampler types are part of the key; bgfx
	keeps what is set until the submit) */
	bg_bind_textures(&key, texture_scale);
	vertex_shader = bg_vertex_shader(program, immediate);
	fragment_shader = bg_fragment_shader(&key);
	handle = vertex_shader && fragment_shader ? bg_program_get(vertex_shader, fragment_shader) :
		(bgfx_program_handle_t)BGFX_INVALID_HANDLE;
	native = !indices && type != D3DPT_QUADLIST && type != D3DPT_TRIANGLEFAN && type != D3DPT_POLYGON &&
		type != D3DPT_LINELOOP;
	generated = native ? 0 : bg_index_count(type, indices ? index_count : vertex_count);
	if (!BGFX_HANDLE_IS_VALID(handle) || (!native && !generated) ||
		bgfx_get_avail_transient_vertex_buffer((uint32_t)vertex_count, &bg.layout) < vertex_count ||
		(generated && bgfx_get_avail_transient_index_buffer((uint32_t)generated, false) < generated))
	{
		/* (drop the textures set for it) */
		bgfx_discard(BGFX_DISCARD_ALL);
		bg.skipped++;
		return;
	}

	/* the vertices: every input register, from the declaration's elements or
	the current values the declaration leaves out (immediate vertices are
	all sixteen registers already) */
	bgfx_alloc_transient_vertex_buffer(&vertices, (uint32_t)vertex_count, &bg.layout);
	if (immediate)
	{
		memcpy(vertices.data, device.immediate_vertices, vertex_count * BG_VERTEX_BYTES);
	}
	else
	{
		for (vertex = 0; vertex < vertex_count; vertex++)
		{
			float *out = (float *)(vertices.data + vertex * BG_VERTEX_BYTES);
			unsigned short provided = 0;

			for (index = 0; index < declaration->element_count; index++)
			{
				const struct vertex_element *element = &declaration->elements[index];
				const unsigned char *source;

				if (!device.streams[element->stream].data || element->type == D3DVSDT_NONE ||
					element->reg >= XGPU_VERTEX_ATTRIBUTE_COUNT || (provided & (1U << element->reg)))
					continue;
				provided |= (unsigned short)(1U << element->reg);
				source = (const unsigned char *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[element->stream].data) +
					(first + vertex) * device.streams[element->stream].stride + element->offset;
				bg_vertex_element(element, source, out + element->reg * 4);
			}
			for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
			{
				if (!(provided & (1U << index)))
					memcpy(out + index * 4, device.attributes[index], 4 * sizeof(float));
			}
		}
	}
	bgfx_set_transient_vertex_buffer(0, &vertices, 0, (uint32_t)vertex_count);
	if (generated)
	{
		bgfx_alloc_transient_index_buffer(&generated_indices, (uint32_t)generated, false);
		bg_indices(type, indices, indices ? index_count : vertex_count, rebase, (WORD *)generated_indices.data);
		bgfx_set_transient_index_buffer(&generated_indices, 0, (uint32_t)generated);
	}

	bg_uniforms_update(texture_scale);
	state = bg_state(has_depth, &stencil, &rgba) | bg_topology(type);
	bgfx_set_state(state, rgba);
	bgfx_set_stencil(stencil, BGFX_STENCIL_NONE);

	if (bg.view_used && memcmp(rect, bg.view_rect, sizeof(rect)) != 0)
		bg_view_open(rect);
	else if (!bg.view_used)
	{
		memcpy(bg.view_rect, rect, sizeof(bg.view_rect));
		bgfx_set_view_rect(bg.view, rect[0], rect[1], rect[2], rect[3]);
	}
	bgfx_submit(bg.view, handle, 0, BGFX_DISCARD_ALL);
	bg.view_used = TRUE;
	bg.drawn++;
}

static void bg_draw_vertices(D3DPRIMITIVETYPE type, unsigned long start, unsigned long count)
{
	bg_draw(type, FALSE, start, count, NULL, 0, 0);
}

static void bg_draw_indexed(D3DPRIMITIVETYPE type, unsigned long count, const WORD *indices)
{
	unsigned long minimum = 0xffff, maximum = 0, index;

	if (!count || !indices)
		return;
	for (index = 0; index < count; index++)
	{
		if (indices[index] < minimum)
			minimum = indices[index];
		if (indices[index] > maximum)
			maximum = indices[index];
	}
	/* (the vertices from the base vertex on: index i is vertex base + i;
	the indices are drawn made relative to the first vertex staged) */
	bg_draw(type, FALSE, device.base_vertex_index + minimum, maximum - minimum + 1, indices, count, minimum);
}

static void bg_draw_immediate(D3DPRIMITIVETYPE type, unsigned long count)
{
	bg_draw(type, TRUE, 0, count, NULL, 0, 0);
}

static void bg_clear(DWORD count, const D3DRECT *rectangles, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
	unsigned short rect[4];
	uint16_t clear = 0;
	BOOL has_depth;

	if (!bg_targets(&has_depth) || !bg_viewport_rect(rect))
		return;
	if (count > 1)
		bg_log_once("clears of several rectangles");
	if (count && rectangles)
	{
		long x0 = scaled_pixel((float)rectangles[0].x1, screen_scale[0]);
		long y0 = scaled_pixel((float)rectangles[0].y1, screen_scale[1]);
		long x1 = scaled_pixel((float)rectangles[0].x2, screen_scale[0]);
		long y1 = scaled_pixel((float)rectangles[0].y2, screen_scale[1]);

		x0 = x0 < 0 ? 0 : x0;
		y0 = y0 < 0 ? 0 : y0;
		x1 = x1 > (long)bg.width ? (long)bg.width : x1;
		y1 = y1 > (long)bg.height ? (long)bg.height : y1;
		if (x1 <= x0 || y1 <= y0)
			return;
		rect[0] = (unsigned short)x0;
		rect[1] = (unsigned short)y0;
		rect[2] = (unsigned short)(x1 - x0);
		rect[3] = (unsigned short)(y1 - y0);
	}
	if ((flags & D3DCLEAR_TARGET) && (flags & D3DCLEAR_TARGET) != D3DCLEAR_TARGET)
		bg_log_once("clears of some color channels");
	if (flags & D3DCLEAR_TARGET)
		clear |= BGFX_CLEAR_COLOR;
	if (has_depth && (flags & D3DCLEAR_ZBUFFER))
		clear |= BGFX_CLEAR_DEPTH;
	if (has_depth && (flags & D3DCLEAR_STENCIL))
		clear |= BGFX_CLEAR_STENCIL;
	if (!clear)
		return;
	/* a view of its own, which clears as it starts */
	bg_view_open(rect);
	bgfx_set_view_clear(bg.view, clear, (uint32_t)((color << 8) | (color >> 24)), z, (uint8_t)stencil);
	bgfx_touch(bg.view);
	bg.view_used = TRUE;
}

/* ---------- targets and presentation */

static const char bg_blit_vertex_source[] =
	"layout(location = 0) in vec4 a_position;\n"
	"layout(location = 1) in vec4 a_normal;\n"
	"out vec4 xT0;\n"
	"void main()\n"
	"{\n"
	"\txT0 = a_normal;\n"
	"\tgl_Position = vec4(a_position.xy, 0.0, 1.0);\n"
	"}\n";

static const char bg_blit_fragment_source[] =
	"in vec4 xT0;\n"
	"uniform sampler2D tex0;\n"
	"layout(location = 0) out vec4 fragment_color;\n"
	"void main()\n"
	"{\n"
	"\tfragment_color = vec4(texture(tex0, xT0.xy).rgb, 1.0);\n"
	"}\n";

static void bg_targets_update(void)
{
	unsigned long width = (unsigned long)scaled_pixel((float)halo_screen_width(), screen_scale[0]);
	unsigned long height = (unsigned long)scaled_pixel((float)SCREEN_HEIGHT, screen_scale[1]);
	bgfx_texture_handle_t attachments[2];

	if (width == bg.width && height == bg.height && BGFX_HANDLE_IS_VALID(bg.back_buffer))
		return;
	if (BGFX_HANDLE_IS_VALID(bg.back_buffer))
		bgfx_destroy_frame_buffer(bg.back_buffer);
	attachments[0] = bgfx_create_texture_2d((uint16_t)width, (uint16_t)height, false, 1, BGFX_TEXTURE_FORMAT_BGRA8,
		BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, NULL, 0);
	attachments[1] = bgfx_create_texture_2d((uint16_t)width, (uint16_t)height, false, 1, BGFX_TEXTURE_FORMAT_D24S8,
		BGFX_TEXTURE_RT_WRITE_ONLY, NULL, 0);
	bg.back_buffer = bgfx_create_frame_buffer_from_handles(2, attachments, true);
	bg.color = attachments[0];
	bg.width = width;
	bg.height = height;
	platform_log("bgfx: back buffer %lux%lu", width, height);
}

static void bg_window_update(void)
{
	int width = 0, height = 0;
	unsigned long flags = config_boolean("display.vsync") ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;

	SDL_GetWindowSizeInPixels(bg.window, &width, &height);
	if (width == bg.window_width && height == bg.window_height && flags == bg.reset_flags)
		return;
	bg.window_width = width;
	bg.window_height = height;
	bg.reset_flags = flags;
	bgfx_reset((uint32_t)width, (uint32_t)height, (uint32_t)flags, BGFX_TEXTURE_FORMAT_COUNT);
}

/* the back buffer into the window, letterboxed to its shape */
static void bg_blit(unsigned short view)
{
	unsigned long width = (unsigned long)bg.window_width;
	unsigned long height = (unsigned long)((unsigned long long)bg.window_width * bg.height / bg.width);
	bgfx_transient_vertex_buffer_t vertices;
	float x0, y0, x1, y1;
	float *out;
	int corner;

	if (height > (unsigned long)bg.window_height)
	{
		height = (unsigned long)bg.window_height;
		width = (unsigned long)((unsigned long long)bg.window_height * bg.width / bg.height);
	}
	bgfx_set_view_frame_buffer(view, (bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE);
	bgfx_set_view_rect(view, 0, 0, (uint16_t)bg.window_width, (uint16_t)bg.window_height);
	bgfx_set_view_clear(view, BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
	bgfx_touch(view);
	if (!BGFX_HANDLE_IS_VALID(bg.blit) || bgfx_get_avail_transient_vertex_buffer(4, &bg.layout) < 4)
		return;
	x0 = -(float)width / bg.window_width;
	x1 = -x0;
	y0 = (float)height / bg.window_height;
	y1 = -y0;
	bgfx_alloc_transient_vertex_buffer(&vertices, 4, &bg.layout);
	memset(vertices.data, 0, 4 * BG_VERTEX_BYTES);
	for (corner = 0; corner < 4; corner++)
	{
		out = (float *)(vertices.data + corner * BG_VERTEX_BYTES);
		out[0] = corner & 1 ? x1 : x0;
		out[1] = corner & 2 ? y1 : y0;
		out[4] = corner & 1 ? 1.0f : 0.0f;
		out[5] = corner & 2 ? 1.0f : 0.0f;
	}
	bgfx_set_transient_vertex_buffer(0, &vertices, 0, 4);
	bgfx_set_texture(0, bg.samplers[0], bg.color, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	bgfx_set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_PT_TRISTRIP, 0);
	bgfx_submit(view, bg.blit, 0, BGFX_DISCARD_ALL);
}

static void bg_present(void)
{
	static long screenshot_every = -1;
	unsigned short present_view;

	if (screenshot_every < 0)
		screenshot_every = *config_string("debug.screenshot_directory") ? config_integer("debug.screenshot_every") : 0;
	present_view = (unsigned short)(bg.view + 1);
	bg_blit(present_view);
	if (screenshot_every > 0 && device.frame % (unsigned long)screenshot_every == 0)
	{
		char path[512];

		snprintf(path, sizeof(path), "%s/frame%05lu.bmp", config_string("debug.screenshot_directory"), device.frame);
		bgfx_request_screen_shot((bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE, path);
	}
	bgfx_frame(BGFX_FRAME_NONE);
	if (debug_settings.statistics && device.frame % 60 == 0)
	{
		platform_log("bgfx frame %lu: %lu draws, %lu skipped, %u views", device.frame, bg.drawn / 60, bg.skipped / 60,
			(unsigned)present_view + 1);
		bg.drawn = bg.skipped = 0;
	}
	platform_video_swap();
	bg_window_update();
	bg_targets_update();
	/* the next frame's first view */
	bg.view = 0;
	bg.view_used = FALSE;
	{
		unsigned short rect[4] = { 0, 0, (unsigned short)bg.width, (unsigned short)bg.height };

		bg_view_open(rect);
	}
}

/* ---------- bgfx's callbacks: its errors to the log, screenshots as BMP */

static void bg_callback_fatal(bgfx_callback_interface_t *self, const char *file, uint16_t line, bgfx_fatal_t code,
	const char *text)
{
	(void)self;
	platform_log("bgfx: fatal %d at %s:%u: %s", (int)code, file, (unsigned)line, text);
	if (code != BGFX_FATAL_DEBUG_CHECK)
		abort();
}

static void bg_callback_trace(bgfx_callback_interface_t *self, const char *file, uint16_t line, const char *format,
	va_list arguments)
{
	(void)self;
	(void)file;
	(void)line;
	(void)format;
	(void)arguments;
}

static void bg_callback_profiler_begin(bgfx_callback_interface_t *self, const char *name, uint32_t abgr,
	const char *file, uint16_t line)
{
	(void)self, (void)name, (void)abgr, (void)file, (void)line;
}

static void bg_callback_profiler_end(bgfx_callback_interface_t *self)
{
	(void)self;
}

static uint32_t bg_callback_cache_read_size(bgfx_callback_interface_t *self, uint64_t id)
{
	(void)self, (void)id;
	return 0;
}

static bool bg_callback_cache_read(bgfx_callback_interface_t *self, uint64_t id, void *data, uint32_t size)
{
	(void)self, (void)id, (void)data, (void)size;
	return false;
}

static void bg_callback_cache_write(bgfx_callback_interface_t *self, uint64_t id, const void *data, uint32_t size)
{
	(void)self, (void)id, (void)data, (void)size;
}

static void bg_callback_screen_shot(bgfx_callback_interface_t *self, const char *path, uint32_t width,
	uint32_t height, uint32_t pitch, bgfx_texture_format_t format, const void *data, uint32_t size, bool flip)
{
	unsigned char header[54] = { 'B', 'M' };
	unsigned long image_size = (unsigned long)width * height * 4, row, column;
	FILE *file;

	(void)self, (void)format, (void)size;
	file = fopen(path, "wb");
	if (!file)
		return;
	*(unsigned int *)(header + 2) = (unsigned int)(54 + image_size);
	*(unsigned int *)(header + 10) = 54;
	*(unsigned int *)(header + 14) = 40;
	*(int *)(header + 18) = (int)width;
	*(int *)(header + 22) = flip ? (int)height : -(int)height;
	*(unsigned short *)(header + 26) = 1;
	*(unsigned short *)(header + 28) = 32;
	*(unsigned int *)(header + 34) = (unsigned int)image_size;
	fwrite(header, 1, sizeof(header), file);
	for (row = 0; row < height; row++)
	{
		unsigned char line[4 * 8192];
		const unsigned char *source = (const unsigned char *)data + row * pitch;

		for (column = 0; column < width && column < 8192; column++)
		{
			line[column * 4] = source[column * 4];
			line[column * 4 + 1] = source[column * 4 + 1];
			line[column * 4 + 2] = source[column * 4 + 2];
			line[column * 4 + 3] = 0xff;
		}
		fwrite(line, 4, width, file);
	}
	fclose(file);
	platform_log("bgfx: screenshot %s", path);
}

static void bg_callback_capture_begin(bgfx_callback_interface_t *self, uint32_t width, uint32_t height,
	uint32_t pitch, bgfx_texture_format_t format, bool flip)
{
	(void)self, (void)width, (void)height, (void)pitch, (void)format, (void)flip;
}

static void bg_callback_capture_end(bgfx_callback_interface_t *self)
{
	(void)self;
}

static void bg_callback_capture_frame(bgfx_callback_interface_t *self, const void *data, uint32_t size)
{
	(void)self, (void)data, (void)size;
}

static const bgfx_callback_vtbl_t bg_callback_table =
{
	bg_callback_fatal, bg_callback_trace, bg_callback_profiler_begin, bg_callback_profiler_begin,
	bg_callback_profiler_end, bg_callback_cache_read_size, bg_callback_cache_read, bg_callback_cache_write,
	bg_callback_screen_shot, bg_callback_capture_begin, bg_callback_capture_end, bg_callback_capture_frame,
};
static bgfx_callback_interface_t bg_callback = { &bg_callback_table };

/* ---------- start-up */

static BOOL bg_initialize(void)
{
	bgfx_init_t init;
	unsigned long index;
	int width = 0, height = 0;
	static const unsigned int black = 0xff000000;

	bg.window = platform_sdl_window();
	bg.view = 0;
	bg.back_buffer = (bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE;
	bg.blit = (bgfx_program_handle_t)BGFX_INVALID_HANDLE;
	SDL_GetWindowSizeInPixels(bg.window, &width, &height);

	/* (drawn on this thread: bgfx renders as each frame is submitted) */
	bgfx_render_frame(-1);
	bgfx_init_ctor(&init);
	init.type = BGFX_RENDERER_TYPE_METAL;
	init.platformData.nwh = SDL_GetPointerProperty(SDL_GetWindowProperties(bg.window),
		SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
	init.resolution.width = (uint32_t)width;
	init.resolution.height = (uint32_t)height;
	init.resolution.reset = config_boolean("display.vsync") ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
	init.limits.maxTransientVbSize = 64u << 20;
	init.limits.maxTransientIbSize = 8u << 20;
	init.callback = &bg_callback;
	if (!bgfx_init(&init))
	{
		platform_log("bgfx: it does not start (nothing is displayed)");
		return FALSE;
	}
	bg.window_width = width;
	bg.window_height = height;
	bg.reset_flags = init.resolution.reset;
	platform_log("bgfx: %s, window %dx%d", bgfx_get_renderer_name(bgfx_get_renderer_type()), width, height);

	bg.compiler = shaderc_compiler_initialize();
	bg.options = shaderc_compile_options_initialize();
	shaderc_compile_options_set_target_env(bg.options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
	shaderc_compile_options_set_auto_map_locations(bg.options, true);

	bgfx_vertex_layout_begin(&bg.layout, BGFX_RENDERER_TYPE_NOOP);
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
		bgfx_vertex_layout_add(&bg.layout, (bgfx_attrib_t)index, 4, BGFX_ATTRIB_TYPE_FLOAT, false, false);
	bgfx_vertex_layout_end(&bg.layout);
	for (index = 0; index < BG_UNIFORM_COUNT; index++)
		bg.uniforms[index] = bgfx_create_uniform(bg_uniform_sources[index].name, BGFX_UNIFORM_TYPE_VEC4,
			bg_uniform_sources[index].components == 1 ? 1 : bg_uniform_sources[index].count);
	for (index = 0; index < 4; index++)
	{
		char name[8];

		snprintf(name, sizeof(name), "tex%lu", index);
		bg.samplers[index] = bgfx_create_uniform(name, BGFX_UNIFORM_TYPE_SAMPLER, 1);
	}
	/* what a stage without a texture samples: black, as OpenGL's unit
	without one */
	bg.blank = bgfx_create_texture_2d(1, 1, false, 1, BGFX_TEXTURE_FORMAT_BGRA8, BGFX_TEXTURE_NONE,
		bgfx_copy(&black, 4), 0);
	{
		unsigned int faces[6] = { black, black, black, black, black, black };

		bg.blank_cube = bgfx_create_texture_cube(1, false, 1, BGFX_TEXTURE_FORMAT_BGRA8, BGFX_TEXTURE_NONE,
			bgfx_copy(faces, sizeof(faces)), 0);
	}
	{
		struct bg_shader *vertex = bg_shader_get(bg_blit_vertex_source, FALSE, "blit vertex");
		struct bg_shader *fragment = bg_shader_get(bg_blit_fragment_source, TRUE, "blit pixel");

		if (vertex != &bg_failed_shader && fragment != &bg_failed_shader)
			bg.blit = bgfx_create_program(vertex->shader, fragment->shader, false);
	}

	/* (gl_initialize's: the guest memory writes that make textures stale,
	the attributes' w, and the debug settings) */
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
		device.attributes[index][3] = 1.0f;
	memory_watch_initialize();
	debug_settings.statistics = config_boolean("debug.gpu_stats");
	halo_screen_width();
	bg_targets_update();
	bg.ready = TRUE;
	{
		unsigned short rect[4] = { 0, 0, (unsigned short)bg.width, (unsigned short)bg.height };

		bg_view_open(rect);
	}
	return TRUE;
}
