/* Hooks absent from the native port. Stereo is mono; existing game clocks,
   input, and interpolation continue to run on the game's normal thread. */
#include "platform.h"
#include "port_config.h"
#include <time.h>

int platform_renderer_metal(void) { return 1; }
int platform_fixed_timestep(void) { return 0; }
void platform_clock_frame(void) { }
void platform_quiescence_wait(void) { }
void render_interpolation_next_frame_due(unsigned long microseconds) { (void)microseconds; }
void input_replay_frame(long width, long height) { (void)width; (void)height; }
double halo_frame_trace_milliseconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
}
void halo_model_counts_take(unsigned long drawn[5], unsigned long *culled) {
    int index;
    for (index = 0; index < 5; index++) drawn[index] = 0;
    *culled = 0;
}

/* Forge selectors remain on the game's normal draw, without GL calls. */
int platform_forge_selector_draw(const char *cross, const char *bar, int selected,
    int size, int view_width, int view_height) {
    (void)cross; (void)bar; (void)selected; (void)size; (void)view_width; (void)view_height;
    return 0;
}
/* The screenshot key (xinput_sdl.c) asks for nothing here yet: this renderer
   writes only debug.screenshot_every's frames (xbox_device.c). */
void platform_screenshot_request(void) { }
/* High-resolution HUD textures still decode and upload on first use. */
