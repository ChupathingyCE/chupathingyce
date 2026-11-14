"""A joining client's MCC base resources are picked by the host's SHA-256
(port/linux/game/mcc_resources.c, mcc_resources_candidates and
mcc_resources_fingerprint_match; mcc_package.c's base sets). From chester's
GulchCE fix.

A machine can hold several base sets that each satisfy a map (the legacy
root, MCC's singleplayer and multiplayer sets). The loader took the first
or refused them as ambiguous, and the client's fingerprint then differed
from the host's. Now every distinct usable combination is fingerprinted and
the one equal to the host's digest is kept; a package's own resource file
is the only choice for its type, and a broken one fails at once.

The real functions are compiled on their own over files in a temporary
folder: a file's text says whether it satisfies the map ("ok ...") and is
what the fake fingerprint hashes. Needs only a C compiler.
"""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
PACKAGE_C = ROOT / "port/linux/game/mcc_package.c"
RESOURCES_C = ROOT / "port/linux/game/mcc_resources.c"


def function(source, signature):
    """The definition that starts with signature, to its closing brace."""
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end]


PRELUDE = r"""
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define MCC_PACKAGE_BASE_SETS 3
#define MCC_CACHE_HEADER_BYTES 16
enum mcc_cache_status { MCC_CACHE_OK, MCC_CACHE_READ_FAILED, MCC_CACHE_NO_MEMORY };
struct mcc_cache_identity { uint32_t tag_data_size; };
struct mcc_cache_source { int (*read)(void *, uint32_t, uint32_t, void *); void *context; uint32_t size; };
struct mcc_report { unsigned external_bitmap_count, external_sound_count; };
struct mcc_resources { char used[2][128]; };
struct mcc_runtime {
    char level_name[64];
    struct mcc_cache_source source;
    struct mcc_report report;
    struct mcc_resources *resources;
    char failure[320];
};
struct mcc_resource_file { FILE *stream; char path[128]; char text[64]; };
struct mcc_resource_candidates { unsigned count[2]; char paths[2][MCC_PACKAGE_BASE_SETS][128]; };
struct mcc_resource_probe { struct mcc_resource_file *file; unsigned type; struct mcc_resource_file *shared; };
struct mcc_resource_reference { int unused; };
#define _error_silent 0
static void error(int priority, char const *format, ...) { (void)priority; (void)format; }
static unsigned bitmaps = 1, sounds = 1;
static int selections, fingerprints;

static int source_read(void *context, uint32_t offset, uint32_t size, void *out)
{ (void)context; (void)offset; memset(out, 0, size); return 1; }
static int mcc_package_name(char const *name, char *canonical) { strcpy(canonical, name); return 1; }
/* (the package's own folder: pack/) */
static int mcc_package_resource_path(char const *name, unsigned type, char path[128])
{
    if (type < 1 || type > 2) return 0;
    if (!strcmp(name, "mcc_maps/root")) snprintf(path, 128, "maps_mcc/%s.map", type == 1 ? "bitmaps" : "sounds");
    else snprintf(path, 128, "maps_mcc/pack/%s.map", type == 1 ? "bitmaps" : "sounds");
    return 1;
}
static char const *mcc_maps_folder(void) { return "mcc_maps"; }
static char const *mcc_status_explanation(enum mcc_cache_status status) { (void)status; return "unreadable"; }
static enum mcc_cache_status mcc_cache_identify(void const *h, size_t n, uint32_t size, struct mcc_cache_identity *id)
{ (void)h; (void)n; (void)size; id->tag_data_size = 16; return MCC_CACHE_OK; }
static enum mcc_cache_status mcc_cache_load(struct mcc_cache_source const *s, void *raw, uint32_t n, struct mcc_report *r)
{ (void)s; (void)raw; (void)n; r->external_bitmap_count = bitmaps; r->external_sound_count = sounds; return MCC_CACHE_OK; }
static void mcc_resource_close(struct mcc_resource_file *file)
{ if (file->stream) fclose(file->stream); memset(file, 0, sizeof(*file)); }
static int mcc_resource_open(struct mcc_resource_file *file, unsigned type, char const *level, char const *path,
    char *reason, size_t size)
{
    (void)type; (void)level;
    errno = 0;
    file->stream = fopen(path, "rb");
    if (!file->stream) return errno == ENOENT ? -1 : 0;
    snprintf(file->path, sizeof(file->path), "%s", path);
    if (!fgets(file->text, sizeof(file->text), file->stream) || !strncmp(file->text, "broken", 6)) {
        if (reason && size) snprintf(reason, size, "%s is invalid", path);
        mcc_resource_close(file);
        return 0;
    }
    return 1;
}
static int mcc_resource_check(void *context, struct mcc_resource_reference const *ref)
{ struct mcc_resource_probe *probe = context; (void)ref; return !strncmp(probe->file->text, "ok", 2); }
static int mcc_cache_visit_resources(void const *raw, struct mcc_report const *report,
    int (*visit)(void *, struct mcc_resource_reference const *), void *context)
{ struct mcc_resource_reference ref = { 0 }; (void)raw; (void)report; return visit(context, &ref); }
static void mcc_resources_dispose(struct mcc_runtime *runtime) { free(runtime->resources); runtime->resources = NULL; }
static int mcc_resources_select_paths(struct mcc_runtime *runtime, void *raw, char const paths[2][128])
{
    (void)raw;
    selections++;
    runtime->resources = calloc(1, sizeof(*runtime->resources));
    memcpy(runtime->resources->used, paths, sizeof(runtime->resources->used));
    return 1;
}
/* the digest: the selected files' first lines */
static int mcc_resources_fingerprint(struct mcc_runtime *runtime, unsigned char digest[32])
{
    unsigned type;
    fingerprints++;
    memset(digest, 0, 32);
    for (type = 0; type < 2; type++) {
        char line[64] = "";
        FILE *file = runtime->resources->used[type][0] ? fopen(runtime->resources->used[type], "rb") : NULL;
        if (file) { if (!fgets(line, sizeof(line), file)) line[0] = 0; fclose(file); }
        memcpy(digest + type * 16, line, strlen(line) < 16 ? strlen(line) : 16);
    }
    return 1;
}
"""

MAIN = r"""
static void put(char const *path, char const *text)
{
    FILE *file = fopen(path, "wb");
    fputs(text, file);
    fclose(file);
}

static int match(char const *bitmap_text, char const *sound_text, char paths[2][128], char *reason)
{
    struct mcc_cache_source source = { source_read, NULL, 1024 };
    unsigned char digest[32];
    memset(digest, 0, 32);
    memcpy(digest, bitmap_text, strlen(bitmap_text) < 16 ? strlen(bitmap_text) : 16);
    memcpy(digest + 16, sound_text, strlen(sound_text) < 16 ? strlen(sound_text) : 16);
    memset(paths, 0, 2 * 128);
    return mcc_resources_fingerprint_match("mcc_maps/pack/level", &source, digest, paths, reason, 320);
}

#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void)
{
    char paths[2][128], reason[320];
    put("mcc_maps/_shared/singleplayer/bitmaps.map", "ok sp bitmaps\n");
    put("mcc_maps/_shared/multiplayer/bitmaps.map", "ok mp bitmaps\n");
    put("mcc_maps/_shared/multiplayer/sounds.map", "ok mp sounds\n");
    put("maps_mcc/sounds.map", "ok root sounds\n");

    /* the host used the multiplayer bitmaps and the root sounds */
    CHECK(match("ok mp bitmaps\n", "ok root sounds\n", paths, reason) == 1);
    CHECK(!strcmp(paths[0], "mcc_maps/_shared/multiplayer/bitmaps.map"));
    CHECK(!strcmp(paths[1], "maps_mcc/sounds.map"));
    /* the singleplayer set */
    CHECK(match("ok sp bitmaps\n", "ok mp sounds\n", paths, reason) == 1);
    CHECK(!strcmp(paths[0], "mcc_maps/_shared/singleplayer/bitmaps.map"));
    CHECK(!strcmp(paths[1], "mcc_maps/_shared/multiplayer/sounds.map"));
    /* no combination is the host's: none, not an error */
    CHECK(match("ok other\n", "ok mp sounds\n", paths, reason) == 0);
    CHECK(!paths[0][0]);

    /* a set that doesn't satisfy the map is never a choice */
    put("mcc_maps/_shared/singleplayer/bitmaps.map", "missing references\n");
    selections = 0;
    CHECK(match("missing references\n", "ok mp sounds\n", paths, reason) == 0);
    /* (one bitmaps choice left, two sounds: two combinations) */
    CHECK(selections == 2);

    /* a package's own file is the only choice for its type */
    put("maps_mcc/pack/bitmaps.map", "ok pack bitmaps\n");
    CHECK(match("ok mp bitmaps\n", "ok mp sounds\n", paths, reason) == 0);
    CHECK(match("ok pack bitmaps\n", "ok mp sounds\n", paths, reason) == 1);
    CHECK(!strcmp(paths[0], "maps_mcc/pack/bitmaps.map"));

    /* a broken package file fails at once, with why */
    put("maps_mcc/pack/bitmaps.map", "broken\n");
    selections = 0;
    CHECK(match("ok mp bitmaps\n", "ok mp sounds\n", paths, reason) == -1);
    CHECK(selections == 0 && strstr(reason, "invalid"));

    /* a map with no sounds: one empty sounds choice */
    remove("maps_mcc/pack/bitmaps.map");
    sounds = 0;
    CHECK(match("ok mp bitmaps\n", "", paths, reason) == 1);
    CHECK(!strcmp(paths[0], "mcc_maps/_shared/multiplayer/bitmaps.map") && !paths[1][0]);
    printf("mcc_resource_candidates_test: ok\n");
    return 0;
}
"""


def test_base_resources_follow_the_hosts_digest(tmp_path):
    cc = shutil.which("clang") or shutil.which("cc")
    if not cc:
        pytest.skip("no C compiler")
    package = PACKAGE_C.read_text(encoding="utf-8")
    resources = RESOURCES_C.read_text(encoding="utf-8")
    program = (PRELUDE +
               function(package, "int mcc_package_base_resource_path(") +
               function(package, "int mcc_package_shared_resource(") +
               function(package, "int mcc_package_resource_candidate(") +
               function(resources, "static int mcc_resources_candidates(") +
               function(resources, "int mcc_resources_fingerprint_match(") +
               MAIN)
    path = tmp_path / "mcc_resource_candidates_test.c"
    path.write_text(program, encoding="utf-8")
    binary = tmp_path / "mcc_resource_candidates_test"
    build = subprocess.run([cc, "-std=gnu11", "-Wall", "-Wno-unused-function", str(path), "-o", str(binary)],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    for folder in ("mcc_maps/_shared/singleplayer", "mcc_maps/_shared/multiplayer", "maps_mcc/pack"):
        (tmp_path / folder).mkdir(parents=True, exist_ok=True)
    run = subprocess.run([str(binary)], capture_output=True, text=True, cwd=tmp_path)
    assert run.returncode == 0 and "mcc_resource_candidates_test: ok" in run.stdout, run.stdout + run.stderr
