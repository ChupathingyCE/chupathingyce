/* The Metal renderer's display.max_fps limit, as pure arithmetic so native
   tests can check it (tools/test_metal_frame_limit.py). Same rules as the
   OpenGL renderer's platform_video_swap (port/linux/src/sdl_platform.c):
   with vsync on, or display.max_fps negative, there is no limit; 0 means
   twice the display's refresh rate (60 Hz when it is unknown). A GPU never
   left idle can hang (Intel's Raptor Lake graphics); the limit gives it a
   rest every frame. */
#ifndef HALO_METAL_FRAME_LIMIT_H
#define HALO_METAL_FRAME_LIMIT_H
#include <stdint.h>

/* the time between frames, in nanoseconds, or 0 for no limit */
static inline uint64_t metal_frame_interval_ns(int vsync, long max_fps, float refresh_hz)
{
    float rate;

    if (vsync || max_fps < 0)
        return 0;
    rate = (float)max_fps;
    if (!max_fps)
        rate = 2.0f * (refresh_hz > 0.0f ? refresh_hz : 60.0f);
    return (uint64_t)(1e9f / rate);
}

/* how long to wait at now (nanoseconds) for the frame due at *next_frame,
   and moves *next_frame on by interval. A frame more than an interval late
   starts the count again, so a stall isn't followed by a burst. */
static inline uint64_t metal_frame_wait_ns(uint64_t now, uint64_t *next_frame, uint64_t interval)
{
    uint64_t wait = 0;

    if (!interval)
        return 0;
    if (*next_frame > now)
    {
        wait = *next_frame - now;
        now = *next_frame;
    }
    *next_frame = now - *next_frame > interval ? now + interval : *next_frame + interval;
    return wait;
}
#endif
