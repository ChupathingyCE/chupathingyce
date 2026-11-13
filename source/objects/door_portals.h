/*
DOOR_PORTALS.H

port: closed doors shut their portals to the dynamic lights (door_portals.c).
*/

#ifndef __DOOR_PORTALS_H
#define __DOOR_PORTALS_H

/* ---------- prototypes/DOOR_PORTALS.C */

/* a new map's doors are other doors */
void door_portals_new_map(void);
/* the portals the doors resting closed shut this frame (each frame, before
the lights are submitted) */
void door_portals_update(void);
/* a rasterizer light just submitted, from its cluster (its object's, for a
light on an object), with its light partition's sphere (center, radius): the
clusters it reaches only through a shut portal, which its passes over the
level and the objects' lights then leave out */
void door_portals_light(long rasterizer_light_index, short cluster_index, real_point3d const *center,
	real radius);
/* whether a light (its rasterizer index) is behind a shut door from a
cluster */
boolean door_portals_light_blocked(long rasterizer_light_index, short cluster_index);
/* a light's clusters without those behind a shut door; how many are left */
short door_portals_light_filter(long rasterizer_light_index, short count, short *clusters);

#endif // __DOOR_PORTALS_H
