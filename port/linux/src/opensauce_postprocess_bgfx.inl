/*
OPENSAUCE_POSTPROCESS_BGFX.INL

The bgfx renderer's part of OpenSauce's post-processing
(opensauce_postprocess.h), included in opensauce_postprocess_gl.inl before
its public code, which hands each call here while bgfx draws. It draws what
the OpenGL part draws, the same way: the effects as opensauce_postprocess_gl.inl
reads and keeps them (their parameters, constants and preshaders are its
own), each pass's GLSL from d3d9_effect.c made into bgfx's shaders as the
translators' are (bg_vulkan_glsl), the scene and secondary chains, the
G-buffer made from the depth buffer once a frame, the fade and OpenSauce's
bloom. Each pass is a view of its own, drawn into a target of the chains;
the result is drawn back into the back buffer at the stage's end. Nothing
here runs on a map without an OpenSauce header and these tags: the game's
stages return before calling it (port/linux/game/opensauce_postprocess.c).
*/

/* ---------- constants */

enum
{
	/* the varyings' first location: bg_vulkan_glsl's own varyings take 0 to
	10 (Direct3D 11 declares all of them in both stages) */
	BGPP_VARYING_LOCATION = 11,
};

/* ---------- structures */

struct bgpp_program
{
	bgfx_program_handle_t program;
	BOOL tried, failed;
	unsigned long vertex_float_count, pixel_float_count;
	unsigned char pixel_samplers[D3D9_SHADER_SAMPLERS];
	/* both shaders' uniform blocks (bg_draw's budget) */
	unsigned long uniform_bytes;
};

/* an effect's bgfx side, by ppgl.effects' index: its passes' programs and
its texture parameters (a bitmap's texture by the game's resource, found
again at each pass as bgfx's texture cache keeps it, or one of the chains') */
struct bgpp_effect
{
	struct bgpp_program programs[PPGL_MAXIMUM_PASSES];
	const DWORD **resources;
	bgfx_texture_handle_t *handles;
	unsigned long parameter_count;
};

/* ---------- globals */

static struct
{
	BOOL tried, failed;
	struct bgpp_effect effects[PPGL_MAXIMUM_EFFECTS];
	/* the targets (as ppgl's): the main chain's three, the secondary
	chain's two, the G-buffer's three, the bloom's two */
	bgfx_texture_handle_t main_textures[3], secondary_textures[2], gbuffer_textures[3], bloom_textures[2];
	bgfx_frame_buffer_handle_t main_framebuffers[3], secondary_framebuffers[2], gbuffer_framebuffer,
		bloom_framebuffers[2];
	BOOL wide;
	/* the programs of its own: copy, fade, G-buffer, bloom's three */
	bgfx_program_handle_t copy, fade, gbuffer, bloom[3];
	/* the effects' uniforms (d3d9_shader_glsl's) and samplers, and its own */
	bgfx_uniform_handle_t vc, pc, vci, pci, vcb, pcb, pixel_fix;
	bgfx_uniform_handle_t samplers[D3D9_SHADER_SAMPLERS];
	bgfx_uniform_handle_t size, clip, world_from_clip, previous_clip_from_world, minimum, maximum, amounts, spread,
		fade_amount;
	bgfx_uniform_handle_t depth_range;
	bgfx_texture_handle_t black, black_cube, black_volume;
	struct bg_target *back_buffer;
	/* the last view drawn into, and its frame buffer */
	bgfx_frame_buffer_handle_t view_framebuffer;
	unsigned short view_rect[4];
} bgpp;

/* ---------- shaders */

/* the full-screen passes' (bg_blit_vertex_source draws them, over
bg.full_quad); each reads the pixel it is drawn at */
static const char bgpp_copy_source[] =
	"uniform sampler2D tex0;\n"
	"layout(location = 0) out vec4 result;\n"
	"void main()\n"
	"{\n"
	"\tresult = texelFetch(tex0, ivec2(gl_FragCoord.xy), 0);\n"
	"}\n";

static const char bgpp_fade_source[] =
	"uniform sampler2D tex0;\n"
	"uniform vec4 pp_fade;\n"
	"layout(location = 0) out vec4 result;\n"
	"void main()\n"
	"{\n"
	"\tresult = vec4(texelFetch(tex0, ivec2(gl_FragCoord.xy), 0).rgb, pp_fade.x);\n"
	"}\n";

/* (ppgl_gbuffer_source's, its uniforms named apart from the game's) */
static const char bgpp_gbuffer_source[] =
	"uniform sampler2D tex0;\n"
	"uniform vec4 pp_world_from_clip[4];\n"
	"uniform vec4 pp_previous_clip_from_world[4];\n"
	"uniform vec4 pp_clip;\n"
	"uniform vec4 pp_size;\n"
	"layout(location = 0) out vec4 out_depth;\n"
	"layout(location = 1) out vec4 out_velocity;\n"
	"layout(location = 2) out vec4 out_normals_index;\n"
	"void main()\n"
	"{\n"
	"\tfloat d = texelFetch(tex0, ivec2(gl_FragCoord.xy), 0).r;\n"
	"\tvec2 uv = gl_FragCoord.xy / pp_size.xy;\n"
	"\tvec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);\n"
	"\tfloat z_ndc = d * 2.0 - 1.0;\n"
	"\tfloat n = pp_clip.x, f = pp_clip.y;\n"
	"\tfloat distance = 2.0 * n * f / max((f + n) - z_ndc * (f - n), 1e-6);\n"
	"\tbool sky = d >= 0.9999999;\n"
	"\tout_depth = vec4(sky ? 1.0 : clamp((distance - n) / (f - n), 0.0, 1.0), 0.0, 0.0, 1.0);\n"
	"\tvec4 point = vec4(ndc, z_ndc, 1.0);\n"
	"\tvec4 world = vec4(dot(pp_world_from_clip[0], point), dot(pp_world_from_clip[1], point),\n"
	"\t\tdot(pp_world_from_clip[2], point), dot(pp_world_from_clip[3], point));\n"
	"\tworld /= world.w;\n"
	"\tvec4 previous = vec4(dot(pp_previous_clip_from_world[0], world), dot(pp_previous_clip_from_world[1], world),\n"
	"\t\tdot(pp_previous_clip_from_world[2], world), dot(pp_previous_clip_from_world[3], world));\n"
	"\tvec2 previous_ndc = previous.xy / max(abs(previous.w), 1e-6) * sign(previous.w);\n"
	"\tout_velocity = vec4(sky ? vec2(0.0) : (ndc - previous_ndc) * 0.5, 0.0, 1.0);\n"
	"\tvec3 view = vec3(ndc.x * distance / pp_clip.z, ndc.y * distance / pp_clip.w, distance);\n"
	"\tvec3 normal = cross(dFdx(view), dFdy(view));\n"
	"\tnormal = dot(normal, normal) > 0.0 ? normalize(normal) : vec3(0.0, 0.0, -1.0);\n"
	"\tout_normals_index = vec4(sky ? vec2(0.5) : normal.xy * 0.5 + 0.5, (sky ? 1.0 : 2.0) / 255.0, 0.0);\n"
	"}\n";

static const char bgpp_bloom_bright_source[] =
	"uniform sampler2D tex0;\n"
	"uniform vec4 pp_minimum;\n"
	"uniform vec4 pp_maximum;\n"
	"uniform vec4 pp_size;\n"
	"layout(location = 0) out vec4 result;\n"
	"void main()\n"
	"{\n"
	"\tvec3 color = texture(tex0, gl_FragCoord.xy / pp_size.xy).rgb;\n"
	"\tcolor = max(pp_minimum.rgb, min(pp_maximum.rgb, color));\n"
	"\tcolor = (color - pp_minimum.rgb) / (pp_maximum.rgb - pp_minimum.rgb);\n"
	"\tresult = vec4(clamp(color, 0.0, 1.0), 1.0);\n"
	"}\n";

static const char bgpp_bloom_blur_source[] =
	"uniform sampler2D tex0;\n"
	"uniform vec4 pp_size;\n"
	"uniform vec4 pp_spread;\n"
	"layout(location = 0) out vec4 result;\n"
	"const vec2 poisson[12] = vec2[12](vec2(-0.326212, -0.40581), vec2(-0.840144, -0.07358),\n"
	"\tvec2(-0.695914, 0.457137), vec2(-0.203345, 0.620716), vec2(0.96234, -0.194983), vec2(0.473434, -0.480026),\n"
	"\tvec2(0.519456, 0.767022), vec2(0.185461, -0.893124), vec2(0.507431, 0.064425), vec2(0.89642, 0.412458),\n"
	"\tvec2(-0.32194, -0.932615), vec2(-0.791559, -0.59771));\n"
	"void main()\n"
	"{\n"
	"\tvec2 uv = gl_FragCoord.xy / pp_size.xy;\n"
	"\tvec2 step = pp_spread.x / pp_size.xy;\n"
	"\tvec3 color = texture(tex0, uv).rgb;\n"
	"\tfor (int i = 0; i < 12; i++)\n"
	"\t\tcolor += texture(tex0, uv + poisson[i] * step).rgb;\n"
	"\tresult = vec4(color / 13.0, 1.0);\n"
	"}\n";

static const char bgpp_bloom_composite_source[] =
	"uniform sampler2D tex0;\n"
	"uniform sampler2D tex1;\n"
	"uniform vec4 pp_size;\n"
	"uniform vec4 pp_amounts;\n"
	"layout(location = 0) out vec4 result;\n"
	"void main()\n"
	"{\n"
	"\tvec3 color = texelFetch(tex0, ivec2(gl_FragCoord.xy), 0).rgb;\n"
	"\tvec3 glow = texture(tex1, gl_FragCoord.xy / pp_size.xy).rgb * 0.5;\n"
	"\tresult = vec4(mix(color, color + glow * pp_amounts.x, pp_amounts.y), 1.0);\n"
	"}\n";

/* a program of its own: the blit's vertex shader and the text's pixel
shader, made now */
static bgfx_program_handle_t bgpp_program_own(const char *fragment, const char *what)
{
	struct bg_shader *vertex = bg_shader_get(bg_blit_vertex_source, FALSE, 0, "blit vertex", TRUE);
	struct bg_shader *pixel = bg_shader_get(fragment, TRUE, 0, what, TRUE);

	if (vertex->failed || pixel->failed || !BGFX_HANDLE_IS_VALID(vertex->shader) || !BGFX_HANDLE_IS_VALID(pixel->shader))
		return (bgfx_program_handle_t)BGFX_INVALID_HANDLE;
	return bgfx_create_program(vertex->shader, pixel->shader, false);
}

/* every identifier of text that is from, made to */
static void bgpp_rename(struct xgpu_text *text, const char *from, const char *to)
{
	struct xgpu_text out = { 0 };
	const char *cursor = text->buffer, *found;
	size_t length = strlen(from);

	if (!cursor)
		return;
	while ((found = strstr(cursor, from)) != NULL)
	{
		char before = found > text->buffer ? found[-1] : ' ', after = found[length];
		BOOL alone = !(isalnum((unsigned char)before) || before == '_') && !(isalnum((unsigned char)after) || after == '_');

		xgpu_text_append(&out, "%.*s%s", (int)(found - cursor), cursor, alone ? to : from);
		cursor = found + length;
	}
	xgpu_text_append(&out, "%s", cursor);
	free(text->buffer);
	*text = out;
}

/* d3d9_shader_glsl's GLSL as the translators' (bg_vulkan_glsl takes it from
there): the bool registers as vec4s (bgfx's uniforms are), the samplers
pp_s0 to pp_s15 at their registers, the vertex inputs as bgfx's attributes
at their locations, and the varyings at the locations of the vertex
shader's list (all of them declared in the pixel shader too, as Direct3D 11
matches them by register) */
static char *bgpp_glsl(const char *source, BOOL pixel, const struct d3d9_glsl_varyings *varyings)
{
	struct xgpu_text text = { 0 };
	const char *line = source, *prefix = pixel ? "p" : "v";
	BOOL declared[16] = { FALSE };
	unsigned long index;
	char from[32], to[32];

	while (*line)
	{
		const char *end = strchr(line, '\n');
		int length = end ? (int)(end - line) : (int)strlen(line);
		char name[64];
		unsigned long location, input;

		if (sscanf(line, "uniform int %63[A-Za-z0-9_][", name) == 1 && strcmp(name + 1, "cb") == 0)
		{
			xgpu_text_append(&text, "uniform vec4 %scb_v4s[%d];\nint %scb[%d];\n", prefix,
				D3D9_SHADER_BOOL_REGISTERS / 4, prefix, D3D9_SHADER_BOOL_REGISTERS);
		}
		else if (sscanf(line, "layout(location = %lu) in vec4 v%lu;", &location, &input) == 2 && !pixel)
		{
			/* (as v<location>_in: bg_vulkan_glsl names it bgfx's attribute) */
			xgpu_text_append(&text, "layout(location = %lu) in vec4 v%lu_in;\n#define pp_v%lu v%lu_in\n", location,
				location, input, location);
		}
		else if ((sscanf(line, "in vec4 vr_%63[A-Za-z0-9_];", name) == 1 ||
			sscanf(line, "out vec4 vr_%63[A-Za-z0-9_];", name) == 1))
		{
			for (index = 0; index < varyings->count; index++)
			{
				if (strcmp(varyings->names[index], name) == 0)
					break;
			}
			if (index >= varyings->count)
			{
				free(text.buffer);
				return NULL;
			}
			declared[index] = TRUE;
			xgpu_text_append(&text, "layout(location = %lu) %.*s\n", BGPP_VARYING_LOCATION + index, length, line);
		}
		else if (strncmp(line, "void main()", 11) == 0 && (length == 11))
		{
			int bit;

			/* (the varyings the pixel shader doesn't read, declared) */
			for (index = 0; pixel && index < varyings->count; index++)
			{
				if (!declared[index])
					xgpu_text_append(&text, "layout(location = %lu) in vec4 vr_%s;\n",
						BGPP_VARYING_LOCATION + index, varyings->names[index]);
			}
			xgpu_text_append(&text, "void main()\n{\n");
			for (bit = 0; bit < D3D9_SHADER_BOOL_REGISTERS; bit++)
				xgpu_text_append(&text, "\t%scb[%d] = int(%scb_v4s[%d].%c);\n", prefix, bit, prefix, bit / 4,
					"xyzw"[bit % 4]);
			/* (main's own brace, next) */
			line += length + (end ? 1 : 0);
			if (*line == '{')
			{
				end = strchr(line, '\n');
				line = end ? end + 1 : line + strlen(line);
			}
			continue;
		}
		else
		{
			xgpu_text_append(&text, "%.*s\n", length, line);
		}
		line += length + (end ? 1 : 0);
	}
	for (index = 0; index < 16; index++)
	{
		snprintf(from, sizeof(from), "s%lu", index);
		snprintf(to, sizeof(to), "pp_s%lu", index);
		bgpp_rename(&text, from, to);
		if (!pixel)
		{
			/* (an input's uses; its own declaration is v<location>_in) */
			snprintf(from, sizeof(from), "v%lu", index);
			snprintf(to, sizeof(to), "pp_v%lu", index);
			bgpp_rename(&text, from, to);
		}
	}
	return text.buffer;
}

/* ---------- targets and views */

static void bgpp_destroy_texture(bgfx_texture_handle_t *texture)
{
	if (BGFX_HANDLE_IS_VALID(*texture))
		bgfx_destroy_texture(*texture);
	*texture = (bgfx_texture_handle_t)BGFX_INVALID_HANDLE;
}

static void bgpp_destroy_framebuffer(bgfx_frame_buffer_handle_t *framebuffer)
{
	if (BGFX_HANDLE_IS_VALID(*framebuffer))
		bgfx_destroy_frame_buffer(*framebuffer);
	*framebuffer = (bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE;
}

static void bgpp_targets_release(void)
{
	int index;

	for (index = 0; index < 3; index++)
	{
		bgpp_destroy_framebuffer(&bgpp.main_framebuffers[index]);
		bgpp_destroy_texture(&bgpp.main_textures[index]);
		bgpp_destroy_texture(&bgpp.gbuffer_textures[index]);
	}
	bgpp_destroy_framebuffer(&bgpp.gbuffer_framebuffer);
	for (index = 0; index < 2; index++)
	{
		bgpp_destroy_framebuffer(&bgpp.secondary_framebuffers[index]);
		bgpp_destroy_texture(&bgpp.secondary_textures[index]);
		bgpp_destroy_framebuffer(&bgpp.bloom_framebuffers[index]);
		bgpp_destroy_texture(&bgpp.bloom_textures[index]);
	}
	ppgl.width = ppgl.height = 0;
}

static bgfx_texture_handle_t bgpp_target_new(bgfx_texture_format_t format, unsigned long width, unsigned long height,
	bgfx_frame_buffer_handle_t *framebuffer)
{
	bgfx_texture_handle_t texture = bgfx_create_texture_2d((uint16_t)width, (uint16_t)height, false, 1, format,
		BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, NULL, 0);

	if (framebuffer)
		*framebuffer = BGFX_HANDLE_IS_VALID(texture) ? bgfx_create_frame_buffer_from_handles(1, &texture, false) :
			(bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE;
	return texture;
}

/* the targets, of the back buffer's size and format (ppgl_targets_fit's);
FALSE if one cannot be made */
static BOOL bgpp_targets_fit(unsigned long width, unsigned long height, BOOL wide)
{
	bgfx_texture_format_t format = wide ? BGFX_TEXTURE_FORMAT_RGBA16F : BGFX_TEXTURE_FORMAT_BGRA8;
	int index;
	BOOL made = TRUE;

	if (ppgl.width == width && ppgl.height == height && bgpp.wide == wide &&
		BGFX_HANDLE_IS_VALID(bgpp.main_framebuffers[0]))
		return TRUE;
	bgpp_targets_release();
	ppgl.width = width;
	ppgl.height = height;
	bgpp.wide = wide;
	for (index = 0; index < 3; index++)
		bgpp.main_textures[index] = bgpp_target_new(format, width, height, &bgpp.main_framebuffers[index]);
	/* (the secondary chain's are 8 bits a channel, as OpenSauce's are) */
	for (index = 0; index < 2; index++)
		bgpp.secondary_textures[index] = bgpp_target_new(BGFX_TEXTURE_FORMAT_BGRA8, width, height,
			&bgpp.secondary_framebuffers[index]);
	bgpp.gbuffer_textures[0] = bgpp_target_new(BGFX_TEXTURE_FORMAT_R32F, width, height, NULL);
	bgpp.gbuffer_textures[1] = bgpp_target_new(BGFX_TEXTURE_FORMAT_RG16F, width, height, NULL);
	bgpp.gbuffer_textures[2] = bgpp_target_new(BGFX_TEXTURE_FORMAT_RGBA8, width, height, NULL);
	if (BGFX_HANDLE_IS_VALID(bgpp.gbuffer_textures[0]) && BGFX_HANDLE_IS_VALID(bgpp.gbuffer_textures[1]) &&
		BGFX_HANDLE_IS_VALID(bgpp.gbuffer_textures[2]))
		bgpp.gbuffer_framebuffer = bgfx_create_frame_buffer_from_handles(3, bgpp.gbuffer_textures, false);
	ppgl.bloom_width = width / PPGL_BLOOM_SHRINK ? width / PPGL_BLOOM_SHRINK : 1;
	ppgl.bloom_height = height / PPGL_BLOOM_SHRINK ? height / PPGL_BLOOM_SHRINK : 1;
	for (index = 0; index < 2; index++)
		bgpp.bloom_textures[index] = bgpp_target_new(format, ppgl.bloom_width, ppgl.bloom_height,
			&bgpp.bloom_framebuffers[index]);
	for (index = 0; index < 3; index++)
		made = made && BGFX_HANDLE_IS_VALID(bgpp.main_framebuffers[index]);
	for (index = 0; index < 2; index++)
		made = made && BGFX_HANDLE_IS_VALID(bgpp.secondary_framebuffers[index]) &&
			BGFX_HANDLE_IS_VALID(bgpp.bloom_framebuffers[index]);
	made = made && BGFX_HANDLE_IS_VALID(bgpp.gbuffer_framebuffer);
	ppgl.gbuffer_serial = -1;
	if (!made)
	{
		bgpp_targets_release();
		ppgl_log_once(6, "its targets cannot be made on bgfx: not drawn");
	}
	return made;
}

/* a pass's view: into framebuffer, all of its width x height, emptied first
or not; FALSE when a frame has no more views */
static BOOL bgpp_view(bgfx_frame_buffer_handle_t framebuffer, unsigned long width, unsigned long height, BOOL clear)
{
	if (bg.view + 2 >= BG_MAXIMUM_VIEWS)
	{
		bg_log_once("more views than a frame has");
		return FALSE;
	}
	bg.view++;
	bgfx_set_view_mode(bg.view, BGFX_VIEW_MODE_SEQUENTIAL);
	bgfx_set_view_frame_buffer(bg.view, framebuffer);
	bgfx_set_view_rect(bg.view, 0, 0, (uint16_t)width, (uint16_t)height, 0.0f, 1.0f);
	bgfx_set_view_clear(bg.view, clear ? BGFX_CLEAR_COLOR : BGFX_CLEAR_NONE, 0, 1.0f, 0);
	bgpp.view_framebuffer = framebuffer;
	bgpp.view_rect[0] = bgpp.view_rect[1] = 0;
	bgpp.view_rect[2] = (unsigned short)width;
	bgpp.view_rect[3] = (unsigned short)height;
	/* (the game's next draw starts a view of its own: bg_view_set) */
	bg.view_framebuffer = framebuffer;
	memcpy(bg.view_rect, bgpp.view_rect, sizeof(bg.view_rect));
	bg.view_used = TRUE;
	if (clear)
		bgfx_touch(bg.view);
	return TRUE;
}

/* a full-screen pass of a program of its own, in the current view (blend:
the fade's, over what is there) */
static void bgpp_full_pass(bgfx_program_handle_t program, BOOL blend)
{
	static const float depth_range[4] = { 0.0f, 1.0f, 0.0f, 0.0f };

	bgfx_set_uniform(bgpp.depth_range, depth_range, 1);
	bgfx_set_vertex_buffer(0, bg.full_quad, 0, 4);
	bgfx_set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_PT_TRISTRIP |
		(blend ? BGFX_STATE_BLEND_ALPHA : 0), 0);
	bgfx_submit(bg.view, program, 0, BGFX_DISCARD_ALL);
}

/* source copied into framebuffer, every pixel (a blit, but between targets
of other formats too, and from a multisampled one's resolved pixels) */
static BOOL bgpp_copy(bgfx_texture_handle_t source, bgfx_frame_buffer_handle_t framebuffer)
{
	if (!bgpp_view(framebuffer, ppgl.width, ppgl.height, FALSE))
		return FALSE;
	bgfx_set_texture(0, bg.samplers[0], source, BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	bgpp_full_pass(bgpp.copy, FALSE);
	return TRUE;
}

/* ---------- preparation */

static BOOL bgpp_prepare(void)
{
	static const unsigned int black = 0;
	int index;

	if (bgpp.tried)
		return !bgpp.failed;
	bgpp.tried = TRUE;
	/* (no targets yet: a handle of zeros is bgfx's first, not none) */
	for (index = 0; index < 3; index++)
	{
		bgpp.main_textures[index] = bgpp.gbuffer_textures[index] = (bgfx_texture_handle_t)BGFX_INVALID_HANDLE;
		bgpp.main_framebuffers[index] = (bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE;
	}
	for (index = 0; index < 2; index++)
	{
		bgpp.secondary_textures[index] = bgpp.bloom_textures[index] = (bgfx_texture_handle_t)BGFX_INVALID_HANDLE;
		bgpp.secondary_framebuffers[index] = bgpp.bloom_framebuffers[index] =
			(bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE;
	}
	bgpp.gbuffer_framebuffer = (bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE;
	/* (the uniforms before the shaders, which Vulkan's finds them by) */
	bgpp.vc = bgfx_create_uniform("vc", BGFX_UNIFORM_TYPE_VEC4, D3D9_SHADER_FLOAT_REGISTERS);
	bgpp.pc = bgfx_create_uniform("pc", BGFX_UNIFORM_TYPE_VEC4, D3D9_SHADER_FLOAT_REGISTERS);
	bgpp.vci = bgfx_create_uniform("vci", BGFX_UNIFORM_TYPE_VEC4, D3D9_SHADER_INT_REGISTERS);
	bgpp.pci = bgfx_create_uniform("pci", BGFX_UNIFORM_TYPE_VEC4, D3D9_SHADER_INT_REGISTERS);
	bgpp.vcb = bgfx_create_uniform("vcb_v4s", BGFX_UNIFORM_TYPE_VEC4, D3D9_SHADER_BOOL_REGISTERS / 4);
	bgpp.pcb = bgfx_create_uniform("pcb_v4s", BGFX_UNIFORM_TYPE_VEC4, D3D9_SHADER_BOOL_REGISTERS / 4);
	bgpp.pixel_fix = bgfx_create_uniform("d3d9_pixel_fix", BGFX_UNIFORM_TYPE_VEC4, 1);
	for (index = 0; index < D3D9_SHADER_SAMPLERS; index++)
	{
		char name[16];

		snprintf(name, sizeof(name), "pp_s%d", index);
		bgpp.samplers[index] = bgfx_create_uniform(name, BGFX_UNIFORM_TYPE_SAMPLER, 1);
	}
	bgpp.size = bgfx_create_uniform("pp_size", BGFX_UNIFORM_TYPE_VEC4, 1);
	bgpp.clip = bgfx_create_uniform("pp_clip", BGFX_UNIFORM_TYPE_VEC4, 1);
	bgpp.world_from_clip = bgfx_create_uniform("pp_world_from_clip", BGFX_UNIFORM_TYPE_VEC4, 4);
	bgpp.previous_clip_from_world = bgfx_create_uniform("pp_previous_clip_from_world", BGFX_UNIFORM_TYPE_VEC4, 4);
	bgpp.minimum = bgfx_create_uniform("pp_minimum", BGFX_UNIFORM_TYPE_VEC4, 1);
	bgpp.maximum = bgfx_create_uniform("pp_maximum", BGFX_UNIFORM_TYPE_VEC4, 1);
	bgpp.amounts = bgfx_create_uniform("pp_amounts", BGFX_UNIFORM_TYPE_VEC4, 1);
	bgpp.spread = bgfx_create_uniform("pp_spread", BGFX_UNIFORM_TYPE_VEC4, 1);
	bgpp.fade_amount = bgfx_create_uniform("pp_fade", BGFX_UNIFORM_TYPE_VEC4, 1);
	bgpp.depth_range = bgfx_create_uniform("depth_range", BGFX_UNIFORM_TYPE_VEC4, 1);
	bgpp.copy = bgpp_program_own(bgpp_copy_source, "OpenSauce post-processing copy");
	bgpp.fade = bgpp_program_own(bgpp_fade_source, "OpenSauce post-processing fade");
	bgpp.gbuffer = bgpp_program_own(bgpp_gbuffer_source, "OpenSauce post-processing G-buffer");
	bgpp.bloom[0] = bgpp_program_own(bgpp_bloom_bright_source, "OpenSauce bloom bright pass");
	bgpp.bloom[1] = bgpp_program_own(bgpp_bloom_blur_source, "OpenSauce bloom blur");
	bgpp.bloom[2] = bgpp_program_own(bgpp_bloom_composite_source, "OpenSauce bloom composite");
	bgpp.black = bgfx_create_texture_2d(1, 1, false, 1, BGFX_TEXTURE_FORMAT_BGRA8, BGFX_TEXTURE_NONE,
		bgfx_copy(&black, 4), 0);
	{
		unsigned int faces[6] = { 0 };

		bgpp.black_cube = bgfx_create_texture_cube(1, false, 1, BGFX_TEXTURE_FORMAT_BGRA8, BGFX_TEXTURE_NONE,
			bgfx_copy(faces, sizeof(faces)), 0);
	}
	bgpp.black_volume = bgfx_create_texture_3d(1, 1, 1, false, BGFX_TEXTURE_FORMAT_BGRA8, BGFX_TEXTURE_NONE,
		bgfx_copy(&black, 4), 0);
	if (!BGFX_HANDLE_IS_VALID(bgpp.copy) || !BGFX_HANDLE_IS_VALID(bgpp.fade) || !BGFX_HANDLE_IS_VALID(bgpp.gbuffer) ||
		!BGFX_HANDLE_IS_VALID(bgpp.bloom[0]) || !BGFX_HANDLE_IS_VALID(bgpp.bloom[1]) ||
		!BGFX_HANDLE_IS_VALID(bgpp.bloom[2]) || !BGFX_HANDLE_IS_VALID(bg.full_quad))
	{
		bgpp.failed = TRUE;
		platform_log("OpenSauce post-processing: its own programs do not build on bgfx, so it is not drawn");
	}
	return !bgpp.failed;
}

/* ---------- effects */

static struct bgpp_effect *bgpp_effect_get(long index)
{
	struct ppgl_effect *effect = ppgl_effect_get(index);
	struct bgpp_effect *own;

	if (!effect)
		return NULL;
	own = &bgpp.effects[index];
	if (!own->resources || own->parameter_count != effect->effect.parameter_count)
	{
		unsigned long parameter;

		free(own->resources);
		free(own->handles);
		own->parameter_count = effect->effect.parameter_count;
		own->resources = calloc(own->parameter_count + 1, sizeof(*own->resources));
		own->handles = calloc(own->parameter_count + 1, sizeof(*own->handles));
		if (!own->resources || !own->handles)
		{
			free(own->resources);
			free(own->handles);
			own->resources = NULL;
			own->handles = NULL;
			own->parameter_count = 0;
			return NULL;
		}
		for (parameter = 0; parameter <= own->parameter_count; parameter++)
			own->handles[parameter] = (bgfx_texture_handle_t)BGFX_INVALID_HANDLE;
	}
	return own;
}

static void bgpp_effect_release(long index)
{
	struct bgpp_effect *own = &bgpp.effects[index];
	int pass;

	for (pass = 0; pass < PPGL_MAXIMUM_PASSES; pass++)
	{
		if (bg.ready && own->programs[pass].tried && BGFX_HANDLE_IS_VALID(own->programs[pass].program))
			bgfx_destroy_program(own->programs[pass].program);
	}
	free(own->resources);
	free(own->handles);
	memset(own, 0, sizeof(*own));
}

/* a texture parameter set to one of the targets */
static void bgpp_texture_set(struct ppgl_effect *effect, struct bgpp_effect *own, long parameter,
	bgfx_texture_handle_t texture)
{
	if (parameter < 0 || (unsigned long)parameter >= effect->effect.parameter_count)
		return;
	own->resources[parameter] = NULL;
	own->handles[parameter] = texture;
}

/* a pass's program, made the first time it is drawn (ppgl_program_get's) */
static struct bgpp_program *bgpp_program_get(struct ppgl_effect *effect, struct bgpp_effect *own, int pass_index)
{
	struct bgpp_program *program = &own->programs[pass_index];
	const struct d3d9_pass *pass = &effect->effect.techniques[effect->technique].passes[pass_index];
	struct d3d9_glsl_varyings inputs, outputs;
	unsigned char vertex_kinds[D3D9_SHADER_SAMPLERS];
	char *pixel_text, *vertex_text, *pixel_glsl = NULL, *vertex_glsl = NULL, error[256], what[128];
	struct bg_shader *vertex = NULL, *pixel = NULL;

	if (program->tried)
		return program->failed ? NULL : program;
	program->tried = TRUE;
	program->failed = TRUE;
	program->program = (bgfx_program_handle_t)BGFX_INVALID_HANDLE;
	snprintf(what, sizeof(what), "OpenSauce effect '%s' pass %d", effect->name, pass_index);
	if (pass->vertex_shader < 0 || pass->pixel_shader < 0)
	{
		platform_log("OpenSauce post-processing: %s has no vertex or pixel shader", what);
		return NULL;
	}
	memset(&inputs, 0, sizeof(inputs));
	memset(&outputs, 0, sizeof(outputs));
	pixel_text = d3d9_shader_glsl(&effect->effect.shaders[pass->pixel_shader], NULL, &inputs,
		&program->pixel_float_count, program->pixel_samplers, error, sizeof(error));
	vertex_text = pixel_text ? d3d9_shader_glsl(&effect->effect.shaders[pass->vertex_shader], &inputs, &outputs,
		&program->vertex_float_count, vertex_kinds, error, sizeof(error)) : NULL;
	if (!pixel_text || !vertex_text)
	{
		platform_log("OpenSauce post-processing: %s does not translate: %s", what, error);
		free(pixel_text);
		free(vertex_text);
		return NULL;
	}
	vertex_glsl = bgpp_glsl(vertex_text, FALSE, &outputs);
	pixel_glsl = bgpp_glsl(pixel_text, TRUE, &outputs);
	free(pixel_text);
	free(vertex_text);
	if (vertex_glsl && pixel_glsl)
	{
		vertex = bg_shader_get(vertex_glsl, FALSE, 0, what, TRUE);
		pixel = bg_shader_get(pixel_glsl, TRUE, 0, what, TRUE);
	}
	free(vertex_glsl);
	free(pixel_glsl);
	if (!vertex || !pixel || vertex->failed || pixel->failed || !BGFX_HANDLE_IS_VALID(vertex->shader) ||
		!BGFX_HANDLE_IS_VALID(pixel->shader))
	{
		platform_log("OpenSauce post-processing: %s does not build on bgfx", what);
		return NULL;
	}
	program->program = bgfx_create_program(vertex->shader, pixel->shader, false);
	if (!BGFX_HANDLE_IS_VALID(program->program))
		return NULL;
	program->uniform_bytes = ((vertex->uniform_size + 255) & ~255UL) + ((pixel->uniform_size + 255) & ~255UL);
	program->failed = FALSE;
	return program;
}

/* a sampler's states as bgfx's flags (ppgl_sampler_get's) */
static uint32_t bgpp_sampler_flags(const struct d3d9_parameter *sampler, BOOL mipmapped)
{
	static const uint32_t addresses[6] =
	{
		0, 0, BGFX_SAMPLER_U_MIRROR, BGFX_SAMPLER_U_CLAMP, BGFX_SAMPLER_U_BORDER, BGFX_SAMPLER_U_MIRROR
	};
	unsigned long states[3], magnify, minify, mip, border;
	uint32_t flags = 0;
	int axis;

	states[0] = sampler->sampler_states[_d3d9_sampler_address_u];
	states[1] = sampler->sampler_states[_d3d9_sampler_address_v];
	states[2] = sampler->sampler_states[_d3d9_sampler_address_w];
	magnify = sampler->sampler_states[_d3d9_sampler_mag_filter];
	minify = sampler->sampler_states[_d3d9_sampler_min_filter];
	mip = sampler->sampler_states[_d3d9_sampler_mip_filter];
	border = sampler->sampler_states[_d3d9_sampler_border_color];
	/* (Direct3D 9's defaults where the effect sets none: wrap, point, no
	mipmaps) */
	for (axis = 0; axis < 3; axis++)
	{
		unsigned long address = states[axis] == D3D9_SAMPLER_STATE_UNSET || states[axis] > 5 ? 1 : states[axis];

		flags |= addresses[address] << (axis * 2);
	}
	magnify = magnify == D3D9_SAMPLER_STATE_UNSET ? 1 : magnify;
	minify = minify == D3D9_SAMPLER_STATE_UNSET ? 1 : minify;
	mip = mip == D3D9_SAMPLER_STATE_UNSET || !mipmapped ? 0 : mip;
	border = border == D3D9_SAMPLER_STATE_UNSET ? 0 : border;
	if (minify < 2)
		flags |= BGFX_SAMPLER_MIN_POINT;
	if (magnify < 2)
		flags |= BGFX_SAMPLER_MAG_POINT;
	if (mip < 2)
		flags |= BGFX_SAMPLER_MIP_POINT;
	{
		unsigned long luminance = ((border >> 16) & 0xff) + ((border >> 8) & 0xff) + (border & 0xff);

		flags |= BGFX_SAMPLER_BORDER_COLOR((border >> 24) < 0x80 ? 0 : luminance >= 3 * 0x80 ? 2 : 1);
	}
	return flags;
}

/* the texture a sampler register of a shader reads, set at its stage
(ppgl_sampler_bind's) */
static void bgpp_sampler_bind(struct ppgl_effect *effect, struct bgpp_effect *own, const struct d3d9_shader *shader,
	unsigned long reg, unsigned char kind)
{
	unsigned long index;
	bgfx_texture_handle_t black = kind == 3 ? bgpp.black_cube : kind == 4 ? bgpp.black_volume : bgpp.black;

	for (index = 0; index < shader->constant_count; index++)
	{
		const struct d3d9_constant *constant = &shader->constants[index];
		long sampler, texture;

		if (constant->regset != _d3d9_regset_sampler || reg < constant->reg || reg >= (unsigned long)constant->reg +
			(constant->count ? constant->count : 1))
		{
			continue;
		}
		sampler = d3d9_effect_parameter_find(&effect->effect, constant->name);
		texture = sampler >= 0 ? effect->effect.parameters[sampler].texture : -1;
		if (sampler >= 0 && texture >= 0 && (unsigned long)texture < own->parameter_count)
		{
			bgfx_texture_handle_t handle = own->handles[texture];
			BOOL mipmapped = FALSE;
			unsigned char has = 2;

			if (own->resources[texture])
			{
				struct xgpu_texture_description description;
				long override = -1;

				handle = (bgfx_texture_handle_t)BGFX_INVALID_HANDLE;
				if (!bg_texture_get(own->resources[texture], NULL, &description, &handle, &mipmapped, &override) ||
					override >= 0)
					handle = (bgfx_texture_handle_t)BGFX_INVALID_HANDLE;
				has = description.cube_map ? 3 : description.depth > 1 ? 4 : 2;
			}
			if (BGFX_HANDLE_IS_VALID(handle) && has == kind)
			{
				bgfx_set_texture((uint8_t)reg, bgpp.samplers[reg], handle,
					bgpp_sampler_flags(&effect->effect.parameters[sampler], mipmapped));
				return;
			}
		}
		break;
	}
	/* (no texture: black, as an unset texture reads in Direct3D 9) */
	if (kind != 2)
		ppgl_log_once(2, "an effect samples a cube or volume texture it has none of: it reads black");
	bgfx_set_texture((uint8_t)reg, bgpp.samplers[reg], black, BGFX_SAMPLER_POINT);
}

/* a pass's quad (ppgl_quad's), into transient buffers; the index count, 0
if the frame has no room */
static int bgpp_quad(const short tessellation[2], const float x_bounds[2], const float y_bounds[2])
{
	enum { QUAD_MAXIMUM = 64 };
	int tx = tessellation[0], ty = tessellation[1], x, y, count = 0, vertex = 0;
	float x_increment, y_increment, width = (float)ppgl.width, height = (float)ppgl.height;
	bgfx_transient_vertex_buffer_t vertices;
	bgfx_transient_index_buffer_t indices;
	uint16_t *index_data;

	tx = tx < 1 ? 1 : tx > QUAD_MAXIMUM ? QUAD_MAXIMUM : tx;
	ty = ty < 1 ? 1 : ty > QUAD_MAXIMUM ? QUAD_MAXIMUM : ty;
	x_increment = (x_bounds[1] - x_bounds[0]) / (float)tx;
	y_increment = (y_bounds[1] - y_bounds[0]) / (float)ty;
	if (bgfx_get_avail_transient_vertex_buffer((uint32_t)((tx + 1) * (ty + 1)), &bg.layout) <
		(uint32_t)((tx + 1) * (ty + 1)) || bgfx_get_avail_transient_index_buffer((uint32_t)(tx * ty * 6), false) <
		(uint32_t)(tx * ty * 6))
		return 0;
	bgfx_alloc_transient_vertex_buffer(&vertices, (uint32_t)((tx + 1) * (ty + 1)), &bg.layout);
	bgfx_alloc_transient_index_buffer(&indices, (uint32_t)(tx * ty * 6), false);
	memset(vertices.data, 0, (size_t)((tx + 1) * (ty + 1)) * BG_VERTEX_BYTES);
	for (y = 0; y <= ty; y++)
	{
		for (x = 0; x <= tx; x++)
		{
			/* (attribute 0 the position, 1 to 4 the texture coordinates: the
			locations d3d9_shader_glsl gives POSITION0 and TEXCOORD0 to 3) */
			float *v = (float *)(vertices.data + (size_t)vertex++ * BG_VERTEX_BYTES);

			v[0] = (x_bounds[0] + (float)x * x_increment) * width - 0.5f;
			v[1] = -((y_bounds[0] + (float)y * y_increment) * height - 0.5f);
			v[2] = 10.0f;
			v[3] = 1.0f;
			v[4] = (float)x / (float)tx;
			v[5] = (float)y / (float)ty;
			v[8] = x_bounds[0] + (float)x * x_increment;
			v[9] = y_bounds[0] + (float)y * y_increment;
			v[12] = x_bounds[0];
			v[13] = x_bounds[1];
			v[14] = y_bounds[0];
			v[15] = y_bounds[1];
		}
	}
	index_data = (uint16_t *)indices.data;
	for (y = 0; y < ty; y++)
	{
		for (x = 0; x < tx; x++)
		{
			uint16_t top_left = (uint16_t)(y * (tx + 1) + x), top_right = (uint16_t)(top_left + 1);
			uint16_t bottom_left = (uint16_t)(top_left + tx + 1), bottom_right = (uint16_t)(bottom_left + 1);

			index_data[count++] = bottom_left;
			index_data[count++] = top_left;
			index_data[count++] = top_right;
			index_data[count++] = top_right;
			index_data[count++] = bottom_right;
			index_data[count++] = bottom_left;
		}
	}
	bgfx_set_transient_vertex_buffer(0, &vertices, 0, (uint32_t)vertex);
	bgfx_set_transient_index_buffer(&indices, 0, (uint32_t)count);
	return count;
}

/* the G-buffer, once a frame, from the depth buffer as it is now
(ppgl_gbuffer_make's) */
static void bgpp_gbuffer_make(void)
{
	struct bg_target *depth;
	float clip_from_world[16], world_from_clip[16], previous[16], clip[4], size[4];
	int row, column;

	if (ppgl.gbuffer_serial == ppgl.frame.frame_serial)
		return;
	ppgl.gbuffer_serial = ppgl.frame.frame_serial;
	depth = bg_target_get(&device.depth_buffer);
	if (depth && depth->depth && depth->samples > 1 && !depth->readable && !bg_vulkan && !bg_d3d11 &&
		!bg_depth_readable)
	{
		/* (a multisampled depth buffer is write-only on bgfx, bg_samples_flags:
		on Metal, readable from the next frame on) */
		bg_depth_readable = TRUE;
		platform_log("OpenSauce post-processing: the multisampled depth buffer is made readable for its G-buffer");
	}
	if (!depth || !depth->depth || depth->pixel_width != ppgl.width || depth->pixel_height != ppgl.height ||
		(depth->samples > 1 && !depth->readable))
	{
		if (!(depth && depth->samples > 1 && bg_depth_readable))
			ppgl_log_once(0, depth && depth->samples > 1 ? "the depth buffer is multisampled and can't be read on this "
				"renderer: the G-buffer is left empty" :
				"the depth buffer is not the back buffer's size: the G-buffer is left empty");
		if (bgpp_view(bgpp.gbuffer_framebuffer, ppgl.width, ppgl.height, FALSE))
		{
			bgfx_set_view_clear(bg.view, BGFX_CLEAR_COLOR, 0xff000000, 1.0f, 0);
			bgfx_touch(bg.view);
		}
		return;
	}
	for (row = 0; row < 4; row++)
	{
		for (column = 0; column < 4; column++)
		{
			clip_from_world[row * 4 + column] = ppgl.frame.clip_from_world[row][column];
			previous[row * 4 + column] = ppgl.frame.previous_clip_from_world[row][column];
		}
	}
	if (!ppgl_invert(clip_from_world, world_from_clip))
		memset(world_from_clip, 0, sizeof(world_from_clip));
	clip[0] = ppgl.frame.near_clip;
	clip[1] = ppgl.frame.far_clip;
	clip[2] = ppgl.frame.projection_scale[0] != 0.0f ? ppgl.frame.projection_scale[0] : 1.0f;
	clip[3] = ppgl.frame.projection_scale[1] != 0.0f ? ppgl.frame.projection_scale[1] : 1.0f;
	size[0] = (float)ppgl.width;
	size[1] = (float)ppgl.height;
	size[2] = size[3] = 0.0f;
	if (!bgpp_view(bgpp.gbuffer_framebuffer, ppgl.width, ppgl.height, FALSE))
		return;
	bgfx_set_uniform(bgpp.world_from_clip, world_from_clip, 4);
	bgfx_set_uniform(bgpp.previous_clip_from_world, previous, 4);
	bgfx_set_uniform(bgpp.clip, clip, 1);
	bgfx_set_uniform(bgpp.size, size, 1);
	bgfx_set_texture(0, bg.samplers[0], depth->texture, BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	bgpp_full_pass(bgpp.gbuffer, FALSE);
}

/* ---------- the stage, as the public code's */

static int bgpp_available(void)
{
	return bg.ready && !bgpp.failed;
}

static void bgpp_unload(void)
{
	long index;

	for (index = 0; index < PPGL_MAXIMUM_EFFECTS; index++)
		bgpp_effect_release(index);
}

static void bgpp_parameter_texture(long effect_index, const char *name, const void *texture)
{
	struct ppgl_effect *effect = ppgl_effect_get(effect_index);
	struct bgpp_effect *own = bgpp_effect_get(effect_index);
	long parameter;

	if (!effect || !own)
		return;
	parameter = d3d9_effect_parameter_find(&effect->effect, name);
	if (parameter < 0 || (unsigned long)parameter >= own->parameter_count)
		return;
	own->resources[parameter] = (const DWORD *)texture;
	own->handles[parameter] = (bgfx_texture_handle_t)BGFX_INVALID_HANDLE;
}

static int bgpp_stage_begin(const struct opensauce_pp_frame *frame)
{
	struct bg_target *target;
	unsigned short rect[4];

	if (!bgpp_prepare())
		return FALSE;
	ppgl_log_once(5, "a stage begun on bgfx (the map has post-processing)");
	target = bg_target_get(&device.back_buffer);
	if (!target || target->pixel_width < 8 || target->pixel_height < 8)
		return FALSE;
	ppgl.frame = *frame;
	/* (the 3D view all of the back buffer: one window; split screen's
	windows are left as they are) */
	if (!bg_window_rect(target, frame->x0, frame->y0, frame->x1, frame->y1, rect) || rect[0] > 2 || rect[1] > 2 ||
		rect[0] + rect[2] < (long)target->pixel_width - 2 || rect[1] + rect[3] < (long)target->pixel_height - 2)
	{
		ppgl_log_once(3, "the 3D view is not the whole screen (split screen): not drawn there");
		return FALSE;
	}
	if (!bgpp_targets_fit(target->pixel_width, target->pixel_height, target->wide))
		return FALSE;
	bgpp.back_buffer = target;
	if (!bgpp_copy(target->texture, bgpp.main_framebuffers[0]))
		return FALSE;
	ppgl.main_chain.scene = 0;
	ppgl.main_chain.current = 1;
	ppgl.main_chain.next = 2;
	if (!ppgl.secondary_chain.current && !ppgl.secondary_chain.next)
	{
		ppgl.secondary_chain.current = 0;
		ppgl.secondary_chain.next = 1;
	}
	bgpp_gbuffer_make();
	ppgl.stage_active = TRUE;
	return TRUE;
}

static void bgpp_effect_end(int drew, float fade)
{
	if (!ppgl.stage_active || !drew)
		return;
	if (fade < 1.0f)
	{
		/* (c_shader_instance_fade::Render: the result over the scene as it
		was, by the fade) */
		float amount[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		ppgl_chain_scene_to_last(&ppgl.main_chain);
		ppgl_chain_flip(&ppgl.main_chain);
		amount[0] = fade < 0.0f ? 0.0f : fade;
		if (bgpp_view(bgpp.main_framebuffers[ppgl.main_chain.current], ppgl.width, ppgl.height, FALSE))
		{
			bgfx_set_uniform(bgpp.fade_amount, amount, 1);
			bgfx_set_texture(0, bg.samplers[0], bgpp.main_textures[ppgl.main_chain.scene],
				BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
			bgpp_full_pass(bgpp.fade, TRUE);
		}
		ppgl_chain_flip(&ppgl.main_chain);
	}
	ppgl_chain_scene_to_last(&ppgl.main_chain);
}

static int bgpp_shader_draw(long effect_index, const struct opensauce_pp_pass *passes, int pass_count,
	const short tessellation[2], const float x_bounds[2], const float y_bounds[2])
{
	struct ppgl_effect *effect = ppgl_effect_get(effect_index);
	struct bgpp_effect *own = bgpp_effect_get(effect_index);
	const struct d3d9_technique *technique;
	float floats[D3D9_SHADER_FLOAT_REGISTERS * 4], int_floats[D3D9_SHADER_INT_REGISTERS * 4];
	int ints[D3D9_SHADER_INT_REGISTERS * 4], bools[D3D9_SHADER_BOOL_REGISTERS];
	float bool_floats[D3D9_SHADER_BOOL_REGISTERS];
	float values[16];
	int register_index, pass_index;

	if (!effect || !own || !ppgl.stage_active)
		return FALSE;
	technique = &effect->effect.techniques[effect->technique];
	if ((unsigned long)pass_count != technique->pass_count)
	{
		ppgl_log_once(4, "an effect's technique has %lu passes, its tag %d: drawn by its tag's", technique->pass_count,
			pass_count);
		if ((unsigned long)pass_count > technique->pass_count)
			pass_count = (int)technique->pass_count;
	}
	/* the standard values (c_shader_postprocess::SetVariables) */
	memset(values, 0, sizeof(values));
	values[0] = 2.0f / (float)ppgl.width;
	values[5] = 2.0f / (float)ppgl.height;
	values[10] = 1.0f / 9.0f;
	values[12] = -1.0f;
	values[13] = 1.0f;
	values[14] = -1.0f / 9.0f;
	values[15] = 1.0f;
	ppgl_semantic_set(effect, _ppgl_semantic_ortho, values, 16);
	values[0] = (float)ppgl.width;
	values[1] = (float)ppgl.height;
	ppgl_semantic_set(effect, _ppgl_semantic_scene_size, values, 2);
	values[0] = 1.0f / (float)ppgl.width;
	values[1] = 1.0f / (float)ppgl.height;
	ppgl_semantic_set(effect, _ppgl_semantic_pixel_size, values, 2);
	ppgl_semantic_set(effect, _ppgl_semantic_hud_scale, ppgl.frame.hud_scale, 2);
	ppgl_semantic_set(effect, _ppgl_semantic_near, &ppgl.frame.near_clip, 1);
	ppgl_semantic_set(effect, _ppgl_semantic_far, &ppgl.frame.far_clip, 1);
	bgpp_texture_set(effect, own, effect->semantics[_ppgl_semantic_depth], bgpp.gbuffer_textures[0]);
	bgpp_texture_set(effect, own, effect->semantics[_ppgl_semantic_velocity], bgpp.gbuffer_textures[1]);
	bgpp_texture_set(effect, own, effect->semantics[_ppgl_semantic_normals], bgpp.gbuffer_textures[2]);
	bgpp_texture_set(effect, own, effect->semantics[_ppgl_semantic_index], bgpp.gbuffer_textures[2]);
	for (pass_index = 0; pass_index < pass_count; pass_index++)
	{
		const struct opensauce_pp_pass *pass = &passes[pass_index];
		struct ppgl_chain *chain = pass->render_chain == 1 ? &ppgl.secondary_chain : &ppgl.main_chain;
		bgfx_frame_buffer_handle_t *chain_framebuffers = pass->render_chain == 1 ? bgpp.secondary_framebuffers :
			bgpp.main_framebuffers;
		struct bgpp_program *program = bgpp_program_get(effect, own, pass_index);
		const struct d3d9_pass *effect_pass = &technique->passes[pass_index];
		const struct d3d9_shader *vertex_shader, *pixel_shader;
		float fix[4];
		unsigned long sampler;

		if (!program)
			return FALSE;
		if (bg.uniform_bytes + program->uniform_bytes > BG_UNIFORM_BUDGET)
		{
			bg_log_once("more OpenSauce post-processing than a frame's uniform space holds");
			return FALSE;
		}
		vertex_shader = &effect->effect.shaders[effect_pass->vertex_shader];
		pixel_shader = &effect->effect.shaders[effect_pass->pixel_shader];
		/* the chains' textures as this pass reads them */
		bgpp_texture_set(effect, own, effect->semantics[_ppgl_semantic_scene],
			bgpp.main_textures[ppgl.main_chain.scene]);
		bgpp_texture_set(effect, own, effect->semantics[_ppgl_semantic_source],
			bgpp.main_textures[ppgl.main_chain.first_render ? ppgl.main_chain.scene : ppgl.main_chain.next]);
		if (ppgl.secondary_chain.first_render &&
			!PPGL_TEST_FLAG(pass->flags, _opensauce_pp_pass_clear_buffer_texture_bit))
			bgpp_texture_set(effect, own, effect->semantics[_ppgl_semantic_buffer],
				bgpp.main_textures[ppgl.main_chain.scene]);
		else
			bgpp_texture_set(effect, own, effect->semantics[_ppgl_semantic_buffer],
				bgpp.secondary_textures[ppgl.secondary_chain.next]);
		if (PPGL_TEST_FLAG(pass->flags, _opensauce_pp_pass_copy_scene_to_target_bit) &&
			!bgpp_copy(bgpp.main_textures[ppgl.main_chain.scene], chain_framebuffers[chain->current]))
			return FALSE;
		if (PPGL_TEST_FLAG(pass->flags, _opensauce_pp_pass_clear_buffer_texture_bit))
		{
			if (!bgpp_view(bgpp.secondary_framebuffers[ppgl.secondary_chain.next], ppgl.width, ppgl.height, TRUE))
				return FALSE;
		}
		if (!bgpp_view(chain_framebuffers[chain->current], ppgl.width, ppgl.height,
			PPGL_TEST_FLAG(pass->flags, _opensauce_pp_pass_clear_target_bit)))
			return FALSE;
		if (!bgpp_quad(tessellation, x_bounds, y_bounds))
		{
			bg_log_once("no transient vertices left for OpenSauce post-processing");
			return FALSE;
		}
		ppgl_shader_registers(effect, vertex_shader, floats, ints, bools);
		bgfx_set_uniform(bgpp.vc, floats, (uint16_t)(program->vertex_float_count ? program->vertex_float_count : 1));
		for (register_index = 0; register_index < D3D9_SHADER_INT_REGISTERS * 4; register_index++)
			int_floats[register_index] = (float)ints[register_index];
		bgfx_set_uniform(bgpp.vci, int_floats, D3D9_SHADER_INT_REGISTERS);
		for (register_index = 0; register_index < D3D9_SHADER_BOOL_REGISTERS; register_index++)
			bool_floats[register_index] = (float)bools[register_index];
		bgfx_set_uniform(bgpp.vcb, bool_floats, D3D9_SHADER_BOOL_REGISTERS / 4);
		ppgl_shader_registers(effect, pixel_shader, floats, ints, bools);
		bgfx_set_uniform(bgpp.pc, floats, (uint16_t)(program->pixel_float_count ? program->pixel_float_count : 1));
		for (register_index = 0; register_index < D3D9_SHADER_INT_REGISTERS * 4; register_index++)
			int_floats[register_index] = (float)ints[register_index];
		bgfx_set_uniform(bgpp.pci, int_floats, D3D9_SHADER_INT_REGISTERS);
		for (register_index = 0; register_index < D3D9_SHADER_BOOL_REGISTERS; register_index++)
			bool_floats[register_index] = (float)bools[register_index];
		bgfx_set_uniform(bgpp.pcb, bool_floats, D3D9_SHADER_BOOL_REGISTERS / 4);
		/* (Direct3D 9's pixel centers; bgfx's clip space is Direct3D's on
		every renderer, so the rows are as they are) */
		fix[0] = 1.0f / (float)ppgl.width;
		fix[1] = 1.0f / (float)ppgl.height;
		fix[2] = 1.0f;
		fix[3] = 0.0f;
		bgfx_set_uniform(bgpp.pixel_fix, fix, 1);
		{
			static const float depth_range[4] = { 0.0f, 1.0f, 0.0f, 0.0f };

			bgfx_set_uniform(bgpp.depth_range, depth_range, 1);
		}
		for (sampler = 0; sampler < D3D9_SHADER_SAMPLERS; sampler++)
		{
			if (program->pixel_samplers[sampler])
				bgpp_sampler_bind(effect, own, pixel_shader, sampler, program->pixel_samplers[sampler]);
		}
		bgfx_set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A, 0);
		bgfx_submit(bg.view, program->program, 0, BGFX_DISCARD_ALL);
		bg.uniform_bytes += program->uniform_bytes;
		chain->first_render = FALSE;
		ppgl_chain_flip(chain);
	}
	return TRUE;
}

static int bgpp_bloom(float size, float exposure, float mix_amount, const float minimum[3], const float maximum[3])
{
	float small_size[4], full_size[4], amounts[4], minimum_color[4], maximum_color[4], spread[4];
	const uint32_t linear = BGFX_SAMPLER_U_MIRROR | BGFX_SAMPLER_V_MIRROR;
	const uint32_t point = BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

	if (!ppgl.stage_active)
		return FALSE;
	memset(small_size, 0, sizeof(small_size));
	memset(full_size, 0, sizeof(full_size));
	memset(amounts, 0, sizeof(amounts));
	memset(minimum_color, 0, sizeof(minimum_color));
	memset(maximum_color, 0, sizeof(maximum_color));
	memset(spread, 0, sizeof(spread));
	memcpy(minimum_color, minimum, sizeof(float) * 3);
	memcpy(maximum_color, maximum, sizeof(float) * 3);
	small_size[0] = (float)ppgl.bloom_width;
	small_size[1] = (float)ppgl.bloom_height;
	full_size[0] = (float)ppgl.width;
	full_size[1] = (float)ppgl.height;
	spread[0] = size;
	halo_opensauce_pp_effect_begin();
	/* the bright pass, into the eighth */
	if (!bgpp_view(bgpp.bloom_framebuffers[0], ppgl.bloom_width, ppgl.bloom_height, FALSE))
		return FALSE;
	bgfx_set_uniform(bgpp.minimum, minimum_color, 1);
	bgfx_set_uniform(bgpp.maximum, maximum_color, 1);
	bgfx_set_uniform(bgpp.size, small_size, 1);
	bgfx_set_texture(0, bg.samplers[0], bgpp.main_textures[ppgl.main_chain.scene], linear);
	bgpp_full_pass(bgpp.bloom[0], FALSE);
	/* the two blurs (their spread the bloom's size in the eighth's texels) */
	if (!bgpp_view(bgpp.bloom_framebuffers[1], ppgl.bloom_width, ppgl.bloom_height, FALSE))
		return FALSE;
	bgfx_set_uniform(bgpp.size, small_size, 1);
	bgfx_set_uniform(bgpp.spread, spread, 1);
	bgfx_set_texture(0, bg.samplers[0], bgpp.bloom_textures[0], linear);
	bgpp_full_pass(bgpp.bloom[1], FALSE);
	if (!bgpp_view(bgpp.bloom_framebuffers[0], ppgl.bloom_width, ppgl.bloom_height, FALSE))
		return FALSE;
	bgfx_set_uniform(bgpp.size, small_size, 1);
	bgfx_set_uniform(bgpp.spread, spread, 1);
	bgfx_set_texture(0, bg.samplers[0], bgpp.bloom_textures[1], linear);
	bgpp_full_pass(bgpp.bloom[1], FALSE);
	/* the composite over the scene, into the chain */
	amounts[0] = exposure;
	amounts[1] = mix_amount;
	if (!bgpp_view(bgpp.main_framebuffers[ppgl.main_chain.current], ppgl.width, ppgl.height, FALSE))
		return FALSE;
	bgfx_set_uniform(bgpp.size, full_size, 1);
	bgfx_set_uniform(bgpp.amounts, amounts, 1);
	bgfx_set_texture(0, bg.samplers[0], bgpp.main_textures[ppgl.main_chain.scene], point);
	bgfx_set_texture(1, bg.samplers[1], bgpp.bloom_textures[0], linear);
	bgpp_full_pass(bgpp.bloom[2], FALSE);
	ppgl_chain_flip(&ppgl.main_chain);
	bgpp_effect_end(TRUE, 1.0f);
	return TRUE;
}

static void bgpp_stage_end(void)
{
	struct bg_target *target = bgpp.back_buffer;

	if (!ppgl.stage_active)
		return;
	ppgl.stage_active = FALSE;
	if (target && ppgl.main_chain.scene != 0)
	{
		/* the result into the back buffer, every channel (as OpenGL's copy) */
		bgfx_frame_buffer_handle_t framebuffer = bg_framebuffer_get(target, NULL);

		if (BGFX_HANDLE_IS_VALID(framebuffer) && bgpp_copy(bgpp.main_textures[ppgl.main_chain.scene], framebuffer))
		{
			target->last_rendered = device.frame + 1;
			target->written = ++bg.write_serial;
		}
	}
}
