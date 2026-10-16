"""Exercise the actual Mac mirror implementation with deterministic guest writes.

No Metal device or game data needed. Run from the repository root:
python3 -m unittest discover -s port/macos/metal/tests -p 'test_metal_mirror_policy.py'
The extracted C implementation uses mocked memory-watch and upload operations;
this tests policy/coherence decisions, not real GPU lifetime or page protection.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from metal_paths import include_flags, read  # noqa: F401

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "draw_profile.h"
typedef int BOOL;
typedef unsigned int gpu_buffer;
#define TRUE 1
#define FALSE 0
#define PLATFORM_CONTIGUOUS_BASE 0x10000UL
#define PLATFORM_CONTIGUOUS_SIZE 0x800000UL
#define GPU_WRITE_UNUSED 1
static struct { unsigned long frame; } device;
static struct { unsigned long mirrored_bytes; } stats;
static struct draw_profile draw_profile;
static unsigned char guest[PLATFORM_CONTIGUOUS_SIZE];
static unsigned char uploaded[PLATFORM_CONTIGUOUS_SIZE];
/* Host pages deliberately cover four Xbox mirror pages (Apple silicon). */
static unsigned long generations[PLATFORM_CONTIGUOUS_SIZE / 0x4000];
static unsigned long writes, protects;
static unsigned int buffers;
static unsigned long memory_watch_generation(unsigned long address, unsigned long size)
{
    unsigned long first = (address - PLATFORM_CONTIGUOUS_BASE) / 0x4000;
    unsigned long last = (address - PLATFORM_CONTIGUOUS_BASE + size - 1) / 0x4000;
    unsigned long newest = 0;
    for (unsigned long i = first; i <= last; i++)
        if (generations[i] > newest) newest = generations[i];
    return newest;
}
static void memory_watch_protect(unsigned long address, unsigned long size)
{
    assert(address >= PLATFORM_CONTIGUOUS_BASE && size != 0);
    protects++;
}
static void *xbox_pointer(unsigned long address) { return guest + address - PLATFORM_CONTIGUOUS_BASE; }
static gpu_buffer gpu_buffer_create(unsigned long size) { assert(size == 0x400000); return ++buffers; }
static void gpu_buffer_write(gpu_buffer buffer, uint32_t offset, uint32_t size, const void *data, unsigned flags)
{
    (void)flags;
    assert(buffer != 0 && offset + (uint64_t)size <= 0x400000);
    memcpy(uploaded + offset, data, size);
    writes++;
}
/* IMPLEMENTATION */
static BOOL resolve(unsigned long address, unsigned long size)
{
    gpu_buffer buffer = 0;
    unsigned long offset = 0, generation = 0;
    BOOL result = mirror_range(address, size, &buffer, &offset, &generation);
    if (result) {
        assert(buffer != 0);
        assert(memcmp(uploaded + offset, xbox_pointer(address), size) == 0);
    }
    return result;
}
static void reset(void)
{
    memset(&mirror, 0, sizeof(mirror));
    memset(guest, 0, sizeof(guest));
    memset(uploaded, 0, sizeof(uploaded));
    memset(generations, 0, sizeof(generations));
    writes = protects = buffers = 0;
    device.frame = 100;
}
static void mutate(unsigned long offset)
{
    guest[offset]++;
    generations[offset / 0x4000]++;
}
int main(void)
{
    unsigned long address = PLATFORM_CONTIGUOUS_BASE;
    reset();
    assert(resolve(address, 64));
    assert(writes == 1 && protects == 1);
    for (int i = 0; i < 100; i++) { device.frame++; assert(resolve(address, 64)); }
    assert(writes == 1); /* static geometry remains mirrored */
    /* A write after draw A is observed by B with no setter notification. */
    mutate(0);
    assert(resolve(address, 64));
    assert(writes == 2 && uploaded[0] == 1);
    /* Rotating data rewritten every three rendered frames. */
    for (int i = 0; i < 8; i++) {
        device.frame += 3;
        mutate(0);
        assert(!resolve(address, 64));
    }
    assert(writes == 2); /* stale bytes are never returned as a mirror hit */
    device.frame = mirror.rewritten_frame[0] + MIRROR_VOLATILE_FRAMES - 1;
    assert(!resolve(address, 64));
    device.frame++;
    assert(resolve(address, 64)); /* cooldown returns through a fresh upload */
    assert(writes == 3);
    /* Rapidly rewritten pages are still classified as volatile. */
    reset();
    assert(resolve(address, 64));
    for (int i = 1; i <= MIRROR_VOLATILE_REWRITES; i++) {
        mutate(0);
        assert(resolve(address, 64) == (i < MIRROR_VOLATILE_REWRITES));
    }
    assert(!resolve(address, 0x8000)); /* one volatile page streams the range */
    /* Sparse updates must not permanently classify static data as dynamic. */
    reset();
    assert(resolve(address, 64));
    for (int i = 0; i < 8; i++) {
        device.frame += MIRROR_REWRITE_FRAMES + 1;
        mutate(0);
        assert(resolve(address, 64));
    }
    /* Shared 16 KiB host page: changing its neighbour is conservative, safe. */
    reset();
    assert(resolve(address, 0x2000));
    mutate(0x1000);
    assert(resolve(address, 0x2000));
    assert(uploaded[0x1000] == 1);
    assert(!resolve(address - 1, 64));
    assert(!resolve(address, 0));
    assert(!resolve(address + PLATFORM_CONTIGUOUS_SIZE - 1, 2));
    assert(!resolve(address + MIRROR_SEGMENT_SIZE - 1, 2));
    return 0;
}
'''


class MirrorPolicyTests(unittest.TestCase):
    def test_real_mirror(self):
        compiler = shutil.which("clang")
        if not compiler:
            self.skipTest("clang is required")
        source = read("xbox_device.c")
        start = source.index("#define MIRROR_SEGMENT_SIZE")
        end = source.index("/* the smallest and largest index", start)
        implementation = source[start:end]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            test = path / "mirror.c"
            test.write_text(HARNESS.replace("/* IMPLEMENTATION */", implementation))
            executable = path / "mirror"
            subprocess.run([compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", *include_flags(),
                            str(test), "-o", str(executable)], check=True, capture_output=True)
            subprocess.run([str(executable)], check=True, capture_output=True)
