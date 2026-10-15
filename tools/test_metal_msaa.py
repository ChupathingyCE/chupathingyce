"""Windowless MSAA color/depth/stencil resolve and sample-preservation tests.
Uses the same attachment helpers as the game, with Metal API Validation."""
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
#include <stdio.h>
#include "metal_msaa.h"
static id<MTLDevice> device;
static id<MTLCommandQueue> queue;
static id<MTLLibrary> library;
static void run(NSUInteger requested) {
    NSUInteger count = metal_msaa_count(device, requested);
    assert(count >= 1 && count <= requested);
    if (count == 1) return;
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
        width:64 height:64 mipmapped:NO];
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    d.storageMode = MTLStorageModeShared;
    id<MTLTexture> color = [device newTextureWithDescriptor:d];
    id<MTLTexture> color_ms = metal_msaa_storage(device, color, count);
    d.pixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    d.storageMode = MTLStorageModePrivate;
    id<MTLTexture> depth = [device newTextureWithDescriptor:d];
    id<MTLTexture> depth_ms = metal_msaa_storage(device, depth, count);
    assert(color && color_ms && depth && depth_ms);
    MTLRenderPipelineDescriptor *p = [MTLRenderPipelineDescriptor new];
    p.vertexFunction = [library newFunctionWithName:@"v"];
    p.fragmentFunction = [library newFunctionWithName:@"f"];
    p.rasterSampleCount = count;
    p.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    p.depthAttachmentPixelFormat = p.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    NSError *error = nil;
    id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:p error:&error];
    if (!pipeline) fprintf(stderr, "%s\n", error.description.UTF8String);
    assert(pipeline);
    MTLDepthStencilDescriptor *ds = [MTLDepthStencilDescriptor new];
    ds.depthCompareFunction = MTLCompareFunctionLess;
    ds.depthWriteEnabled = YES;
    id<MTLDepthStencilState> state = [device newDepthStencilStateWithDescriptor:ds];
    unsigned char before[64*64*4], after[64*64*4];
    for (int step = 0; step < 2; step++) {
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        metal_msaa_color(pass.colorAttachments[0], color, color_ms);
        metal_msaa_depth(pass, depth, depth_ms);
        pass.colorAttachments[0].loadAction = step ? MTLLoadActionLoad : MTLLoadActionClear;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
        pass.depthAttachment.loadAction = pass.stencilAttachment.loadAction =
            step ? MTLLoadActionLoad : MTLLoadActionClear;
        pass.depthAttachment.clearDepth = 1;
        pass.stencilAttachment.clearStencil = 23;
        id<MTLRenderCommandEncoder> draw = [commands renderCommandEncoderWithDescriptor:pass];
        assert(draw);
        if (!step) {
            [draw setRenderPipelineState:pipeline];
            [draw setDepthStencilState:state];
            [draw drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        }
        [draw endEncoding];
        [commands commit]; [commands waitUntilCompleted];
        if (commands.error) fprintf(stderr, "%s\n", commands.error.description.UTF8String);
        assert(commands.status == MTLCommandBufferStatusCompleted);
        [color getBytes:step ? after : before bytesPerRow:64*4
            fromRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0];
    }
    assert(!memcmp(before, after, sizeof(before))); /* all samples survive a pass boundary */
    int partial = 0, white = 0;
    for (int i = 0; i < 64*64; i++) {
        if (after[4*i] > 0 && after[4*i] < 255) partial++;
        if (after[4*i] == 255) white++;
    }
    assert(partial > 0 && white > 0);
    printf("%lu requested, %lu samples: resolved edge coverage and retained samples OK\n", requested, count);
}
int main(void) { @autoreleasepool {
    device = MTLCreateSystemDefaultDevice(); if (!device) return 77;
    queue = [device newCommandQueue];
    NSError *error = nil;
    library = [device newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "vertex float4 v(uint id [[vertex_id]]) { const float2 p[3] = {float2(-0.9,-0.8),"
        "float2(0.8,-0.7),float2(-0.7,0.9)}; return float4(p[id],0.5,1); }\n"
        "fragment float4 f() { return float4(1); }" options:nil error:&error];
    assert(library);
    assert(metal_msaa_count(device, 0) == 1);
    assert(metal_msaa_count(device, 3) == 1);
    run(2); run(4); run(8);
    return 0;
}}
'''


class MetalMSAATests(TestCase):
    @skipUnless(platform.system() == "Darwin", "Metal requires macOS")
    def test_color_depth_stencil_resolve(self):
        with tempfile.TemporaryDirectory(prefix="metal-msaa-") as folder:
            source, binary = Path(folder) / "msaa.m", Path(folder) / "msaa"
            source.write_text(DRIVER)
            subprocess.run(["clang", "-fobjc-arc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "port/macos/metal"), str(source), "-framework", "Metal",
                            "-framework", "Foundation", "-o", str(binary)], check=True, timeout=120)
            result = subprocess.run([str(binary)], env={**os.environ, "MTL_DEBUG_LAYER": "1"},
                                    capture_output=True, text=True, timeout=120)
            if result.returncode == 77:
                self.skipTest("No Metal device")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertNotIn("validation error", result.stderr.lower())

    def test_pipeline_and_warm_keys_include_samples(self):
        source = (ROOT / "port/macos/metal/gpu_metal.m").read_text()
        self.assertIn("descriptor.rasterSampleCount = key->sample_count;", source)
        self.assertIn("descriptor.rasterSampleCount = pass_samples;", source)
        self.assertIn("description->sample_count = (uint8_t)key->sample_count;", source)
        self.assertIn("metal_msaa_count(device, description->sample_count)", source)
        frontend = (ROOT / "port/macos/metal/renderer/d3d8_device.c").read_text()
        self.assertIn("entry->target.sample_count != samples", frontend)
        self.assertIn("built.alpha_destination, built.sample_count", frontend)
        self.assertIn("description.sample_count = fields == 15", frontend)

    def test_present_resolves_multisampled_depth(self):
        source = (ROOT / "port/macos/metal/gpu_metal.m").read_text()
        begin = source.index("static void pass_finish(BOOL frame_end)")
        end = source.index("static void pass_end(void)", begin)
        finish = source[begin:end]
        self.assertIn("pass_samples > 1 ? MTLStoreActionStoreAndMultisampleResolve", finish)
        self.assertIn("[encoder setDepthStoreAction:store]", finish)
        self.assertIn("[encoder setStencilStoreAction:store]", finish)
