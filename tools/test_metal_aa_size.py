"""Sanitizer tests for Metal SSAA target sizing, matching ChupathingyCE GL."""
import shutil
import subprocess
import tempfile
from pathlib import Path
from unittest import TestCase, skipUnless

ROOT = Path(__file__).resolve().parents[1]

DRIVER = r'''
#include "aa_size.h"
#include <assert.h>
int main(void) {
    assert(metal_ssaa_factor(1920, 1080, 16384) == 2.0f);
    assert(metal_ssaa_factor(5120, 2880, 8192) == 1.6f);
    assert(metal_ssaa_factor(2880, 5120, 8192) == 1.6f);
    assert(metal_ssaa_factor(8192, 8192, 8192) == 1.0f);
    assert(metal_ssaa_factor(10000, 10000, 8192) == 1.0f);
    assert(metal_ssaa_factor(0, 1080, 8192) == 1.0f);
    assert(metal_ssaa_factor(1920, -1, 8192) == 1.0f);
    assert(metal_ssaa_factor(1920, 1080, 0) == 1.0f);
    assert(metal_ssaa_factor(NAN, 1080, 8192) == 1.0f);
    assert(metal_ssaa_factor(INFINITY, 1080, 8192) == 1.0f);
    for (int w = 1; w <= 8192; w += 17) {
        for (int h = 1; h <= 8192; h += 31) {
            float factor = metal_ssaa_factor(w, h, 8192);
            assert(factor >= 1.0f && factor <= 2.0f);
            assert(w * factor <= 8192.001f && h * factor <= 8192.001f);
        }
    }
    return 0;
}
'''


class MetalAASizeTests(TestCase):
    @skipUnless(shutil.which("clang"), "clang required")
    def test_supersampling_size(self):
        with tempfile.TemporaryDirectory() as folder:
            source, binary = Path(folder) / "aa.c", Path(folder) / "aa"
            source.write_text(DRIVER)
            subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                            "-I", str(ROOT / "port/macos/metal/renderer"), str(source),
                            "-o", str(binary)], check=True, timeout=60)
            subprocess.run([str(binary)], check=True, timeout=60)
