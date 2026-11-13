"""MCC passengers must not enter or continue independent movement avoidance."""
from pathlib import Path
import shutil
import subprocess
import sys
import pytest
from harness import function

ROOT = Path(__file__).resolve().parents[1]


def test_passenger_avoidance_and_legacy_path(tmp_path):
    compiler = shutil.which("clang") or shutil.which("cc")
    if not compiler:
        pytest.skip("C compiler required")
    adapter = (ROOT / "port/linux/game/mcc_seats.c").read_text()
    native = (ROOT / "source/ai/action_avoid.c").read_text()
    source = r'''
#include <string.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define match_assert(a,b,c) ((void)0)
enum {_firing_point_evaluation_mode_avoid=6};
struct actor_datum {struct {int timeslice,swarm;} meta;struct {int vehicle_passenger;} input;
    struct {int danger_type;} danger_zone;};
struct avoid_state_data {int unused;};
struct firing_position {int unused;};
struct firing_position_evaluation_context {int evaluation_mode;};
struct path_state {int unused;};
static struct actor_datum actor;
static int active,selections;
static boolean mcc_cache_tags_loaded(void) {return active;}
static struct actor_datum *actor_get(long i) {(void)i;return &actor;}
static short actor_active_select_firing_position(long i,void *s,void *c,long *o,void *w,boolean *f)
{(void)i;(void)s;(void)c;(void)w;*o=0;*f=0;++selections;return -1;}
static void actor_change_firing_position(long i,short p,void *c,long o,void *w,boolean f)
{(void)i;(void)p;(void)c;(void)o;(void)w;(void)f;}
''' + function(adapter, "mcc_seat_blocks_avoidance") + "\n" + "\n".join(
        function(native, name) for name in ("action_avoid_setup", "action_avoid_perform")) + r'''
int main(void) {
    struct avoid_state_data state;
    actor.meta.timeslice=1;actor.danger_zone.danger_type=1;
    actor.input.vehicle_passenger=1;
    if(!action_avoid_setup(0,&state)||action_avoid_perform(0)||selections!=1)return 1;
    active=1;state.unused=123;
    if(action_avoid_setup(0,&state)||state.unused!=123||!action_avoid_perform(0)||selections!=1)return 2;
    actor.input.vehicle_passenger=0;
    if(!action_avoid_setup(0,&state)||state.unused||action_avoid_perform(0)||selections!=2)return 3;
    actor.input.vehicle_passenger=1; /* Seated after avoidance setup. */
    if(!action_avoid_perform(0)||selections!=2)return 4;
    active=0;if(action_avoid_perform(0)||selections!=3)return 5;
    return 0;
}
'''
    path = tmp_path / "avoidance.c"
    path.write_text(source)
    binary = tmp_path / ("avoidance.exe" if sys.platform == "win32" else "avoidance")
    command = [compiler, "-std=gnu89", "-Wall", "-Werror", str(path), "-o", str(binary)]
    if sys.platform == "win32":
        command[1:1] = ["--target=i686-pc-windows-msvc", "-fuse-ld=lld"]
    result = subprocess.run(command,
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
