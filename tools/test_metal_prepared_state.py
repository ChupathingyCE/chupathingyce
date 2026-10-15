"""CPU-only transition tests of the actual Metal V2 cache implementations.

Compile the prepared module as a real separate translation unit and exercise
its public interface through the frontend adapters. Extract only V1 reference
conversions and existing declaration helpers; never extract the module bodies.
Xbox enums are distinct synthetic values: this tests observation/invalidation,
not the already established Xbox-to-GPU enum mapping or real GPU lifetime.
"""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
METAL = ROOT / "port/macos/metal"


def function(source, name):
    # Find the declaration start without accidentally including a prior function.
    start = source.rfind("\nstatic ", 0, source.index(name)) + 1
    opening = source.index("{", source.index(name))
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


COMMON = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "metal_attribute_table.h"
#include "metal_state_cache.h"
#include "renderer/draw_profile.h"
typedef uint32_t DWORD;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define D3DTSS_MAXSTAGES 4
#define XGPU_VERTEX_ATTRIBUTE_COUNT 16
static DWORD D3D__RenderState[512], D3D__TextureState[4][512];
static struct { int clip_y_flip; } shader_dialect;
static struct { int vertex_bgra, occlusion_mode; } device_capabilities;
static struct { int visibility_test_active; } device;
static struct draw_profile draw_profile;
static float dword_to_float(DWORD bits) { float f; memcpy(&f, &bits, 4); return f; }
static long config_integer(const char *name) { (void)name; return 4; }
/* KEY_TYPE */
static unsigned lookups;
static int fail_shader;
static gpu_shader fragment_shader_get(const struct nv2a_pixel_shader_key *key)
{
    (void)key;
    lookups++;
    return fail_shader ? 0 : lookups;
}
struct vertex_element { unsigned char reg, stream, type, bytes; unsigned short offset; };
struct vertex_shader_object {
    struct vertex_element elements[16];
    unsigned long element_count, packed_mask;
    struct gpu_vertex_attribute attribute_plan[16];
    uint32_t plan_streams, plan_constants;
    BOOL plan_valid, plan_bgra;
};
'''

STATE_MAIN = r'''
struct prepared_outputs {
    struct gpu_depth_stencil_state depth;
    struct gpu_blend_state blend;
    struct gpu_raster_state raster;
    struct gpu_sampler_state samplers[GPU_STAGE_COUNT];
    struct nv2a_pixel_shader_key key;
};
static void prepare_context(struct metal_v2_prepared *cache, struct draw_profile *profile,
                            struct prepared_outputs *out)
{
    const DWORD *stages[GPU_STAGE_COUNT];
    memset(out, 0, sizeof(*out));
    metal_v2_prepared_depth(cache, profile, D3D__RenderState, 1, &out->depth);
    metal_v2_prepared_blend(cache, profile, D3D__RenderState, &out->blend);
    metal_v2_prepared_raster(cache, profile, D3D__RenderState, 0, &out->raster);
    for (unsigned stage = 0; stage < GPU_STAGE_COUNT; stage++) {
        stages[stage] = D3D__TextureState[stage];
        metal_v2_prepared_sampler(cache, profile, stage, stages[stage], 1, 0, 4,
                                  &out->samplers[stage]);
    }
    (void)stages;
}
static void test_reset_and_ownership(void)
{
    struct metal_v2_prepared first = {0}, second = {0};
    struct draw_profile profile = {.enabled = 1};
    struct prepared_outputs expected, actual;
    prepare_context(&first, &profile, &expected);
    prepare_context(&second, &profile, &actual);
    assert(!memcmp(&actual, &expected, sizeof(actual)));
    memset(&actual, 0xa5, sizeof(actual));
    prepare_context(&first, &profile, &actual);
    assert(!memcmp(&actual, &expected, sizeof(actual)));
    unsigned before = lookups;
    gpu_shader a = metal_v2_prepared_fragment(&first, &profile, &actual.key, fragment_shader_get);
    gpu_shader b = metal_v2_prepared_fragment(&second, &profile, &actual.key, fragment_shader_get);
    assert(a != b && lookups == before + 2);
    assert(metal_v2_prepared_fragment(&first, &profile, &actual.key, fragment_shader_get) == a);
    unsigned long misses[DRAW_CACHE_DOMAINS];
    memcpy(misses, profile.cache_misses, sizeof(misses));
    metal_v2_prepared_reset(&first);
    prepare_context(&first, &profile, &actual);
    assert(!memcmp(&actual, &expected, sizeof(actual)));
    const unsigned domains[] = {DRAW_CACHE_DEPTH, DRAW_CACHE_BLEND, DRAW_CACHE_RASTER};
    for (unsigned i = 0; i < sizeof(domains)/sizeof(domains[0]); i++)
        assert(profile.cache_misses[domains[i]] == misses[domains[i]] + 1);
    assert(profile.cache_misses[DRAW_CACHE_SAMPLER] == misses[DRAW_CACHE_SAMPLER] + GPU_STAGE_COUNT);
    assert(metal_v2_prepared_fragment(&first, &profile, &actual.key, fragment_shader_get) != a);
    assert(metal_v2_prepared_fragment(&second, &profile, &actual.key, fragment_shader_get) == b);
    assert(lookups == before + 3);
    memcpy(misses, profile.cache_misses, sizeof(misses));
    prepare_context(&second, &profile, &actual);
    assert(!memcmp(misses, profile.cache_misses, sizeof(misses))); /* reset was not global */
    profile.enabled = 0;
    struct draw_profile saved = profile;
    metal_v2_prepared_reset(&first);
    prepare_context(&first, &profile, &actual);
    assert(!memcmp(&actual, &expected, sizeof(actual)));
    assert(!memcmp(&profile, &saved, sizeof(profile)));
}
static void test_sampler_policy(void)
{
    struct metal_v2_prepared cache = {0};
    struct draw_profile profile = {.enabled = 1};
    struct gpu_sampler_state sampler;
    DWORD words[512] = {0};
    words[D3DTSS_MINFILTER] = words[D3DTSS_MIPFILTER] = D3DTEXF_LINEAR;
    metal_v2_prepared_sampler(&cache, &profile, 0, words, 1, 0, 1, &sampler);
    assert(sampler.min_filter == GPU_FILTER_LINEAR);
    metal_v2_prepared_sampler(&cache, &profile, 0, words, 1, 0, 16, &sampler);
    assert(sampler.min_filter == GPU_FILTER_ANISOTROPIC && sampler.max_anisotropy == 16);
    assert(profile.cache_misses[DRAW_CACHE_SAMPLER] == 2);
    metal_v2_prepared_sampler(&cache, &profile, 0, words, 1, 0, 16, &sampler);
    assert(profile.cache_hits[DRAW_CACHE_SAMPLER] == 1);
    metal_v2_prepared_sampler(&cache, &profile, 0, words, 1, 0, 1, &sampler);
    assert(sampler.min_filter == GPU_FILTER_LINEAR && sampler.max_anisotropy == 0);
    assert(profile.cache_misses[DRAW_CACHE_SAMPLER] == 3);
}
static void compare_key(void)
{
    struct nv2a_pixel_shader_key actual, expected;
    pixel_shader_base_key(&actual);
    ref_pixel_shader_base_key(&expected);
    assert(!memcmp(&actual, &expected, sizeof(actual)));
    /* Live resource/lighting specialization must never contaminate the base. */
    actual.coverage_alpha = 3;
    actual.per_pixel_lighting = 2;
    actual.sampler_type[0] = 4;
    pixel_shader_base_key(&actual);
    assert(!memcmp(&actual, &expected, sizeof(actual)));
}
static void compare_domains(BOOL has_depth)
{
    struct gpu_depth_stencil_state depth, expected_depth;
    struct gpu_blend_state blend, expected_blend;
    struct gpu_raster_state raster, expected_raster;
    draw_depth_fill(has_depth, &depth); ref_draw_depth_fill(has_depth, &expected_depth);
    draw_blend_fill(&blend); ref_draw_blend_fill(&expected_blend);
    draw_raster_fill(&raster); ref_draw_raster_fill(&expected_raster);
    assert(!memcmp(&depth, &expected_depth, sizeof(depth)));
    assert(!memcmp(&blend, &expected_blend, sizeof(blend)));
    assert(!memcmp(&raster, &expected_raster, sizeof(raster)));
    compare_key();
    /* Callers' HUD/depth-clamp edits must not poison the saved base state. */
    memset(&depth, 0xa5, sizeof(depth));
    memset(&blend, 0xa5, sizeof(blend));
    memset(&raster, 0xa5, sizeof(raster));
}
static void compare_sampler(int stage, BOOL mips, BOOL hires)
{
    struct gpu_sampler_state actual, expected;
    sampler_state_fill(stage, mips, hires, &actual);
    ref_sampler_state_fill(stage, mips, hires, &expected);
    assert(!memcmp(&actual, &expected, sizeof(actual)));
    compare_key();
}
int main(void)
{
    draw_profile.enabled = 1;
    compare_domains(1);
    compare_domains(1);
    assert(draw_profile.cache_hits[DRAW_CACHE_DEPTH] == 1);
    assert(draw_profile.cache_hits[DRAW_CACHE_BLEND] == 1);
    assert(draw_profile.cache_hits[DRAW_CACHE_RASTER] == 1);
    /* Dynamic encoder values stay live without invalidating converted bases. */
    unsigned long depth_misses = draw_profile.cache_misses[DRAW_CACHE_DEPTH];
    unsigned long blend_misses = draw_profile.cache_misses[DRAW_CACHE_BLEND];
    for (unsigned transition = 0; transition < 4; transition++) {
        DWORD value = transition & 1 ? 0xffffffffu : 0;
        D3D__RenderState[D3DRS_STENCILREF] = value;
        D3D__RenderState[D3DRS_BLENDCOLOR] = value;
        struct gpu_depth_stencil_state depth;
        struct gpu_blend_state blend;
        draw_depth_fill(1, &depth);
        draw_blend_fill(&blend);
        assert(depth.stencil_reference == value);
        assert(blend.color == value);
        assert(draw_profile.cache_misses[DRAW_CACHE_DEPTH] == depth_misses);
        assert(draw_profile.cache_misses[DRAW_CACHE_BLEND] == blend_misses);
        /* Edits to returned copies cannot poison subsequent results. */
        memset(&depth, 0xa5, sizeof(depth));
        memset(&blend, 0xa5, sizeof(blend));
        draw_depth_fill(1, &depth);
        draw_blend_fill(&blend);
        assert(depth.stencil_reference == value);
        assert(blend.color == value);
    }
    D3D__RenderState[D3DRS_STENCILREF] = 0;
    D3D__RenderState[D3DRS_BLENDCOLOR] = 0;
    D3D__RenderState[D3DRS_ZENABLE] = 1;
    compare_domains(1);
    assert(draw_profile.cache_misses[DRAW_CACHE_DEPTH] == 2);
    assert(draw_profile.cache_misses[DRAW_CACHE_BLEND] == 1);
    assert(draw_profile.cache_misses[DRAW_CACHE_RASTER] == 1);
    /* Every live render input: direct write, draw, restore, draw. */
    DWORD values[] = {0, 1, 0xffffffffu, 0x3f800000u, 0x80000000u, 0x7fc01234u};
    for (unsigned state = 0; state < ENUM_COUNT; state++) {
        for (unsigned v = 0; v < sizeof(values)/sizeof(values[0]); v++) {
            D3D__RenderState[state] = values[v];
            compare_domains(1);
            compare_domains(1);
            compare_domains(0);
            D3D__RenderState[state] = 0;
            compare_domains(1);
        }
    }
    /* Exercise named enum branches as well as arbitrary bit patterns. */
    for (unsigned value = 0; value < ENUM_COUNT; value++) {
        for (unsigned state = 0; state < ENUM_COUNT; state++) D3D__RenderState[state] = value;
        compare_domains(1);
        shader_dialect.clip_y_flip ^= 1;
        compare_domains(1);
    }
    for (int stage = 0; stage < 4; stage++) {
        for (unsigned state = 0; state < ENUM_COUNT; state++) {
            for (unsigned value = 0; value < ENUM_COUNT; value++) {
                D3D__TextureState[stage][state] = value;
                for (int mips = 0; mips < 2; mips++) for (int hires = 0; hires < 2; hires++) {
                    compare_sampler(stage, mips, hires);
                    compare_sampler(stage, mips, hires);
                }
                D3D__TextureState[stage][state] = 0;
                compare_sampler(stage, 0, 0);
            }
        }
    }
    for (unsigned word = 0; word < 256; word++) {
        D3D__RenderState[word] ^= 0x12345678;
        compare_key();
        D3D__RenderState[word] ^= 0x12345678;
        compare_key();
    }
    device_capabilities.occlusion_mode = GPU_OCCLUSION_SHADER_COUNTER;
    device.visibility_test_active = 1;
    compare_key();
    device.visibility_test_active = 0;
    compare_key();
    unsigned long key_misses = draw_profile.cache_misses[DRAW_CACHE_SHADER_KEY];
    D3D__RenderState[D3DRS_PSCONSTANT0_0]++;
    compare_key();
    assert(draw_profile.cache_misses[DRAW_CACHE_SHADER_KEY] == key_misses);
    struct nv2a_pixel_shader_key key = {0};
    gpu_shader first = prepared_fragment_shader(&key);
    assert(first && prepared_fragment_shader(&key) == first && lookups == 1);
    key.combiner_state[255]++;
    assert(prepared_fragment_shader(&key) != first && lookups == 2);
    key.combiner_state[255]--;
    assert(prepared_fragment_shader(&key) && lookups == 3);
    fail_shader = 1; key.combiner_state[0]++;
    assert(!prepared_fragment_shader(&key));
    assert(!prepared_fragment_shader(&key) && lookups == 5);
    fail_shader = 0;
    assert(prepared_fragment_shader(&key) && lookups == 6);
    struct nv2a_pixel_shader_key variants[6];
    for (unsigned i = 0; i < 6; i++) variants[i] = key;
    variants[0].sampler_type[3]++;
    variants[1].coverage_alpha++;
    variants[2].per_pixel_lighting++;
    variants[3].count_samples++;
    variants[4].texture_modes++;
    variants[5].color_sign[2]++;
    for (unsigned i = 0; i < 6; i++) {
        unsigned before = lookups;
        gpu_shader shader = prepared_fragment_shader(&variants[i]);
        assert(shader && lookups == before + 1);
        assert(prepared_fragment_shader(&variants[i]) == shader && lookups == before + 1);
    }
    test_reset_and_ownership();
    test_sampler_policy();

    struct vertex_shader_object declaration = {0}, other = {0};
    declaration.element_count = 3;
    declaration.packed_mask = 1u << 7;
    declaration.elements[0] = (struct vertex_element){0, 0, D3DVSDT_FLOAT3, 12, 0};
    declaration.elements[1] = (struct vertex_element){2, 1, D3DVSDT_D3DCOLOR, 4, 12};
    declaration.elements[2] = (struct vertex_element){4, 1, D3DVSDT_NONE, 0, 0};
    for (int bgra = 0; bgra < 2; bgra++) {
        device_capabilities.vertex_bgra = bgra;
        for (uint32_t streams = 0; streams < 4; streams++) {
            declaration_plan(&declaration, streams);
            assert(declaration.attribute_plan[7].format == GPU_ATTRIBUTE_NORMPACKED3);
            assert(declaration.attribute_plan[4].stream == GPU_STREAM_CONSTANT);
            assert(declaration.attribute_plan[0].stream == ((streams & 1) ? 0 : GPU_STREAM_CONSTANT));
            assert(declaration.attribute_plan[2].stream == ((streams & 2) ? 1 : GPU_STREAM_CONSTANT));
            if (streams & 2) assert(declaration.attribute_plan[2].format == (bgra ? GPU_ATTRIBUTE_BGRA8 : GPU_ATTRIBUTE_RGBA8));
            assert(declaration.plan_constants == (0xffffu & ~((streams & 1 ? 1u : 0) | (streams & 2 ? 4u : 0))));
            unsigned long hits = draw_profile.cache_hits[DRAW_CACHE_DECLARATION];
            declaration_plan(&declaration, streams);
            assert(draw_profile.cache_hits[DRAW_CACHE_DECLARATION] == hits + 1);
            declaration_plan(&other, streams); /* another declaration cannot inherit the plan */
            assert(other.plan_constants == 0xffffu);
        }
    }
    return 0;
}
'''

ATTRIBUTE_MAIN = r'''
/* The old bind_attributes table construction, without Objective-C binding. */
static struct metal_attribute_table reference(const struct gpu_draw *draw, uint32_t records)
{
    struct metal_attribute_table table = {0};
    for (unsigned i = 0; i < GPU_ATTRIBUTE_COUNT; i++) {
        const struct gpu_vertex_attribute *a = &draw->attributes[i];
        struct metal_attribute *e = &table.entries[i];
        e->format = a->format;
        if (!(records & (1u << i))) {
            e->stream = GPU_STREAM_CONSTANT;
            memcpy(table.constants[i], draw->constant_values[i], sizeof(table.constants[i]));
        } else {
            e->stream = a->stream;
            e->offset = draw->streams[a->stream].offset + a->offset;
            e->stride = draw->streams[a->stream].stride ? draw->streams[a->stream].stride : metal_attribute_size(a->format);
        }
    }
    return table;
}
static void check(struct metal_attribute_cache *cache, struct gpu_draw *draw, uint32_t records)
{
    struct metal_attribute_table expected = reference(draw, records);
    int changed = !!memcmp(&cache->table, &expected, sizeof(expected));
    assert(metal_attribute_table_prepare(cache, draw, records) == changed);
    assert(!memcmp(&cache->table, &expected, sizeof(expected)));
    assert(!metal_attribute_table_prepare(cache, draw, records));
}
int main(void)
{
    struct metal_attribute_cache cache = {0};
    struct gpu_draw draw = {0};
    struct metal_state_cache state;
    uint64_t revision = 0;
    for (unsigned i = 0; i < GPU_ATTRIBUTE_COUNT; i++) {
        draw.attributes[i].stream = GPU_STREAM_CONSTANT;
        draw.attributes[i].format = GPU_ATTRIBUTE_FLOAT4;
        draw.constant_values[i][3] = 1;
    }
    check(&cache, &draw, 0);
    for (unsigned i = 0; i < GPU_ATTRIBUTE_COUNT; i++) {
        draw.attributes[i].stream = i;
        draw.attributes[i].offset = 12;
        draw.attributes[i].format = GPU_ATTRIBUTE_SHORT3;
        draw.streams[i].buffer = 1;
        draw.streams[i].offset = i * 64;
        check(&cache, &draw, 1u << i);
        draw.streams[i].offset++;
        check(&cache, &draw, 1u << i); /* base vertex / mirror offset */
        draw.streams[i].stride = 32;
        check(&cache, &draw, 1u << i);
        draw.streams[i].buffer++; /* rename: binding changes, table need not */
        assert(!metal_attribute_table_prepare(&cache, &draw, 1u << i));
        check(&cache, &draw, 0); /* lost record means a constant */
        draw.constant_values[i][0]++;
        check(&cache, &draw, 0);
        draw.attributes[i].format = GPU_ATTRIBUTE_NORMPACKED3;
        check(&cache, &draw, 0);
    }
    /* Shared streams, unused input changes and constant/streamed transitions.
       Compare every effective byte against the uncached construction. */
    for (unsigned i = 0; i < GPU_ATTRIBUTE_COUNT; i++) {
        draw.attributes[i].stream = 0;
        draw.attributes[i].format = GPU_ATTRIBUTE_FLOAT4;
        draw.attributes[i].offset = i * 4;
    }
    check(&cache, &draw, 0xffffu);
    for (unsigned i = 0; i < GPU_ATTRIBUTE_COUNT; i++) {
        draw.constant_values[i][0] += 1; /* ignored while streamed */
        draw.streams[15].offset++; /* unused stream */
        draw.streams[0].buffer++; /* rename is not part of table bytes */
        assert(!metal_attribute_table_prepare(&cache, &draw, 0xffffu));
    }
    draw.streams[0].stride = 0; /* format-derived stride */
    draw.streams[0].offset++;
    check(&cache, &draw, 0xffffu);
    check(&cache, &draw, 0); /* all changed constants must become visible */
    check(&cache, &draw, 0xffffu);
    uint32_t seed = 0x74696765u;
    for (unsigned step = 0; step < 4096; step++) {
        seed = seed * 1664525u + 1013904223u; /* Intentional PRNG wrap. */
        unsigned slot = (seed >> 16) & 15;
        unsigned stream = (seed >> 20) & 15;
        draw.attributes[slot].stream = stream;
        draw.attributes[slot].offset = (seed >> 8) & 255;
        draw.attributes[slot].format = step & 1 ? GPU_ATTRIBUTE_FLOAT3 : GPU_ATTRIBUTE_SHORT3;
        draw.streams[stream].offset = seed & 0xffff;
        draw.streams[stream].stride = step & 2 ? 0 : 32;
        draw.constant_values[slot][1] = (float)step;
        check(&cache, &draw, seed & 0xffff);
    }
    metal_state_initialize(&state, 1);
    assert(metal_state_value(&state, METAL_STATE_ATTRIBUTE_TABLE, &revision, sizeof(revision)));
    assert(!metal_state_value(&state, METAL_STATE_ATTRIBUTE_TABLE, &revision, sizeof(revision)));
    revision++;
    assert(metal_state_value(&state, METAL_STATE_ATTRIBUTE_TABLE, &revision, sizeof(revision)));
    metal_state_reset(&state); /* new pass even with identical inputs */
    assert(metal_state_value(&state, METAL_STATE_ATTRIBUTE_TABLE, &revision, sizeof(revision)));
    metal_state_clear_quad(&state);
    assert(metal_state_value(&state, METAL_STATE_ATTRIBUTE_TABLE, &revision, sizeof(revision)));
    state.enabled = 0; /* correctness/reference mode always binds */
    for (int i = 0; i < 2; i++) assert(metal_state_value(&state, METAL_STATE_ATTRIBUTE_TABLE, &revision, sizeof(revision)));
    return 0;
}
'''


class PreparedStateTests(unittest.TestCase):
    def run_c(self, source, prelude=None):
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            source_file = Path(directory) / "probe.c"
            binary = Path(directory) / "probe"
            source_file.write_text(source)
            inputs = [str(source_file)]
            if prelude is not None:
                prelude_file = Path(directory) / "fixture.h"
                prelude_file.write_text(prelude)
                inputs += [str(METAL / "renderer/metal_v2_prepared.c"),
                           "-include", str(prelude_file), "-DHALO_MACOS_METAL_V2=1"]
            subprocess.run([compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-function", "-Wno-unused-variable",
                            "-fsanitize=address,undefined", "-I", str(METAL),
                            *inputs, "-o", str(binary)], check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, capture_output=True)

    def test_live_state_conversion_and_declaration_reuse(self):
        source = (METAL / "renderer/d3d8_device.c").read_text()
        conversions = ["gpu_compare", "gpu_stencil_operation", "gpu_blend_factor",
                       "gpu_blend_operation", "gpu_filter", "gpu_address", "sampler_state_fill",
                       "draw_depth_base_fill", "draw_depth_fill",
                       "draw_blend_base_fill", "draw_blend_fill", "draw_raster_fill", "attribute_format",
                       "pixel_shader_base_key"]
        bodies = "\n".join(function(source, name) for name in conversions)
        extras = "\n".join(function(source, name) for name in ["prepared_fragment_shader", "declaration_plan"])
        module = (METAL / "renderer/metal_v2_prepared.c").read_text()
        tokens = sorted(set(re.findall(r"\bD3D[A-Z][A-Za-z0-9_]*\b", bodies + extras + module))
                        - {"D3DTSS_MAXSTAGES", "D3DRS_PS_MAX"})
        enums = "enum { " + ", ".join(tokens) + ", ENUM_COUNT };\n"
        reference = bodies
        for name in sorted(conversions, key=len, reverse=True):
            reference = re.sub(r"\b" + name + r"\b", "ref_" + name, reference)
        xgpu = (METAL / "renderer/xgpu.h").read_text()
        start = xgpu.index("struct nv2a_pixel_shader_key\n{")
        key_type = xgpu[start:xgpu.index("\n};", start) + 3].replace("D3DRS_PS_MAX", "256")
        common = COMMON.replace("/* KEY_TYPE */", key_type)
        # Only replace platform declarations: the production header and .c
        # implementation are compiled unchanged. No game globals are linked.
        prelude = (common + enums + "\n#define D3DRS_PS_MAX 256\n"
                   "#define __HALO_LINUX_XGPU_H\n"
                   '#include "renderer/metal_v2_prepared.h"\n'
                   "static struct metal_v2_prepared prepared_state;\n")
        self.run_c("#undef HALO_MACOS_METAL_V2\n" + reference +
                   "\n#define HALO_MACOS_METAL_V2 1\n" + bodies + extras + STATE_MAIN, prelude)

    def test_attribute_table_and_encoder_transitions(self):
        self.run_c(COMMON.replace("/* KEY_TYPE */", "struct nv2a_pixel_shader_key { int unused; }; ") + ATTRIBUTE_MAIN)


if __name__ == "__main__":
    unittest.main()
