/* display.vsync on the Metal renderer's CAMetalLayer. With display sync on,
   a presented drawable waits for the display's vertical blank; off, it shows
   at once (and may tear). Shared by metal_host.m and the native test
   (tools/test_metal_vsync.py). */
#ifndef HALO_METAL_VSYNC_H
#define HALO_METAL_VSYNC_H
#import <QuartzCore/QuartzCore.h>

/* sets the layer's display sync; returns whether it changed */
static inline BOOL metal_layer_set_vsync(CAMetalLayer *layer, int vsync)
{
    BOOL wanted = vsync ? YES : NO;

    if (!layer || layer.displaySyncEnabled == wanted)
        return NO;
    /* (no implicit animation: the game's thread has no run loop to end one) */
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    layer.displaySyncEnabled = wanted;
    [CATransaction commit];
    return YES;
}
#endif
