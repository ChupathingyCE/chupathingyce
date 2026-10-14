#include "engine_ai/aim.h"

#include <assert.h>

static bool aim_finite(float value)
{
    return value == value && value < 3.0e38f && value > -3.0e38f;
}

float engine_ai_aim_error_degrees(const struct engine_ai_aim_skill *skill,
    float distance, float seconds_tracked, bool moving)
{
    float settle_fraction = 0.f;
    float range_scale = (float)ENGINE_AI_AIM_RANGE_SCALE_MAX;
    float error;

    assert(skill);
    assert(skill->base_error_degrees >= 0.f);
    assert(skill->minimum_error_degrees > 0.f);
    assert(skill->accurate_range > 0.f);
    assert(skill->moving_penalty >= 1.f);
    if (aim_finite(seconds_tracked) && seconds_tracked >= 0.f) {
        settle_fraction = skill->settle_seconds > 0.f ?
            seconds_tracked / skill->settle_seconds : 1.f;
        if (settle_fraction > 1.f) settle_fraction = 1.f;
    }
    if (aim_finite(distance) && distance >= 0.f) {
        range_scale = 1.f;
        if (distance > skill->accurate_range)
            range_scale += (distance - skill->accurate_range) / skill->accurate_range;
        if (range_scale > (float)ENGINE_AI_AIM_RANGE_SCALE_MAX)
            range_scale = (float)ENGINE_AI_AIM_RANGE_SCALE_MAX;
    }
    error = skill->base_error_degrees * (1.f + 2.f * (1.f - settle_fraction)) * range_scale;
    if (moving) error *= skill->moving_penalty;
    if (error < skill->minimum_error_degrees) error = skill->minimum_error_degrees;
    assert(error > 0.f);
    return error;
}
