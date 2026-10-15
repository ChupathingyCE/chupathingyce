/* Multisample attachments keep all samples between passes; their ordinary
   textures are resolved at each pass boundary for sampling/readback. These
   helpers are also exercised by the windowless GPU test. */
#ifndef HALO_METAL_MSAA_H
#define HALO_METAL_MSAA_H
#import <Metal/Metal.h>

static inline NSUInteger metal_msaa_count(id<MTLDevice> device, NSUInteger requested)
{
    NSUInteger count = requested == 8 || requested == 4 || requested == 2 ? requested : 1;
    while (count > 1 && ![device supportsTextureSampleCount:count])
        count /= 2;
    return count;
}

static inline id<MTLTexture> metal_msaa_storage(id<MTLDevice> device, id<MTLTexture> resolved, NSUInteger samples)
{
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:resolved.pixelFormat
        width:resolved.width height:resolved.height mipmapped:NO];
    d.textureType = MTLTextureType2DMultisample;
    d.sampleCount = samples;
    d.storageMode = MTLStorageModePrivate;
    d.usage = MTLTextureUsageRenderTarget;
    return [device newTextureWithDescriptor:d];
}

static inline void metal_msaa_color(MTLRenderPassColorAttachmentDescriptor *attachment,
    id<MTLTexture> resolved, id<MTLTexture> multisample)
{
    attachment.texture = multisample ?: resolved;
    attachment.resolveTexture = multisample ? resolved : nil;
    attachment.storeAction = multisample ? MTLStoreActionStoreAndMultisampleResolve : MTLStoreActionStore;
}

static inline void metal_msaa_depth(MTLRenderPassDescriptor *pass, id<MTLTexture> resolved,
    id<MTLTexture> multisample)
{
    pass.depthAttachment.texture = pass.stencilAttachment.texture = multisample ?: resolved;
    pass.depthAttachment.resolveTexture = pass.stencilAttachment.resolveTexture = multisample ? resolved : nil;
    pass.depthAttachment.storeAction = pass.stencilAttachment.storeAction = multisample ?
        MTLStoreActionStoreAndMultisampleResolve : MTLStoreActionStore;
    if (multisample) {
        pass.depthAttachment.depthResolveFilter = MTLMultisampleDepthResolveFilterSample0;
        pass.stencilAttachment.stencilResolveFilter = MTLMultisampleStencilResolveFilterSample0;
    }
}
#endif
