"""Metal map shader lists: learned lists, warming at map load and the
pipeline archive kept across launches (shader_list_learn.h, d3d8_device.c,
gpu_metal.m)."""
import shutil
import subprocess
import tempfile
from pathlib import Path
from unittest import TestCase, skipUnless

ROOT = Path(__file__).resolve().parents[1]
METAL = ROOT / "port/macos/metal"

DRIVER = r'''
#include "shader_list_learn.h"
#include <assert.h>
int main(void) {
    char name[16], text[64];
    size_t n;
    assert(metal_shader_list_map_name("levels\\test\\bloodgulch\\bloodgulch", name, sizeof(name)));
    assert(!strcmp(name, "bloodgulch"));
    assert(metal_shader_list_map_name("maps/ui", name, sizeof(name)) && !strcmp(name, "ui"));
    assert(metal_shader_list_map_name("plain", name, sizeof(name)) && !strcmp(name, "plain"));
    assert(!metal_shader_list_map_name("", name, sizeof(name)) && !name[0]);
    assert(!metal_shader_list_map_name("maps/", name, sizeof(name)) && !name[0]);
    assert(!metal_shader_list_map_name(NULL, name, sizeof(name)));
    assert(!metal_shader_list_map_name("x", name, 0));
    assert(metal_shader_list_map_name("a/abcdefghijklmnopqrstuvwxyz", name, sizeof(name)));
    assert(strlen(name) == 15 && !strncmp(name, "abcdefghijklmno", 15));

    /* joins, with a newline only where needed */
    n = metal_shader_list_join("a\n", 2, "b\n", 2, text, sizeof(text));
    assert(n == 4 && !memcmp(text, "a\nb\n", 4));
    n = metal_shader_list_join("a", 1, "b", 1, text, sizeof(text));
    assert(n == 3 && !memcmp(text, "a\nb", 3));
    assert(metal_shader_list_join(NULL, 0, "b", 1, text, sizeof(text)) == 1 && text[0] == 'b');
    assert(metal_shader_list_join("a", 1, NULL, 0, text, sizeof(text)) == 1 && text[0] == 'a');
    assert(metal_shader_list_join(NULL, 0, NULL, 0, text, sizeof(text)) == 0);
    /* measuring, and too small: nothing written */
    assert(metal_shader_list_join("abc", 3, "def", 3, NULL, 0) == 7);
    text[0] = 'z';
    assert(metal_shader_list_join("abc", 3, "def", 3, text, 6) == 7 && text[0] == 'z');
    return 0;
}
'''


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    return source[start:source.index("\n}\n", start)]


class ShaderListTests(TestCase):
    @skipUnless(shutil.which("clang"), "clang required")
    def test_name_and_join(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "lists.c"
            binary = Path(folder) / "lists"
            source.write_text(DRIVER)
            subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                            "-I", str(METAL / "renderer"), str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_map_load_warms_and_learns(self):
        source = (METAL / "renderer/d3d8_device.c").read_text()
        present = body(source, "void WINAPI D3DDevice_Present(")
        self.assertIn("halo_shader_list_warm(loaded)", present)
        self.assertIn("cache_file_loaded_map_name()", present)
        warm = body(source, "void halo_shader_list_warm(char const *map_name)")
        self.assertIn("gpu_warm_list_read(", warm)
        self.assertIn("shader_list_directory()", warm)
        self.assertIn("metal_shader_list_join(", warm)
        directory = body(source, "static const char *shader_list_directory(void)")
        self.assertIn('"%s/metal-shader-lists", platform_data_root()', directory)
        self.assertIn('config_boolean("debug.shader_list_warm")', directory)

    def test_archive_saved_at_exit(self):
        source = (METAL / "gpu_metal.m").read_text()
        self.assertIn("atexit(archive_save_at_exit);", body(source, "static void gpu_metal_initialize("))
        self.assertIn("archive_save();", body(source, "static void archive_save_at_exit(void)"))
