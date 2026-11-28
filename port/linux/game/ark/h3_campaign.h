/*
H3_CAMPAIGN.H

A Halo 3 mission's squads placed as its own scripts place them
(h3_campaign.c). A Halo 3 level here (h3_level.c) has this engine's
encounters made from Halo 3's squads (h3_characters.c); nothing runs Halo
3's scripts. This file reads the mission's script trees from the player's
map as the level is made and, while it is played, walks the parts of them
that decide when a squad appears: sleep_until on a trigger volume the
player enters, on a squad's living count, on a script global; wake, sleep,
set, if, a static script's call; ai_place, ai_erase, ai_kill,
ai_allegiance and switch_zone_set. Every other function is skipped (a
statement) or unknown (a value: a wait on an unknown value passes at
once), so cutscenes, objectives, dialogue, vehicles and command scripts do
not happen. Unknown on purpose: the counts of squads that are all vehicles'
crews (never made here), a global only command scripts set, and the
missions' own "is the cutscene played" scripts answer no.

A mission is played a structure BSP at a time (h3_level.c makes one). The
zone set the scripts and the zone sets' trigger volumes switch to says
which BSPs Halo 3 has loaded; when the player stands in another of them
than the one made, the level is made again on it with the player where he
stood, the scripts going on where they were. A death there begins that BSP
again with the scripts as they were when he came into it.

Switches: HALO_H3_SCRIPTS=0 (nothing walked: no squad is placed),
HALO_H3_SEGMENTS=0 (the BSP never changes), HALO_H3_INSERTION=n (the
mission's insertion point), HALO_H3_AI_DEBUG=1|2 (logs), and for tests
HALO_H3_AI_AUTOWALK=ticks (the player put in the next trigger volume a
script waits for him in) with HALO_H3_AI_AUTOWALK_MEET=distance (then stood
that far from the first enemy squad placed).

Desktop only (PC, not the Vita, not the null renderer build): elsewhere
the calls do nothing.
*/

#ifndef __H3_CAMPAIGN_H
#define __H3_CAMPAIGN_H

#include "cseries.h"

struct h3_cache;

/* (the level's make) the mission's scripts, globals, trigger volumes and
squad groups read from its scenario (its tag data's address); nothing kept
if it has no scripts or HALO_H3_SCRIPTS=0 */
void h3_campaign_gather(
	struct h3_cache *cache,
	unsigned long scenario);

/* (the level's make) Halo 3's squad h3_squad is this engine's encounter
(NONE: not made) */
void h3_campaign_squad_encounter(
	long h3_squad,
	short encounter);

/* (the level's make) nothing gathered, no squads: a level that is not a
mission's */
void h3_campaign_clear(void);

/* a tick of the game (hs_runtime.c) */
void h3_campaign_update(void);

#endif
