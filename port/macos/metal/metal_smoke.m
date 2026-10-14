/* Opt-in, offscreen Metal bring-up test. No game data or window required. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>

/* Build selection is enforced by generate_macos_metal_build; this executable
   is never linked into halo, even when the development flag is enabled. */

static id<MTLRenderPipelineState> make_pipeline(id<MTLDevice> device)
{
    NSString *source = @"#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "vertex float4 smoke_vertex(uint i [[vertex_id]]) {\n"
        "  const float2 p[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)};\n"
        "  return float4(p[i], 0, 1);\n"
        "}\n"
        "fragment float4 smoke_fragment() { return float4(0.25,0.5,0.75,1); }\n";
    NSError *error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    if (!library) {
        fprintf(stderr, "Metal shader compilation failed: %s\n", error.description.UTF8String);
        return nil;
    }
    MTLRenderPipelineDescriptor *description = [MTLRenderPipelineDescriptor new];
    description.vertexFunction = [library newFunctionWithName:@"smoke_vertex"];
    description.fragmentFunction = [library newFunctionWithName:@"smoke_fragment"];
    description.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:description error:&error];
    if (!pipeline)
        fprintf(stderr, "Metal pipeline creation failed: %s\n", error.description.UTF8String);
    return pipeline;
}

static BOOL render_and_check(id<MTLDevice> device, id<MTLRenderPipelineState> pipeline)
{
    /* Fixed allocations and exactly one triangle: no caller-controlled sizes. */
    const NSUInteger width = 16, height = 16, row_bytes = 64;
    MTLTextureDescriptor *description = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
        width:width height:height mipmapped:NO];
    description.storageMode = MTLStorageModePrivate;
    description.usage = MTLTextureUsageRenderTarget;
    id<MTLTexture> texture = [device newTextureWithDescriptor:description];
    id<MTLBuffer> readback = [device newBufferWithLength:row_bytes * height
        options:MTLResourceStorageModeShared];
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLCommandBuffer> command = [queue commandBuffer];
    if (!texture || !readback || !command) {
        fprintf(stderr, "Metal resource allocation failed\n");
        return NO;
    }
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> render = [command renderCommandEncoderWithDescriptor:pass];
    if (!render) return NO;
    [render setRenderPipelineState:pipeline];
    [render drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [render endEncoding];
    id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
    if (!blit) return NO;
    [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(width,height,1) toBuffer:readback destinationOffset:0
        destinationBytesPerRow:row_bytes destinationBytesPerImage:row_bytes * height];
    [blit endEncoding];
    dispatch_semaphore_t complete = dispatch_semaphore_create(0);
    [command addCompletedHandler:^(id<MTLCommandBuffer> finished) {
        (void)finished;
        dispatch_semaphore_signal(complete);
    }];
    [command commit];
    if (dispatch_semaphore_wait(complete, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC))) {
        fprintf(stderr, "Metal smoke test timed out\n");
        return NO;
    }
    if (command.status != MTLCommandBufferStatusCompleted) {
        fprintf(stderr, "Metal command failed: %s\n", command.error.description.UTF8String);
        return NO;
    }
    const unsigned char *bytes = readback.contents;
    if (!bytes) {
        fprintf(stderr, "Metal readback buffer is not CPU-accessible\n");
        return NO;
    }
    const unsigned char *pixel = bytes + 8 * row_bytes + 8 * 4;
    if (pixel[0] < 190 || pixel[0] > 192 || pixel[1] < 127 ||
        pixel[1] > 129 || pixel[2] < 63 || pixel[2] > 65 || pixel[3] != 255) {
        fprintf(stderr, "Metal readback did not match the rendered color\n");
        return NO;
    }
    return YES;
}

int main(void)
{
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            fprintf(stderr, "No Metal device available\n");
            return 1;
        }
        id<MTLRenderPipelineState> pipeline = make_pipeline(device);
        if (!pipeline || !render_and_check(device, pipeline)) return 1;
        printf("Metal smoke test passed on %s (shader, triangle, GPU readback)\n", device.name.UTF8String);
        return 0;
    }
}
