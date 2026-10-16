"""Render the production overlay's picture/icon shader paths offscreen."""
import platform
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from metal_paths import include_flags

DRIVER = r'''
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "metal_overlay.h"
enum { FRAMES = 3 };
static id<MTLDevice> device;
static id<MTLCommandQueue> queue;
static id<MTLCommandBuffer> commands;
static unsigned frame_slot;
@interface MetalTexture : NSObject { @public id<MTLTexture> texture; }
@property(nonatomic, strong) id<MTLTexture> texture;
@end
@implementation MetalTexture
@synthesize texture = texture;
@end
static MetalTexture *target;
static struct metal_overlay_frame fixture;
static MetalTexture *texture_record(uint32_t name) { return name == 1 ? target : nil; }
static void command_buffer(void) { commands = [queue commandBuffer]; assert(commands); }
static void pass_finish(BOOL unused) { (void)unused; }
static void use_texture(MetalTexture *unused) { (void)unused; }
void platform_log(const char *format, ...) { (void)format; }
static void host_fatal(const char *format, ...) __attribute__((noreturn));
static void host_fatal(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    abort();
}
void metal_overlay_prepare(int width, int height, struct metal_overlay_frame *frame)
{
    assert(width == 8 && height == 8);
    *frame = fixture;
}
#include "metal_overlay.inc"
static unsigned char atlas[METAL_OVERLAY_ATLAS_SIZE * METAL_OVERLAY_ATLAS_SIZE];
static struct metal_overlay_vertex vertices[6];
static void render_and_check(unsigned red, unsigned green, unsigned blue)
{
    uint8_t black[8 * 8 * 4] = {0};
    for (unsigned i = 0; i < 64; i++) black[i * 4 + 3] = 255;
    [target.texture replaceRegion:MTLRegionMake2D(0, 0, 8, 8) mipmapLevel:0
        withBytes:black bytesPerRow:32];
    macos_metal_overlay_draw(1);
    [commands commit];
    [commands waitUntilCompleted];
    assert(commands.status == MTLCommandBufferStatusCompleted);
    uint8_t pixels[8 * 8 * 4];
    [target.texture getBytes:pixels bytesPerRow:32 fromRegion:MTLRegionMake2D(0, 0, 8, 8) mipmapLevel:0];
    unsigned offset = (4 * 8 + 4) * 4;
    assert(abs((int)pixels[offset] - (int)blue) <= 1);
    assert(abs((int)pixels[offset + 1] - (int)green) <= 1);
    assert(abs((int)pixels[offset + 2] - (int)red) <= 1);
    assert(pixels[offset + 3] == 255);
}
int main(void)
{
    @autoreleasepool {
        device = MTLCreateSystemDefaultDevice();
        if (!device) return 77;
        queue = [device newCommandQueue]; assert(queue);
        target = [MetalTexture new];
        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
            MTLPixelFormatBGRA8Unorm width:8 height:8 mipmapped:NO];
        d.usage = MTLTextureUsageRenderTarget;
        d.storageMode = MTLStorageModeShared;
        target.texture = [device newTextureWithDescriptor:d]; assert(target.texture);
        const float points[6][2] = {{0,0}, {8,0}, {0,8}, {8,0}, {8,8}, {0,8}};
        for (unsigned i = 0; i < 6; i++) {
            vertices[i].x = points[i][0]; vertices[i].y = points[i][1];
            vertices[i].u = points[i][0] / 8; vertices[i].v = points[i][1] / 8;
            vertices[i].radius = -3;
            memset(vertices[i].color, 255, 4);
        }
        memset(atlas, 255, sizeof(atlas));
        const uint8_t icon[4] = {0,255,0,128}, red[4] = {255,0,0,255}, blue[4] = {0,0,255,255};
        fixture.vertices = vertices; fixture.vertex_count = 6; fixture.atlas = atlas;
        fixture.atlas_dirty = 1; fixture.icon_size = 1;
        for (unsigned i = 0; i < METAL_OVERLAY_ICON_COUNT; i++) fixture.icons[i] = icon;
        fixture.picture = red; fixture.picture_width = fixture.picture_height = 1; fixture.picture_serial = 1;
        render_and_check(255, 0, 0);
        fixture.picture = blue;
        render_and_check(255, 0, 0); /* The serial, not caller pointer changes, invalidates uploads. */
        fixture.picture_serial = 2;
        render_and_check(0, 0, 255);
        for (unsigned i = 0; i < 6; i++) vertices[i].radius = -2;
        render_and_check(0, 128, 0); /* Premultiplied icons must not multiply alpha twice. */
    }
    return 0;
}
'''


class OverlayImageTests(unittest.TestCase):
    @unittest.skipUnless(platform.system() == "Darwin" and shutil.which("clang"), "Metal requires macOS")
    def test_picture_serials_and_icon_alpha(self):
        with tempfile.TemporaryDirectory(prefix="metal-overlay-images-") as folder:
            root = Path(folder)
            source, binary = root / "images.m", root / "images"
            source.write_text(DRIVER)
            subprocess.run(["clang", "-std=gnu11", "-fobjc-arc", "-Wall", "-Wextra", "-Werror",
                            *include_flags(), str(source), "-framework", "Metal", "-framework", "Foundation",
                            "-o", str(binary)], check=True)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=120)
            if run.returncode == 77:
                self.skipTest("No Metal device")
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
