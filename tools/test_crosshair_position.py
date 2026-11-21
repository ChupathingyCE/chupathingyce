#!/usr/bin/env python3
"""display.crosshair_position (#508): the aim stays under the crosshair.

The Xbox draws a player's crosshair in the middle of its window (the
title-safe frame) and aims through the same point: the view's frustum is
built about the window's middle (render_cameras.c,
render_camera_build_frustum_bounds). "lowered" moves both a tenth of the
view's height below the view's middle. ("centered", the view's middle in
split screen, is retired: display.hud_safe_zone puts a split screen view's
window in its middle now, test_hud_safe_zone.py; a file's "centered" is
rewritten, port_config.c's config_retire_centered_crosshair.) main.c's first_person_aim_offset says how far;
render_player_frame moves the frustum's bounds and crosshairs_draw the
crosshair's window by it.

This builds first_person_aim_offset and render_camera_build_frustum_bounds
with stand-ins, and checks, for a game alone and each split screen view:
    - "retail" (and any other word, "centered" too) changes nothing;
    - the aim point (the frustum's middle, view direction 0) is drawn where
      the crosshair is drawn, for every setting;
    - "lowered" draws both a tenth of the view's height below its middle;
    - zoomed, in a cutscene, or a view that isn't the player's: 0;
and that render.c and hud_weapon.c use the same offset.
Needs clang (or cc) and Python 3 alone.

    python3 -m pytest tools/test_crosshair_position.py
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


PRELUDE = r'''
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef float real;
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define MAXIMUM_NUMBER_OF_LOCAL_PLAYERS 4
#define match_assert(file, line, condition) do { if (!(condition)) abort(); } while (0)
typedef struct { short x0, y0, x1, y1; } rectangle2d;
typedef struct { real x0, x1, y0, y1; } real_rectangle2d;
struct render_camera { rectangle2d viewport_bounds, window_bounds; };
enum { _director_perspective_first_person, _director_perspective_third_person,
    _director_perspective_scripted, _director_perspective_neutral };
static char setting[32] = "retail";
static unsigned long changes = 1;
static boolean cinematic, zoomed;
static short perspective;
static unsigned long config_changes(void) { return changes; }
static int config_text(const char *name, char *text, size_t size) {
    if (strcmp(name, "display.crosshair_position")) abort();
    snprintf(text, size, "%s", setting);
    return 1;
}
static boolean cinematic_in_progress(void) { return cinematic; }
static short player_control_get_zoom_level(short index) { (void)index; return zoomed ? 0 : NONE; }
static short director_get_perspective(short index) { (void)index; return perspective; }
static long fast_ftol(real value) { return (long)lrintf(value); }
'''

MAIN = r'''
static int failures;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL: "); fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); failures++; } } while (0)

/* where a view direction straight ahead (the aim) is drawn, in the
viewport's units, with the frustum's bounds moved as render_player_frame
moves them */
static real aim_drawn_at(struct render_camera const *camera, real offset) {
    real_rectangle2d bounds;
    real shift;

    render_camera_build_frustum_bounds(camera, &bounds);
    shift = 2.0f * offset / (real)(camera->window_bounds.y1 - camera->window_bounds.y0);
    bounds.y0 += shift;
    bounds.y1 += shift;
    /* (bounds are up-positive: the top of the view is y1) */
    return camera->viewport_bounds.y0 +
        (bounds.y1 - 0.0f) / (bounds.y1 - bounds.y0) * (camera->viewport_bounds.y1 - camera->viewport_bounds.y0);
}

static void set(char const *word) { snprintf(setting, sizeof(setting), "%s", word); changes++; }

int main(void) {
    /* a game alone, and a two-player split screen's top and bottom views
    (main.c's compute_window_bounds: the window is the title-safe frame, the
    viewport reaches the screen's edges) */
    struct render_camera views[3] = {
        { { 0, 0, 854, 480 }, { 0, 36, 854, 444 } },
        { { 0, 0, 854, 240 }, { 0, 36, 854, 240 } },
        { { 0, 240, 854, 480 }, { 0, 240, 854, 444 } },
    };
    char const *words[] = { "retail", "centered", "lowered", "sideways" };
    int view, word;

    for (view = 0; view < 3; view++) {
        struct render_camera *camera = &views[view];
        real window_middle = (camera->window_bounds.y0 + camera->window_bounds.y1) * 0.5f;
        real viewport_middle = (camera->viewport_bounds.y0 + camera->viewport_bounds.y1) * 0.5f;
        real height = (real)(camera->viewport_bounds.y1 - camera->viewport_bounds.y0);

        for (word = 0; word < 4; word++) {
            real offset, crosshair, aim;

            set(words[word]);
            perspective = _director_perspective_first_person;
            cinematic = zoomed = FALSE;
            offset = first_person_aim_offset(0, &camera->viewport_bounds, &camera->window_bounds);
            crosshair = window_middle + offset;
            aim = aim_drawn_at(camera, offset);
            printf("view %d %-8s offset %5.1f crosshair %6.1f aim %6.1f\n", view, words[word], offset, crosshair, aim);
            CHECK(fabsf(aim - crosshair) < 0.01f, "view %d %s: aim at %g, crosshair at %g", view, words[word], aim, crosshair);
            if (word != 2)
                CHECK(offset == 0.0f, "view %d %s moved", view, words[word]);
            if (word == 2)
                CHECK(fabsf(crosshair - (viewport_middle + height * 0.1f)) < 0.51f, "view %d lowered wrong", view);
            if (word == 0)
                CHECK(fabsf(aim - window_middle) < 0.01f, "view %d retail aim not in the window's middle", view);

            /* never zoomed, in a cutscene, or from another view */
            zoomed = TRUE;
            CHECK(first_person_aim_offset(0, &camera->viewport_bounds, &camera->window_bounds) == 0.0f, "zoomed moved");
            zoomed = FALSE;
            cinematic = TRUE;
            CHECK(first_person_aim_offset(0, &camera->viewport_bounds, &camera->window_bounds) == 0.0f, "cutscene moved");
            cinematic = FALSE;
            perspective = _director_perspective_neutral;
            CHECK(first_person_aim_offset(0, &camera->viewport_bounds, &camera->window_bounds) == 0.0f, "neutral moved");
            perspective = _director_perspective_first_person;
            CHECK(first_person_aim_offset(NONE, &camera->viewport_bounds, &camera->window_bounds) == 0.0f,
                "no player moved");
        }
    }
    printf("%d failures\n", failures);
    return failures != 0;
}
'''


def build_and_run():
    main = (ROOT / "source/main/main.c").read_text(encoding="utf-8")
    cameras = (ROOT / "source/render/render_cameras.c").read_text(encoding="utf-8")
    code = PRELUDE + function(main, "first_person_aim_offset") + "\n" + \
        function(cameras, "render_camera_build_frustum_bounds") + "\n" + MAIN
    code = code.replace("\textern int config_text(const char *name, char *text, size_t size);\n", "")
    compiler = shutil.which("clang") or shutil.which("cc")
    assert compiler, "needs clang or cc"
    with tempfile.TemporaryDirectory() as work:
        path = Path(work) / "crosshair_position.c"
        path.write_text(code)
        program = Path(work) / "crosshair_position"
        subprocess.run([compiler, "-std=c99", "-Wall", "-Werror", "-Wno-unused-function", str(path), "-lm",
                        "-o", str(program)], check=True)
        return subprocess.run([str(program)], capture_output=True, text=True)


def test_aim_under_the_crosshair():
    result = build_and_run()
    assert result.returncode == 0, result.stdout + result.stderr


def test_both_sides_use_the_offset():
    render = function((ROOT / "source/render/render.c").read_text(encoding="utf-8"), "render_player_frame")
    assert "first_person_aim_offset(" in render
    # (moved before a tiled screenshot's tile is cut from the bounds)
    assert render.index("first_person_aim_offset(") < render.index("screenshot_combined_index != NULL")
    hud = function((ROOT / "source/interface/hud_weapon.c").read_text(encoding="utf-8"), "crosshairs_draw")
    assert "first_person_aim_offset(" in hud
    assert "render.camera.window_bounds = saved_window_bounds;" in hud
    config = (ROOT / "port/linux/src/port_config.c").read_text(encoding="utf-8")
    assert re.search(r'\{ "display\.crosshair_position", _config_string, "\\"retail\\"", "HALO_CROSSHAIR_POSITION"',
                     config)
    assert '{ "display.crosshair_position", { "retail", "lowered", NULL }, "\\"retail\\"" }' in config
    rows = (ROOT / "port/linux/src/settings_rows.c").read_text(encoding="utf-8")
    assert 'crosshair_position_choices[] = { "retail", "lowered", NULL };' in rows
    assert 'crosshair_position_names[] = { "XBOX", "LOWERED", NULL };' in rows


def test_centered_retired():
    # (a file's "centered" becomes "retail", and its "xbox" safe zone "auto",
    # as it is read: before its values are taken)
    config = (ROOT / "port/linux/src/port_config.c").read_text(encoding="utf-8")
    retire = function(config, "config_retire_centered_crosshair")
    assert 'strcmp(position.u.s, "centered")' in retire
    assert '"crosshair_position = \\"retail\\"\\n"' in retire
    assert '"hud_safe_zone = \\"auto\\"\\n"' in retire
    assert "platform_log(" in retire
    load = config[config.index("config_retire_centered_crosshair(text, result.toptab)"):]
    assert load.index("updated = text;") < load.index("config_set_from_file(")


if __name__ == "__main__":
    result = build_and_run()
    print(result.stdout + result.stderr)
