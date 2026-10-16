"""display.vsync on the Metal renderer: the CAMetalLayer's display sync
(metal_vsync.h), and no GL swap interval on the Metal path."""
import platform
import re
import shutil
import subprocess
import tempfile
from pathlib import Path
from unittest import TestCase, skipUnless

from metal_paths import ROOT, include_flags, read  # noqa: F401
METAL = ROOT / "port/macos/metal"

DRIVER = r'''
#import "metal_vsync.h"
#include <assert.h>
int main(void) {
    @autoreleasepool {
        CAMetalLayer *layer = [CAMetalLayer layer];
        assert(!metal_layer_set_vsync(nil, 1));
        assert(layer.displaySyncEnabled);            /* the default: on */
        assert(!metal_layer_set_vsync(layer, 1));    /* unchanged */
        assert(metal_layer_set_vsync(layer, 0));
        assert(!layer.displaySyncEnabled);
        assert(!metal_layer_set_vsync(layer, 0));
        assert(metal_layer_set_vsync(layer, 7));     /* any true value */
        assert(layer.displaySyncEnabled);
    }
    return 0;
}
'''


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    end = source.index("\n}\n", start)
    return source[start:end]


class MetalVsyncTests(TestCase):
    @skipUnless(platform.system() == "Darwin" and shutil.which("clang"), "macOS and clang required")
    def test_layer_display_sync_follows_setting(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "vsync.m"
            binary = Path(folder) / "vsync"
            source.write_text(DRIVER)
            subprocess.run(["clang", "-fobjc-arc", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                            *include_flags(), str(source), "-framework", "QuartzCore",
                            "-framework", "Foundation", "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_metal_platform_has_no_gl_swap_interval(self):
        source = read("sdl_platform.c")
        for gl in ("SDL_GL_SetSwapInterval", "macos_set_swap_interval", "SDL_GL_CreateContext"):
            self.assertNotIn(gl, source)

    def test_startup_and_settings_apply_set_layer_vsync(self):
        source = read("sdl_platform.c")
        for signature in ("BOOL platform_video_initialize(", "void platform_display_apply(void)"):
            body = function_body(source, signature)
            self.assertRegex(body, re.escape('macos_metal_set_vsync(config_boolean("display.vsync"))'))
        host = read("metal_host.m")
        self.assertIn("metal_layer_set_vsync(", function_body(host, "void macos_metal_set_vsync(int vsync)"))

    def test_opengl_platform_unchanged(self):
        # the OpenGL renderer keeps its own swap interval
        source = (ROOT / "port/linux/src/sdl_platform.c").read_text()
        self.assertIn("macos_set_swap_interval(config_boolean(\"display.vsync\") ? 1 : 0)", source)
