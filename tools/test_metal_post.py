"""Run the production Metal FXAA pass offscreen: no SDL window, focus or
mouse capture. Check RGB filtering, scratch alpha, and split-view clipping."""
import os
import platform
import subprocess
import tempfile
from pathlib import Path
from unittest import TestCase, skipUnless

ROOT = Path(__file__).resolve().parents[1]

DRIVER = r'''
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
typedef uint32_t gpu_texture;
@interface MetalTexture : NSObject { @public id<MTLTexture> texture; }
@end
@implementation MetalTexture
@end
static id<MTLDevice> device;
static id<MTLCommandBuffer> commands;
static id<MTLCommandQueue> queue;
static MetalTexture *record;
static MetalTexture *texture_record(gpu_texture h) { return h == 1 ? record : nil; }
static void command_buffer(void) { if (!commands) commands = [queue commandBuffer]; }
static void pass_end(void) { }
static void use_texture(MetalTexture *t) { (void)t; }
static void platform_log(const char *format, ...) {
    va_list ap; va_start(ap, format); vfprintf(stderr, format, ap); va_end(ap); fputc('\n', stderr);
}
#include "metal_post.inc"

static void run(int width, int height, int clipped, int uniform) {
    const size_t size = (size_t)width * height * 4;
    unsigned char *input = malloc(size), *output = malloc(size);
    assert(input && output);
    for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
        size_t p = ((size_t)y * width + x) * 4;
        unsigned char color = uniform ? 97 : (x * 2 > y * 3 ? 255 : 0);
        input[p] = input[p+1] = input[p+2] = color;
        input[p+3] = (unsigned char)(x * 13 + y * 7);
    }
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
        width:width height:height mipmapped:NO];
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    d.storageMode = MTLStorageModeShared;
    record = [MetalTexture new]; record->texture = [device newTextureWithDescriptor:d];
    assert(record->texture);
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [record->texture replaceRegion:region mipmapLevel:0 withBytes:input bytesPerRow:width*4];
    int32_t rect[4] = {clipped ? width / 4 : 0, clipped ? height / 4 : 0,
        clipped ? 3 * width / 4 : width, clipped ? 3 * height / 4 : height};
    macos_metal_fxaa(1, rect);
    assert(commands && post_fxaa_pipeline);
    [commands commit]; [commands waitUntilCompleted];
    assert(commands.status == MTLCommandBufferStatusCompleted);
    [record->texture getBytes:output bytesPerRow:width*4 fromRegion:region mipmapLevel:0];
    int blended = 0;
    for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
        size_t p = ((size_t)y * width + x) * 4;
        assert(input[p+3] == output[p+3]);
        int outside = x < rect[0] || x >= rect[2] || y < rect[1] || y >= rect[3];
        if (uniform || outside) assert(!memcmp(input+p, output+p, 4));
        else if (output[p] > 0 && output[p] < 255) blended++;
    }
    if (!uniform) assert(blended > 0);
    commands = nil;
    free(input); free(output);
}
int main(void) { @autoreleasepool {
    device = MTLCreateSystemDefaultDevice(); if (!device) return 77;
    queue = [device newCommandQueue]; assert(queue);
    run(64, 64, 0, 1);
    run(64, 64, 0, 0);
    run(96, 80, 1, 0); /* resize and per-view sampling bounds */
    run(96, 80, 1, 1);
    return 0;
}}
'''


class MetalPostTests(TestCase):
    @skipUnless(platform.system() == "Darwin", "Metal requires macOS")
    def test_production_fxaa_offscreen(self):
        with tempfile.TemporaryDirectory(prefix="metal-post-") as folder:
            source, binary = Path(folder) / "post.m", Path(folder) / "post"
            source.write_text(DRIVER)
            subprocess.run(["clang", "-fobjc-arc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "port/macos/metal"), str(source),
                            "-framework", "Metal", "-framework", "Foundation", "-o", str(binary)],
                           check=True, timeout=120)
            result = subprocess.run([str(binary)], env={**os.environ, "MTL_DEBUG_LAYER": "1"},
                                    capture_output=True, text=True, timeout=120)
            if result.returncode == 77:
                self.skipTest("No Metal device")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertNotIn("validation error", result.stderr.lower())

    def test_hook_precedes_hud(self):
        source = (ROOT / "port/macos/metal/renderer/d3d8_device.c").read_text()
        self.assertIn('strcmp(config_string("display.anti_aliasing"), "fxaa")', source)
        self.assertIn("macos_metal_fxaa(target->target.texture, corners);", source)
        compat = (ROOT / "port/macos/metal/renderer/metal_compat.c").read_text()
        self.assertNotIn("void halo_screen_anti_alias(", compat)
