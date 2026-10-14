"""Native overlay presentation and bounded layout regression tests."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class MetalOverlayTests(unittest.TestCase):
    def test_present_draws_overlay_before_screenshot_and_present(self):
        source = (ROOT / 'port/macos/metal/renderer/d3d8_device.c').read_text()
        present = source[source.index('void WINAPI D3DDevice_Present('):]
        draw = present.index('macos_metal_overlay_draw(back_buffer->target.texture)')
        self.assertLess(draw, present.index('write_screenshot(back_buffer'))
        self.assertLess(draw, present.index('gpu_present(back_buffer->target.texture)'))

    def test_production_layout_queue_bounds_and_feature_off(self):
        compiler = shutil.which('clang')
        if not compiler:
            self.skipTest('clang required for sanitizer regression')
        renderer = ROOT / 'port/macos/metal/renderer'
        with tempfile.TemporaryDirectory(prefix='metal-overlay-') as directory:
            temporary = Path(directory)
            harness = temporary / 'driver.c'
            harness.write_text(DRIVER)
            for enabled in (True, False):
                output = temporary / ('overlay-on' if enabled else 'overlay-off')
                command = [compiler, '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                           '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                           f'-I{renderer}', f'-I{ROOT / "port/linux/src"}',
                           str(renderer / 'ui_overlay.c'), str(harness), '-lm', '-o', str(output)]
                if enabled:
                    command.append('-DHALO_GAME_BROWSER=1')
                subprocess.run(command, check=True, capture_output=True, text=True)
                subprocess.run([str(output)], check=True, capture_output=True, text=True)

    def test_launcher_keeps_manual_input_and_disables_only_metal_board(self):
        with tempfile.TemporaryDirectory(prefix='metal launcher-') as directory:
            repo = Path(directory)
            (repo / 'tools').mkdir()
            app = repo / 'build/macos/ChupathingyCE.app/Contents/MacOS'
            app.mkdir(parents=True)
            script = repo / 'tools/play_platform_macos.sh'
            shutil.copyfile(ROOT / 'tools/play_platform_macos.sh', script)
            binaries = repo / 'bin'
            binaries.mkdir()
            open_stub = binaries / 'open'
            open_stub.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
            open_stub.chmod(0o755)
            otool = binaries / 'otool'
            for renderer, expected in (('Metal', 'false'), ('OpenGL', 'true')):
                otool.write_text(f'#!/bin/sh\necho /System/Library/Frameworks/{renderer}.framework/Versions/A/{renderer}\n')
                otool.chmod(0o755)
                output = subprocess.run(['sh', str(script)], check=True, capture_output=True, text=True,
                                        env={'PATH': f'{binaries}:/usr/bin:/bin'}).stdout
                self.assertIn(f'HALO_PLATFORM_ENABLED={expected}\n', output)
                self.assertIn('HALO_TEST_INPUT=\n', output)
                self.assertIn('HALO_INFECTION_LOCAL=false\n', output)
                self.assertIn('HALO_INFECTION_TEST_SCENARIO=local-play\n', output)
                self.assertIn('HALO_NET_ONLINE=false\n', output)


DRIVER = r'''
#include "metal_overlay.h"
#include "ui_overlay.h"
#include "ui_font.h"
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
_Static_assert(offsetof(struct metal_overlay_vertex, color) == 40, "MSL color offset");
#ifdef HALO_GAME_BROWSER
static int oversized;
int platform_input_scheme(void) { return 0; }
int posix_ui_font_has(int font, unsigned int codepoint) { (void)font; (void)codepoint; return 1; }
int posix_ui_font_metrics(int font, float size, float *a, float *d) {
    (void)font; *a = size * 0.8f; *d = size * 0.2f; return 1;
}
float posix_ui_font_advance(int font, float size, unsigned int c, unsigned int previous) {
    (void)font; (void)c; (void)previous; return size / 2;
}
unsigned char *posix_ui_font_glyph(int font, float size, unsigned int c,
    int *w, int *h, int *x, int *y) {
    (void)font; (void)size; (void)c;
    *w = oversized ? METAL_OVERLAY_ATLAS_SIZE + 1 : 8;
    *h = 9; *x = 0; *y = -8;
    unsigned char *pixels = malloc(72); assert(pixels); memset(pixels, 255, 72); return pixels;
}
void posix_ui_font_free(unsigned char *pixels) { free(pixels); }
#endif
int main(void) {
    struct metal_overlay_frame frame;
    metal_overlay_prepare(1280, 960, &frame);
    assert(frame.vertex_count == 0);
#ifdef HALO_GAME_BROWSER
    ui_overlay_rect(10, 20, 30, 40, 4, 0x11223344);
    ui_overlay_cutout(12, 24, 10, 11);
    ui_overlay_text(UI_FONT_BOLD, 12, 50, 60, UI_ALIGN_LEFT, 0xFFFFFFFF, "AB");
    metal_overlay_prepare(1280, 960, &frame);
    assert(frame.vertex_count == 18 && frame.atlas_dirty);
    assert(frame.vertices[0].x == 19 && frame.vertices[0].y == 39);
    assert(frame.vertices[0].color[0] == 0x11 && frame.vertices[0].color[3] == 0x44);
    assert(frame.cutouts[0][0] == 24 && frame.cutouts[0][3] == 70);
    assert(frame.atlas[2 * METAL_OVERLAY_ATLAS_SIZE + 2] == 255);
    ui_overlay_text(UI_FONT_BOLD, 12, 50, 60, UI_ALIGN_LEFT, 0xFFFFFFFF, "AB");
    metal_overlay_prepare(1280, 960, &frame);
    assert(frame.vertex_count == 12 && !frame.atlas_dirty);
    assert(frame.cutouts[0][3] == 0);
    ui_overlay_text(UI_FONT_BOLD, 12, 50, 60, UI_ALIGN_LEFT, 0xFFFFFFFF, "AB");
    metal_overlay_prepare(640, 480, &frame);
    assert(frame.vertex_count == 12 && frame.atlas_dirty);
    ui_overlay_rect(0, 0, 1, 1, 0, 0xFFFFFFFF);
    ui_overlay_cutout(0, 0, 1, 1);
    metal_overlay_prepare(0, 0, &frame);
    assert(frame.vertex_count == 0);
    metal_overlay_prepare(640, 480, &frame);
    assert(frame.vertex_count == 0 && frame.cutouts[0][3] == 0);
    for (int i = 0; i < 20000; i++) ui_overlay_rect(0, 0, 1, 1, 0, 0xFFFFFFFF);
    metal_overlay_prepare(640, 480, &frame);
    assert(frame.vertex_count == METAL_OVERLAY_MAX_VERTICES);
    oversized = 1;
    ui_overlay_text(UI_FONT_BOLD, 13, 0, 0, UI_ALIGN_LEFT, 0xFFFFFFFF, "C");
    metal_overlay_prepare(640, 480, &frame);
    assert(frame.vertex_count == 0);
    ui_overlay_text(UI_FONT_BOLD, NAN, 0, 0, UI_ALIGN_LEFT, 0xFFFFFFFF, "A");
    metal_overlay_prepare(640, 480, &frame);
    assert(frame.vertex_count == 0);
#endif
    return 0;
}
'''


if __name__ == '__main__':
    unittest.main()
