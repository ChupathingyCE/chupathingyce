"""Exercise production pipeline archive writes across map-like lifetimes, offscreen."""
import os
import platform
import subprocess
import tempfile
from pathlib import Path
from unittest import TestCase, skipUnless

from metal_paths import ROOT, read  # noqa: F401
METAL = ROOT / "port/macos/metal"


def body(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]


DRIVER = r'''
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
/* Reporting is tested separately; this harness exercises archive lifetimes. */
#define METAL_REPORT(kind, operation, state, value, fallback, detail) ((void)0)
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <assert.h>
#include <stdarg.h>
static id<MTLDevice> device;
static id<MTLBinaryArchive> archive;
static NSURL *archive_url;
static BOOL archive_dirty;
static NSMutableArray *archive_descriptors;
static NSArray *archive_reads;
static pthread_rwlock_t archive_lock = PTHREAD_RWLOCK_INITIALIZER;
static void platform_log(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    va_end(args);
}
/* PRODUCTION */
int main(int argc, const char **argv) {
    assert(argc == 2);
    @autoreleasepool {
        device = MTLCreateSystemDefaultDevice();
        if (!device) return 77;
        archive_url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]];
        archive = [device newBinaryArchiveWithDescriptor:[MTLBinaryArchiveDescriptor new] error:nil];
        assert(archive);
        NSMutableArray *checks = [NSMutableArray array];
        for (unsigned map = 0; map < 4; map++) {
            @autoreleasepool {
                NSString *text = [NSString stringWithFormat:
                    @"#include <metal_stdlib>\nusing namespace metal;\n"
                    "constant uint kind [[function_constant(0)]];\n"
                    "vertex float4 v(uint i [[vertex_id]]) { return float4(float(i + kind),0,0,1); }\n"
                    "fragment float4 f() { return float4(%u.0/8.0); }\n", map + 1];
                id<MTLLibrary> library = [device newLibraryWithSource:text options:nil error:nil];
                assert(library);
                for (size_t item_index = 0; item_index < 32; item_index++) {
                    @autoreleasepool {
                        unsigned item = (unsigned)item_index;
                        MTLFunctionConstantValues *values = [MTLFunctionConstantValues new];
                        [values setConstantValue:&item type:MTLDataTypeUInt atIndex:0];
                        MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
                        descriptor.vertexFunction = [library newFunctionWithName:@"v" constantValues:values error:nil];
                        descriptor.fragmentFunction = [library newFunctionWithName:@"f"];
                        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
                        assert(pipeline_make(descriptor) != [NSNull null]);
                        if (!item) [checks addObject:descriptor];
                    }
                }
            }
            /* Map shaders and temporary descriptors have left their pool. */
            archive_save();
            assert(!archive_dirty);
            assert([[NSFileManager defaultManager] fileExistsAtPath:archive_url.path]);
            /* Reopen the saved file and require hits for every prior map,
               not just the latest batch. Never add to this loaded archive. */
            MTLBinaryArchiveDescriptor *input = [MTLBinaryArchiveDescriptor new];
            input.url = archive_url;
            id<MTLBinaryArchive> read = [device newBinaryArchiveWithDescriptor:input error:nil];
            assert(read);
            for (MTLRenderPipelineDescriptor *check in checks) {
                MTLRenderPipelineDescriptor *lookup = [check copy];
                lookup.binaryArchives = @[read];
                assert([device newRenderPipelineStateWithDescriptor:lookup
                    options:MTLPipelineOptionFailOnBinaryArchiveMiss reflection:nil error:nil]);
            }
            fprintf(stderr, "map %u saved and reloaded\n", map);
        }
        archive_save(); /* Clean exit must be a no-op. */
    }
    return 0;
}
'''


class MetalArchiveLifecycleTests(TestCase):
    @skipUnless(platform.system() == "Darwin", "Metal requires macOS")
    def test_archive_survives_map_lifetimes(self):
        production = read("gpu_metal.m")
        helpers = "\n".join(body(production, signature) for signature in (
            "static id<MTLBinaryArchive> archive_snapshot(NSError **error)",
            "static void archive_save(void)",
            "static id pipeline_make(MTLRenderPipelineDescriptor *descriptor)",
        ))
        with tempfile.TemporaryDirectory(prefix="metal-archive-lifecycle-") as folder:
            source, binary = Path(folder) / "archive.m", Path(folder) / "archive"
            source.write_text(DRIVER.replace("/* PRODUCTION */", helpers))
            subprocess.run(["clang", "-fobjc-arc", "-std=gnu11", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
                            str(source), "-framework", "Metal", "-framework", "Foundation",
                            "-framework", "QuartzCore", "-o", str(binary)], check=True, timeout=120)
            env = {**os.environ, "MTL_DEBUG_LAYER": "0"}
            result = subprocess.run([str(binary), str(Path(folder) / "pipelines.binarchive")],
                                    env=env, capture_output=True, text=True, timeout=120)
            if result.returncode == 77:
                self.skipTest("No Metal device")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("map 3 saved and reloaded", result.stderr)
