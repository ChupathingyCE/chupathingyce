#include "../source/features/bots/bot_ballistics.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static uint32_t seed = 7;
static float draw(void)
{
    seed = seed * 1664525u + 1013904223u;
    return (float)(seed >> 8) / 16777215.f;
}
int main(void)
{
    struct bot_arc arc;
    float origin[3] = {0.f, 0.f, 1.f};
    float target[3] = {20.f, 0.f, 1.f};
    float point[3];
    uint32_t solved = 0;
    assert(bot_arc_plan(origin, target, 1.f, 0.02f, &arc));
    assert(arc.velocity[2] > 0.f && bot_arc_chord_margin(&arc) > 0.f);
    assert(!bot_arc_plan(origin, target, 0.f, 0.02f, &arc));
    assert(!bot_arc_plan(origin, target, NAN, 0.02f, &arc));
    assert(!bot_arc_plan(origin, target, 1.f, -0.02f, &arc));
    assert(!bot_arc_plan(origin, target, 0.01f, 1.f, &arc));
    target[0] = NAN;
    assert(!bot_arc_plan(origin, target, 1.f, 0.f, &arc));
    target[0] = 0.f; target[2] = 11.f;
    assert(bot_arc_plan(origin, target, 1.f, 0.f, &arc));
    assert(bot_arc_point(&arc, 1.f, point) && fabsf(point[2] - 11.f) < 0.001f);
    assert(!bot_arc_point(&arc, NAN, point) && !bot_arc_point(&arc, 1.1f, point));
    for (uint32_t index = 0; index < 20000; index++) {
        float speed = 0.1f + draw() * 1.9f;
        float gravity = draw() * 0.01f;
        for (uint32_t axis = 0; axis < 3; axis++) {
            origin[axis] = (draw() - 0.5f) * 40.f;
            target[axis] = (draw() - 0.5f) * 40.f;
        }
        if (!bot_arc_plan(origin, target, speed, gravity, &arc)) {
            continue;
        }
        solved++;
        assert(bot_arc_point(&arc, 1.f, point));
        for (uint32_t axis = 0; axis < 3; axis++) {
            assert(fabsf(point[axis] - target[axis]) < 0.001f);
        }
        assert(fabsf(sqrtf(arc.velocity[0] * arc.velocity[0] +
            arc.velocity[1] * arc.velocity[1] + arc.velocity[2] * arc.velocity[2]) - speed) < 0.00002f);
        for (uint32_t step = 0; step <= BOT_ARC_SEGMENTS; step++) {
            assert(bot_arc_point(&arc, (float)step / BOT_ARC_SEGMENTS, point));
        }
    }
    assert(solved > 1000);
    puts("bounded ballistic planner randomized/endpoints/invalid inputs passed");
    return 0;
}
