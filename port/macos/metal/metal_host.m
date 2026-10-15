/* Native SDL/Metal boundary. No guest runtime or iOS lifecycle dependencies. */
#import "macos_metal.h"
#import <QuartzCore/CAMetalLayer.h>
#import "metal_vsync.h"
#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_MetalView metal_view;
void *host_sdl_metal_layer(void) { return metal_view ? SDL_Metal_GetLayer(metal_view) : NULL; }

int macos_metal_attach(void *window)
{
    metal_view = SDL_Metal_CreateView((SDL_Window *)window);
    if (!metal_view) {
        fprintf(stderr, "SDL_Metal_CreateView failed: %s\n", SDL_GetError());
        return 0;
    }
    return 1;
}

/* display.vsync: at start-up and whenever Settings re-applies the display
   (sdl_platform.c); not a GL swap interval, which a Metal window has none of */
void macos_metal_set_vsync(int vsync)
{
    CAMetalLayer *layer = (__bridge CAMetalLayer *)host_sdl_metal_layer();

    if (metal_layer_set_vsync(layer, vsync))
        fprintf(stderr, "Metal: display sync %s (display.vsync)\n", vsync ? "on" : "off");
}

void host_fatal(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

void host_config_string(const char *name, const char *fallback, char *out, size_t capacity)
{
    /* Synthetic rate maps are intentionally unavailable in the mono port. */
    (void)name;
    if (capacity) snprintf(out, capacity, "%s", fallback);
}
