#ifndef HALO_MACOS_METAL_H
#define HALO_MACOS_METAL_H
#import <Metal/Metal.h>
#include <stddef.h>
void *host_sdl_metal_layer(void);
int macos_metal_attach(void *window);
void host_fatal(const char *format, ...) __attribute__((format(printf, 1, 2), noreturn));
void host_config_string(const char *name, const char *fallback, char *out, size_t capacity);
/* Native macOS is mono. No visionOS compositor or guest address arena. */
static inline id<MTLRasterizationRateMap> host_stereo_rate_map(int eye) { (void)eye; return nil; }
static inline id<MTLBuffer> host_stereo_rate_map_parameters(int eye) { (void)eye; return nil; }
static inline int host_stereo_foveated_size(int *w, int *h, int *aw, int *ah) {
    (void)w; (void)h; (void)aw; (void)ah; return 0;
}
#endif
