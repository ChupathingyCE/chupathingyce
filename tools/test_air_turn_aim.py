#!/usr/bin/env python3
"""The 180 glitch's fix (network.air_turn_aim, #1310) changes nothing while off.

A player's aim (the unit's aiming vector) is held within its body's aiming
bounds (units.c, unit_clip_to_aiming_bounds), and a body turned away in
mid-air doesn't turn back until it lands, so on the Xbox a shot fired after
turning around in a jump leaves at the bound's angle, not where the view
(the desired aim) looks; its origin, the gun marker projected on the aim,
can be behind the eye. With the host's rule on (host_rules_air_turn_aim), a
player on foot whose unit has no aiming speed limits shoots along its
desired aim, from no further back than the eye.

This builds units.c's unit_adjust_projectile_ray and unit_air_turn_aim with a
stand-in unit, and checks:
    - rule off: the Xbox's shot (the held aim, the origin behind the eye);
    - rule on, turned around in mid-air: the desired aim, the origin at the
      eye;
    - rule on, aim not held (the usual shot): the Xbox's shot, bit for bit;
    - rule on, but in a vehicle seat, an AI's unit, a unit with aiming speed
      limits, or an AI's blind fire (use_aiming_vector off): the Xbox's shot;
and that the rule is a host's rule, off by default, in config.toml, the
dedicated server's settings, HOST_RULES and the docs.
Needs clang (or cc) and Python 3 alone.

    python3 -m pytest tools/test_air_turn_aim.py
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
#define HALO_GAME_BROWSER 1
typedef float real;
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
typedef struct { real i, j, k; } real_vector3d;
typedef struct { real x, y, z; } real_point3d;
struct unit_definition { struct { real aiming_velocity_maximum, aiming_acceleration_maximum; } unit; };
struct unit_datum {
    struct { long parent_object_index; } object;
    struct { long player_index; real_vector3d aiming_vector, desired_aiming_vector; } unit;
    long definition_index;
};
static struct unit_datum the_unit;
static struct unit_definition the_definition;
static real_point3d the_camera;
static real_vector3d the_velocity;
static boolean rule;
static struct unit_datum *unit_get(long index) { (void)index; return &the_unit; }
static struct unit_definition *unit_definition_get(long index) { (void)index; return &the_definition; }
static boolean host_rules_air_turn_aim(void) { return rule; }
static void unit_get_camera_position(long index, real_point3d *point) { (void)index; *point = the_camera; }
static void object_get_velocities(long index, real_vector3d *velocity, void *angular) {
    (void)index; (void)angular; *velocity = the_velocity;
}
static void vector_from_points3d(real_point3d const *a, real_point3d const *b, real_vector3d *v) {
    v->i = b->x - a->x; v->j = b->y - a->y; v->k = b->z - a->z;
}
static void point_from_line3d(real_point3d const *p, real_vector3d const *v, real t, real_point3d *r) {
    r->x = p->x + v->i * t; r->y = p->y + v->j * t; r->z = p->z + v->k * t;
}
static real dot_product3d(real_vector3d const *a, real_vector3d const *b) {
    return a->i * b->i + a->j * b->j + a->k * b->k;
}
static real normalize3d(real_vector3d *v) {
    real length = sqrtf(dot_product3d(v, v));
    if (length == 0.f) return 0.f;
    v->i /= length; v->j /= length; v->k /= length;
    return length;
}
'''

MAIN = r'''
static int failures;
#define CHECK(c, what) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)
static boolean same(real_vector3d a, real_vector3d b) { return a.i == b.i && a.j == b.j && a.k == b.k; }
static boolean at(real_point3d a, real_point3d b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

/* a player's unit, on foot, looking down +x, its eye at the origin */
static void setup(boolean turned) {
    memset(&the_unit, 0, sizeof(the_unit));
    memset(&the_definition, 0, sizeof(the_definition));
    the_unit.object.parent_object_index = NONE;
    the_unit.unit.player_index = 0;
    the_camera.x = 0.f; the_camera.y = 0.f; the_camera.z = 0.6f;
    the_velocity.i = 0.f; the_velocity.j = 0.f; the_velocity.k = -2.f;
    the_unit.unit.desired_aiming_vector.i = 1.f;
    if (turned) {
        /* turned around in a jump: the body still faces -x, its aim held at
        its bound, 60 degrees off the view */
        the_unit.unit.aiming_vector.i = 0.5f;
        the_unit.unit.aiming_vector.j = 0.8660254f;
    } else {
        the_unit.unit.aiming_vector.i = 1.f;
    }
}

/* the gun marker: behind the eye when turned (the body faces -x) */
static void shoot(boolean turned, boolean adjust, boolean use_aim, real_point3d *origin, real_vector3d *direction) {
    real velocity = 0.f;
    origin->x = turned ? -0.3f : 0.3f; origin->y = -0.1f; origin->z = 0.55f;
    direction->i = -1.f; direction->j = 0.f; direction->k = 0.f;
    unit_adjust_projectile_ray(0, origin, direction, &velocity, adjust, use_aim);
}

int main(void) {
    real_point3d origin, xbox_origin;
    real_vector3d direction, xbox_direction;
    int retail;

    /* rule off: the Xbox's shot */
    setup(TRUE); rule = FALSE;
    shoot(TRUE, TRUE, TRUE, &xbox_origin, &xbox_direction);
    CHECK(same(xbox_direction, the_unit.unit.aiming_vector), "off: the shot's aim isn't the held aim");
    CHECK(xbox_origin.x < 0.f, "off: the shot doesn't start behind the eye (the Xbox's does)");
    printf("off: direction %.3f %.3f origin %.3f %.3f\n", xbox_direction.i, xbox_direction.j, xbox_origin.x, xbox_origin.y);

    /* rule on, turned around: the desired aim, from the eye */
    setup(TRUE); rule = TRUE;
    shoot(TRUE, TRUE, TRUE, &origin, &direction);
    CHECK(same(direction, the_unit.unit.desired_aiming_vector), "on: the shot's aim isn't the desired aim");
    CHECK(at(origin, the_camera), "on: the shot doesn't start at the eye");
    printf("on: direction %.3f %.3f origin %.3f %.3f\n", direction.i, direction.j, origin.x, origin.y);

    /* rule on, aim not held: the Xbox's shot, bit for bit */
    setup(FALSE); rule = FALSE;
    shoot(FALSE, TRUE, TRUE, &xbox_origin, &xbox_direction);
    rule = TRUE;
    shoot(FALSE, TRUE, TRUE, &origin, &direction);
    CHECK(same(direction, xbox_direction) && at(origin, xbox_origin), "on: a usual shot changed");
    CHECK(origin.x > 0.f, "a usual shot doesn't start ahead of the eye");

    /* rule on, where it doesn't apply: the Xbox's shot */
    for (retail = 0; retail < 4; retail++) {
        setup(TRUE); rule = FALSE;
        if (retail == 0) the_unit.object.parent_object_index = 7;
        if (retail == 1) the_unit.unit.player_index = NONE;
        if (retail == 2) the_definition.unit.aiming_velocity_maximum = 3.f;
        shoot(TRUE, TRUE, retail != 3, &xbox_origin, &xbox_direction);
        rule = TRUE;
        shoot(TRUE, TRUE, retail != 3, &origin, &direction);
        CHECK(same(direction, xbox_direction) && at(origin, xbox_origin), "on: a shot it doesn't apply to changed");
    }

    /* rule on, a unit that doesn't fire from its camera: the marker */
    setup(TRUE); rule = TRUE;
    shoot(TRUE, FALSE, TRUE, &origin, &direction);
    CHECK(origin.x == -0.3f && same(direction, the_unit.unit.desired_aiming_vector),
        "on: a shot not from the camera moved");

    printf("%d failures\n", failures);
    return failures != 0;
}
'''


def build_and_run():
    source = (ROOT / "source/units/units.c").read_text(encoding="utf-8")
    code = PRELUDE + function(source, "unit_air_turn_aim") + "\n" + \
        function(source, "unit_adjust_projectile_ray") + "\n" + MAIN
    compiler = shutil.which("clang") or shutil.which("cc")
    assert compiler, "needs clang or cc"
    with tempfile.TemporaryDirectory() as work:
        path = Path(work) / "air_turn_aim.c"
        path.write_text(code)
        program = Path(work) / "air_turn_aim"
        subprocess.run([compiler, "-std=c99", "-Wall", "-Werror", "-Wno-unused-function", str(path), "-lm",
                        "-o", str(program)], check=True)
        return subprocess.run([str(program)], capture_output=True, text=True)


def test_shots():
    result = build_and_run()
    assert result.returncode == 0, result.stdout + result.stderr


def test_a_host_rule_off_by_default():
    config = (ROOT / "port/linux/src/port_config.c").read_text(encoding="utf-8")
    assert re.search(r'\{ "network\.air_turn_aim", _config_boolean, "false", "HALO_NET_AIR_TURN_AIM"', config)
    rules = (ROOT / "port/linux/game/host_rules.c").read_text(encoding="utf-8")
    assert 'config_boolean("network.air_turn_aim")' in rules
    assert "rules.flags |= DELTA_WIRE_HOST_AIR_TURN_AIM" in rules
    assert "DELTA_WIRE_HOST_AIR_TURN_AIM = 0x10" in (ROOT / "port/linux/src/delta_wire.h").read_text(encoding="utf-8")
    server = (ROOT / "server/src/server_config.c").read_text(encoding="utf-8")
    assert '{ "air_turn_aim", SERVER_FIELD_BOOLEAN, 0, 1, "HALO_DEDICATED_AIR_TURN_AIM",' in server
    assert "`network.air_turn_aim` | `false` | `HALO_NET_AIR_TURN_AIM` |" in \
        (ROOT / "port/linux/README.md").read_text(encoding="utf-8")
    assert "`HALO_DEDICATED_AIR_TURN_AIM` | `false` |" in (ROOT / "server/docs/settings.md").read_text(encoding="utf-8")
    assert "0x10 shots turned around in mid-air" in (ROOT / "docs/delta/peer.md").read_text(encoding="utf-8")


if __name__ == "__main__":
    result = build_and_run()
    print(result.stdout + result.stderr)
