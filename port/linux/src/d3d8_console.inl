/*
D3D8_CONSOLE.INL

A console's native renderer under the Xbox Direct3D 8 device
(HALO_CONSOLE_NATIVE_RENDERER, display.renderer = "native"): the device's
state read at each draw, as the OpenGL path reads it, and handed to the
console's own graphics API through console_renderer.h, with no OpenGL in
between. It is part of d3d8_gl.c, included at its end, as the bgfx test
backend is (d3d8_bgfx.inl, whose structure this follows), and shares the
device's state. OpenGL is never initialized while it runs (gl_ready stays
FALSE), so the OpenGL path's entry points do nothing; "gl" draws with the
OpenGL path as before, the fallback.

What it does at each draw, and what it leaves out:
- textures are made once at the console from their Xbox texels (decoded to
  ARGB words, or DXT blocks as they are) and made again only when the game
  writes their pages; render targets are the console's, found by address;
- shaders are the translators' GLSL, made once by the console (which keeps
  them compiled on its drive), the vertex shader per Xbox program and the
  pixel shader per combiner state;
- the vertex constants the game changed since the last draw are the only
  ones sent (every shader has them at the same registers), and the pixel
  shader's uniforms only when they change;
- vertex data in the Xbox window are read by the GPU where they are, the
  CPU's cache written back when a page is first used or after the game
  writes it; pages the game rewrites frame after frame (dynamic vertices)
  are copied for the frame, as the OpenGL path's mirror does, and so is
  everything with console.native_in_place off; an element the GPU cannot
  fetch where it is (not on a word) is copied to a stream of its own;
- indices are copied for the frame on a console without a write watch, as
  the OpenGL path copies them;
- visibility tests (lens flares) are the console's queries, read a frame or
  more later and never waited for.
*/

#include "console_renderer.h"

#define CR_TEXTURE_BUCKETS 4096
#define CR_TARGET_BUCKETS 256
#define CR_FRAGMENT_BUCKETS 1024
/* a draw's vertices copied for the frame (each register four floats) */
#define CR_IMMEDIATE_STRIDE (XGPU_VERTEX_ATTRIBUTE_COUNT * 16)

struct cr_texture
{
	struct cr_texture *next;
	DWORD data, format_word, size_word;
	unsigned long palette_hash;
	unsigned long address, size, generation;
	struct xgpu_texture_description description;
	struct console_texture *texture;
	BOOL failed;
	unsigned long used_frame;
};

struct cr_target
{
	struct cr_target *next;
	DWORD data;
	unsigned long width, height, pixel_width, pixel_height;
	float scale[2];
	BOOL depth, fresh;
	struct console_target *target;
	unsigned long last_rendered;
};

struct cr_fragment
{
	struct cr_fragment *next;
	unsigned long hash;
	struct nv2a_pixel_shader_key key;
	struct console_shader *shader;
	BOOL failed;
};

/* the pages of the window in memory the GPU reads in place, as the OpenGL
path's mirror keeps them (mirror_refresh): written back from the CPU's
cache when first drawn from or after the game writes them; a page the game
keeps rewriting is copied instead for a while (MIRROR_* above) */
static struct
{
	unsigned char state[MIRROR_PAGE_COUNT];
	unsigned char rewrites[MIRROR_PAGE_COUNT];
	unsigned long generation[MIRROR_PAGE_COUNT];
	unsigned long rewritten_frame[MIRROR_PAGE_COUNT];
} cr_pages;

static struct
{
	BOOL ready;
	unsigned long frame;
	struct cr_texture *textures[CR_TEXTURE_BUCKETS];
	struct cr_target *targets[CR_TARGET_BUCKETS];
	struct cr_fragment *fragments[CR_FRAGMENT_BUCKETS];
	struct console_texture *blank;
	/* the bound targets */
	struct cr_target *target_color, *target_depth;
	float target_scale[2];
	unsigned long target_width, target_height;
	/* the vertex constants the console has (constants_serial when sent) */
	unsigned long long constants_serial;
	BOOL constants_sent;
	/* the uniforms the console has */
	struct draw_uniforms uniforms;
	BOOL uniforms_sent;
	/* the visibility test running, and each slot's count known */
	BOOL test_active;
	DWORD test_slot;
	float test_area[VISIBILITY_TEST_SLOTS];
	UINT test_known[VISIBILITY_TEST_SLOTS];
	unsigned long drawn, skipped_program, skipped_target, skipped_shader, skipped_copy, copied_bytes, in_place_bytes;
	/* attributes copied to a stream of their own (not on a word) */
	unsigned long split_attributes;
	/* textures freed for not being drawn */
	unsigned long textures_freed;
	/* console.native_in_place: the map's vertices read where they are */
	BOOL in_place;
} cr;

static BOOL cr_active(void)
{
	return cr.ready;
}

static void cr_log_once(const char *what)
{
	static const char *seen[32];
	static int count;
	int index;

	for (index = 0; index < count; index++)
	{
		if (seen[index] == what)
			return;
	}
	if (count < (int)(sizeof(seen) / sizeof(seen[0])))
		seen[count++] = what;
	platform_log("native renderer: %s", what);
}

/* ---------- the console's enumerations (console_renderer.h) from the
Xbox's, which are OpenGL's */

static unsigned char cr_compare(DWORD function)
{
	/* (D3DCMP_NEVER 0x200 to D3DCMP_ALWAYS 0x207, in the same order) */
	return function >= D3DCMP_NEVER && function <= D3DCMP_ALWAYS ? (unsigned char)(function - D3DCMP_NEVER) : 7;
}


static unsigned char cr_blend_factor(DWORD factor)
{
	switch (factor)
	{
	case D3DBLEND_ZERO: return _console_blend_zero;
	case D3DBLEND_ONE: return _console_blend_one;
	case D3DBLEND_SRCCOLOR: return _console_blend_source_color;
	case D3DBLEND_INVSRCCOLOR: return _console_blend_inverse_source_color;
	case D3DBLEND_SRCALPHA: return _console_blend_source_alpha;
	case D3DBLEND_INVSRCALPHA: return _console_blend_inverse_source_alpha;
	case D3DBLEND_DESTALPHA: return _console_blend_destination_alpha;
	case D3DBLEND_INVDESTALPHA: return _console_blend_inverse_destination_alpha;
	case D3DBLEND_DESTCOLOR: return _console_blend_destination_color;
	case D3DBLEND_INVDESTCOLOR: return _console_blend_inverse_destination_color;
	case D3DBLEND_SRCALPHASAT: return _console_blend_source_alpha_saturate;
	case D3DBLEND_CONSTANTCOLOR: return _console_blend_constant_color;
	case D3DBLEND_INVCONSTANTCOLOR: return _console_blend_inverse_constant_color;
	case D3DBLEND_CONSTANTALPHA: return _console_blend_constant_alpha;
	case D3DBLEND_INVCONSTANTALPHA: return _console_blend_inverse_constant_alpha;
	default: return _console_blend_one;
	}
}

/* add, subtract, reverse subtract, minimum, maximum */
static unsigned char cr_blend_operation(DWORD operation)
{
	switch (operation)
	{
	case D3DBLENDOP_SUBTRACT: return 1;
	case D3DBLENDOP_REVSUBTRACT: return 2;
	case D3DBLENDOP_MIN: return 3;
	case D3DBLENDOP_MAX: return 4;
	default: return 0;
	}
}

/* keep, zero, replace, increment and decrement (saturating), invert,
increment and decrement (wrapping) */
static unsigned char cr_stencil_operation(DWORD operation)
{
	switch (operation)
	{
	case D3DSTENCILOP_ZERO: return 1;
	case D3DSTENCILOP_REPLACE: return 2;
	case D3DSTENCILOP_INCRSAT: return 3;
	case D3DSTENCILOP_DECRSAT: return 4;
	case D3DSTENCILOP_INVERT: return 5;
	case D3DSTENCILOP_INCR: return 6;
	case D3DSTENCILOP_DECR: return 7;
	default: return 0;
	}
}

static unsigned char cr_address(DWORD mode)
{
	switch (mode)
	{
	case D3DTADDRESS_MIRROR: return _console_address_mirror;
	case D3DTADDRESS_CLAMP: case D3DTADDRESS_CLAMPTOEDGE: return _console_address_clamp;
	case D3DTADDRESS_BORDER: return _console_address_border;
	default: return _console_address_wrap;
	}
}

/* ---------- render targets (bg_target_get's) */

static struct cr_target **cr_target_bucket(DWORD data)
{
	return &cr.targets[((data >> 12) ^ (data >> 20)) % CR_TARGET_BUCKETS];
}

static struct cr_target *cr_target_get(const D3DSurface *surface)
{
	struct cr_target *entry;
	unsigned long width, height;
	float scale[2] = { 1.0f, 1.0f };
	BOOL depth;

	if (!surface || !surface->Data)
		return NULL;
	surface_dimensions(surface, &width, &height, &depth);
	if (width == (unsigned long)halo_screen_width() && height == SCREEN_HEIGHT)
	{
		scale[0] = screen_scale[0];
		scale[1] = screen_scale[1];
	}
	else if (!depth && surface_is_shadow_map(surface))
	{
		scale[0] = (float)halo_shadow_map_scale();
		scale[1] = scale[0];
	}
	for (entry = *cr_target_bucket(surface->Data); entry; entry = entry->next)
	{
		if (entry->data == surface->Data && entry->width == width && entry->height == height && entry->depth == depth &&
			entry->scale[0] == scale[0] && entry->scale[1] == scale[1])
			return entry;
	}
	entry = calloc(1, sizeof(*entry));
	if (!entry)
		return NULL;
	entry->data = surface->Data;
	entry->width = width;
	entry->height = height;
	entry->depth = depth;
	entry->scale[0] = scale[0];
	entry->scale[1] = scale[1];
	entry->pixel_width = (unsigned long)(width * scale[0] + 0.5f);
	entry->pixel_height = (unsigned long)(height * scale[1] + 0.5f);
	entry->target = console_target_create(entry->pixel_width, entry->pixel_height, depth);
	if (!entry->target)
	{
		free(entry);
		return NULL;
	}
	/* (cleared once: what a new target holds is undefined) */
	entry->fresh = TRUE;
	entry->next = *cr_target_bucket(entry->data);
	*cr_target_bucket(entry->data) = entry;
	return entry;
}

static BOOL cr_back_buffer_size(int *width, int *height, int *pixel_width, int *pixel_height)
{
	struct cr_target *back_buffer = cr.ready ? cr_target_get(&device.back_buffer) : NULL;

	if (!back_buffer)
		return FALSE;
	*width = (int)back_buffer->width;
	*height = (int)back_buffer->height;
	*pixel_width = (int)back_buffer->pixel_width;
	*pixel_height = (int)back_buffer->pixel_height;
	return TRUE;
}

/* the color target last drawn into at this address, or NULL */
static struct cr_target *cr_target_find(DWORD data)
{
	struct cr_target *entry, *best = NULL;

	for (entry = *cr_target_bucket(data); entry; entry = entry->next)
	{
		if (entry->data == data && !entry->depth && entry->last_rendered &&
			(!best || entry->last_rendered > best->last_rendered))
			best = entry;
	}
	return best;
}

/* a rectangle of the bound targets' units in their pixels, clipped to them */
static BOOL cr_target_rect(float x, float y, float width, float height, unsigned long rect[4])
{
	long x0 = scaled_pixel(x, cr.target_scale[0]);
	long y0 = scaled_pixel(y, cr.target_scale[1]);
	long x1 = scaled_pixel(x + width, cr.target_scale[0]);
	long y1 = scaled_pixel(y + height, cr.target_scale[1]);

	x0 = x0 < 0 ? 0 : x0;
	y0 = y0 < 0 ? 0 : y0;
	x1 = x1 > (long)cr.target_width ? (long)cr.target_width : x1;
	y1 = y1 > (long)cr.target_height ? (long)cr.target_height : y1;
	if (x1 <= x0 || y1 <= y0)
		return FALSE;
	rect[0] = (unsigned long)x0;
	rect[1] = (unsigned long)y0;
	rect[2] = (unsigned long)(x1 - x0);
	rect[3] = (unsigned long)(y1 - y0);
	return TRUE;
}

static BOOL cr_bind_targets(BOOL *has_depth)
{
	struct cr_target *color = cr_target_get(device.render_target);
	struct cr_target *depth = cr_target_get(device.depth_stencil);

	if (depth && !depth->depth)
		depth = NULL;
	if (color && depth && (color->pixel_width > depth->pixel_width || color->pixel_height > depth->pixel_height))
	{
		/* (the console's depth buffer is laid out with its target's rows: one
		smaller than the color cannot be used with it) */
		cr_log_once("a depth buffer smaller than its target");
		depth = NULL;
	}
	if (!color && !depth)
	{
		/* (the OpenGL path leaves these out too: say what the game had bound,
		the first few times) */
		static int said;

		if (said < 8)
		{
			said++;
			platform_log("native renderer: a draw without targets at frame %lu: color surface %08lx (data %08lx, format %08lx, "
				"size %08lx), depth surface %08lx (data %08lx)", cr.frame, (unsigned long)(uintptr_t)device.render_target,
				device.render_target ? (unsigned long)device.render_target->Data : 0UL,
				device.render_target ? (unsigned long)device.render_target->Format : 0UL,
				device.render_target ? (unsigned long)device.render_target->Size : 0UL,
				(unsigned long)(uintptr_t)device.depth_stencil,
				device.depth_stencil ? (unsigned long)device.depth_stencil->Data : 0UL);
		}
		return FALSE;
	}
	if (color)
		color->last_rendered = cr.frame + 1;
	cr.target_color = color;
	cr.target_depth = depth;
	cr.target_scale[0] = color ? color->scale[0] : depth->scale[0];
	cr.target_scale[1] = color ? color->scale[1] : depth->scale[1];
	cr.target_width = color ? color->pixel_width : depth->pixel_width;
	cr.target_height = color ? color->pixel_height : depth->pixel_height;
	console_targets_set(color ? color->target : NULL, depth ? depth->target : NULL);
	if ((color && color->fresh) || (depth && depth->fresh))
	{
		unsigned long rect[4] = { 0, 0, cr.target_width, cr.target_height };

		console_clear((color && color->fresh ? 1UL : 0UL) | (depth && depth->fresh ? 6UL : 0UL), rect, 1, 0xff000000UL,
			1.0f, 0);
		if (color)
			color->fresh = FALSE;
		if (depth)
			depth->fresh = FALSE;
	}
	*has_depth = depth != NULL;
	return TRUE;
}

/* ---------- textures (bg_texture_get's) */

static unsigned long cr_hash(const void *data, unsigned long size, unsigned long hash)
{
	const unsigned char *bytes = data;
	unsigned long index;

	for (index = 0; index < size; index++)
		hash = (hash ^ bytes[index]) * 16777619UL;
	return hash;
}

static unsigned long cr_level_dimension(unsigned long base, unsigned long level)
{
	unsigned long value = base >> level;

	return value ? value : 1;
}

static void cr_texture_upload(struct cr_texture *entry, const unsigned char *base, const D3DCOLOR *palette)
{
	struct xgpu_texture_description *description = &entry->description;
	int dxt = description->compressed ? xgpu_texture_dxt(description) : 0;
	unsigned long levels = description->levels ? description->levels : 1, level, face;
	BOOL volume = description->depth > 1;
	unsigned long *decoded = NULL;

	if (description->compressed && !dxt)
	{
		cr_log_once("compressed textures other than DXT are not drawn");
		entry->failed = TRUE;
		return;
	}
	if (entry->texture)
		console_texture_delete(entry->texture);
	entry->texture = console_texture_create(volume ? _console_texture_volume :
		description->cube_map ? _console_texture_cube : _console_texture_2d,
		dxt == 1 ? _console_format_dxt1 : dxt == 3 ? _console_format_dxt3 : dxt == 5 ? _console_format_dxt5 :
		_console_format_argb8, description->width, description->height, volume ? description->depth : 1, levels);
	if (!entry->texture)
	{
		entry->failed = TRUE;
		return;
	}
	if (!dxt)
	{
		decoded = malloc(description->width * description->height * (volume ? description->depth : 1) * 4);
		if (!decoded)
		{
			cr_log_once("no memory to convert a texture");
			return;
		}
	}
	for (face = 0; face < (description->cube_map ? 6UL : 1UL); face++)
	for (level = 0; level < levels; level++)
	{
		const unsigned char *source = base + face * xgpu_texture_face_size(description) +
			xgpu_texture_level_offset(description, level);

		/* (Halo PC's cube map: its levels one after another, each the six
		faces', the second and third swapped; xbox_textures.c's upload) */
		if (description->pc_layout && description->cube_map)
			source = base + xgpu_texture_level_offset(description, level) * 6 +
				(face == 1 ? 2 : face == 2 ? 1 : face) * xgpu_texture_level_bytes(description, level);
		if (dxt)
		{
			console_texture_level(entry->texture, face, level, source);
		}
		else if (xgpu_texture_decode_level(description, level, source, palette, decoded))
		{
			console_texture_level(entry->texture, face, level, decoded);
		}
	}
	free(decoded);
}

static struct console_texture *cr_texture_get(const DWORD *resource, const D3DCOLOR *palette,
	struct xgpu_texture_description *description)
{
	DWORD data = resource[1], format_word = resource[3], size_word = resource[4];
	unsigned long hash = palette ? cr_hash(palette, 256 * sizeof(D3DCOLOR), 2166136261UL) | 1 : 0;
	unsigned long bucket = cr_hash(&data, sizeof(data), cr_hash(&format_word, sizeof(format_word),
		cr_hash(&size_word, sizeof(size_word), 2166136261UL))) % CR_TEXTURE_BUCKETS;
	struct cr_texture *entry;
	unsigned long generation;

	for (entry = cr.textures[bucket]; entry; entry = entry->next)
	{
		if (entry->data == data && entry->format_word == format_word && entry->size_word == size_word &&
			entry->palette_hash == hash)
			break;
	}
	if (!entry)
	{
		entry = calloc(1, sizeof(*entry));
		if (!entry)
			return NULL;
		entry->data = data;
		entry->format_word = format_word;
		entry->size_word = size_word;
		entry->palette_hash = hash;
		xgpu_texture_describe(format_word, size_word, &entry->description);
#ifdef HALO_64BIT
		entry->address = (unsigned int)data | PLATFORM_CONTIGUOUS_BASE;
#else
		entry->address = (unsigned long)PLATFORM_PHYSICAL_TO_VIRTUAL(data);
#endif
		entry->size = xgpu_texture_face_size(&entry->description) * (entry->description.cube_map ? 6 : 1);
		entry->next = cr.textures[bucket];
		cr.textures[bucket] = entry;
	}
	*description = entry->description;
	if (entry->failed || !entry->size)
		return NULL;
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
			return NULL;
		cr_texture_upload(entry, (const unsigned char *)xbox_pointer(entry->address), palette);
		*description = entry->description;
	}
	if (entry->failed)
		return NULL;
	entry->used_frame = cr.frame;
	return entry->texture;
}

/* the stages' textures and samplers, and the pixel shader key's part of
them (bind_textures') */
static unsigned long cr_bind_textures(struct nv2a_pixel_shader_key *key, float texture_scale[4][4],
	struct console_sampler samplers[D3DTSS_MAXSTAGES])
{
	int stage;

	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		D3DBaseTexture *texture = device.textures[stage];
		unsigned long mode = stage_texture_mode(stage);
		struct console_sampler *sampler = &samplers[stage];
		DWORD *state = D3D__TextureState[stage];
		struct xgpu_texture_description description;
		struct cr_target *target;
		BOOL mipmapped;

		texture_scale[stage][0] = texture_scale[stage][1] = 1.0f;
		texture_scale[stage][2] = texture_scale[stage][3] = 1.0f;
		memset(sampler, 0, sizeof(*sampler));
		sampler->address[0] = sampler->address[1] = sampler->address[2] = _console_address_wrap;
		sampler->magnify = sampler->minify = 1;
		if (!texture || !texture->Data || mode == 0 || mode == 0x04 || mode == 0x05 || mode == 0x11)
		{
			key->sampler_type[stage] = mode == 0x11 ? _xgpu_sampler_2d : _xgpu_sampler_none;
			sampler->texture = cr.blank;
			continue;
		}
		xgpu_texture_describe(texture->Format, texture->Size, &description);
		key->sampler_type[stage] = description.cube_map ? _xgpu_sampler_cube :
			description.depth > 1 ? _xgpu_sampler_3d : _xgpu_sampler_2d;
		if ((target = cr_target_find(texture->Data)) != NULL)
		{
			/* a render target, as it was last drawn */
			key->sampler_type[stage] = _xgpu_sampler_2d;
			sampler->texture = console_target_texture(target->target);
			if (description.linear)
			{
				texture_scale[stage][0] = 1.0f / (float)target->width;
				texture_scale[stage][1] = 1.0f / (float)target->height;
			}
			description.levels = 1;
		}
		else
		{
			const D3DCOLOR *palette = device.palettes[stage] && device.palettes[stage]->Data ?
				(const D3DCOLOR *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.palettes[stage]->Data) : NULL;

			sampler->texture = cr_texture_get((const DWORD *)texture, palette, &description);
			if (!sampler->texture)
			{
				/* (not drawn yet: blank, as the OpenGL path's unbound unit) */
				sampler->texture = cr.blank;
				if (description.cube_map || description.depth > 1)
					key->sampler_type[stage] = _xgpu_sampler_2d;
				description.levels = 1;
			}
			if (description.linear)
			{
				texture_scale[stage][0] = 1.0f / (float)description.width;
				texture_scale[stage][1] = 1.0f / (float)description.height;
			}
		}
		mipmapped = description.levels > 1;
		/* (sampler_parameters') */
		sampler->minify = state[D3DTSS_MINFILTER] == D3DTEXF_POINT ? 0 : 1;
		sampler->magnify = state[D3DTSS_MAGFILTER] == D3DTEXF_POINT ? 0 : 1;
		sampler->mip = !mipmapped || state[D3DTSS_MIPFILTER] == D3DTEXF_NONE ? 0 :
			state[D3DTSS_MIPFILTER] == D3DTEXF_POINT ? 1 : 2;
		sampler->anisotropy = 1;
		if (state[D3DTSS_MINFILTER] == D3DTEXF_ANISOTROPIC && state[D3DTSS_MAXANISOTROPY] > 1)
		{
			sampler->minify = 2;
			sampler->anisotropy = (unsigned char)(state[D3DTSS_MAXANISOTROPY] > 16 ? 16 : state[D3DTSS_MAXANISOTROPY]);
		}
		sampler->address[0] = cr_address(state[D3DTSS_ADDRESSU]);
		sampler->address[1] = cr_address(state[D3DTSS_ADDRESSV]);
		sampler->address[2] = cr_address(state[D3DTSS_ADDRESSW]);
		sampler->border = state[D3DTSS_BORDERCOLOR];
		sampler->lod_bias = dword_to_float(state[D3DTSS_MIPMAPLODBIAS]);
		sampler->top_level = state[D3DTSS_MAXMIPLEVEL];
	}
	return D3DTSS_MAXSTAGES;
}

/* ---------- shaders */

static struct console_shader *cr_vertex_shader(struct vertex_shader_object *program, BOOL immediate)
{
	int variant = immediate ? 1 : 0;
	unsigned char tried = (unsigned char)(0x10 << variant);

	if (!program->console_shader[variant] && !(program->shaders_tried & tried))
	{
		char *source;

		program->shaders_tried |= tried;
		source = nv2a_vertex_shader_to_glsl(program->instructions, program->instruction_count,
			immediate ? 0 : device.vertex_shader->packed_mask, NULL);
		if (source)
		{
			program->console_shader[variant] = console_shader_create(1, source);
			if (!program->console_shader[variant])
				platform_log("native renderer: vertex shader %lu does not compile; its draws are left out", program->id);
			free(source);
		}
	}
	return program->console_shader[variant];
}

static struct console_shader *cr_pixel_shader(const struct nv2a_pixel_shader_key *key)
{
	static struct cr_fragment *recent[4];
	static unsigned long recent_next;
	unsigned long hash, index;
	struct cr_fragment **bucket;
	struct cr_fragment *entry;
	char *source;

	for (index = 0; index < 4; index++)
	{
		if (recent[index] && words_equal(&recent[index]->key, key, sizeof(*key)))
			return recent[index]->shader;
	}
	hash = hash_words(key, sizeof(*key));
	bucket = &cr.fragments[hash % CR_FRAGMENT_BUCKETS];
	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->hash == hash && words_equal(&entry->key, key, sizeof(*key)))
			break;
	}
	if (!entry)
	{
		entry = calloc(1, sizeof(*entry));
		if (!entry)
			return NULL;
		entry->hash = hash;
		entry->key = *key;
		source = nv2a_pixel_shader_to_glsl(key);
		entry->shader = source ? console_shader_create(0, source) : NULL;
		if (!entry->shader)
			cr_log_once("a pixel shader does not compile; its draws are left out");
		free(source);
		entry->next = *bucket;
		*bucket = entry;
	}
	recent[recent_next++ % 4] = entry;
	return entry->shader;
}

/* ---------- uniforms: only what changed since the console last had it */

static void cr_uniform_changed(int uniform, float *shadow, const float *value, unsigned long count)
{
	if (cr.uniforms_sent && !memcmp(shadow, value, count * 4 * sizeof(float)))
		return;
	memcpy(shadow, value, count * 4 * sizeof(float));
	console_uniform_set(uniform, 0, value, count);
}

static void cr_uniform_float(int uniform, float *shadow, float value)
{
	float vector[4] = { value, 0.0f, 0.0f, 0.0f };

	if (cr.uniforms_sent && *shadow == value)
		return;
	*shadow = value;
	console_uniform_set(uniform, 0, vector, 1);
}

static void cr_constants_update(void)
{
	unsigned long first = XGPU_VERTEX_CONSTANT_COUNT, last = 0, index;

	if (cr.constants_sent && cr.constants_serial == constants_serial)
		return;
	if (cr.constants_sent && constants_serial - cr.constants_serial <= CONSTANT_LOG_SIZE)
	{
		unsigned long long serial;

		for (serial = cr.constants_serial + 1; serial <= constants_serial; serial++)
		{
			index = constant_log[serial % CONSTANT_LOG_SIZE];
			if (first > index)
				first = index;
			if (last < index)
				last = index;
		}
	}
	else
	{
		first = 0;
		last = XGPU_VERTEX_CONSTANT_COUNT - 1;
	}
	if (first <= last)
		console_uniform_set(_console_uniform_constants, first, device.constants[first], last - first + 1);
	cr.constants_serial = constants_serial;
	cr.constants_sent = TRUE;
}

static void cr_uniforms_update(float texture_scale[4][4])
{
	struct draw_uniforms now;
	struct draw_uniforms *sent = &cr.uniforms;
	float model_lights[XGPU_MODEL_LIGHT_COUNT][4];
	int stage, light;

	memset(&now, 0, sizeof(now));
	memcpy(now.viewport_scale, device.viewport_scale, sizeof(now.viewport_scale));
	memcpy(now.viewport_offset, device.viewport_offset, sizeof(now.viewport_offset));
	memcpy(now.texture_scale, texture_scale, sizeof(now.texture_scale));
	now.point_size = D3D__RenderState[D3DRS_POINTSIZE] ? dword_to_float(D3D__RenderState[D3DRS_POINTSIZE]) : 1.0f;
	for (stage = 0; stage < 8; stage++)
	{
		color_to_vec4(D3D__RenderState[D3DRS_PSCONSTANT0_0 + stage], now.ps_c0[stage]);
		color_to_vec4(D3D__RenderState[D3DRS_PSCONSTANT1_0 + stage], now.ps_c1[stage]);
	}
	color_to_vec4(D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0], now.ps_final_c0);
	color_to_vec4(D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1], now.ps_final_c1);
	color_to_vec4(D3D__RenderState[D3DRS_FOGCOLOR], now.fog_color);
	now.fog_parameters[0] = dword_to_float(D3D__RenderState[D3DRS_FOGSTART]);
	now.fog_parameters[1] = dword_to_float(D3D__RenderState[D3DRS_FOGEND]);
	now.fog_parameters[2] = dword_to_float(D3D__RenderState[D3DRS_FOGDENSITY]);
	now.alpha_reference = (float)(D3D__RenderState[D3DRS_ALPHAREF] & 0xff);
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		DWORD *state = D3D__TextureState[stage];

		now.bump_matrix[stage][0] = dword_to_float(state[D3DTSS_BUMPENVMAT00]);
		now.bump_matrix[stage][1] = dword_to_float(state[D3DTSS_BUMPENVMAT01]);
		now.bump_matrix[stage][2] = dword_to_float(state[D3DTSS_BUMPENVMAT10]);
		now.bump_matrix[stage][3] = dword_to_float(state[D3DTSS_BUMPENVMAT11]);
		now.bump_luminance[stage][0] = dword_to_float(state[D3DTSS_BUMPENVLSCALE]);
		now.bump_luminance[stage][1] = dword_to_float(state[D3DTSS_BUMPENVLOFFSET]);
		now.texture_lod_bias[stage] = dword_to_float(state[D3DTSS_MIPMAPLODBIAS]);
	}
	now.screen_offset = (float)UI_OFFSET;
	cr_uniform_changed(_console_uniform_viewport_scale, sent->viewport_scale, now.viewport_scale, 1);
	cr_uniform_changed(_console_uniform_viewport_offset, sent->viewport_offset, now.viewport_offset, 1);
	cr_uniform_float(_console_uniform_point_size, &sent->point_size, now.point_size);
	cr_uniform_float(_console_uniform_screen_offset, &sent->screen_offset, now.screen_offset);
	cr_uniform_changed(_console_uniform_ps_c0, sent->ps_c0[0], now.ps_c0[0], 8);
	cr_uniform_changed(_console_uniform_ps_c1, sent->ps_c1[0], now.ps_c1[0], 8);
	cr_uniform_changed(_console_uniform_ps_final_c0, sent->ps_final_c0, now.ps_final_c0, 1);
	cr_uniform_changed(_console_uniform_ps_final_c1, sent->ps_final_c1, now.ps_final_c1, 1);
	cr_uniform_changed(_console_uniform_fog_color, sent->fog_color, now.fog_color, 1);
	cr_uniform_changed(_console_uniform_fog_parameters, sent->fog_parameters, now.fog_parameters, 1);
	cr_uniform_float(_console_uniform_alpha_reference, &sent->alpha_reference, now.alpha_reference);
	cr_uniform_changed(_console_uniform_bump_matrix, sent->bump_matrix[0], now.bump_matrix[0], 4);
	cr_uniform_changed(_console_uniform_bump_luminance, sent->bump_luminance[0], now.bump_luminance[0], 4);
	cr_uniform_changed(_console_uniform_texture_scale, sent->texture_scale[0], now.texture_scale[0], 4);
	cr_uniform_changed(_console_uniform_texture_lod_bias, sent->texture_lod_bias, now.texture_lod_bias, 1);
	/* (the per-pixel lighting's: not drawn by this renderer yet) */
	(void)model_lights;
	(void)light;
	cr.uniforms_sent = TRUE;
}

/* ---------- state */

static void cr_state(BOOL has_depth, struct console_state *state)
{
	DWORD *rs = D3D__RenderState;
	DWORD write = rs[D3DRS_COLORWRITEENABLE];
	unsigned long rect[4];

	memset(state, 0, sizeof(*state));
	state->color_write = (unsigned char)(((write & D3DCOLORWRITEENABLE_RED) ? 1 : 0) |
		((write & D3DCOLORWRITEENABLE_GREEN) ? 2 : 0) | ((write & D3DCOLORWRITEENABLE_BLUE) ? 4 : 0) |
		((write & D3DCOLORWRITEENABLE_ALPHA) ? 8 : 0));
	state->depth_test = has_depth && rs[D3DRS_ZENABLE];
	state->depth_write = state->depth_test && rs[D3DRS_ZWRITEENABLE];
	state->depth_function = rs[D3DRS_ZFUNC] ? cr_compare(rs[D3DRS_ZFUNC]) : 0;
	state->stencil = has_depth && rs[D3DRS_STENCILENABLE];
	if (state->stencil)
	{
		state->stencil_function = rs[D3DRS_STENCILFUNC] ? cr_compare(rs[D3DRS_STENCILFUNC]) : 0;
		state->stencil_fail = cr_stencil_operation(rs[D3DRS_STENCILFAIL]);
		state->stencil_depth_fail = cr_stencil_operation(rs[D3DRS_STENCILZFAIL]);
		state->stencil_pass = cr_stencil_operation(rs[D3DRS_STENCILPASS]);
		state->stencil_reference = (unsigned char)rs[D3DRS_STENCILREF];
		state->stencil_mask = (unsigned char)rs[D3DRS_STENCILMASK];
		state->stencil_write_mask = (unsigned char)rs[D3DRS_STENCILWRITEMASK];
	}
	state->blend = rs[D3DRS_ALPHABLENDENABLE] != 0;
	if (state->blend)
	{
		state->blend_source = cr_blend_factor(rs[D3DRS_SRCBLEND]);
		state->blend_destination = cr_blend_factor(rs[D3DRS_DESTBLEND]);
		state->blend_operation = cr_blend_operation(rs[D3DRS_BLENDOP]);
		state->blend_color = rs[D3DRS_BLENDCOLOR];
	}
	/* the cull mode names the winding to discard, in the target's rows from
	the top (the OpenGL path's front face and cull mode, as the 360's layer
	turned them back, come to this) */
	state->cull = rs[D3DRS_CULLMODE] == D3DCULL_CW ? _console_cull_cw :
		rs[D3DRS_CULLMODE] == D3DCULL_CCW ? _console_cull_ccw : _console_cull_none;
	state->wireframe = rs[D3DRS_FILLMODE] == D3DFILL_WIREFRAME;
	if (rs[D3DRS_SOLIDOFFSETENABLE])
	{
		state->slope_bias = dword_to_float(rs[D3DRS_POLYGONOFFSETZSLOPESCALE]);
		state->depth_bias = dword_to_float(rs[D3DRS_POLYGONOFFSETZOFFSET]);
	}
	if (cr_target_rect((float)device.viewport.X, (float)device.viewport.Y, (float)device.viewport.Width,
		(float)device.viewport.Height, rect))
		memcpy(state->viewport, rect, sizeof(rect));
	state->depth_near = device.viewport.MinZ;
	state->depth_far = device.viewport.MaxZ;
}

/* ---------- vertices and indices where they are */

/* whether [address, address + size) of the window can be read by the GPU
in place, its pages written back from the CPU's cache when the game has
written them; FALSE if one of them is rewritten too often (it is copied) */
static BOOL cr_resident(unsigned long address, unsigned long size)
{
	unsigned long start = address - PLATFORM_CONTIGUOUS_BASE, first, last, page;

	if (!size || address < PLATFORM_CONTIGUOUS_BASE || start + size > PLATFORM_CONTIGUOUS_SIZE)
		return FALSE;
	first = start / MIRROR_PAGE_SIZE;
	last = (start + size - 1) / MIRROR_PAGE_SIZE + 1;
	for (page = first; page < last; page++)
	{
		if (cr_pages.state[page] == _mirror_page_volatile)
		{
			if (cr.frame - cr_pages.rewritten_frame[page] < MIRROR_VOLATILE_FRAMES)
				return FALSE;
			cr_pages.state[page] = _mirror_page_absent;
		}
	}
	for (page = first; page < last; page++)
	{
		unsigned long page_address = PLATFORM_CONTIGUOUS_BASE + page * MIRROR_PAGE_SIZE;
		unsigned long generation;

		if (cr_pages.state[page] == _mirror_page_present)
		{
			generation = memory_watch_generation(page_address, MIRROR_PAGE_SIZE);
			if (generation <= cr_pages.generation[page])
				continue;
			if (cr.frame - cr_pages.rewritten_frame[page] <= MIRROR_REWRITE_FRAMES)
				cr_pages.rewrites[page]++;
			else
				cr_pages.rewrites[page] = 1;
			cr_pages.rewritten_frame[page] = cr.frame;
			if (cr_pages.rewrites[page] >= MIRROR_VOLATILE_REWRITES)
			{
				cr_pages.state[page] = _mirror_page_volatile;
				return FALSE;
			}
		}
		else
		{
			cr_pages.rewrites[page] = 0;
			cr_pages.rewritten_frame[page] = cr.frame;
		}
		memory_watch_protect(page_address, MIRROR_PAGE_SIZE);
		cr_pages.generation[page] = memory_watch_generation(page_address, MIRROR_PAGE_SIZE);
		cr_pages.state[page] = _mirror_page_present;
		console_memory_written(xbox_pointer(page_address), MIRROR_PAGE_SIZE);
	}
	return TRUE;
}

static int cr_vertex_format(const struct vertex_element *element)
{
	switch (element->type)
	{
	case D3DVSDT_FLOAT1: return _console_vertex_float1;
	case D3DVSDT_FLOAT2: return _console_vertex_float2;
	case D3DVSDT_FLOAT3: case D3DVSDT_FLOAT2H: return _console_vertex_float3;
	case D3DVSDT_FLOAT4: return _console_vertex_float4;
	case D3DVSDT_D3DCOLOR: return _console_vertex_color;
	case D3DVSDT_SHORT1: case D3DVSDT_SHORT2: return _console_vertex_short2;
	case D3DVSDT_SHORT3: case D3DVSDT_SHORT4: return _console_vertex_short4;
	case D3DVSDT_NORMSHORT1: case D3DVSDT_NORMSHORT2: return _console_vertex_short2n;
	case D3DVSDT_NORMSHORT3: case D3DVSDT_NORMSHORT4: return _console_vertex_short4n;
	case D3DVSDT_PBYTE1: case D3DVSDT_PBYTE2: case D3DVSDT_PBYTE3: case D3DVSDT_PBYTE4: return _console_vertex_ubyte4n;
	case D3DVSDT_NORMPACKED3: return _console_vertex_normpacked3;
	default: return 0;
	}
}

/* the bytes the console's fetch of an element's format reads (some read
more than the Xbox type has: a NORMSHORT1 is fetched as two) */
static unsigned long cr_fetch_bytes(int format)
{
	switch (format)
	{
	case _console_vertex_float2: case _console_vertex_short4: case _console_vertex_short4n: return 8;
	case _console_vertex_float3: return 12;
	case _console_vertex_float4: return 16;
	default: return 4;
	}
}

/* An element the console cannot fetch where it is: its GPU fetches whole
words at word offsets, so an element not on a word of its vertex (a model
vertex's node weight, at byte 30 of 32), in a stream whose stride is not
whole words, or whose fetch runs past its vertex, is copied for the frame
to a stream of its own, a slot of whole words a vertex, as x360_gl.c's
setup_vertices does. Its bytes keep their order. */
static const void *cr_split_element(const unsigned char *vertices, unsigned long stride, unsigned long count,
	const struct vertex_element *element, unsigned long slot)
{
	unsigned long *copy = console_frame_copy(NULL, slot * count);
	unsigned long index, bytes = element->bytes < slot ? element->bytes : slot;

	if (!copy)
		return NULL;
	for (index = 0; index < count; index++)
	{
		unsigned long words[4] = { 0, 0, 0, 0 };
		unsigned long word;

		memcpy(words, vertices + index * stride + element->offset, bytes);
		for (word = 0; word < slot / 4; word++)
			copy[index * (slot / 4) + word] = words[word];
	}
	cr.split_attributes++;
	return copy;
}

static int cr_primitive(D3DPRIMITIVETYPE type)
{
	switch (type)
	{
	case D3DPT_POINTLIST: return _console_primitive_points;
	case D3DPT_LINELIST: return _console_primitive_lines;
	case D3DPT_LINELOOP:
	case D3DPT_LINESTRIP: return _console_primitive_line_strip;
	case D3DPT_TRIANGLESTRIP:
	case D3DPT_QUADSTRIP: return _console_primitive_triangle_strip;
	case D3DPT_TRIANGLEFAN:
	case D3DPT_POLYGON: return _console_primitive_triangle_fan;
	case D3DPT_QUADLIST: return _console_primitive_quads;
	default: return _console_primitive_triangles;
	}
}

/* ---------- draws (bg_draw's) */

static void cr_draw(D3DPRIMITIVETYPE type, BOOL immediate, unsigned long first, unsigned long vertex_count,
	const WORD *indices, unsigned long index_count, unsigned long lowest)
{
	struct vertex_shader_object *program = current_program();
	struct vertex_shader_object *declaration = device.vertex_shader;
	struct nv2a_pixel_shader_key key;
	struct console_sampler samplers[D3DTSS_MAXSTAGES];
	struct console_stream streams[CONSOLE_VERTEX_STREAMS];
	struct console_element elements[CONSOLE_VERTEX_REGISTERS];
	struct console_state state;
	struct console_draw draw;
	float texture_scale[4][4];
	unsigned short loop_indices[2];
	BOOL has_depth;
	int stage;

	if (!vertex_count || !program || !declaration || !program->instructions)
	{
		cr.skipped_program++;
		return;
	}
	memset(&draw, 0, sizeof(draw));
	memset(&key, 0, sizeof(key));
	memcpy(key.combiner_state, D3D__RenderState, sizeof(key.combiner_state));
	memset(&key.combiner_state[D3DRS_PSCONSTANT0_0], 0, 16 * sizeof(DWORD));
	key.combiner_state[D3DRS_PSFINALCOMBINERCONSTANT0] = 0;
	key.combiner_state[D3DRS_PSFINALCOMBINERCONSTANT1] = 0;
	key.texture_modes = D3D__RenderState[D3DRS_PSTEXTUREMODES];
	draw.sampler_count = cr_bind_textures(&key, texture_scale, samplers);
	draw.samplers = samplers;
	if (!cr_bind_targets(&has_depth))
	{
		cr.skipped_target++;
		return;
	}
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		key.alpha_kill[stage] = D3D__TextureState[stage][D3DTSS_ALPHAKILL] == D3DTALPHAKILL_ENABLE;
		key.color_sign[stage] = (unsigned char)((D3D__TextureState[stage][D3DTSS_COLORSIGN] >> 28) & 0xf);
	}
	key.alpha_test_function = D3D__RenderState[D3DRS_ALPHATESTENABLE] ? D3D__RenderState[D3DRS_ALPHAFUNC] : 0;
	key.fog_enable = D3D__RenderState[D3DRS_FOGENABLE] != 0;
	key.fog_table_mode = (unsigned char)D3D__RenderState[D3DRS_FOGTABLEMODE];
	draw.vertex_shader = cr_vertex_shader(program, immediate);
	draw.pixel_shader = cr_pixel_shader(&key);
	if (!draw.vertex_shader || !draw.pixel_shader)
	{
		cr.skipped_shader++;
		return;
	}
	cr_state(has_depth, &state);
	draw.state = &state;
	draw.primitive = cr_primitive(type);
	draw.counted = cr.test_active;

	/* the vertices: the declaration's streams where they are (or copied for
	the frame), or the immediate vertices, every register four floats */
	if (immediate)
	{
		unsigned long index;

		streams[0].data = console_frame_copy(device.immediate_vertices, vertex_count * CR_IMMEDIATE_STRIDE);
		if (!streams[0].data)
		{
			cr.skipped_copy++;
			return;
		}
		streams[0].size = vertex_count * CR_IMMEDIATE_STRIDE;
		streams[0].stride = CR_IMMEDIATE_STRIDE;
		streams[0].little_endian = FALSE;
		for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
		{
			elements[index].reg = (unsigned char)index;
			elements[index].stream = 0;
			elements[index].format = _console_vertex_float4;
			elements[index].offset = (unsigned short)(index * 16);
		}
		draw.stream_count = 1;
		draw.element_count = XGPU_VERTEX_ATTRIBUTE_COUNT;
		/* (one layout for every immediate draw) */
		draw.layout_key = (const void *)&cr;
		cr.copied_bytes += streams[0].size;
	}
	else
	{
		unsigned long index, stream;
		BOOL placed[CONSOLE_VERTEX_STREAMS] = { FALSE };
		/* the elements copied to streams of their own, placed after the rest */
		struct
		{
			const struct vertex_element *element;
			unsigned long element_index, slot;
		} split[CONSOLE_VERTEX_REGISTERS];
		unsigned long split_count = 0, element_count = 0;

		for (index = 0; index < declaration->element_count; index++)
		{
			const struct vertex_element *element = &declaration->elements[index];
			unsigned long stride;
			int format;

			stream = element->stream;
			if (stream >= CONSOLE_VERTEX_STREAMS || !device.streams[stream].data || element->type == D3DVSDT_NONE ||
				element->reg >= CONSOLE_VERTEX_REGISTERS || !(format = cr_vertex_format(element)))
				continue;
			stride = device.streams[stream].stride;
			if (!placed[stream])
			{
				unsigned long bytes = stride ? stride * vertex_count : 64;
				unsigned long address = PLATFORM_WINDOW_ADDRESS(PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[stream].data)) +
					first * stride;
				/* (the map's own vertices: little-endian, as loaded; the GPU reads
				them where they are when they are on words, or a copy of them,
				the same bytes, with console.native_in_place off) */
				BOOL resident = cr_resident(address, bytes);

				placed[stream] = TRUE;
				streams[stream].stride = stride;
				streams[stream].size = bytes;
				streams[stream].little_endian = (unsigned char)resident;
				if (resident && cr.in_place && !(address & 3) && !(stride & 3))
				{
					streams[stream].data = xbox_pointer(address);
					cr.in_place_bytes += bytes;
				}
				else
				{
					streams[stream].data = console_frame_copy(xbox_pointer(address), bytes);
					cr.copied_bytes += bytes;
					if (!streams[stream].data)
					{
						cr.skipped_copy++;
						return;
					}
				}
				if (draw.stream_count < stream + 1)
					draw.stream_count = stream + 1;
			}
			elements[element_count].reg = element->reg;
			elements[element_count].stream = (unsigned char)stream;
			elements[element_count].format = (unsigned char)format;
			elements[element_count].offset = element->offset;
			if (stride && ((element->offset & 3) || (stride & 3) || element->offset + cr_fetch_bytes(format) > stride) &&
				split_count < CONSOLE_VERTEX_REGISTERS)
			{
				split[split_count].element = element;
				split[split_count].element_index = element_count;
				split[split_count].slot = (cr_fetch_bytes(format) + 3) & ~3UL;
				split_count++;
			}
			element_count++;
		}
		for (stream = 0; stream < draw.stream_count; stream++)
		{
			if (!placed[stream])
				memset(&streams[stream], 0, sizeof(streams[stream]));
		}
		for (index = 0; index < split_count; index++)
		{
			const struct vertex_element *element = split[index].element;
			unsigned long source = element->stream, slot = split[index].slot;
			unsigned long target = draw.stream_count;
			const unsigned char *vertices = PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[source].data);

			if (target >= CONSOLE_VERTEX_STREAMS)
				break;
			streams[target].data = cr_split_element(vertices + first * streams[source].stride, streams[source].stride,
				vertex_count, element, slot);
			if (!streams[target].data)
			{
				cr.skipped_copy++;
				return;
			}
			streams[target].size = slot * vertex_count;
			streams[target].stride = slot;
			streams[target].little_endian = streams[source].little_endian;
			cr.copied_bytes += streams[target].size;
			elements[split[index].element_index].stream = (unsigned char)target;
			elements[split[index].element_index].offset = 0;
			draw.stream_count = target + 1;
		}
		draw.element_count = element_count;
		/* (the elements follow from the declaration, which streams it has and
		their strides: console_draw's layout key; strides change rarely, and
		the console checks them with the layout it finds) */
		draw.layout_key = declaration;
	}
	draw.streams = streams;
	draw.elements = elements;
	draw.constant_values = (const float (*)[4])device.attributes;
	draw.vertex_count = vertex_count;
	draw.vertex_first = lowest;
	if (indices)
	{
#ifdef HALO_CONSOLE_NO_WRITE_WATCH
		/* (copied, as the OpenGL path copies them: a console without a write
		watch sees the game's index writes only through a lock, and the
		dynamic triangles are rewritten in place frame after frame while the
		GPU may still be reading the last frame's) */
		BOOL in_place = FALSE;
#else
		BOOL in_place = cr.in_place && !((uintptr_t)indices & 3) &&
			cr_resident((unsigned long)XBOX_ADDRESS(indices), index_count * sizeof(WORD));
#endif

		draw.index_count = index_count;
		if (in_place)
		{
			draw.indices = (const unsigned short *)indices;
			draw.indices_resident = TRUE;
		}
		else
		{
			draw.indices = console_frame_copy(indices, (index_count * sizeof(WORD) + 3) & ~3UL);
			if (!draw.indices)
			{
				cr.skipped_copy++;
				return;
			}
		}
	}
	else if (type == D3DPT_LINELOOP)
	{
		/* (a strip back to its first vertex) */
		unsigned short *loop = console_frame_copy(NULL, ((vertex_count + 1) * sizeof(WORD) + 3) & ~3UL);
		unsigned long index;

		if (!loop)
			return;
		for (index = 0; index < vertex_count; index++)
			loop[index] = (unsigned short)index;
		loop[vertex_count] = 0;
		draw.indices = loop;
		draw.index_count = vertex_count + 1;
		(void)loop_indices;
	}
	cr_constants_update();
	cr_uniforms_update(texture_scale);
	console_draw(&draw);
	cr.drawn++;
}

static void cr_draw_vertices(D3DPRIMITIVETYPE type, unsigned long start, unsigned long count)
{
	cr_draw(type, FALSE, start, count, NULL, 0, 0);
}

static void cr_draw_indexed(D3DPRIMITIVETYPE type, unsigned long count, const WORD *indices)
{
	unsigned long minimum, maximum, address;
	BOOL watched;

	if (!count || !indices)
		return;
	/* (the same ranges are drawn frame after frame: index_extent keeps
	their extents; vertices count from the base vertex, and the indices
	are drawn as they are, from it) */
	/* (kept by write generation only where there is one: in the window;
	indices elsewhere are read again each time) */
	address = (unsigned long)XBOX_ADDRESS(indices);
	watched = address >= PLATFORM_CONTIGUOUS_BASE &&
		address - PLATFORM_CONTIGUOUS_BASE + count * sizeof(WORD) <= PLATFORM_CONTIGUOUS_SIZE;
	index_extent(indices, count, watched ? memory_watch_generation(address, count * sizeof(WORD)) : 0, watched,
		&minimum, &maximum);
	cr_draw(type, FALSE, device.base_vertex_index, maximum + 1, indices, count, minimum);
}

static void cr_draw_immediate(D3DPRIMITIVETYPE type, unsigned long count)
{
	cr_draw(type, TRUE, 0, count, NULL, 0, 0);
}

static void cr_clear(DWORD count, const D3DRECT *rectangles, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
	unsigned long rects[16][4];
	unsigned long rect_count = 0, clear = 0;
	BOOL has_depth;

	if (!cr_bind_targets(&has_depth))
		return;
	if (flags & D3DCLEAR_TARGET)
		clear |= 1;
	if (has_depth && (flags & D3DCLEAR_ZBUFFER))
		clear |= 2;
	if (has_depth && (flags & D3DCLEAR_STENCIL))
		clear |= 4;
	if (!clear)
		return;
	if (count && rectangles)
	{
		for (; count && rect_count < 16; count--, rectangles++)
		{
			if (cr_target_rect((float)rectangles->x1, (float)rectangles->y1, (float)(rectangles->x2 - rectangles->x1),
				(float)(rectangles->y2 - rectangles->y1), rects[rect_count]))
				rect_count++;
		}
	}
	else if (cr_target_rect((float)device.viewport.X, (float)device.viewport.Y, (float)device.viewport.Width,
		(float)device.viewport.Height, rects[0]))
	{
		rect_count = 1;
	}
	if (rect_count)
		console_clear(clear, rects[0], rect_count, color, z, stencil);
}

/* ---------- visibility tests: the console's queries, never waited for */

static void cr_visibility_begin(void)
{
	if (cr.test_active)
		return;
	cr.test_active = TRUE;
	console_query_begin();
}

static void cr_visibility_end(DWORD index)
{
	if (!cr.test_active)
		return;
	cr.test_active = FALSE;
	index %= VISIBILITY_TEST_SLOTS;
	/* (the count in the game's pixels: lens flares divide it by their area) */
	cr.test_area[index] = cr.target_scale[0] * cr.target_scale[1];
	console_query_end(index);
}

static UINT cr_visibility_result(DWORD index)
{
	unsigned long samples;

	index %= VISIBILITY_TEST_SLOTS;
	if (console_query_result(index, &samples))
	{
		float area = cr.test_area[index];

		cr.test_known[index] = area > 1.0f ? (UINT)(samples / area + 0.5f) : (UINT)samples;
	}
	return cr.test_known[index];
}

/* ---------- the frame */

/* textures not drawn for this many frames give their memory back (made
again from the game's texels at their next use) */
#define CR_TEXTURE_IDLE_FRAMES 900
#define CR_TEXTURE_SWEEP_BUCKETS 64

/* The game's texture cache loads a level's textures as they come into
view, often into memory another texture had: the console's copy of one no
longer drawn stays made, so a long level (The Silent Cartographer) used up
the console's memory a few MB at a time. A few buckets a frame are looked
through, every one in about two seconds. */
static void cr_texture_sweep(void)
{
	static unsigned long next;
	unsigned long count;

	for (count = 0; count < CR_TEXTURE_SWEEP_BUCKETS; count++, next++)
	{
		struct cr_texture *entry;

		for (entry = cr.textures[next % CR_TEXTURE_BUCKETS]; entry; entry = entry->next)
		{
			if (entry->texture && cr.frame - entry->used_frame > CR_TEXTURE_IDLE_FRAMES)
			{
				console_texture_delete(entry->texture);
				entry->texture = NULL;
				entry->generation = 0;
				cr.textures_freed++;
			}
		}
	}
}

static void cr_present(void)
{
	struct cr_target *back_buffer = cr_target_get(&device.back_buffer);

	/* (every 10 seconds of the game's 30 frames, what the console's
	renderer left out and what it read in place) */
	if (cr.frame % 300 == 299)
	{
		platform_log("native renderer: in 300 frames, %lu draws; left out %lu without a program, %lu without targets, %lu without shaders, "
			"%lu without copy space; %lu KB read in place, %lu KB copied (%lu attributes off a word)", cr.drawn,
			cr.skipped_program, cr.skipped_target, cr.skipped_shader, cr.skipped_copy, cr.in_place_bytes >> 10,
			cr.copied_bytes >> 10, cr.split_attributes);
		cr.drawn = cr.skipped_program = cr.skipped_target = cr.skipped_shader = cr.skipped_copy = cr.copied_bytes = cr.in_place_bytes = 0;
		cr.split_attributes = 0;
		if (cr.textures_freed)
		{
			platform_log("native renderer: %lu textures not drawn for %d frames freed", cr.textures_freed,
				CR_TEXTURE_IDLE_FRAMES);
			cr.textures_freed = 0;
		}
	}
	cr_texture_sweep();

	if (back_buffer)
		console_present(back_buffer->target);
	cr.frame++;
	/* (the bound targets are the console's again after its present) */
	cr.target_color = cr.target_depth = NULL;
}

static BOOL cr_initialize(void)
{
	static const unsigned long white = 0xffffffffUL;

	if (!console_renderer_initialize())
	{
		platform_log("native renderer: the console's device did not start; drawing with the OpenGL path");
		return FALSE;
	}
	cr.blank = console_texture_create(_console_texture_2d, _console_format_argb8, 1, 1, 1, 1);
	if (cr.blank)
		console_texture_level(cr.blank, 0, 0, &white);
	cr.in_place = config_boolean("console.native_in_place");
	cr.ready = TRUE;
	platform_log("native renderer: drawing with the console's own graphics (display.renderer = \"native\"; "
		"\"gl\" for the OpenGL path); the map's vertices %s", cr.in_place ? "read where they are" :
		"copied (console.native_in_place = false)");
	return TRUE;
}
