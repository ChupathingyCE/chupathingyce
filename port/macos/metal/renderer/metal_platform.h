/* The Metal renderer's platform-layer hooks (sdl_platform.c, metal_host.m):
   the window's layer, its display sync and the display.max_fps limit. */
#ifndef HALO_METAL_PLATFORM_H
#define HALO_METAL_PLATFORM_H

/* metal_host.m */
int macos_metal_attach(void *window);
void macos_metal_set_vsync(int vsync);
/* sdl_platform.c: called once a frame, after gpu_present */
void platform_video_frame_limit(void);
#endif
