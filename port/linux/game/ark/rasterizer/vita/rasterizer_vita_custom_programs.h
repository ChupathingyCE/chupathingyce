/*
RASTERIZER_VITA_CUSTOM_PROGRAMS.H

(port) The Vita's twins of the desktop's custom fragment programs
(rasterizer_xbox_transparent_geometry.c): Halo 2's passes that the
register combiners cannot draw, as Cg for the GXM device. Each is the body
of a fragment program: the device (d3d8_gxm.c custom_fragment_source)
wraps it in what every Direct3D draw's program has - the vertex program's
varyings xD0, xD1, xT0..xT3 and xFog (nv2a_psh_cg.c), the stages' maps
tex0..tex3 (2D), texture_scale[4] (the device's second uniform buffer:
1 for a swizzled map) - and h2u[] at BUFFER[0]: h2u[0] the viewport in the
target's pixels, h2u[1..] the caller's values, as the desktop's GLSL has
them. The arithmetic is the GLSL's, line for line, in float (the noises'
sharpenings square a difference three times: half precision would band).
Their programs are in shaders.pak (vita_shader_pack.py): the Vita compiles
nothing that is not.
Included by rasterizer_xbox_transparent_geometry.c.
*/

#ifndef __RASTERIZER_VITA_CUSTOM_PROGRAMS_H
#define __RASTERIZER_VITA_CUSTOM_PROGRAMS_H

/* h2_plasma_fragment_source's (plasma_alpha: the needler's core, the plasma
rifle's and pistol's currents, the flywheels' lightning and static) */
static __attribute__((unused)) char const vita_h2_plasma_body[] =
	"\tfloat4 t1 = tex2D(tex0, xT0.xy * texture_scale[0].xy);\n"
	"\tfloat4 t2 = tex2D(tex1, xT1.xy * texture_scale[1].xy);\n"
	"\tfloat4 t3 = tex2D(tex2, xT2.xy * texture_scale[2].xy);\n"
	"\tfloat r0a = clamp(t2.b - (t1.b - 0.5), -1.0, 1.0);\n"
	"\tfloat r0b = clamp(t1.b - (t2.b - 0.5), -1.0, 1.0);\n"
	"\tfloat basic = clamp(4.0 * (r0a < 0.5 ? r0a * r0a : r0b * r0b), -1.0, 1.0);\n"
	"\tfloat e = 2.0 * saturate(basic) - 1.0;\n"
	"\tfloat s1 = basic < 0.5 ? 0.0 : e * e;\n"
	"\tfloat3 r1 = clamp(s1 * h2u[1].rgb + (1.0 - s1) * h2u[2].rgb, -1.0, 1.0);\n"
	"\te = 2.0 * s1 - 1.0;\n"
	"\tfloat s2 = s1 < 0.5 ? 0.0 : e * e;\n"
	"\te = 2.0 * s2 - 1.0;\n"
	"\tfloat s3 = s2 < 0.5 ? 0.0 : e * e;\n"
	"\tfloat d0a = h2u[4].y;\n"
	"\tfloat d1 = (1.0 - d0a) * (1.0 - d0a);\n"
	"\td1 *= d1;\n"
	"\td0a = d0a * (1.0 - h2u[4].x) + (1.0 - d1) * h2u[4].x;\n"
	"\tfloat3 c = saturate(r1 * basic + s3 * h2u[3].rgb);\n"
	"\tfloat a = saturate(d0a * t3.a);\n"
	"\tif (h2u[4].z > 3.5) return float4(t3.a, t3.a, t3.a, 1.0);\n"
	"\tif (h2u[4].z > 2.5) return float4(s3, s3, s3, 1.0);\n"
	"\tif (h2u[4].z > 1.5) return float4(basic, basic, basic, 1.0);\n"
	"\tif (h2u[4].z > 0.5) return float4(t1.b, t1.b, t1.b, 1.0);\n"
	"\treturn float4(c * a, 0.0);\n";

/* h2_plasma_mask_offset_fragment_source's (plasma_mask_offset: the energy
sword's blade, the Jackal shield, the Spectre's window shield) */
static __attribute__((unused)) char const vita_h2_plasma_mask_offset_body[] =
	"\tfloat4 t0 = tex2D(tex0, xT0.xy * texture_scale[0].xy);\n"
	"\tfloat4 t1 = tex2D(tex1, xT1.xy * texture_scale[1].xy);\n"
	"\tfloat4 t2 = tex2D(tex2, xT2.xy * texture_scale[2].xy);\n"
	"\tfloat4 t3 = tex2D(tex3, xT3.xy * texture_scale[3].xy);\n"
	"\tfloat f1 = h2u[3].x, f2 = h2u[3].y;\n"
	"\tfloat v = (1.0 - f1) * t2.a + f1 * t0.a;\n"
	"\tfloat3 r0 = float3(v, v, v);\n"
	"\tfloat r0a = (1.0 - f2) * 0.5 + f2 * t1.a;\n"
	"\tr0 = float3(t3.a * 0.5, t3.a * 0.5, t3.a * 0.5) + (1.0 - t3.a) * r0;\n"
	"\tr0a = t3.a * 0.5 + (1.0 - t3.a) * r0a;\n"
	"\tfloat3 n0 = clamp(r0 - (r0a - 0.5), -1.0, 1.0);\n"
	"\tfloat n0a = clamp(r0a - (r0.b - 0.5), -1.0, 1.0);\n"
	"\tfloat ua = saturate(n0a), ub = saturate(n0.b);\n"
	"\tfloat basic = clamp(4.0 * (ua < 0.5 ? ua * ua : ub * ub), -1.0, 1.0);\n"
	"\tfloat e = 2.0 * saturate(basic) - 1.0;\n"
	"\tfloat s1 = basic < 0.5 ? 0.0 : e * e;\n"
	"\tfloat3 tinted = t3.rgb * h2u[1].rgb;\n"
	"\tfloat3 glow = (1.0 - t3.a) * h2u[2].rgb;\n"
	"\tfloat dull = saturate(s1 + s1 * (1.0 - s1));\n"
	"\tfloat3 c = saturate(dull * tinted + glow) * h2u[3].z;\n"
	"\tif (h2u[3].w > 4.5) return float4(t3.rgb, 1.0);\n"
	"\tif (h2u[3].w > 3.5) return float4(t3.a, t3.a, t3.a, 1.0);\n"
	"\tif (h2u[3].w > 2.5) return float4(t2.a, t2.a, t2.a, 1.0);\n"
	"\tif (h2u[3].w > 1.5) return float4(basic, basic, basic, 1.0);\n"
	"\tif (h2u[3].w > 0.5) return float4(t0.a, t0.a, t0.a, 1.0);\n"
	"\treturn float4(c, 0.0);\n";

/* h2_particle_plasma_fragment_source's (particle_plasma: a particle model
of the plasma_alpha pass without its alpha map). The maps read at their
largest level (the GLSL's textureLod 0), wrapped by hand */
static __attribute__((unused)) char const vita_h2_particle_plasma_body[] =
	"\tfloat2 uv = xT0.xy * max(texture_scale[0].xy, float2(1.0, 1.0));\n"
	"\tfloat4 t0 = tex2Dlod(tex0, float4(frac(uv * h2u[4].x), 0.0, 0.0));\n"
	"\tfloat4 t1 = tex2Dlod(tex1, float4(frac((uv + float2(0.0, h2u[4].z)) * h2u[4].y), 0.0, 0.0));\n"
	"\tfloat n0 = min(t0.b, t0.a), n1 = min(t1.b, t1.a);\n"
	"\tfloat r0b = clamp(n0 - (n1 - 0.5), -1.0, 1.0);\n"
	"\tfloat r0a = clamp(n1 - (n0 - 0.5), -1.0, 1.0);\n"
	"\tfloat basic = clamp(4.0 * (r0a < 0.5 ? r0a * r0a : r0b * r0b), -1.0, 1.0);\n"
	"\tfloat e = 2.0 * saturate(basic) - 1.0;\n"
	"\tfloat s1 = basic < 0.5 ? 0.0 : e * e;\n"
	"\tfloat3 r1 = s1 * h2u[1].rgb + (1.0 - s1) * h2u[2].rgb;\n"
	"\te = 2.0 * s1 - 1.0;\n"
	"\tfloat s2 = s1 < 0.5 ? 0.0 : e * e;\n"
	"\te = 2.0 * s2 - 1.0;\n"
	"\tfloat s3 = s2 < 0.5 ? 0.0 : e * e;\n"
	"\tfloat3 c = saturate(r1 * basic + s3 * h2u[3].rgb);\n"
	"\treturn float4(saturate(basic * xD0.a) * c * xD1.rgb, 0.0);\n";

/* distortion_fragment_source's (Halo 2's distortion particles, shader flag
13: the heat haze over a plasma bolt, a shock wave's ring): the screen's
copy (tex1, rasterizer_distortion_set_texture) read again offset by the
map's normal (tex0: red and green about one half) times the distortion
scale and the sprite's alpha. wpos is the fragment's place in the target's
pixels (the device's WPOS, from the top left; the copy upside down to it on
the hardware, as the GL device's: h2u[1].y 1, the copy's lookup turned
over); h2u[0] the viewport */
static __attribute__((unused)) char const vita_h2_distortion_body[] =
	"\tfloat4 n = tex2D(tex0, xT0.xy * texture_scale[0].xy);\n"
	"\tfloat2 d = n.rg * 2.0 - 1.0;\n"
	"\tfloat a = saturate(xD1.a);\n"
	"\tfloat2 uv = (wpos.xy - h2u[0].xy) / max(h2u[0].zw, float2(1.0, 1.0));\n"
	"\tfloat2 o = d * h2u[1].x * a;\n"
	"\tif (h2u[1].y > 0.5) { uv.y = 1.0 - uv.y; o.y = -o.y; }\n"
	"\tfloat3 c = tex2D(tex1, clamp(uv + o, float2(0.002, 0.002), float2(0.998, 0.998))).rgb;\n"
	"\tfloat m = saturate(n.a * 3.0) * a;\n"
	"\tif (h2u[1].z > 2.5) return float4(n.rgb, 1.0);\n"
	"\tif (h2u[1].z > 1.5) return float4(tex2D(tex1, uv).rgb, 1.0);\n"
	"\tif (h2u[1].z > 0.5) return float4(1.0, 0.0, 0.0, 0.3 + 0.7 * m);\n"
	"\treturn float4(c, m);\n";

/* ---------- program pairs (h2_materials_render.c: the models' per-pixel pass)

A vertex body and a fragment body drawn as a pair (d3d8_gxm.c
custom_vertex_source): the vertex body runs after the draw's own vertex
program (its NV2A program as nv2a_vsh_cg.c translates it, so the position -
out_position - is that program's to the bit, and the pass's LESSEQUAL depth
test meets the part it draws over), in a block of its own: it sees the
attributes v0..v15 (NORMPACKED3 unpacked), the constant chunks cA (c0-11),
cB (c12-16), cC1 (c17-27), cC2 (c28-59), cD (c60-183), cE (c184-191)
(vita_xgpu.h) and h2u[] (BUFFER[7]), and writes out_t0..out_t3 (out_d0,
out_d1 and out_fog written zero). Its names start with h_ (the program's own
are r0.., A, B, C, oPos...). The fragment body reads xT0..xT3 and h2u[] as
the caller set them - no viewport in h2u[0]: a pair's values are the GLSL's
own. */

/* model_pass_vertex_source's varyings: the model's compressed vertices
skinned by its node matrices (c[60 + 3 node], cD), out: the base map's
coordinates, the two lights' and the eye's directions in tangent space */
static __attribute__((unused)) char const vita_h2_model_pass_vertex_body[] =
	"\t\tfloat h_i0 = floor(v5.x * cA[7].w + 0.001);\n"
	"\t\tfloat h_i1 = floor(v5.y * cA[7].w + 0.001);\n"
	"\t\tfloat h_w0 = v6.x, h_w1 = v6.w - v6.x;\n"
	"\t\tfloat4 h_r4 = h_w0 * cD[(int)clamp(h_i0, 0.0, 123.0)] + h_w1 * cD[(int)clamp(h_i1, 0.0, 123.0)];\n"
	"\t\tfloat4 h_r5 = h_w0 * cD[(int)clamp(h_i0 + 1.0, 0.0, 123.0)] + h_w1 * cD[(int)clamp(h_i1 + 1.0, 0.0, 123.0)];\n"
	"\t\tfloat4 h_r6 = h_w0 * cD[(int)clamp(h_i0 + 2.0, 0.0, 123.0)] + h_w1 * cD[(int)clamp(h_i1 + 2.0, 0.0, 123.0)];\n"
	"\t\tfloat3 h_p = float3(dot(v0.xyz, h_r4.xyz) + h_r4.w, dot(v0.xyz, h_r5.xyz) + h_r5.w, dot(v0.xyz, h_r6.xyz) + h_r6.w);\n"
	"\t\tfloat3 h_n = v1.xyz, h_b = v2.xyz, h_t = v3.xyz;\n"
	"\t\tfloat3 h_N = normalize(float3(dot(h_n, h_r4.xyz), dot(h_n, h_r5.xyz), dot(h_n, h_r6.xyz)));\n"
	"\t\tfloat3 h_B = normalize(float3(dot(h_b, h_r4.xyz), dot(h_b, h_r5.xyz), dot(h_b, h_r6.xyz)));\n"
	"\t\tfloat3 h_T = normalize(float3(dot(h_t, h_r4.xyz), dot(h_t, h_r5.xyz), dot(h_t, h_r6.xyz)));\n"
	"\t\tfloat3 h_l0 = -h2u[0].xyz, h_l1 = -h2u[2].xyz, h_e = h2u[5].xyz - h_p;\n"
	"\t\tout_t1 = float4(dot(h_l0, h_T), dot(h_l0, h_B), dot(h_l0, h_N), 0.0);\n"
	"\t\tout_t2 = float4(dot(h_l1, h_T), dot(h_l1, h_B), dot(h_l1, h_N), 0.0);\n"
	"\t\tout_t3 = float4(dot(h_e, h_T), dot(h_e, h_B), dot(h_e, h_N), 0.0);\n"
	"\t\tfloat4 h_uv = float4(v4.xy, 0.0, 1.0);\n"
	"\t\tout_t0 = float4(dot(h_uv, cB[1]), dot(h_uv, cB[2]), 0.0, 1.0);\n";

/* model_pass_fragment_source's (xL0, xL1, xE: xT1, xT2, xT3) */
static __attribute__((unused)) char const vita_h2_model_pass_fragment_body[] =
	"\tfloat3 xL0 = xT1.xyz, xL1 = xT2.xyz, xE = xT3.xyz;\n"
	"\tif (h2u[8].z > 3.5)\n"
	"\t{\n"
	"\t\tfloat3 base = tex2D(tex0, xT0.xy * texture_scale[0].xy).rgb;\n"
	"\t\tfloat shade = dot(base, float3(0.299, 0.587, 0.114));\n"
	"\t\tfloat3 want = h2u[6].rgb * shade;\n"
	"\t\tif (h2u[8].z < 4.5)\n"
	"\t\t\treturn float4(saturate(0.5 * want / max(base, float3(0.03, 0.03, 0.03))), 1.0);\n"
	"\t\tfloat3 nv = normalize(tex2D(tex1, xT0.xy * h2u[8].xy * texture_scale[1].xy).rgb * 2.0 - 1.0);\n"
	"\t\tfloat3 m0 = normalize(xL0), m1 = normalize(xL1);\n"
	"\t\tfloat3 lit = h2u[4].rgb * max(nv.z, 0.0) + h2u[1].rgb * max(dot(nv, m0), 0.0) + h2u[3].rgb * max(dot(nv, m1), 0.0);\n"
	"\t\treturn float4(lit * max(want - 2.0 * base, float3(0.0, 0.0, 0.0)), 1.0);\n"
	"\t}\n"
	"\tif (h2u[8].z > 2.5)\n"
	"\t\treturn float4(lerp(float3(1.0, 1.0, 1.0), h2u[6].rgb, tex2D(tex0, xT0.xy * texture_scale[0].xy).a), 1.0);\n"
	"\tfloat3 n = normalize(tex2D(tex1, xT0.xy * h2u[8].xy * texture_scale[1].xy).rgb * 2.0 - 1.0);\n"
	"\tfloat3 l0 = normalize(xL0), l1 = normalize(xL1);\n"
	"\tif (h2u[8].z < 0.5)\n"
	"\t{\n"
	"\t\tfloat3 bumped = h2u[4].rgb * max(n.z, 0.0) + h2u[1].rgb * max(dot(n, l0), 0.0) + h2u[3].rgb * max(dot(n, l1), 0.0);\n"
	"\t\tfloat3 flat_ = h2u[4].rgb + h2u[1].rgb * max(l0.z, 0.0) + h2u[3].rgb * max(l1.z, 0.0);\n"
	"\t\tfloat f = h2u[9].x * dot(h2u[4].rgb + h2u[1].rgb + h2u[3].rgb, float3(0.299, 0.587, 0.114)) + 0.01;\n"
	"\t\tfloat3 floor_ = float3(f, f, f);\n"
	"\t\tfloat3 relief = clamp(lerp(float3(1.0, 1.0, 1.0), (bumped + floor_) / (flat_ + floor_), h2u[9].y), h2u[9].z, h2u[9].w);\n"
	"\t\tfloat3 dbg_e = normalize(xE);\n"
	"\t\tfloat dbg_r = saturate(2.0 * dot(n, l0) * dot(n, dbg_e) - dot(l0, dbg_e));\n"
	"\t\tif (h2u[8].w > 4.5) return float4(max(dot(n, l0), 0.0), max(dot(n, dbg_e), 0.0), max(-dot(l0, dbg_e), 0.0), 1.0);\n"
	"\t\tif (h2u[8].w > 3.5) { float q = pow(dbg_r, 8.0); return float4(q, q, q, 1.0); }\n"
	"\t\tif (h2u[8].w > 2.5) { float a = tex2D(tex0, xT0.xy * texture_scale[0].xy).a; return float4(a, a, a, 1.0); }\n"
	"\t\tif (h2u[8].w > 1.5) return float4(tex2D(tex0, xT0.xy * texture_scale[0].xy).rgb, 1.0);\n"
	"\t\tif (h2u[8].w > 0.5) return float4(n * 0.5 + 0.5, 1.0);\n"
	"\t\treturn float4(relief * 0.5, 1.0);\n"
	"\t}\n"
	"\tfloat3 e = normalize(xE);\n"
	"\tfloat3 luma = float3(0.299, 0.587, 0.114);\n"
	"\tfloat w0 = dot(h2u[1].rgb, luma), w1 = dot(h2u[3].rgb, luma) * h2u[2].w;\n"
	"\tfloat3 ls = normalize(l0 * w0 + l1 * w1 + float3(0.0, 0.0, 1e-4));\n"
	"\tfloat3 cs = h2u[1].rgb + h2u[3].rgb * h2u[2].w;\n"
	"\tfloat nl = saturate(dot(n, ls)), ne = saturate(dot(n, e));\n"
	"\tfloat r, s;\n"
	"\tif (h2u[8].z > 1.5)\n"
	"\t{\n"
	"\t\tnl = ne;\n"
	"\t\tr = saturate(4.0 * (ne * ne - 0.7529));\n"
	"\t\ts = h2u[7].a < 0.5 ? 0.0 : h2u[7].a < 1.5 ? r * r : h2u[7].a < 2.5 ? r : saturate(4.0 * (r * r - 0.7529));\n"
	"\t}\n"
	"\telse\n"
	"\t{\n"
	"\t\tr = saturate(2.0 * dot(n, ls) * dot(n, e) - dot(ls, e));\n"
	"\t\tfloat r2 = r * r, r4 = r2 * r2, r8 = r4 * r4;\n"
	"\t\ts = h2u[7].a < 0.5 ? 0.0 : h2u[7].a < 1.5 ? r8 : h2u[7].a < 2.5 ? r2 : saturate(4.0 * (r8 - 0.7529));\n"
	"\t}\n"
	"\tfloat3 tint = ne * h2u[6].rgb + (1.0 - ne) * h2u[7].rgb;\n"
	"\tfloat mask = tex2D(tex0, xT0.xy * texture_scale[0].xy).a * saturate(8.0 * nl);\n"
	"\treturn float4(cs * tint * (s * mask * h2u[6].a), 1.0);\n";

/* model_plasma_illum_fragment_source's (Halo 2's plasma self-illumination:
the Covenant weapons' and vehicles' glowing channels) */
static __attribute__((unused)) char const vita_h2_model_plasma_illum_fragment_body[] =
	"\tfloat4 t = tex2D(tex0, xT0.xy * texture_scale[0].xy);\n"
	"\tfloat time = h2u[3].x;\n"
	"\tfloat r0 = clamp(t.a - (time - 0.5), -1.0, 1.0);\n"
	"\tfloat r0a = clamp(time - (t.a - 0.5), -1.0, 1.0);\n"
	"\tfloat basic = clamp(4.0 * (r0a < 0.5 ? r0a * r0a : r0 * r0), -1.0, 1.0);\n"
	"\tfloat e = 2.0 * saturate(basic) - 1.0;\n"
	"\tfloat s1 = basic < 0.5 ? 0.0 : e * e;\n"
	"\te = 2.0 * s1 - 1.0;\n"
	"\tfloat s2 = s1 < 0.5 ? 0.0 : e * e;\n"
	"\tif (time > 1.5) s2 = 1.0;\n"
	"\tfloat3 c = s2 * h2u[0].rgb * t.r + t.g * h2u[1].rgb + t.b * h2u[2].rgb;\n"
	"\treturn float4(saturate(c), 0.0);\n";

/* h2_water_fragment_source's (rasterizer_xbox_water.c: Halo 2's
water_static pass over a converted level's sea and its pools' 2D
reflections): tex0 the ripple target (its levels 128, 64 and 32 chained -
d3d8_gxm.c render_target_chain - so the sampler's own level is the GLSL's
min(textureQueryLod, 2)), tex1 the fresnel map, tex2 the screen's copy,
tex3 the reflection map (2D: the sky by azimuth and elevation, or a pool's
own map at the screen's coordinates). wpos from the top, as the GL
device's flipped target has gl_FragCoord: the eye rays' ndc.y turned over
as the GL's are; the copy's lookup turned over by h2u[1].y (1 on the
hardware, as on GL).
h2u[10] the fog colour (the GL program's fog_color: D3DRS_FOGCOLOR). The
bump flipbook sheet (h2u[8], its width in texels h2u[7].w - the GL's
textureSize) as the PC's program reads it: the frame of the moment's cell,
its level by the footprint's short axis, flattened past it by the long
axis's - read before as one bump map, the sheet's 32 small frames averaged
the Vita's sea flat, none of the PC's ripples or their glitter */
static __attribute__((unused)) char const vita_h2_water_body[] =
	"\tfloat2 uv = (wpos.xy - h2u[0].xy) / max(h2u[0].zw, float2(1.0, 1.0));\n"
	"\tfloat2 ndc = uv * 2.0 - 1.0;\n"
	"\tndc.y = -ndc.y;\n"
	"\tfloat3 d = normalize(h2u[4].xyz + h2u[5].xyz * (ndc.x * h2u[4].w * h2u[5].w) + h2u[6].xyz * (ndc.y * h2u[4].w));\n"
	"\tfloat3 e = -d;\n"
	"\tfloat along = h2u[6].w / max(-d.z, 0.001);\n"
	"\tfloat2 world = float2(h2u[2].w, h2u[3].g) + d.xy * along;\n"
	"\tfloat2 tc = world * h2u[1].z;\n"
	"\tfloat4 b;\n"
	"\tif (h2u[8].x > 0.5)\n"
	"\t{\n"
	"\t\tfloat2 sheet = float2(h2u[8].y, h2u[8].z);\n"
	"\t\tfloat2 cell = float2(fmod(h2u[8].w, h2u[8].y), floor(h2u[8].w / h2u[8].y));\n"
	"\t\tfloat2 ftc = tc + 0.5;\n"
	"\t\tfloat2 px = ftc * h2u[7].w / h2u[8].y;\n"
	"\t\tfloat2 dx = ddx(px), dy = ddy(px);\n"
	"\t\tfloat lodshort = 0.5 * log2(max(min(dot(dx, dx), dot(dy, dy)), 1.0));\n"
	"\t\tfloat lodlong = 0.5 * log2(max(max(dot(dx, dx), dot(dy, dy)), 1.0));\n"
	"\t\tfloat lod = clamp(lodshort + h2u[9].w, 0.0, 3.0);\n"
	"\t\tfloat edge = 0.5 * h2u[8].y / max(h2u[7].w, 1.0);\n"
	"\t\tb = tex2Dlod(tex0, float4((cell + clamp(frac(ftc), edge, 1.0 - edge)) / sheet, 0.0, lod));\n"
	"\t\tb.rgb = lerp(float3(128.0 / 255.0, 128.0 / 255.0, 128.0 / 255.0), b.rgb, exp2(-0.5 * h2u[8].z * max(lodlong - lod, 0.0)));\n"
	"\t}\n"
	"\telse b = tex2D(tex0, tc);\n"
	"\tfloat3 bn = (b.rgb * 255.0 - 128.0) / 127.0;\n"
	"\tbn.xy *= h2u[9].x;\n"
	"\tbn.xy /= 1.0 + along * h2u[3].b;\n"
	"\tfloat3 n = normalize(bn);\n"
	"\tfloat2 o = n.xy * h2u[1].x;\n"
	"\tfloat2 suv = uv, so = o;\n"
	"\tif (h2u[1].y > 0.5) { suv.y = 1.0 - suv.y; so.y = -so.y; }\n"
	"\tfloat3 scene = tex2D(tex2, clamp(suv + so, float2(0.002, 0.002), float2(0.998, 0.998))).rgb;\n"
	"\tfloat3 r;\n"
	"\tif (h2u[9].y > 0.5) r = reflect(d, n);\n"
	"\telse\n"
	"\t{\n"
	"\t\tfloat2 ndc2 = ndc + 2.0 * o * float2(1.0, -1.0);\n"
	"\t\tr = normalize(h2u[4].xyz + h2u[5].xyz * (ndc2.x * h2u[4].w * h2u[5].w) + h2u[6].xyz * (ndc2.y * h2u[4].w));\n"
	"\t\tr.z = -r.z;\n"
	"\t}\n"
	"\tfloat3 refl;\n"
	"\tif (h2u[1].w > 0.5)\n"
	"\t{\n"
	"\t\tfloat el = asin(clamp(r.z, 0.02, 1.0)) / 1.5707963;\n"
	"\t\tfloat az = atan2(r.y, r.x) / 6.2831853 + 0.5;\n"
	"\t\trefl = tex2D(tex3, float2(az, el)).rgb;\n"
	"\t}\n"
	"\telse refl = tex2D(tex3, uv + float2(o.x, -o.y)).rgb;\n"
	"\tfloat4 f = tex2D(tex1, float2(0.5 + 0.5 * min(length(e.xy), 1.0), 0.5));\n"
	"\tfloat3 fn = normalize(f.rgb * 2.0 - 1.0);\n"
	"\tfloat glitter = pow(max(dot(fn, h2u[2].xyz), 0.0), 8.0);\n"
	"\tfloat3 c = f.a * refl + (1.0 - f.a) * scene + glitter * h2u[3].r;\n"
	"\tfloat fogged = saturate((along - h2u[7].x) / max(h2u[7].y - h2u[7].x, 0.001)) * h2u[7].z;\n"
	"\tif (h2u[3].a > 3.5) return float4(refl, 1.0);\n"
	"\tif (h2u[3].a > 2.5) return float4(f.aaa, 1.0);\n"
	"\tif (h2u[3].a > 1.5) return float4(n * 0.5 + 0.5, 1.0);\n"
	"\tif (h2u[3].a > 0.5) return float4(scene, 1.0);\n"
	"\treturn float4(lerp(c, h2u[10].rgb, fogged), 1.0);\n";

/* vita_h2_water_body's two common cases as programs of their own (the
values the body tests for its cases - the copy's flip, the debug view, the
reflection's way - as the hardware has them: the copy turned over, no debug
view, the reflection along the ray off the tilted normal), the sky's map by
azimuth and elevation (a sea: Zanzibar) or a pool's own map at the screen's
coordinates (Beaver Creek). Each is the body's own steps for its case, in
the same order, the other cases' steps left out: compiled, the general
body is 178 instructions and 30 temporaries a pixel, the sky's 123 and 17,
the pool's 79 and 17 (the dev Vita's sea camera: GPU 37.4-37.7 ms -> 30.7-31.7).
rasterizer_xbox_water.c picks one with HALO_H2_WATER_SPECIALIZED=1 (off by
default: a frame differs from the general body's in a few pixels by one
level, which the sea's refraction of its own last frame then carries) */
static __attribute__((unused)) char const vita_h2_water_sky_body[] =
	"\tfloat2 uv = (wpos.xy - h2u[0].xy) / max(h2u[0].zw, float2(1.0, 1.0));\n"
	"\tfloat2 ndc = uv * 2.0 - 1.0;\n"
	"\tndc.y = -ndc.y;\n"
	"\tfloat3 d = normalize(h2u[4].xyz + h2u[5].xyz * (ndc.x * h2u[4].w * h2u[5].w) + h2u[6].xyz * (ndc.y * h2u[4].w));\n"
	"\tfloat3 e = -d;\n"
	"\tfloat along = h2u[6].w / max(-d.z, 0.001);\n"
	"\tfloat2 world = float2(h2u[2].w, h2u[3].g) + d.xy * along;\n"
	"\tfloat2 tc = world * h2u[1].z;\n"
	"\tfloat4 b = tex2D(tex0, tc);\n"
	"\tfloat3 bn = (b.rgb * 255.0 - 128.0) / 127.0;\n"
	"\tbn.xy *= h2u[9].x;\n"
	"\tbn.xy /= 1.0 + along * h2u[3].b;\n"
	"\tfloat3 n = normalize(bn);\n"
	"\tfloat2 o = n.xy * h2u[1].x;\n"
	"\tfloat2 suv = uv, so = o;\n"
	"\tsuv.y = 1.0 - suv.y; so.y = -so.y;\n"
	"\tfloat3 scene = tex2D(tex2, clamp(suv + so, float2(0.002, 0.002), float2(0.998, 0.998))).rgb;\n"
	"\tfloat3 r = reflect(d, n);\n"
	"\tfloat el = asin(clamp(r.z, 0.02, 1.0)) / 1.5707963;\n"
	"\tfloat az = atan2(r.y, r.x) / 6.2831853 + 0.5;\n"
	"\tfloat3 refl = tex2D(tex3, float2(az, el)).rgb;\n"
	"\tfloat4 f = tex2D(tex1, float2(0.5 + 0.5 * min(length(e.xy), 1.0), 0.5));\n"
	"\tfloat3 fn = normalize(f.rgb * 2.0 - 1.0);\n"
	"\tfloat glitter = pow(max(dot(fn, h2u[2].xyz), 0.0), 8.0);\n"
	"\tfloat3 c = f.a * refl + (1.0 - f.a) * scene + glitter * h2u[3].r;\n"
	"\tfloat fogged = saturate((along - h2u[7].x) / max(h2u[7].y - h2u[7].x, 0.001)) * h2u[7].z;\n"
	"\treturn float4(lerp(c, h2u[10].rgb, fogged), 1.0);\n";

static __attribute__((unused)) char const vita_h2_water_pool_body[] =
	"\tfloat2 uv = (wpos.xy - h2u[0].xy) / max(h2u[0].zw, float2(1.0, 1.0));\n"
	"\tfloat2 ndc = uv * 2.0 - 1.0;\n"
	"\tndc.y = -ndc.y;\n"
	"\tfloat3 d = normalize(h2u[4].xyz + h2u[5].xyz * (ndc.x * h2u[4].w * h2u[5].w) + h2u[6].xyz * (ndc.y * h2u[4].w));\n"
	"\tfloat3 e = -d;\n"
	"\tfloat along = h2u[6].w / max(-d.z, 0.001);\n"
	"\tfloat2 world = float2(h2u[2].w, h2u[3].g) + d.xy * along;\n"
	"\tfloat2 tc = world * h2u[1].z;\n"
	"\tfloat4 b = tex2D(tex0, tc);\n"
	"\tfloat3 bn = (b.rgb * 255.0 - 128.0) / 127.0;\n"
	"\tbn.xy *= h2u[9].x;\n"
	"\tbn.xy /= 1.0 + along * h2u[3].b;\n"
	"\tfloat3 n = normalize(bn);\n"
	"\tfloat2 o = n.xy * h2u[1].x;\n"
	"\tfloat2 suv = uv, so = o;\n"
	"\tsuv.y = 1.0 - suv.y; so.y = -so.y;\n"
	"\tfloat3 scene = tex2D(tex2, clamp(suv + so, float2(0.002, 0.002), float2(0.998, 0.998))).rgb;\n"
	"\tfloat3 refl = tex2D(tex3, uv + float2(o.x, -o.y)).rgb;\n"
	"\tfloat4 f = tex2D(tex1, float2(0.5 + 0.5 * min(length(e.xy), 1.0), 0.5));\n"
	"\tfloat3 fn = normalize(f.rgb * 2.0 - 1.0);\n"
	"\tfloat glitter = pow(max(dot(fn, h2u[2].xyz), 0.0), 8.0);\n"
	"\tfloat3 c = f.a * refl + (1.0 - f.a) * scene + glitter * h2u[3].r;\n"
	"\tfloat fogged = saturate((along - h2u[7].x) / max(h2u[7].y - h2u[7].x, 0.001)) * h2u[7].z;\n"
	"\treturn float4(lerp(c, h2u[10].rgb, fogged), 1.0);\n";

#endif
