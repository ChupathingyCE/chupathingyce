/*
DELTA_MAPS.H

Halo PC maps' identity for Delta Peer's ce_maps capability (delta_maps.c,
docs/delta.md "Map identity").
*/

#ifndef HALO_DELTA_MAPS_H
#define HALO_DELTA_MAPS_H

#include "delta_wire.h"

/* the identity of the map map_name names (a scenario's name or path, or
<file>@ce, @md, @pc: halo_map_families.h), as MAP carries it: 1 when map
holds it (an Xbox map's name alone, a Halo PC map's with its file's size and
hash), 0 while the file's hash is being made (on a thread of its own: asked
again a frame later), -1 if there is none (no such file here, or a name MAP
cannot carry) */
int delta_maps_identity(const char *map_name, struct delta_wire_map *map);

/* the map name a MAP's map has on this build (<file><family's suffix>) into
name: 0 for a family this build does not know, or a name MAP cannot carry */
int delta_maps_name(const struct delta_wire_map *map, char *name, int size);

#endif
