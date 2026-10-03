/*
GL_IOS.H

The OpenGL ES 3.1 and 3.2 entry points and enumerants the renderer names
(gl.h, d3d8_gl.c) that iOS, which stops at ES 3.0, does not declare. The
prototypes only give the run-time pointers their types: on iOS each resolves
to NULL, and the renderer takes its ES 3.0 paths (xgpu_capabilities).
*/

#ifndef __HALO_GL_IOS_H
#define __HALO_GL_IOS_H

#ifndef GL_ATOMIC_COUNTER_BUFFER
#define GL_ATOMIC_COUNTER_BUFFER 0x92c0
#endif
#ifndef GL_ATOMIC_COUNTER_BUFFER_BINDING
#define GL_ATOMIC_COUNTER_BUFFER_BINDING 0x92c1
#endif
#ifndef GL_MAX_FRAGMENT_ATOMIC_COUNTERS
#define GL_MAX_FRAGMENT_ATOMIC_COUNTERS 0x92d6
#endif

void glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex);
void glCopyImageSubData(GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ,
	GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ,
	GLsizei srcWidth, GLsizei srcHeight, GLsizei srcDepth);

#endif
