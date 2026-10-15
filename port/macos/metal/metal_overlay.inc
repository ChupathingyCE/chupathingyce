/* Native overlay pass. Included after gpu_metal.m's resource/pass helpers.
   The CPU queue is drained once per Present. Buffers have three fixed slots,
   protected by frame_begin; an updated atlas gets new storage so in-flight
   draws never observe glyphs from a later frame. No OpenGL state is touched. */
#include "renderer/metal_overlay.h"

static id<MTLRenderPipelineState> overlay_pipeline;
static id<MTLSamplerState> overlay_sampler;
static id<MTLTexture> overlay_atlas;
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
    "    sampler linear [[sampler(0)]], constant float4 *cutouts [[buffer(0)]]) {\n"
    "    for (uint n=0; n<4; n++)\n"
    "        if (all(i.pixel >= cutouts[n].xy) && all(i.pixel < cutouts[n].zw)) discard_fragment();\n"
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
        sampler.sAddressMode = sampler.tAddressMode = MTLSamplerAddressModeClampToEdge;
        overlay_sampler = [device newSamplerStateWithDescriptor:sampler];
        if (!overlay_sampler) host_fatal("Metal overlay: sampler allocation failed");
    }
    platform_log("Metal: native menu overlay initialized");
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
        [draw setFragmentSamplerState:overlay_sampler atIndex:0];
        [draw drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:frame.vertex_count];
        [draw endEncoding];
        use_texture(record);
    }
}
