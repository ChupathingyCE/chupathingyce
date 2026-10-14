"""The production vehicle steering (vehicle_navigation_steer) and the
chauffeur's ride estimate (bot_ride_saving), compiled from the sources with
AddressSanitizer and UndefinedBehaviorSanitizer against fixed routes."""

import os
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def body(source: str, name: str) -> str:
    match = re.search(r"(?:^|\n)(?:static )?[\w *]+\b" + name + r"\([^;{]*\)\s*\{", source)
    assert match, name
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def defines(source: str, prefix: str) -> str:
    return "\n".join(line for line in source.splitlines() if line.startswith("#define " + prefix))


FIXTURE = r"""
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef float real;
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define csmemset memset
#define _pi ((real)M_PI)
static real square_root(real x) { return sqrtf(x); }
static real arctangent(real y, real x) { return atan2f(y, x); }
struct sn_point { float x, y, z; };
/* HEADER */
/* DEFINES */
/* FUNCTIONS */
static struct sn_point P(float x, float y) { struct sn_point p = { x, y, 0.f }; return p; }
static struct vehicle_steer_input input(struct sn_point const *points, uint32_t count,
    float x, float y, float forward_yaw) {
    struct vehicle_steer_input in;
    memset(&in, 0, sizeof(in));
    in.origin = P(x, y); in.forward_yaw = forward_yaw; in.half_length = 1.f;
    in.points = points; in.count = count; in.waypoint = 1;
    return in;
}
int main(void) {
    struct vehicle_navigation_command c;
    struct vehicle_steer_input in;
    /* A straight road: full throttle ahead, steering at a point well along it. */
    struct sn_point straight[] = { {0,0,0}, {2,0,0}, {4,0,0}, {6,0,0}, {20,0,0} };
    in = input(straight, 5, 0.f, 0.f, 0.f);
    assert(vehicle_navigation_steer(&in, &c) && c.has_drive);
    assert(fabsf(c.yaw) < 0.001f && c.throttle == 1.f);
    assert(in.target == 2); /* looks two vehicle lengths ahead (4 half-lengths), not to the end */
    /* Reached waypoints are passed: within 0.8 half-lengths, or already behind. */
    in = input(straight, 5, 1.9f, 0.f, 0.f);
    assert(vehicle_navigation_steer(&in, &c) && in.waypoint == 2);
    in = input(straight, 5, 4.5f, 0.3f, 0.f);
    assert(vehicle_navigation_steer(&in, &c) && in.waypoint == 3);
    /* A right-angle corner is not cut: steering stays on the corner. */
    {
        struct sn_point corner[] = { {0,0,0}, {3,0,0}, {3,10,0} };
        in = input(corner, 3, 0.f, 0.f, 0.f);
        assert(vehicle_navigation_steer(&in, &c) && in.target == 1 && fabsf(c.yaw) < 0.001f);
    }
    /* Facing away: slow, and turning toward the route. */
    in = input(straight, 5, 0.f, 0.f, (real)M_PI);
    assert(vehicle_navigation_steer(&in, &c) && fabsf(c.throttle - 0.35f) < 0.001f);
    /* A quarter turn off: between full and slow. */
    in = input(straight, 5, 0.f, 0.f, (real)M_PI / 2.f);
    assert(vehicle_navigation_steer(&in, &c) && c.throttle > 0.35f && c.throttle < 1.f);
    /* Near the end it slows, never below the minimum. */
    {
        struct sn_point end[] = { {0,0,0}, {1.5f,0,0} };
        in = input(end, 2, 0.f, 0.f, 0.f); in.half_length = 1.f;
        assert(vehicle_navigation_steer(&in, &c) && c.throttle < 1.f && c.throttle >= 0.3f);
    }
    /* At the end the route is done; bad input steers nowhere. */
    in = input(straight, 5, 20.f, 0.f, 0.f); in.waypoint = 4;
    assert(!vehicle_navigation_steer(&in, &c) && in.waypoint == 5);
    /* A waypoint far behind (beyond the lookahead) is not taken as passed. */
    in = input(straight, 5, 20.f, 0.f, 0.f);
    assert(vehicle_navigation_steer(&in, &c) && in.waypoint == 1);
    assert(!vehicle_navigation_steer(NULL, &c));
    in = input(straight, 0, 0.f, 0.f, 0.f);
    assert(!vehicle_navigation_steer(&in, &c));
    in = input(NULL, 5, 0.f, 0.f, 0.f);
    assert(!vehicle_navigation_steer(&in, &c));
    /* Angles wrap: a route behind across the -pi/pi seam is a small turn. */
    {
        struct sn_point behind[] = { {0,0,0}, {-10,0.1f,0} };
        in = input(behind, 2, 0.f, 0.f, -(real)M_PI + 0.05f);
        assert(vehicle_navigation_steer(&in, &c) && c.throttle == 1.f);
    }
    /* Long routes: the waypoint loop and lookahead stay within count. */
    {
        static struct sn_point many[1025];
        uint32_t i;
        for (i = 0; i < 1025; i++) many[i] = P((float)i * 0.1f, 0.f);
        in = input(many, 1025, 0.f, 0.f, 0.f);
        assert(vehicle_navigation_steer(&in, &c) && in.target < 1025);
        in = input(many, 1025, 102.4f, 0.f, 0.f); in.waypoint = 1000;
        assert(!vehicle_navigation_steer(&in, &c) && in.waypoint == 1025);
    }

    /* The chauffeur: a long walk is worth a ride; a short one is not. */
    assert(bot_ride_saving(10.f, 60.f) > 5.f);
    assert(bot_ride_saving(25.f, 8.f) < 5.f);
    assert(bot_ride_saving(0.f, 0.f) < 0.f); /* getting in costs time */
    assert(bot_ride_saving(NAN, 60.f) == 0.f && bot_ride_saving(10.f, NAN) == 0.f);
    assert(bot_ride_saving(-1.f, 60.f) == 0.f && bot_ride_saving(10.f, -1.f) == 0.f);
    assert(bot_ride_saving(10.f, 200.f) > bot_ride_saving(10.f, 100.f)); /* farther: more saved */
    assert(bot_ride_saving(30.f, 60.f) < bot_ride_saving(10.f, 60.f)); /* a long fetch: less saved */
    puts("vehicle navigation tests passed");
    return 0;
}
"""


def test_vehicle_steering_and_ride_estimate():
    navigation = (ROOT / "source/features/bots/vehicle_navigation.c").read_text()
    bots = (ROOT / "source/features/bots/bots.c").read_text()
    header = (ROOT / "source/features/bots/vehicle_navigation.h").read_text()
    structures = "\n".join(re.findall(r"struct vehicle_(?:navigation_command|steer_input)\n\{.*?\n\};", header, re.S))
    functions = "\n".join(body(navigation, name) for name in (
        "vehicle_nav_abs", "vehicle_nav_horizontal", "vehicle_nav_angle", "vehicle_nav_yaw",
        "vehicle_navigation_steer"))
    functions += "\n" + body(bots, "bot_ride_saving")
    fixture = FIXTURE.replace("/* HEADER */", structures)
    fixture = fixture.replace("/* DEFINES */", defines(navigation, "VEHICLE_NAVIGATION_") + "\n" +
                              defines(bots, "BOT_RIDE_"))
    fixture = fixture.replace("/* FUNCTIONS */", functions)
    with tempfile.TemporaryDirectory(prefix="vehicle-navigation-") as directory:
        source = Path(directory) / "test.c"
        executable = Path(directory) / "test"
        source.write_text(fixture)
        subprocess.run([os.environ.get("CC", "clang"), "-std=gnu89", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-function", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        str(source), "-lm", "-o", str(executable)], check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)


if __name__ == "__main__":
    test_vehicle_steering_and_ride_estimate()
    print("ok")
