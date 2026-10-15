"""CPU-only transition tests of the actual Metal V2 cache implementations.

Extract small C functions rather than duplicate the state dependency lists.
The same conversions compiled without V2 are the byte-for-byte reference.
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
    def run_c(self, source):
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            source_file = Path(directory) / "probe.c"
            binary = Path(directory) / "probe"
            source_file.write_text(source)
            subprocess.run([compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-function", "-Wno-unused-variable",
                            "-fsanitize=address,undefined", "-I", str(METAL),
                            str(source_file), "-o", str(binary)], check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, capture_output=True)

    def test_live_state_conversion_and_declaration_reuse(self):
        source = (METAL / "renderer/d3d8_device.c").read_text()
        conversions = ["gpu_compare", "gpu_stencil_operation", "gpu_blend_factor",
                       "gpu_blend_operation", "gpu_filter", "gpu_address", "sampler_state_fill",
                       "draw_depth_fill", "draw_blend_fill", "draw_raster_fill", "attribute_format",
                       "shader_combiner_matches", "pixel_shader_base_key"]
        bodies = "\n".join(function(source, name) for name in conversions)
        extras = "\n".join(function(source, name) for name in ["prepared_fragment_shader", "declaration_plan"])
        tokens = sorted(set(re.findall(r"\bD3D[A-Z][A-Za-z0-9_]*\b", bodies + extras)) - {"D3DTSS_MAXSTAGES"})
        enums = "enum { " + ", ".join(tokens) + ", ENUM_COUNT };\n"
        reference = bodies
        for name in sorted(conversions, key=len, reverse=True):
            reference = re.sub(r"\b" + name + r"\b", "ref_" + name, reference)
        xgpu = (METAL / "renderer/xgpu.h").read_text()
        start = xgpu.index("struct nv2a_pixel_shader_key\n{")
        key_type = xgpu[start:xgpu.index("\n};", start) + 3].replace("D3DRS_PS_MAX", "256")
        common = COMMON.replace("/* KEY_TYPE */", key_type)
        self.run_c(common + enums + reference + "\n#define HALO_MACOS_METAL_V2 1\n" + bodies + extras + STATE_MAIN)

    def test_attribute_table_and_encoder_transitions(self):
        self.run_c(COMMON.replace("/* KEY_TYPE */", "struct nv2a_pixel_shader_key { int unused; }; ") + ATTRIBUTE_MAIN)


if __name__ == "__main__":
    unittest.main()
