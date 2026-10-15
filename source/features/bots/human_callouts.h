/* port: implicit local-human callouts, derived and host-local only. */
#ifndef HALO_HUMAN_CALLOUTS_H
#define HALO_HUMAN_CALLOUTS_H
#include "cseries/cseries.h"
#include "math/real_math.h"
void human_callouts_reset(void);
void human_callouts_update(void);
/* Incoming travel direction from the damage event, never an attacker position. */
void human_callouts_damage(long unit_index, long enemy_player_index, real_vector3d const *incoming);
/* A nearby teammate's short-lived approximate danger area; no enemy identity. */
boolean human_callouts_nearest_alert(long team_index, real_point3d const *from, real_point3d *point);
#endif
