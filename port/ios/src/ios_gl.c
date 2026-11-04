/*
IOS_GL.C

The renderer's OpenGL ES helpers (port/linux/src/xgpu.h, under HALO_ANDROID),
as chupathingyce's port/android/host/host_gl.c has them, for iOS's OpenGL ES
3.0: the same calls, without EGL. iOS's driver is Apple's, on Metal, and
offers none of the extensions the renderer looks for beyond ES 3.0 (no copy
image, border clamp, base vertex or S3TC), so the renderer takes its ES 3.0
paths (d3d8_gl.c, gl_initialize).

The renderer's own functions are loaded by name through SDL
(gl_functions.c); these few are called directly, and come from the OpenGLES
framework.
*/

#include <GLES3/gl32.h>
#include <dlfcn.h>
#include <string.h>

void *host_gl_resolve(const char *name)
{
	/* (the OpenGLES framework's exports, as SDL_GL_GetProcAddress finds them) */
	return dlsym(RTLD_DEFAULT, name);
}

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

/* copies size bytes of a buffer object (the snapshots of the visibility
tests' counters, d3d8_gl.c); ES has no glGetBufferSubData. Binds
GL_COPY_READ_BUFFER, which the renderer uses only for its copies, and leaves
it unbound. */
void host_gl_read_buffer(unsigned int buffer, unsigned int offset, unsigned int size, void *data)
{
	const void *mapping;

	glBindBuffer(GL_COPY_READ_BUFFER, buffer);
	mapping = glMapBufferRange(GL_COPY_READ_BUFFER, offset, size, GL_MAP_READ_BIT);
	if (mapping)
	{
		memcpy(data, mapping, size);
		glUnmapBuffer(GL_COPY_READ_BUFFER);
	}
	else
	{
		memset(data, 0, size);
	}
	glBindBuffer(GL_COPY_READ_BUFFER, 0);
}

/* A fence marks the end of each frame's work in the renderer's ring of
stream buffers, and a buffer is written again only once the GPU has passed
the fence of the frame that last used it (host_gl.c has the reasons). */
#define FRAME_FENCE_SLOTS 8

static GLsync frame_fences[FRAME_FENCE_SLOTS];

void host_gl_fence_frame(unsigned int slot)
{
	if (slot >= FRAME_FENCE_SLOTS)
		return;
	if (frame_fences[slot])
		glDeleteSync(frame_fences[slot]);
	frame_fences[slot] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

void host_gl_wait_frame(unsigned int slot)
{
	if (slot >= FRAME_FENCE_SLOTS || !frame_fences[slot])
		return;
	/* at most a second: a lost context must not hang the game */
	glClientWaitSync(frame_fences[slot], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
	glDeleteSync(frame_fences[slot]);
	frame_fences[slot] = NULL;
}

/* writes data into the buffer bound to target, unsynchronized and without
invalidating the range: the renderer only writes ranges no queued draw
reads (host_gl.c has why invalidation is ruinous) */
void host_gl_buffer_write(unsigned int target, unsigned int offset, unsigned int size, const void *data)
{
	void *mapping = glMapBufferRange(target, offset, size, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);

	if (!mapping)
	{
		glBufferSubData(target, offset, size, data);
		return;
	}
	memcpy(mapping, data, size);
	glUnmapBuffer(target);
}
