/* MSL frame around this branch's bundled SMAA.hlsl (HIGH, SMAA 1x).
   The algorithm and lookup textures are unchanged. HLSL out parameters need
   Metal thread references; swizzles cannot bind references, so the few
   inout-swizzle calls use values or temporaries instead. Keep this adapter
   tested against the bundled file when updating SMAA. */
#ifndef METAL_SMAA_SOURCE_H
#define METAL_SMAA_SOURCE_H
#import <Foundation/Foundation.h>

static NSString *metal_smaa_source(NSString *body)
{
    if (!body.length) return nil;
    NSRange begin = [body rangeOfString:@"void SMAAMovc(bool2"];
    NSRange end = [body rangeOfString:@"#if SMAA_INCLUDE_VS" options:0
        range:NSMakeRange(begin.location == NSNotFound ? 0 : begin.location,
            begin.location == NSNotFound ? body.length : body.length - begin.location)];
    if (begin.location == NSNotFound || end.location == NSNotFound) return nil;
    body = [body stringByReplacingCharactersInRange:NSMakeRange(begin.location, end.location - begin.location)
        withString:@"float2 SMAAMovc(bool2 c, float2 v, float2 x) { return select(v, x, c); }\n"
                    "float4 SMAAMovc(bool4 c, float4 v, float4 x) { return select(v, x, c); }\n"];
    NSArray<NSArray<NSString *> *> *changes = @[
        @[@"out float4 offset[3]", @"thread float4 *offset"],
        @[@"float4 offset[3]", @"thread float4 *offset"],
        @[@"out float2 pixcoord", @"thread float2 &pixcoord"],
        @[@"out float4 offset", @"thread float4 &offset"],
        @[@"out float2 e", @"thread float2 &e"],
        @[@"inout float2 weights", @"thread float2 &weights"],
        @[@"SMAAMovc(bool2(step(0.9, d.zw)), cc, float2(0.0, 0.0));",
          @"cc = SMAAMovc(bool2(step(0.9, d.zw)), cc, float2(0.0, 0.0));"],
        @[@"SMAAMovc(bool4(h, h, h, h), blendingOffset, float4(a.x, 0.0, a.z, 0.0));",
          @"blendingOffset = SMAAMovc(bool4(h, h, h, h), blendingOffset, float4(a.x, 0.0, a.z, 0.0));"],
        @[@"SMAAMovc(bool2(h, h), blendingWeight, a.xz);",
          @"blendingWeight = SMAAMovc(bool2(h, h), blendingWeight, a.xz);"],
        @[@"SMAADetectHorizontalCornerPattern(SMAATexturePass2D(edgesTex), weights.rg, coords.xyzy, d);",
          @"float2 corner = weights.rg; SMAADetectHorizontalCornerPattern(edgesTex, corner, coords.xyzy, d); weights.rg = corner;"],
        @[@"SMAADetectVerticalCornerPattern(SMAATexturePass2D(edgesTex), weights.ba, coords.xyxz, d);",
          @"float2 corner = weights.ba; SMAADetectVerticalCornerPattern(edgesTex, corner, coords.xyxz, d); weights.ba = corner;"]
    ];
    for (NSArray<NSString *> *change in changes) {
        if ([body rangeOfString:change[0]].location == NSNotFound) {
            NSLog(@"Metal SMAA: incompatible bundled shader (missing %@)", change[0]);
            return nil;
        }
        body = [body stringByReplacingOccurrencesOfString:change[0] withString:change[1]];
    }
    NSString *prefix = @"#include <metal_stdlib>\nusing namespace metal;\n"
        "#define SMAA_CUSTOM_SL 1\n#define SMAA_PRESET_HIGH 1\n"
        "#define SMAA_RT_METRICS metrics\n#define SMAA_FLATTEN\n#define SMAA_BRANCH\n"
        "#define mad(a,b,c) fma(a,b,c)\n#define lerp(a,b,t) mix(a,b,t)\n"
        "#define saturate(a) clamp(a,0.0,1.0)\n#define discard discard_fragment()\n"
        "#define SMAATexture2D(t) texture2d<float> t\n#define SMAATexturePass2D(t) t\n"
        "#define areaTex_bounds float4(0,0,1,1)\n#define searchTex_bounds float4(0,0,1,1)\n"
        "#define tex_bounds bounds\n#define edgesTex_bounds bounds\n#define colorTex_bounds bounds\n"
        "#define blendTex_bounds bounds\n#define depthTex_bounds bounds\n#define predicationTex_bounds bounds\n"
        "#define velocityTex_bounds bounds\n#define currentColorTex_bounds bounds\n#define previousColorTex_bounds bounds\n"
        "constexpr sampler smaa_linear(coord::normalized, address::clamp_to_edge, filter::linear);\n"
        "constexpr sampler smaa_point(coord::normalized, address::clamp_to_edge, filter::nearest);\n"
        "#define SMAASampleLevelZero(t,c) t.sample(smaa_linear,clamp((c),t##_bounds.xy,t##_bounds.zw),level(0))\n"
        "#define SMAASampleLevelZeroPoint(t,c) t.sample(smaa_point,clamp((c),t##_bounds.xy,t##_bounds.zw),level(0))\n"
        "#define SMAASampleLevelZeroOffset(t,c,o) SMAASampleLevelZero(t,(c)+float2(o)*metrics.xy)\n"
        "#define SMAASample(t,c) SMAASampleLevelZero(t,c)\n#define SMAASamplePoint(t,c) SMAASampleLevelZeroPoint(t,c)\n"
        "#define SMAASampleOffset(t,c,o) SMAASampleLevelZeroOffset(t,c,o)\n"
        "struct SMAA { float4 metrics; float4 bounds;\n";
    NSString *suffix = @"\n};\n"
        "vertex float4 smaa_vertex(uint v [[vertex_id]]) { return float4(float((v&1)<<2)-1, float((v&2)<<1)-1,0,1); }\n"
        "fragment float4 smaa_edges(float4 p [[position]], constant float4 &metrics [[buffer(0)]],"
        "constant float4 &bounds [[buffer(1)]], texture2d<float> color [[texture(0)]]) {\n"
        "SMAA s; s.metrics=metrics; s.bounds=bounds; float4 offsets[3]; float2 uv=p.xy*metrics.xy;\n"
        "s.SMAAEdgeDetectionVS(uv,offsets); return float4(s.SMAALumaEdgeDetectionPS(uv,offsets,color),0,0); }\n"
        "fragment float4 smaa_weights(float4 p [[position]], constant float4 &metrics [[buffer(0)]],"
        "constant float4 &bounds [[buffer(1)]], texture2d<float> edges [[texture(0)]],"
        "texture2d<float> area [[texture(1)]], texture2d<float> search [[texture(2)]]) {\n"
        "SMAA s; s.metrics=metrics; s.bounds=bounds; float4 offsets[3]; float2 pix; float2 uv=p.xy*metrics.xy;\n"
        "s.SMAABlendingWeightCalculationVS(uv,pix,offsets);"
        "return s.SMAABlendingWeightCalculationPS(uv,pix,offsets,edges,area,search,float4(0)); }\n"
        "fragment float4 smaa_blend(float4 p [[position]], constant float4 &metrics [[buffer(0)]],"
        "constant float4 &bounds [[buffer(1)]], texture2d<float> color [[texture(0)]],"
        "texture2d<float> weights [[texture(1)]]) {\n"
        "SMAA s; s.metrics=metrics; s.bounds=bounds; float4 offset; float2 uv=p.xy*metrics.xy;\n"
        "s.SMAANeighborhoodBlendingVS(uv,offset); return s.SMAANeighborhoodBlendingPS(uv,offset,color,weights); }\n";
    return [[prefix stringByAppendingString:body] stringByAppendingString:suffix];
}
#endif
