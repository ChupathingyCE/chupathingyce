/* CE grenade/danger adapter. Only ordinary player aim/control is produced;
 * engine objects, ownership and scoring are never modified. */
#ifndef HALO_BOT_GRENADE_H
#define HALO_BOT_GRENADE_H
#include <stdbool.h>
#include <stdint.h>
struct bot_grenade_request
{
    int32_t unit_key;
    int32_t target_unit_key;
    float eye[3];
    float target[3]; /* perceived position, never fetched from the enemy */
    float facing[2]; /* yaw, pitch actually submitted */
};
bool bot_grenade_plan(struct bot_grenade_request const *request, float aim[2]);
bool bot_grenade_validate(struct bot_grenade_request const *request, float landing[3]);
struct bot_danger_request
{
    int32_t unit_key;
    float eye[3];
    float origin[3];
    float yaw;
    float sight_range;
    float half_fov_radians;
};
struct bot_danger
{
    float position[3];
    float escape_yaw;
};
/* At most 256 candidates and four LOS tests. Only visible (or arm's-reach
 * LOS) grenades/approaching vehicles can produce a danger observation. */
bool bot_danger_observe(struct bot_danger_request const *request, struct bot_danger *danger);
#endif
