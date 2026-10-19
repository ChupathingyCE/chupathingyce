#include "bot_ballistics.h"
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

bool bot_arc_plan(float const origin[3], float const target[3], float speed_per_tick,
    float gravity_per_tick, struct bot_arc *arc)
{
    double delta[3];
    double distance_squared = 0.0;
    double speed_squared;
    double b;
    double discriminant;
    double denominator;
    double ticks;
    uint32_t index;
    assert(origin != NULL);
    assert(target != NULL);
    assert(arc != NULL);
    memset(arc, 0, sizeof(*arc));
    if (!isfinite(speed_per_tick) || speed_per_tick <= 0.f || speed_per_tick > 8.f ||
        !isfinite(gravity_per_tick) || gravity_per_tick < 0.f || gravity_per_tick > 8.f) {
        return false;
    }
    for (index = 0; index < 3; index++) {
        if (!isfinite(origin[index]) || !isfinite(target[index]) ||
            fabsf(origin[index]) > 100000.f || fabsf(target[index]) > 100000.f) {
            return false;
        }
        delta[index] = (double)target[index] - origin[index];
        distance_squared += delta[index] * delta[index];
    }
    if (distance_squared < 0.000001) {
        return false;
    }
    speed_squared = (double)speed_per_tick * speed_per_tick;
    b = speed_squared - (double)gravity_per_tick * delta[2];
    discriminant = b * b - (double)gravity_per_tick * gravity_per_tick * distance_squared;
    if (discriminant < 0.0) {
        return false;
    }
    denominator = b + sqrt(discriminant);
    if (denominator <= 0.0) {
        return false;
    }
    /* Rationalized low-arc root avoids cancellation as gravity approaches 0. */
    ticks = sqrt(2.0 * distance_squared / denominator);
    if (!isfinite(ticks) || ticks <= 0.0 || ticks > BOT_ARC_TICKS_MAX) {
        return false;
    }
    for (index = 0; index < 3; index++) {
        arc->origin[index] = origin[index];
        arc->velocity[index] = (float)(delta[index] / ticks);
    }
    arc->velocity[2] += (float)(0.5 * gravity_per_tick * ticks);
    arc->gravity = gravity_per_tick;
    arc->ticks = (float)ticks;
    return true;
}

bool bot_arc_point(struct bot_arc const *arc, float fraction, float point[3])
{
    double tick;
    uint32_t index;
    assert(arc != NULL);
    assert(point != NULL);
    if (!isfinite(fraction) || fraction < 0.f || fraction > 1.f ||
        !isfinite(arc->ticks) || arc->ticks <= 0.f || arc->ticks > BOT_ARC_TICKS_MAX ||
        !isfinite(arc->gravity) || arc->gravity < 0.f || arc->gravity > 8.f) {
        return false;
    }
    tick = (double)arc->ticks * fraction;
    for (index = 0; index < 3; index++) {
        double value;
        if (!isfinite(arc->origin[index]) || !isfinite(arc->velocity[index])) {
            return false;
        }
        value = arc->origin[index] + (double)arc->velocity[index] * tick;
        if (index == 2) {
            value -= 0.5 * arc->gravity * tick * tick;
        }
        if (!isfinite(value) || fabs(value) > 100000.0) {
            return false;
        }
        point[index] = (float)value;
    }
    return true;
}

float bot_arc_chord_margin(struct bot_arc const *arc)
{
    double interval;
    assert(arc != NULL);
    if (!isfinite(arc->gravity) || arc->gravity < 0.f || arc->gravity > 8.f ||
        !isfinite(arc->ticks) || arc->ticks <= 0.f || arc->ticks > BOT_ARC_TICKS_MAX) {
        return 0.f;
    }
    interval = (double)arc->ticks / BOT_ARC_SEGMENTS;
    return (float)(arc->gravity * interval * interval / 8.0);
}
