/*
H2_CAMPAIGN.H

port: a Halo 2 campaign level played by this engine (h2_level.c makes it
from the player's own map): which of its structure BSPs each of its
objects is placed in, as Halo 2 places them, and its BSPs by Halo 2's
names (switch_bsp_by_name, for its scripts once they are translated).

A placement's BSPs are Halo 2's: its BSP policy (default: the BSP it was
placed in, its origin BSP; always placed: every BSP; manual: the BSPs its
manual BSP flags name). h2_level.c writes them in the placement's unused
word (scenario_object_datum, 0x24): a mark at 0x25 and the BSPs, a bit
each, at 0x26. Scenery (removed and placed again at every BSP switch,
object_types.c) is placed in a BSP only if Halo 2 places it there; the
other objects are made when the first BSP they are in is first loaded.
*/

#ifndef __H2_CAMPAIGN_H
#define __H2_CAMPAIGN_H
#pragma once

#include "cseries.h"

enum
{
	H2_CAMPAIGN_PLACEMENT_MARK_OFFSET = 0x25,
	H2_CAMPAIGN_PLACEMENT_MARK = 0xB5,
	H2_CAMPAIGN_PLACEMENT_BSPS_OFFSET = 0x26,
};

struct scenario_object_datum;

/* (object_types.c) TRUE and the BSPs a Halo 2 level's placement is in (bit
k: BSP k), FALSE when it says none (not a Halo 2 level's, or an object
placed as this engine always did) */
boolean h2_campaign_placement_bsps(
	struct scenario_object_datum const *placement,
	unsigned short *bsps);

/* the loaded scenario's structure BSP whose tag is named name (the last
part of its tag's name, Halo 2's: "01_bsp_0", "earthcity_3b"), NONE if
none */
short h2_campaign_structure_bsp_index(
	char const *name);

/* a script's switch_bsp_by_name: that BSP loaded (scenario_switch_structure_bsp,
as switch_bsp does); FALSE if the scenario has none so named */
boolean h2_campaign_switch_bsp_by_name(
	char const *name);

/* Halo 2's campaign levels in the order they are played (00a, 01a, 01b, 03a,
03b, 04a, 04b, 05a, 05b, 06a, 06b, 07a, 08a, 07b, 08b: Uprising comes before
High Charity, as the levels' scripts lead on): their count, their names
("03a_oldmombasa"), and the place of a level named by its scenario's path,
its tag's name or its name (NONE: not a campaign level) */
long h2_campaign_level_count(
	void);
char const *h2_campaign_level_name(
	long index);
long h2_campaign_level_index(
	char const *map_name);

/* (h2_scripts.c) the campaign's functions of Halo 2's scripts played here:
unit_add_equipment (the level's starting profiles, Halo 2's), the nav
points (activate_team_nav_point_*, breadcrumbs_*: Halo 2's waypoint
arrows, h2_hud.c) */
struct h2_scripts_native;
struct h2_scripts_native const *h2_campaign_natives(
	long *count);

/* (hud_nav_points.c) the point a script's nav point stands at (its
reference: breadcrumbs_activate_team_nav_point_position's); FALSE if not
known */
boolean h2_campaign_nav_point_position(
	long index,
	real_point3d *position);

/* (main.c, a level won) the Halo 2 campaign's level after map_name (its
scenario's path or its map's name), as Halo 2 goes on: 01a The Armory to
01b Cairo Station ...; NULL after the last or for a map not in it */
char const *h2_campaign_next_level(
	char const *map_name);

#endif
