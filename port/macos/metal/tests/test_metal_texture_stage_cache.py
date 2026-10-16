"""Run actual texture-cache lookup/eviction code with deterministic guest writes.

No game data or Metal device required. GPU uploads, page watching and override
providers are mocked; cache policy, identity matching, palette hashes, span
checks and eviction are extracted from xbox_textures.c and run under sanitizers.
"""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from metal_paths import include_flags, read  # noqa: F401


def function(source, name):
    match = re.search(r"^(?:static )?(?:gpu_texture|void|BOOL|unsigned long) " + name + r"\(", source, re.M)
    if not match:
        raise AssertionError(f"missing function {name}")
    opening = source.index("{", match.start())
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "gpu.h"
typedef uint32_t DWORD, D3DCOLOR;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define D3DFORMAT_FORMAT_MASK 0xff
#define D3DFORMAT_FORMAT_SHIFT 0
#define PLATFORM_CONTIGUOUS_BASE 0x10000UL
#define PLATFORM_CONTIGUOUS_SIZE 0x80000UL
#define PLATFORM_PHYSICAL_TO_VIRTUAL(data) ((uintptr_t)(data))
#define XBOX_ADDRESS(value) ((uint32_t)(uintptr_t)(value))
#define _custom_edition_channels_xbox 0
/* DESCRIPTION */
static unsigned char guest[PLATFORM_CONTIGUOUS_SIZE];
static unsigned long page_generation[PLATFORM_CONTIGUOUS_SIZE / 0x4000];
static unsigned long watch_serial = 1, hashes, generation_checks, uploads, palette_hashes;
static unsigned long texture_creates, texture_destroys, result_checks;
static int force_no_cache;
static struct { BOOL s3tc; } device_capabilities = {1};
static gpu_texture atlas, menu, hud, replacement;
static BOOL coverage, threshold;
static unsigned char uploaded[4096], uploaded_tail[4096];
static D3DCOLOR uploaded_palette[4096];
struct format_information { int kind; };
enum { _texel_dxt1, _texel_dxt3, _texel_dxt5 };
static struct format_information format_information(unsigned long format)
{
    (void)format;
    return (struct format_information){_texel_dxt1};
}
static int config_boolean(const char *name)
{
    return !strcmp(name, "debug.texture_no_cache") ? force_no_cache : 0;
}
static long config_integer(const char *name) { (void)name; return 16; }
static void platform_log(const char *format, ...) { (void)format; }
static void *xbox_pointer(unsigned long address)
{
    assert(address >= PLATFORM_CONTIGUOUS_BASE);
    assert(address - PLATFORM_CONTIGUOUS_BASE < PLATFORM_CONTIGUOUS_SIZE);
    return guest + address - PLATFORM_CONTIGUOUS_BASE;
}
static int platform_is_contiguous(const void *pointer)
{
    return (uintptr_t)pointer >= (uintptr_t)guest && (uintptr_t)pointer < (uintptr_t)(guest + sizeof(guest));
}
static unsigned long memory_watch_serial(void) { return watch_serial; }
static unsigned long memory_watch_generation(unsigned long address, unsigned long size)
{
    assert(address >= PLATFORM_CONTIGUOUS_BASE && size);
    assert(address - PLATFORM_CONTIGUOUS_BASE < PLATFORM_CONTIGUOUS_SIZE);
    assert(size <= PLATFORM_CONTIGUOUS_SIZE - (address - PLATFORM_CONTIGUOUS_BASE));
    unsigned long first = (address - PLATFORM_CONTIGUOUS_BASE) / 0x4000;
    unsigned long last = (address - PLATFORM_CONTIGUOUS_BASE + size - 1) / 0x4000;
    unsigned long generation = 1;
    for (unsigned long i = first; i <= last; i++)
        if (page_generation[i] > generation) generation = page_generation[i];
    generation_checks++;
    return generation;
}
static void memory_watch_protect(unsigned long address, unsigned long size)
{
    assert(address >= PLATFORM_CONTIGUOUS_BASE && size != 0);
}
static void mutate(unsigned long address)
{
    (*(unsigned char *)xbox_pointer(address))++;
    page_generation[(address - PLATFORM_CONTIGUOUS_BASE) / 0x4000] = ++watch_serial;
}
static void xgpu_texture_describe(DWORD format, DWORD size, struct xgpu_texture_description *d)
{
    memset(d, 0, sizeof(*d));
    d->format = format & 0xff;
    d->width = size ? size : 4;
    d->height = d->width;
    d->depth = 1;
    d->levels = 1;
    d->compressed = format & 0x100;
}
static BOOL texture_size_supported(const struct xgpu_texture_description *d)
{
    return d->width <= 1024 && d->height <= 1024;
}
static unsigned long xgpu_texture_face_size(const struct xgpu_texture_description *d)
{
    return d->width * d->height * 4;
}
static unsigned long xgpu_texture_level_offset(const struct xgpu_texture_description *d, unsigned long level)
{
    (void)level; return xgpu_texture_face_size(d);
}
gpu_texture gpu_texture_create(const struct gpu_texture_description *description)
{
    (void)description;
    assert(texture_creates + 1 < 4096);
    return (gpu_texture)++texture_creates;
}
void gpu_texture_destroy(gpu_texture texture) { assert(texture); texture_destroys++; }
static void upload(gpu_texture texture, uint32_t type, const struct xgpu_texture_description *d,
    const void *source, const D3DCOLOR *palette, BOOL decoded, unsigned char order)
{
    (void)type; (void)d; (void)decoded; (void)order;
    uploaded[texture] = *(const unsigned char *)source;
    uploaded_tail[texture] = ((const unsigned char *)source)[32];
    uploaded_palette[texture] = palette ? palette[0] : 0;
    uploads++;
}
static long hud_hires_override_find(unsigned long a, unsigned long w, unsigned long h, unsigned long size)
{
    (void)a; (void)w; (void)h; (void)size; return -1;
}
static gpu_texture hud_hires_override_texture(long index, unsigned long *levels)
{
    (void)index; *levels = 4; return hud;
}
static BOOL hud_hires_override_coverage(long index) { (void)index; return coverage; }
static BOOL hud_hires_override_point_threshold(long index) { (void)index; return threshold; }
static gpu_texture text_hires_atlas_texture(DWORD data) { (void)data; result_checks++; return atlas; }
static gpu_texture menu_art_texture(DWORD data, uint32_t *levels) { (void)data; *levels = 3; return menu; }
static unsigned char custom_edition_texels_order(unsigned long address)
{
    (void)address; return _custom_edition_channels_xbox;
}
/* CACHE TYPES */
static void texture_override_apply(struct texture_entry *entry, const D3DCOLOR *palette)
{
    (void)palette;
    entry->texture_override = replacement;
    entry->texture_override_levels = 2;
}
/* FUNCTIONS */
static gpu_texture resolve(unsigned stage, DWORD data, DWORD format, DWORD size,
    const D3DCOLOR *palette, struct xgpu_texture_description *description)
{
    DWORD resource[5] = {0, data, 0, format, size};
    uint32_t type = 0;
    gpu_texture texture = xgpu_texture_stage_get(stage, resource, palette, &type, description);
    assert(type == GPU_TEXTURE_2D);
    return texture;
}
int main(int argc, char **argv)
{
    (void)argv;
    force_no_cache = argc > 1;
    struct xgpu_texture_description description;
    DWORD a = PLATFORM_CONTIGUOUS_BASE, b = a + 0x4000;
    D3DCOLOR palette[256] = {0};
    if (force_no_cache) {
        assert(resolve(0, a, 0, 0, NULL, &description));
        assert(resolve(0, a, 0, 0, NULL, &description));
        assert(uploads == 2 && hashes != 0);
        goto cleanup;
    }
    gpu_texture first = resolve(0, a, 0, 0, NULL, &description);
    assert(first && uploads == 1);
    unsigned long old_hashes = hashes, old_checks = generation_checks;
    for (int i = 0; i < 100; i++) assert(resolve(0, a, 0, 0, NULL, &description) == first);
    assert(uploads == 1 && generation_checks == old_checks);
    assert(hashes == old_hashes); /* bypass bucket hashes/global recent table */
    assert(result_checks == 101); /* live override lookup is never suppressed */
    atlas = 2000;
    assert(resolve(0, a, 0, 0, NULL, &description) == atlas && description.levels == 1);
    atlas = 0; menu = 2001;
    assert(resolve(0, a, 0, 0, NULL, &description) == menu && description.hires && description.levels == 3);
    menu = 0;
    assert(resolve(0, a, 0, 0, NULL, &description) == first && !description.hires);
    assert(uploads == 1);

    mutate(a); /* raw pointer write between draws, without a setter/Lock */
    assert(resolve(0, a, 0, 0, NULL, &description) == first);
    assert(uploads == 2 && uploaded[first] == 1);
    old_checks = generation_checks;
    mutate(b); /* unrelated watched page: revalidate but do not upload a */
    assert(resolve(0, a, 0, 0, NULL, &description) == first);
    assert(uploads == 2 && generation_checks > old_checks);
    old_checks = generation_checks;
    assert(resolve(0, a, 0, 0, NULL, &description) == first && generation_checks == old_checks);
    unsigned long before_partial = uploads;
    mutate(a + 32); /* partial write inside an existing texture span */
    assert(resolve(0, a, 0, 0, NULL, &description) == first);
    assert(uploads == before_partial + 1 && uploaded_tail[first] == 1);
    mutate(a + 4096); /* another 4 KiB region on the same 16 KiB host page */
    assert(resolve(0, a, 0, 0, NULL, &description) == first);
    assert(uploads == before_partial + 2 && uploaded[first] == 1);

    /* Live header metadata, not resource pointer identity, determines reuse. */
    gpu_texture second = resolve(0, b, 0, 0, NULL, &description);
    assert(second != first && uploaded[second] == 1);
    assert(resolve(0, a, 1, 0, NULL, &description) != first);
    assert(resolve(0, a, 0, 8, NULL, &description) != first && description.width == 8);
    assert(resolve(0, a, 0, 0, NULL, &description) == first);
    /* An invalid stage index takes the generic path, never indexes stage storage. */
    assert(resolve(999, a, 0, 0, NULL, &description) == first);
    gpu_texture stages[GPU_STAGE_COUNT];
    for (unsigned stage = 0; stage < GPU_STAGE_COUNT; stage++)
        stages[stage] = resolve(stage, a, 0x20 + stage, 0, NULL, &description);
    old_hashes = hashes; old_checks = uploads;
    for (unsigned repeat = 0; repeat < 100; repeat++)
        for (unsigned stage = 0; stage < GPU_STAGE_COUNT; stage++)
            assert(resolve(stage, a, 0x20 + stage, 0, NULL, &description) == stages[stage]);
    assert(uploads == old_checks);
    assert(hashes == old_hashes); /* all four stage identities remain independent */
    assert(resolve(0, a, 0, 0, NULL, &description) == first);

    /* Find a distinct texture colliding in the global recent table. */
    DWORD collision = 0;
    for (DWORD address = a + 64; address < a + PLATFORM_CONTIGUOUS_SIZE - 64; address += 64)
        if (bucket_index(address, 0, 0) % RECENT_TEXTURE_COUNT == bucket_index(a, 0, 0) % RECENT_TEXTURE_COUNT) {
            collision = address; break;
        }
    assert(collision);
    assert(resolve(1, collision, 0, 0, NULL, &description));
    old_hashes = hashes;
    assert(resolve(0, a, 0, 0, NULL, &description) == first);
    assert(hashes == old_hashes); /* another stage cannot displace this hint */

    /* P8 palettes can change without a watched texel write, even at one address. */
    unsigned long before_palettes = palette_hashes;
    palette[0] = 10;
    gpu_texture p8 = resolve(0, a, 0x0b, 0, palette, &description);
    assert(uploaded_palette[p8] == 10);
    old_checks = uploads;
    assert(resolve(0, a, 0x0b, 0, palette, &description) == p8 && uploads == old_checks);
    palette[0] = 11;
    gpu_texture p8_other = resolve(0, a, 0x0b, 0, palette, &description);
    assert(p8_other != p8 && uploaded_palette[p8_other] == 11);
    palette[0] = 10;
    assert(resolve(0, a, 0x0b, 0, palette, &description) == p8);
    assert(palette_hashes == before_palettes + 4);
    for (unsigned variant = 12; variant < 24; variant++) {
        palette[0] = variant;
        gpu_texture t = resolve(0, a, 0x0b, 0, palette, &description);
        assert(uploaded_palette[t] == variant); /* bounded palette-variant recycling */
    }
    p8 = resolve(0, a, 0x0b, 0, NULL, &description);
    assert(uploaded_palette[p8] == 0);
    assert(resolve(0, a, 0, 0, NULL, &description) == first);

    /* Existing entries still re-resolve mutable HUD flags on a stage hit. */
    struct texture_entry *entry = texture_buckets[bucket_index(a, 0, 0)];
    while (entry && (entry->data != a || entry->format_word != 0 || entry->size_word != 0)) entry = entry->next;
    assert(entry);
    entry->override = 0; hud = 2002; coverage = threshold = 1;
    assert(resolve(0, a, 0, 0, NULL, &description) == hud);
    assert(description.hires && description.hires_coverage && description.hires_point_threshold);
    coverage = threshold = 0;
    assert(resolve(0, a, 0, 0, NULL, &description) == hud && !description.hires_coverage && !description.hires_point_threshold);
    hud = 0;
    replacement = 2003;
    mutate(a);
    assert(resolve(0, a, 0, 0, NULL, &description) == replacement && description.levels == 2);
    replacement = 0; mutate(a);
    assert(resolve(0, a, 0, 0, NULL, &description) == first);

    /* Invalid spans must never enter the stage cache or page watcher. */
    old_checks = generation_checks;
    assert(!resolve(0, PLATFORM_CONTIGUOUS_BASE - 1, 0, 0, NULL, &description));
    assert(!resolve(0, PLATFORM_CONTIGUOUS_BASE + PLATFORM_CONTIGUOUS_SIZE - 4, 0, 0, NULL, &description));
    assert(!resolve(0, 0xfffffff0u, 0, 0, NULL, &description));
    assert(!resolve(0, a, 0, 4096, NULL, &description));
    assert(generation_checks == old_checks);
    assert(resolve(0, a, 0, 0, NULL, &description) == first);

    /* Idle eviction frees the cached pointer. Epoch must be checked first. */
    texture_frame = 2399;
    xgpu_texture_cache_begin_frame();
    assert(texture_entry_count == 0 && texture_destroys == texture_creates);
    gpu_texture recreated = resolve(0, a, 0, 0, NULL, &description);
    assert(recreated != first && uploaded[recreated] == 3);
    texture_frame++;
    assert(texture_cache_room(16u * 1024 * 1024)); /* budget eviction also invalidates */
    assert(texture_entry_count == 0);
    assert(resolve(0, a, 0, 0, NULL, &description) != recreated);
cleanup:
    texture_frame++;
    assert(texture_cache_room(16u * 1024 * 1024));
    assert(texture_entry_count == 0 && texture_destroys == texture_creates);
    return 0;
}
'''


def cache_harness(fixture=HARNESS):
    """Compile production cache policy against a deterministic resource fixture."""
    source = read("xbox_textures.c")
    xgpu = read("xgpu.h")
    start = xgpu.index("struct xgpu_texture_description\n{")
    description = xgpu[start : xgpu.index("\n};", start) + 3]
    start = source.index("struct texture_entry\n{")
    types = source[start : source.index("static BOOL texture_cache_room", start)]
    names = [
        "texture_cache_room",
        "bucket_index",
        "palette_hash",
        "texture_entry_result",
        "texture_get",
        "xgpu_texture_get",
        "xgpu_texture_cache_begin_frame",
    ]
    # Failure reporting is covered by test_metal_report, not the cache model.
    functions = "static void texture_not_drawn(const char *why, const struct xgpu_texture_description *d) { (void)why; (void)d; }\n"
    functions += "\n".join(function(source, name) for name in names)
    functions = functions.replace(
        "static unsigned long bucket_index(", "static unsigned long real_bucket_index("
    )
    functions = functions.replace(
        "static unsigned long palette_hash(", "static unsigned long real_palette_hash("
    )
    functions = functions.replace(
        "static unsigned long real_palette_hash(",
        """
static unsigned long bucket_index(DWORD data, DWORD format, DWORD size)
{
    hashes++;
    return real_bucket_index(data, format, size);
}
static unsigned long real_palette_hash(""",
    )
    # Put the palette counter wrapper after its implementation, before consumers.
    palette_body = function(source, "palette_hash").replace(
        "palette_hash(", "real_palette_hash(", 1
    )
    functions = functions.replace(
        palette_body,
        palette_body
        + """
static unsigned long palette_hash(const D3DCOLOR *palette)
{
    palette_hashes++;
    return real_palette_hash(palette);
}
""",
    )
    stage_function = function(source, "xgpu_texture_stage_get")
    functions += "\n" + stage_function + "\n"
    harness = fixture.replace("/* DESCRIPTION */", description).replace(
        "/* CACHE TYPES */", types
    )
    return harness.replace("/* FUNCTIONS */", functions)


class TextureStageCacheTests(unittest.TestCase):
    def test_lookup_coherency(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            self.skipTest("C compiler unavailable")
        harness = cache_harness()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "probe.c"
            path.write_text(harness)
            binary = Path(directory) / "probe"
            command = [compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                       "-fsanitize=address,undefined", *include_flags(), str(path), "-o", str(binary)]
            compiled = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            for arguments in ([], ["no-cache"]):
                with self.subTest(arguments=arguments):
                    result = subprocess.run([str(binary), *arguments], text=True, capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
