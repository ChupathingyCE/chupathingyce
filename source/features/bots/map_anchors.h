/*
MAP_ANCHORS.H

port: a map's tactical anchors in world coordinates (map_anchors.c): team
starting locations, netgame flags (CTF flags and vehicles, oddball spawns,
hills, teleporter sources and targets) and weapon/powerup spawns, read from
the scenario and placed on the BSP walkable polygons. The semantic map layer
starts from these instead of from an overhead image.
*/

#ifndef HALO_MAP_ANCHORS_H
#define HALO_MAP_ANCHORS_H
#pragma once

#include "cseries/cseries.h"

/* Each tick after navigation_world_update_for_players: when a new walkable
 * resource is published, re-place the anchors on it (bounded per tick), then
 * log them and write the debug.map_anchors_dump file once. */
void map_anchors_update(void);
void map_anchors_reset(void);

/* The CTF flag of a team (0 red, 1 blue), whatever the game type: the map's
 * base for that team. FALSE when the map has none for it. */
boolean map_anchors_team_base(long team_index, real_point3d *position);

#endif /* HALO_MAP_ANCHORS_H */
