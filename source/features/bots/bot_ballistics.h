/* Bounded constant-air-gravity throw planning. CE supplies actual unit/tag
 * speed and gravity; no Infinite tuning values or world access are assumed. */
#ifndef HALO_BOT_BALLISTICS_H
#define HALO_BOT_BALLISTICS_H
#include <stdbool.h>
#include <stdint.h>
enum { BOT_ARC_SEGMENTS = 16, BOT_ARC_TICKS_MAX = 120 };
struct bot_arc
{
    float origin[3];
    float velocity[3];
    float gravity;
    float ticks;
};
bool bot_arc_plan(float const origin[3], float const target[3], float speed_per_tick,
    float gravity_per_tick, struct bot_arc *arc);
bool bot_arc_point(struct bot_arc const *arc, float fraction, float point[3]);
/* Each chord is conservatively inflated by this bound on parabolic sag. */
float bot_arc_chord_margin(struct bot_arc const *arc);
#endif
