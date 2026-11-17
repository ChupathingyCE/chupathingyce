/*
HOST_VK_API.H

What the game's Vulkan renderer (port/linux/src/d3d8_vk.inl) asks of the
Vulkan backend (host_vk*.c) in the native 64-bit app (port/android/VULKAN.md).
The backend came from the guest build of the Android app (PR #152, by
thelinkin3000), where the game ran as a 32-bit image and handed the host
guest addresses; here both are one library, so these take pointers. The
backend's own files see this through host.h.
*/

#ifndef __HOST_VK_API_H
#define __HOST_VK_API_H

#include <stdint.h>
#include <sys/types.h>

/* brings Vulkan up as far as an instance and a physical device (the driver
display.vk_driver names: host_vk_driver.c; the validation layer when asked
for and the app carries it). Returns 1 and keeps them for the backend, or 0
after destroying what it made. line receives the text of the log line
"renderer: ...", for either outcome. Called once, by the thread that starts
the game, after SDL's video is up */
int host_vk_startup(const char *vk_driver, int validation, char *line, size_t size);
/* the renderer's commands (vk_commands.h), run during the call */
void host_vk_submit(const void *commands, uint32_t size);
/* the highest submission number the GPU has finished */
uint32_t host_vk_retired(void);
/* the shader service (host_vk_shaders.c): find returns a handle (1 or more)
of the shader's module, or 0 and writes a VK_SHADER_STATUS_* to *status_out;
compile queues GLSL (copied during the call) for the compile thread */
uint32_t host_vk_shader_find(uint32_t stage, uint64_t hash, uint32_t *status_out);
void host_vk_shader_compile(uint32_t stage, uint64_t hash, const char *glsl, uint32_t glsl_size);
/* whether the device reads a Vulkan format as a vertex attribute */
uint32_t host_vk_format_supported(uint32_t format);
/* whether BC1 to BC3 images can be sampled with linear filtering and written
by a copy (sent as they are if so, decoded to BGRA if not) */
uint32_t host_vk_bc_supported(void);
/* the latest count of the game's visibility test slot, in the game's pixels;
never waits */
uint32_t host_vk_visibility(uint32_t index);
/* the name of the Vulkan driver archive in use, into out (an empty string
for the phone's own driver). Returns its length */
uint32_t host_vk_driver_name(char *out, uint32_t size);
/* the game's exit: what the backend keeps on the device is written */
void host_vk_exit(void);
/* errors the validation layer has reported so far */
unsigned host_vk_validation_errors(void);

/* the game draws with Vulkan (set before the window is made: sdl_platform.c) */
extern int host_renderer_vulkan;
/* the game's window, an SDL_Window, which the backend makes its surface on */
extern void *host_vk_window;
/* set once the backend presents frames, after which its swapchain paces them */
extern int host_vk_presenting;
/* config.toml's debug.vk_present_marker and debug.vk_self_test */
extern int host_vk_present_marker;
extern int host_vk_self_test;

#endif
