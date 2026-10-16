/* Native overlay pass. Included after gpu_metal.m's resource/pass helpers.
   The CPU queue is drained once per Present. Buffers have three fixed slots,
   protected by frame_begin; an updated atlas gets new storage so in-flight
   draws never observe glyphs from a later frame. No OpenGL state is touched. */
#include "metal_overlay.h"

static id<MTLRenderPipelineState> overlay_pipeline;
static id<MTLSamplerState> overlay_sampler;
static id<MTLTexture> overlay_atlas, overlay_icons, overlay_picture;
static uint32_t overlay_picture_serial;
static id<MTLBuffer> overlay_vertices[FRAMES];
static MTLPixelFormat overlay_format;

static NSString *const overlay_source =
    @"#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct OverlayVertex { packed_float2 p, uv; packed_float4 shape; packed_float2 edge; uchar4 color; };\n"
    "struct OverlayOut { float4 p [[position]]; float2 pixel; float2 uv; float4 shape; float2 edge; float4 color; };\n"
    "vertex OverlayOut overlay_vertex(uint id [[vertex_id]], const device OverlayVertex *v [[buffer(0)]],\n"
    "    constant float2 &size [[buffer(1)]]) {\n"
    "    OverlayOut o; o.p = float4(float2(v[id].p) / size * float2(2,-2) + float2(-1,1), 0, 1);\n"
    "    o.pixel = float2(v[id].p); o.uv = v[id].uv; o.shape = v[id].shape; o.edge = v[id].edge;\n"
    "    o.color = float4(v[id].color) / 255.0; return o;\n"
    "}\n"
    "fragment float4 overlay_fragment(OverlayOut i [[stage_in]], texture2d<float> atlas [[texture(0)]],\n"
    "    texture2d_array<float> icons [[texture(1)]], texture2d<float> picture [[texture(2)]],\n"
    "    sampler linear [[sampler(0)]], constant float4 *cutouts [[buffer(0)]]) {\n"
    "    for (uint n=0; n<4; n++)\n"
    "        if (all(i.pixel >= cutouts[n].xy) && all(i.pixel < cutouts[n].zw)) discard_fragment();\n"
    "    if (i.edge.x == -2.0) {\n"
    "        float4 c = icons.sample(linear, i.uv, uint(i.shape.x));\n"
    "        return float4((c.a > 0.0 ? c.rgb / c.a : float3(0)) * i.color.rgb, c.a * i.color.a);\n"
    "    }\n"
    "    if (i.edge.x == -3.0) return picture.sample(linear, i.uv) * i.color;\n"
    "    float alpha;\n"
    "    if (i.uv.x >= 0) alpha = atlas.sample(linear, i.uv).r;\n"
    "    else {\n"
    "        float2 q = abs(i.shape.xy) - i.shape.zw + i.edge.x;\n"
    "        float d = length(max(q, float2(0))) + min(max(q.x,q.y),0.0) - i.edge.x;\n"
    "        alpha = clamp(0.5-d,0.0,1.0);\n"
    "        if (i.edge.y > 0) alpha *= clamp(d+i.edge.y+0.5,0.0,1.0);\n"
    "    }\n"
    "    return float4(i.color.rgb, i.color.a * alpha);\n"
    "}\n";

static void overlay_set_up(MTLPixelFormat format)
{
    if (overlay_pipeline && overlay_format == format)
        return;
    NSError *error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:overlay_source options:nil error:&error];
    if (!library)
        host_fatal("Metal overlay: shader compilation failed: %s", error.localizedDescription.UTF8String);
    MTLRenderPipelineDescriptor *pipeline = [MTLRenderPipelineDescriptor new];
    pipeline.label = @"menu overlay";
    pipeline.vertexFunction = [library newFunctionWithName:@"overlay_vertex"];
    pipeline.fragmentFunction = [library newFunctionWithName:@"overlay_fragment"];
    MTLRenderPipelineColorAttachmentDescriptor *color = pipeline.colorAttachments[0];
    color.pixelFormat = format;
    color.blendingEnabled = YES;
    color.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    color.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    color.sourceAlphaBlendFactor = MTLBlendFactorOne;
    color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    overlay_pipeline = [device newRenderPipelineStateWithDescriptor:pipeline error:&error];
    if (!overlay_pipeline)
        host_fatal("Metal overlay: pipeline creation failed: %s", error.localizedDescription.UTF8String);
    overlay_format = format;
    if (!overlay_sampler) {
        MTLSamplerDescriptor *sampler = [MTLSamplerDescriptor new];
        sampler.minFilter = sampler.magFilter = MTLSamplerMinMagFilterLinear;
        sampler.mipFilter = MTLSamplerMipFilterLinear;
        sampler.sAddressMode = sampler.tAddressMode = MTLSamplerAddressModeClampToEdge;
        overlay_sampler = [device newSamplerStateWithDescriptor:sampler];
        if (!overlay_sampler) host_fatal("Metal overlay: sampler allocation failed");
    }
    platform_log("Metal: native menu overlay initialized");
}

/* Immutable images per update: Metal retains old resources for in-flight draws.
   Icon mip levels are premultiplied to avoid dark fringes at small prompt sizes. */
static void overlay_icons_upload(const struct metal_overlay_frame *frame)
{
    if (overlay_icons) return;
    uint32_t size = frame->icon_size;
    if (!size || size > METAL_OVERLAY_ICON_SIZE_MAX)
        host_fatal("Metal overlay: invalid icon size");
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
        MTLPixelFormatRGBA8Unorm width:size height:size mipmapped:YES];
    d.textureType = MTLTextureType2DArray;
    d.arrayLength = METAL_OVERLAY_ICON_COUNT;
    d.usage = MTLTextureUsageShaderRead;
    d.storageMode = device.hasUnifiedMemory ? MTLStorageModeShared : MTLStorageModeManaged;
    overlay_icons = [device newTextureWithDescriptor:d];
    NSMutableData *storage = [NSMutableData dataWithLength:(size_t)size * size * 4];
    if (!overlay_icons || !storage) host_fatal("Metal overlay: icon allocation failed");
    uint8_t *pixels = storage.mutableBytes;
    for (uint32_t icon = 0; icon < METAL_OVERLAY_ICON_COUNT; icon++) {
        if (!frame->icons[icon]) host_fatal("Metal overlay: missing icon pixels");
        const uint8_t *source = frame->icons[icon];
        for (uint32_t texel = 0; texel < size * size; texel++) {
            uint32_t alpha = source[texel * 4 + 3];
            for (uint32_t channel = 0; channel < 3; channel++)
                pixels[texel * 4 + channel] = (uint8_t)(source[texel * 4 + channel] * alpha / 255);
            pixels[texel * 4 + 3] = (uint8_t)alpha;
        }
        [overlay_icons replaceRegion:MTLRegionMake2D(0, 0, size, size) mipmapLevel:0 slice:icon
            withBytes:pixels bytesPerRow:size * 4 bytesPerImage:(size_t)size * size * 4];
    }
    id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
    if (!blit) host_fatal("Metal overlay: cannot create icon mip encoder");
    [blit generateMipmapsForTexture:overlay_icons];
    [blit endEncoding];
}

static void overlay_picture_upload(const struct metal_overlay_frame *frame)
{
    if (overlay_picture && overlay_picture_serial == frame->picture_serial) return;
    BOOL present = frame->picture && frame->picture_width && frame->picture_height;
    uint32_t width = present ? frame->picture_width : 1;
    uint32_t height = present ? frame->picture_height : 1;
    if (width > METAL_OVERLAY_PICTURE_SIZE_MAX || height > METAL_OVERLAY_PICTURE_SIZE_MAX)
        host_fatal("Metal overlay: invalid picture size");
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
        MTLPixelFormatRGBA8Unorm width:width height:height mipmapped:NO];
    d.usage = MTLTextureUsageShaderRead;
    d.storageMode = device.hasUnifiedMemory ? MTLStorageModeShared : MTLStorageModeManaged;
    id<MTLTexture> picture = [device newTextureWithDescriptor:d];
    if (!picture) host_fatal("Metal overlay: picture allocation failed");
    uint32_t transparent = 0;
    [picture replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0
        withBytes:present ? frame->picture : (const uint8_t *)&transparent bytesPerRow:width * 4];
    overlay_picture = picture;
    overlay_picture_serial = frame->picture_serial;
}

static void overlay_upload(const struct metal_overlay_frame *frame)
{
    if (!overlay_vertices[frame_slot]) {
        overlay_vertices[frame_slot] = [device newBufferWithLength:
            METAL_OVERLAY_MAX_VERTICES * sizeof(struct metal_overlay_vertex)
            options:MTLResourceStorageModeShared];
        if (!overlay_vertices[frame_slot]) host_fatal("Metal overlay: vertex allocation failed");
    }
    memcpy(overlay_vertices[frame_slot].contents, frame->vertices,
        frame->vertex_count * sizeof(struct metal_overlay_vertex));
    if (!overlay_atlas || frame->atlas_dirty) {
        MTLTextureDescriptor *description = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
            width:METAL_OVERLAY_ATLAS_SIZE height:METAL_OVERLAY_ATLAS_SIZE mipmapped:NO];
        description.usage = MTLTextureUsageShaderRead;
        description.storageMode = device.hasUnifiedMemory ? MTLStorageModeShared : MTLStorageModeManaged;
        id<MTLTexture> atlas = [device newTextureWithDescriptor:description];
        if (!atlas) host_fatal("Metal overlay: atlas allocation failed");
        [atlas replaceRegion:MTLRegionMake2D(0, 0, METAL_OVERLAY_ATLAS_SIZE, METAL_OVERLAY_ATLAS_SIZE)
            mipmapLevel:0 withBytes:frame->atlas bytesPerRow:METAL_OVERLAY_ATLAS_SIZE];
        overlay_atlas = atlas;
    }
}

void macos_metal_overlay_draw(uint32_t target)
{
    @autoreleasepool {
        MetalTexture *record = texture_record(target);
        struct metal_overlay_frame frame;
        id<MTLTexture> texture = record ? record->texture : nil;
        /* Invalid/minimized targets still drain the CPU queue. */
        metal_overlay_prepare(texture ? (int)texture.width : 0, texture ? (int)texture.height : 0, &frame);
        if (!frame.vertex_count) return;
        if (!texture || !frame.vertices || !frame.atlas || frame.vertex_count > METAL_OVERLAY_MAX_VERTICES)
            host_fatal("Metal overlay: invalid frame");
        command_buffer();
        pass_finish(YES);
        overlay_set_up(texture.pixelFormat);
        overlay_upload(&frame);
        overlay_icons_upload(&frame);
        overlay_picture_upload(&frame);
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> draw = [commands renderCommandEncoderWithDescriptor:pass];
        if (!draw) host_fatal("Metal overlay: cannot create encoder");
        draw.label = @"menu overlay";
        float size[2] = {(float)texture.width, (float)texture.height};
        [draw setRenderPipelineState:overlay_pipeline];
        [draw setViewport:(MTLViewport){0, 0, (double)texture.width, (double)texture.height, 0, 1}];
        [draw setVertexBuffer:overlay_vertices[frame_slot] offset:0 atIndex:0];
        [draw setVertexBytes:size length:sizeof(size) atIndex:1];
        [draw setFragmentBytes:frame.cutouts length:sizeof(frame.cutouts) atIndex:0];
        [draw setFragmentTexture:overlay_atlas atIndex:0];
        [draw setFragmentTexture:overlay_icons atIndex:1];
        [draw setFragmentTexture:overlay_picture atIndex:2];
        [draw setFragmentSamplerState:overlay_sampler atIndex:0];
        [draw drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:frame.vertex_count];
        [draw endEncoding];
        use_texture(record);
    }
}
