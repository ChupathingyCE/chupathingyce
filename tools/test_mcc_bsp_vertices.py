"""MCC BSP vertices MCC draws load here too; only broken positions refuse (#1157).

Double Take from the MCC Workshop failed with "invalid BSP vertex payload":
the check before the engine's vertex compressor refused a whole BSP for one
direction vector a little over unit length or one value that isn't a number,
both of which MCC draws. The converter now repairs those in place and
refuses only a position that isn't a number. These tests run the converter's
own functions, then the engine's own compressor (with its asserts live) on
what they produce.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys

import pytest
from harness import function

ROOT = Path(__file__).resolve().parents[1]
GEOMETRY = ROOT / "port/linux/game/mcc_geometry.c"
RASTERIZER = ROOT / "source/rasterizer/rasterizer_geometry.c"

TEST = r'''
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef float real;
typedef struct { real i, j, k; } real_vector3d;
#define PIN(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define fast_ftol(x) ((int32_t)(x))
static int asserts;
#define match_assert(file, line, expr) if (!(expr)) { ++asserts; }
real_vector3d uncompress_int32_to_real_vector3d(uint32_t compressed);
/* UNITS */

static int compresses(float const *v) {
    real_vector3d r;
    int before = asserts;
    r.i = v[0]; r.j = v[1]; r.k = v[2];
    (void)compress_real_vector3d_to_int32_clamp(&r);
    return asserts == before;
}
static int in_range(float const *v) {
    int j;
    /* (a rebuilt frame may round a hair over 1: the compressor takes that) */
    for (j = 0; j < 3; j++) if (!isfinite(v[j]) || fabsf(v[j]) > 1.0001f) return 0;
    return compresses(v);
}
static float length(float const *v) { return sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); }
static float dot(float const *a, float const *b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static void good_vertex(float *v, float x) {
    float values[14] = {0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0.25f, 0.75f};
    memcpy(v, values, sizeof(values));
    v[0] = x; v[1] = -x; v[2] = 2 * x;
}

int main(void) {
    float environment[8 * 14], lightmap[4 * 5], copy[8 * 14], lightmap_copy[4 * 5];
    float nan = (float)NAN, inf = (float)INFINITY;
    uint32_t repaired, bad, i;
    int j;

    /* a sound BSP is left exactly as it was */
    for (i = 0; i < 8; i++) good_vertex(environment + i * 14, (float)i);
    for (i = 0; i < 4; i++) { lightmap[i*5] = 0.6f; lightmap[i*5+1] = 0.8f; lightmap[i*5+2] = 0; lightmap[i*5+3] = 0.5f; lightmap[i*5+4] = 0.5f; }
    memcpy(copy, environment, sizeof(copy)); memcpy(lightmap_copy, lightmap, sizeof(lightmap));
    repaired = 0;
    CHECK(mg_bsp_vertices(environment, 8, lightmap, 4, &repaired, &bad));
    CHECK(repaired == 0);
    CHECK(!memcmp(copy, environment, sizeof(copy)) && !memcmp(lightmap_copy, lightmap, sizeof(lightmap)));

    /* what Double Take has: directions a little over unit length, values that
       aren't numbers in its tangent frames, UVs and lightmap directions */
    environment[0*14+3] = 0; environment[0*14+4] = 0.0f; environment[0*14+5] = 1.02f;
    environment[1*14+6] = nan; environment[1*14+9] = nan;
    environment[2*14+9] = 1.3f; environment[2*14+10] = 0.9f;
    environment[3*14+12] = inf; environment[3*14+13] = nan;
    environment[4*14+3] = nan;
    environment[5*14+6] = 3e38f; environment[5*14+7] = 3e38f;
    environment[6*14+3] = 0; environment[6*14+4] = 0; environment[6*14+5] = 0;
    environment[6*14+6] = -inf;
    lightmap[0] = 1.4f; lightmap[1] = 0.2f;
    lightmap[1*5+2] = nan;
    lightmap[2*5+3] = -inf;
    repaired = 0;
    CHECK(mg_bsp_vertices(environment, 8, lightmap, 4, &repaired, &bad));
    CHECK(repaired >= 9);
    for (i = 0; i < 8; i++) {
        float *v = environment + i * 14;
        for (j = 0; j < 3; j++) CHECK(v[j] == copy[i*14+j]);
        CHECK(in_range(v + 3) && in_range(v + 6) && in_range(v + 9));
        CHECK(isfinite(v[12]) && isfinite(v[13]));
    }
    CHECK(fabsf(length(environment + 3) - 1.0f) < 0.0001f && environment[5] > 0.999f);
    CHECK(environment[4*14+5] == 1.0f);
    /* a rebuilt tangent frame is unit and at right angles to the normal */
    for (i = 1; i < 7; i += 3) {
        float *v = environment + i * 14;
        CHECK(fabsf(length(v + 6) - 1.0f) < 0.001f && fabsf(length(v + 9) - 1.0f) < 0.001f);
        CHECK(fabsf(dot(v + 3, v + 9)) < 0.001f && fabsf(dot(v + 6, v + 9)) < 0.001f);
    }
    CHECK(fabsf(length(environment + 2*14 + 9) - 1.0f) < 0.0001f);
    CHECK(environment[3*14+12] == 0 && environment[3*14+13] == 0);
    CHECK(environment[7*14+12] == 0.25f);
    for (i = 0; i < 4; i++) CHECK(in_range(lightmap + i * 5) && isfinite(lightmap[i*5+3]) && isfinite(lightmap[i*5+4]));
    CHECK(fabsf(length(lightmap) - 1.0f) < 0.0001f);
    CHECK(lightmap[5] == 0 && lightmap[6] == 0 && lightmap[7] == 0);
    CHECK(lightmap[2*5+3] == 0);

    /* a position that isn't a number refuses the BSP, naming the vertex */
    for (i = 0; i < 8; i++) good_vertex(environment + i * 14, (float)i);
    environment[5*14+1] = nan;
    repaired = 0; bad = 99;
    CHECK(!mg_bsp_vertices(environment, 8, lightmap, 4, &repaired, &bad) && bad == 5);
    environment[5*14+1] = 0; environment[2*14+2] = -inf;
    CHECK(!mg_bsp_vertices(environment, 8, lightmap, 0, &repaired, &bad) && bad == 2);

    /* untrusted bytes: every pattern either refuses or leaves only what the
       engine's compressor takes without an assert */
    srand(1157);
    for (i = 0; i < 20000; i++) {
        unsigned char *bytes = (unsigned char *)environment;
        size_t k;
        int ok;
        for (k = 0; k < sizeof(environment); k++) bytes[k] = (unsigned char)rand();
        bytes = (unsigned char *)lightmap;
        for (k = 0; k < sizeof(lightmap); k++) bytes[k] = (unsigned char)rand();
        /* keep positions sound in most rounds, so the repairs are reached */
        if (i % 4) for (k = 0; k < 8; k++) { environment[k*14] = 1; environment[k*14+1] = 2; environment[k*14+2] = 3; }
        repaired = 0;
        ok = mg_bsp_vertices(environment, 8, lightmap, 4, &repaired, &bad);
        if (i % 4) CHECK(ok);
        if (!ok) { CHECK(bad < 8); continue; }
        for (k = 0; k < 8; k++) {
            float *v = environment + k * 14;
            CHECK(in_range(v + 3) && in_range(v + 6) && in_range(v + 9) && isfinite(v[12]) && isfinite(v[13]));
        }
        for (k = 0; k < 4; k++) CHECK(in_range(lightmap + k * 5) && isfinite(lightmap[k*5+3]) && isfinite(lightmap[k*5+4]));
    }
    CHECK(asserts == 0);
    printf("ok\n");
    return 0;
}
'''


def test_bsp_vertices_repaired_for_the_engine_compressor(tmp_path):
    clang = shutil.which("clang")
    if not clang or sys.platform == "win32":
        pytest.skip("clang on a POSIX host required")
    geometry = GEOMETRY.read_text()
    rasterizer = RASTERIZER.read_text()
    # the compressor's shifts are for the 32-bit long it was written for
    units = [re.sub(r"\blong\b", "int32_t", function(rasterizer, name)).replace("unsigned int32_t", "uint32_t")
             for name in ("uncompress_int32_to_real_vector3d", "compress_real_vector3d_to_int32_clamp")]
    units += [function(geometry, name) for name in ("mg_direction", "mg_perpendicular", "mg_bsp_vertices")]
    source = tmp_path / "test.c"
    source.write_text(TEST.replace("/* UNITS */", "\n".join(units)))
    exe = tmp_path / "test"
    result = subprocess.run([clang, "-std=gnu89", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                             str(source), "-o", str(exe), "-lm"], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
    assert run.returncode == 0 and run.stdout.strip() == "ok", run.stdout + run.stderr


def test_bsp_material_repairs_before_compressing():
    body = function(GEOMETRY.read_text(), "mg_bsp_material")
    repair = body.index("mg_bsp_vertices(")
    assert repair < body.index("rasterizer_geometry_compress_vertices(")
    # the old all-or-nothing range check no longer guards BSP vertices
    assert "mg_float_vertices" not in body
    # the refusal names its reason in debug.txt
    assert re.search(r'error\(_error_silent,"MCC geometry: BSP vertex %lu', body)


def test_failed_mcc_load_returns_to_the_menus():
    """A refused BSP fails scenario_load quietly and main_new_map goes back to
    the menus with a message (mcc_main_load_failed), not the modal error."""
    scenario = (ROOT / "source/scenario/scenario.c").read_text()
    assert re.search(r"mcc_cache_tags_loaded\(\) && old_structure_bsp_index == NONE\)\s*\{\s*"
                     r"error\(_error_silent, \"mcc: failed to load initial structure bsp", scenario)
    main = function((ROOT / "source/main/main.c").read_text(), "main_new_map")
    assert main.index("mcc_main_load_failed(") < main.index('"game_load() failed."')
