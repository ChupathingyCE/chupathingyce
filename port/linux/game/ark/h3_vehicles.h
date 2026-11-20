/*
H3_VEHICLES.H

Halo 3's vehicles made into this engine's (h3_vehicles.c, compiled as a part
of h2_level.c after h3_weapons.c): what another lane asks of them.
*/

#ifndef H3_VEHICLES_H
#define H3_VEHICLES_H

/* the made Halo 3 vehicle a vehi tag name stands for - its full path
(objects\vehicles\warthog\warthog) or its last part (warthog) -: its tag
index from the vehicles' first tag (h2_level.c's vehicles' first, as
h2_vehicles_for: the vehicle's v->tag), NONE if none is made. Good after
the level's vehicles are gathered (h2_vehicles_gather, with
HALO_H3_VEHICLES=1), while that level is made */
long h3_vehicles_for(char const *h3_name);

#endif
