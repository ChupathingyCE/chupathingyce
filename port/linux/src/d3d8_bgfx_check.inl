/*
D3D8_BGFX_CHECK.INL

The bgfx shader check (game issue #1833): HALO_BGFX_SHADER_CHECK=<folder>
compiles every shader bgfx can be asked for, for each backend the build has,
and the game stops with 0 when all of them compile and 1 when one doesn't.
bgfx's shaders are made from the translators' GLSL at run time
(d3d8_bgfx.inl), so a shader that breaks on one backend was otherwise only
found on a machine that draws with it.

What it compiles, through the very functions a draw's shaders go through
(bg_vulkan_glsl, bg_compile):

- the port's own: the present's blit, HDR tone mapping, the copy, the CRT
  filter, the overlay, FXAA and SMAA's three passes;
- each of the game's vertex programs (rasterizer_xbox_vertex_shaders.c's
  table), as bg_vertex_shader translates them;
- the pixel shader of every key in the stock maps' list
  (port/assets/shaders/stock.keys), as bg_fragment_shader is asked for it
  at a map's loading screen (d3d8_shader_prep.inl's prep_start), and again
  without display.hdr's glow.

The backends: Vulkan (SPIR-V in bgfx's container) everywhere but macOS;
Direct3D 11 on Windows (SPIRV-Cross's HLSL compiled by d3dcompiler_47.dll);
Metal on macOS (SPIRV-Cross's Metal source, compiled by the system's Metal
device as bgfx's Metal renderer compiles it). Nothing is drawn and no window
is made: it runs as the device is made (d3d8_gl.c), before the video starts.

The folder gets report.txt (a line for each backend, then each failure);
each failure's GLSL (as the backend was given it) and its error go in
<backend>/<name>.glsl and .txt beside it, and Vulkan's SPIR-V for every shader
in vulkan/<name>.spv, for spirv-val. tools/bgfx_shader_check.py runs it, and
CI runs that on each 64-bit desktop build.
*/

/* the game's vertex programs (source/rasterizer/xbox/rasterizer_xbox_vertex_shaders.h:
its table, statically made, of code that starts with a header word, the
instruction count in its high half) */
struct vertex_shader_entry
{
	void const *declaration;
	void const *code;
	unsigned long handle;
	long instruction_count;
};
#define BG_CHECK_VERTEX_PROGRAMS 67
extern struct vertex_shader_entry vertex_shader_table[BG_CHECK_VERTEX_PROGRAMS];

#ifdef __APPLE__
/* (port/macos/src/macos_video.c) */
int macos_metal_library_compile(void *device, const char *source, char *why, unsigned long size);
void *macos_metal_default_device(void);
#endif

static struct
{
	char folder[1024];
	FILE *report;
	const char *backend;
	unsigned long compiled, failed;
#ifdef __APPLE__
	void *metal_device;
#endif
} bg_check;

static void bg_check_write(const char *name, const char *extension, const void *data, size_t size)
{
	char path[1200];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s/%s.%s", bg_check.folder, bg_check.backend, name, extension);
	if ((file = fopen(path, "wb")) != NULL)
	{
		fwrite(data, 1, size, file);
		fclose(file);
	}
}

/* one shader, as the backend is given it: FALSE if it does not compile */
static BOOL bg_check_shader(const char *glsl, BOOL fragment, int flags, const char *name)
{
	char *source, *code = NULL, *error = NULL;
	unsigned long size = 0;

	if (!glsl)
	{
		error = strdup("the translator made no GLSL");
		source = NULL;
	}
	else if ((source = bg_vulkan_glsl(glsl, fragment, flags, NULL)) == NULL)
	{
		error = strdup("its GLSL could not be rewritten for bgfx");
	}
	else
	{
		code = bg_compile(source, fragment, name, &error, &size);
#ifdef __APPLE__
		if (code)
		{
			char why[8192];

			if (!macos_metal_library_compile(bg_check.metal_device, code, why, sizeof(why)))
			{
				error = strdup(why);
				free(code);
				code = NULL;
			}
		}
#endif
		if (code && bg_vulkan)
		{
			unsigned long count = 0;
			char *ignored = NULL;
			unsigned int *words = bg_spirv(source, fragment, &count, &ignored);

			if (words)
				bg_check_write(name, "spv", words, count * sizeof(*words));
			free(words);
			free(ignored);
		}
	}
	if (code)
	{
		bg_check.compiled++;
		free(code);
		free(source);
		return TRUE;
	}
	bg_check.failed++;
	if (source)
		bg_check_write(name, "glsl", source, strlen(source));
	else if (glsl)
		bg_check_write(name, "glsl", glsl, strlen(glsl));
	bg_check_write(name, "txt", error ? error : "", error ? strlen(error) : 0);
	fprintf(bg_check.report, "%s: %s FAILED: %s\n", bg_check.backend, name, error ? error : "");
	fprintf(stderr, "bgfx shader check: %s: %s FAILED: %s\n", bg_check.backend, name, error ? error : "");
	free(error);
	free(source);
	return FALSE;
}

/* the pixel keys of the stock maps' list, each once (malloc'd, *count of them) */
static struct xgpu_program_key *bg_check_stock_keys(unsigned long *count)
{
	unsigned long size = 0, index, kept = 0;
	const char *text = xgpu_shader_keys_stock(&size), *line;
	struct xgpu_shader_key_list list = { 0 };
	struct xgpu_program_key *keys;

	*count = 0;
	if (!text)
		return NULL;
	for (line = text; line && line < text + size; line = strchr(line, '\n'), line = line ? line + 1 : NULL)
	{
		char map[64];

		if (sscanf(line, "map %63s", map) == 1)
			xgpu_shader_keys_parse(text, size, map, &list);
	}
	if (!list.count || (keys = malloc(list.count * 2 * sizeof(*keys))) == NULL)
	{
		xgpu_shader_key_list_free(&list);
		return NULL;
	}
	for (index = 0; index < list.count * 2; index++)
	{
		struct xgpu_program_key key = list.keys[index / 2];
		unsigned long other;

		/* (as prep_start asks bgfx for it: each vertex lit, without the
		Light effects or counted samples; and the same without the glow) */
		key.pixel.per_pixel_lighting = 0;
		key.pixel.plasma_model = 0;
		key.pixel.count_samples = 0;
		if (index & 1)
		{
			if (!key.pixel.hdr)
				continue;
			key.pixel.hdr = 0;
		}
		key.flags &= _xgpu_key_depth_bias_bit;
		key.vertex_code = 0;
		key.packed_mask = 0;
		for (other = 0; other < kept; other++)
		{
			if (keys[other].flags == key.flags && !memcmp(&keys[other].pixel, &key.pixel, sizeof(key.pixel)))
				break;
		}
		if (other == kept)
			keys[kept++] = key;
	}
	xgpu_shader_key_list_free(&list);
	*count = kept;
	return keys;
}

/* everything, on the backend now set (bg_vulkan, bg_d3d11) */
static void bg_check_backend(const char *backend, const struct xgpu_program_key *keys, unsigned long key_count)
{
	unsigned long index, before = bg_check.failed, total_before = bg_check.compiled + bg_check.failed;
	char path[1200], name[64];
	int pass;
	double start = stats_seconds();

	bg_check.backend = backend;
	snprintf(path, sizeof(path), "%s/%s", bg_check.folder, backend);
	SDL_CreateDirectory(path);

	bg_check_shader(bg_blit_vertex_source, FALSE, 0, "blit_vertex");
	bg_check_shader(bg_blit_fragment_source, TRUE, 0, "blit_pixel");
	bg_check_shader(bg_tone_map_fragment_source, TRUE, 0, "tone_map_pixel");
	bg_check_shader(bg_copy_fragment_source, TRUE, 0, "copy_pixel");
	bg_check_shader(bg_crt_fragment_source, TRUE, 0, "crt_pixel");
	bg_check_shader(bg_overlay_vertex_source, FALSE, 0, "overlay_vertex");
	bg_check_shader(bg_overlay_fragment_source, TRUE, 0, "overlay_pixel");
	for (pass = 0; pass < 4; pass++)
	{
		char *text = bg_anti_alias_text(pass);

		snprintf(name, sizeof(name), "anti_alias_%d_pixel", pass);
		bg_check_shader(text, TRUE, 0, name);
		free(text);
	}
	for (index = 0; index < BG_CHECK_VERTEX_PROGRAMS; index++)
	{
		const DWORD *code = vertex_shader_table[index].code;
		char *glsl;

		if (!code)
			continue;
		glsl = nv2a_vertex_shader_to_glsl(code + 1, code[0] >> 16, 0, NULL, FALSE);
		snprintf(name, sizeof(name), "vertex_%02lu", index);
		bg_check_shader(glsl, FALSE, 0, name);
		free(glsl);
	}
	for (index = 0; index < key_count; index++)
	{
		char *glsl;

		nv2a_pixel_shader_lod_bias = TRUE;
		glsl = nv2a_pixel_shader_to_glsl(&keys[index].pixel);
		nv2a_pixel_shader_lod_bias = FALSE;
		snprintf(name, sizeof(name), "pixel_%04lu%s", index,
			keys[index].flags & _xgpu_key_depth_bias_bit ? "_biased" : "");
		bg_check_shader(glsl, TRUE, keys[index].flags & _xgpu_key_depth_bias_bit ? _bg_shader_depth_bias : 0, name);
		free(glsl);
	}
	fprintf(bg_check.report, "%s: %lu shaders, %lu failed, %.1f s\n", backend,
		bg_check.compiled + bg_check.failed - total_before, bg_check.failed - before, stats_seconds() - start);
	fprintf(stderr, "bgfx shader check: %s: %lu shaders, %lu failed\n", backend,
		bg_check.compiled + bg_check.failed - total_before, bg_check.failed - before);
	fflush(bg_check.report);
}

/* HALO_BGFX_SHADER_CHECK: the check, and the game stops (d3d8_gl.c, as the
device is made); nothing otherwise */
static void bg_shader_check_if_asked(void)
{
	const char *folder = getenv("HALO_BGFX_SHADER_CHECK");
	struct xgpu_program_key *keys;
	unsigned long key_count = 0;
	char path[1200];

	if (!folder || !*folder)
		return;
	snprintf(bg_check.folder, sizeof(bg_check.folder), "%s", folder);
	SDL_CreateDirectory(bg_check.folder);
	snprintf(path, sizeof(path), "%s/report.txt", bg_check.folder);
	if ((bg_check.report = fopen(path, "w")) == NULL)
	{
		fprintf(stderr, "bgfx shader check: %s can't be written\n", path);
		exit(2);
	}
	keys = bg_check_stock_keys(&key_count);
	fprintf(bg_check.report, "pixel keys: %lu (the stock maps' list)\n", key_count);
	if (!key_count)
		fprintf(bg_check.report, "the stock maps' list FAILED: no keys read\n");
	glslang_initialize_process();
#ifdef __APPLE__
	bg_vulkan = bg_d3d11 = FALSE;
	if ((bg_check.metal_device = macos_metal_default_device()) == NULL)
	{
		fprintf(bg_check.report, "metal: FAILED: no Metal device\n");
		bg_check.failed++;
	}
	else
	{
		bg_check_backend("metal", keys, key_count);
	}
#else
	bg_vulkan = TRUE;
	bg_d3d11 = FALSE;
	bg_check_backend("vulkan", keys, key_count);
#ifdef _WIN32
	bg_vulkan = FALSE;
	bg_d3d11 = TRUE;
	if (!bg_dxbc_available())
	{
		fprintf(bg_check.report, "d3d11: FAILED: d3dcompiler_47.dll is missing\n");
		bg_check.failed++;
	}
	else
	{
		fprintf(bg_check.report, "d3d11: d3dcompiler_47.dll %s\n", bg_d3d_compiler_identity);
		bg_check_backend("d3d11", keys, key_count);
	}
#endif
#endif
	glslang_finalize_process();
	free(keys);
	fprintf(bg_check.report, "%s\n", bg_check.failed || !key_count ? "FAIL" : "PASS");
	fclose(bg_check.report);
	exit(bg_check.failed || !key_count ? 1 : 0);
}
