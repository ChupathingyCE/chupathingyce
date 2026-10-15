"""The Metal pipeline archive's segments (metal_archive.h, gpu_metal.m):
earlier runs' segments are read only and each run writes its own, because
serializing an archive loaded from a file faulted inside Metal."""
import shutil
import subprocess
import tempfile
from pathlib import Path
from unittest import TestCase, skipUnless

ROOT = Path(__file__).resolve().parents[1]
METAL = ROOT / "port/macos/metal"

DRIVER = r'''
#include "metal_archive.h"
#include <assert.h>
int main(void) {
    const char *p = "0.7.5b_Apple_M2.";
    assert(metal_archive_segment_is_current("0.7.5b_Apple_M2.1760000000-123.binarchive", p));
    assert(metal_archive_segment_is_current("0.7.5b_Apple_M2.1-2.binarchive", p));
    /* the unsegmented file of older builds, half-written files, others' */
    assert(!metal_archive_segment_is_current("0.7.5b_Apple_M2.binarchive", p));
    assert(!metal_archive_segment_is_current("0.7.5b_Apple_M2.1-2.binarchive.new", p));
    assert(!metal_archive_segment_is_current("0.7.5c_Apple_M2.1-2.binarchive", p));
    assert(!metal_archive_segment_is_current("0.7.5b_Apple_M2.-2.binarchive", p));
    assert(!metal_archive_segment_is_current("0.7.5b_Apple_M2.1-.binarchive", p));
    assert(!metal_archive_segment_is_current("0.7.5b_Apple_M2.12.binarchive", p));
    assert(!metal_archive_segment_is_current("0.7.5b_Apple_M2.1-2.binarchiveX", p));
    assert(!metal_archive_segment_is_current("", p));
    assert(METAL_ARCHIVE_SEGMENTS_MAX > 1);
    return 0;
}
'''


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    return source[start:source.index("\n}\n", start)]


class MetalArchiveTests(TestCase):
    @skipUnless(shutil.which("clang"), "clang required")
    def test_segment_names(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "archive.c"
            binary = Path(folder) / "archive"
            source.write_text(DRIVER)
            subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                            "-I", str(METAL), str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_loaded_archives_are_never_written(self):
        source = (METAL / "gpu_metal.m").read_text()
        opened = body(source, "static void archive_open(void)")
        # earlier segments go to archive_reads; the written archive is always new
        self.assertIn("archive_reads = reads.count ? reads : nil;", opened)
        self.assertIn("descriptor.url = nil;\n\tarchive = [device newBinaryArchiveWithDescriptor:", opened)
        self.assertIn("METAL_ARCHIVE_SEGMENTS_MAX", opened)
        made = body(source, "static id pipeline_make(MTLRenderPipelineDescriptor *descriptor)")
        self.assertIn("descriptor.binaryArchives = archive_reads;", made)
        self.assertNotIn("descriptor.binaryArchives = @[archive]", made)
        self.assertIn("[archive addRenderPipelineFunctionsWithDescriptor:descriptor", made)
        saved = body(source, "static void archive_save(void)")
        self.assertNotIn("archive_reads", saved)
