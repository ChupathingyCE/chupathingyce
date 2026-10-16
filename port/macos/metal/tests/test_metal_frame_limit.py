"""The Metal renderer's display.max_fps arithmetic (frame_limit.h), under
AddressSanitizer and UndefinedBehaviorSanitizer."""
import shutil
import subprocess
import tempfile
from pathlib import Path
from unittest import TestCase, skipUnless

from metal_paths import include_flags, read  # noqa: F401

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
                            *include_flags(),
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


class FrameLimitWiringTests(TestCase):
    """the limit runs on the Metal path, once a frame, without GL"""

    def source(self, name):
        return read(name.rsplit("/", 1)[-1])

    def test_present_limits_after_gpu_present(self):
        source = self.source("xbox_device.c")
        present = source[source.index("void WINAPI D3DDevice_Present("):]
        present = present[:present.index("\n}\n")]
        self.assertEqual(present.count("platform_video_frame_limit();"), 1)
        self.assertLess(present.index("gpu_present(back_buffer->target.texture)"),
                        present.index("platform_video_frame_limit();"))

    def test_limit_uses_shared_arithmetic_and_no_gl_swap(self):
        source = self.source("sdl_platform.c")
        body = source[source.index("void platform_video_frame_limit(void)"):]
        body = body[:body.index("\n}\n")]
        self.assertIn("metal_frame_wait_ns(", body)
        self.assertIn("frame_interval_ns()", body)
        interval = source[source.index("static Uint64 frame_interval_ns(void)"):]
        interval = interval[:interval.index("\n}\n")]
        self.assertIn('config_integer("display.max_fps")', interval)
        self.assertIn("metal_frame_interval_ns(", interval)
        self.assertNotIn("SDL_GL_SwapWindow", source)
        self.assertNotIn("void platform_video_swap(void)", source)

    def test_metal_scheduler_stays_off(self):
        source = self.source("gpu_metal.m")
        start = source[source.index("static void pacing_start(uint32_t flags)"):]
        start = start[:start.index("\n}\n")]
        self.assertIn("pacing.off = YES;", start)
        self.assertNotIn("if (!(flags & GPU_INITIALIZE_PACING_OFF))\n\t\tpacing.off", start)
