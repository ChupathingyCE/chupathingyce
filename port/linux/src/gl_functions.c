/*
GL_FUNCTIONS.C

Run-time resolution of the OpenGL entry points listed in gl.h.
*/

#include "platform.h"
#define GL_FUNCTIONS_DEFINE
#include "gl.h"
#include "xgpu.h"

#include <SDL3/SDL.h>
#include <string.h>

#define GL_DEFINE_FUNCTION(name) __typeof__(&name) halo_##name;
GL_FUNCTIONS(GL_DEFINE_FUNCTION)
#ifdef HALO_GL_ES
GL_OPTIONAL_FUNCTIONS(GL_DEFINE_FUNCTION)
#endif

#ifdef XGPU_GL_LEGACY_AT_RUN_TIME
/* the path without what came after OpenGL 3.3 (xgpu.h), and its shaders'
#version line: set by sdl_platform.c with the context */
int xgpu_gl_legacy;
const char *xgpu_gl_version_line = "#version 450 core\n";
int xgpu_gl_sample_mask = 1;
#endif

void *gl_function_extra(const char *name)
{
	return (void *)SDL_GL_GetProcAddress(name);
}

int gl_functions_load(void)
{
	int success = TRUE;

/* newer than OpenGL 4.1 (macOS's newest); callers check for NULL */
#define GL_OPTIONAL_FUNCTION(name) \
	(!strcmp(#name, "glClipControl") || !strcmp(#name, "glCopyImageSubData") || \
		!strcmp(#name, "glDebugMessageCallback") || !strcmp(#name, "glBufferStorage") || \
		!strcmp(#name, "glMemoryBarrier") || !strcmp(#name, "glVertexAttribFormat") || \
		!strcmp(#name, "glVertexAttribIFormat") || !strcmp(#name, "glVertexAttribBinding") || \
		!strcmp(#name, "glBindVertexBuffer") || !strcmp(#name, "glGetQueryBufferObjectuiv") || \
		!strcmp(#name, "glBindTextures"))
#ifdef HALO_ANDROID
/* ES 3.2's; on 3.0 (iOS's newest) gl_initialize does without it */
#define GL_ES_OPTIONAL_FUNCTION(name) !strcmp(#name, "glDrawElementsBaseVertex")
#else
#define GL_ES_OPTIONAL_FUNCTION(name) 0
#endif
#ifdef HALO_CONSOLE_GLES2
	/* ES 2: what the driver has, then the port's layer fills in the ES 3
	the renderer uses (console_gles2_layer), and only then is anything
	missing a failure */
#define GL_LOAD_FUNCTION(name) \
	halo_##name = (__typeof__(halo_##name))SDL_GL_GetProcAddress(#name);
	GL_FUNCTIONS(GL_LOAD_FUNCTION)
#undef GL_LOAD_FUNCTION
	if (!console_gles2_layer())
		success = FALSE;
#define GL_CHECK_FUNCTION(name) \
	if (!halo_##name && !GL_OPTIONAL_FUNCTION(name)) \
	{ \
		platform_log("OpenGL function %s is unavailable", #name); \
		success = FALSE; \
	}
	GL_FUNCTIONS(GL_CHECK_FUNCTION)
#undef GL_CHECK_FUNCTION
#else
#define GL_LOAD_FUNCTION(name) \
	halo_##name = (__typeof__(halo_##name))SDL_GL_GetProcAddress(#name); \
	if (!halo_##name && !GL_OPTIONAL_FUNCTION(name) && !GL_ES_OPTIONAL_FUNCTION(name)) \
	{ \
		platform_log("OpenGL function %s is unavailable", #name); \
		success = FALSE; \
	}
	GL_FUNCTIONS(GL_LOAD_FUNCTION)
#undef GL_LOAD_FUNCTION
#endif
#ifdef HALO_GL_ES
	/* a missing one is no failure: the renderer uses each only where the
	context lists its extension (gl_initialize, d3d8_gl.c) */
#define GL_LOAD_OPTIONAL_FUNCTION(name) \
	halo_##name = (__typeof__(halo_##name))SDL_GL_GetProcAddress(#name);
	GL_OPTIONAL_FUNCTIONS(GL_LOAD_OPTIONAL_FUNCTION)
#undef GL_LOAD_OPTIONAL_FUNCTION
#endif
#ifdef XGPU_GL_LEGACY_AT_RUN_TIME
	/* (an older context: none of the entry points past 3.3, as on macOS,
	whatever the driver gives for their names; Windows' drivers give one
	for every name they know, not only their context's) */
	if (xgpu_gl_legacy)
	{
		GLint major = 0, minor = 0;
		int barrier;

		/* but glMemoryBarrier where the context has it (OpenGL 4.2, or
		ARB_shader_image_load_store before it): Mesa Intel's pipeline
		flush is one (draw_flush, d3d8_gl.c), and Ivy Bridge's 4.2 takes
		this path */
		if (halo_glGetIntegerv)
		{
			halo_glGetIntegerv(GL_MAJOR_VERSION, &major);
			halo_glGetIntegerv(GL_MINOR_VERSION, &minor);
		}
		barrier = major > 4 || (major == 4 && minor >= 2) ||
			SDL_GL_ExtensionSupported("GL_ARB_shader_image_load_store");
#define GL_DROP_OPTIONAL_FUNCTION(name) \
	if (GL_OPTIONAL_FUNCTION(name) && !(barrier && !strcmp(#name, "glMemoryBarrier"))) \
		halo_##name = NULL;
		GL_FUNCTIONS(GL_DROP_OPTIONAL_FUNCTION)
#undef GL_DROP_OPTIONAL_FUNCTION
	}
#endif
	return success;
}

#if defined(HALO_SHADER_PREP) && !defined(HALO_GL_ES)
/* whether the driver compiles shaders on threads of its own, asked without
waiting (GL_KHR_parallel_shader_compile, or ARB's), with as many threads as
it will (d3d8_shader_prep.inl) */
int gl_parallel_compile_start(void)
{
	typedef void (GLAPIENTRY *threads_function)(GLuint count);
	threads_function threads = NULL;

	if (SDL_GL_ExtensionSupported("GL_KHR_parallel_shader_compile"))
		threads = (threads_function)SDL_GL_GetProcAddress("glMaxShaderCompilerThreadsKHR");
	else if (SDL_GL_ExtensionSupported("GL_ARB_parallel_shader_compile"))
		threads = (threads_function)SDL_GL_GetProcAddress("glMaxShaderCompilerThreadsARB");
	else
		return FALSE;
	if (threads)
		threads(0xffffffffU);
	return TRUE;
}
#endif
