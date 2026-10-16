/* Included after metal_post.inc: share its color-copy storage. The two
   intermediate targets clear every view so discarded non-edges and pixels
   outside the current split-screen view cannot reuse an older view's data. */
#include "metal_smaa_source.h"
/* zconf skips Byte when MacTypes already defines it, but Z_PREFIX names
   this copy z_Byte. Supply the prefixed byte type beside Foundation's. */
#if defined(__MACTYPES__)
typedef unsigned char z_Byte;
#endif
#include "../../../third_party/zlib/zlib_prefixed.h"

/* Assets are generated for the game ABI (unsigned long is 32-bit there). */
extern const unsigned int xgpu_smaa_shader[], xgpu_smaa_area_texture[], xgpu_smaa_search_texture[];
extern const uint32_t xgpu_smaa_shader_size, xgpu_smaa_area_texture_size, xgpu_smaa_search_texture_size;
static id<MTLRenderPipelineState> smaa_pipeline[3];
static id<MTLTexture> smaa_area, smaa_search, smaa_edges, smaa_weights;
static MTLPixelFormat smaa_format;

static id<MTLTexture> smaa_lookup(const unsigned int *bytes, uint32_t size,
    NSUInteger width, NSUInteger height, NSUInteger channels, MTLPixelFormat format)
{
    if (!size) return nil;
    uLongf length = width * height * channels;
    NSMutableData *decoded = [NSMutableData dataWithLength:length];
    if (uncompress(decoded.mutableBytes, &length, (const Bytef *)bytes, size) != Z_OK || length != decoded.length)
        return nil;
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
        width:width height:height mipmapped:NO];
    d.storageMode = MTLStorageModeShared;
    d.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> texture = [device newTextureWithDescriptor:d];
    [texture replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0
        withBytes:decoded.bytes bytesPerRow:width * channels];
    return texture;
}

static BOOL smaa_prepare_resources(MTLPixelFormat format)
{
    if (smaa_pipeline[2] && smaa_area && smaa_search && smaa_format == format) return YES;
    if (!xgpu_smaa_shader_size) return NO;
    /* embed_assets replaces non-ASCII comment bytes; Latin1 also accepts
       the unmodified bundled shader used by the windowless tests. */
    NSString *body = [[NSString alloc] initWithBytes:xgpu_smaa_shader length:xgpu_smaa_shader_size - 1
        encoding:NSISOLatin1StringEncoding];
    NSString *source = metal_smaa_source(body);
    if (!source) return NO;
    NSError *error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    if (!library) {
        platform_log("Metal SMAA: shader failed: %s", error.localizedDescription.UTF8String);
        return NO;
    }
    NSArray<NSString *> *names = @[@"smaa_edges", @"smaa_weights", @"smaa_blend"];
    for (NSUInteger stage = 0; stage < 3; stage++) {
        MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = [library newFunctionWithName:@"smaa_vertex"];
        d.fragmentFunction = [library newFunctionWithName:names[stage]];
        d.colorAttachments[0].pixelFormat = stage == 0 ? MTLPixelFormatRG8Unorm :
            stage == 1 ? MTLPixelFormatRGBA8Unorm : format;
        if (stage == 2)
            d.colorAttachments[0].writeMask = MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue;
        smaa_pipeline[stage] = [device newRenderPipelineStateWithDescriptor:d error:&error];
        if (!smaa_pipeline[stage]) {
            platform_log("Metal SMAA: pipeline failed: %s", error.localizedDescription.UTF8String);
            return NO;
        }
    }
    if (!smaa_area)
        smaa_area = smaa_lookup(xgpu_smaa_area_texture, xgpu_smaa_area_texture_size, 160, 560, 2, MTLPixelFormatRG8Unorm);
    if (!smaa_search)
        smaa_search = smaa_lookup(xgpu_smaa_search_texture, xgpu_smaa_search_texture_size, 64, 16, 1, MTLPixelFormatR8Unorm);
    if (!smaa_area || !smaa_search) {
        platform_log("Metal SMAA: cannot inflate lookup textures");
        return NO;
    }
    smaa_format = format;
    platform_log("Metal: SMAA HIGH initialized");
    return YES;
}

static BOOL smaa_prepare(MTLPixelFormat format)
{
    static BOOL failed;
    if (failed) return NO;
    if (smaa_prepare_resources(format)) return YES;
    failed = YES;
    platform_log("Metal SMAA: initialization failed; post pass disabled for this run");
    return NO;
}

static BOOL smaa_resize(id<MTLTexture> target)
{
    if (smaa_edges && smaa_weights && smaa_edges.width == target.width && smaa_edges.height == target.height)
        return YES;
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRG8Unorm
        width:target.width height:target.height mipmapped:NO];
    d.storageMode = MTLStorageModePrivate;
    d.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
    smaa_edges = [device newTextureWithDescriptor:d];
    d.pixelFormat = MTLPixelFormatRGBA8Unorm;
    smaa_weights = [device newTextureWithDescriptor:d];
    return smaa_edges && smaa_weights;
}

static BOOL smaa_draw(NSUInteger stage, id<MTLTexture> target, const int32_t corners[4],
    const float metrics[4], const float bounds[4])
{
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = stage == 0 ? smaa_edges : stage == 1 ? smaa_weights : target;
    pass.colorAttachments[0].loadAction = stage == 2 ? MTLLoadActionLoad : MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    id<MTLRenderCommandEncoder> draw = [commands renderCommandEncoderWithDescriptor:pass];
    if (!draw) return NO;
    draw.label = stage == 0 ? @"SMAA edges" : stage == 1 ? @"SMAA weights" : @"SMAA blend (RGB only)";
    [draw setRenderPipelineState:smaa_pipeline[stage]];
    [draw setViewport:(MTLViewport){0, 0, (double)target.width, (double)target.height, 0, 1}];
    [draw setScissorRect:(MTLScissorRect){corners[0], corners[1], corners[2] - corners[0], corners[3] - corners[1]}];
    [draw setFragmentBytes:metrics length:4 * sizeof(float) atIndex:0];
    [draw setFragmentBytes:bounds length:4 * sizeof(float) atIndex:1];
    [draw setFragmentTexture:stage == 1 ? smaa_edges : post_color atIndex:0];
    if (stage == 1) {
        [draw setFragmentTexture:smaa_area atIndex:1];
        [draw setFragmentTexture:smaa_search atIndex:2];
    } else if (stage == 2) {
        [draw setFragmentTexture:smaa_weights atIndex:1];
    }
    [draw drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [draw endEncoding];
    return YES;
}

void macos_metal_smaa(gpu_texture handle, const int32_t corners[4])
{
    @autoreleasepool {
        MetalTexture *record = texture_record(handle);
        id<MTLTexture> target = record ? record->texture : nil;
        if (!target || target.sampleCount != 1 || corners[0] < 0 || corners[1] < 0 ||
            corners[2] <= corners[0] || corners[3] <= corners[1] ||
            (NSUInteger)corners[2] > target.width || (NSUInteger)corners[3] > target.height)
            return;
        if (!smaa_prepare(target.pixelFormat) || !smaa_resize(target)) return;
        command_buffer();
        pass_end(); /* Keep depth for the HUD, as FXAA does. */
        if (!post_copy_color(target)) return;
        float metrics[4] = {1.0f / target.width, 1.0f / target.height, target.width, target.height};
        float bounds[4] = {(corners[0] + 0.5f) * metrics[0], (corners[1] + 0.5f) * metrics[1],
            (corners[2] - 0.5f) * metrics[0], (corners[3] - 0.5f) * metrics[1]};
        for (NSUInteger stage = 0; stage < 3; stage++)
            if (!smaa_draw(stage, target, corners, metrics, bounds)) return;
        use_texture(record);
    }
}
