#!/usr/bin/env python3
"""A zoomed view is drawn at the Xbox's width at any field of view (#1666).

display.field_of_view widens the first-person view by scaling the tangent of
the camera's half angle (main.c, set_window_camera_values). The scope's
picture (its mask, reticle, ticks and zoom label) is drawn at the Xbox's
size while zoomed, so a zoomed world widened by the setting no longer
matched it: at 95 the zoomed reticle covered 1.56 times the angle it does
on the Xbox (2.04 at 110), and its marks no longer lined up with the target.
local_view_zoom_weight takes the widening back while zoomed, easing with the
zoom itself.

This builds main.c's local_view_zoom_weight and
local_view_field_of_view_update with stand-ins, and checks:
    - at 70 (the Xbox's), the view's scale is exactly 1 in every state;
    - unzoomed, the view keeps the setting's full scale;
    - at each zoom level (2x, 8x), the drawn view is the Xbox's: the zoomed
      reticle, drawn at the Xbox's size, covers the same angle as there;
    - while the zoom eases in or out, the scale moves between the two;
    - in a seat, or holding a weapon that doesn't zoom, as before;
and that the camera and the first-person weapon use it.
Needs clang (or cc) and Python 3 alone.

    python3 -m pytest tools/test_zoomed_field_of_view.py
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
typedef float real;
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define MAXIMUM_NUMBER_OF_LOCAL_PLAYERS 4
#define PIN(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define valid_real(x) (!isnan(x) && !isinf(x))
#define VEHICLE_VIEW_EASE_MILLISECONDS 500
#define DEGREES_TO_RADIANS(d) ((d) * 3.14159265f / 180.0f)
enum { _director_perspective_first_person, _director_perspective_third_person,
    _director_perspective_scripted, _director_perspective_neutral };
struct unit_definition { struct { real camera_field_of_view; } unit; };
struct unit_datum { long definition_index; struct { short current_weapon_index; } unit; };
static struct unit_definition cyborg = { { 0 } };
static struct unit_datum player_unit = { 0, { 0 } };
static boolean seated, has_unit = TRUE, has_weapon = TRUE;
static real first_zoom = 2.0f;
static real setting_scale = 1.0f;
static long player_control_get_unit_index(short index) { (void)index; return has_unit ? 7 : NONE; }
static boolean local_player_seated(short index) { (void)index; return seated; }
static struct unit_datum *unit_get(long index) { (void)index; return &player_unit; }
#define unit_definition_get(index) (&cyborg)
static long unit_inventory_get_weapon(long unit, short index) { (void)unit; (void)index; return has_weapon ? 9 : NONE; }
static real weapon_get_zoom_magnification(long weapon, short level) { (void)weapon; (void)level; return first_zoom; }
static short director_get_perspective(short index) { (void)index; return _director_perspective_first_person; }
static unsigned long system_milliseconds(void) { return 1000; }
static real first_person_field_of_view_scale(void) { return setting_scale; }
static real vehicle_field_of_view_scale(short perspective) { (void)perspective; return setting_scale; }
'''

MAIN = r'''
static int failures;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL: "); fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); failures++; } } while (0)

/* the setting's tangent scale, as first_person_field_of_view_scale works it out */
static real scale_for(real degrees) {
    return degrees == 70.0f ? 1.0f :
        tanf(DEGREES_TO_RADIANS(degrees) * 0.5f) / tanf(DEGREES_TO_RADIANS(70.0f) * 0.5f);
}

/* the drawn view's vertical tangent (set_window_camera_values) */
static real drawn(real observer_field_of_view) {
    return 0.75f * 0.85f * tanf(observer_field_of_view * 0.5f) *
        local_view_field_of_view_update(0, observer_field_of_view);
}

static real retail(real observer_field_of_view) {
    return 0.75f * 0.85f * tanf(observer_field_of_view * 0.5f);
}

int main(void) {
    real const unit_fov = DEGREES_TO_RADIANS(70.0f);
    real const settings[] = { 50.0f, 70.0f, 95.0f, 110.0f, 120.0f };
    real const zooms[] = { 2.0f, 8.0f };
    int s, z, step;

    cyborg.unit.camera_field_of_view = unit_fov;
    for (s = 0; s < 5; s++) {
        real scale = scale_for(settings[s]);

        setting_scale = scale;
        seated = FALSE; has_unit = TRUE; has_weapon = TRUE; first_zoom = 2.0f;

        /* unzoomed: the setting's whole scale */
        CHECK(local_view_field_of_view_update(0, unit_fov) == scale, "%g unzoomed: %g, not %g",
            settings[s], local_view_field_of_view_update(0, unit_fov), scale);

        for (z = 0; z < 2; z++) {
            real zoomed_fov = unit_fov / zooms[z];
            real coverage = drawn(zoomed_fov) / retail(zoomed_fov);

            printf("fov %5.1f zoom %gx: reticle covers %.4f of the Xbox's angle (was %.4f)\n",
                settings[s], zooms[z], coverage, scale);
            /* zoomed: the Xbox's view, exactly; at 70 nothing at all changes */
            CHECK(local_view_field_of_view_update(0, zoomed_fov) == 1.0f, "%g at %gx: scale %g",
                settings[s], zooms[z], local_view_field_of_view_update(0, zoomed_fov));
        }

        /* easing in and out of 2x: between the two, never past either */
        {
            real previous = 1.0f;
            for (step = 0; step <= 20; step++) {
                real fov = unit_fov / 2.0f + (unit_fov - unit_fov / 2.0f) * step / 20.0f;
                real value = local_view_field_of_view_update(0, fov);

                CHECK(value >= fminf(1.0f, scale) - 1e-6f && value <= fmaxf(1.0f, scale) + 1e-6f,
                    "%g easing: %g out of range", settings[s], value);
                if (scale >= 1.0f)
                    CHECK(value >= previous - 1e-6f, "%g easing: not monotonic", settings[s]);
                else
                    CHECK(value <= previous + 1e-6f, "%g easing: not monotonic", settings[s]);
                previous = value;
            }
            CHECK(previous == scale, "%g: the ease doesn't end at the setting", settings[s]);
        }

        /* as before: in a seat, without a weapon or with one that doesn't zoom */
        seated = TRUE;
        CHECK(local_view_field_of_view_update(0, unit_fov / 2.0f) == scale, "%g seated changed", settings[s]);
        seated = FALSE;
        has_weapon = FALSE;
        CHECK(local_view_field_of_view_update(0, unit_fov / 2.0f) == scale, "%g no weapon changed", settings[s]);
        has_weapon = TRUE;
        first_zoom = 1.0f;
        CHECK(local_view_field_of_view_update(0, unit_fov / 2.0f) == scale, "%g no zoom changed", settings[s]);
        first_zoom = 2.0f;
        has_unit = FALSE;
        CHECK(local_view_field_of_view_update(0, unit_fov / 2.0f) == scale, "%g no unit changed", settings[s]);
        has_unit = TRUE;
    }
    printf("%d failures\n", failures);
    return failures != 0;
}
'''


def statics(source):
    """the per-player arrays local_view_field_of_view_update keeps"""
    lines = [line for line in source.splitlines()
             if re.match(r"static (real|unsigned long) local_view_\w+\[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS\]", line)]
    assert len(lines) >= 4, lines
    return "\n".join(lines) + "\n"


def build_and_run():
    main = (ROOT / "source/main/main.c").read_text(encoding="utf-8")
    code = PRELUDE + statics(main) + function(main, "local_view_zoom_weight") + "\n" + \
        function(main, "local_view_field_of_view_update") + "\n" + MAIN
    compiler = shutil.which("clang") or shutil.which("cc")
    assert compiler, "needs clang or cc"
    with tempfile.TemporaryDirectory() as work:
        path = Path(work) / "zoomed_field_of_view.c"
        path.write_text(code)
        program = Path(work) / "zoomed_field_of_view"
        subprocess.run([compiler, "-std=c99", "-Wall", "-Werror", "-Wno-unused-function",
                        "-Wno-unused-variable", str(path), "-lm", "-o", str(program)], check=True)
        return subprocess.run([str(program)], capture_output=True, text=True)


def test_zoomed_view_is_the_xboxs():
    result = build_and_run()
    assert result.returncode == 0, result.stdout + result.stderr


def test_camera_and_weapon_use_it():
    main = (ROOT / "source/main/main.c").read_text(encoding="utf-8")
    camera = function(main, "set_window_camera_values")
    assert "local_view_field_of_view_update(window->local_player_index, observer->field_of_view)" in camera
    # (the first-person weapon is drawn in the world's frustum: its own
    # width follows the same weight while a zoom eases out)
    weapon = function(main, "first_person_weapon_projection_scale")
    assert "local_view_zoom_weights[render.local_player_index]" in weapon


if __name__ == "__main__":
    result = build_and_run()
    print(result.stdout + result.stderr)
