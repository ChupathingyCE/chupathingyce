"""Production CTF snapshots, goals and execution against a bounded fake world."""
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


def header(path: str) -> str:
    return re.sub(r"^#(?:include|pragma once).*$", "", (ROOT / path).read_text(), flags=re.M)


def compile_fixture(fixture: str, name: str, extra=()):
    with tempfile.TemporaryDirectory(prefix=f"bot-ctf-{name}-") as directory:
        source = Path(directory) / "test.c"
        executable = Path(directory) / "test"
        source.write_text(fixture)
        subprocess.run([os.environ.get("CC", "clang"), "-std=gnu89", "-Wall", "-Wextra", "-Werror",
                        "-Wno-sign-compare", "-fsanitize=address,undefined", "-I", str(ROOT / "source"),
                        str(source), *[str(ROOT / path) for path in extra], "-o", str(executable)],
                       check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)


def test_ctf_snapshot_and_goals():
    engine = (ROOT / "source/game/game_engine_ctf.c").read_text()
    manager = (ROOT / "source/features/bots/bot_manager.c").read_text()
    functions = "\n".join(body(engine, name) for name in (
        "ctf_set_flag_warning", "ctf_reset_flag", "ctf_award_capture", "ctf_position_near_flag",
        "ctf_player_drop_flag", "ctf_weapon_pickup", "ctf_engine_player_update",
        "game_engine_ctf_bot_variant_supported", "ctf_bot_point_valid", "ctf_bot_copy_flag",
        "game_engine_ctf_bot_snapshot"))
    functions += "\n" + "\n".join(body(manager, name) for name in (
        "bot_manager_ambition_allowed", "bot_manager_claim_scope", "bot_manager_claim_item",
        "bot_manager_item_claimed_by_other", "bot_manager_release_item", "bot_manager_release_bot",
        "bot_manager_seat_key", "bot_manager_claim_seat", "bot_manager_seat_claimed_by_other",
        "bot_manager_vehicle_claimed_by_other", "bot_manager_release_seat",
        "bot_manager_ctf_goal", "bot_manager_ctf_add", "bot_manager_ctf_observe", "bot_manager_publish_ctf", "bot_manager_publish_ambitions"))
    fixture = (ROOT / "tools/test_bot_ctf_snapshot.c.in").read_text()
    fixture = fixture.replace("/* HEADERS */", header("port/linux/include/halo_bot_objectives.h") +
                              header("source/features/bots/bot_manager.h"))
    fixture = fixture.replace("/* FUNCTIONS */", functions)
    compile_fixture(fixture, "snapshot", ("source/engine_ai/claims.c",))


def test_ctf_objective_execution():
    source = (ROOT / "source/features/bots/bots.c").read_text()
    functions = "\n".join(body(source, name) for name in (
        "bots_navigation_requested", "bot_carries_flag", "bot_objective_release", "bot_update_objective", "bot_leaf_objective", "bot_keep_moving"))
    fixture = (ROOT / "tools/test_bot_ctf_execution.c.in").read_text()
    fixture = fixture.replace("/* HEADERS */", header("source/features/bots/bot_manager.h"))
    fixture = fixture.replace("/* FUNCTIONS */", functions)
    compile_fixture(fixture, "execution")


def test_ctf_arrival_requires_correct_floor():
    source = (ROOT / "source/features/bots/bot_navigation.c").read_text()
    functions = "\n".join(body(source, name) for name in (
        "bot_nav_abs", "bot_nav_distance", "bot_nav_horizontal_distance", "bot_nav_route_step"))
    fixture = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
typedef float real;
typedef int boolean;
#define FALSE 0
#define TRUE 1
#define square_root sqrtf
#define BOT_NAVIGATION_REPLAN_DISTANCE 12
struct sn_point { real x,y,z; };
struct sn_resource { int unused; };
struct bot_navigation_command { int unused; };
struct bot_navigation_agent { boolean searching,following,patrol_goal,goal_valid; struct sn_point goal; };
enum bot_navigation_result { BOT_NAVIGATION_UNAVAILABLE, BOT_NAVIGATION_WAITING,
    BOT_NAVIGATION_MOVING, BOT_NAVIGATION_ARRIVED, BOT_NAVIGATION_FAILED };
static struct sn_point origin;
static struct sn_resource resource;
static int available,resets,searches;
static struct sn_point bot_nav_position(long unit) { assert(unit==100); return origin; }
static void bot_navigation_agent_reset_path(struct bot_navigation_agent *a) {
    assert(a); a->searching=a->following=FALSE; resets++;
}
static struct sn_resource const *navigation_world_resource(void) { return available ? &resource : NULL; }
static int bot_nav_refresh(struct bot_navigation_agent *a,struct sn_point p,struct sn_point g) {
    assert(a); (void)p; (void)g; return 0;
}
static enum bot_navigation_result bot_nav_search(struct bot_navigation_agent *a,long u,
    struct sn_resource const *r,struct sn_point start) {
    assert(a && u==100 && r==&resource); (void)start; searches++; return BOT_NAVIGATION_WAITING;
}
static enum bot_navigation_result bot_nav_follow(struct bot_navigation_agent *a,long u,
    struct sn_resource const *r,struct sn_point p,real speed,boolean patrol,struct bot_navigation_command *c) {
    assert(a && u==100 && r==&resource && c); (void)p; (void)speed; (void)patrol; return BOT_NAVIGATION_MOVING;
}
/* FUNCTIONS */
int main(void) {
    struct bot_navigation_agent a={0};
    struct bot_navigation_command c={0};
    struct sn_point goal={0};
    assert(bot_nav_abs(-1.f)==1.f);
    origin.x=0.2f; origin.z=0.84f;
    assert(bot_nav_route_step(&a,100,goal,1.f,0.45f,FALSE,&c)==BOT_NAVIGATION_UNAVAILABLE);
    assert(resets==0 && searches==0); /* same XY on another floor is not arrival */
    available=1;
    assert(bot_nav_route_step(&a,100,goal,1.f,0.45f,FALSE,&c)==BOT_NAVIGATION_WAITING && searches==1);
    origin.z=-0.84f;
    assert(bot_nav_route_step(&a,100,goal,1.f,0.45f,FALSE,&c)==BOT_NAVIGATION_WAITING && searches==2);
    origin.z=0.1f;
    assert(bot_nav_route_step(&a,100,goal,1.f,0.45f,FALSE,&c)==BOT_NAVIGATION_ARRIVED && resets==1);
    origin.x=0.5f;
    assert(bot_nav_route_step(&a,100,goal,1.f,0.45f,FALSE,&c)==BOT_NAVIGATION_WAITING && searches==3);
    puts("CTF navigation arrival rejects stacked floors"); return 0;
}
'''
    compile_fixture(fixture.replace("/* FUNCTIONS */", functions), "arrival")
