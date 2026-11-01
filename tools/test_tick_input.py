"""Compile the real per-tick button latch (player_queues_new.c's
update_client_queue and update_client_queue_push) and drive it the way the
native builds do: several frames, each building an action, then a 30 Hz tick
taking the last one. Taps, holds and fast tapping at 1, 2, 4 and 8 frames a
tick must reach the ticks as at 1 frame a tick (the Xbox: one read a tick):
every tap a press, a plasma pistol's tap a shot (not a charge), a hold a
charge and an overcharge.

The negative control puts back the old latch (every button held on any frame
since the last tick stays held), which charged a plasma pistol on fast taps.
"""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "source/game/player_queues_new.c"

PRELUDE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef float real;
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define MAX(a,b) ((a)>(b)?(a):(b))
#define FLAG(b) (1ul<<(b))
#define TEST_FLAG(v,b) (((v)&FLAG(b))!=0)
#define csmemset memset
#define MAXIMUM_LOCAL_PLAYERS 4
enum { _unit_control_action_bit = 6, _unit_control_weapon_primary_trigger_bit = 11 };
struct player_action { unsigned long control_flags; real primary_trigger; };
static struct
{
	long current_local_player;
	struct { struct player_action actions[MAXIMUM_LOCAL_PLAYERS]; } saved_action_collection;
} update_client_globals;
static long game_time;
static long game_time_get(void) { return game_time; }
static boolean game_time_get_paused(void) { return FALSE; }
static boolean game_time_held(void) { return FALSE; }
static long local_player_get_player_index(short local_player_index) { return local_player_index == 0 ? 0 : NONE; }
'''

DRIVER = r'''
#define FIRE _unit_control_weapon_primary_trigger_bit
#define CHARGING_TICKS 6
#define CHARGED_TICKS 20

/* a plasma pistol's trigger as weapons.c steps it (two triggers: a tap
fires on letting go while charging, a hold charges, then overcharges) */
struct result { int presses, shots, charged_shots, overcharges; };

static int failures;

static struct result run(int frames_per_tick, int phase, int const *down, int frame_count)
{
	struct result result = {0};
	int state = 0, timer = 0, was_down = 0, frame;

	game_time = 0;
	memset(&update_client_globals, 0, sizeof(update_client_globals));
	update_client_queue_push();
	for (frame = 0; frame < frame_count + 2 * CHARGED_TICKS * frames_per_tick; frame++)
	{
		struct player_action action = {0};
		int tick_down;

		action.control_flags = frame < frame_count && down[frame] ? FLAG(FIRE) : 0;
		action.primary_trigger = action.control_flags ? 1.f : 0.f;
		update_client_queue_push();
		update_client_queue(&action);
		if ((frame + phase) % frames_per_tick != frames_per_tick - 1)
			continue;
		/* the tick */
		tick_down = TEST_FLAG(update_client_globals.saved_action_collection.actions[0].control_flags, FIRE);
		if (tick_down && update_client_globals.saved_action_collection.actions[0].primary_trigger != 1.f)
			failures++, printf("FAIL trigger pull %f\n", update_client_globals.saved_action_collection.actions[0].primary_trigger);
		result.presses += tick_down && !was_down;
		was_down = tick_down;
		switch (state)
		{
		case 0: if (tick_down) state = 1, timer = CHARGING_TICKS; break;
		case 1:
			if (timer) { if (!tick_down) result.shots++, state = 0; else timer--; }
			else state = 2, timer = CHARGED_TICKS;
			break;
		case 2:
			if (!tick_down) result.charged_shots++, state = 0;
			else if (!timer) result.overcharges++, state = 3;
			else timer--;
			break;
		case 3: if (!tick_down) state = 0; break; /* fired: locked until let go */
		}
		game_time++;
	}
	return result;
}

/* taps of down_ticks then up_ticks (fractions of a tick are frames) */
static int pattern(int *down, int frames_per_tick, int taps, int down_frames, int up_frames)
{
	int count = 0, tap, frame;

	for (frame = 0; frame < 2 * frames_per_tick; frame++)
		down[count++] = 0;
	for (tap = 0; tap < taps; tap++)
	{
		for (frame = 0; frame < down_frames; frame++)
			down[count++] = 1;
		for (frame = 0; frame < up_frames; frame++)
			down[count++] = 0;
	}
	return count;
}

static void expect(char const *name, int frames_per_tick, int phase, struct result got, struct result want)
{
	if (memcmp(&got, &want, sizeof(got)))
	{
		failures++;
		printf("FAIL %s at %d frames a tick (phase %d): presses %d shots %d charged %d overcharged %d, "
			"want %d %d %d %d\n", name, frames_per_tick, phase,
			got.presses, got.shots, got.charged_shots, got.overcharges,
			want.presses, want.shots, want.charged_shots, want.overcharges);
	}
}

int main(void)
{
	static int down[4096];
	int const rates[] = { 1, 2, 4, 8 };
	int rate_index;

	for (rate_index = 0; rate_index < 4; rate_index++)
	{
		int f = rates[rate_index], phase;

		for (phase = 0; phase < f; phase++)
		{
			struct result want_taps = { 10, 10, 0, 0 };
			struct result want_charge = { 1, 0, 1, 0 };
			struct result want_overcharge = { 1, 0, 0, 1 };
			int count;

			/* one-frame taps, two ticks apart */
			count = pattern(down, f, 10, 1, 2 * f - 1);
			expect("one-frame taps", f, phase, run(f, phase, down, count), want_taps);
			/* half-tick taps (one frame at 1 and 2 frames a tick) */
			count = pattern(down, f, 10, (f + 1) / 2, 2 * f - (f + 1) / 2);
			expect("half-tick taps", f, phase, run(f, phase, down, count), want_taps);
			/* fast tapping: a tick down, a tick up */
			count = pattern(down, f, 10, f, f);
			expect("tick-long taps", f, phase, run(f, phase, down, count), want_taps);
			/* a hold to charge, let go before it overcharges */
			count = pattern(down, f, 1, (CHARGING_TICKS + 4) * f, 2 * f);
			expect("charge", f, phase, run(f, phase, down, count), want_charge);
			/* a hold past the overcharge */
			count = pattern(down, f, 1, (CHARGING_TICKS + CHARGED_TICKS + 6) * f, 2 * f);
			expect("overcharge", f, phase, run(f, phase, down, count), want_overcharge);
		}
	}
	printf(failures ? "%d failures\n" : "ok\n", failures);
	return failures != 0;
}
'''


def latch_code(old):
    source = SOURCE.read_text()
    start = source.index("/* The native builds draw several frames per 30 Hz tick")
    end = source.index("/* the local player (of this machine) controlling", start)
    code = source[start:end]
    if old:
        # the latch before: held on any frame since the last tick
        code, count = re.subn(
            r"saved->control_flags =\s*\(tick_flags[^;]*;",
            "saved->control_flags = update_client_pending_down_flags[local_player_index];",
            code)
        assert count == 1, "the latch's expression was not found"
    return code


def build_and_run(cc, folder, old):
    path = Path(folder) / ("old.c" if old else "new.c")
    path.write_text(PRELUDE + latch_code(old) + DRIVER)
    binary = path.with_suffix("")
    subprocess.run([cc, "-std=c99", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-unused-variable", "-o", str(binary), str(path)],
        check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    return result.returncode, result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cc", default=shutil.which("cc") or "cc")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as folder:
        code, output = build_and_run(args.cc, folder, old=False)
        print(output, end="")
        if code:
            print("the tick latch loses or invents button changes")
            return 1
        code, output = build_and_run(args.cc, folder, old=True)
        if not code:
            print("negative control: the old latch passed, so this test proves nothing")
            return 1
        print("negative control: the old latch fails, as it should (%s)" % output.strip().splitlines()[-1])
    return 0


if __name__ == "__main__":
    sys.exit(main())
