/*
MACOS_VIDEO.C

macOS-only parts of the platform layer's video, compiled with the host's own
ABI and headers (like port/linux/src/posix_*.c).
*/

/* (Apple deprecated OpenGL; it is still what macOS has for it) */
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>

/* SDL 3.4 waits for vertical sync on a display link that the main thread's
run loop drives. The game draws its first frames before its event loop runs
(rasterizer_preinitialize), so that wait never ends. sdl_platform.c turns
SDL's swap interval off and sets the context's own instead, which paces the
buffer swap without the run loop. */
void macos_set_swap_interval(int interval)
{
	CGLContextObj context = CGLGetCurrentContext();
	GLint value = interval;

	if (context)
		CGLSetParameter(context, kCGLCPSwapInterval, &value);
}

#include <objc/message.h>
#include <objc/runtime.h>

/* whether the window (an NSWindow) can't be seen at all: covered by others,
minimized or on another space. bgfx's Metal shows nothing then, so nothing
waits for the display (d3d8_bgfx.inl paces the frames itself) */
int macos_window_occluded(void *window)
{
	typedef unsigned long (*occlusion_state)(void *, SEL);

	if (!window)
		return 0;
	/* (NSWindowOcclusionStateVisible) */
	return !(((occlusion_state)objc_msgSend)(window, sel_registerName("occlusionState")) & (1ul << 1));
}

/* whether the window (an NSWindow) is in a full-screen Space of its own,
macOS's full screen: its style has NSWindowStyleMaskFullScreen */
int macos_window_in_full_screen_space(void *window)
{
	typedef unsigned long (*style_mask)(void *, SEL);

	if (!window)
		return 0;
	return (((style_mask)objc_msgSend)(window, sel_registerName("styleMask")) & (1ul << 14)) != 0;
}

/* starts the window's move into or out of its full-screen Space, as the
green button does */
void macos_window_toggle_full_screen_space(void *window)
{
	typedef void (*toggle)(void *, SEL, void *);

	if (window)
		((toggle)objc_msgSend)(window, sel_registerName("toggleFullScreen:"), NULL);
}
