/* Imperfect aim: an error cone that widens with distance and movement and
 * narrows as a target is tracked. Skill values are game-owned; no heap, no libm. */
#ifndef ENGINE_AI_AIM_H
#define ENGINE_AI_AIM_H

#include <stdbool.h>

enum { ENGINE_AI_AIM_RANGE_SCALE_MAX = 4 };

struct engine_ai_aim_skill {
    float base_error_degrees;    /* settled, still, within accurate range */
    float minimum_error_degrees; /* never perfect: a floor above zero */
    float accurate_range;        /* metres; error grows linearly beyond it */
    float settle_seconds;        /* tracking time to reach base error */
    float moving_penalty;        /* error multiplier while moving (>= 1) */
};

/* The half-angle of this skill's error cone, in degrees. Unsettled aim is up
 * to three times base; distance scales by 1 + (d - accurate) / accurate, capped
 * at ENGINE_AI_AIM_RANGE_SCALE_MAX. Non-finite or negative inputs are treated
 * as the worst case (unsettled, far, moving). */
float engine_ai_aim_error_degrees(const struct engine_ai_aim_skill *skill,
    float distance, float seconds_tracked, bool moving);

#endif
