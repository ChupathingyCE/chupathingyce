#ifndef ENGINE_AI_FIRE_CONTROL_H
#define ENGINE_AI_FIRE_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

struct engine_ai_fire_control
{
	int64_t next_press_tick;
	int64_t release_tick;
	bool pressed;
};

/* Call once per game tick. Tap mode holds for hold_ticks, then always
   releases for at least one tick. Losing intent releases immediately.
   period_ticks is the minimum spacing between presses; hold_ticks = 1
   gives ordinary tap fire, larger values give charged shots. */
bool engine_ai_fire_control_step(struct engine_ai_fire_control *state,
	int64_t tick, bool wants_fire, bool tap, int32_t period_ticks, int32_t hold_ticks);

#endif
