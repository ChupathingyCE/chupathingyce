"""The Metal renderer's high-res HUD preload (hud_preload.h): decoding ahead
on a thread, taking, waiting and stopping, under ThreadSanitizer and under
AddressSanitizer with UndefinedBehaviorSanitizer."""
import shutil
import subprocess
import tempfile
from pathlib import Path
from unittest import TestCase

ROOT = Path(__file__).resolve().parents[1]
METAL = ROOT / "port/macos/metal"

DRIVER = r'''
#include "hud_preload.h"
#include <assert.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>
#define COUNT 16
static atomic_int decoded[COUNT];
static unsigned char *decode(long asset, unsigned long *width, unsigned long *height, void *context) {
    unsigned char *pixels;
    (void)context;
    usleep(200);                          /* long enough to race a take */
    atomic_fetch_add(&decoded[asset], 1);
    if (asset == 3)
        return NULL;                      /* a picture that won't decode */
    *width = (unsigned long)asset + 1;
    *height = 2;
    pixels = malloc(*width * *height * 4);
    memset(pixels, (int)asset, *width * *height * 4);
    return pixels;
}
static int wanted(long asset, void *context) { (void)context; return asset != 5; }
static struct hud_preload_slot slots[COUNT];
static struct hud_preload preload = HUD_PRELOAD_INITIALIZER(slots, COUNT, decode, NULL);
int main(void) {
    long assets[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1, 99, 1 };
    unsigned char *pixels = NULL;
    unsigned long width = 0, height = 0;
    int round, asset;

    for (round = 0; round < 20; round++) {
        memset(decoded, 0, sizeof(decoded));
        /* 0-9 but 5 (unwanted); -1 and 99 out of range; 1 only once */
        assert(hud_preload_start(&preload, assets, 13, wanted) == 9);
        /* taken as soon as asked, waiting for it if need be */
        assert(hud_preload_take(&preload, 9, &pixels, &width, &height));
        assert(width == 10 && height == 2 && pixels[0] == 9);
        free(pixels);
        /* one that failed: taken as NULL (the caller logs it) */
        assert(hud_preload_take(&preload, 3, &pixels, &width, &height) && !pixels);
        /* not queued: the caller decodes it */
        assert(!hud_preload_take(&preload, 5, &pixels, &width, &height));
        assert(!hud_preload_take(&preload, 12, &pixels, &width, &height));
        assert(!hud_preload_take(&preload, -1, &pixels, &width, &height));
        if (round % 2) {
            /* a map unloaded mid-decode: the rest let go, none leaked */
            hud_preload_stop(&preload);
        } else {
            for (asset = 0; asset < 10; asset++) {
                if (asset == 3 || asset == 5 || asset == 9)
                    continue;
                assert(hud_preload_take(&preload, asset, &pixels, &width, &height));
                assert(pixels && width == (unsigned long)asset + 1);
                free(pixels);
                assert(atomic_load(&decoded[asset]) == 1);
            }
            /* a second take: already the caller's */
            assert(!hud_preload_take(&preload, 0, &pixels, &width, &height));
            hud_preload_stop(&preload);
        }
        assert(atomic_load(&decoded[5]) == 0);
    }
    /* nothing to queue: no thread */
    assert(hud_preload_start(&preload, assets, 0, NULL) == 0);
    hud_preload_stop(&preload);
    return 0;
}
'''


class HudPreloadTests(TestCase):
    def run_with(self, sanitizers):
        compiler = shutil.which("clang")
        if not compiler:
            self.skipTest("clang required")
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "preload.c"
            binary = Path(folder) / "preload"
            source.write_text(DRIVER)
            subprocess.run([compiler, "-std=gnu11", "-g", "-Wall", "-Wextra", "-Werror", f"-fsanitize={sanitizers}",
                            "-fno-sanitize-recover=all", "-I", str(METAL / "renderer"), str(source),
                            "-lpthread", "-o", str(binary)], check=True)
            env = {"TSAN_OPTIONS": "halt_on_error=1", "ASAN_OPTIONS": "halt_on_error=1"}
            subprocess.run([str(binary)], check=True, env=env)

    def test_thread_sanitizer(self):
        self.run_with("thread")

    def test_address_and_undefined(self):
        self.run_with("address,undefined")

    def test_wired_into_hud_hires(self):
        source = (METAL / "renderer/hud_hires.c").read_text()
        self.assertIn("hud_preload_start(&preload, assets, count, preload_wanted)", source)
        self.assertIn("hud_preload_take(&preload, asset,", source)
        self.assertIn("void hud_hires_map_unloaded(void)\n{\n\thud_preload_stop(&preload);", source)
        compat = (METAL / "renderer/metal_compat.c").read_text()
        self.assertNotIn("hud_hires_map_loaded", compat)
        self.assertNotIn("hud_hires_map_unloaded", compat)
