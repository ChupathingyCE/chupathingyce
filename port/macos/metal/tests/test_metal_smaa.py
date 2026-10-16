"""Windowless SMAA adapter/pipeline checks using the bundled HIGH preset."""
import os
import platform
import subprocess
import tempfile
from pathlib import Path
from unittest import TestCase, skipUnless

from test_metal_post import DRIVER as POST_DRIVER

from metal_paths import ROOT, include_flags, read  # noqa: F401
DRIVER = r'''
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <assert.h>
#include <stdio.h>
#include "metal_smaa_source.h"
int main(int argc, const char **argv) { @autoreleasepool {
    assert(argc == 2);
    assert(metal_smaa_source(@"not the expected SMAA source") == nil);
    NSString *body = [NSString stringWithContentsOfFile:@(argv[1]) encoding:NSISOLatin1StringEncoding error:nil];
    NSString *source = metal_smaa_source(body);
    assert(source);
    id<MTLDevice> device = MTLCreateSystemDefaultDevice(); if (!device) return 77;
    NSError *error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    if (!library) fprintf(stderr, "%s\n", error.description.UTF8String);
    assert(library);
    for (NSString *name in @[@"smaa_edges", @"smaa_weights", @"smaa_blend"]) {
        MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = [library newFunctionWithName:@"smaa_vertex"];
        d.fragmentFunction = [library newFunctionWithName:name];
        d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:d error:&error];
        if (!pipeline) fprintf(stderr, "%s\n", error.description.UTF8String);
        assert(pipeline);
    }
    return 0;
}}
'''


def asset_source():
    assets = []
    for name, symbol in (("SMAA.hlsl", "xgpu_smaa_shader"),
                         ("area_tex.zlib", "xgpu_smaa_area_texture"),
                         ("search_tex.zlib", "xgpu_smaa_search_texture")):
        data = (ROOT / "port/third_party/smaa" / name).read_bytes()
        if name.endswith("hlsl"):
            data = bytes(value if value < 128 else 32 for value in data) + b"\0"
        size = len(data)
        data += b"\0" * (-len(data) % 4)
        words = ",".join(str(int.from_bytes(data[start:start + 4], "little")) for start in range(0, len(data), 4))
        assets.append(f"const unsigned int {symbol}[] = {{{words}}};\nconst uint32_t {symbol}_size = {size};")
    return "\n".join(assets)


class MetalSMAATests(TestCase):
    @skipUnless(platform.system() == "Darwin", "Metal requires macOS")
    def test_bundled_high_preset_compiles(self):
        with tempfile.TemporaryDirectory(prefix="metal-smaa-") as folder:
            source, binary = Path(folder) / "smaa.m", Path(folder) / "smaa"
            source.write_text(DRIVER)
            subprocess.run(["clang", "-fobjc-arc", "-Wall", "-Wextra", "-Werror", *include_flags(), str(source), "-framework", "Foundation",
                            "-framework", "Metal", "-o", str(binary)], check=True, timeout=120)
            run = subprocess.run([str(binary), str(ROOT / "port/third_party/smaa/SMAA.hlsl")],
                                 env={**os.environ, "MTL_DEBUG_LAYER": "1"}, capture_output=True,
                                 text=True, timeout=120)
            if run.returncode == 77:
                self.skipTest("No Metal device")
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    @skipUnless(platform.system() == "Darwin", "Metal requires macOS")
    def test_production_three_pass_offscreen(self):
        driver = POST_DRIVER.replace('#include "metal_post.inc"',
                                     '#include <zlib.h>\n' + asset_source()
                                     + '\n#include "metal_post.inc"\n#include "metal_smaa.inc"')
        driver = driver.replace("macos_metal_fxaa(1, rect);", "macos_metal_smaa(1, rect);")
        driver = driver.replace("commands && post_fxaa_pipeline", "commands && smaa_pipeline[2]")
        # A flat view next to a contrasting neighbor must not read across
        # the view boundary during edges, searches or final blending.
        driver = driver.replace("input[p] = input[p+1] = input[p+2] = color;",
                                "if (uniform && clipped && (x < width/4 || x >= 3*width/4 || "
                                "y < height/4 || y >= 3*height/4)) color = 255;\n"
                                "input[p] = input[p+1] = input[p+2] = color;")
        with tempfile.TemporaryDirectory(prefix="metal-smaa-pass-") as folder:
            source, binary = Path(folder) / "pass.m", Path(folder) / "pass"
            source.write_text(driver)
            subprocess.run(["clang", "-fobjc-arc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", *include_flags(), str(source), "-framework", "Foundation",
                            "-framework", "Metal", "-lz", "-o", str(binary)], check=True, timeout=120)
            run = subprocess.run([str(binary)], env={**os.environ, "MTL_DEBUG_LAYER": "1"},
                                 capture_output=True, text=True, timeout=120)
            if run.returncode == 77:
                self.skipTest("No Metal device")
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertNotIn("validation error", run.stderr.lower())

    def test_hook_and_depth_lifetime(self):
        source = read("xbox_device.c")
        self.assertIn('BOOL smaa = !strcmp(mode, "smaa");', source)
        self.assertIn("macos_metal_smaa(target->target.texture, corners)", source)
        post = read("metal_smaa.inc")
        self.assertIn("pass_end();", post)
        self.assertNotIn("pass_finish(YES)", post)
        self.assertIn("MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue", post)
