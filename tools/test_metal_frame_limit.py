"""The Metal renderer's display.max_fps arithmetic (frame_limit.h), under
AddressSanitizer and UndefinedBehaviorSanitizer."""
import shutil
import subprocess
import tempfile
from pathlib import Path
from unittest import TestCase, skipUnless

ROOT = Path(__file__).resolve().parents[1]

DRIVER = r'''
#include "frame_limit.h"
#include <assert.h>
int main(void) {
    uint64_t next = 0, wait;
    /* vsync on, or a negative limit: none */
    assert(metal_frame_interval_ns(1, 0, 60.0f) == 0);
    assert(metal_frame_interval_ns(1, 144, 60.0f) == 0);
    assert(metal_frame_interval_ns(0, -1, 60.0f) == 0);
    /* an explicit limit */
    assert(metal_frame_interval_ns(0, 100, 60.0f) == 10000000);
    assert(metal_frame_interval_ns(0, 1, 60.0f) == 1000000000);
    /* 0: twice the refresh rate, 60 Hz when unknown */
    assert(metal_frame_interval_ns(0, 0, 60.0f) == 8333333);
    assert(metal_frame_interval_ns(0, 0, 120.0f) == 4166666);
    assert(metal_frame_interval_ns(0, 0, 0.0f) == 8333333);
    assert(metal_frame_interval_ns(0, 0, -5.0f) == 8333333);

    /* no limit: no wait, schedule untouched */
    next = 123;
    assert(metal_frame_wait_ns(1000, &next, 0) == 0 && next == 123);
    /* first frame: no wait, next one an interval on */
    next = 0;
    assert(metal_frame_wait_ns(1000, &next, 100) == 0 && next == 1100);
    /* early: wait until due */
    wait = metal_frame_wait_ns(1040, &next, 100);
    assert(wait == 60 && next == 1200);
    /* a little late: keeps the cadence */
    assert(metal_frame_wait_ns(1250, &next, 100) == 0 && next == 1300);
    /* more than an interval late: starts again from now */
    assert(metal_frame_wait_ns(5000, &next, 100) == 0 && next == 5100);
    /* exactly on time */
    assert(metal_frame_wait_ns(5100, &next, 100) == 0 && next == 5200);
    return 0;
}
'''


class FrameLimitTests(TestCase):
    @skipUnless(shutil.which("clang"), "clang required")
    def test_interval_and_schedule(self):
        compiler = shutil.which("clang")
        assert compiler is not None
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "limit.c"
            binary = Path(folder) / "limit"
            source.write_text(DRIVER)
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                            "-I", str(ROOT / "port/macos/metal/renderer"),
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
