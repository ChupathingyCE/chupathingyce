/*
OPENSAUCE_POSTPROCESS_GL.INL

The OpenGL renderer's part of OpenSauce's post-processing
(opensauce_postprocess.h; the game's part is
port/linux/game/opensauce_postprocess.c), included at the end of d3d8_gl.c
for the back buffer's targets. It does what OpenSauce's Direct3D 9 code and
D3DX do for an effect:
- an effect's compiled binary read and each pass's shaders turned into
  GLSL (d3d9_effect.c), built as the effect is first drawn;
- parameters kept by name as D3DX keeps them, put into each shader's
  registers by its constant table at a pass, its preshader run first;
- the scene chain and the secondary chain OpenSauce draws through (its
  RenderTargetChain.cpp and c_shader_postprocess::Render): a pass reads the
  scene (TEXSCENE), the last pass's result (TEXSOURCE) and the secondary
  buffer (TEXBUFFER), and draws into the next target; an effect's result
  becomes the scene, faded over it by its instance's fade
  (c_shader_instance_fade);
- the "G-buffer" OpenSauce's shaders read (TEXDEPTH, TEXVELOCITY,
  TEXNORMALS, TEXINDEX: its GBuffer.cpp and GBuffer_PS.fx formats), made
  here from the depth buffer each frame: linear depth, normals from it,
  velocity from the camera's motion, and an index of sky or world (the
  objects' own indices and velocities would need a pass of their own);
- OpenSauce's bloom (its PP_Bloom.fx: a bright pass at an eighth of the
  size, two blurs, and the composite), written here in GLSL;
- its standard values (ORTHOWORLDVIEWPROJECTION, SCENESIZE, PIXELSIZE,
  HUDSCALE, NEARCLIPDISTANCE, FARCLIPDISTANCE).
OpenSauce is Kornner Studios' (GPL-3.0-or-later); its sources are read as
facts. Desktop OpenGL draws it, and so does bgfx, from the same effects
(opensauce_postprocess_bgfx.inl); OpenGL ES and the consoles' layers say
they don't (halo_opensauce_pp_available), and the game logs that.
*/

#include "opensauce_postprocess.h"

#if !defined(HALO_GL_ES) && !defined(HALO_CONSOLE_GL_LAYER) && !defined(HALO_CONSOLE_GLES2) && \
	!defined(HALO_CONSOLE_NATIVE_RENDERER) && !defined(HALO_CONSOLE) && !defined(HALO_SERVER)

#include "d3d9_effect.h"

#define PPGL_TEST_FLAG(flags, bit) (((flags) & (1U << (bit))) != 0)

/* ---------- constants */

enum
{
	PPGL_MAXIMUM_EFFECTS = 64,
	PPGL_MAXIMUM_PASSES = 32,
	PPGL_SAMPLER_CACHE = 32,
	/* the bloom's targets are an eighth of the screen (PP_Bloom.fx's
	SHRINKSIZE) */
	PPGL_BLOOM_SHRINK = 8,

	/* the standard parameters, by semantic */
	_ppgl_semantic_ortho = 0,
	_ppgl_semantic_scene_size,
	_ppgl_semantic_hud_scale,
	_ppgl_semantic_pixel_size,
	_ppgl_semantic_near,
	_ppgl_semantic_far,
	_ppgl_semantic_source,
	_ppgl_semantic_scene,
	_ppgl_semantic_buffer,
	_ppgl_semantic_depth,
	_ppgl_semantic_velocity,
	_ppgl_semantic_normals,
	_ppgl_semantic_index,
	_ppgl_semantic_depth_x,
	_ppgl_semantic_velocity_x,
	_ppgl_semantic_velocity_y,
	_ppgl_semantic_normals_x,
	_ppgl_semantic_normals_y,
	_ppgl_semantic_index_x,
	_ppgl_semantic_index_y,
	NUMBER_OF_PPGL_SEMANTICS
};

static const char *const ppgl_semantics[NUMBER_OF_PPGL_SEMANTICS] =
{
	"ORTHOWORLDVIEWPROJECTION", "SCENESIZE", "HUDSCALE", "PIXELSIZE", "NEARCLIPDISTANCE", "FARCLIPDISTANCE",
	"TEXSOURCE", "TEXSCENE", "TEXBUFFER", "TEXDEPTH", "TEXVELOCITY", "TEXNORMALS", "TEXINDEX",
	"GBUFFER_DEPTH_X", "GBUFFER_VELOCITY_X", "GBUFFER_VELOCITY_Y", "GBUFFER_NORMALS_X", "GBUFFER_NORMALS_Y",
	"GBUFFER_INDEX_X", "GBUFFER_INDEX_Y"
};

/* ---------- structures */

struct ppgl_program
{
	GLuint program;
	BOOL failed;
	GLint vertex_floats, pixel_floats, vertex_ints, pixel_ints, vertex_bools, pixel_bools, pixel_fix;
	unsigned long vertex_float_count, pixel_float_count;
	unsigned char pixel_samplers[D3D9_SHADER_SAMPLERS];
};

struct ppgl_effect
{
	BOOL used;
	void *binary;
	struct d3d9_effect effect;
	long technique;
	/* each parameter's value (its defaults' words, as D3DX keeps them) and
	a texture parameter's texture */
	uint32_t **values;
	GLuint *textures;
	GLenum *texture_targets;
	BOOL *texture_mipmapped;
	long semantics[NUMBER_OF_PPGL_SEMANTICS];
	struct ppgl_program programs[PPGL_MAXIMUM_PASSES];
	char name[64];
};

struct ppgl_sampler
{
	unsigned long key;
	GLuint sampler;
};

/* a chain's targets (RenderTargetChain.cpp): the scene's chain has the
scene's copy and two of its own, the secondary chain two */
struct ppgl_chain
{
	int scene, current, next;
	BOOL first_render;
};

/* ---------- globals */

static struct
{
	BOOL failed;
	struct ppgl_effect effects[PPGL_MAXIMUM_EFFECTS];
	/* the targets: the main chain's three (0 the scene's copy), the
	secondary chain's two, of the back buffer's size and format */
	GLuint main_textures[3], main_framebuffers[3];
	GLuint secondary_textures[2], secondary_framebuffers[2];
	unsigned long width, height;
	GLenum format;
	struct ppgl_chain main_chain, secondary_chain;
	/* the G-buffer */
	GLuint gbuffer_textures[3], gbuffer_framebuffer;
	GLuint gbuffer_program;
	GLint gbuffer_uniforms[6];
	long gbuffer_serial;
	/* the bloom's targets, an eighth of the size */
	GLuint bloom_textures[2], bloom_framebuffers[2];
	unsigned long bloom_width, bloom_height;
	GLuint bloom_programs[3];
	GLint bloom_uniforms[3][4];
	/* the copy into the back buffer and the fade */
	GLuint copy_program, fade_program;
	GLint fade_amount;
	/* the vertex shader of the passes of its own */
	GLuint triangle_shader;
	GLuint empty_vertex_array, quad_vertex_array, quad_buffer, quad_indices;
	GLuint black_texture;
	struct ppgl_sampler samplers[PPGL_SAMPLER_CACHE];
	int sampler_count, sampler_evict;
	GLuint point_sampler, linear_sampler;
	/* the stage drawing */
	BOOL stage_active;
	struct opensauce_pp_frame frame;
	struct render_target_entry *back_buffer;
	long frame_serial;
	unsigned long logged;
} ppgl;

/* ---------- private code */

static void ppgl_log_once(unsigned long bit, const char *format, ...)
{
	char text[512];
	va_list arguments;

	if (ppgl.logged & (1UL << bit))
		return;
	ppgl.logged |= 1UL << bit;
	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	platform_log("OpenSauce post-processing: %s", text);
}

static GLuint ppgl_texture_new(GLenum format, unsigned long width, unsigned long height)
{
	GLuint texture;
	GLenum data_format = GL_RGBA, type = GL_UNSIGNED_BYTE;

	if (format == GL_R32F)
		data_format = GL_RED, type = GL_FLOAT;
	else if (format == GL_RG16F)
		data_format = GL_RG, type = GL_HALF_FLOAT;
	else if (format == GL_RGBA16F)
		type = GL_HALF_FLOAT;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, (GLint)format, (GLsizei)width, (GLsizei)height, 0, data_format, type, NULL);
	return texture;
}

static GLuint ppgl_framebuffer_new(GLuint texture)
{
	GLuint framebuffer;
	GLenum draw_buffer = GL_COLOR_ATTACHMENT0;

	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
	glDrawBuffers(1, &draw_buffer);
	return framebuffer;
}

static void ppgl_targets_release(void)
{
	glDeleteFramebuffers(3, ppgl.main_framebuffers);
	glDeleteTextures(3, ppgl.main_textures);
	glDeleteFramebuffers(2, ppgl.secondary_framebuffers);
	glDeleteTextures(2, ppgl.secondary_textures);
	glDeleteFramebuffers(1, &ppgl.gbuffer_framebuffer);
	glDeleteTextures(3, ppgl.gbuffer_textures);
	glDeleteFramebuffers(2, ppgl.bloom_framebuffers);
	glDeleteTextures(2, ppgl.bloom_textures);
	memset(ppgl.main_textures, 0, sizeof(ppgl.main_textures));
	memset(ppgl.main_framebuffers, 0, sizeof(ppgl.main_framebuffers));
	memset(ppgl.secondary_textures, 0, sizeof(ppgl.secondary_textures));
	memset(ppgl.secondary_framebuffers, 0, sizeof(ppgl.secondary_framebuffers));
	memset(ppgl.gbuffer_textures, 0, sizeof(ppgl.gbuffer_textures));
	ppgl.gbuffer_framebuffer = 0;
	memset(ppgl.bloom_textures, 0, sizeof(ppgl.bloom_textures));
	memset(ppgl.bloom_framebuffers, 0, sizeof(ppgl.bloom_framebuffers));
	ppgl.width = ppgl.height = 0;
}

/* the targets, of the back buffer's size and format */
static void ppgl_targets_fit(unsigned long width, unsigned long height, GLenum format)
{
	int index;
	GLenum draw_buffers[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };

	if (ppgl.width == width && ppgl.height == height && ppgl.format == format && ppgl.main_textures[0])
		return;
	ppgl_targets_release();
	ppgl.width = width;
	ppgl.height = height;
	ppgl.format = format;
	for (index = 0; index < 3; index++)
	{
		ppgl.main_textures[index] = ppgl_texture_new(format, width, height);
		ppgl.main_framebuffers[index] = ppgl_framebuffer_new(ppgl.main_textures[index]);
	}
	/* (the secondary chain's are 8 bits a channel, as OpenSauce's are) */
	for (index = 0; index < 2; index++)
	{
		ppgl.secondary_textures[index] = ppgl_texture_new(GL_RGBA8, width, height);
		ppgl.secondary_framebuffers[index] = ppgl_framebuffer_new(ppgl.secondary_textures[index]);
	}
	ppgl.gbuffer_textures[0] = ppgl_texture_new(GL_R32F, width, height);
	ppgl.gbuffer_textures[1] = ppgl_texture_new(GL_RG16F, width, height);
	ppgl.gbuffer_textures[2] = ppgl_texture_new(GL_RGBA8, width, height);
	glGenFramebuffers(1, &ppgl.gbuffer_framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, ppgl.gbuffer_framebuffer);
	for (index = 0; index < 3; index++)
		glFramebufferTexture2D(GL_FRAMEBUFFER, draw_buffers[index], GL_TEXTURE_2D, ppgl.gbuffer_textures[index], 0);
	glDrawBuffers(3, draw_buffers);
	ppgl.bloom_width = width / PPGL_BLOOM_SHRINK ? width / PPGL_BLOOM_SHRINK : 1;
	ppgl.bloom_height = height / PPGL_BLOOM_SHRINK ? height / PPGL_BLOOM_SHRINK : 1;
	for (index = 0; index < 2; index++)
	{
		ppgl.bloom_textures[index] = ppgl_texture_new(format, ppgl.bloom_width, ppgl.bloom_height);
		ppgl.bloom_framebuffers[index] = ppgl_framebuffer_new(ppgl.bloom_textures[index]);
	}
	ppgl.gbuffer_serial = -1;
	xgpu_gl_state_invalidate();
}

static void ppgl_chain_flip(struct ppgl_chain *chain)
{
	int swap = chain->current;

	chain->current = chain->next;
	chain->next = swap;
}

/* (OpenSauce's SetSceneToLast: the last result made the scene) */
static void ppgl_chain_scene_to_last(struct ppgl_chain *chain)
{
	int swap = chain->next;

	chain->next = chain->scene;
	chain->scene = swap;
}

/* ---------- programs of its own */

static const char ppgl_triangle_source[] =
	"void main()\n"
	"{\n"
	"\tgl_Position = vec4(float((gl_VertexID & 1) << 2) - 1.0, float((gl_VertexID & 2) << 1) - 1.0, 0.0, 1.0);\n"
	"}\n";

static GLuint ppgl_program_own(const char *fragment, const char *what)
{
	struct xgpu_text text = { 0 };
	GLuint shader, program;

	if (!ppgl.triangle_shader)
	{
		xgpu_text_append(&text, "%s%s", XGPU_GL_VERSION_LINE, ppgl_triangle_source);
		ppgl.triangle_shader = xgpu_compile_shader(GL_VERTEX_SHADER, text.buffer, "OpenSauce post-processing");
		text.length = 0;
		if (!ppgl.triangle_shader)
		{
			free(text.buffer);
			return 0;
		}
	}
	xgpu_text_append(&text, "%s%s", XGPU_GL_VERSION_LINE, fragment);
	shader = xgpu_compile_shader(GL_FRAGMENT_SHADER, text.buffer, what);
	free(text.buffer);
	if (!shader)
		return 0;
	program = xgpu_link_program(ppgl.triangle_shader, shader, what);
	glDeleteShader(shader);
	return program;
}

/* the G-buffer from the depth buffer: OpenSauce's linear depth (view
distance past the near plane over the clip range), velocity (half the
change in clip position since the last frame, the camera's), view-space
normals (0.5 + half, from the depth's slopes) and the index (mesh 1 the
sky, 2 the world, over 255) */
static const char ppgl_gbuffer_source[] =
	"uniform sampler2D depth_texture;\n"
	"/* the matrices' rows */\n"
	"uniform vec4 world_from_clip[4];\n"
	"uniform vec4 previous_clip_from_world[4];\n"
	"/* near, far, projection x and y scales */\n"
	"uniform vec4 clip;\n"
	"uniform vec4 size;\n"
	"layout(location = 0) out vec4 out_depth;\n"
	"layout(location = 1) out vec4 out_velocity;\n"
	"layout(location = 2) out vec4 out_normals_index;\n"
	"void main()\n"
	"{\n"
	"\tfloat d = texelFetch(depth_texture, ivec2(gl_FragCoord.xy), 0).r;\n"
	"\tvec2 uv = gl_FragCoord.xy / size.xy;\n"
	"\tvec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);\n"
	"\tfloat z_ndc = d * 2.0 - 1.0;\n"
	"\tfloat n = clip.x, f = clip.y;\n"
	"\tfloat distance = 2.0 * n * f / max((f + n) - z_ndc * (f - n), 1e-6);\n"
	"\tbool sky = d >= 0.9999999;\n"
	"\tout_depth = vec4(sky ? 1.0 : clamp((distance - n) / (f - n), 0.0, 1.0), 0.0, 0.0, 1.0);\n"
	"\tvec4 point = vec4(ndc, z_ndc, 1.0);\n"
	"\tvec4 world = vec4(dot(world_from_clip[0], point), dot(world_from_clip[1], point), dot(world_from_clip[2], point),\n"
	"\t\tdot(world_from_clip[3], point));\n"
	"\tworld /= world.w;\n"
	"\tvec4 previous = vec4(dot(previous_clip_from_world[0], world), dot(previous_clip_from_world[1], world),\n"
	"\t\tdot(previous_clip_from_world[2], world), dot(previous_clip_from_world[3], world));\n"
	"\tvec2 previous_ndc = previous.xy / max(abs(previous.w), 1e-6) * sign(previous.w);\n"
	"\tout_velocity = vec4(sky ? vec2(0.0) : (ndc - previous_ndc) * 0.5, 0.0, 1.0);\n"
	"\tvec3 view = vec3(ndc.x * distance / clip.z, ndc.y * distance / clip.w, distance);\n"
	"\tvec3 normal = cross(dFdx(view), dFdy(view));\n"
	"\tnormal = dot(normal, normal) > 0.0 ? normalize(normal) : vec3(0.0, 0.0, -1.0);\n"
	"\tout_normals_index = vec4(sky ? vec2(0.5) : normal.xy * 0.5 + 0.5, (sky ? 1.0 : 2.0) / 255.0, 0.0);\n"
	"}\n";

/* PP_Bloom.fx's passes: the bright pass (into the eighth), the Poisson
blur, and the composite over the scene */
static const char ppgl_bloom_bright_source[] =
	"uniform sampler2D source;\n"
	"uniform vec4 minimum_color;\n"
	"uniform vec4 maximum_color;\n"
	"uniform vec4 size;\n"
	"out vec4 result;\n"
	"void main()\n"
	"{\n"
	"\tvec3 color = texture(source, gl_FragCoord.xy / size.xy).rgb;\n"
	"\tcolor = max(minimum_color.rgb, min(maximum_color.rgb, color));\n"
	"\tcolor = (color - minimum_color.rgb) / (maximum_color.rgb - minimum_color.rgb);\n"
	"\tresult = vec4(clamp(color, 0.0, 1.0), 1.0);\n"
	"}\n";

static const char ppgl_bloom_blur_source[] =
	"uniform sampler2D source;\n"
	"/* the eighth's size, and the bloom's size in its texels */\n"
	"uniform vec4 size;\n"
	"uniform float spread;\n"
	"out vec4 result;\n"
	"const vec2 poisson[12] = vec2[12](vec2(-0.326212, -0.40581), vec2(-0.840144, -0.07358),\n"
	"\tvec2(-0.695914, 0.457137), vec2(-0.203345, 0.620716), vec2(0.96234, -0.194983), vec2(0.473434, -0.480026),\n"
	"\tvec2(0.519456, 0.767022), vec2(0.185461, -0.893124), vec2(0.507431, 0.064425), vec2(0.89642, 0.412458),\n"
	"\tvec2(-0.32194, -0.932615), vec2(-0.791559, -0.59771));\n"
	"void main()\n"
	"{\n"
	"\tvec2 uv = gl_FragCoord.xy / size.xy;\n"
	"\tvec2 step = spread / size.xy;\n"
	"\tvec3 color = texture(source, uv).rgb;\n"
	"\tfor (int i = 0; i < 12; i++)\n"
	"\t\tcolor += texture(source, uv + poisson[i] * step).rgb;\n"
	"\tresult = vec4(color / 13.0, 1.0);\n"
	"}\n";

static const char ppgl_bloom_composite_source[] =
	"uniform sampler2D scene;\n"
	"uniform sampler2D bloom;\n"
	"uniform vec4 size;\n"
	"/* exposure, mix amount */\n"
	"uniform vec4 amounts;\n"
	"out vec4 result;\n"
	"void main()\n"
	"{\n"
	"\tvec3 color = texelFetch(scene, ivec2(gl_FragCoord.xy), 0).rgb;\n"
	"\tvec3 glow = texture(bloom, gl_FragCoord.xy / size.xy).rgb * 0.5;\n"
	"\tresult = vec4(mix(color, color + glow * amounts.x, amounts.y), 1.0);\n"
	"}\n";

static const char ppgl_copy_source[] =
	"uniform sampler2D source;\n"
	"out vec4 result;\n"
	"void main()\n"
	"{\n"
	"\tresult = texelFetch(source, ivec2(gl_FragCoord.xy), 0);\n"
	"}\n";

/* PP_EffectFade.fx: the effect's result over the scene as it was, by the
fade, in its alpha */
static const char ppgl_fade_source[] =
	"uniform sampler2D source;\n"
	"uniform float fade;\n"
	"out vec4 result;\n"
	"void main()\n"
	"{\n"
	"\tresult = vec4(texelFetch(source, ivec2(gl_FragCoord.xy), 0).rgb, fade);\n"
	"}\n";

static BOOL ppgl_prepare(void)
{
	static const GLushort none = 0;

	if (ppgl.failed)
		return FALSE;
	if (ppgl.copy_program)
		return TRUE;
	ppgl.copy_program = ppgl_program_own(ppgl_copy_source, "OpenSauce post-processing copy");
	ppgl.fade_program = ppgl_program_own(ppgl_fade_source, "OpenSauce post-processing fade");
	ppgl.gbuffer_program = ppgl_program_own(ppgl_gbuffer_source, "OpenSauce post-processing G-buffer");
	ppgl.bloom_programs[0] = ppgl_program_own(ppgl_bloom_bright_source, "OpenSauce bloom bright pass");
	ppgl.bloom_programs[1] = ppgl_program_own(ppgl_bloom_blur_source, "OpenSauce bloom blur");
	ppgl.bloom_programs[2] = ppgl_program_own(ppgl_bloom_composite_source, "OpenSauce bloom composite");
	if (!ppgl.copy_program || !ppgl.fade_program || !ppgl.gbuffer_program || !ppgl.bloom_programs[0] ||
		!ppgl.bloom_programs[1] || !ppgl.bloom_programs[2])
	{
		ppgl.failed = TRUE;
		platform_log("OpenSauce post-processing: its own programs do not build, so it is not drawn");
		return FALSE;
	}
	glUseProgram(ppgl.copy_program);
	glUniform1i(glGetUniformLocation(ppgl.copy_program, "source"), 0);
	glUseProgram(ppgl.fade_program);
	glUniform1i(glGetUniformLocation(ppgl.fade_program, "source"), 0);
	ppgl.fade_amount = glGetUniformLocation(ppgl.fade_program, "fade");
	glUseProgram(ppgl.gbuffer_program);
	glUniform1i(glGetUniformLocation(ppgl.gbuffer_program, "depth_texture"), 0);
	ppgl.gbuffer_uniforms[0] = glGetUniformLocation(ppgl.gbuffer_program, "world_from_clip");
	ppgl.gbuffer_uniforms[1] = glGetUniformLocation(ppgl.gbuffer_program, "previous_clip_from_world");
	ppgl.gbuffer_uniforms[2] = glGetUniformLocation(ppgl.gbuffer_program, "clip");
	ppgl.gbuffer_uniforms[3] = glGetUniformLocation(ppgl.gbuffer_program, "size");
	glUseProgram(ppgl.bloom_programs[0]);
	glUniform1i(glGetUniformLocation(ppgl.bloom_programs[0], "source"), 0);
	ppgl.bloom_uniforms[0][0] = glGetUniformLocation(ppgl.bloom_programs[0], "minimum_color");
	ppgl.bloom_uniforms[0][1] = glGetUniformLocation(ppgl.bloom_programs[0], "maximum_color");
	ppgl.bloom_uniforms[0][2] = glGetUniformLocation(ppgl.bloom_programs[0], "size");
	glUseProgram(ppgl.bloom_programs[1]);
	glUniform1i(glGetUniformLocation(ppgl.bloom_programs[1], "source"), 0);
	ppgl.bloom_uniforms[1][0] = glGetUniformLocation(ppgl.bloom_programs[1], "size");
	ppgl.bloom_uniforms[1][1] = glGetUniformLocation(ppgl.bloom_programs[1], "spread");
	glUseProgram(ppgl.bloom_programs[2]);
	glUniform1i(glGetUniformLocation(ppgl.bloom_programs[2], "scene"), 0);
	glUniform1i(glGetUniformLocation(ppgl.bloom_programs[2], "bloom"), 1);
	ppgl.bloom_uniforms[2][0] = glGetUniformLocation(ppgl.bloom_programs[2], "size");
	ppgl.bloom_uniforms[2][1] = glGetUniformLocation(ppgl.bloom_programs[2], "amounts");
	glGenVertexArrays(1, &ppgl.empty_vertex_array);
	glGenVertexArrays(1, &ppgl.quad_vertex_array);
	glGenBuffers(1, &ppgl.quad_buffer);
	glGenBuffers(1, &ppgl.quad_indices);
	glGenTextures(1, &ppgl.black_texture);
	glBindTexture(GL_TEXTURE_2D, ppgl.black_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &none);
	glGenSamplers(1, &ppgl.point_sampler);
	glSamplerParameteri(ppgl.point_sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glSamplerParameteri(ppgl.point_sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glSamplerParameteri(ppgl.point_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glSamplerParameteri(ppgl.point_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glGenSamplers(1, &ppgl.linear_sampler);
	glSamplerParameteri(ppgl.linear_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glSamplerParameteri(ppgl.linear_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glSamplerParameteri(ppgl.linear_sampler, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
	glSamplerParameteri(ppgl.linear_sampler, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
	xgpu_gl_state_invalidate();
	return TRUE;
}

/* a texture bound to a unit with a sampler */
static void ppgl_bind(GLuint unit, GLenum target, GLuint texture, GLuint sampler)
{
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(target, texture);
	glBindSampler(unit, sampler);
}

/* the state the passes draw in (PostProcessing_RenderStates.inl: no depth,
stencil, blending or alpha test; every channel written) */
static void ppgl_pass_state(void)
{
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_FALSE);
}

static void ppgl_triangle_draw(GLuint framebuffer, unsigned long width, unsigned long height)
{
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glViewport(0, 0, (GLsizei)width, (GLsizei)height);
	glBindVertexArray(ppgl.empty_vertex_array);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

/* a 4x4 matrix's inverse (rows as given); FALSE if it has none */
static BOOL ppgl_invert(const float m[16], float out[16])
{
	float inv[16], determinant;
	int index;

	inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
		m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
		m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
		m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
		m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
		m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
		m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
		m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
		m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
		m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
	inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
		m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
	inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
		m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
	inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
		m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
	inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
		m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
	inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
		m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
	inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
		m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
	inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
		m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
	determinant = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	if (determinant == 0.0f || determinant != determinant)
		return FALSE;
	for (index = 0; index < 16; index++)
		out[index] = inv[index] / determinant;
	return TRUE;
}

/* the G-buffer, once a frame, from the depth buffer as it is now */
static void ppgl_gbuffer_make(void)
{
	struct render_target_entry *depth;
	float clip_from_world[16], world_from_clip[16], previous[16], clip[4], size[4];
	int row, column;

	if (ppgl.gbuffer_serial == ppgl.frame.frame_serial)
		return;
	ppgl.gbuffer_serial = ppgl.frame.frame_serial;
	depth = render_target_get(&device.depth_buffer);
	if (!depth || !depth->target.depth || depth->target.gl_width != ppgl.width ||
		depth->target.gl_height != ppgl.height)
	{
		ppgl_log_once(0, "the depth buffer is not the back buffer's size: the G-buffer is left empty");
		glBindFramebuffer(GL_FRAMEBUFFER, ppgl.gbuffer_framebuffer);
		glClearColor(1.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		return;
	}
	render_target_resolve(&depth->target);
	/* (the matrices as rows: clip = row . (x, y, z, 1)) */
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
	ppgl_pass_state();
	glUseProgram(ppgl.gbuffer_program);
	glUniform4fv(ppgl.gbuffer_uniforms[0], 4, world_from_clip);
	glUniform4fv(ppgl.gbuffer_uniforms[1], 4, previous);
	glUniform4fv(ppgl.gbuffer_uniforms[2], 1, clip);
	glUniform4fv(ppgl.gbuffer_uniforms[3], 1, size);
	ppgl_bind(0, GL_TEXTURE_2D, depth->target.texture, ppgl.point_sampler);
	ppgl_triangle_draw(ppgl.gbuffer_framebuffer, ppgl.width, ppgl.height);
	glBindFramebuffer(GL_FRAMEBUFFER, ppgl.gbuffer_framebuffer);
	{
		GLenum draw_buffers[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };

		glDrawBuffers(3, draw_buffers);
	}
}

/* ---------- effects */

static struct ppgl_effect *ppgl_effect_get(long effect)
{
	if (effect < 0 || effect >= PPGL_MAXIMUM_EFFECTS || !ppgl.effects[effect].used)
		return NULL;
	return &ppgl.effects[effect];
}

static void ppgl_effect_free(struct ppgl_effect *effect)
{
	unsigned long index;
	int pass;

	for (pass = 0; pass < PPGL_MAXIMUM_PASSES; pass++)
	{
		if (effect->programs[pass].program)
			glDeleteProgram(effect->programs[pass].program);
	}
	for (index = 0; effect->values && index < effect->effect.parameter_count; index++)
		free(effect->values[index]);
	free(effect->values);
	free(effect->textures);
	free(effect->texture_targets);
	free(effect->texture_mipmapped);
	d3d9_effect_dispose(&effect->effect);
	free(effect->binary);
	memset(effect, 0, sizeof(*effect));
}

/* a parameter's words written (as its type: a float from an integer's, an
integer from a float's) */
static void ppgl_value_write(struct ppgl_effect *effect, long parameter, int type, const void *values, int count)
{
	const struct d3d9_parameter *description;
	const uint32_t *words = values;
	int index;

	if (parameter < 0 || !effect->values[parameter])
		return;
	description = &effect->effect.parameters[parameter];
	for (index = 0; index < count && (unsigned long)index < description->default_count; index++)
	{
		uint32_t word = words[index];
		BOOL given_float = type == _opensauce_pp_value_float || type == _opensauce_pp_value_argb_color ||
			type == _opensauce_pp_value_matrix;

		if (description->type == _d3d9_type_float && !given_float)
		{
			float value = (float)(int32_t)word;

			memcpy(&word, &value, sizeof(word));
		}
		else if (description->type != _d3d9_type_float && given_float)
		{
			float value;

			memcpy(&value, &word, sizeof(value));
			word = description->type == _d3d9_type_bool ? value != 0.0f : (uint32_t)(int32_t)value;
		}
		effect->values[parameter][index] = word;
	}
}

static void ppgl_semantic_set(struct ppgl_effect *effect, int semantic, const float *values, int count)
{
	ppgl_value_write(effect, effect->semantics[semantic], _opensauce_pp_value_float, values, count);
}

/* a parameter's word as a float, by its type */
static float ppgl_value_float(const struct d3d9_parameter *description, uint32_t word)
{
	float value;

	if (description->type == _d3d9_type_float)
	{
		memcpy(&value, &word, sizeof(value));
		return value;
	}
	if (description->type == _d3d9_type_bool)
		return word ? 1.0f : 0.0f;
	return (float)(int32_t)word;
}

/* constants (a table's) filled from the parameters by name: the float
ones into floats (registers of four), the integer ones into ints, the
booleans into bools */
static void ppgl_constants_fill(struct ppgl_effect *effect, const struct d3d9_constant *constants,
	unsigned long constant_count, float *floats, unsigned long float_registers, int *ints, int *bools)
{
	unsigned long index;

	for (index = 0; index < constant_count; index++)
	{
		const struct d3d9_constant *constant = &constants[index];
		long parameter = d3d9_effect_parameter_find(&effect->effect, constant->name);
		const struct d3d9_parameter *description;
		const uint32_t *words;
		unsigned long rows, columns, elements, element, register_index = 0;

		if (parameter < 0 || !effect->values[parameter])
			continue;
		description = &effect->effect.parameters[parameter];
		words = effect->values[parameter];
		rows = description->rows ? description->rows : 1;
		columns = description->columns ? description->columns : 1;
		elements = description->elements ? description->elements : 1;
		/* (only as many as its value holds: a constant table may say more) */
		if (rows * columns * elements > description->default_count)
			elements = description->default_count / (rows * columns);
		for (element = 0; element < elements; element++)
		{
			const uint32_t *element_words = words + element * rows * columns;
			unsigned long major, minor, majors, minors;
			BOOL by_columns = constant->klass == _d3d9_class_matrix_columns;

			majors = by_columns ? columns : rows;
			minors = by_columns ? rows : columns;
			for (major = 0; major < majors; major++, register_index++)
			{
				unsigned long reg = constant->reg + register_index;

				if (register_index >= constant->count)
					break;
				for (minor = 0; minor < minors && minor < 4; minor++)
				{
					uint32_t word = by_columns ? element_words[minor * columns + major] :
						element_words[major * columns + minor];

					if (constant->regset == _d3d9_regset_float4 && reg < float_registers)
						floats[reg * 4 + minor] = ppgl_value_float(description, word);
					else if (constant->regset == _d3d9_regset_int4 && reg < D3D9_SHADER_INT_REGISTERS && ints)
					{
						ints[reg * 4 + minor] = (int)(description->type == _d3d9_type_float ?
							(int32_t)ppgl_value_float(description, word) : (int32_t)word);
						/* (a lone integer as a loop counter: start 0, step 1) */
						if (minors == 1)
						{
							ints[reg * 4 + 1] = 0;
							ints[reg * 4 + 2] = 1;
							ints[reg * 4 + 3] = 0;
						}
					}
					else if (constant->regset == _d3d9_regset_bool && reg < D3D9_SHADER_BOOL_REGISTERS && bools &&
						minor == 0)
					{
						bools[reg] = ppgl_value_float(description, word) != 0.0f;
					}
				}
			}
		}
	}
}

/* a shader's registers: its constants', then its preshader's outputs */
static void ppgl_shader_registers(struct ppgl_effect *effect, const struct d3d9_shader *shader, float *floats,
	int *ints, int *bools)
{
	memset(floats, 0, sizeof(float) * D3D9_SHADER_FLOAT_REGISTERS * 4);
	memset(ints, 0, sizeof(int) * D3D9_SHADER_INT_REGISTERS * 4);
	memset(bools, 0, sizeof(int) * D3D9_SHADER_BOOL_REGISTERS);
	ppgl_constants_fill(effect, shader->constants, shader->constant_count, floats, D3D9_SHADER_FLOAT_REGISTERS, ints,
		bools);
	if (shader->has_preshader)
	{
		float inputs[D3D9_SHADER_FLOAT_REGISTERS * 4];
		unsigned long input_registers = shader->preshader.input_components / 4;

		memset(inputs, 0, sizeof(inputs));
		if (input_registers > D3D9_SHADER_FLOAT_REGISTERS)
			input_registers = D3D9_SHADER_FLOAT_REGISTERS;
		ppgl_constants_fill(effect, shader->preshader.inputs, shader->preshader.input_count, inputs, input_registers,
			NULL, NULL);
		if (!d3d9_preshader_run(&shader->preshader, inputs, floats))
			ppgl_log_once(1, "a preshader of '%s' has an instruction this doesn't run", effect->name);
	}
}

/* a sampler object for a sampler's states (D3DTADDRESS, D3DTEXF), mipmapped
or not */
static GLuint ppgl_sampler_get(const struct d3d9_parameter *sampler, BOOL mipmapped)
{
	static const GLenum addresses[6] =
	{
		GL_REPEAT, GL_REPEAT, GL_MIRRORED_REPEAT, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_BORDER, GL_MIRRORED_REPEAT
	};
	unsigned long u = sampler->sampler_states[_d3d9_sampler_address_u];
	unsigned long v = sampler->sampler_states[_d3d9_sampler_address_v];
	unsigned long w = sampler->sampler_states[_d3d9_sampler_address_w];
	unsigned long magnify = sampler->sampler_states[_d3d9_sampler_mag_filter];
	unsigned long minify = sampler->sampler_states[_d3d9_sampler_min_filter];
	unsigned long mip = sampler->sampler_states[_d3d9_sampler_mip_filter];
	unsigned long border = sampler->sampler_states[_d3d9_sampler_border_color];
	unsigned long key;
	GLuint object;
	GLenum min_filter;
	int index;

	/* (Direct3D 9's defaults where the effect sets none: wrap, point, no
	mipmaps) */
	u = u == D3D9_SAMPLER_STATE_UNSET || u > 5 ? 1 : u;
	v = v == D3D9_SAMPLER_STATE_UNSET || v > 5 ? 1 : v;
	w = w == D3D9_SAMPLER_STATE_UNSET || w > 5 ? 1 : w;
	magnify = magnify == D3D9_SAMPLER_STATE_UNSET ? 1 : magnify;
	minify = minify == D3D9_SAMPLER_STATE_UNSET ? 1 : minify;
	mip = mip == D3D9_SAMPLER_STATE_UNSET || !mipmapped ? 0 : mip;
	border = border == D3D9_SAMPLER_STATE_UNSET ? 0 : border;
	key = u | (v << 3) | (w << 6) | ((magnify >= 2) << 9) | ((minify >= 2) << 10) | ((mip ? (mip >= 2 ? 2 : 1) : 0) << 11);
	for (index = 0; index < ppgl.sampler_count; index++)
	{
		if (ppgl.samplers[index].key == key)
			return ppgl.samplers[index].sampler;
	}
	glGenSamplers(1, &object);
	if (!mip)
		min_filter = minify >= 2 ? GL_LINEAR : GL_NEAREST;
	else if (mip >= 2)
		min_filter = minify >= 2 ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
	else
		min_filter = minify >= 2 ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
	glSamplerParameteri(object, GL_TEXTURE_MIN_FILTER, (GLint)min_filter);
	glSamplerParameteri(object, GL_TEXTURE_MAG_FILTER, magnify >= 2 ? GL_LINEAR : GL_NEAREST);
	glSamplerParameteri(object, GL_TEXTURE_WRAP_S, (GLint)addresses[u]);
	glSamplerParameteri(object, GL_TEXTURE_WRAP_T, (GLint)addresses[v]);
	glSamplerParameteri(object, GL_TEXTURE_WRAP_R, (GLint)addresses[w]);
	{
		float color[4];

		color[0] = (float)((border >> 16) & 0xFF) / 255.0f;
		color[1] = (float)((border >> 8) & 0xFF) / 255.0f;
		color[2] = (float)(border & 0xFF) / 255.0f;
		color[3] = (float)((border >> 24) & 0xFF) / 255.0f;
		glSamplerParameterfv(object, GL_TEXTURE_BORDER_COLOR, color);
	}
	/* (kept for the next ask; when the cache is full, the oldest let go:
	no draw is still bound to it, as each pass binds its own) */
	if (ppgl.sampler_count < PPGL_SAMPLER_CACHE)
		index = ppgl.sampler_count++;
	else
	{
		index = ppgl.sampler_evict++ % PPGL_SAMPLER_CACHE;
		glDeleteSamplers(1, &ppgl.samplers[index].sampler);
	}
	ppgl.samplers[index].key = key;
	ppgl.samplers[index].sampler = object;
	return object;
}

/* a pass's program, built the first time it is drawn */
static struct ppgl_program *ppgl_program_get(struct ppgl_effect *effect, int pass_index)
{
	struct ppgl_program *program = &effect->programs[pass_index];
	const struct d3d9_pass *pass = &effect->effect.techniques[effect->technique].passes[pass_index];
	struct d3d9_glsl_varyings inputs;
	unsigned char vertex_kinds[D3D9_SHADER_SAMPLERS];
	char *pixel_text, *vertex_text, error[256], what[128];
	struct xgpu_text text = { 0 };
	GLuint vertex_shader = 0, pixel_shader = 0;
	unsigned long index;

	if (program->program || program->failed)
		return program->program ? program : NULL;
	program->failed = TRUE;
	snprintf(what, sizeof(what), "OpenSauce effect '%s' pass %d", effect->name, pass_index);
	if (pass->vertex_shader < 0 || pass->pixel_shader < 0)
	{
		platform_log("OpenSauce post-processing: %s has no vertex or pixel shader", what);
		return NULL;
	}
	memset(&inputs, 0, sizeof(inputs));
	pixel_text = d3d9_shader_glsl(&effect->effect.shaders[pass->pixel_shader], NULL, &inputs,
		&program->pixel_float_count, program->pixel_samplers, error, sizeof(error));
	vertex_text = pixel_text ? d3d9_shader_glsl(&effect->effect.shaders[pass->vertex_shader], &inputs, NULL,
		&program->vertex_float_count, vertex_kinds, error, sizeof(error)) : NULL;
	if (!pixel_text || !vertex_text)
	{
		platform_log("OpenSauce post-processing: %s does not translate: %s", what, error);
		free(pixel_text);
		free(vertex_text);
		return NULL;
	}
	xgpu_text_append(&text, "%s%s", XGPU_GL_VERSION_LINE, vertex_text);
	vertex_shader = xgpu_compile_shader(GL_VERTEX_SHADER, text.buffer, what);
	text.length = 0;
	xgpu_text_append(&text, "%s%s", XGPU_GL_VERSION_LINE, pixel_text);
	pixel_shader = vertex_shader ? xgpu_compile_shader(GL_FRAGMENT_SHADER, text.buffer, what) : 0;
	free(text.buffer);
	free(pixel_text);
	free(vertex_text);
	if (!vertex_shader || !pixel_shader)
	{
		if (vertex_shader)
			glDeleteShader(vertex_shader);
		return NULL;
	}
	program->program = xgpu_link_program(vertex_shader, pixel_shader, what);
	glDeleteShader(vertex_shader);
	glDeleteShader(pixel_shader);
	if (!program->program)
		return NULL;
	program->failed = FALSE;
	glUseProgram(program->program);
	program->vertex_floats = glGetUniformLocation(program->program, "vc");
	program->pixel_floats = glGetUniformLocation(program->program, "pc");
	program->vertex_ints = glGetUniformLocation(program->program, "vci");
	program->pixel_ints = glGetUniformLocation(program->program, "pci");
	program->vertex_bools = glGetUniformLocation(program->program, "vcb");
	program->pixel_bools = glGetUniformLocation(program->program, "pcb");
	program->pixel_fix = glGetUniformLocation(program->program, "d3d9_pixel_fix");
	for (index = 0; index < D3D9_SHADER_SAMPLERS; index++)
	{
		char name[8];

		snprintf(name, sizeof(name), "s%lu", index);
		if (program->pixel_samplers[index])
			glUniform1i(glGetUniformLocation(program->program, name), (GLint)index);
	}
	return program;
}

/* the texture a sampler register of a shader reads, with its sampler */
static void ppgl_sampler_bind(struct ppgl_effect *effect, const struct d3d9_shader *shader, unsigned long reg,
	unsigned char kind)
{
	unsigned long index;
	GLenum target = kind == 3 ? GL_TEXTURE_CUBE_MAP : kind == 4 ? GL_TEXTURE_3D : GL_TEXTURE_2D;

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
		if (sampler >= 0 && texture >= 0 && effect->textures[texture] && effect->texture_targets[texture] == target)
		{
			ppgl_bind((GLuint)reg, target, effect->textures[texture],
				ppgl_sampler_get(&effect->effect.parameters[sampler], effect->texture_mipmapped[texture]));
			return;
		}
		break;
	}
	/* (no texture: black, as an unset texture reads in Direct3D 9) */
	ppgl_bind((GLuint)reg, GL_TEXTURE_2D, target == GL_TEXTURE_2D ? ppgl.black_texture : 0, ppgl.point_sampler);
	if (target != GL_TEXTURE_2D)
	{
		glBindTexture(target, 0);
		ppgl_log_once(2, "an effect samples a cube or volume texture it has none of: it reads black");
	}
}

static void ppgl_texture_set(struct ppgl_effect *effect, long parameter, GLenum target, GLuint texture,
	BOOL mipmapped)
{
	if (parameter < 0 || (unsigned long)parameter >= effect->effect.parameter_count)
		return;
	effect->textures[parameter] = texture;
	effect->texture_targets[parameter] = target;
	effect->texture_mipmapped[parameter] = mipmapped;
}

/* a pass's quad (c_quad_manager::AddVertices): positions in pixels, half a
pixel up and left, y turned over; its texture coordinates 0 to 1 across the
quad, across the screen, and its bounds */
static int ppgl_quad(const short tessellation[2], const float x_bounds[2], const float y_bounds[2])
{
	enum { QUAD_MAXIMUM = 64 };
	static float vertices[(QUAD_MAXIMUM + 1) * (QUAD_MAXIMUM + 1) * 19];
	static GLushort indices[QUAD_MAXIMUM * QUAD_MAXIMUM * 6];
	int tx = tessellation[0], ty = tessellation[1], x, y, count = 0, vertex = 0;
	float x_increment = (x_bounds[1] - x_bounds[0]) / (float)tx;
	float y_increment = (y_bounds[1] - y_bounds[0]) / (float)ty;
	float width = (float)ppgl.width, height = (float)ppgl.height;

	for (y = 0; y <= ty; y++)
	{
		for (x = 0; x <= tx; x++)
		{
			float *v = &vertices[vertex++ * 19];

			v[0] = (x_bounds[0] + (float)x * x_increment) * width - 0.5f;
			v[1] = -((y_bounds[0] + (float)y * y_increment) * height - 0.5f);
			v[2] = 10.0f;
			v[3] = (float)x / (float)tx;
			v[4] = (float)y / (float)ty;
			v[5] = v[6] = 0.0f;
			v[7] = x_bounds[0] + (float)x * x_increment;
			v[8] = y_bounds[0] + (float)y * y_increment;
			v[9] = v[10] = 0.0f;
			v[11] = x_bounds[0];
			v[12] = x_bounds[1];
			v[13] = y_bounds[0];
			v[14] = y_bounds[1];
			v[15] = v[16] = v[17] = v[18] = 0.0f;
		}
	}
	for (y = 0; y < ty; y++)
	{
		for (x = 0; x < tx; x++)
		{
			GLushort top_left = (GLushort)(y * (tx + 1) + x), top_right = (GLushort)(top_left + 1);
			GLushort bottom_left = (GLushort)(top_left + tx + 1), bottom_right = (GLushort)(bottom_left + 1);

			indices[count++] = bottom_left;
			indices[count++] = top_left;
			indices[count++] = top_right;
			indices[count++] = top_right;
			indices[count++] = bottom_right;
			indices[count++] = bottom_left;
		}
	}
	glBindVertexArray(ppgl.quad_vertex_array);
	glBindBuffer(GL_ARRAY_BUFFER, ppgl.quad_buffer);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vertex * 19 * sizeof(float)), vertices, GL_STREAM_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ppgl.quad_indices);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof(GLushort)), indices, GL_STREAM_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 19 * sizeof(float), (const void *)0);
	for (x = 0; x < 4; x++)
	{
		glEnableVertexAttribArray((GLuint)(1 + x));
		glVertexAttribPointer((GLuint)(1 + x), 4, GL_FLOAT, GL_FALSE, 19 * sizeof(float),
			(const void *)(size_t)((3 + x * 4) * sizeof(float)));
	}
	return count;
}

#ifdef HALO_BGFX
/* (bgfx's: each public function below hands its call there while bgfx draws) */
#include "opensauce_postprocess_bgfx.inl"
#endif

/* ---------- public code */

int halo_opensauce_pp_available(void)
{
#ifdef HALO_BGFX
	if (bg_active())
		return bgpp_available();
#endif
#ifdef HALO_VULKAN
	if (vk_active())
		return 0;
#endif
	return device.gl_ready && !ppgl.failed;
}

long halo_opensauce_pp_effect_load(const void *binary, unsigned long size, const char *name, const char *technique)
{
	struct ppgl_effect *effect = NULL;
	char error[256];
	long index;
	unsigned long parameter;
	int semantic;

	if (!halo_opensauce_pp_available() || !binary || !size)
		return -1;
	for (index = 0; index < PPGL_MAXIMUM_EFFECTS; index++)
	{
		if (!ppgl.effects[index].used)
		{
			effect = &ppgl.effects[index];
			break;
		}
	}
	if (!effect)
	{
		platform_log("OpenSauce post-processing: more than %d effects", PPGL_MAXIMUM_EFFECTS);
		return -1;
	}
	snprintf(effect->name, sizeof(effect->name), "%s", name && name[0] ? name : technique ? technique : "");
	/* (kept, 4-byte aligned, while the effect is: its shaders point into it) */
	effect->binary = malloc(size + 4);
	if (!effect->binary)
		return -1;
	memcpy(effect->binary, binary, size);
	if (!d3d9_effect_read(effect->binary, size, &effect->effect, error, sizeof(error)))
	{
		platform_log("OpenSauce post-processing: effect '%s' does not read: %s", effect->name, error);
		free(effect->binary);
		memset(effect, 0, sizeof(*effect));
		return -1;
	}
	effect->used = TRUE;
	effect->technique = d3d9_effect_technique_find(&effect->effect, technique);
	if (effect->technique < 0 || effect->effect.techniques[effect->technique].pass_count > PPGL_MAXIMUM_PASSES)
	{
		platform_log("OpenSauce post-processing: effect '%s' has no technique '%s' to draw with", effect->name,
			technique ? technique : "");
		ppgl_effect_free(effect);
		return -1;
	}
	effect->values = calloc(effect->effect.parameter_count + 1, sizeof(*effect->values));
	effect->textures = calloc(effect->effect.parameter_count + 1, sizeof(*effect->textures));
	effect->texture_targets = calloc(effect->effect.parameter_count + 1, sizeof(*effect->texture_targets));
	effect->texture_mipmapped = calloc(effect->effect.parameter_count + 1, sizeof(*effect->texture_mipmapped));
	if (!effect->values || !effect->textures || !effect->texture_targets || !effect->texture_mipmapped)
	{
		ppgl_effect_free(effect);
		return -1;
	}
	for (parameter = 0; parameter < effect->effect.parameter_count; parameter++)
	{
		const struct d3d9_parameter *description = &effect->effect.parameters[parameter];

		if (description->default_count)
		{
			effect->values[parameter] = malloc(description->default_count * sizeof(uint32_t));
			if (effect->values[parameter])
				memcpy(effect->values[parameter], description->defaults, description->default_count * sizeof(uint32_t));
		}
	}
	for (semantic = 0; semantic < NUMBER_OF_PPGL_SEMANTICS; semantic++)
		effect->semantics[semantic] = d3d9_effect_parameter_by_semantic(&effect->effect, ppgl_semantics[semantic]);
	if (effect->semantics[_ppgl_semantic_ortho] < 0)
	{
		/* (c_shader_postprocess::ValidateImpl: a shader without it is not
		valid) */
		platform_log("OpenSauce post-processing: effect '%s' has no ORTHOWORLDVIEWPROJECTION, so OpenSauce would not "
			"draw it either", effect->name);
		ppgl_effect_free(effect);
		return -1;
	}
	{
		/* the G-buffer's channels (GBuffer.hpp) */
		static const uint32_t channels[] = { 0, 0, 1, 0, 1, 2, 3 };

		for (semantic = _ppgl_semantic_depth_x; semantic <= _ppgl_semantic_index_y; semantic++)
		{
			uint32_t value = channels[semantic - _ppgl_semantic_depth_x];

			ppgl_value_write(effect, effect->semantics[semantic], _opensauce_pp_value_integer, &value, 1);
		}
	}
	return index;
}

int halo_opensauce_pp_parameter_exists(long effect_index, const char *name)
{
	struct ppgl_effect *effect = ppgl_effect_get(effect_index);

	return effect && d3d9_effect_parameter_find(&effect->effect, name) >= 0;
}

void halo_opensauce_pp_unload(void)
{
	int index;

#ifdef HALO_BGFX
	bgpp_unload();
#endif
	for (index = 0; index < PPGL_MAXIMUM_EFFECTS; index++)
	{
		if (ppgl.effects[index].used)
			ppgl_effect_free(&ppgl.effects[index]);
	}
}

void halo_opensauce_pp_parameter_set(long effect_index, const char *name, int type, const void *values, int count)
{
	struct ppgl_effect *effect = ppgl_effect_get(effect_index);

	if (effect)
		ppgl_value_write(effect, d3d9_effect_parameter_find(&effect->effect, name), type, values, count);
}

void halo_opensauce_pp_parameter_texture(long effect_index, const char *name, const void *texture)
{
	struct ppgl_effect *effect = ppgl_effect_get(effect_index);
	struct xgpu_texture_description description;
	GLenum target = GL_TEXTURE_2D;
	GLuint gl_texture = 0;

	if (!effect)
		return;
#ifdef HALO_BGFX
	if (bg_active())
	{
		bgpp_parameter_texture(effect_index, name, texture);
		return;
	}
#endif
	if (texture)
	{
		memset(&description, 0, sizeof(description));
		gl_texture = xgpu_texture_get((const DWORD *)texture, NULL, &target, &description);
	}
	ppgl_texture_set(effect, d3d9_effect_parameter_find(&effect->effect, name), target, gl_texture,
		gl_texture && description.levels > 1);
}

int halo_opensauce_pp_stage_begin(const struct opensauce_pp_frame *frame, int stage)
{
	struct render_target_entry *target;
	GLuint source;

	(void)stage;
#ifdef HALO_BGFX
	if (bg_active())
		return halo_opensauce_pp_available() && bgpp_stage_begin(frame);
#endif
	if (!halo_opensauce_pp_available() || !ppgl_prepare())
		return FALSE;
	ppgl_log_once(5, "a stage begun (the map has post-processing)");
	target = render_target_get(&device.back_buffer);
	if (!target || target->target.gl_width < 8 || target->target.gl_height < 8)
		return FALSE;
	ppgl.frame = *frame;
	/* (the 3D view all of the back buffer: one window; split screen's
	windows are left as they are) */
	{
		GLint corners[4];

		corners[0] = scaled_pixel(frame->x0, target->target.scale[0]);
		corners[1] = scaled_pixel(frame->y0, target->target.scale[1]);
		corners[2] = scaled_pixel(frame->x1, target->target.scale[0]);
		corners[3] = scaled_pixel(frame->y1, target->target.scale[1]);
		if (corners[0] > 2 || corners[1] > 2 || corners[2] < (GLint)target->target.gl_width - 2 ||
			corners[3] < (GLint)target->target.gl_height - 2)
		{
			ppgl_log_once(3, "the 3D view is not the whole screen (split screen): not drawn there");
			return FALSE;
		}
	}
	ppgl.back_buffer = target;
	ppgl_targets_fit(target->target.gl_width, target->target.gl_height, target->target.format);
	render_target_resolve(&target->target);
	source = framebuffer_get(target->target.texture, 0);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, source);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ppgl.main_framebuffers[0]);
	glDisable(GL_SCISSOR_TEST);
	glBlitFramebuffer(0, 0, (GLint)ppgl.width, (GLint)ppgl.height, 0, 0, (GLint)ppgl.width, (GLint)ppgl.height,
		GL_COLOR_BUFFER_BIT, GL_NEAREST);
	ppgl.main_chain.scene = 0;
	ppgl.main_chain.current = 1;
	ppgl.main_chain.next = 2;
	if (!ppgl.secondary_chain.current && !ppgl.secondary_chain.next)
	{
		ppgl.secondary_chain.current = 0;
		ppgl.secondary_chain.next = 1;
	}
	ppgl_gbuffer_make();
	ppgl.stage_active = TRUE;
	return TRUE;
}

void halo_opensauce_pp_effect_begin(void)
{
	ppgl.main_chain.first_render = TRUE;
	ppgl.secondary_chain.first_render = TRUE;
}

void halo_opensauce_pp_effect_end(int drew, float fade)
{
#ifdef HALO_BGFX
	if (bg_active())
	{
		bgpp_effect_end(drew, fade);
		return;
	}
#endif
	if (!ppgl.stage_active || !drew)
		return;
	if (fade < 1.0f)
	{
		/* (c_shader_instance_fade::Render: the result over the scene as it
		was, by the fade) */
		ppgl_chain_scene_to_last(&ppgl.main_chain);
		ppgl_chain_flip(&ppgl.main_chain);
		ppgl_pass_state();
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glUseProgram(ppgl.fade_program);
		glUniform1f(ppgl.fade_amount, fade < 0.0f ? 0.0f : fade);
		ppgl_bind(0, GL_TEXTURE_2D, ppgl.main_textures[ppgl.main_chain.scene], ppgl.point_sampler);
		ppgl_triangle_draw(ppgl.main_framebuffers[ppgl.main_chain.current], ppgl.width, ppgl.height);
		glDisable(GL_BLEND);
		ppgl_chain_flip(&ppgl.main_chain);
	}
	ppgl_chain_scene_to_last(&ppgl.main_chain);
}

int halo_opensauce_pp_shader_draw(long effect_index, const struct opensauce_pp_pass *passes, int pass_count,
	const short tessellation[2], const float x_bounds[2], const float y_bounds[2])
{
	struct ppgl_effect *effect = ppgl_effect_get(effect_index);
	const struct d3d9_technique *technique;
	float floats[D3D9_SHADER_FLOAT_REGISTERS * 4], int_floats[D3D9_SHADER_INT_REGISTERS * 4];
	int ints[D3D9_SHADER_INT_REGISTERS * 4], bools[D3D9_SHADER_BOOL_REGISTERS];
	float values[16];
	int register_index;
	int pass_index, index_count;

#ifdef HALO_BGFX
	if (bg_active())
		return bgpp_shader_draw(effect_index, passes, pass_count, tessellation, x_bounds, y_bounds);
#endif
	if (!effect || !ppgl.stage_active)
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
	ppgl_texture_set(effect, effect->semantics[_ppgl_semantic_depth], GL_TEXTURE_2D, ppgl.gbuffer_textures[0], FALSE);
	ppgl_texture_set(effect, effect->semantics[_ppgl_semantic_velocity], GL_TEXTURE_2D, ppgl.gbuffer_textures[1], FALSE);
	ppgl_texture_set(effect, effect->semantics[_ppgl_semantic_normals], GL_TEXTURE_2D, ppgl.gbuffer_textures[2], FALSE);
	ppgl_texture_set(effect, effect->semantics[_ppgl_semantic_index], GL_TEXTURE_2D, ppgl.gbuffer_textures[2], FALSE);
	index_count = ppgl_quad(tessellation, x_bounds, y_bounds);
	for (pass_index = 0; pass_index < pass_count; pass_index++)
	{
		const struct opensauce_pp_pass *pass = &passes[pass_index];
		struct ppgl_chain *chain = pass->render_chain == 1 ? &ppgl.secondary_chain : &ppgl.main_chain;
		GLuint *chain_framebuffers = pass->render_chain == 1 ? ppgl.secondary_framebuffers : ppgl.main_framebuffers;
		struct ppgl_program *program = ppgl_program_get(effect, pass_index);
		const struct d3d9_pass *effect_pass = &technique->passes[pass_index];
		const struct d3d9_shader *vertex_shader, *pixel_shader;
		float fix[4];
		unsigned long sampler;

		if (!program)
			return FALSE;
		vertex_shader = &effect->effect.shaders[effect_pass->vertex_shader];
		pixel_shader = &effect->effect.shaders[effect_pass->pixel_shader];
		/* the chains' textures as this pass reads them */
		ppgl_texture_set(effect, effect->semantics[_ppgl_semantic_scene], GL_TEXTURE_2D,
			ppgl.main_textures[ppgl.main_chain.scene], FALSE);
		ppgl_texture_set(effect, effect->semantics[_ppgl_semantic_source], GL_TEXTURE_2D,
			ppgl.main_textures[ppgl.main_chain.first_render ? ppgl.main_chain.scene : ppgl.main_chain.next], FALSE);
		if (ppgl.secondary_chain.first_render &&
			!PPGL_TEST_FLAG(pass->flags, _opensauce_pp_pass_clear_buffer_texture_bit))
		{
			ppgl_texture_set(effect, effect->semantics[_ppgl_semantic_buffer], GL_TEXTURE_2D,
				ppgl.main_textures[ppgl.main_chain.scene], FALSE);
		}
		else
		{
			ppgl_texture_set(effect, effect->semantics[_ppgl_semantic_buffer], GL_TEXTURE_2D,
				ppgl.secondary_textures[ppgl.secondary_chain.next], FALSE);
		}
		ppgl_pass_state();
		if (PPGL_TEST_FLAG(pass->flags, _opensauce_pp_pass_copy_scene_to_target_bit))
		{
			glBindFramebuffer(GL_READ_FRAMEBUFFER, ppgl.main_framebuffers[ppgl.main_chain.scene]);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, chain_framebuffers[chain->current]);
			glBlitFramebuffer(0, 0, (GLint)ppgl.width, (GLint)ppgl.height, 0, 0, (GLint)ppgl.width,
				(GLint)ppgl.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
		if (PPGL_TEST_FLAG(pass->flags, _opensauce_pp_pass_clear_buffer_texture_bit))
		{
			glBindFramebuffer(GL_FRAMEBUFFER, ppgl.secondary_framebuffers[ppgl.secondary_chain.next]);
			glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		glBindFramebuffer(GL_FRAMEBUFFER, chain_framebuffers[chain->current]);
		glViewport(0, 0, (GLsizei)ppgl.width, (GLsizei)ppgl.height);
		if (PPGL_TEST_FLAG(pass->flags, _opensauce_pp_pass_clear_target_bit))
		{
			glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		glUseProgram(program->program);
		ppgl_shader_registers(effect, vertex_shader, floats, ints, bools);
		glUniform4fv(program->vertex_floats, (GLsizei)program->vertex_float_count, floats);
		for (register_index = 0; register_index < D3D9_SHADER_INT_REGISTERS * 4; register_index++)
			int_floats[register_index] = (float)ints[register_index];
		glUniform4fv(program->vertex_ints, D3D9_SHADER_INT_REGISTERS, int_floats);
		glUniform1iv(program->vertex_bools, D3D9_SHADER_BOOL_REGISTERS, bools);
		ppgl_shader_registers(effect, pixel_shader, floats, ints, bools);
		glUniform4fv(program->pixel_floats, (GLsizei)program->pixel_float_count, floats);
		for (register_index = 0; register_index < D3D9_SHADER_INT_REGISTERS * 4; register_index++)
			int_floats[register_index] = (float)ints[register_index];
		glUniform4fv(program->pixel_ints, D3D9_SHADER_INT_REGISTERS, int_floats);
		glUniform1iv(program->pixel_bools, D3D9_SHADER_BOOL_REGISTERS, bools);
		fix[0] = 1.0f / (float)ppgl.width;
		fix[1] = 1.0f / (float)ppgl.height;
		fix[2] = XGPU_GL_LEGACY ? -1.0f : 1.0f;
		fix[3] = 0.0f;
		glUniform4fv(program->pixel_fix, 1, fix);
		for (sampler = 0; sampler < D3D9_SHADER_SAMPLERS; sampler++)
		{
			if (program->pixel_samplers[sampler])
				ppgl_sampler_bind(effect, pixel_shader, sampler, program->pixel_samplers[sampler]);
		}
		glBindVertexArray(ppgl.quad_vertex_array);
		glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_SHORT, (const void *)0);
		chain->first_render = FALSE;
		ppgl_chain_flip(chain);
	}
	return TRUE;
}

int halo_opensauce_pp_bloom(float size, float exposure, float mix_amount, const float minimum[3],
	const float maximum[3])
{
	float small_size[4], full_size[4], amounts[4], minimum_color[4], maximum_color[4];

#ifdef HALO_BGFX
	if (bg_active())
		return bgpp_bloom(size, exposure, mix_amount, minimum, maximum);
#endif
	if (!ppgl.stage_active)
		return FALSE;
	memset(small_size, 0, sizeof(small_size));
	memset(full_size, 0, sizeof(full_size));
	memset(amounts, 0, sizeof(amounts));
	memset(minimum_color, 0, sizeof(minimum_color));
	memset(maximum_color, 0, sizeof(maximum_color));
	memcpy(minimum_color, minimum, sizeof(float) * 3);
	memcpy(maximum_color, maximum, sizeof(float) * 3);
	small_size[0] = (float)ppgl.bloom_width;
	small_size[1] = (float)ppgl.bloom_height;
	full_size[0] = (float)ppgl.width;
	full_size[1] = (float)ppgl.height;
	halo_opensauce_pp_effect_begin();
	ppgl_pass_state();
	/* the bright pass, into the eighth */
	glUseProgram(ppgl.bloom_programs[0]);
	glUniform4fv(ppgl.bloom_uniforms[0][0], 1, minimum_color);
	glUniform4fv(ppgl.bloom_uniforms[0][1], 1, maximum_color);
	glUniform4fv(ppgl.bloom_uniforms[0][2], 1, small_size);
	ppgl_bind(0, GL_TEXTURE_2D, ppgl.main_textures[ppgl.main_chain.scene], ppgl.linear_sampler);
	ppgl_triangle_draw(ppgl.bloom_framebuffers[0], ppgl.bloom_width, ppgl.bloom_height);
	/* the two blurs (their spread the bloom's size in the eighth's texels:
	OpenSauce's eighth is drawn in a corner of a whole-screen target, and
	offset by its size in that target's) */
	glUseProgram(ppgl.bloom_programs[1]);
	glUniform4fv(ppgl.bloom_uniforms[1][0], 1, small_size);
	glUniform1f(ppgl.bloom_uniforms[1][1], size);
	ppgl_bind(0, GL_TEXTURE_2D, ppgl.bloom_textures[0], ppgl.linear_sampler);
	ppgl_triangle_draw(ppgl.bloom_framebuffers[1], ppgl.bloom_width, ppgl.bloom_height);
	ppgl_bind(0, GL_TEXTURE_2D, ppgl.bloom_textures[1], ppgl.linear_sampler);
	ppgl_triangle_draw(ppgl.bloom_framebuffers[0], ppgl.bloom_width, ppgl.bloom_height);
	/* the composite over the scene, into the chain */
	glUseProgram(ppgl.bloom_programs[2]);
	amounts[0] = exposure;
	amounts[1] = mix_amount;
	glUniform4fv(ppgl.bloom_uniforms[2][0], 1, full_size);
	glUniform4fv(ppgl.bloom_uniforms[2][1], 1, amounts);
	ppgl_bind(0, GL_TEXTURE_2D, ppgl.main_textures[ppgl.main_chain.scene], ppgl.point_sampler);
	ppgl_bind(1, GL_TEXTURE_2D, ppgl.bloom_textures[0], ppgl.linear_sampler);
	ppgl_triangle_draw(ppgl.main_framebuffers[ppgl.main_chain.current], ppgl.width, ppgl.height);
	ppgl_chain_flip(&ppgl.main_chain);
	halo_opensauce_pp_effect_end(TRUE, 1.0f);
	return TRUE;
}

void halo_opensauce_pp_stage_end(void)
{
	struct render_target_entry *target = ppgl.back_buffer;

#ifdef HALO_BGFX
	if (bg_active())
	{
		bgpp_stage_end();
		return;
	}
#endif
	if (!ppgl.stage_active)
		return;
	ppgl.stage_active = FALSE;
	/* (debug: HALO_OPENSAUCE_PP_SHOW=depth, velocity or normals shows that
	part of the G-buffer in the 3D view's place) */
	{
		const char *show = getenv("HALO_OPENSAUCE_PP_SHOW");
		int part = !show ? -1 : !strcmp(show, "depth") ? 0 : !strcmp(show, "velocity") ? 1 :
			!strcmp(show, "normals") ? 2 : -1;

		if (part >= 0 && target)
		{
			glBindFramebuffer(GL_READ_FRAMEBUFFER, ppgl.gbuffer_framebuffer);
			glReadBuffer(GL_COLOR_ATTACHMENT0 + (GLenum)part);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ppgl.main_framebuffers[1]);
			glBlitFramebuffer(0, 0, (GLint)ppgl.width, (GLint)ppgl.height, 0, 0, (GLint)ppgl.width, (GLint)ppgl.height,
				GL_COLOR_BUFFER_BIT, GL_NEAREST);
			glReadBuffer(GL_COLOR_ATTACHMENT0);
			ppgl.main_chain.scene = 1;
		}
	}
	if (target && ppgl.main_chain.scene != 0)
	{
		/* the result into the back buffer: its multisampled renderbuffer
		with multisampling (drawn: a blit can't go into one) */
		GLuint destination = target->target.samples ? framebuffer_find(target->target.multisample, 0, TRUE) :
			framebuffer_get(target->target.texture, 0);

		ppgl_pass_state();
		glUseProgram(ppgl.copy_program);
		ppgl_bind(0, GL_TEXTURE_2D, ppgl.main_textures[ppgl.main_chain.scene], ppgl.point_sampler);
		ppgl_triangle_draw(destination, ppgl.width, ppgl.height);
		if (target->target.samples)
			target->target.unresolved = TRUE;
		target->target.written = ++render_target_write_serial;
	}
	glActiveTexture(GL_TEXTURE0);
	glBindVertexArray(device.vertex_array);
	xgpu_gl_state_invalidate();
}

#else

/* (no post-processing here: OpenGL ES, the consoles, the server) */
int halo_opensauce_pp_available(void) { return 0; }
long halo_opensauce_pp_effect_load(const void *binary, unsigned long size, const char *name, const char *technique)
{
	(void)binary;
	(void)size;
	(void)name;
	(void)technique;
	return -1;
}
int halo_opensauce_pp_parameter_exists(long effect, const char *name)
{
	(void)effect;
	(void)name;
	return 0;
}
void halo_opensauce_pp_unload(void) { }
int halo_opensauce_pp_stage_begin(const struct opensauce_pp_frame *frame, int stage)
{
	(void)frame;
	(void)stage;
	return 0;
}
void halo_opensauce_pp_effect_begin(void) { }
void halo_opensauce_pp_effect_end(int drew, float fade)
{
	(void)drew;
	(void)fade;
}
void halo_opensauce_pp_parameter_set(long effect, const char *name, int type, const void *values, int count)
{
	(void)effect;
	(void)name;
	(void)type;
	(void)values;
	(void)count;
}
void halo_opensauce_pp_parameter_texture(long effect, const char *name, const void *texture)
{
	(void)effect;
	(void)name;
	(void)texture;
}
int halo_opensauce_pp_shader_draw(long effect, const struct opensauce_pp_pass *passes, int pass_count,
	const short tessellation[2], const float x_bounds[2], const float y_bounds[2])
{
	(void)effect;
	(void)passes;
	(void)pass_count;
	(void)tessellation;
	(void)x_bounds;
	(void)y_bounds;
	return 0;
}
int halo_opensauce_pp_bloom(float size, float exposure, float mix_amount, const float minimum[3],
	const float maximum[3])
{
	(void)size;
	(void)exposure;
	(void)mix_amount;
	(void)minimum;
	(void)maximum;
	return 0;
}
void halo_opensauce_pp_stage_end(void) { }

#endif
