"""A map pack's own movies with its own menus (#990): with game.pack_menus
(Halo CE+'s), the game's movies are looked for first in the bink folder
beside the pack's ui.map, as asked for and then without their language
(port/linux/game/map_packs.c, map_pack_menus_movie_path); without the pack's
menus, or without its movie, the game's own plays (attract_mode.c).

The real function is compiled on its own over a fake set of files. Needs
only a C compiler.
"""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
MAP_PACKS_C = ROOT / "port/linux/game/map_packs.c"


def function(source, signature):
    """The definition that starts with signature, to its closing brace."""
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end]


PRELUDE = r"""
#include <stdio.h>
#include <string.h>
#include <strings.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define NUMBEROF(a) (sizeof(a) / sizeof((a)[0]))
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
static char const *pack = "cex";
static char const *ui_folder = "maps_cex";
static char const *files[8];
static char const *map_pack_menus(void) { return pack; }
static boolean map_pack_file_path(char const *id, char const *file, char *path, long size)
{
    return snprintf(path, (size_t)size, "d:\\%s\\%s.map", ui_folder, file) < size && id && file;
}
static boolean file_exists(char const *path)
{
    int index;
    for (index = 0; index < 8 && files[index]; index++)
        if (!strcasecmp(files[index], path)) return TRUE;
    return FALSE;
}
"""

MAIN = r"""
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void)
{
    char path[64];

    /* the pack's intro, for the game's */
    files[0] = "d:\\maps_cex\\bink\\intro.bik";
    strcpy(path, "d:\\bink\\intro.bik");
    CHECK(map_pack_menus_movie_path(path, path, sizeof(path)) && !strcmp(path, "d:\\maps_cex\\bink\\intro.bik"));
    /* a language the pack doesn't have: its movie without one */
    strcpy(path, "d:\\bink\\intro_de.bik");
    CHECK(map_pack_menus_movie_path(path, path, sizeof(path)) && !strcmp(path, "d:\\maps_cex\\bink\\intro.bik"));
    /* one it has */
    files[1] = "d:\\maps_cex\\bink\\intro_de.bik";
    strcpy(path, "d:\\bink\\intro_de.bik");
    CHECK(map_pack_menus_movie_path(path, path, sizeof(path)) && !strcmp(path, "d:\\maps_cex\\bink\\intro_de.bik"));
    /* a movie the pack lacks: the game's, path untouched */
    strcpy(path, "d:\\bink\\credits.bik");
    CHECK(!map_pack_menus_movie_path(path, path, sizeof(path)) && !strcmp(path, "d:\\bink\\credits.bik"));
    strcpy(path, "d:\\bink\\attract1_fr.bik");
    CHECK(!map_pack_menus_movie_path(path, path, sizeof(path)) && !strcmp(path, "d:\\bink\\attract1_fr.bik"));
    /* the folder that has the pack's ui.map (an alias's) */
    ui_folder = "maps_ceplus";
    files[2] = "d:\\maps_ceplus\\bink\\credits.bik";
    strcpy(path, "d:\\bink\\credits.bik");
    CHECK(map_pack_menus_movie_path(path, path, sizeof(path)) && !strcmp(path, "d:\\maps_ceplus\\bink\\credits.bik"));
    /* without the pack's menus: the game's */
    pack = NULL;
    strcpy(path, "d:\\bink\\credits.bik");
    CHECK(!map_pack_menus_movie_path(path, path, sizeof(path)) && !strcmp(path, "d:\\bink\\credits.bik"));
    printf("pack_movies_test: ok\n");
    return 0;
}
"""


def test_a_packs_menus_play_its_movies(tmp_path):
    cc = shutil.which("clang") or shutil.which("cc")
    if not cc:
        pytest.skip("no C compiler")
    source = MAP_PACKS_C.read_text(encoding="utf-8")
    program = PRELUDE + function(source, "boolean map_pack_menus_movie_path(") + MAIN
    path = tmp_path / "pack_movies_test.c"
    path.write_text(program, encoding="utf-8")
    binary = tmp_path / "pack_movies_test"
    build = subprocess.run([cc, "-std=gnu11", "-Wall", str(path), "-o", str(binary)], capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run.returncode == 0 and "pack_movies_test: ok" in run.stdout, run.stdout + run.stderr
