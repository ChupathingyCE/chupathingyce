#!/usr/bin/env python3
"""Probe live high-res setting changes through production cache/selection code.

Run from the repository root: python3 port/macos/metal/tests/probe_live_hires.py
Exit 1 means a confirmed compatibility gap; 0 means every transition passed.
This is a CPU integration probe, not an on-screen game or PNG-decoder test.
"""
import json
import subprocess
import tempfile
from pathlib import Path

from metal_paths import include_flags, read
from test_metal_texture_stage_cache import HARNESS, cache_harness


def body(text, signature):
    start = text.index(signature)
    return text[start:text.index("\n}\n", start) + 3]


PROVIDERS = r'''
static int hud_enabled_setting, text_enabled_setting;
static unsigned long changes = 1;
static unsigned long config_changes(void) { return changes; }
static int config_boolean(const char *name)
{
    if (!strcmp(name, "display.high_res_hud")) return hud_enabled_setting;
    if (!strcmp(name, "display.high_res_text")) return text_enabled_setting;
    return 0;
}
/* Test map/tag and CRC providers. Selection and enable caching are production. */
typedef unsigned char Bytef;
typedef unsigned int uInt;
static struct { int title; unsigned long crc; const char *tag; int bitmap; }
    hud_hires_embedded[1] = {{0, 0, "probe asset", 0}};
static struct { int other_pixels_logged; } textures[1];
static long hud_hires_asset_count(void) { return 1; }
static long hud_hires_asset_at(unsigned long a, long w, long h)
{
    (void)a; (void)w; (void)h; return 0;
}
static unsigned long crc_checks;
static unsigned long crc32(unsigned long crc, const Bytef *pixels, uInt bytes)
{
    (void)crc; (void)pixels; (void)bytes; crc_checks++; return 0;
}
'''

MAIN = r'''
/* Force the three public lookup scenarios without a memory write/eviction. */
static gpu_texture bind_live(const char *path, DWORD address, struct xgpu_texture_description *description)
{
    if (!strcmp(path, "recent")) {
        memset(texture_stage_lookups, 0, sizeof(texture_stage_lookups));
    } else if (!strcmp(path, "bucket")) {
        memset(texture_stage_lookups, 0, sizeof(texture_stage_lookups));
        memset(recent_textures, 0, sizeof(recent_textures));
    }
    return resolve(0, address, 0, 0, NULL, description);
}
int main(int argc, char **argv)
{
    assert(argc == 4);
    int title = !strcmp(argv[1], "title");
    int initial = !strcmp(argv[2], "on");
    hud_hires_embedded[0].title = title;
    int *setting = title ? &text_enabled_setting : &hud_enabled_setting;
    *setting = initial;
    if (title) assert(text_hires_enabled() == initial);
    hud = 3000;
    struct xgpu_texture_description description;
    DWORD address = PLATFORM_CONTIGUOUS_BASE;
    guest[0] = 37; /* Independent expected content for the original bitmap. */
    gpu_texture before = bind_live(argv[3], address, &description);
    assert((before == hud) == initial);
    unsigned long uploads_before = uploads;
    /* An unrelated config edit must not invalidate selection or upload data. */
    unsigned long generation = hud_hires_settings(), crc_before = crc_checks;
    changes++;
    assert(hud_hires_settings() == generation);
    assert(bind_live(argv[3], address, &description) == before);
    assert(crc_checks == crc_before && uploads == uploads_before);
    /* Changing the other high-res flag must leave this asset's upload alone. */
    int *other = title ? &hud_enabled_setting : &text_enabled_setting;
    *other = 1;
    changes++;
    assert(bind_live(argv[3], address, &description) == before);
    assert(uploads == uploads_before);
    *setting = !initial;
    changes++;
    /* Both providers see the live config, without map reload or guest writes. */
    int selection = hud_hires_override_find(address, 4, 4, 64) >= 0;
    assert(selection == !initial);
    if (title) assert(text_hires_enabled() == !initial);
    gpu_texture after = bind_live(argv[3], address, &description);
    int cache_passed = (after == hud) == !initial;
    assert(uploads == uploads_before + (initial && cache_passed ? 1 : 0));
    if (cache_passed) {
        assert(description.hires == !initial);
        assert(description.levels == (initial ? 1 : 4));
        if (initial) assert(uploaded[after] == 37);
        assert(bind_live(argv[3], address, &description) == after);
        assert(uploads == uploads_before + (initial ? 1 : 0));
    }
    /* Toggle back and confirm the same unchanged resource remains usable. */
    *setting = initial;
    changes++;
    gpu_texture restored = bind_live(argv[3], address, &description);
    assert((restored == hud) == initial);
    assert(description.hires == initial);
    printf("{\"asset\":\"%s\",\"transition\":\"%s\",\"cache_path\":\"%s\",\"selection_passed\":true,"
           "\"cached_binding_passed\":%s,\"font_enable_passed\":%s}\n",
           title ? "title" : "hud", initial ? "on-to-off" : "off-to-on", argv[3],
           cache_passed ? "true" : "false", title ? "true" : "null");
    texture_frame++;
    assert(texture_cache_room(16u * 1024 * 1024));
    assert(texture_entry_count == 0 && texture_destroys == texture_creates);
    return 0;
}
'''


def main():
    fixture = HARNESS[:HARNESS.index("int main(int argc, char **argv)")] + MAIN
    old_config = body(fixture, "static int config_boolean(")
    fixture = fixture.replace(old_config, "static int config_boolean(const char *name);")
    fixture = fixture.replace("static int force_no_cache;", "")
    fixture = fixture.replace(body(fixture, "static void mutate("), "")
    old_provider = body(fixture, "static long hud_hires_override_find(")
    hud = read("hud_hires.c")
    selection = body(hud, "long hud_hires_override_find(")
    settings = hud[hud.index("static int hud_enabled"):hud.index("long hud_hires_override_find(")]
    fixture = fixture.replace("static unsigned long hud_hires_settings(void) { return 0; }", "")
    text = read("text_hires.c")
    providers = PROVIDERS + settings + selection + body(text, "static int text_enabled(")
    providers += body(text, "int text_hires_enabled(")
    # The fixture's platform/tag helpers precede the production selection body.
    fixture = fixture.replace(old_provider, providers)
    fixture = fixture.replace("#include <assert.h>", "#include <assert.h>\n#include <stdio.h>")
    results = []
    with tempfile.TemporaryDirectory(prefix="metal-live-hires-") as folder:
        root = Path(folder)
        source, binary = root / "probe.c", root / "probe"
        source.write_text(cache_harness(fixture))
        subprocess.run(["clang", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", *include_flags(), str(source), "-o", str(binary)], check=True)
        for asset in ("hud", "title"):
            for initial in ("off", "on"):
                for cache_path in ("stage", "recent", "bucket"):
                    run = subprocess.run([str(binary), asset, initial, cache_path], capture_output=True, text=True, check=True)
                    result = json.loads(run.stdout)
                    results.append(result)
                    print(json.dumps(result), flush=True)
    return 0 if all(r["cached_binding_passed"] for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
