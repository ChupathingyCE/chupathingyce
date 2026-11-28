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
#include <stdio.h>
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

/* (the Objective-C runtime's autorelease pools, for a thread of our own) */
void *objc_autoreleasePoolPush(void);
void objc_autoreleasePoolPop(void *pool);

/* compiles a shader's Metal source on the device (an MTLDevice, bgfx's) as
bgfx's Metal renderer does (renderer_mtl.h's newLibraryWithSource: invariance
preserved), and lets the library go. Metal keeps what it compiled, so
bgfx's own compile of the same source in its frame is quick. For bgfx's
compiler threads (d3d8_bgfx.inl): a compile is tens of milliseconds the
first time a build sees a shader, which a frame would otherwise wait for.
0 if it did not compile, and Metal's reason in why (size bytes, when not
NULL) */
int macos_metal_library_compile(void *device, const char *source, char *why, unsigned long size)
{
	typedef void *(*object_message)(void *, SEL);
	typedef void *(*string_message)(void *, SEL, const char *);
	typedef void (*flag_message)(void *, SEL, signed char);
	typedef signed char (*responds_message)(void *, SEL, SEL);
	typedef void *(*library_message)(void *, SEL, void *, void *, void **);
	void *pool, *string, *options, *library, *error = NULL;
	Class string_class = objc_getClass("NSString"), options_class = objc_getClass("MTLCompileOptions");

	if (!device || !source || !string_class || !options_class)
		return 0;
	pool = objc_autoreleasePoolPush();
	string = ((string_message)objc_msgSend)(((object_message)objc_msgSend)((void *)string_class,
		sel_registerName("alloc")), sel_registerName("initWithUTF8String:"), source);
	options = ((object_message)objc_msgSend)(((object_message)objc_msgSend)((void *)options_class,
		sel_registerName("alloc")), sel_registerName("init"));
	if (options && ((responds_message)objc_msgSend)(options, sel_registerName("respondsToSelector:"),
		sel_registerName("setPreserveInvariance:")))
		((flag_message)objc_msgSend)(options, sel_registerName("setPreserveInvariance:"), 1);
	library = string && options ? ((library_message)objc_msgSend)(device,
		sel_registerName("newLibraryWithSource:options:error:"), string, options, &error) : NULL;
	if (library)
		((object_message)objc_msgSend)(library, sel_registerName("release"));
	else if (why && size)
	{
		/* (the NSError's localizedDescription, which has the compiler's
		messages) */
		void *description = error ? ((object_message)objc_msgSend)(error, sel_registerName("localizedDescription")) :
			NULL;
		const char *text = description ? (const char *)((object_message)objc_msgSend)(description,
			sel_registerName("UTF8String")) : NULL;

		snprintf(why, size, "%s", text ? text : "Metal made no library");
	}
	if (options)
		((object_message)objc_msgSend)(options, sel_registerName("release"));
	if (string)
		((object_message)objc_msgSend)(string, sel_registerName("release"));
	objc_autoreleasePoolPop(pool);
	return library != NULL;
}

int macos_metal_library_prepare(void *device, const char *source)
{
	return macos_metal_library_compile(device, source, NULL, 0);
}

/* (Metal.framework's) */
void *MTLCreateSystemDefaultDevice(void);

/* the system's default Metal device (an MTLDevice, retained), or NULL: for
compiles with no bgfx running (d3d8_bgfx_check.inl) */
void *macos_metal_default_device(void)
{
	return MTLCreateSystemDefaultDevice();
}
