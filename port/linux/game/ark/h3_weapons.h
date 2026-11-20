/*
H3_WEAPONS.H

Halo 3's weapons made into this engine's (h3_weapons.c, compiled as a part
of h2_level.c): what another lane asks of them.
*/

#ifndef H3_WEAPONS_H
#define H3_WEAPONS_H

/* the made Halo 3 weapon a weap tag name stands for - its full path
(objects\weapons\rifle\battle_rifle\battle_rifle) or its last part
(battle_rifle) -: its tag index from the Halo 2/3 weapons' first tag
(h2_level.c's h2w_first, as h2_weapons_for: i * H2W_TAGS_PER_WEAPON), NONE
if none is made. Good after the level's weapons are gathered
(h3_weapons_gather, with HALO_H3_WEAPONS=1), while that level is made */
long h3_weapons_for(char const *h3_name);

#endif
