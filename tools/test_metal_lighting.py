"""Real NV2A model recognition under sanitizers; production lighting on the GPU."""
import math
import os
import platform
import re
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path
from unittest import TestCase, skipUnless

ROOT = Path(__file__).resolve().parents[1]
RENDERER = ROOT / "port/macos/metal/renderer"
C_DRIVER = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t DWORD;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define XGPU_VERTEX_CONSTANT_BIAS 96
#define __HALO_LINUX_XGPU_H
struct xgpu_text { char *buffer; unsigned long length, capacity; };
static void xgpu_text_append(struct xgpu_text *text, const char *format, ...) {
    if (!text->buffer) { text->capacity = 65536; text->buffer = calloc(1, text->capacity); assert(text->buffer); }
    va_list args; va_start(args, format);
    int count = vsnprintf(text->buffer + text->length, text->capacity - text->length, format, args);
    va_end(args); assert(count >= 0 && (unsigned long)count < text->capacity - text->length);
    text->length += (unsigned long)count;
}
@TYPES@
BOOL gl_vertex_shader_lighting(const DWORD *, unsigned long, struct nv2a_vertex_lighting *);
@FIELDS@
#include "model_lighting.inc"
#include "model_lighting_source.h"
static const DWORD data[] = {
#include "rasterizer_xbox_vertex_shaders_data.inc"
};
static const unsigned offsets[] = { @OFFSETS@ };
int main(int argc, char **argv) {
    (void)argv;
    if (argc > 1) {
        struct xgpu_text text = {0};
        puts("#include <metal_stdlib>\nusing namespace metal;\ntypedef float3 vec3; typedef float4 vec4;\n#define inversesqrt rsqrt");
        model_lighting_source(&text, TRUE, TRUE);
        puts(text.buffer); free(text.buffer);
        return 0;
    }
    struct nv2a_vertex_lighting lighting;
    assert(!nv2a_vertex_shader_lighting(NULL, 0, &lighting) && !lighting.lights);
    assert(!nv2a_vertex_shader_lighting(data, 137, &lighting) && !lighting.lights);
    unsigned recognized = 0;
    for (unsigned shader = 0; shader < sizeof(offsets)/sizeof(offsets[0]); shader++) {
        const DWORD *header = data + offsets[shader]/4;
        unsigned count = header[0] >> 16;
        assert(header >= data && header + 1 + 4*count <= data + sizeof(data)/sizeof(data[0]));
        BOOL supported = shader == 9 || shader == 10 || shader == 17 || shader == 27;
        BOOL result = nv2a_vertex_shader_lighting(header + 1, count, &lighting);
        struct nv2a_vertex_lighting reference;
        assert(result == gl_vertex_shader_lighting(header + 1, count, &reference));
        /* Other permutations can share this sum, but the game opts in only
           these four. Compare recognition to GL for every program anyway. */
        if (!supported) continue;
        assert(result);
        recognized++;
        assert(lighting.lights == (shader == 10 || shader == 17 ? 2 : 1));
        assert(lighting.normal_register == (shader == 27 ? 3 : 0));
        assert(lighting.normal_instruction == (shader == 27 ? 7 : shader == 9 ? 19 : 24));
        if (lighting.lights == 2) {
            assert(lighting.position_register == 10 && lighting.position_instruction == 19);
        }
        DWORD mutated[136*4]; memcpy(mutated, header + 1, count*16);
        /* Clobber xyz of the captured normal between its first use and the
           diffuse sum. Must fall back, not replace this different program. */
        unsigned clobber = (unsigned)lighting.normal_instruction + 1;
        mutated[clobber*4 + 1] = (mutated[clobber*4 + 1] & ~(15u << 21)) | (1u << 21);
        mutated[clobber*4 + 3] = (mutated[clobber*4 + 3] & ~((15u << 20) | (15u << 24))) |
            ((unsigned)lighting.normal_register << 20) | (14u << 24);
        assert(!nv2a_vertex_shader_lighting(mutated, count, &lighting) && !lighting.lights);
        /* Fuzz each microcode word without trusting the candidate's fields. */
        for (unsigned word = 0; word < count*4; word++) {
            memcpy(mutated, header + 1, count*16);
            mutated[word] ^= 0xFFFFFFFFu;
            assert(nv2a_vertex_shader_lighting(mutated, count, &lighting) ==
                gl_vertex_shader_lighting(mutated, count, &reference));
        }
    }
    assert(recognized == 4);
    return 0;
}
'''
GPU_DRIVER = r'''
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <assert.h>
#include <stdio.h>
int main(int argc, const char **argv) { @autoreleasepool {
    assert(argc == 4);
    id<MTLDevice> device = MTLCreateSystemDefaultDevice(); if (!device) return 77;
    NSError *error = nil;
    NSString *source = [NSString stringWithContentsOfFile:@(argv[1]) encoding:NSUTF8StringEncoding error:&error];
    assert(source);
    source = [source stringByAppendingString:@"\nkernel void test_lights(constant float4 *lights [[buffer(0)]],"
        "constant float4 *normals [[buffer(1)]], constant float4 *positions [[buffer(2)]],"
        "device float4 *out [[buffer(3)]], uint i [[thread_position_in_grid]]) {"
        "out[i] = float4(model_lighting(normals[i], positions[i].xyz, lights), 1.0); }"];
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    if (!library) fprintf(stderr, "%s\n", error.description.UTF8String);
    assert(library);
    id<MTLComputePipelineState> pso = [device newComputePipelineStateWithFunction:
        [library newFunctionWithName:@"test_lights"] error:&error];
    assert(pso);
    NSData *input = [NSData dataWithContentsOfFile:@(argv[2])];
    assert(input.length == (12 + 5 + 5)*16);
    const unsigned char *bytes = input.bytes;
    id<MTLBuffer> lights = [device newBufferWithBytes:bytes length:12*16 options:MTLResourceStorageModeShared];
    id<MTLBuffer> normals = [device newBufferWithBytes:bytes + 12*16 length:5*16 options:MTLResourceStorageModeShared];
    id<MTLBuffer> positions = [device newBufferWithBytes:bytes + 17*16 length:5*16 options:MTLResourceStorageModeShared];
    id<MTLBuffer> output = [device newBufferWithLength:5*16 options:MTLResourceStorageModeShared];
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLCommandBuffer> commands = [queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
    [encoder setComputePipelineState:pso];
    [encoder setBuffer:lights offset:0 atIndex:0]; [encoder setBuffer:normals offset:0 atIndex:1];
    [encoder setBuffer:positions offset:0 atIndex:2]; [encoder setBuffer:output offset:0 atIndex:3];
    [encoder dispatchThreads:MTLSizeMake(5,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
    [encoder endEncoding]; [commands commit]; [commands waitUntilCompleted];
    assert(commands.status == MTLCommandBufferStatusCompleted);
    assert([[NSData dataWithBytes:output.contents length:output.length] writeToFile:@(argv[3]) atomically:YES]);
    return 0;
}}
'''


def build_driver(folder):
    vsh = (RENDERER / "nv2a_vsh.c").read_text()
    fields = vsh[vsh.index("static unsigned long field("):vsh.index("/* output register addresses")]
    xgpu = (RENDERER / "xgpu.h").read_text()
    start = xgpu.index("struct nv2a_vertex_lighting\n")
    types = xgpu[start:xgpu.index("};", start) + 2]
    table = (ROOT / "source/rasterizer/xbox/rasterizer_xbox_vertex_shaders.c").read_text()
    offsets = re.findall(r"VERTEX_SHADER_ENTRY\((0x[0-9A-Fa-f]+),", table)
    assert len(offsets) == 67
    source, binary = folder / "lighting.c", folder / "lighting"
    source.write_text(C_DRIVER.replace("@FIELDS@", fields).replace("@TYPES@", types)
                      .replace("@OFFSETS@", ",".join(offsets)))
    gl = (ROOT / "port/linux/src/nv2a_vsh.c").read_text()
    decoder = gl[gl.index("struct operand_fields\n"):gl.index("static void operand(struct xgpu_text")]
    recognition = gl[gl.index("static BOOL xyz_unswizzled"):gl.index("/* ---------- translation */")]
    reference = folder / "reference.c"
    reference.write_text('#include <stdint.h>\n#include <stddef.h>\n'
                         'typedef uint32_t DWORD; typedef int BOOL;\n'
                         '#define TRUE 1\n#define FALSE 0\n#define XGPU_VERTEX_CONSTANT_BIAS 96\n'
                         + types + '\n' + fields + '\n' + decoder + '\n'
                         + recognition.replace('BOOL nv2a_vertex_shader_lighting(', 'BOOL gl_vertex_shader_lighting('))
    subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-I", str(RENDERER), "-I",
                    str(ROOT / "source/rasterizer/xbox"), str(source), str(reference), "-o", str(binary)],
                   check=True, timeout=120)
    return binary


class MetalLightingTests(TestCase):
    @skipUnless(shutil.which("clang"), "clang required")
    def test_real_program_recognition_and_corruption(self):
        with tempfile.TemporaryDirectory() as folder:
            subprocess.run([str(build_driver(Path(folder)))], check=True, timeout=60)

    @skipUnless(platform.system() == "Darwin", "Metal requires macOS")
    def test_production_lighting_equation_on_gpu(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            generator = build_driver(folder)
            msl = subprocess.run([str(generator), "source"], capture_output=True, text=True, check=True).stdout
            (folder / "lighting.metal").write_text(msl)
            source, binary = folder / "gpu.m", folder / "gpu"
            source.write_text(GPU_DRIVER)
            subprocess.run(["clang", "-fobjc-arc", "-Wall", "-Wextra", "-Werror", str(source),
                            "-framework", "Foundation", "-framework", "Metal", "-o", str(binary)],
                           check=True, timeout=120)
            lights = [(0, 0, .4, 0), (0, 0, 1, .25), (0, 0, -1, 1), (.2, .1, .4, 0),
                      (1, 0, 0, .0625), (-1, 0, 0, 1), (.1, .2, .05, 0),
                      (0, 0, -1, 0), (.3, .2, .1, 0), (-1, 0, 0, 0), (.2, .4, .6, 0), (.03, .04, .05, 0)]
            normals = [(0, 0, 1, 1), (1, 0, 0, 1), (0, 0, -1, 1), (0, 0, 0, 0), (.3, .6, .8, .5)]
            positions = [(0, 0, 0, 0), (0, 0, 1, 0), (0, 0, 0, 0), (100, 0, 0, 0), (.2, .3, .4, 0)]
            (folder / "input.bin").write_bytes(b"".join(struct.pack("<4f", *row) for row in lights + normals + positions))
            run = subprocess.run([str(binary), str(folder / "lighting.metal"), str(folder / "input.bin"),
                                  str(folder / "output.bin")], env={**os.environ, "MTL_DEBUG_LAYER": "1"},
                                 capture_output=True, text=True, timeout=120)
            if run.returncode == 77:
                self.skipTest("No Metal device")
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            gpu = list(struct.iter_unpack("<4f", (folder / "output.bin").read_bytes()))
            for normal, position, actual in zip(normals, positions, gpu, strict=True):
                n = [v * normal[3] / math.sqrt(max(sum(x*x for x in normal[:3]), 1e-24)) for v in normal[:3]]
                facing = n[2]
                expected = [lights[11][c] + max(facing, -facing * .4, 0) * lights[8][c]
                            + max(n[0], 0) * lights[10][c] for c in range(3)]
                for first in (1, 4):
                    to_light = [lights[first][c] - position[c] for c in range(3)]
                    distance2 = sum(x*x for x in to_light)
                    direction = [v / math.sqrt(max(distance2, 1e-12)) for v in to_light]
                    cone = sum(-direction[c]*lights[first+1][c] for c in range(3))
                    term = max(1 - distance2 * lights[first][3], 0)
                    term *= max(0, min(1, sum(direction[c]*n[c] for c in range(3))))
                    term *= max(0, min(1, cone*lights[first+1][3] + lights[first+2][3]))
                    expected = [expected[c] + term*lights[first+2][c] for c in range(3)]
                for value, target in zip(actual[:3], expected, strict=True):
                    self.assertAlmostEqual(value, max(0, min(1, target)), delta=2e-5)
                self.assertEqual(actual[3], 1)

    def test_frontend_variants_uniforms_and_alpha(self):
        source = (RENDERER / "d3d8_device.c").read_text()
        self.assertIn('config_boolean("display.per_pixel_lighting")', source)
        self.assertIn("memcpy(&inputs[count], constant_store.c[reg]", source)
        self.assertIn("memcpy(converted->model_lights", source)
        self.assertIn("variant > 3", source)
        self.assertIn("variant & 2 ? &program->lighting : NULL", source)
        psh = (RENDERER / "nv2a_psh.c").read_text()
        self.assertIn("v0.rgb = model_lighting", psh)
        self.assertNotIn("v0 = model_lighting", psh)
