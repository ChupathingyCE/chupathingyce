#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
"""display.hud_safe_zone: how far the HUD sits from the screen's edges.

The real compute_window_bounds and compute_subframe_counts (main.c) and
hud_safe_zone_margins and hud_safe_zone_bounds (hud_draw.c) are compiled on
their own, over a 640x480 screen and an 854x480 one, for one to four players:

- "xbox" (the consoles' default; and no setting, and a word it doesn't
  know) gives the window itself, every edge, as the Xbox lays the HUD out;
- the Xbox's own split screen is lopsided: a view's edges on the screen's
  edges are 48 columns or 36 lines in, its edges by the lines between views
  4 pixels, so the HUD of the top left view sits far from the left and close
  to the middle (as the retail compute_window_bounds does);
- "pc" moves only the screen's edges, to 8 in (scaled across a wider
  screen as the Xbox's frame is), and leaves the 4 pixels between views;
- "auto" (the desktop's and Android's default) is the window alone and
  "pc" in split screen;
- two numbers are clamped to 8..48 and 8..36.

Needs only a C compiler. Usage: test_hud_safe_zone.py
"""
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def function(source, name):
    match = re.search(r"^(?:static )?[\w *]+\b" + name + r"\(\s*[^;{]*\)\s*\{", source, re.M)
    assert match, f"function not found: {name}"
    start = source.index("{", match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def block(source, first, last):
    start = source.index(first)
    return source[start:source.index(last, start) + len(last)]


PRELUDE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define PIN(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define match_assert(file, line, condition) do { if (!(condition)) abort(); } while (0)
typedef union { short v[4]; struct { short y0, x0, y1, x1; }; } rectangle2d;
static struct { struct { rectangle2d screen_bounds, frame_bounds; } reserved04; } rasterizer_globals;
static struct { struct { rectangle2d viewport_bounds, window_bounds; } camera; } render;
static char setting[32];
static unsigned long changes;
static short window_count;
int config_text(const char *name, char *text, size_t size)
{
    if (strcmp(name, "display.hud_safe_zone")) abort();
    if (!setting[0]) return 0;
    snprintf(text, size, "%s", setting);
    return 1;
}
unsigned long config_changes(void) { return changes; }
short main_get_window_count(void) { return window_count; }
'''

MAIN = r'''
static int failures;
#define CHECK(condition, what) do { if (!(condition)) { failures++; \
    printf("FAIL %s: %s (players %ld, width %d, \"%s\")\n", what, #condition, players, width, setting); } } while (0)

static void use(const char *value)
{
    snprintf(setting, sizeof(setting), "%s", value);
    changes++;
}

int main(void)
{
    static const int widths[] = { 640, 854 };
    int w;

    for (w = 0; w < 2; w++)
    {
        int width = widths[w];
        long players;
        rectangle2d *screen = &rasterizer_globals.reserved04.screen_bounds;
        rectangle2d *frame = &rasterizer_globals.reserved04.frame_bounds;

        /* (as _rasterizer_initialize sets them) */
        screen->x0 = 0; screen->y0 = 0; screen->x1 = (short)width; screen->y1 = 480;
        frame->x0 = (short)(48 * width / 640); frame->x1 = (short)(width - frame->x0);
        frame->y0 = 36; frame->y1 = 444;
        for (players = 1; players <= 4; players++)
        {
            long index;

            window_count = (short)players;
            for (index = 0; index < players; index++)
            {
                rectangle2d viewport, window, bounds;
                short pc_x = (short)(frame->x0 - 8 * width / 640), pc_y = 28;
                short custom_x = (short)(frame->x0 - 24 * width / 640), custom_y = 18;
                int left = 0, right = 0, top = 0, bottom = 0;

                compute_window_bounds(index, players, &viewport, &window);
                render.camera.viewport_bounds = viewport;
                render.camera.window_bounds = window;
                left = viewport.x0 == 0; right = viewport.x1 == width;
                top = viewport.y0 == 0; bottom = viewport.y1 == 480;

                /* the Xbox's layout: the screen's edges the frame's, the
                others 4 from the line between views */
                CHECK(window.x0 == (left ? frame->x0 : viewport.x0 + 4), "xbox left");
                /* (a lone column's right edge 4 more, as on the Xbox:
                compute_window_bounds takes them off column 0's right) */
                CHECK(window.x1 == (right ? frame->x1 - (index == 0 || players == 2 ? (players > 1 ? 4 : 0) : 0) :
                    viewport.x1 - 4), "xbox right");
                CHECK(window.y0 == (top ? frame->y0 : viewport.y0 + 4), "xbox top");
                CHECK(window.y1 == (bottom ? frame->y1 : viewport.y1 - 4), "xbox bottom");

                use("");
                hud_safe_zone_bounds(&bounds);
                CHECK(!memcmp(&bounds, &window, sizeof(bounds)), "no setting is the Xbox's");
                use("xbox");
                hud_safe_zone_bounds(&bounds);
                CHECK(!memcmp(&bounds, &window, sizeof(bounds)), "xbox");
                use("sideways");
                hud_safe_zone_bounds(&bounds);
                CHECK(!memcmp(&bounds, &window, sizeof(bounds)), "another word is the Xbox's");

                use("pc");
                hud_safe_zone_bounds(&bounds);
                CHECK(bounds.x0 == window.x0 - (left ? pc_x : 0), "pc left");
                CHECK(bounds.x1 == window.x1 + (right ? pc_x : 0), "pc right");
                CHECK(bounds.y0 == window.y0 - (top ? pc_y : 0), "pc top");
                CHECK(bounds.y1 == window.y1 + (bottom ? pc_y : 0), "pc bottom");
                if (left)
                    CHECK(bounds.x0 == 8 * width / 640, "pc 8 in");
                if (top)
                    CHECK(bounds.y0 == 8, "pc 8 down");

                use("auto");
                hud_safe_zone_bounds(&bounds);
                if (players == 1)
                    CHECK(!memcmp(&bounds, &window, sizeof(bounds)), "auto alone");
                else
                    CHECK(bounds.x0 == window.x0 - (left ? pc_x : 0) && bounds.y1 == window.y1 + (bottom ? pc_y : 0),
                        "auto in split screen");

                use("24 18");
                hud_safe_zone_bounds(&bounds);
                CHECK(bounds.x0 == window.x0 - (left ? custom_x : 0) && bounds.y0 == window.y0 - (top ? custom_y : 0),
                    "two numbers");
                use("0,99");
                hud_safe_zone_bounds(&bounds);
                CHECK(bounds.x0 == window.x0 - (left ? pc_x : 0) && bounds.y0 == window.y0,
                    "two numbers clamped");
            }
        }
    }
    printf("%d failures\n", failures);
    return failures != 0;
}
'''


def build_and_run():
    main = (ROOT / "source/main/main.c").read_text(encoding="utf-8")
    hud = (ROOT / "source/interface/hud_draw.c").read_text(encoding="utf-8")
    safe = block(hud, "enum\n{\n\tHUD_SAFE_ZONE_XBOX_COLUMNS", "\n#ifdef HALO_CONSOLE_PERFORMANCE_MODE")
    safe = safe[:-len("\n#ifdef HALO_CONSOLE_PERFORMANCE_MODE")]
    safe = re.sub(r"^(int config_text|unsigned long config_changes|short main_get_window_count)\(.*\);\n", "",
                  safe, flags=re.M)
    code = PRELUDE + function(main, "compute_subframe_counts") + "\n" + \
        function(main, "compute_window_bounds") + "\n" + safe + "\n" + MAIN
    compiler = shutil.which("clang") or shutil.which("cc")
    assert compiler, "needs clang or cc"
    with tempfile.TemporaryDirectory() as work:
        path = Path(work) / "hud_safe_zone.c"
        path.write_text(code)
        program = Path(work) / "hud_safe_zone"
        subprocess.run([compiler, "-std=c11", "-Wall", "-Werror", "-Wno-unused-function", str(path),
                        "-o", str(program)], check=True)
        return subprocess.run([str(program)], capture_output=True, text=True)


def test_hud_safe_zone():
    result = build_and_run()
    assert result.returncode == 0, result.stdout + result.stderr


def test_wired_and_registered():
    hud = (ROOT / "source/interface/hud_draw.c").read_text(encoding="utf-8")
    point = function(hud, "hud_calculate_point")
    corner = point[point.index("if (corner < _hud_anchor_center)"):point.index("else", point.index("if (corner <"))]
    # (the corners in the safe frame; the middle, the crosshair's, in the window)
    assert "hud_safe_zone_bounds(&safe_bounds);" in corner
    assert "render.camera.window_bounds.v[" not in corner
    assert "render.camera.window_bounds.x1 + render.camera.window_bounds.x0" in point
    config = (ROOT / "port/linux/src/port_config.c").read_text(encoding="utf-8")
    assert re.search(r'\{ "display\.hud_safe_zone", _config_string, DEFAULT_HUD_SAFE_ZONE, "HALO_HUD_SAFE_ZONE", '
                     r'_environment_value,\s*_platform_all \| _platform_xbox,', config)
    # ("auto" on the desktop and Android; the Xbox's own on the consoles,
    # whose televisions overscan)
    default = config[config.index("#if defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE)"):]
    default = default[:default.index("#endif") + len("#endif")]
    assert "defined(HALO_XBOX_CONSOLE)" in default and "defined(HALO_XBOX360_CONSOLE)" in default
    assert "defined(HALO_WIIU_CONSOLE)" in default
    assert re.search(r'^#define DEFAULT_HUD_SAFE_ZONE "\\"xbox\\""\n#else\n#define DEFAULT_HUD_SAFE_ZONE "\\"auto\\""$',
                     default, re.M), default
    settings = (ROOT / "tools/port_settings.py").read_text(encoding="utf-8")
    assert '[("ON", "xbox"), ("SINGLE SCREEN ONLY", "auto"), ("OFF", "pc")]' in settings


if __name__ == "__main__":
    result = build_and_run()
    print(result.stdout + result.stderr)
    raise SystemExit(result.returncode)
