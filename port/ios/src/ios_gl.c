/*
IOS_GL.C
The host GL helpers the ES renderer calls (port/linux/src/xgpu.h), which the
Android build gets from its host (port/android/host/host_gl.c). On iOS the
game runs natively, so these call OpenGL ES directly.
*/

#define GL_SILENCE_DEPRECATION
#include <OpenGLES/ES3/gl.h>
#include <string.h>

int host_gl_has_extension(const char *name)
{
	GLint count = 0, index;

	glGetIntegerv(GL_NUM_EXTENSIONS, &count);
	for (index = 0; index < count; index++)
	{
		const char *extension = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)index);

		if (extension && !strcmp(extension, name))
			return 1;
	}
	return 0;
}

/* the visibility test counters are atomic counters (ES 3.1), which iOS does
not have; d3d8_gl.c reads them only when xgpu_capabilities.atomic_counters */
unsigned int host_gl_read_buffer_word(unsigned int buffer, unsigned int offset)
{
	(void)buffer;
	(void)offset;
	return 0;
}

/* d3d8_gl.c writes the visibility counters through the host (atomic
counters only, which iOS lacks); a write into the buffer bound to target */
void host_gl_buffer_write(unsigned int target, unsigned int offset, unsigned int size, const void *data)
{
	glBufferSubData(target, offset, size, data);
}
