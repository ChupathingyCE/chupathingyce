"""MCC maps' textures under the bgfx renderer (d3d8_bgfx.inl).

An MCC map's textures live outside the Xbox's memory, behind headers whose
Data word names nothing (mcc_texture_bridge.c). The bgfx renderer only knew
textures in the Xbox's memory, so with display.renderer = "bgfx" an MCC map
drew its world black and its HUD as flat boxes. These tests compile the
bgfx side's MCC lookup against stubs: an MCC header is uploaded once and
reused, a paletted one once for each palette, one that cannot be made is
logged once and never retried, a header that is not MCC's goes on to the
ordinary path, and a map's headers going take their copies with them.
"""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from harness import function, mutated, read, structure

ROOT = Path(__file__).resolve().parents[1]
BGFX = "port/linux/src/d3d8_bgfx.inl"
BRIDGE = "port/linux/src/mcc_texture_bridge.c"

STUBS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned long DWORD;
typedef int BOOL;
typedef DWORD D3DCOLOR;
#define TRUE 1
#define FALSE 0
typedef struct { unsigned short idx; } bgfx_texture_handle_t;
struct xgpu_texture_description {
    DWORD format; unsigned long width, height, depth, levels;
    BOOL compressed, linear, cube_map, hires, hires_coverage, hires_point_threshold;
};
static struct { BOOL ready; } bg = { TRUE };
static int uploads, destroyed, failures, hud_textures, high_res_hud;
static BOOL refuse_upload;
static unsigned short next_handle = 1;
static DWORD mcc_headers[3][5];
static unsigned char pixels[64];
static unsigned long bg_palette_hash(const D3DCOLOR *palette) { return palette ? (unsigned long)palette[0] | 1 : 0; }
static int config_boolean(const char *name) { (void)name; return high_res_hud; }
static unsigned int hud_hires_override_texture(long asset, unsigned long *levels) {
    *levels = 1; return asset == 7 ? (hud_textures++, 99u) : 0u;
}
static void xgpu_mcc_texture_failed(struct xgpu_texture_description const *d, char const *reason) {
    (void)d; (void)reason; failures++;
}
static BOOL xgpu_mcc_texture_source(const DWORD *resource, struct xgpu_texture_description *d,
    const void **out, unsigned long *bytes, long *hud_asset) {
    int index;
    for (index = 0; index < 3; index++) {
        if (resource != mcc_headers[index]) continue;
        memset(d, 0, sizeof(*d));
        d->width = d->height = 4; d->depth = d->levels = 1;
        d->format = index == 1 ? 0x0b : 0x06;      /* the second: paletted */
        d->hires_coverage = index == 2;            /* the third: a converted meter */
        *out = pixels; *bytes = sizeof(pixels); *hud_asset = index == 2 ? 7 : -1;
        return TRUE;
    }
    return FALSE;
}
struct bg_texture;
static void bgfx_destroy_texture(bgfx_texture_handle_t handle) { (void)handle; destroyed++; }
'''

MAIN = r'''
static void bg_texture_upload(struct bg_texture *entry, const unsigned char *base, const D3DCOLOR *palette)
{
    (void)base; (void)palette; uploads++;
    if (refuse_upload) { entry->failed = TRUE; return; }
    entry->created = TRUE; entry->texture.idx = next_handle++; entry->mipmapped = FALSE;
}

int main(void)
{
    struct xgpu_texture_description d;
    bgfx_texture_handle_t handle, first;
    BOOL mipmapped;
    long override;
    DWORD stock[5] = {0};
    D3DCOLOR red[256] = {0xff0000}, blue[256] = {0x0000ff};

    /* a header outside the MCC map: the ordinary path's */
    assert(bg_mcc_texture_get(stock, NULL, &d, &handle, &mipmapped, &override) == -1);
    assert(uploads == 0);

    /* an MCC header: made once, then the same texture */
    assert(bg_mcc_texture_get(mcc_headers[0], NULL, &d, &first, &mipmapped, &override) == TRUE);
    assert(override == -1 && uploads == 1);
    assert(bg_mcc_texture_get(mcc_headers[0], NULL, &d, &handle, &mipmapped, &override) == TRUE);
    assert(uploads == 1 && handle.idx == first.idx);

    /* a paletted one: once for each palette it is drawn with */
    assert(bg_mcc_texture_get(mcc_headers[1], red, &d, &handle, &mipmapped, &override) == TRUE);
    assert(bg_mcc_texture_get(mcc_headers[1], blue, &d, &handle, &mipmapped, &override) == TRUE);
    assert(bg_mcc_texture_get(mcc_headers[1], red, &d, &handle, &mipmapped, &override) == TRUE);
    assert(uploads == 3);

    /* a meter keeps its coverage description; with High-Res HUD its stand-in */
    assert(bg_mcc_texture_get(mcc_headers[2], NULL, &d, &handle, &mipmapped, &override) == TRUE);
    assert(d.hires_coverage && override == -1 && uploads == 4);
    high_res_hud = 1;
    assert(bg_mcc_texture_get(mcc_headers[2], NULL, &d, &handle, &mipmapped, &override) == TRUE);
    assert(override == 7 && uploads == 4);
    high_res_hud = 0;

    /* the map goes: its copies with it, and a new one at the same header is made again */
    bg_mcc_texture_forget(mcc_headers[0]);
    bg_mcc_texture_forget(mcc_headers[1]);
    assert(destroyed == 3);
    assert(bg_mcc_texture_get(mcc_headers[0], NULL, &d, &handle, &mipmapped, &override) == TRUE);
    assert(uploads == 5);
    bg_mcc_texture_forget(mcc_headers[0]);

    /* one that cannot be made: logged once, never retried, not drawn */
    refuse_upload = TRUE;
    assert(bg_mcc_texture_get(mcc_headers[0], NULL, &d, &handle, &mipmapped, &override) == FALSE);
    assert(bg_mcc_texture_get(mcc_headers[0], NULL, &d, &handle, &mipmapped, &override) == FALSE);
    assert(uploads == 6 && failures == 1);
    printf("ok\n");
    return 0;
}
'''


def program(tmp_path, source=None):
    clang = shutil.which("clang") or shutil.which("cc")
    if not clang:
        pytest.skip("a C compiler is required")
    source = source if source is not None else read(BGFX)
    parts = [structure(source, "bg_texture"), structure(source, "bg_mcc_texture"),
             "#define BG_MCC_TEXTURE_BUCKETS 512",
             "static struct bg_mcc_texture *bg_mcc_textures[BG_MCC_TEXTURE_BUCKETS];",
             "static void bg_texture_upload(struct bg_texture *entry, const unsigned char *base, const D3DCOLOR *palette);",
             function(source, "bg_mcc_bucket"), function(source, "bg_mcc_texture_get"),
             function(source, "bg_mcc_texture_forget")]
    text = STUBS + "\n".join(parts) + MAIN
    # (bg_texture's members the test leaves alone)
    text = text.replace("#ifdef HALO_SHADER_PREP", "#if 0")
    (tmp_path / "test.c").write_text(text)
    exe = tmp_path / ("test.exe" if sys.platform == "win32" else "test")
    result = subprocess.run([clang, "-std=gnu99", "-Wall", "-Wno-unused-function", str(tmp_path / "test.c"),
                             "-o", str(exe)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return subprocess.run([str(exe)], capture_output=True, text=True)


def test_mcc_textures_drawn_by_bgfx(tmp_path):
    result = program(tmp_path)
    assert result.returncode == 0 and result.stdout.strip() == "ok", result.stdout + result.stderr


def test_failed_texture_is_not_retried_negative_control(tmp_path):
    source = mutated(read(BGFX), "\tif (entry->texture.failed)\n\t\treturn FALSE;\n\t*handle", "\t*handle")
    assert program(tmp_path, source).returncode != 0


def test_bgfx_binds_mcc_textures_before_the_xbox_path():
    source = read(BGFX)
    bind = function(source, "bg_bind_textures")
    assert bind.index("bg_mcc_texture_get(") < bind.index("bg_texture_get((const DWORD *)texture")
    unbind = function(read("port/linux/src/d3d8_gl.c"), "xgpu_mcc_texture_unbind")
    assert "bg_mcc_texture_forget(resource)" in unbind


def test_bridge_logs_textures_not_drawn_and_skips_gl_warmup_under_bgfx():
    bridge = read(BRIDGE)
    assert "mcc textures: %lu textures of this map were not drawn" in function(bridge, "mcc_texture_bridge_dispose")
    assert "xgpu_mcc_texture_failed(" in function(bridge, "xgpu_mcc_texture_get")
    assert "if (xgpu_bgfx) return 0;" in function(bridge, "mcc_texture_bridge_prepare")
    assert '"display.mcc_texture_prepare_mb"' in read("port/linux/src/port_config.c")
