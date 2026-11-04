/*
GL_FUNCTIONS.C

Run-time resolution of the OpenGL entry points listed in gl.h.
*/

#include "platform.h"
#define GL_FUNCTIONS_DEFINE
#include "gl.h"

#include <SDL3/SDL.h>
#include <string.h>

#define GL_DEFINE_FUNCTION(name) __typeof__(&name) halo_##name;
GL_FUNCTIONS(GL_DEFINE_FUNCTION)
#ifdef HALO_GL_ES
GL_OPTIONAL_FUNCTIONS(GL_DEFINE_FUNCTION)
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
	return success;
}
