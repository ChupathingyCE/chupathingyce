/*
MATCH_LIGHT_LIMITS.H

The original Xbox's rule for a multiplayer match's lights, decided at the
match's start from its players and vehicles (the retail game's
game_engine_build_lighting; source/game/game_engine.c):
- no dynamic lights with 13 or more players, or with 9 or more players and
  2 or more vehicles;
- no flashlights with 5 or more players, or with 4 or more vehicles, or
  when there are no dynamic lights.

Here it decides only what is drawn (HALO_CONSOLE_MATCH_LIGHT_LIMITS: the
original Xbox's GPU). The retail game also stopped the flashlight itself
(its toggle, its battery, its effect: unit_update), which is simulation; a
console in a game with a PC, another console or an OpenCE player keeps the
flashlight's simulation as every other machine runs it, and only its beam
goes undrawn. Compiled alone by tools/test_console_light_limits.py.
*/

#ifndef __MATCH_LIGHT_LIMITS_H
#define __MATCH_LIGHT_LIMITS_H
#pragma once

struct match_light_limits
{
	int no_dynamic_lights;
	int no_flashlights;
};

static struct match_light_limits match_light_limits_for(long player_count, long vehicle_count)
{
	struct match_light_limits limits;

	limits.no_dynamic_lights = (player_count > 8 && vehicle_count >= 2) || player_count >= 13;
	limits.no_flashlights = player_count > 4 || vehicle_count >= 4 || limits.no_dynamic_lights;
	return limits;
}

#endif
