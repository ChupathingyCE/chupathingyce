/*
H2_LEVEL_FAST.H

(perf/ce-floor-2) "Is this a Halo 2 level?" for the hot callers, inline:
h2_rules_level (h2_rules.h), h2_scripts_level, h2_damage_rules and
h2_player_physics_level are functions every object, unit, projectile, AI
action and damage call asked through a call on Halo CE's levels too (~130
call sites). Their answers are kept until the scenario or the map changes;
these read the kept answer while it is current, else call the function.
The same answers.
*/

#ifndef __H2_LEVEL_FAST_H
#define __H2_LEVEL_FAST_H
#pragma once

/* h2_rules.c */
boolean h2_rules_level(void);
/* (perf/ce-floor-2) the same answer, inline while the one h2_rules_level
kept is current (the same scenario, generation unchanged): a load and two
compares for the callers asking per object, per ray or per particle, where
Halo CE's tick made ~2500 calls a tick (Pi b30 fight) */
extern unsigned long halo_scenario_generation;
extern long global_scenario_index;
extern unsigned long h2_rules_level_seen;
extern long h2_rules_level_seen_index;
extern boolean h2_rules_level_answer;
static __inline boolean h2_rules_level_fast(void)
{
	if (h2_rules_level_seen == halo_scenario_generation && h2_rules_level_seen_index == global_scenario_index)
		return h2_rules_level_answer;
	return h2_rules_level();
}

/* h2_scripts.c: h2_rules_level's answer */
static __inline boolean h2_scripts_level_fast(void)
{
	return h2_rules_level_fast();
}

/* h2_damage.c: its level's answer, kept with the scenario and the map's
generation it was found for (h2_damage_rules sets them) */
boolean h2_damage_rules(void);
extern unsigned long halo_map_generation;
extern long h2_damage_rules_seen_scenario;
extern unsigned long h2_damage_rules_seen_generation;
extern boolean h2_damage_rules_answer;
static __inline boolean h2_damage_rules_fast(void)
{
	if (h2_damage_rules_seen_scenario == global_scenario_index && h2_damage_rules_seen_generation == halo_map_generation)
		return h2_damage_rules_answer;
	return h2_damage_rules();
}

/* h2_player_physics.c: set as each level loads */
extern boolean h2_player_physics_is_level;
static __inline boolean h2_player_physics_level_fast(void)
{
	return h2_player_physics_is_level;
}

#endif
