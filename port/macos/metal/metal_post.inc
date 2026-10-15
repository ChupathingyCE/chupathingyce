/* Native postprocessing, included after gpu_metal.m's pass/resource helpers.
   End the current render pass without discarding depth: the HUD still uses
   it. Copy color before sampling, and write only RGB so destination alpha
   (the game's scratch data) survives. One scratch texture, resized on demand;
   command buffers retain old storage while it is in flight. */
#include "metal_fxaa_source.h"

static id<MTLRenderPipelineState> post_fxaa_pipeline;
static id<MTLSamplerState> post_sampler;
static id<MTLTexture> post_color;
static MTLPixelFormat post_format;

static BOOL post_fxaa_prepare(MTLPixelFormat format)
{
    if (post_fxaa_pipeline && post_format == format)
        return YES;
    NSError *error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:
        [NSString stringWithUTF8String:metal_fxaa_source] options:nil error:&error];
    if (!library) {
        platform_log("Metal FXAA: cannot compile shader: %s", error.localizedDescription.UTF8String);
        return NO;
    }
    MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction = [library newFunctionWithName:@"fxaa_vertex"];
    descriptor.fragmentFunction = [library newFunctionWithName:@"fxaa_fragment"];
    descriptor.colorAttachments[0].pixelFormat = format;
    descriptor.colorAttachments[0].writeMask = MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue;
    post_fxaa_pipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    if (!post_fxaa_pipeline) {
        platform_log("Metal FXAA: cannot create pipeline: %s", error.localizedDescription.UTF8String);
        return NO;
    }
    if (!post_sampler) {
        MTLSamplerDescriptor *sampler = [MTLSamplerDescriptor new];
        sampler.minFilter = sampler.magFilter = MTLSamplerMinMagFilterLinear;
        sampler.sAddressMode = sampler.tAddressMode = MTLSamplerAddressModeClampToEdge;
        post_sampler = [device newSamplerStateWithDescriptor:sampler];
    }
    if (!post_sampler)
        return NO;
    post_format = format;
    platform_log("Metal: FXAA initialized");
    return YES;
}

static BOOL post_copy_color(id<MTLTexture> target)
{
    if (!post_color || post_color.width != target.width || post_color.height != target.height ||
        post_color.pixelFormat != target.pixelFormat) {
        MTLTextureDescriptor *description = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
            target.pixelFormat width:target.width height:target.height mipmapped:NO];
        description.usage = MTLTextureUsageShaderRead;
        description.storageMode = MTLStorageModePrivate;
        post_color = [device newTextureWithDescriptor:description];
    }
    if (!post_color)
        return NO;
    id<MTLBlitCommandEncoder> copy = [commands blitCommandEncoder];
    if (!copy)
        return NO;
    [copy copyFromTexture:target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
        sourceSize:MTLSizeMake(target.width, target.height, 1) toTexture:post_color destinationSlice:0
        destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
    [copy endEncoding];
    return YES;
}

/* corners are target pixel coordinates, clipped by the frontend to one
   player's view (not the centered menu coordinate system). */
void macos_metal_fxaa(gpu_texture handle, const int32_t corners[4])
{
    @autoreleasepool {
        MetalTexture *record = texture_record(handle);
        id<MTLTexture> target = record ? record->texture : nil;
        if (!target || target.sampleCount != 1 || corners[0] < 0 || corners[1] < 0 ||
            corners[2] <= corners[0] || corners[3] <= corners[1] ||
            (NSUInteger)corners[2] > target.width || (NSUInteger)corners[3] > target.height)
            return;
        if (!post_fxaa_prepare(target.pixelFormat))
            return;
        command_buffer();
        pass_end();
        if (!post_copy_color(target))
            return;
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target;
        pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> draw = [commands renderCommandEncoderWithDescriptor:pass];
        if (!draw)
            return;
        draw.label = @"FXAA (RGB only)";
        float metrics[4] = {1.0f / target.width, 1.0f / target.height, target.width, target.height};
        float bounds[4] = {(corners[0] + 0.5f) * metrics[0], (corners[1] + 0.5f) * metrics[1],
            (corners[2] - 0.5f) * metrics[0], (corners[3] - 0.5f) * metrics[1]};
        [draw setRenderPipelineState:post_fxaa_pipeline];
        [draw setViewport:(MTLViewport){0, 0, (double)target.width, (double)target.height, 0, 1}];
        [draw setScissorRect:(MTLScissorRect){corners[0], corners[1],
            corners[2] - corners[0], corners[3] - corners[1]}];
        [draw setFragmentTexture:post_color atIndex:0];
        [draw setFragmentSamplerState:post_sampler atIndex:0];
        [draw setFragmentBytes:metrics length:sizeof(metrics) atIndex:0];
        [draw setFragmentBytes:bounds length:sizeof(bounds) atIndex:1];
        [draw drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [draw endEncoding];
        use_texture(record);
    }
}
