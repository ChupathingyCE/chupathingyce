"""The Steam offer's Workshop title (port/linux/game/browser_screen.c,
draw_fitted_lines, utf8_whole): a long title wraps to two lines and ends in
an ellipsis past them, and every cut falls between UTF-8 characters, never
inside one, for a multi-byte title too; a title that came cut short mid
character loses that piece.

The real functions are compiled on their own over a fake text measure (each
character one unit wide) that records the lines drawn. Needs only a C
compiler.
"""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
BROWSER_SCREEN_C = ROOT / "port/linux/game/browser_screen.c"


def function(source, signature):
    """The definition that starts with signature, to its closing brace."""
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end]


PRELUDE = r"""
#include <stdio.h>
#include <string.h>
#define UI_FONT_MENU 0
#define UI_ALIGN_CENTER 0
static char drawn[8][256];
static int drawn_count;
/* one unit a character (a UTF-8 lead byte or a plain one) */
static float ui_overlay_text_width(int font, float size, char const *text)
{
    float width = 0;
    (void)font; (void)size;
    for (; *text; text++)
        if (((unsigned char)*text & 0xC0) != 0x80) width++;
    return width;
}
static float ui_overlay_text_glow(int font, float size, float x, float y, int align, unsigned color,
    unsigned glow, char const *text)
{
    (void)font; (void)size; (void)x; (void)align; (void)color; (void)glow;
    snprintf(drawn[drawn_count++], sizeof(drawn[0]), "%s", text);
    return y;
}
"""

MAIN = r"""
/* whether text is whole UTF-8: every lead byte has all its continuation bytes */
static int whole(char const *text)
{
    while (*text) {
        unsigned char lead = (unsigned char)*text;
        int need = lead < 0x80 ? 0 : lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : -1;
        if (need < 0) return 0;
        text++;
        while (need--) {
            if (((unsigned char)*text & 0xC0) != 0x80) return 0;
            text++;
        }
    }
    return 1;
}

#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void)
{
    /* a Japanese title (3 bytes a character) with spaces, an accented one,
       one with no spaces at all, and emoji (4 bytes) */
    static char const *const titles[] = {
        "Get \xE3\x83\x8F\xE3\x83\xAD\xE3\x83\xBC \xE3\x82\xB3\xE3\x83\xB3\xE3\x83\x90\xE3\x83\x83\xE3\x83\x88 \xE3\x82\xA8\xE3\x83\x9C\xE3\x83\xAB\xE3\x83\x96\xE3\x83\x89 \xE3\x83\x95\xE3\x83\xAB \xE3\x83\xAA\xE3\x83\x9E\xE3\x82\xB9\xE3\x82\xBF\xE3\x83\xBC \xE3\x82\xAD\xE3\x83\xA3\xE3\x83\xB3\xE3\x83\x9A\xE3\x83\xBC\xE3\x83\xB3",
        "Get Op\xC3\xA9ration Fr\xC3\xA9g\xC3\xA2te \xC3\x89" "dition Sp\xC3\xA9" "ciale R\xC3\xA9vis\xC3\xA9" "e Compl\xC3\xA8te",
        "Get \xE3\x83\x8F\xE3\x83\xAD\xE3\x83\xBC\xE3\x82\xB3\xE3\x83\xB3\xE3\x83\x90\xE3\x83\x83\xE3\x83\x88\xE3\x82\xA8\xE3\x83\x9C\xE3\x83\xAB\xE3\x83\x96\xE3\x83\x89\xE3\x83\x95\xE3\x83\xAB\xE3\x83\xAA\xE3\x83\x9E\xE3\x82\xB9\xE3\x82\xBF\xE3\x83\xBC",
        "Get \xF0\x9F\x94\xA5\xF0\x9F\x94\xA5 Blood Gulch \xF0\x9F\x94\xA5\xF0\x9F\x94\xA5\xF0\x9F\x94\xA5 Remix \xF0\x9F\x8E\xAE\xF0\x9F\x8E\xAE\xF0\x9F\x8E\xAE\xF0\x9F\x8E\xAE",
    };
    float widths[] = { 6, 7, 9, 12, 15 };
    unsigned t, w;
    int i;
    char cut[64];

    for (t = 0; t < sizeof(titles) / sizeof(titles[0]); t++) {
        for (w = 0; w < sizeof(widths) / sizeof(widths[0]); w++) {
            drawn_count = 0;
            draw_fitted_lines(titles[t], 0, 0, widths[w], 2);
            CHECK(drawn_count >= 1 && drawn_count <= 2);
            for (i = 0; i < drawn_count; i++) {
                if (!whole(drawn[i])) { printf("title %u width %g line %d cut inside a character\n", t, widths[w], i); return 1; }
                CHECK(ui_overlay_text_width(0, 0, drawn[i]) <= widths[w] || ui_overlay_text_width(0, 0, drawn[i]) == 1);
            }
        }
    }
    /* a short title: one line, as it is */
    drawn_count = 0;
    draw_fitted_lines("Get Op\xC3\xA9ration", 0, 0, 40, 2);
    CHECK(drawn_count == 1 && !strcmp(drawn[0], "Get Op\xC3\xA9ration"));
    /* a long one: two lines, the second ending in the ellipsis */
    drawn_count = 0;
    draw_fitted_lines(titles[1], 0, 0, 12, 2);
    CHECK(drawn_count == 2 && strstr(drawn[1], "\xE2\x80\xA6"));

    /* a title cut short mid character (Steam's, at its buffer) */
    strcpy(cut, "Halo \xE3\x83\x8F\xE3\x83");
    utf8_whole(cut);
    CHECK(!strcmp(cut, "Halo \xE3\x83\x8F"));
    strcpy(cut, "Halo \xF0\x9F\x94");
    utf8_whole(cut);
    CHECK(!strcmp(cut, "Halo "));
    strcpy(cut, "Halo \xC3\xA9");
    utf8_whole(cut);
    CHECK(!strcmp(cut, "Halo \xC3\xA9"));
    printf("workshop_offer_title_test: ok\n");
    return 0;
}
"""


def test_titles_are_cut_between_characters(tmp_path):
    cc = shutil.which("clang") or shutil.which("cc")
    if not cc:
        pytest.skip("no C compiler")
    source = BROWSER_SCREEN_C.read_text(encoding="utf-8")
    program = (PRELUDE +
               function(source, "static long utf8_boundary(\n") +
               function(source, "static void utf8_whole(\n") +
               function(source, "static float draw_fitted_lines(\n") +
               MAIN)
    path = tmp_path / "workshop_offer_title_test.c"
    path.write_text(program, encoding="utf-8")
    binary = tmp_path / "workshop_offer_title_test"
    build = subprocess.run([cc, "-std=gnu11", "-Wall", "-Wno-unused-function", str(path), "-o", str(binary)],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run.returncode == 0 and "workshop_offer_title_test: ok" in run.stdout, run.stdout + run.stderr
