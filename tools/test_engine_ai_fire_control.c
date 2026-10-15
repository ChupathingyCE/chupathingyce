#include "../source/engine_ai/fire_control.h"
#include <assert.h>
#include <stdio.h>

static void test_taps_and_automatic(void)
{
	struct engine_ai_fire_control state = { 0 };
	int tick;
	int shots = 0;

	for (tick = 0; tick < 30; tick++)
	{
		bool previous = state.pressed;
		bool pressed = engine_ai_fire_control_step(&state, tick, true, true, 12, 1);
		assert(!pressed || !previous);
		assert(pressed == (tick % 12 == 0));
		shots += pressed;
	}
	assert(shots == 3);
	assert(!engine_ai_fire_control_step(&state, 30, false, true, 12, 1));
	assert(!engine_ai_fire_control_step(&state, 31, true, true, 12, 1));
	assert(engine_ai_fire_control_step(&state, 36, true, true, 12, 1));
	assert(!engine_ai_fire_control_step(&state, 37, true, true, 12, 1));
	for (tick = 38; tick < 68; tick++)
		assert(engine_ai_fire_control_step(&state, tick, true, false, 12, 1));
	assert(!engine_ai_fire_control_step(&state, 68, false, false, 12, 1));
}

static void test_charged_release(void)
{
	struct engine_ai_fire_control state = { 0 };
	int tick;

	/* Charge for 30 ticks; short tap cadence cannot override the release. */
	for (tick = 0; tick < 30; tick++)
		assert(engine_ai_fire_control_step(&state, tick, true, true, 12, 30));
	assert(!engine_ai_fire_control_step(&state, 30, true, true, 12, 30));
	assert(engine_ai_fire_control_step(&state, 31, true, true, 12, 30));
	/* Lost sight interrupts the charge immediately. */
	assert(!engine_ai_fire_control_step(&state, 32, false, true, 12, 30));
	assert(!engine_ai_fire_control_step(&state, 33, true, true, 12, 1));
	/* Next shot may be a tap; duration isn't inherited from the charge. */
	assert(engine_ai_fire_control_step(&state, 62, true, true, 12, 1));
	assert(!engine_ai_fire_control_step(&state, 63, true, true, 12, 1));
}

int main(void)
{
	test_taps_and_automatic();
	test_charged_release();
	puts("fire control: passed (cadence, charged release, interrupted charge)");
	return 0;
}
