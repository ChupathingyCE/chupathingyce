"""Compile and exercise the Metal frontend's checked Xbox upload spans."""
import shutil
import subprocess
import tempfile
from pathlib import Path
from unittest import TestCase, skipUnless

ROOT = Path(__file__).resolve().parents[1]


class UploadBoundsTests(TestCase):
    @skipUnless(shutil.which("clang"), "clang required")
    def test_boundaries_and_overflow(self):
        compiler = shutil.which("clang")
        assert compiler is not None
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "bounds.c"
            binary = Path(folder) / "bounds"
            source.write_text('''
#include "upload_bounds.h"
#include <assert.h>
int main(void) {
    uint32_t start = 0, bytes = 0;
    const uint32_t base = 0x80000000U, capacity = 0x04000000U;
    assert(metal_upload_span(base, 0, 3, 16, base, capacity, &start, &bytes));
    assert(start == base && bytes == 48);
    assert(metal_upload_span(base + capacity - 64, 0, 1, 64, base, capacity, &start, &bytes));
    assert(!metal_upload_span(base + capacity - 63, 0, 1, 64, base, capacity, &start, &bytes));
    assert(!metal_upload_span(base - 1, 0, 1, 16, base, capacity, &start, &bytes));
    assert(!metal_upload_span(base, UINT32_MAX, 1, UINT32_MAX, base, capacity, &start, &bytes));
    assert(!metal_upload_span(base, 0, UINT32_MAX, UINT32_MAX, base, capacity, &start, &bytes));
    assert(!metal_upload_span(base, 0, 0, 16, base, capacity, &start, &bytes));
    assert(metal_upload_span(base, 0, METAL_DRAW_VERTICES_MAX, 256, base, capacity, &start, &bytes));
    assert(bytes == METAL_UPLOAD_BYTES_MAX);
    assert(!metal_upload_span(base, 0, METAL_DRAW_VERTICES_MAX + 1, 1, base, capacity, &start, &bytes));
    assert(!metal_upload_span(base, 0, METAL_DRAW_VERTICES_MAX, 257, base, capacity, &start, &bytes));
    assert(metal_upload_span(base, UINT32_MAX, 100, 0, base, capacity, &start, &bytes));
    assert(start == base && bytes == 64);
    return 0;
}
''')
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-I", str(ROOT / "port/macos/metal/renderer"),
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
