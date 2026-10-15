/* Offscreen fixture for the actual backend, not a second draw implementation.
No game data/window/present path. Timing excludes compilation, clear, commit,
readback and GPU waits. Counters are collected in a separate untimed batch. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../port/macos/metal/gpu_metal.m"

static CAMetalLayer *probe_layer;

void *host_sdl_metal_layer(void) { return (__bridge void *)probe_layer; }
void host_config_string(const char *name, const char *fallback, char *out, size_t capacity)
{
    (void)name;
    snprintf(out, capacity, "%s", fallback);
}
void host_fatal(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    exit(1);
}
void platform_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    va_end(args);
}
void metal_overlay_prepare(int width, int height, struct metal_overlay_frame *frame)
{
    (void)width; (void)height;
    memset(frame, 0, sizeof(*frame));
}

static const float vertices[12] = { -1,-1, 3,-1, -1,3, -1,-1, 3,-1, -1,3 };
static struct gpu_constant_store constants;
static struct gpu_uniforms uniforms;

enum { PROBE_DRAWS = 2048, PROBE_RUNS = 7, PROBE_WIDTH = 16 };

static struct gpu_draw make_draw(void)
{
    const char *vertex_source = "#include <metal_stdlib>\nusing namespace metal;\n"
        "struct A { uint format, stream, offset, stride; };\n"
        "struct T { A entries[16]; float4 constants[16]; };\n"
        "struct O { float4 p [[position]]; float4 color; };\n"
        "vertex O vertex_main(uint v [[vertex_id]], constant T &t [[buffer(2)]],\n"
        " device const uchar *data [[buffer(10)]], constant float4 *c [[buffer(0)]]) {\n"
        " O o; device const float2 *p = (device const float2 *)(data+t.entries[0].offset+v*t.entries[0].stride);\n"
        " o.p=float4(*p,0.5,1); o.color=c[0]; return o; }\n";
    const char *pixel_source = "#include <metal_stdlib>\nusing namespace metal;\n"
        "struct O { float4 p [[position]]; float4 color; };\n"
        "fragment float4 fragment_main(O i [[stage_in]], texture2d<float> t [[texture(0)]],\n"
        " sampler s [[sampler(0)]]) { return i.color*t.sample(s,float2(0.5)); }\n";
    struct gpu_texture_description target = { .type = GPU_TEXTURE_2D, .format = GPU_FORMAT_BGRA8,
        .usage = GPU_USAGE_RENDER_TARGET, .width = PROBE_WIDTH, .height = PROBE_WIDTH, .depth = 1, .levels = 1 };
    struct gpu_texture_description texel = { .type = GPU_TEXTURE_2D, .format = GPU_FORMAT_BGRA8,
        .usage = GPU_USAGE_UPLOAD, .width = 1, .height = 1, .depth = 1, .levels = 1 };
    struct gpu_draw draw = {0};
    uint32_t white = 0xffffffff;

    draw.color_target = gpu_metal_texture_create(&target);
    draw.vertex_shader = gpu_metal_shader_create(GPU_SHADER_VERTEX, vertex_source);
    draw.pixel_shader = gpu_metal_shader_create(GPU_SHADER_PIXEL, pixel_source);
    draw.viewport = (struct gpu_viewport){0, 0, PROBE_WIDTH, PROBE_WIDTH, 0, 1};
    draw.scissor = (struct gpu_rect){0, 0, PROBE_WIDTH, PROBE_WIDTH};
    draw.blend.color_write_mask = 15;
    draw.primitive = GPU_PRIMITIVE_TRIANGLES;
    draw.count = 3;
    draw.streams[0].buffer = gpu_metal_buffer_create(sizeof(vertices));
    draw.streams[0].stride = 8;
    gpu_metal_buffer_write(draw.streams[0].buffer, 0, sizeof(vertices), vertices, 0);
    for (int i = 0; i < GPU_ATTRIBUTE_COUNT; i++)
        draw.attributes[i] = (struct gpu_vertex_attribute){GPU_ATTRIBUTE_FLOAT4, GPU_STREAM_CONSTANT, 0};
    draw.attributes[0] = (struct gpu_vertex_attribute){GPU_ATTRIBUTE_FLOAT2, 0, 0};
    draw.stages[0].texture = gpu_metal_texture_create(&texel);
    draw.stages[0].type = GPU_TEXTURE_2D;
    draw.stages[0].sampler.min_filter = draw.stages[0].sampler.mag_filter = GPU_FILTER_POINT;
    gpu_metal_texture_upload(draw.stages[0].texture, 0, 0, &white, sizeof(white));
    constants.c[0][0] = 0.25f; constants.c[0][1] = 0.5f;
    constants.c[0][2] = 0.75f; constants.c[0][3] = 1;
    constants.serial = uniforms.serial = 1;
    return draw;
}

/* Commit after the timer, then read all pixels through an explicitly checked
command buffer. Backend frame serials/slots advance exactly as at Present. */
static uint64_t finish_batch(gpu_texture color, int split)
{
    id<MTLBuffer> readback = [device newBufferWithLength:PROBE_WIDTH*PROBE_WIDTH*4
        options:MTLResourceStorageModeShared];
    assert(readback);
    pass_end();
    id<MTLBlitCommandEncoder> blit = [command_buffer() blitCommandEncoder];
    assert(blit);
    [blit copyFromTexture:texture_record(color)->texture sourceSlice:0 sourceLevel:0
        sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(PROBE_WIDTH,PROBE_WIDTH,1)
        toBuffer:readback destinationOffset:0 destinationBytesPerRow:PROBE_WIDTH*4
        destinationBytesPerImage:PROBE_WIDTH*PROBE_WIDTH*4];
    [blit endEncoding];
    commit(YES);
    [last_committed waitUntilCompleted];
    assert(last_committed.status == MTLCommandBufferStatusCompleted);
    frames++;
    const unsigned char *pixels = readback.contents;
    uint64_t digest = 14695981039346656037ull;
    for (int y = 0; y < PROBE_WIDTH; y++) for (int x = 0; x < PROBE_WIDTH; x++) {
        const unsigned char *p = pixels + (y*PROBE_WIDTH+x)*4;
        if (split) {
            assert(p[0] == (x < PROBE_WIDTH/2 ? 0 : 255));
            assert(p[1] == 0 && p[2] == (x < PROBE_WIDTH/2 ? 255 : 0) && p[3] == 255);
        } else {
            assert(p[0] >= 190 && p[0] <= 192 && p[1] >= 127 && p[1] <= 129);
            assert(p[2] >= 63 && p[2] <= 65 && p[3] == 255);
        }
        for (int c = 0; c < 4; c++) digest = (digest ^ p[c]) * 1099511628211ull;
    }
    return digest;
}

static double draw_batch(struct gpu_draw *draw, int mode, int counted, uint64_t *digest)
{
    struct gpu_clear clear = { .flags = GPU_CLEAR_COLOR, .color = 0xff000000 };
    metal_profile = (struct draw_profile){ .enabled = counted };
#ifdef HALO_MACOS_METAL_V2
    prepared_native_reset();
#endif
    assert(pass_begin(draw->color_target, 0, &clear));
    double started = CACurrentMediaTime();
    for (unsigned i = 0; i < PROBE_DRAWS; i++) {
        draw->streams[0].offset = mode == 2 ? (i & 1)*24 : 0;
        if (mode == 1) {
            constants.c[0][0] = (i & 1) ? 0.25f : 0.125f;
            constants.serial++;
        }
        assert(gpu_metal_draw(draw, &constants, &uniforms) == 1);
    }
    double ns = (CACurrentMediaTime() - started)*1e9/PROBE_DRAWS;
    *digest = finish_batch(draw->color_target, 0);
    return ns;
}

static void transition_test(struct gpu_draw draw)
{
    uint32_t red = 0xffff0000, blue = 0xff0000ff, white = 0xffffffff;
    struct gpu_clear clear = { .flags = GPU_CLEAR_COLOR, .color = 0xff000000 };
    metal_profile = (struct draw_profile){ .enabled = 1 };
    for (int c = 0; c < 4; c++) constants.c[0][c] = 1;
    constants.serial++;
    draw.streams[0].offset = 0;
    draw.viewport.width = draw.scissor.width = PROBE_WIDTH/2;
    gpu_metal_texture_upload(draw.stages[0].texture, 0, 0, &red, 4);
    assert(pass_begin(draw.color_target, 0, &clear));
    assert(gpu_metal_draw(&draw, &constants, &uniforms));
    /* Both storage writes happen before submission/completion. The left draw
    must retain the old texel/buffer, the right draw must see the new ones. */
    id<MTLBuffer> previous = buffer_record(draw.streams[0].buffer)->buffer;
    gpu_metal_buffer_write(draw.streams[0].buffer, 0, sizeof(vertices), vertices, 0);
    assert(buffer_record(draw.streams[0].buffer)->buffer != previous);
    gpu_metal_texture_upload(draw.stages[0].texture, 0, 0, &blue, 4);
    pass_end(); /* identical prepared state, new encoder must rebind */
    draw.viewport.x = draw.scissor.x = PROBE_WIDTH/2;
    draw.depth_stencil.stencil_reference = 7; /* dynamic, not a depth object key */
    assert(gpu_metal_draw(&draw, &constants, &uniforms));
    finish_batch(draw.color_target, 1);
    assert(metal_profile.attribute_table_binds == 2);
#ifdef HALO_MACOS_METAL_V2
    unsigned long expected = prepared_native_enabled ? 1 : 2;
#else
    unsigned long expected = 2;
#endif
    assert(metal_profile.pipeline_lookups == expected && metal_profile.depth_lookups == expected);
    gpu_metal_texture_upload(draw.stages[0].texture, 0, 0, &white, 4);
    constants.c[0][0] = 0.25f; constants.c[0][1] = 0.5f; constants.c[0][2] = 0.75f;
    constants.serial++;
}

static unsigned long expected_lookup(int changed)
{
#ifdef HALO_MACOS_METAL_V2
    return prepared_native_enabled ? (unsigned long)changed : 1;
#else
    (void)changed;
    return 1;
#endif
}

static void pipeline_transition_test(struct gpu_draw draw)
{
    struct draw_attributes attributes;
    draw_attributes_decide(&draw, &attributes);
    draw.blend.enable = 1;
    draw.blend.source = GPU_BLEND_ONE;
    draw.blend.destination = GPU_BLEND_ZERO;
    draw.blend.operation = GPU_BLEND_OP_ADD;
    struct gpu_draw cases[8];
    for (int i = 0; i < 8; i++) cases[i] = draw;
    cases[0].blend.source = GPU_BLEND_SOURCE_ALPHA;
    cases[1].blend.destination = GPU_BLEND_ONE;
    cases[2].blend.operation = GPU_BLEND_OP_REVERSE_SUBTRACT;
    cases[3].blend.color_write_mask = 7;
    cases[4].blend.alpha_separate = 1; cases[4].blend.alpha_source = GPU_BLEND_ZERO;
    cases[5].blend.alpha_separate = 1; cases[5].blend.alpha_source = GPU_BLEND_ONE;
    cases[5].blend.alpha_destination = GPU_BLEND_ONE;
    cases[6].pixel_shader = [shaders add:table_get(shaders, draw.pixel_shader)];
    cases[7].vertex_shader = [shaders add:table_get(shaders, draw.vertex_shader)];
    id base = draw_pipeline(&draw, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid);
    assert(base);
    for (int i = 0; i < 8; i++) {
        unsigned long before = metal_profile.pipeline_lookups;
        assert(draw_pipeline(&cases[i], &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid));
        assert(metal_profile.pipeline_lookups == before + 1);
        before = metal_profile.pipeline_lookups;
        assert(draw_pipeline(&cases[i], &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid));
        assert(metal_profile.pipeline_lookups == before + expected_lookup(0));
        assert(draw_pipeline(&draw, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid) == base);
    }
    unsigned long before = metal_profile.pipeline_lookups;
    struct gpu_draw invalid = draw;
    invalid.pixel_shader = 0;
    assert(!draw_pipeline(&invalid, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid));
    assert(metal_profile.pipeline_lookups == before + 1);
    before = metal_profile.pipeline_lookups;
    draw.blend.color = 0xff123456; /* dynamic blend color is not a pipeline key */
    assert(draw_pipeline(&draw, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid) == base);
    assert(metal_profile.pipeline_lookups == before + expected_lookup(0));
    attributes.kinds[0] = GPU_ATTRIBUTE_FLOAT3;
    before = metal_profile.pipeline_lookups;
    assert(draw_pipeline(&draw, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid));
    assert(metal_profile.pipeline_lookups == before + 1);
    before = metal_profile.pipeline_lookups;
    assert(draw_pipeline(&draw, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatDepth32Float_Stencil8));
    assert(metal_profile.pipeline_lookups == before + 1);
    gpu_metal_warm_begin(); gpu_metal_warm_end();
    before = metal_profile.pipeline_lookups;
    assert(draw_pipeline(&draw, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatDepth32Float_Stencil8));
    assert(metal_profile.pipeline_lookups == before + 1);
    assert(draw_pipeline(&draw, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid));
    before = metal_profile.pipeline_lookups;
    assert(draw_pipeline(&draw, &attributes, 1, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid));
    assert(metal_profile.pipeline_lookups == before + 1); /* exact-border shader variant */
    assert(draw_pipeline(&draw, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid));
    if ([device supportsTextureSampleCount:2]) {
        struct gpu_texture_description target = texture_record(draw.color_target)->description;
        target.sample_count = 2;
        draw.color_target = gpu_metal_texture_create(&target);
        before = metal_profile.pipeline_lookups;
        assert(draw_pipeline(&draw, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid));
        assert(metal_profile.pipeline_lookups == before + 1); /* live MSAA storage changed */
        gpu_metal_texture_destroy(draw.color_target);
    }
}

static void depth_transition_test(void)
{
    struct gpu_depth_stencil_state base = { .depth_test = 1, .depth_function = GPU_COMPARE_ALWAYS,
        .stencil_test = 1, .stencil_function = GPU_COMPARE_ALWAYS,
        .stencil_read_mask = 255, .stencil_write_mask = 255 };
    struct gpu_depth_stencil_state cases[11];
    for (int i = 0; i < 11; i++) cases[i] = base;
    cases[0].depth_test = 0; cases[1].depth_write = 1;
    cases[2].depth_function = GPU_COMPARE_LESS; cases[3].stencil_test = 0;
    cases[4].stencil_function = GPU_COMPARE_EQUAL; cases[5].stencil_fail = GPU_STENCIL_REPLACE;
    cases[6].stencil_depth_fail = GPU_STENCIL_INCREMENT_CLAMP;
    cases[7].stencil_pass = GPU_STENCIL_INVERT;
    cases[8].stencil_read_mask = 127; cases[9].stencil_write_mask = 127;
    cases[10].stencil_function = GPU_COMPARE_NEVER;
    id original = depth_state(&base);
    assert(original);
    for (int i = 0; i < 11; i++) {
        unsigned long before = metal_profile.depth_lookups;
        assert(depth_state(&cases[i]));
        assert(metal_profile.depth_lookups == before + 1);
        before = metal_profile.depth_lookups;
        assert(depth_state(&cases[i]));
        assert(metal_profile.depth_lookups == before + expected_lookup(0));
        assert(depth_state(&base) == original);
    }
    unsigned long before = metal_profile.depth_lookups;
    base.stencil_reference = 42; base.stencil_read_mask |= 0xff00;
    assert(depth_state(&base) == original);
    assert(metal_profile.depth_lookups == before + expected_lookup(0));
}

int main(int argc, const char **argv)
{
    @autoreleasepool {
        if (!MTLCreateSystemDefaultDevice()) return 77;
        probe_layer = [CAMetalLayer layer];
        struct gpu_capabilities capabilities;
        gpu_metal_initialize(GPU_INITIALIZE_NO_PIPELINE_ARCHIVE | GPU_INITIALIZE_NO_SPECIALIZE, &capabilities);
#ifdef HALO_MACOS_METAL_V2
        prepared_native_enabled = !(argc > 1 && !strcmp(argv[1], "--native-off"));
#else
        (void)argc; (void)argv;
#endif
        struct gpu_draw draw = make_draw();
        assert(draw.vertex_shader && draw.pixel_shader && draw.color_target);
        transition_test(draw);
        static const char *names[] = { "same-state", "changing-constants", "changing-ranges" };
        for (int mode = 0; mode < 3; mode++) {
            uint64_t digest = 0;
            for (int warm = 0; warm < 2; warm++) draw_batch(&draw, mode, 0, &digest);
            printf("{\"workload\":\"%s\",\"draws\":%u,\"ns_per_draw\":[", names[mode], PROBE_DRAWS);
            for (int run = 0; run < PROBE_RUNS; run++) {
                double ns = draw_batch(&draw, mode, 0, &digest);
                printf("%s%.3f", run ? "," : "", ns);
            }
            draw_batch(&draw, mode, 1, &digest); /* counters never contaminate timings */
            assert(metal_profile.calls[DRAW_PROFILE_ENCODE] == PROBE_DRAWS);
#ifdef HALO_MACOS_METAL_V2
            unsigned long lookups = prepared_native_enabled ? 1 : PROBE_DRAWS;
            assert(metal_profile.pipeline_lookups == lookups && metal_profile.depth_lookups == lookups);
            assert(metal_profile.attribute_table_binds == (mode == 2 ? PROBE_DRAWS : 1));
#else
            assert(metal_profile.pipeline_lookups == PROBE_DRAWS && metal_profile.depth_lookups == PROBE_DRAWS);
            assert(metal_profile.attribute_table_binds == PROBE_DRAWS);
#endif
            printf("],\"digest\":\"%016llx\",\"shader_lookups\":%lu,\"pipeline_lookups\":%lu,"
                "\"depth_lookups\":%lu,\"attribute_binds\":%lu}\n", (unsigned long long)digest,
                metal_profile.shader_lookups, metal_profile.pipeline_lookups, metal_profile.depth_lookups,
                metal_profile.attribute_table_binds);
        }
        pipeline_transition_test(draw);
        depth_transition_test();
        dispatch_group_wait(completion_handlers, DISPATCH_TIME_FOREVER);
        return 0;
    }
}
