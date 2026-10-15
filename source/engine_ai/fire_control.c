#include "fire_control.h"
#include <assert.h>
#include <limits.h>

bool engine_ai_fire_control_step(struct engine_ai_fire_control *state,
	int64_t tick, bool wants_fire, bool tap, int32_t period_ticks, int32_t hold_ticks)
{
	assert(state != 0);
	assert(tick >= 0);
	assert(period_ticks >= 2);
	assert(hold_ticks >= 1);
	assert(hold_ticks < INT32_MAX);
	assert(tick <= INT64_MAX - period_ticks);
	assert(tick <= INT64_MAX - hold_ticks - 1);

	if (tap && state->pressed)
	{
		if (wants_fire && tick < state->release_tick)
			return true;
		state->pressed = false;
		return false;
	}
	if (!wants_fire)
	{
		state->pressed = false;
		return false;
	}
	if (tap && tick < state->next_press_tick)
		return false;
	state->pressed = true;
	state->release_tick = tick + hold_ticks;
	state->next_press_tick = tick + period_ticks;
	if (tap && state->next_press_tick <= state->release_tick)
		state->next_press_tick = state->release_tick + 1;
	return true;
}
