/*
BOTS.C

Multiplayer bots. A bot is a player the machine running the game drives
itself: every tick it gets a player_action from bot_think, the same input a
person's controller or a network client's command gives, so bots play by
exactly the same rules as everyone else.

A bot, each tick:
	- looks for enemies it can see, remembering the last one for a while
	- aims with a limited turn rate, a reaction delay on first sight and
	  aim error that settles while it tracks, leading targets by the
	  flight time of its weapon's projectile
	- fights: fires on target, strafes and jumps, keeps a distance that
	  suits its weapon, melees up close, throws grenades, reloads
	- chooses where to go: power weapons, powerups, better weapons,
	  vehicles (its own, or a seat in a teammate's in team games), the
	  enemy it last saw, or somewhere to roam
	- gets there over a navigation graph (bots_nav below) that starts
	  from the map's spawn, item and flag points and grows from where
	  players walk and drive
	- drives, or mans a vehicle's gun, when it is in one

Every bot has a personality: how aggressive it is, how it likes to fight,
how much it wants vehicles and power weapons, how often it throws grenades
and jumps. Skill (bot_skill 1-5) scales reaction time, turn rate and aim,
and narrows each bot's own accuracy, reactions and agility toward the best.
*/

#include "bot_internal.h"
#include "tag_files/files.h" /* the navigation cache's folder */

#include "game/game_allegiance.h"
#include "game/game_engine.h"
#include "game/game_engine_king.h"
#include "items/projectile_definitions.h"
#include "items/weapon_definitions.h"
#include "items/weapons.h"
#include "memory/data.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "physics/collision_bsp_definitions.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "cache/cache_files.h"
#include "tag_files/tag_groups.h"
#include "units/bipeds.h"
#include "units/biped_definitions.h"
#include "units/unit_definitions.h"
#include "units/units.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


struct player_starting_location *player_get_starting_location(short starting_location_index);
void game_engine_player_added(long player_index);
long player_new(long machine_index, long player_index, short local_player_index, struct network_player const *network_player);

long bot_skill = 3; /* 1 easy, 2 normal, 3 hard, 4 legendary, 5 near perfect */

/* flag juggling (a carrier dropping the flag ahead on the run and snatching
it up again, to be home quicker): 1 to have the bots do it */
#define BOT_FLAG_JUGGLING 0

/* ---------- small helpers */

real bot_angle_difference(real a, real b)
{
	real d = a - b;
	while (d > BOT_PI)
		d -= BOT_TWO_PI;
	while (d < -BOT_PI)
		d += BOT_TWO_PI;
	return d;
}

real bot_wrap(real a)
{
	while (a >= BOT_TWO_PI)
		a -= BOT_TWO_PI;
	while (a < 0.f)
		a += BOT_TWO_PI;
	return a;
}

real bot_distance2d(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x;
	real dy = a->y - b->y;
	return (real)sqrt(dx * dx + dy * dy);
}

real bot_yaw_to(real_point3d const *from, real_point3d const *to)
{
	return bot_wrap((real)atan2(to->y - from->y, to->x - from->x));
}

static unsigned long bot_hash(unsigned long value)
{
	value ^= value >> 16;
	value *= 0x7feb352dUL;
	value ^= value >> 15;
	value *= 0x846ca68bUL;
	value ^= value >> 16;
	return value;
}

/* (objects are only tried at all with the objects bit set, whichever
   kinds are asked for: without it a line of sight went straight through
   every rock, tree, crate and door that's an object and not the map) */
static unsigned long const bot_sight_flags =
	FLAG(_collision_test_front_facing_surfaces_bit) |
	FLAG(_collision_test_ignore_invisible_surfaces_bit) |
	FLAG(_collision_test_structure_bit) |
	FLAG(_collision_test_objects_bit) |
	FLAG(_collision_test_objects_scenery_bit) |
	FLAG(_collision_test_objects_machines_bit);

static unsigned long const bot_move_flags =
	FLAG(_collision_test_front_facing_surfaces_bit) |
	FLAG(_collision_test_structure_bit) |
	FLAG(_collision_test_objects_bit) |
	FLAG(_collision_test_objects_scenery_bit) |
	FLAG(_collision_test_objects_machines_bit);

static int bot_line_clear(unsigned long flags, real_point3d const *from, real_point3d const *to, long ignore)
{
	struct collision_result collision;
	real_vector3d vector;

	vector.i = to->x - from->x;
	vector.j = to->y - from->y;
	vector.k = to->z - from->z;
	return !collision_test_vector(flags, from, &vector, ignore, &collision);
}

/* distance free of walls along a horizontal direction */
real bot_probe(real_point3d const *from, real yaw, real length, long ignore)
{
	struct collision_result collision;
	real_vector3d vector;

	vector.i = (real)cos(yaw) * length;
	vector.j = (real)sin(yaw) * length;
	vector.k = 0.f;
	if (!collision_test_vector(bot_move_flags | FLAG(_collision_test_objects_vehicles_bit), from, &vector, ignore, &collision))
		return length;
	return collision.t * length;
}

/* the ground under a point, within depth; FALSE over a drop */
int bot_ground(real_point3d const *point, real depth, long ignore, real *ground_z)
{
	struct collision_result collision;
	real_vector3d vector = { 0.f, 0.f, 0.f };

	vector.k = -depth;
	/* upward-facing surfaces only: the back of a surface met on the way down
	   is the underside of a ceiling or the outside of the level's shell, and
	   standing there is being inside something */
	if (!collision_test_vector(bot_move_flags, point, &vector, ignore, &collision))
		return FALSE;
	if (ground_z)
		*ground_z = collision.point.z;
	return TRUE;
}

int bot_unit_alive(long unit_index)
{
	struct object_datum *unit = unit_index != NONE ? object_try_and_get(unit_index) : NULL;
	return unit && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit) && unit->object.body_vitality > 0.f;
}

/* (asked many times a tick of the same few tags: remembered, per map) */
static long bot_name_cache_map = 1;
static int bot_name_has(long definition_index, char const *text)
{
	static struct { long definition_index; char const *text; int result; long map; } cache[1024];
	unsigned long slot = bot_hash((unsigned long)definition_index * 31u + (unsigned long)(size_t)text) & 1023;
	char const *name;
	if (definition_index == NONE)
		return FALSE;
	if (cache[slot].text == text && cache[slot].definition_index == definition_index && cache[slot].map == bot_name_cache_map)
		return cache[slot].result;
	name = tag_get_name(definition_index);
	cache[slot].map = bot_name_cache_map;
	cache[slot].definition_index = definition_index;
	cache[slot].text = text;
	cache[slot].result = name && strstr(name, text) != NULL;
	return cache[slot].result;
}

/* ---------- the map (the area awareness system: bot_nav.c, bot_aas.c)

The map is turned into a heightfield from every collision surface, then
cut into convex areas kept a body's width away from walls, trees and
rocks: anywhere in an area can be walked to in a straight line from
anywhere else in it. Areas are joined by reachabilities: a walk across
their shared edge, a jump up a ledge, a drop, a jump over a gap, a ladder,
a teleporter. Vehicles get their own set of areas, kept a warthog's width
off walls, with no stairs and no jumps, so drivers plan with vehicle rules
instead of walking ones. Worked out once per map and cached. */

#define NAV_DANGER_TEAMS 16
#define AAS_FOOT_SPEED_BOT 2.25f /* a Spartan's run, world units a second */
/* when each side last had someone in each 16-unit square of the map */
#define NAV_VISIT_CELLS 64
#define NAV_VISIT_LEVELS 8 /* (3-unit bands of height: one floor over another; 24 units before a band comes round again) */
static long nav_visited[NAV_DANGER_TEAMS][NAV_VISIT_CELLS * NAV_VISIT_CELLS * NAV_VISIT_LEVELS];
static real nav_vehicle_bonus; /* 0 on small maps .. 0.8 on big ones: how much everyone wants a vehicle */
static int nav_flood_done; /* the map's areas are ready (or there are none to be had) */
static short nav_aas_bsp = -1;
static int nav_aas_tried;

static long *nav_visit_slot(long team, real_point3d const *point)
{
	long x = ((long)floor(point->x / 16.f) % NAV_VISIT_CELLS + NAV_VISIT_CELLS) % NAV_VISIT_CELLS;
	long y = ((long)floor(point->y / 16.f) % NAV_VISIT_CELLS + NAV_VISIT_CELLS) % NAV_VISIT_CELLS;
	long z = (((long)floor(point->z / 3.f)) % NAV_VISIT_LEVELS + NAV_VISIT_LEVELS) % NAV_VISIT_LEVELS;
	return &nav_visited[((team % NAV_DANGER_TEAMS) + NAV_DANGER_TEAMS) % NAV_DANGER_TEAMS][(z * NAV_VISIT_CELLS + y) * NAV_VISIT_CELLS + x];
}

static long nav_place_count(void)
{
	return aas_ready() ? aas_place_count() : 0;
}

static real_point3d nav_random_place(real random)
{
	long count = nav_place_count();
	return *aas_place((long)(random * count) % count);
}

/* the part of a very large map the spots are to be in (NONE: anywhere) */
static long nav_spot_region = NONE;

/* the part of a very large map a place is in, on foot (NONE: none it is
   near, or a map with navigation all over, where it doesn't matter) */
static long nav_region_at(real_point3d const *point)
{
	long area = aas_ready() && navm_in_parts() ? aas_nearest_area(AAS_FOOT, point, 3.f, NULL) : NONE;
	return area != NONE ? aas_area_region(AAS_FOOT, area) : NONE;
}

/* somewhere to go on the map (a playable area, bigger ones likelier) */
static int nav_random_spot(unsigned long seed, real_point3d *spot)
{
	long area = NONE;
	if (nav_spot_region != NONE && aas_ready())
	{
		/* one of this part's own (any alike: a part is a base and the
		   ground about it) */
		long count = aas_region_size(AAS_FOOT, nav_spot_region);
		long tries;
		for (tries = 0; tries < 12 && count > 0; tries++)
		{
			long a;
			seed = bot_hash(seed + 0x9e3779b9UL);
			a = aas_region_area(AAS_FOOT, nav_spot_region, (long)(seed % (unsigned long)count));
			if (a != NONE && ((aas_area(AAS_FOOT, a)->flags & _aas_area_playable) || tries >= 8))
			{
				*spot = aas_area(AAS_FOOT, a)->center;
				return TRUE;
			}
		}
		return FALSE;
	}
	/* half the time any playable area alike (the walkways, the corridors,
	   the upper floors: not just the open ground, which bigger-likelier
	   favours) */
	if (aas_ready() && (seed & 1))
	{
		long count = aas_area_count(AAS_FOOT);
		long tries;
		for (tries = 0; tries < 12 && count > 0; tries++)
		{
			long a;
			struct aas_area const *candidate;
			seed = bot_hash(seed + 0x9e3779b9UL);
			a = (long)(seed % (unsigned long)count);
			candidate = aas_area(AAS_FOOT, a);
			if ((candidate->flags & _aas_area_playable) && (candidate->x1 - candidate->x0) * (candidate->y1 - candidate->y0) >= 0.8f)
			{
				area = a;
				break;
			}
		}
	}
	if (area == NONE)
		area = aas_ready() ? aas_random_area(AAS_FOOT, seed) : NONE;
	if (area == NONE)
		return FALSE;
	*spot = aas_area(AAS_FOOT, area)->center;
	return TRUE;
}

/* Where a vehicle can get closest to point from where it is: somewhere to
   drive to and get out when a vehicle can't get there itself. The areas it
   can drive to are the ones the reachabilities lead to from the area it is
   in, the way they go: a drop is one way, so what is down it can be got to
   and what is up it cannot. Worked out for the area the vehicle is in, and
   kept while it stays in it. */
static long *nav_vehicle_reached; /* per vehicle area: the number of the search that got to it */
static long *nav_vehicle_queue;
static long nav_vehicle_reached_count;
static long nav_vehicle_reached_from = NONE;
static long nav_vehicle_reached_search;

static void nav_vehicle_forget(void)
{
	free(nav_vehicle_reached);
	free(nav_vehicle_queue);
	nav_vehicle_reached = NULL;
	nav_vehicle_queue = NULL;
	nav_vehicle_reached_count = 0;
	nav_vehicle_reached_from = NONE;
}

static int nav_vehicle_dropoff(real_point3d const *from, real_point3d const *point, real_point3d *spot)
{
	long count = aas_ready() ? aas_area_count(AAS_VEHICLE) : 0;
	long here;
	long a;
	real best = 1.0e9f;
	if (count <= 0)
		return FALSE;
	if (!nav_vehicle_reached || nav_vehicle_reached_count != count)
	{
		nav_vehicle_forget();
		nav_vehicle_reached = (long *)calloc(count, sizeof(long));
		nav_vehicle_queue = (long *)malloc(count * sizeof(long));
		if (!nav_vehicle_reached || !nav_vehicle_queue)
		{
			nav_vehicle_forget();
			return FALSE;
		}
		nav_vehicle_reached_count = count;
		nav_vehicle_reached_search = 0;
	}
	here = aas_nearest_area(AAS_VEHICLE, from, 6.f, NULL);
	if (here == NONE)
		return FALSE;
	if (here != nav_vehicle_reached_from)
	{
		long head = 0, tail = 0;
		nav_vehicle_reached_from = here;
		nav_vehicle_reached_search++;
		nav_vehicle_reached[here] = nav_vehicle_reached_search;
		nav_vehicle_queue[tail++] = here;
		while (head < tail)
		{
			struct aas_area const *area = aas_area(AAS_VEHICLE, nav_vehicle_queue[head++]);
			long r;
			for (r = area->first_reach; r < area->first_reach + area->reach_count; r++)
			{
				long to = aas_reach(AAS_VEHICLE, r)->to;
				if (nav_vehicle_reached[to] != nav_vehicle_reached_search)
				{
					nav_vehicle_reached[to] = nav_vehicle_reached_search;
					nav_vehicle_queue[tail++] = to;
				}
			}
		}
	}
	for (a = 0; a < count; a++)
	{
		struct aas_area const *area = aas_area(AAS_VEHICLE, a);
		real d;
		if (nav_vehicle_reached[a] != nav_vehicle_reached_search)
			continue;
		d = distance3d(&area->center, point);
		if (d < best)
		{
			best = d;
			*spot = area->center;
		}
	}
	return best < 1.0e8f;
}

static void bots_find_vantages(void);
static long bots_vehicle_count;



/* what each side knows, for routes round danger. Each side pools what its
   bots see, hear and have on their motion trackers (where the enemy is),
   and where its people died. From that, every two seconds, what it costs (in
   seconds) to go through each area: near where the enemy is, and in their
   line of sight (more so below them: they have the height), and a little
   where our own people are (so we spread out and come at them from more
   than one way). Routes are planned round it: a covered stretch is worth a
   longer way round, to the enemy, the flag, wherever. Sides: the two teams
   of a team game (cost slots 0 and 1), everyone for themselves (slot 2) */

#define BOT_KNOWN 24
#define BOT_KNOWN_SECONDS 6.f

struct bot_known
{
	long unit; /* NONE: a place one of ours died */
	real_point3d at;
	long seen; /* game time */
};

/* Working a side's costs out is a look at every area for everything the
   side knows of, with a line of sight for most: over a thousand of them on
   a big map. All in one tick it was the bots' longest tick, every second.
   So it is done a part each tick (so many lines of sight, so many areas
   looked at), and the costs are the routes' once it is all done; then the
   other side's; and a side's again no sooner than BOT_DANGER_PERIOD after
   its last began. */
#define BOT_DANGER_PERIOD (2 * BOT_TICKS_PER_SECOND)
#define BOT_DANGER_RAYS_A_TICK 40
#define BOT_DANGER_AREAS_A_TICK 6000
#define BOT_DANGER_RAYS 1200 /* an update's most: past it, what is known of counts for less, unseen */

struct bot_side
{
	struct bot_known known[BOT_KNOWN];
	long known_count;
	float *cost; /* per foot area: being worked out */
	long cost_count;
	/* the update going on: the next of the known and of the areas to look
	   at, the lines of sight so far, and whether there is anything */
	boolean updating;
	long update_known;
	long update_area;
	long update_rays;
	boolean update_any;
	long update_began; /* game time: the last one */
};

static struct bot_side bot_sides[AAS_COST_SLOTS];

static long bot_side_of(struct player_datum const *player)
{
	if (game_engine_has_teams())
		return player->team_index == 0 || player->team_index == 1 ? player->team_index : NONE;
	return 2;
}

static void nav_danger_reset(void)
{
	long side;
	for (side = 0; side < AAS_COST_SLOTS; side++)
	{
		free(bot_sides[side].cost);
		memset(&bot_sides[side], 0, sizeof(bot_sides[side]));
	}
}

static void bot_side_saw(long side, long unit, real_point3d const *at)
{
	struct bot_side *knowledge;
	long now = game_time_get();
	long i;
	long oldest = 0;
	if (side < 0 || side >= AAS_COST_SLOTS)
		return;
	knowledge = &bot_sides[side];
	for (i = 0; i < knowledge->known_count; i++)
	{
		if (unit != NONE && knowledge->known[i].unit == unit)
		{
			knowledge->known[i].at = *at;
			knowledge->known[i].seen = now;
			return;
		}
		if (knowledge->known[i].seen < knowledge->known[oldest].seen)
			oldest = i;
	}
	if (knowledge->known_count < BOT_KNOWN)
		oldest = knowledge->known_count++;
	knowledge->known[oldest].unit = unit;
	knowledge->known[oldest].at = *at;
	knowledge->known[oldest].seen = now;
}

/* the nearest enemy the side knows of lately (FALSE: none) */
static int bot_side_nearest_known(long side, real_point3d const *from, real seconds, real_point3d *at)
{
	struct bot_side const *knowledge;
	long now = game_time_get();
	real best = 1.0e9f;
	long i;
	if (side < 0 || side >= AAS_COST_SLOTS)
		return FALSE;
	knowledge = &bot_sides[side];
	for (i = 0; i < knowledge->known_count; i++)
	{
		struct bot_known const *known = &knowledge->known[i];
		real distance;
		if (known->unit == NONE || now - known->seen > (long)(seconds * BOT_TICKS_PER_SECOND) || !bot_unit_alive(known->unit))
			continue;
		distance = distance3d(&known->at, from);
		if (distance < best)
		{
			best = distance;
			*at = known->at;
		}
	}
	return best < 1.0e8f;
}

/* a part of the side's update (begun, if none is going on): TRUE once it
   is done and its costs are the routes' */
static boolean bot_side_update_costs(long side)
{
	struct bot_side *knowledge = &bot_sides[side];
	long now = game_time_get();
	long area_count = aas_area_count(AAS_FOOT);
	long rays = 0;
	long looked = 0;
	long a;
	struct data_iterator iterator;
	struct player_datum *player;
	unsigned long const flags = FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit);

	if (area_count <= 0)
		return TRUE;
	if (knowledge->cost_count != area_count)
	{
		free(knowledge->cost);
		knowledge->cost = (float *)malloc(area_count * sizeof(float));
		knowledge->cost_count = knowledge->cost ? area_count : 0;
		knowledge->updating = FALSE;
		if (!knowledge->cost)
			return TRUE;
	}
	if (!knowledge->updating)
	{
		memset(knowledge->cost, 0, area_count * sizeof(float));
		knowledge->updating = TRUE;
		knowledge->update_known = 0;
		knowledge->update_area = 0;
		knowledge->update_rays = 0;
		knowledge->update_any = FALSE;
		knowledge->update_began = now;
	}
	for (; knowledge->update_known < knowledge->known_count; knowledge->update_known++, knowledge->update_area = 0)
	{
		struct bot_known const *known = &knowledge->known[knowledge->update_known];
		real age = (now - known->seen) / (real)BOT_TICKS_PER_SECOND;
		real fresh;
		real_point3d eye = known->at;
		if (age > BOT_KNOWN_SECONDS || (known->unit != NONE && !bot_unit_alive(known->unit)))
			continue;
		fresh = 1.f - age / BOT_KNOWN_SECONDS;
		eye.z += 0.3f;
		knowledge->update_any = TRUE;
		for (a = knowledge->update_area; a < area_count; a++)
		{
			struct aas_area const *area = aas_area(AAS_FOOT, a);
			real_point3d center;
			real distance;
			real danger;
			/* (enough for this tick: on from here in the next) */
			if (rays >= BOT_DANGER_RAYS_A_TICK || looked >= BOT_DANGER_AREAS_A_TICK)
			{
				knowledge->update_area = a;
				return FALSE;
			}
			looked++;
			if (!(area->flags & _aas_area_reachable))
				continue;
			center.x = PIN(known->at.x, area->x0, area->x1);
			center.y = PIN(known->at.y, area->y0, area->y1);
			center.z = area->center.z;
			distance = distance3d(&center, &known->at);
			if (distance > 30.f)
				continue;
			/* a place one of ours died: dangerous close by */
			if (known->unit == NONE)
			{
				if (distance < 8.f)
					knowledge->cost[a] += 1.f * fresh * (1.f - distance / 8.f);
				continue;
			}
			danger = 1.f * fresh * (1.f - distance / 30.f);
			/* below them: they have the height on us */
			if (known->at.z > area->center.z + 1.f)
				danger *= 1.f + MIN(1.f, (known->at.z - area->center.z) / 4.f);
			if (distance < 6.f)
				knowledge->cost[a] += danger;
			else if (knowledge->update_rays < BOT_DANGER_RAYS)
			{
				/* in their sight: covered */
				real_point3d target = area->center;
				target.z += 0.6f;
				rays++;
				knowledge->update_rays++;
				if (bot_line_clear(flags, &eye, &target, NONE))
					knowledge->cost[a] += danger;
			}
			else
				knowledge->cost[a] += danger * 0.3f;
		}
	}
	/* our own people: a little, so we spread out */
	if (side != 2)
	{
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			long area;
			if (bot_side_of(player) != side || player->unit_index == NONE || !bot_unit_alive(player->unit_index))
				continue;
			area = aas_point_area(AAS_FOOT, &object_get(object_get_ultimate_parent(player->unit_index))->object.position);
			if (area != NONE)
			{
				knowledge->cost[area] += 0.4f;
				knowledge->update_any = TRUE;
			}
		}
	}
	/* (per stretch of ground, not per area: a route over open ground
	   crosses many small areas, and seconds for each would have everyone
	   hugging the edges of the map, going round the back of the bases) */
	for (a = 0; a < area_count; a++)
	{
		struct aas_area const *area = aas_area(AAS_FOOT, a);
		real w = area->x1 - area->x0;
		real h = area->y1 - area->y0;
		knowledge->cost[a] *= MIN(1.f, (real)sqrt(w * w + h * h) / 5.f);
		knowledge->cost[a] = MIN(knowledge->cost[a], 3.f);
	}
	aas_set_area_costs(AAS_FOOT, side, knowledge->update_any ? knowledge->cost : NULL);
	knowledge->updating = FALSE;
	return TRUE;
}

/* each side's route costs, by turns: the one whose turn it is a part each
   tick until it is done */
static void bot_sides_update(void)
{
	static long turn;
	long first = game_engine_has_teams() ? 0 : 2;
	long count = game_engine_has_teams() ? 2 : 1;
	long side;

	if (turn < first || turn >= first + count)
		turn = first;
	side = turn;
	/* (not begun before it is due; game time starts over with each game) */
	if (!bot_sides[side].updating && bot_sides[side].cost && game_time_get() >= bot_sides[side].update_began &&
		game_time_get() - bot_sides[side].update_began < BOT_DANGER_PERIOD)
	{
		/* (the other side, if it is due) */
		turn = first + (turn - first + 1) % count;
		return;
	}
	if (bot_side_update_costs(side))
		turn = first + (turn - first + 1) % count;
}

/* the motion tracker: what a player's shows them (enemies moving faster
   than a crouch walk, shooting or throwing, within its range), when the
   game has it on */
static int bot_on_tracker(long player_index, real_point3d const *eye, long unit_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	real range = 25.f;
	real sensitivity = 0.0025f;
	real_point3d at;
	real_vector3d velocity;
	if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return FALSE;
	if (!game_engine_hud_draw_motion_sensor(player_index) || !game_engine_draw_object_in_motion_sensor(unit_index))
		return FALSE;
	at = unit->object.bounding_sphere_center;
	if (fabs(at.x - eye->x) >= range || fabs(at.y - eye->y) >= range)
		return FALSE;
	if (TEST_FLAG(unit->unit.control_flags, _unit_control_weapon_primary_trigger_bit) ||
		(unit->unit.grenade_throw_state != _unit_grenade_throw_idle && unit->unit.grenade_throw_state != _unit_grenade_throw_ending))
	{
		return TRUE;
	}
	object_get_velocities(object_get_ultimate_parent(unit_index), &velocity, NULL);
	return velocity.i * velocity.i + velocity.j * velocity.j + velocity.k * velocity.k >= sensitivity;
}

/* heard: gunfire in the last second and a half, as far as it carries (a
   rocket, a sniper shot further than a pistol) */
static int bot_heard(real_point3d const *eye, long unit_index, real *distance_out)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	long slot;
	real distance;
	real carries = 40.f;
	if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return FALSE;
	slot = unit->unit.current_weapon_index;
	if (slot < 0 || slot >= MAXIMUM_WEAPONS_PER_UNIT || unit->unit.weapon_last_used_at_game_time[slot] <= game_time_get() - BOT_SECONDS(1.5f))
		return FALSE;
	if (unit->unit.weapon_object_indices[slot] != NONE && object_try_and_get(unit->unit.weapon_object_indices[slot]))
	{
		long definition = object_get(unit->unit.weapon_object_indices[slot])->definition_index;
		if (bot_name_has(definition, "rocket") || bot_name_has(definition, "sniper") || bot_name_has(definition, "fuel rod") || bot_name_has(definition, "plasma_cannon"))
			carries = 70.f;
	}
	distance = distance3d(eye, &unit->object.bounding_sphere_center);
	if (distance_out)
		*distance_out = distance;
	return distance < carries;
}

/* ---------- personalities */

struct bot_personality
{
	real aggression;    /* 0 cautious .. 1 reckless: range, chasing, retreating */
	real accuracy;      /* aim error scale: 0.7 sharp .. 1.4 sloppy */
	real vehicle_love;  /* how much it wants vehicles */
	real greed;         /* how far it goes for power weapons and powerups */
	real grenade_happy;
	real jumpiness;
	real patience;      /* holds a spot (precision weapons) instead of roaming */
	real reaction;      /* reaction delay scale */
	real teamwork;      /* sticks with, covers and helps teammates */
	real focus;         /* the objective over a fight (flags, the ball, the hill, checkpoints) */
	real daring;        /* takes the risky way: long jumps, ladders under fire, dangerous routes */
	real agility;       /* how fast and how often it sidesteps in a fight */
	real whim;          /* how loosely it weighs its choices: 0 by the book .. 1 on a hunch */
	/* (unused: accuracy, reactions and agility come from the skill level) */
	real accuracy_luck;
	real reaction_luck;
	real agility_luck;
	long favorite;      /* the gun it likes best this game (_bot_gun_*) */
	/* what it has learned this game (kept through deaths) */
	long grenades_thrown;
	long grenades_hit;
	long kills_seen;
	long deaths_seen;
	/* the way it last went for the objective (-1 left of the way
	   straight there, 1 right, 0 straight), and whether it died on it */
	long route_side;
	int route_died;
	/* how far this game has moved its daring and teamwork (either way,
	   within a limit): added to its temperament's each life */
	real learned_daring;
	real learned_teamwork;
	real learned_aggression; /* (only up from where it started: 0 .. 1) */
	long learned_time; /* game time the game's length was last counted to */
	real taunt;         /* rubs it in: a teabag on whoever it just killed */
	/* character: the same all game, what makes one bot not another */
	real grudge;        /* how hard it goes after whoever keeps killing it */
	real temper;        /* a run of deaths: -1 goes timid .. 1 goes reckless */
	real swagger;       /* a run of kills: how far it goes to its head */
	long style;         /* how it fights (_bot_style_*) */
	long signature;     /* a habit of its own in a fight (_bot_signature_*) */
	real discipline;    /* the trigger: 0 holds it down .. 1 waits for the shot */
	real melee;         /* how readily it closes for a blow, and goes for backs */
	real vengeful;      /* (teams) after whoever just killed one of ours close by */
	real composure;     /* its aim when it is hit: 0 rattled .. 1 steady */
	real loyalty;       /* to its favourite gun */
	real sporting;      /* 1 leaves the busy and the just spawned alone .. 0 takes any kill */
	real showboat;      /* plays about: a spin after a kill, crouches at a friend */
	real camping;       /* how long it will sit in one place */
	real herd;          /* -1 keeps clear of its own side .. 1 trails the nearest of them */
	/* its run this game (kept through deaths): kills since it last died,
	   deaths since it last killed, and what that does to its aggression */
	long kill_streak;
	long death_streak;
	real mood;
};

/* how a bot fights */
enum
{
	_bot_style_duelist, /* at the gun's range, strafing */
	_bot_style_rusher, /* in close, and a blow to finish */
	_bot_style_lurker, /* holds a place and waits; does not chase */
	_bot_style_hit_and_run, /* breaks off when its shields are going, and comes back */
	NUMBER_OF_BOT_STYLES
};

/* a habit of its own */
enum
{
	_bot_signature_none,
	_bot_signature_crouch_strafe, /* ducks as it sidesteps */
	_bot_signature_jump_shot, /* in the air half the fight */
	_bot_signature_melee_finish, /* shields down and close: in for the blow */
	_bot_signature_grenade_opener, /* a grenade first, always */
	_bot_signature_backpedal, /* backs away firing when it is hit */
	NUMBER_OF_BOT_SIGNATURES
};

static char const *const bot_style_names[NUMBER_OF_BOT_STYLES] = { "duelist", "rusher", "lurker", "hit and run" };
static char const *const bot_signature_names[NUMBER_OF_BOT_SIGNATURES] =
	{ "no habit", "crouch strafe", "jump shots", "melee finish", "grenade first", "backpedal" };

/* ---------- this match's luck

Every match draws its bots afresh: who they are (their traits, a favourite
gun), and every choice they make after, from a seed taken from the clock
when the map starts. The same slot on the same map is someone else each
time. (Only the machine that runs the bots decides what they do, and sends
it: nothing else has to agree on this.) */

static unsigned long bots_match_seed;
long config_integer(char const *name);

static unsigned long bots_match(void)
{
	if (!bots_match_seed)
	{
		unsigned long long now = bot_time_microseconds();
		long fixed = config_integer("debug.random_seed");

		bots_match_seed = bot_hash((unsigned long)now ^ bot_hash((unsigned long)(now >> 32) ^ (unsigned long)clock())) | 1;
		/* (debug.random_seed: the same bots every time, to compare two builds) */
		if (fixed)
			bots_match_seed = bot_hash((unsigned long)fixed) | 1;
	}
	return bots_match_seed;
}

/* accuracy, reactions and agility: set by the skill level alone, better
   at every level up, the same for every bot at that level (like its
   aggression). At 1: aim error 1.3x, reactions 1.4x, sidestepping 0.3;
   at 5: 0.5x, 0.7x, 1 */
static real bot_skill_fraction(void);

/* how aggressive the bots start a game: set by the skill level, the same
   for every bot (1: 0.25 .. 5: 0.7). From there it climbs as the game goes:
   each kill makes a bot hungrier, the longer the game the hungrier they
   all get, and a death takes off a little, never below where it started
   (bot_learn_from_score) */
static real bot_skill_aggression(void)
{
	return 0.25f + 0.45f * bot_skill_fraction();
}

/* testing: turn this session's changes off per team, to pit them against
   the old behaviour in the same game (HALO_BOT_OFF_T0 / _T1: a list of
   switch, melee, threat, via, teabag, costslot, pace, notice, human, or
   "all") */
enum
{
	BOT_F_SWITCH = 2, BOT_F_MELEE = 4, BOT_F_THREAT = 8, BOT_F_VIA = 16,
	BOT_F_TEABAG = 32, BOT_F_COSTSLOT = 128, BOT_F_PACE = 256, BOT_F_NOTICE = 512,
	/* moving and looking as a person's hands do (the look eased into and
	   out of, the movement a thumb's, a person's strafing) */
	BOT_F_HUMAN = 1024,
};
static unsigned bot_off;

static unsigned bot_off_parse(char const *text)
{
	static struct { char const *name; unsigned bit; } const names[] =
	{
		{ "switch", BOT_F_SWITCH }, { "melee", BOT_F_MELEE }, { "threat", BOT_F_THREAT },
		{ "via", BOT_F_VIA }, { "teabag", BOT_F_TEABAG }, { "costslot", BOT_F_COSTSLOT }, { "pace", BOT_F_PACE },
		{ "notice", BOT_F_NOTICE }, { "human", BOT_F_HUMAN },
		{ "all", 0xFFFFu },
	};
	unsigned mask = 0;
	long i;
	if (!text)
		return 0;
	for (i = 0; i < (long)(sizeof(names) / sizeof(names[0])); i++)
		if (strstr(text, names[i].name))
			mask |= names[i].bit;
	return mask;
}

static unsigned bot_off_for_team(long team)
{
	static int read;
	static unsigned masks[2];
	if (!read)
	{
		read = TRUE;
		masks[0] = bot_off_parse(getenv("HALO_BOT_OFF_T0"));
		masks[1] = bot_off_parse(getenv("HALO_BOT_OFF_T1"));
	}
	{
		static int all_read;
		static unsigned all;
		if (!all_read)
		{
			all_read = TRUE;
			all = bot_off_parse(getenv("HALO_BOT_OFF"));
		}
		return all | (team == 0 || team == 1 ? masks[team] : 0);
	}
}
#define BOT_ON(bit) (!(bot_off & (bit)))
/* flanking on the way to someone (bot_plan_via) */
#define BOT_FLANKING 1

static void bot_skill_traits(struct bot_personality *personality)
{
	real t = bot_skill_fraction();
	personality->accuracy = 1.3f - 0.8f * t;
	personality->reaction = 1.4f - 0.7f * t;
	personality->agility = 0.3f + 0.7f * t;
}

static void bot_make_personality(long slot, struct bot_personality *personality)
{
	unsigned long seed = bot_hash((unsigned long)(slot * 2654435761UL) ^ 0x9e3779b9UL ^ bots_match());
#define BOT_TRAIT(low, high) ((low) + ((high) - (low)) * (real)((seed = bot_hash(seed)) & 0xFFFF) / 65535.f)
	/* (the better the bots, the hungrier: at 5 none of them hangs back) */
	seed = bot_hash(seed); /* (aggression's draw, when it had one: the rest stay as they were) */
	personality->aggression = bot_skill_aggression();
	personality->accuracy_luck = BOT_TRAIT(0.f, 1.f);
	personality->vehicle_love = BOT_TRAIT(0.f, 1.f);
	personality->greed = BOT_TRAIT(0.2f, 1.f);
	personality->grenade_happy = BOT_TRAIT(0.05f, 1.f);
	personality->jumpiness = BOT_TRAIT(0.1f, 1.f);
	personality->patience = BOT_TRAIT(0.f, 1.f);
	personality->reaction_luck = BOT_TRAIT(0.f, 1.f);
	personality->teamwork = BOT_TRAIT(0.1f, 1.f);
	personality->focus = BOT_TRAIT(0.2f, 1.f);
	personality->daring = BOT_TRAIT(0.f, 1.f);
	personality->agility_luck = BOT_TRAIT(0.f, 1.f);
	personality->whim = BOT_TRAIT(0.15f, 1.f);
	{
		/* most don't gloat; a few do, and a few are shameless about it */
		real t = BOT_TRAIT(0.f, 1.f);
		personality->taunt = t * t;
	}
	/* a favourite: the rifle, the pistol, the shotgun, the sniper or the
	   rockets, or the plasma rifle (nobody's is the needler: it is a gun to
	   be rid of) */
	{
		static long const guns[] = { 1, 2, 3, 4, 5, 7, 2 }; /* (_bot_gun_*, below; as many as there were) */
		personality->favorite = guns[(seed = bot_hash(seed)) % (sizeof(guns) / sizeof(guns[0]))];
	}
	/* its character (drawn after the rest: the traits above are as they
	   were) */
	{
		/* (two in five are duelists; the rest, a fifth each) */
		static long const styles[] = { _bot_style_duelist, _bot_style_duelist, _bot_style_rusher, _bot_style_lurker, _bot_style_hit_and_run };
		personality->style = styles[(seed = bot_hash(seed)) % (sizeof(styles) / sizeof(styles[0]))];
		personality->signature = (long)((seed = bot_hash(seed)) % NUMBER_OF_BOT_SIGNATURES);
	}
	personality->grudge = BOT_TRAIT(0.f, 1.f);
	personality->temper = BOT_TRAIT(-1.f, 1.f);
	personality->swagger = BOT_TRAIT(0.f, 1.f);
	personality->discipline = BOT_TRAIT(0.f, 1.f);
	personality->melee = BOT_TRAIT(0.f, 1.f);
	personality->vengeful = BOT_TRAIT(0.f, 1.f);
	personality->composure = BOT_TRAIT(0.f, 1.f);
	personality->loyalty = BOT_TRAIT(0.f, 1.f);
	personality->sporting = BOT_TRAIT(0.f, 1.f);
	personality->showboat = BOT_TRAIT(0.f, 1.f);
	personality->camping = BOT_TRAIT(0.f, 1.f);
	personality->herd = BOT_TRAIT(-1.f, 1.f);
	/* (a style is a lean of the traits it goes with, too) */
	switch (personality->style)
	{
	case _bot_style_rusher:
		personality->melee = MAX(personality->melee, 0.6f);
		personality->patience *= 0.5f;
		break;
	case _bot_style_lurker:
		personality->patience = MAX(personality->patience, 0.7f);
		personality->camping = MAX(personality->camping, 0.6f);
		break;
	}
	if (personality->signature == _bot_signature_melee_finish)
		personality->melee = MAX(personality->melee, 0.7f);
	if (personality->signature == _bot_signature_grenade_opener)
		personality->grenade_happy = MAX(personality->grenade_happy, 0.6f);
	if (personality->signature == _bot_signature_jump_shot)
		personality->jumpiness = MAX(personality->jumpiness, 0.7f);
#undef BOT_TRAIT
	bot_skill_traits(personality);
}

/* the places of its own a bot keeps going back to */
#define BOT_HAUNTS 3

/* ---------- bot state */
struct bot_state;
static void bot_learn_from_score(struct bot_state *bot, struct player_datum const *me);

#define BOT_RECENT_SPOTS 6

struct bot_state
{
	long unit_index; /* the unit this state belongs to (reset on respawn) */
	unsigned long random;
	struct bot_personality personality; /* this life's */
	struct bot_personality temperament; /* this match's, which each life varies */
	int has_personality;
	/* what it remembers this game (kept through deaths; gone with the
	   game): who has killed it and how often, and the places it keeps
	   going back to */
	struct
	{
		unsigned char killed_by[BOT_MAXIMUM_PLAYERS];
		real_point3d haunts[BOT_HAUNTS];
		long haunt_count;
		long haunt_visited[BOT_HAUNTS]; /* game time */
	} memory;
	/* aim */
	real yaw;
	real pitch;
	real aim_error_yaw;
	real aim_error_pitch;
	/* target */
	long target_unit;
	long seen_ticks;
	long lost_ticks;
	real_point3d last_seen;
	/* combat movement */
	long strafe_ticks;
	real strafe_sign;
	long jump_cooldown;
	long grenade_cooldown;
	long trigger_ticks;
	long melee_cooldown;
	/* goals */
	real_point3d goal;
	long goal_kind;
	long goal_object;
	long goal_ticks;
	long action_ticks; /* holding action at an item or seat */
	/* movement over the areas (bot_move.c) */
	struct bot_mover mover;
	/* stuck */
	real_point3d progress_point;
	long progress_ticks;
	long unstick_ticks;
	real unstick_yaw;
	real goal_score; /* how much the current goal was worth when chosen */
	long team; /* for the danger map */
	long seat_retries;
	long glance_ticks; /* standing guard: where we're looking */
	real glance_yaw;
	real last_health;
	long hit_ticks; /* just took damage */
	real_point3d fight_spot; /* higher ground to fight from */
	int fight_spot_valid;
	/* shields down: somewhere close out of their sight, to get them back */
	real_point3d cover_spot;
	int in_cover; /* going there, or there waiting */
	long cover_until; /* give up waiting (a push, or it's taking too long) */
	long cover_check_ticks;
	long shot_cooldown; /* firing: ticks to the next trigger pull */
	long burst_ticks;
	long burst_pause;
	long charge_ticks; /* plasma pistol: <0 cooling off after a charged shot */
	long charge_time; /* ...and the last tick it was held */
	long role;
	long seat_check_ticks;
	int goal_objective; /* get there even under fire (items, vehicles, flags) */
	/* a grenade on its way: aim (lobbed) at grenade_point, then throw */
	long grenade_pending_ticks;
	real_point3d grenade_point;
	short grenade_index;
	long opened_on_target; /* the target we opened on with a grenade */
	/* the last grenade's target, watched a few seconds to see if it told */
	long grenade_watch_unit;
	long grenade_watch_ticks;
	real grenade_watch_vitality;
	real circle_sign; /* driving round an enemy: which way */
	long circle_target; /* ...round whom (someone new: the way round's chosen afresh) */
	long headshot_ticks; /* going for the head: rolled afresh when this runs out */
	real headshot_roll;
	long ignore_object[6]; /* items or vehicles we failed to get, until game time ignore_until */
	long ignore_until[6];
	long last_weapon_definition;
	real goal_best_distance; /* closest we've been to the goal */
	long goal_progress_ticks;
	long last_vehicle;
	int last_seat_kind;
	long crouch_jump_ticks; /* a crouch jump: crouch from a few ticks in */
	/* an enemy we can't see but know is there: on the motion tracker, or
	   heard shooting */
	long tracked_unit;
	long item_streak; /* items gone for one after another */
	long grab_object; /* the better gun lying near that it broke off for */
	long item_route_ticks; /* by the thing it is after, and the route still taking it there: for how long */
	real_point3d explore_point; /* where we're exploring toward (until explore_until) */
	long explore_until;
	/* a way round of this trip's own: somewhere off the straight route to
	   go by first */
	real_point3d via_point;
	int via_valid;
	int via_pending;
	long via_ticks;
	long via_side;
	/* ... being looked for: a place tried each tick (bot_plan_via_step),
	   not all in one, which was a long tick */
	struct
	{
		long samples; /* tries left (0: not looking) */
		int objective, carrying, flank;
		real slack;
		float direct;
		long quarry;
		real best_score;
		real_point3d best_via;
		float best_first;
		long best_side;
	} via_plan;
	real_point3d noroute_point; /* the last place there was no way to, and until when to leave it be */
	long noroute_until;
	long region; /* a very large map: the part of it we are in on foot (NONE: not known) */
	long flank_side; /* the last flank's side, and when (keep to it, not this way then that) */
	long flank_time;
	int on_objective_route; /* going for the objective by a way of its own (dying on it: another way next time) */
	int carrying_objective; /* the flag or the ball in hand */
	/* rubbing it in: over the body of whoever it just killed */
	long last_fired_unit;
	long last_fired_time;
	real_point3d teabag_spot;
	long teabag_ticks;
	long teabag_walk_ticks;
	int teabag_pending;
	/* flag juggling: dropped it ahead a tick ago, picking it up again */
	long juggle_ticks;
	long juggle_cooldown;
	long juggle_flag;
	/* where it's been going lately */
	real_point3d recent_spot[BOT_RECENT_SPOTS];
	long recent_until[BOT_RECENT_SPOTS];
	int goal_squad; /* team slayer: the goal's keeping with the team */
	real_point3d squad_point;
	/* the target's movement, smoothed (for leading them) */
	long aim_velocity_unit;
	real_vector3d aim_velocity;
	/* how the enemy we lost sight of was moving: where they'll be */
	real_point3d last_seen_feet;
	real_vector3d last_seen_velocity; /* world units a tick */
	real_point3d tracked_at;
	long tracked_check_ticks;
	long tracked_age; /* ticks since we last knew */
	/* a health pack to get in a fight */
	long health_object;
	long health_check_ticks;
	long drive_stranded_ticks; /* driving with no way there */
	real_point3d drive_enemy_at; /* where who we're driving after was when we set off */
	/* pacing about when there's nowhere to go */
	real_point3d pace_anchor;
	real pace_angle;
	long pace_ticks;
	long attack_jump_cooldown;
	long weapon_switch_ticks; /* just changed weapons: not again for a bit */
	/* noticing: how far along to seeing each player there is (1: seen),
	   and when each was last in view */
	real notice[BOT_MAXIMUM_PLAYERS];
	long notice_time[BOT_MAXIMUM_PLAYERS];
	long notice_unit[BOT_MAXIMUM_PLAYERS];
	/* looking about on the way somewhere */
	long look_about_ticks; /* >0: glancing aside; <=0: counting down to the next */
	real look_about_yaw; /* off the way we're going */
	/* a person's hands: how fast the look is turning (radians a tick), and
	   where the thumb has the movement (the world's x and y, 0..1 long) */
	real yaw_rate;
	real pitch_rate;
	real thumb_x;
	real thumb_y;
	long crowd_grenade_time; /* when a grenade into a fight is next thought of */
	/* (teams) whoever just killed one of ours close by, and until when we
	   are after them for it; the death we last looked at */
	long avenge_player;
	long avenge_until;
	long avenge_seen_time;
	long signature_ticks; /* its habit's rhythm (a crouch strafe's ducks) */
	long spin_ticks; /* showing off: a turn right round after a kill */
	long antic_ticks; /* ... and ducking at a friend */
	long antic_time;
};


enum
{
	_bot_goal_none = 0,
	_bot_goal_enemy,
	_bot_goal_item,
	_bot_goal_vehicle,
	_bot_goal_roam,
	_bot_goal_hold,
};

static struct bot_state bot_states[BOT_MAXIMUM_PLAYERS];

/* who killed each player last, and when (the kill that went on someone's
   score in the tick they died); and when each last came back to life */
static long bots_kills_counted[BOT_MAXIMUM_PLAYERS];
static long bots_killer[BOT_MAXIMUM_PLAYERS];
static long bots_killed_time[BOT_MAXIMUM_PLAYERS];
static long bots_spawned_time[BOT_MAXIMUM_PLAYERS];

struct bot_skill_parameters
{
	long reaction_ticks;
	real turn_rate; /* radians per second */
	real aim_error; /* radians when a target is first acquired */
	real aim_settle;
};

static struct bot_skill_parameters const bot_skills[5] =
{
	{ 12, 4.0f, 0.08f, 0.90f },
	{ 8, 6.5f, 0.045f, 0.86f },
	{ 5, 10.0f, 0.025f, 0.82f },
	{ 3, 15.0f, 0.012f, 0.76f },
	{ 2, 22.0f, 0.005f, 0.65f },
};

/* the skill level, 0 (bot_skill 1) .. 1 (bot_skill 5) */
static real bot_skill_fraction(void)
{
	return (real)(PIN(bot_skill, 1, 5) - 1) / 4.f;
}

/* how well the skill level (bot_skill 1-5) moves in a fight: 0.3 .. 1 */
static real bot_skill_movement(void)
{
	static real const movement[5] = { 0.3f, 0.6f, 0.85f, 1.f, 1.f };
	return movement[PIN(bot_skill, 1, 5) - 1];
}

static real bot_random(struct bot_state *bot)
{
	bot->random = bot->random * 1664525UL + 1013904223UL;
	return (real)((bot->random >> 8) & 0xFFFF) / 65535.f;
}

struct bot_mover *bot_mover_get(long index)
{
	if (index < 0 || index >= BOT_MAXIMUM_PLAYERS || !bot_states[index].has_personality || bot_states[index].unit_index == NONE)
		return NULL;
	return &bot_states[index].mover;
}

real bot_mover_random(struct bot_mover *mover)
{
	mover->random = mover->random * 1664525UL + 1013904223UL;
	return (real)((mover->random >> 8) & 0xFFFF) / 65535.f;
}

/* ---------- weapons */

enum
{
	_bot_gun_other = 0,
	_bot_gun_assault_rifle,
	_bot_gun_pistol,
	_bot_gun_sniper,
	_bot_gun_shotgun,
	_bot_gun_rocket,
	_bot_gun_plasma_pistol,
	_bot_gun_plasma_rifle,
	_bot_gun_needler,
	_bot_gun_fuel_rod,
	_bot_gun_flamethrower,
};

struct bot_weapon
{
	long kind;
	real heat;
	real projectile_speed; /* world units per tick; 0: instant or unknown */
	real range;
	real preferred_distance;
	int automatic;
	int charging;
	int rounds_loaded;
	int rounds_total;
	int uses_ammunition;
	int precision;
	int none;
	int spent; /* can't fire any more (a plasma weapon's battery run down) */
	int rounds_capacity; /* in it and carried, full up (0: not a weapon that takes rounds) */
	real value;
};

/* how good a weapon (or powerup) is to have: power weapons first; then the
   pistol, the rifles, and last the needler and the plasma pistol (guns to
   have until there is a better one) */
static real bot_item_value(long definition_index)
{
	static struct { char const *name; real value; } const table[] =
	{
		{ "rocket launcher", 10.f }, { "sniper rifle", 9.f }, { "fuel rod", 8.5f }, { "plasma_cannon", 8.5f } /* (the fuel rod's tag) */, { "shotgun", 8.f },
		{ "flamethrower", 7.f }, { "over shield", 9.5f }, { "overshield", 9.5f }, { "active camo", 9.f },
		{ "camouflage", 9.f }, { "pistol\\pistol", 6.f }, { "assault rifle", 5.f }, { "plasma rifle", 4.5f },
		{ "needler", 4.f }, { "plasma pistol", 3.f }, { "frag grenade", 2.f }, { "plasma grenade", 2.f },
		{ "health", 2.5f },
	};
	long i;

	if (definition_index == NONE)
		return 0.f;
	/* (a box of rockets is not a rocket launcher, nor an overshield) */
	if (bot_name_has(definition_index, "ammo"))
		return 1.f;
	for (i = 0; i < (long)(sizeof(table) / sizeof(table[0])); i++)
		if (bot_name_has(definition_index, table[i].name))
			return table[i].value;
	if (tag_get_group_tag(definition_index) == WEAPON_DEFINITION_TAG)
	{
		real rate = 0.f;
		/* flags and balls have no triggers */
		if (weapon_definition_get(definition_index)->weapon.triggers.count <= 0)
			return 0.f;
		real potential = weapon_definition_get_damage_potential(definition_index, &rate) * 0.05f; /* (once: MIN is a macro) */
		return 3.f + MIN(4.f, potential);
	}
	return 1.f;
}

static int bot_is_power_item(long definition_index)
{
	return bot_item_value(definition_index) >= 7.f;
}

static void bot_weapon_describe(long weapon_index, struct bot_weapon *result)
{
	struct weapon_datum *weapon = weapon_index != NONE ? weapon_try_and_get(weapon_index) : NULL;
	struct weapon_definition *definition;
	struct weapon_trigger_definition *trigger;

	memset(result, 0, sizeof(*result));
	result->range = 25.f;
	result->preferred_distance = 10.f;
	result->automatic = TRUE;
	if (!weapon)
	{
		result->none = TRUE;
		result->range = 1.5f;
		result->preferred_distance = 0.8f;
		return;
	}
	definition = weapon_definition_get(weapon->definition_index);
	result->value = bot_item_value(weapon->definition_index);
	result->heat = weapon->weapon.heat;
	result->spent = weapon->weapon.age >= 1.f; /* (weapon_can_be_fired) */
	{
		static struct { char const *name; long kind; } const kinds[] =
		{
			{ "assault rifle", _bot_gun_assault_rifle }, { "plasma pistol", _bot_gun_plasma_pistol }, { "pistol", _bot_gun_pistol },
			{ "sniper", _bot_gun_sniper }, { "shotgun", _bot_gun_shotgun }, { "rocket", _bot_gun_rocket },
			{ "plasma rifle", _bot_gun_plasma_rifle }, { "needler", _bot_gun_needler }, { "fuel rod", _bot_gun_fuel_rod }, { "plasma_cannon", _bot_gun_fuel_rod },
			{ "flamethrower", _bot_gun_flamethrower },
		};
		long k;
		for (k = 0; k < (long)(sizeof(kinds) / sizeof(kinds[0])); k++)
			if (bot_name_has(weapon->definition_index, kinds[k].name))
			{
				result->kind = kinds[k].kind;
				break;
			}
	}
	result->uses_ammunition = definition->weapon.magazines.count > 0 &&
		TAG_BLOCK_GET_ELEMENT(&definition->weapon.magazines, 0, struct weapon_magazine_definition)->rounds_loaded_maximum > 0;
	if (result->uses_ammunition)
	{
		struct weapon_magazine_definition const *magazine = TAG_BLOCK_GET_ELEMENT(&definition->weapon.magazines, 0, struct weapon_magazine_definition);
		result->rounds_capacity = MAX(1, magazine->rounds_total_maximum + magazine->rounds_loaded_maximum);
	}
	result->rounds_loaded = weapon->weapon.magazines[0].rounds_loaded;
	result->rounds_total = weapon->weapon.magazines[0].rounds_total;
	if (definition->weapon.triggers.count <= 0)
	{
		/* a flag or a ball: melee only */
		result->none = TRUE;
		result->range = 1.5f;
		result->preferred_distance = 0.8f;
		return;
	}
	trigger = TAG_BLOCK_GET_ELEMENT(&definition->weapon.triggers, 0, struct weapon_trigger_definition);
	result->automatic = trigger->initial_rate_of_fire >= 5.f || trigger->final_rate_of_fire >= 5.f;
	result->charging = trigger->charging_time > 0.f;
	if (trigger->projectile.index != NONE)
	{
		struct projectile_definition *projectile = projectile_definition_get(trigger->projectile.index);
		real speed = projectile->projectile.initial_velocity;
		/* world units a tick (the tags are converted when they load: a
		   pistol round is 10, a rocket 0.4) */
		result->projectile_speed = speed;
		if (projectile->projectile.maximum_range > 0.f)
			result->range = projectile->projectile.maximum_range;
	}
	if (result->range > 120.f)
		result->range = 120.f;
	result->precision = definition->weapon.zoom_level_count > 0;
	if (trigger->projectiles_per_shot > 3)
	{
		result->preferred_distance = 3.f;
		if (result->range > 12.f)
			result->range = 12.f;
	}
	else if (result->precision && result->range > 40.f && bot_name_has(weapon->definition_index, "sniper"))
		result->preferred_distance = 30.f;
	else if (result->precision)
		result->preferred_distance = 11.f; /* (the pistol: a scope, but a fighting gun, not a reason to keep away) */
	else if (bot_name_has(weapon->definition_index, "rocket"))
		result->preferred_distance = 9.f; /* (its blast is two world units across: no need to stand off far) */
	else
		result->preferred_distance = MIN(12.f, result->range * 0.4f);
}

/* ---------- perception */

static int bot_is_enemy(short my_team, struct player_datum *other)
{
	if (my_team == NONE || other->team_index == NONE)
		return TRUE;
	return game_team_is_enemy(my_team, (short)other->team_index);
}

/* who the bots are shooting at, by the target's object index, worked out
   once a tick (bots_world_tick) rather than by every bot for every enemy */
static long bot_target_owner[BOT_MAXIMUM_OBJECTS]; /* the target's datum index, NONE */
static unsigned long bot_target_teams[BOT_MAXIMUM_OBJECTS]; /* teams (bit per team) with a bot on it */
static long bot_target_single[BOT_MAXIMUM_OBJECTS]; /* the unit of one bot on it (to tell it's not us) */
static long bot_target_count[BOT_MAXIMUM_OBJECTS];

static void bots_targets_tally(void)
{
	long i;
	for (i = 0; i < BOT_MAXIMUM_OBJECTS; i++)
		bot_target_owner[i] = NONE;
	for (i = 0; i < BOT_MAXIMUM_PLAYERS; i++)
	{
		struct bot_state const *bot = &bot_states[i];
		long slot;
		if (!bot->has_personality || bot->target_unit == NONE || bot->unit_index == NONE)
			continue;
		slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(bot->target_unit);
		if (slot < 0 || slot >= BOT_MAXIMUM_OBJECTS)
			continue;
		if (bot_target_owner[slot] != bot->target_unit)
		{
			bot_target_owner[slot] = bot->target_unit;
			bot_target_teams[slot] = 0;
			bot_target_count[slot] = 0;
			bot_target_single[slot] = bot->unit_index;
		}
		bot_target_teams[slot] |= 1UL << (bot->team & 31);
		bot_target_count[slot]++;
	}
}

static int bot_targeted_by(long unit_index, long team, long me)
{
	long slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(unit_index);
	if (slot < 0 || slot >= BOT_MAXIMUM_OBJECTS || bot_target_owner[slot] != unit_index ||
		!(bot_target_teams[slot] & (1UL << (team & 31))))
	{
		return FALSE;
	}
	return bot_target_count[slot] > 1 || bot_target_single[slot] != me;
}

/* someone busy with somebody else: being hit just now, or shooting, and
   not at us (their aim is off somewhere else). In a free for all that is a
   kill there to be taken: they are hurt, or soon will be, and not looking */
static int bot_unit_busy(long unit_index, real_point3d const *eye)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	long slot;
	int firing;
	real_vector3d toward;
	real length;
	if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return FALSE;
	slot = unit->unit.current_weapon_index;
	firing = slot >= 0 && slot < MAXIMUM_WEAPONS_PER_UNIT && unit->unit.weapon_last_used_at_game_time[slot] > game_time_get() - BOT_SECONDS(1.f);
	toward.i = eye->x - unit->object.bounding_sphere_center.x;
	toward.j = eye->y - unit->object.bounding_sphere_center.y;
	toward.k = eye->z - unit->object.bounding_sphere_center.z;
	length = (real)sqrt(toward.i * toward.i + toward.j * toward.j + toward.k * toward.k);
	/* (looking our way: it is us they are busy with) */
	if (length > 0.1f && (unit->unit.aiming_vector.i * toward.i + unit->unit.aiming_vector.j * toward.j +
		unit->unit.aiming_vector.k * toward.k) / length > 0.9f)
	{
		return FALSE;
	}
	return firing || unit->object.recent_shield_damage > 0.02f || unit->object.recent_body_damage > 0.02f;
}

/* someone with their back to us (their aim well off where we are) */
static int bot_unit_looking_away(long unit_index, real_point3d const *eye)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	real_vector3d toward;
	real length;
	if (!unit)
		return FALSE;
	toward.i = eye->x - unit->object.bounding_sphere_center.x;
	toward.j = eye->y - unit->object.bounding_sphere_center.y;
	toward.k = eye->z - unit->object.bounding_sphere_center.z;
	length = (real)sqrt(toward.i * toward.i + toward.j * toward.j + toward.k * toward.k);
	return length > 0.1f && (unit->unit.aiming_vector.i * toward.i + unit->unit.aiming_vector.j * toward.j +
		unit->unit.aiming_vector.k * toward.k) / length < 0.2f;
}

static long bot_player_count; /* players in the game, this tick */
static real bot_finding_teamwork = 0.5f; /* the looking bot's, for bot_find_target */

/* active camouflage: a camouflaged player is seen only now and then, the
   way the game's own AI sees one: when they shoot (the camouflage
   shimmers off), right up close, moving close by (the shimmer), and in
   glimpses when we're already on them and not far off; the better hidden
   (all but gone), the less. Glimpses come and go a few times a second */
static int bot_sees_camouflaged(long unit_index, real distance, int already_on)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	real camouflage;
	real_vector3d velocity;
	real speed;
	unsigned long flicker;
	real glimpse;
	long slot;
	if (!unit)
		return FALSE;
	camouflage = unit->unit.active_camouflage;
	if (camouflage < 0.5f)
		return TRUE;
	if (distance < 2.5f)
		return TRUE;
	slot = unit->unit.current_weapon_index;
	if (slot >= 0 && slot < MAXIMUM_WEAPONS_PER_UNIT && unit->unit.weapon_last_used_at_game_time[slot] > game_time_get() - BOT_SECONDS(0.8f) &&
		distance < 40.f)
	{
		return TRUE;
	}
	object_get_velocities(object_get_ultimate_parent(unit_index), &velocity, NULL);
	speed = (real)sqrt(velocity.i * velocity.i + velocity.j * velocity.j) * BOT_TICKS_PER_SECOND;
	/* a glimpse: likelier close, moving, and when we're on them already */
	glimpse = 0.f;
	if (distance < 10.f && speed > 1.f)
		glimpse += 0.5f * (1.f - distance / 10.f) + 0.1f * MIN(speed, 3.f);
	if (already_on && distance < 18.f)
		glimpse += 0.35f * (1.f - distance / 18.f) + (speed > 1.f ? 0.15f : 0.f);
	glimpse *= 1.5f - camouflage;
	flicker = bot_hash(((unsigned long)unit_index * 2654435761UL + (unsigned long)(game_time_get() / 6)) ^ bots_match());
	return (real)(flicker & 0xFFFF) / 65535.f < glimpse;
}

/* a teammate between us and who we're shooting at (a team game): hold
   fire rather than put rounds in their back */
static int bot_friend_in_line(struct player_datum *me, real_point3d const *eye, real_point3d const *at, long my_vehicle)
{
	struct data_iterator iterator;
	struct player_datum *player;
	real dx = at->x - eye->x, dy = at->y - eye->y, dz = at->z - eye->z;
	real length = (real)sqrt(dx * dx + dy * dy + dz * dz);
	if (!game_engine_has_teams() || length < 0.5f)
		return FALSE;
	dx /= length, dy /= length, dz /= length;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct object_datum *mate;
		real ox, oy, oz, along, across2;
		if (player == me || player->unit_index == NONE || player->team_index != me->team_index || !bot_unit_alive(player->unit_index))
			continue;
		if (my_vehicle != NONE && object_get_ultimate_parent(player->unit_index) == my_vehicle)
			continue; /* (riding with us) */
		mate = object_get(player->unit_index);
		ox = mate->object.bounding_sphere_center.x - eye->x;
		oy = mate->object.bounding_sphere_center.y - eye->y;
		oz = mate->object.bounding_sphere_center.z - eye->z;
		along = ox * dx + oy * dy + oz * dz;
		if (along < 0.3f || along > length - 0.3f)
			continue;
		across2 = ox * ox + oy * oy + oz * oz - along * along;
		if (across2 < 0.45f * 0.45f)
			return TRUE;
	}
	return FALSE;
}

/* noticing someone in plain sight takes a moment, as it does a player:
   none to speak of for someone close and straight ahead, longer the
   further off they are and the further out to the side of the view, and
   not at all behind (that's what the motion tracker, gunfire and getting
   shot are for: they turn us round). Someone moving or shooting catches
   the eye sooner than someone standing still. Once seen they stay seen
   while in sight, and for a few seconds out of it. Better reactions (the
   skill level) notice sooner.
   Called for an enemy there's a clear line to; TRUE when they're seen */
#define BOT_VIEW_HALF_ANGLE 0.96f /* radians: 55 degrees either side */
#define BOT_VIEW_CENTER_ANGLE 0.26f /* 15 degrees: what we're looking right at */
/* the ticks from one look round to the next (bot_think_inner) */
static long bot_look_period = 1;

static int bot_notices(struct bot_state *bot, long player_absolute_index, long unit_index, real_point3d const *eye, real distance, int hit_lately)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	long now = game_time_get();
	real_point3d at;
	real_vector3d velocity;
	real dx, dy, dz, length, cosine, angle, off, seconds;
	int moving, firing;
	long slot;
	if (!BOT_ON(BOT_F_NOTICE))
		return TRUE;
	if (!unit || player_absolute_index < 0 || player_absolute_index >= BOT_MAXIMUM_PLAYERS)
		return TRUE;
	/* (out of view a good while, or a new life of theirs: forgotten) */
	if (now - bot->notice_time[player_absolute_index] > BOT_SECONDS(4.f) || bot->notice_unit[player_absolute_index] != unit_index)
		bot->notice[player_absolute_index] = 0.f;
	bot->notice_unit[player_absolute_index] = unit_index;
	if (bot->notice[player_absolute_index] >= 1.f || unit_index == bot->target_unit)
	{
		bot->notice[player_absolute_index] = 1.f;
		bot->notice_time[player_absolute_index] = now;
		return TRUE;
	}
	at = unit->object.bounding_sphere_center;
	dx = at.x - eye->x, dy = at.y - eye->y, dz = at.z - eye->z;
	length = (real)sqrt(dx * dx + dy * dy + dz * dz);
	cosine = length > 0.01f ? (dx * (real)(cos(bot->pitch) * cos(bot->yaw)) + dy * (real)(cos(bot->pitch) * sin(bot->yaw)) +
		dz * (real)sin(bot->pitch)) / length : 1.f;
	angle = (real)acos(PIN(cosine, -1.f, 1.f));
	slot = unit->unit.current_weapon_index;
	firing = slot >= 0 && slot < MAXIMUM_WEAPONS_PER_UNIT && unit->unit.weapon_last_used_at_game_time[slot] > now - BOT_SECONDS(0.7f);
	if (angle > BOT_VIEW_HALF_ANGLE)
	{
		/* not in view. Shooting us from there, though (the hits say from
		   where): as good as at the edge of it */
		real_vector3d const *aim = &unit->unit.aiming_vector;
		if (!(hit_lately && firing && length > 0.01f && -(aim->i * dx + aim->j * dy + aim->k * dz) / length > 0.96f))
			return FALSE;
		angle = BOT_VIEW_HALF_ANGLE;
	}
	bot->notice_time[player_absolute_index] = now;
	object_get_velocities(object_get_ultimate_parent(unit_index), &velocity, NULL);
	moving = velocity.i * velocity.i + velocity.j * velocity.j + velocity.k * velocity.k > 0.02f * 0.02f;
	off = PIN((angle - BOT_VIEW_CENTER_ANGLE) / (BOT_VIEW_HALF_ANGLE - BOT_VIEW_CENTER_ANGLE), 0.f, 1.f);
	seconds = 0.1f + 0.5f * off * off + 0.006f * distance;
	if (firing)
		seconds *= 0.6f;
	else if (!moving)
		seconds *= 1.4f;
	/* (reactions 0.7 at the top skill to 1.4 at the bottom, and the lower
	   levels slower again: up to 2.8 times as long) */
	seconds *= bot->personality.reaction * (2.f - bot_skill_fraction());
	/* (a look every so many ticks counts for as many: noticed as soon) */
	bot->notice[player_absolute_index] += (real)bot_look_period / MAX(1.f, seconds * BOT_TICKS_PER_SECOND);
	if (bot->notice[player_absolute_index] < 1.f)
		return FALSE;
	bot->notice[player_absolute_index] = 1.f;
	return TRUE;
}

static long bot_find_target(struct bot_state *bot, int hit_lately, long my_unit, short my_team, real_point3d const *eye, real range, long current, int prefer_vehicles, real *distance_out)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long best = NONE;
	real best_score = 1.0e9f;
	long my_root = object_get_ultimate_parent(my_unit);

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct object_datum *unit;
		real distance;
		real score;

		if (player->unit_index == NONE || player->unit_index == my_unit || !bot_unit_alive(player->unit_index))
			continue;
		if (!bot_is_enemy(my_team, player))
			continue;
		unit = object_get(player->unit_index);
		distance = distance3d(eye, &unit->object.bounding_sphere_center);
		if (distance > range)
			continue;
		/* weakened targets first, then the closest */
		score = distance * (0.6f + 0.4f * (unit->object.shield_vitality + unit->object.body_vitality) * 0.5f);
		/* stay on the one we're shooting unless another is much better */
		if (player->unit_index == current)
			score *= 0.5f;
		/* help a teammate: gang up on whoever they're fighting */
		if (game_engine_has_teams() && bot_targeted_by(player->unit_index, my_team, my_unit))
			score *= 1.f - 0.45f * bot_finding_teamwork;
		/* everyone for themselves: whoever is busy with someone else (hurt,
		   or about to be, and looking the other way) before anyone fresh:
		   the kill somebody else has paid for. As its sportsmanship goes:
		   the least sporting most of all, the most sporting not at all. */
		if (!game_engine_has_teams() && player->unit_index != current && bot_unit_busy(player->unit_index, eye))
			score *= 1.f - 0.55f * (1.f - bot->personality.sporting);
		{
			long index = iterator.datum_index & 0xFFFF;
			if (index < BOT_MAXIMUM_PLAYERS)
			{
				/* whoever keeps killing it: first, the more it rankles */
				if (bot->memory.killed_by[index] > 0)
					score *= 1.f - 0.45f * bot->personality.grudge * MIN(bot->memory.killed_by[index], 3) / 3.f;
				/* whoever just killed one of ours */
				if (index == bot->avenge_player && game_time_get() < bot->avenge_until)
					score *= 0.55f;
				/* someone only just back in the game, who has not fired
				   yet: the sporting give them a moment (not when they are
				   the one shooting at us) */
				if (bot->personality.sporting > 0.5f && player->unit_index != current && !hit_lately &&
					game_time_get() - bots_spawned_time[index] < BOT_SECONDS(2.f) && game_time_get() >= bots_spawned_time[index])
				{
					score *= 1.f + 3.f * bot->personality.sporting;
				}
			}
		}
		/* the threat first: whoever has us in their sights, and whoever
		   carries a power weapon */
		if (BOT_ON(BOT_F_THREAT))
		{
			struct unit_datum *enemy = unit_try_and_get(player->unit_index);
			real_vector3d toward;
			real length;
			toward.i = eye->x - unit->object.bounding_sphere_center.x;
			toward.j = eye->y - unit->object.bounding_sphere_center.y;
			toward.k = eye->z - unit->object.bounding_sphere_center.z;
			length = (real)sqrt(toward.i * toward.i + toward.j * toward.j + toward.k * toward.k);
			if (enemy && length > 0.1f && (enemy->unit.aiming_vector.i * toward.i + enemy->unit.aiming_vector.j * toward.j +
				enemy->unit.aiming_vector.k * toward.k) / length > 0.97f)
				score *= 0.7f;
			if (enemy && enemy->unit.current_weapon_index >= 0 && enemy->unit.current_weapon_index < MAXIMUM_WEAPONS_PER_UNIT &&
				enemy->unit.weapon_object_indices[enemy->unit.current_weapon_index] != NONE &&
				object_try_and_get(enemy->unit.weapon_object_indices[enemy->unit.current_weapon_index]) &&
				bot_is_power_item(object_get(enemy->unit.weapon_object_indices[enemy->unit.current_weapon_index])->definition_index))
				score *= 0.8f;
		}
		/* rockets are for vehicles */
		if (prefer_vehicles && unit->object.parent_object_index != NONE)
			score *= 0.4f;
		if (score >= best_score)
			continue;
		if (!bot_line_clear(bot_sight_flags, eye, &unit->object.bounding_sphere_center, my_root))
			continue;
		if (!bot_sees_camouflaged(player->unit_index, distance, player->unit_index == current))
			continue;
		if (!bot_notices(bot, iterator.datum_index & 0xFFFF, player->unit_index, eye, distance, hit_lately))
			continue;
		best = player->unit_index;
		best_score = score;
		*distance_out = distance;
	}
	return best;
}

/* ---------- goals */

static void bot_remember_spot(struct bot_state *bot, real_point3d const *point);

static void bot_goal_snap(struct bot_state *bot)
{
	long kind = bot->goal_kind;
	real_point3d const *point = &bot->goal;
	/* a place (not a thing to pick up): onto the floor near it, when it's
	   off it (in a wall, up in the air: a guess at where someone is) */
	if ((kind == _bot_goal_enemy || kind == _bot_goal_roam || kind == _bot_goal_hold) && aas_ready() &&
		aas_nearest_area(AAS_FOOT, point, 3.f, NULL) == NONE)
	{
		real_point3d spot;
		real_point3d raised = *point;
		raised.z += 2.5f; /* (under the ground: a guess from below the terrain) */
		if (aas_nearest_area(AAS_FOOT, point, 12.f, &spot) != NONE || aas_nearest_area(AAS_FOOT, &raised, 12.f, &spot) != NONE)
			bot->goal = spot; /* (point is bot->goal: spot's a copy) */
	}
}

static void bot_set_goal(struct bot_state *bot, long kind, real_point3d const *point, long object_index, long ticks)
{
	if (bot->goal_kind != kind || bot->goal_object != object_index || distance3d(&bot->goal, point) > 2.f)
	{
		bot->goal_best_distance = 1.0e9f;
		bot->goal_progress_ticks = 0;
	}
	if (bot->goal_kind != _bot_goal_none && bot->goal_kind != _bot_goal_enemy && !bot->goal_squad &&
		(bot->goal_kind != kind || distance3d(&bot->goal, point) > 6.f))
	{
		bot_remember_spot(bot, &bot->goal);
	}
	if (bot->goal_kind != kind || distance3d(&bot->goal, point) > 6.f)
	{
		/* a new trip: a lane of its own, and maybe a way round of its own
		   (and dying on it says nothing about the last one's way) */
		bot->on_objective_route = FALSE;
		bot->mover.move.lane = 0.1f + 0.8f * bot_random(bot);
		bot->mover.move.lane_set = TRUE;
		/* (flanking someone who's moved a little: still the same flank) */
		if (!(kind == _bot_goal_enemy && bot->goal_kind == _bot_goal_enemy && bot->via_valid && distance3d(&bot->goal, point) < 15.f))
		{
			bot->via_valid = FALSE;
			bot->via_pending = TRUE;
			bot->via_plan.samples = 0;
		}
	}
	bot->goal_kind = kind;
	bot->goal_squad = FALSE;
	bot->goal = *point;
	bot_goal_snap(bot);
	bot->goal_object = object_index;
	bot->goal_ticks = ticks;
}

static real bot_weapon_worth(long weapon_index);

static real bot_best_held_value(struct unit_datum *unit)
{
	real best = 0.f;
	long i;

	for (i = 0; i < MAXIMUM_WEAPONS_PER_UNIT; i++)
	{
		long weapon = unit->unit.weapon_object_indices[i];
		if (weapon != NONE && object_try_and_get(weapon))
		{
			real value = bot_weapon_worth(weapon); /* (an empty rocket launcher is no rocket launcher) */
			if (value > best)
				best = value;
		}
	}
	return best;
}

static int bot_holds_weapon_type(struct unit_datum *unit, long definition_index)
{
	long i;
	for (i = 0; i < MAXIMUM_WEAPONS_PER_UNIT; i++)
	{
		long weapon = unit->unit.weapon_object_indices[i];
		if (weapon != NONE && object_try_and_get(weapon) && object_get(weapon)->definition_index == definition_index)
			return TRUE;
	}
	return FALSE;
}

static int bot_goal_is_game_object(long object_index);

/* a weapon's worth with what's in it: an empty one is only good for its
   ammunition later; a nearly empty one less than a full one */
static real bot_weapon_worth(long weapon_index)
{
	struct bot_weapon weapon;
	real value;
	bot_weapon_describe(weapon_index, &weapon);
	if (weapon.none)
		return 0.f;
	value = weapon.value;
	if (weapon.uses_ammunition)
	{
		long rounds = weapon.rounds_loaded + weapon.rounds_total;
		real full = weapon.rounds_capacity > 0 ? (real)rounds / weapon.rounds_capacity : 1.f;
		if (rounds <= 0)
			return 0.3f;
		/* (by how full it is, for its kind: eight rockets is a full launcher) */
		if (full < 0.5f)
			value *= 0.6f + 0.8f * full;
	}
	else if (weapon.spent)
		return 0.3f; /* (a plasma weapon run down: cooled off or not) */
	return value;
}

/* the weaker of the weapons we carry (and which slot it's in) */
static real bot_worst_held(struct unit_datum *unit, long *slot)
{
	real worst = 1.0e9f;
	long i;
	*slot = NONE;
	for (i = 0; i < MAXIMUM_WEAPONS_PER_UNIT; i++)
	{
		long weapon = unit->unit.weapon_object_indices[i];
		real worth;
		if (weapon == NONE || !object_try_and_get(weapon))
			continue;
		worth = bot_goal_is_game_object(weapon) ? 1.0e8f : bot_weapon_worth(weapon);
		if (worth < worst)
		{
			worst = worth;
			*slot = i;
		}
	}
	return worst < 1.0e8f ? worst : 0.f;
}

static int bot_holds_named(struct unit_datum *unit, char const *name)
{
	long i;
	for (i = 0; i < MAXIMUM_WEAPONS_PER_UNIT; i++)
		if (unit->unit.weapon_object_indices[i] != NONE && object_try_and_get(unit->unit.weapon_object_indices[i]) &&
			bot_name_has(object_get(unit->unit.weapon_object_indices[i])->definition_index, name))
			return TRUE;
	return FALSE;
}

/* how many weapons we carry */
static long bot_weapons_held(struct unit_datum *unit)
{
	long count = 0;
	long i;
	for (i = 0; i < MAXIMUM_WEAPONS_PER_UNIT; i++)
		if (unit->unit.weapon_object_indices[i] != NONE && object_try_and_get(unit->unit.weapon_object_indices[i]))
			count++;
	return count;
}

/* another bot already heading for it? */
static int bot_goal_taken(long object_index, long me)
{
	long i;
	for (i = 0; i < BOT_MAXIMUM_PLAYERS; i++)
		if (i != me && bot_states[i].unit_index != NONE && bot_unit_alive(bot_states[i].unit_index) && bot_states[i].goal_object == object_index &&
			(bot_states[i].goal_kind == _bot_goal_item || bot_states[i].goal_kind == _bot_goal_vehicle))
			return TRUE;
	return FALSE;
}

/* how many other bots are heading for it, and whether one of them is
   nearer it than we are */
static long bot_goal_takers(long object_index, long me, real_point3d const *from, int *nearer)
{
	struct object_datum *object = object_try_and_get(object_index);
	real mine = object ? distance3d(from, &object->object.position) : 0.f;
	long count = 0;
	long i;
	*nearer = FALSE;
	for (i = 0; i < BOT_MAXIMUM_PLAYERS; i++)
	{
		struct object_datum *unit;
		if (i == me || bot_states[i].unit_index == NONE || bot_states[i].goal_object != object_index || bot_states[i].goal_kind != _bot_goal_item)
			continue;
		count++;
		if (object && (unit = object_try_and_get(bot_states[i].unit_index)) != NULL && distance3d(&unit->object.position, &object->object.position) < mine)
			*nearer = TRUE;
	}
	return count;
}

/* somewhere we've been going lately (and gave up on, or got to): not
   straight back there */
static void bot_remember_spot(struct bot_state *bot, real_point3d const *point)
{
	long i;
	long oldest = 0;
	for (i = 0; i < BOT_RECENT_SPOTS; i++)
	{
		if (bot->recent_until[i] < bot->recent_until[oldest])
			oldest = i;
		if (bot->recent_until[i] > game_time_get() && distance3d(&bot->recent_spot[i], point) < 4.f)
		{
			oldest = i;
			break;
		}
	}
	bot->recent_spot[oldest] = *point;
	bot->recent_until[oldest] = game_time_get() + 25 * BOT_TICKS_PER_SECOND;
}

static int bot_recently_at(struct bot_state const *bot, real_point3d const *point)
{
	long i;
	for (i = 0; i < BOT_RECENT_SPOTS; i++)
		if (bot->recent_until[i] > game_time_get() && distance3d(&bot->recent_spot[i], point) < 6.f)
			return TRUE;
	return FALSE;
}

/* A weapon or a piece of equipment lying in the world, to be had. Not one
   somebody has: the weapon in a unit's hand is the unit's child, but the
   one it has put away is not. That one is taken off its unit, hidden and
   taken off the map (units.c, unit_ready_desired_weapon), and stays where
   it was put away, a weapon with no parent that nobody can see or pick
   up: a bot that takes it for one lying there walks to an empty spot,
   stands on it, and gives up. */
static int bot_item_lying(struct object_datum const *object)
{
	return object->object.parent_object_index == NONE && !TEST_FLAG(object->object.flags, _object_invisible_bit) &&
		TEST_FLAG(object->object.flags, _object_connected_to_map_bit);
}

/* a vehicle's kind, as its physics are (vehicles.c: the tank, the jeep, the
   boat, the plane, the scout (a Ghost), the fighter (a Banshee), a turret) */
enum
{
	_bot_vehicle_tank = 0,
	_bot_vehicle_jeep,
	_bot_vehicle_boat,
	_bot_vehicle_plane,
	_bot_vehicle_scout,
	_bot_vehicle_fighter,
	_bot_vehicle_turret,
	NUMBER_OF_BOT_VEHICLE_KINDS
};

static long bot_vehicle_kind(long definition_index)
{
	/* (vehicle_definition, vehicles.c: its flags, then its type, after the unit's) */
	char const *definition = (char const *)object_definition_get(definition_index);
	short type = *(short const *)(definition + sizeof(struct unit_definition) + 4);
	return type >= 0 && type < NUMBER_OF_BOT_VEHICLE_KINDS ? type : _bot_vehicle_jeep;
}

/* how good a vehicle is to take into a fight */
static real bot_vehicle_worth(long definition_index)
{
	if (bot_name_has(definition_index, "scorpion"))
		return 1.4f;
	if (bot_name_has(definition_index, "banshee"))
		return 1.2f;
	if (bot_name_has(definition_index, "warthog") || bot_name_has(definition_index, "rwarthog"))
		return 1.1f;
	if (bot_name_has(definition_index, "ghost"))
		return 1.f;
	return 0.6f;
}

static long bot_vehicle_seats(long vehicle_index)
{
	struct object_datum *vehicle = object_get(vehicle_index);
	struct unit_definition *definition = (struct unit_definition *)object_definition_get(vehicle->definition_index);
	return definition->unit.seats.count;
}

/* the driver's own weapon (ghost, tank, banshee) or none (warthog: the gunner's) */
static int bot_driver_shoots(long vehicle_index)
{
	long definition_index = object_get(vehicle_index)->definition_index;
	return bot_name_has(definition_index, "ghost") || bot_name_has(definition_index, "scorpion") ||
		bot_name_has(definition_index, "banshee") || bot_name_has(definition_index, "turret");
}

/* things a bot couldn't reach or get: left alone for a while */
static int bot_ignoring(struct bot_state const *bot, long object_index)
{
	long i;
	for (i = 0; i < 6; i++)
		if (bot->ignore_object[i] == object_index && bot->ignore_until[i] > game_time_get())
			return TRUE;
	return FALSE;
}

static int bot_goal_is_game_object(long object_index);

static void bot_ignore(struct bot_state *bot, long object_index, long ticks)
{
	long i;
	long oldest = 0;
	/* never give up on the flag or the ball for long: it's the game. (It
	   may only be out of reach while it's in the air, or for the moment) */
	if (bot_goal_is_game_object(object_index) && ticks > 3 * BOT_TICKS_PER_SECOND)
		ticks = 3 * BOT_TICKS_PER_SECOND;
	if (bot_logging() && object_index != NONE && object_try_and_get(object_index))
		bot_log("bot%ld ignores %s for %lds", (long)(bot - bot_states), tag_get_name(object_get(object_index)->definition_index), ticks / BOT_TICKS_PER_SECOND);
	for (i = 1; i < 6; i++)
		if (bot->ignore_until[i] < bot->ignore_until[oldest])
			oldest = i;
	bot->ignore_object[oldest] = object_index;
	bot->ignore_until[oldest] = game_time_get() + ticks;
}

boolean unit_get_seat_entrance_point(long unit_index, long target_unit_index, short seat_index,
	real_point3d *entry_position, real_point3d *exit_position, real_point3d *seat_transform);

/* where to stand to get into the seat we want: the wheel if nobody's
   driving, else the gun, else any free seat; FALSE when it's full */
static int bot_vehicle_entry_rank(long unit_index, long vehicle_index, real_point3d *entry, int *rank_out)
{
	struct object_datum *vehicle = object_get(vehicle_index);
	struct unit_definition *definition = (struct unit_definition *)object_definition_get(vehicle->definition_index);
	int has_driver = unit_scripting_unit_driver(vehicle_index) != NONE;
	short best = NONE;
	int best_rank = 99;
	short seat;

	for (seat = 0; seat < definition->unit.seats.count; seat++)
	{
		long child;
		int taken = FALSE;
		int rank;
		for (child = vehicle->object.first_child_object_index; child != NONE; child = object_get(child)->object.next_object_index)
		{
			struct unit_datum *rider = (struct unit_datum *)object_try_and_get_and_verify_type(child, _object_mask_unit);
			if (rider && rider->unit.parent_seat_index == seat)
				taken = TRUE;
		}
		if (taken)
			continue;
		/* nobody at the wheel: the wheel (a gun on a parked vehicle is a
		   sitting duck); someone driving: the gun, then any seat */
		if (unit_seat_is_driver(vehicle_index, seat))
			rank = has_driver ? 99 : 0;
		else if (unit_seat_is_gunner(vehicle_index, seat))
			rank = has_driver ? 0 : 99;
		else
			rank = has_driver ? 2 : 99;
		if (rank < best_rank)
		{
			real_point3d exit_point;
			real_point3d seat_point;
			if (unit_get_seat_entrance_point(unit_index, vehicle_index, seat, entry, &exit_point, &seat_point))
			{
				best_rank = rank;
				best = seat;
			}
		}
	}
	if (best == NONE)
		return FALSE;
	if (rank_out)
		*rank_out = best_rank;
	{
		real_point3d exit_point;
		real_point3d seat_point;
		return unit_get_seat_entrance_point(unit_index, vehicle_index, best, entry, &exit_point, &seat_point);
	}
}

static int bot_vehicle_entry(long unit_index, long vehicle_index, real_point3d *entry)
{
	return bot_vehicle_entry_rank(unit_index, vehicle_index, entry, NULL);
}

/* teammate bots on their way to get in this vehicle */
static long bot_riders_coming(long vehicle_index, long me)
{
	long i;
	long count = 0;
	for (i = 0; i < BOT_MAXIMUM_PLAYERS; i++)
		if (i != me && bot_states[i].unit_index != NONE && bot_states[i].goal_kind == _bot_goal_vehicle &&
			bot_states[i].goal_object == vehicle_index && bot_unit_alive(bot_states[i].unit_index) &&
			(!game_engine_has_teams() || bot_states[i].team == bot_states[me].team) && /* (ours: an enemy on its way is no one to wait for) */
			distance3d(&object_get(bot_states[i].unit_index)->object.position, &object_get(vehicle_index)->object.position) < 25.f)
		{
			count++;
		}
	return count;
}

boolean ctf_bot_flag(long team_index, long *weapon_index, real_point3d *base); /* game_engine_ctf.c */
long ctf_bot_score(long team_index);

/* someone of the other side in it (a seat left for us or not, it's theirs) */
static int bot_vehicle_has_enemy(long vehicle_index, struct player_datum *me)
{
	struct data_iterator iterator;
	struct player_datum *player;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		if (player->unit_index != NONE && bot_unit_alive(player->unit_index) && player != me &&
			object_get_ultimate_parent(player->unit_index) == vehicle_index && player->unit_index != vehicle_index &&
			bot_is_enemy((short)me->team_index, player))
		{
			return TRUE;
		}
	return FALSE;
}

static int bot_holds_object(struct unit_datum *unit, long object_index)
{
	long i;
	for (i = 0; i < MAXIMUM_WEAPONS_PER_UNIT; i++)
		if (object_index != NONE && unit->unit.weapon_object_indices[i] == object_index)
			return TRUE;
	return FALSE;
}

/* is this unit one of ours (a bot) rather than a person? */
static int bot_unit_is_bot(long unit_index)
{
	long i;
	for (i = 0; i < BOT_MAXIMUM_PLAYERS; i++)
		if (bot_states[i].has_personality && bot_states[i].unit_index == unit_index)
			return TRUE;
	return FALSE;
}

/* capture the flag roles: the most patient, least aggressive quarter of a
   team (at least one when there are three or more) defends; everyone else
   attacks */
static int bot_ctf_defender(struct bot_state const *bot, struct player_datum const *player)
{
	struct data_iterator iterator;
	struct player_datum *other;
	long teammates = 0;
	long keener = 0;
	real mine = bot->personality.patience - bot->personality.aggression + 0.5f * bot->personality.teamwork;

	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		if (other->team_index != player->team_index || index >= BOT_MAXIMUM_PLAYERS)
			continue;
		teammates++;
		if (other != player && bot_states[index].has_personality &&
			bot_states[index].personality.patience - bot_states[index].personality.aggression + 0.5f * bot_states[index].personality.teamwork > mine)
		{
			keener++;
		}
	}
	return teammates >= 3 && keener < MAX(1, teammates / 4);
}

/* a candidate goal: the best one wins */
struct bot_candidate
{
	real score;
	long kind;
	real_point3d point;
	long object;
	long ticks;
	int objective; /* something to get to even under fire */
	int squad; /* team slayer: with the team */
	int explore; /* somewhere to explore */
};

/* the choice being weighed: whose, and how loosely. Every candidate's score
   is nudged by a hunch of the bot's about it (up to a quarter either way,
   for the most whimsical), the same hunch for a few seconds so that it
   doesn't dither, a new one after. Close calls go either way, match to
   match and life to life; a far better choice still wins */
static unsigned long bot_hunch_seed;
static real bot_hunch_width;

/* somewhere this bot just found no way to (someone up where it can't go):
   not straight back there */
static real_point3d const *bot_consider_noroute;

static int bot_noroute_near(struct bot_state const *bot, real_point3d const *point)
{
	return bot->noroute_until > game_time_get() && distance3d(point, &bot->noroute_point) < 4.f;
}

static void bot_hunches_begin(struct bot_state *bot, long me)
{
	bot_hunch_seed = bot_hash((unsigned long)bot->unit_index ^ ((unsigned long)me * 0x9e3779b1UL) ^ bots_match() ^
		(unsigned long)((game_time_get() + me * 37) / (6 * BOT_TICKS_PER_SECOND)) * 0x85ebca6bUL); /* (the bots' hunches don't all turn over on the same tick) */
	bot_hunch_width = 0.06f + 0.2f * bot->personality.whim;
	bot_consider_noroute = bot->noroute_until > game_time_get() ? &bot->noroute_point : NULL;
}

static real bot_hunch(real score, long kind, real_point3d const *point, long object)
{
	unsigned long key;
	real u;
	if (bot_hunch_width <= 0.f || score <= 0.f)
		return score;
	key = object != NONE ? (unsigned long)object :
		((unsigned long)(long)(point->x * 0.25f) * 73856093UL) ^ ((unsigned long)(long)(point->y * 0.25f) * 19349663UL);
	u = (real)(bot_hash(bot_hunch_seed ^ key ^ ((unsigned long)kind * 83492791UL)) & 0xFFFF) / 65535.f;
	return score * (1.f + bot_hunch_width * (2.f * u - 1.f));
}

/* the part of a very large map the bot choosing a goal is in, on foot
   (NONE: not known, or a map with navigation all over), and where it is */
static long bot_consider_region = NONE;
static real_point3d bot_consider_from;

/* Whether somewhere is in that part. Somewhere that isn't cannot be got to
   on foot: another base of a very large map, a mile off, with nothing
   worked out between (bot_nav.c, navm_choose_parts). Not a goal: a route
   to it is looked for through the whole of this part, found wanting, and
   the next second it is chosen again. */
static int bot_within_reach(real_point3d const *point)
{
	long area;
	if (bot_consider_region == NONE)
		return TRUE;
	area = aas_nearest_area(AAS_FOOT, point, 3.f, NULL);
	return area != NONE ? aas_area_region(AAS_FOOT, area) == bot_consider_region : distance3d(point, &bot_consider_from) <= 30.f;
}

/* TRUE when this one is now the best (its score is the hunch's, not the
   one passed in: compare with this, not with the score) */
static int bot_consider(struct bot_candidate *best, real score, long kind, real_point3d const *point, long object, long ticks, int objective)
{
	if (!bot_within_reach(point))
		return FALSE;
	if (kind == _bot_goal_item && object != NONE && aas_ready())
	{
		/* something lying where no one can get to it (a flag knocked off
		   the edge, falling to the bottom of the map): not worth a thing,
		   or bots chase it till it's back */
		struct object_datum *thing = object_try_and_get(object);
		if (thing && thing->object.parent_object_index == NONE &&
			(thing->object.translational_velocity.k < -0.1f || aas_nearest_area(AAS_FOOT, &thing->object.position, 3.f, NULL) == NONE))
		{
			return FALSE;
		}
		/* (or one put away on someone's back) */
		if (thing && thing->object.parent_object_index == NONE && !bot_item_lying(thing))
			return FALSE;
	}
	score = bot_hunch(score, kind, point, object);
	if (bot_consider_noroute && object == NONE && !objective && distance3d(point, bot_consider_noroute) < 4.f)
		score *= 0.1f;
	if (score > best->score)
	{
		best->score = score;
		best->kind = kind;
		best->point = *point;
		best->object = object;
		best->ticks = ticks;
		best->objective = objective;
		best->squad = FALSE;
		best->explore = FALSE;
		return TRUE;
	}
	return FALSE;
}

/* carrying the ball (or it): somewhere well away from them, and not the
   same corner every time. Places far from them and not far from us score
   best; each bot weighs them with a bit of its own chance, and one it's
   already heading for stays while it's still a good way off from them */
static void bot_keep_away_spot(struct bot_state *bot, real_point3d const *enemy, real_point3d const *position, real_point3d *away)
{
	real best_far = -1.0e9f;
	long i;
	*away = *position;
	for (i = 0; i < nav_place_count(); i++)
	{
		real_point3d const *place = aas_place(i);
		real far = bot_distance2d(place, enemy) - 0.5f * bot_distance2d(place, position);
		far *= 0.75f + 0.5f * bot_random(bot);
		if (bot->goal_kind == _bot_goal_roam && bot->goal_objective && distance3d(place, &bot->goal) < 3.f)
			far *= 1.3f; /* (keep going where we were going) */
		if (far > best_far)
		{
			best_far = far;
			*away = *place;
		}
	}
}

/* How much the game's objective (the flag, the hill, the ball, the next
   checkpoint) is worth to a bot: much the same to all of them. Its character
   shows in how it goes about it, and in what it does when there is nothing
   to be done for the objective; it does not decide whether it plays the game.
   (It was 0.6 to 1.4 times: the least minded hardly went for the flag.) */
#define BOT_OBJECTIVE_FOCUS(bot) (0.9f + 0.3f * (bot)->personality.focus)
/* ... and how much its own errands are worth in a game that has one (its
   grudges, its places, trailing its side, other people's fights), and going
   to look for a fight for the fight's sake */
#define BOT_OBJECTIVE_PERSONAL 0.35f
#define BOT_OBJECTIVE_HUNT 0.7f

/* capture the flag goals. The variants change the plan:
   - single flag (a turn each): the side with a flag defends it, everyone
     on the other side attacks
   - flag at home to score: a carrier waits near base (out of sight) while
     the others get our flag back
   - flag must reset: touching our dropped flag does nothing, so it's
     guarded where it lies until it goes home
   - assault/invasion: the scoring spot isn't our own base, it's wherever
     the game says our capture point is (ctf_bot_flag's base) */
static void bot_ctf_candidates(struct bot_state *bot, struct player_datum *player, struct unit_datum *unit,
	real_point3d const *position, real held, struct bot_candidate *best)
{
	struct ctf_variant const *variant = &game_engine_get_variant()->game_engine_variant.ctf;
	long team = player->team_index;
	long my_flag;
	long their_flag;
	real_point3d my_base;
	real_point3d their_base;
	struct object_datum *mine;
	struct object_datum *theirs;
	int defender;
	int single = variant->single_flag_time > 0;
	int wait_for_ours = variant->flag_at_home_to_score && !single;
	int must_reset = variant->flag_must_reset != 0;
	int ours_home;
	real focus = BOT_OBJECTIVE_FOCUS(bot);
	real armed = held < 3.5f ? 0.6f + 0.3f * bot->personality.focus : 1.f; /* unarmed: a weapon on the way first */

	if (team < 0 || team > 1 || !ctf_bot_flag(team, &my_flag, &my_base) || !ctf_bot_flag(1 - team, &their_flag, &their_base))
		return;
	mine = my_flag != NONE ? object_try_and_get(my_flag) : NULL;
	theirs = their_flag != NONE ? object_try_and_get(their_flag) : NULL;
	ours_home = !mine || (mine->object.parent_object_index == NONE && distance3d(&mine->object.position, &my_base) < 2.f);
	defender = bot_ctf_defender(bot, player);
	/* one flag at a time: whoever has it defends, the others all attack */
	if (single && mine && !theirs)
		defender = TRUE;
	else if (single && theirs && !mine)
		defender = FALSE;

	/* got it: to our capture point, nothing else matters (when ours has
	   to be home first: close by, out of the way, until it is) */
	if (theirs && bot_holds_object(unit, their_flag))
	{
		if (wait_for_ours && !ours_home && distance3d(position, &my_base) < 14.f)
		{
			real_point3d spot = my_base;
			spot.x += (bot_random(bot) - 0.5f) * 8.f;
			spot.y += (bot_random(bot) - 0.5f) * 8.f;
			/* (3000: over the run home it's already on, which is kept
			   unless something's 1.3 times better, hunches and all) */
			bot_consider(best, 3000.f, _bot_goal_hold, &spot, NONE, 3 * BOT_TICKS_PER_SECOND, TRUE);
		}
		else
			bot_consider(best, 1000.f, _bot_goal_roam, &my_base, NONE, 30 * BOT_TICKS_PER_SECOND, TRUE);
		return;
	}
	/* ours is out: stop the carrier; touch it to send it back, or (when it
	   has to go back by itself) stand over it. More so when one of ours
	   is waiting with theirs to score */
	{
		real urgency = 1.f;
		if (wait_for_ours && theirs && theirs->object.parent_object_index != NONE)
		{
			long carrier = object_get_ultimate_parent(their_flag);
			struct unit_datum *carrier_unit = unit_try_and_get(carrier);
			if (carrier_unit && carrier_unit->unit.player_index != NONE && player_try_and_get(carrier_unit->unit.player_index) &&
				player_get(carrier_unit->unit.player_index)->team_index == team)
			{
				urgency = 2.f;
			}
		}
		if (mine && mine->object.parent_object_index != NONE)
		{
			/* after them; from far off, cut them off on the way to where
			   they're taking it */
			real_point3d at = object_get(object_get_ultimate_parent(my_flag))->object.position;
			real distance = distance3d(&at, position);
			if (distance > 15.f)
			{
				at.x += (their_base.x - at.x) * 0.3f;
				at.y += (their_base.y - at.y) * 0.3f;
				at.z += (their_base.z - at.z) * 0.3f;
			}
			bot_consider(best, urgency * focus * (defender ? 60.f : 42.f) / (1.f + distance / 100.f), _bot_goal_enemy, &at, NONE,
				2 * BOT_TICKS_PER_SECOND, TRUE);
		}
		else if (mine && !ours_home)
		{
			real distance = distance3d(&mine->object.position, position);
			if (must_reset)
			{
				/* nothing to touch: guard it where it lies */
				real_point3d spot = mine->object.position;
				if (distance < 5.f)
				{
					spot.x += (bot_random(bot) - 0.5f) * 6.f;
					spot.y += (bot_random(bot) - 0.5f) * 6.f;
					bot_consider(best, urgency * focus * (defender ? 16.f : 8.f), _bot_goal_hold, &spot, NONE, 4 * BOT_TICKS_PER_SECOND, TRUE);
				}
				else
					bot_consider(best, urgency * focus * (defender ? 20.f : 10.f) / (1.f + distance / 60.f), _bot_goal_roam, &spot, NONE,
						10 * BOT_TICKS_PER_SECOND, TRUE);
			}
			else
				bot_consider(best, urgency * focus * 25.f / (1.f + distance / 60.f), _bot_goal_item, &mine->object.position, my_flag,
					20 * BOT_TICKS_PER_SECOND, TRUE);
		}
	}
	if (defender && mine)
	{
		/* guard our flag: get back to it, then walk about near it (the
		   bold further out, to meet them on the way in) */
		real reach = 6.f + 14.f * bot->personality.aggression;
		if (distance3d(position, &my_base) > reach + 4.f)
			bot_consider(best, 12.f * focus, _bot_goal_roam, &my_base, NONE, 20 * BOT_TICKS_PER_SECOND, TRUE);
		else if (bot->goal_kind != _bot_goal_hold || bot->goal_ticks <= 0)
		{
			real_point3d spot = my_base;
			real toward = bot_distance2d(&my_base, &their_base);
			spot.x += (bot_random(bot) - 0.5f) * 10.f;
			spot.y += (bot_random(bot) - 0.5f) * 10.f;
			/* out toward where they come from */
			if (toward > 1.f)
			{
				real out = reach * (0.3f + 0.7f * bot_random(bot));
				spot.x += (their_base.x - my_base.x) / toward * out;
				spot.y += (their_base.y - my_base.y) / toward * out;
			}
			bot_consider(best, 6.f * focus, _bot_goal_hold, &spot, NONE, (long)((4.f + 6.f * bot->personality.patience) * BOT_TICKS_PER_SECOND), FALSE);
		}
		return;
	}
	if (!theirs)
		return;
	if (theirs->object.parent_object_index == NONE)
	{
		/* attack: go and get it, wherever it is; far off, in a vehicle.
		   The team-minded wait a moment for company when others are on
		   the way too */
		real distance = distance3d(&theirs->object.position, position);
		real score = armed * focus * (20.f + 10.f * bot->personality.aggression) / (1.f + distance / 120.f);
		bot_consider(best, score, _bot_goal_item, &theirs->object.position, their_flag, 40 * BOT_TICKS_PER_SECOND, TRUE);
		if (distance > 50.f && unit->object.parent_object_index == NONE)
		{
			struct object_iterator iterator;
			struct object_datum *object;
			object_iterator_new(&iterator, _object_mask_vehicle, 0);
			while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
			{
				real_point3d entry;
				real to_vehicle = distance3d(&object->object.position, position);
				long driver = unit_scripting_unit_driver(iterator.index);
				if (to_vehicle > 25.f || object->object.parent_object_index != NONE || bot_ignoring(bot, iterator.index) ||
					!bot_vehicle_entry(player->unit_index, iterator.index, &entry) ||
					bot_vehicle_has_enemy(iterator.index, player)) /* (one of them at its gun and nobody driving: theirs) */
				{
					continue;
				}
				if (driver != NONE)
				{
					struct unit_datum *driver_unit = unit_try_and_get(driver);
					struct player_datum *driver_player = driver_unit && driver_unit->unit.player_index != NONE ?
						player_try_and_get(driver_unit->unit.player_index) : NULL;
					if (!driver_player || bot_is_enemy((short)player->team_index, driver_player))
						continue;
				}
				bot_consider(best, score * (0.9f + 0.8f * bot->personality.vehicle_love) / (1.f + to_vehicle / 40.f), _bot_goal_vehicle, &entry,
					iterator.index, 20 * BOT_TICKS_PER_SECOND, TRUE);
			}
		}
	}
	else
	{
		/* one of ours has it: go with them, a little ahead toward the
		   capture point, and shoot whoever chases them */
		long carrier = object_get_ultimate_parent(their_flag);
		real_point3d at = object_get(carrier)->object.position;
		real distance = distance3d(&at, position);
		if (distance > 5.f)
		{
			real toward = bot_distance2d(&at, &my_base);
			if (toward > 1.f)
			{
				at.x += (my_base.x - at.x) / toward * 4.f;
				at.y += (my_base.y - at.y) / toward * 4.f;
			}
			bot_consider(best, (6.f + 16.f * bot->personality.teamwork) / (1.f + distance / 80.f), _bot_goal_enemy, &at, NONE,
				2 * BOT_TICKS_PER_SECOND, TRUE);
		}
	}
}

extern struct game_engine king_engine;
extern struct game_engine oddball_engine;
extern struct game_engine ctf_engine;
extern struct game_engine race_engine;
extern struct game_engine slayer_engine;
boolean player_has_ball(long player_index); /* game_engine_oddball.c */
boolean king_bot_hill(real_point3d *center); /* game_engine_king.c */
boolean race_bot_next_checkpoint(long player_index, real_point3d const *from, real_point3d *point); /* game_engine_race.c */

/* a game that is won by something other than kills */
static int bot_game_has_objective(void)
{
	return game_engine == &ctf_engine || game_engine == &king_engine || game_engine == &oddball_engine || game_engine == &race_engine;
}

/* the nearest living enemy's position: FALSE when there is none */
static int bot_nearest_enemy(struct player_datum *player, real_point3d const *position, real_point3d *at, real *distance_out)
{
	struct data_iterator iterator;
	struct player_datum *other;
	real best = 1.0e9f;

	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		real_point3d where;
		real distance;
		if (other == player || other->unit_index == NONE || !bot_unit_alive(other->unit_index) ||
			!bot_is_enemy((short)player->team_index, other))
		{
			continue;
		}
		where = object_get(object_get_ultimate_parent(other->unit_index))->object.position;
		distance = distance3d(&where, position);
		if (distance < best)
		{
			best = distance;
			*at = where;
		}
	}
	if (distance_out)
		*distance_out = best;
	return best < 1.0e8f;
}

/* king of the hill and oddball */
/* where to go to catch someone on the move: ahead of them along the way
   they're going, by how long it takes us to get near (not where they are
   now: that's behind them by the time we get there) */
static real_point3d bot_intercept(long unit_index, real_point3d const *from)
{
	struct object_datum *root = object_get(object_get_ultimate_parent(unit_index));
	real_point3d at = root->object.position;
	real distance = distance3d(&at, from);
	real ahead = MIN(3.f, distance / (2.f * AAS_FOOT_SPEED_BOT));
	real_point3d lead = at;
	lead.x += root->object.translational_velocity.i * BOT_TICKS_PER_SECOND * ahead;
	lead.y += root->object.translational_velocity.j * BOT_TICKS_PER_SECOND * ahead;
	/* (not through a wall: as far as the floor goes that way) */
	if (aas_ready() && !aas_line_in_areas(AAS_FOOT, &at, &lead))
		return at;
	return lead;
}

static void bot_game_candidates(struct bot_state *bot, struct player_datum *player, struct unit_datum *unit,
	real_point3d const *position, real held, struct bot_candidate *best)
{
	real armed = held < 3.5f ? 0.6f + 0.3f * bot->personality.focus : 1.f;
	real focus = BOT_OBJECTIVE_FOCUS(bot);
	real_point3d hill_center;

	/* race: the next checkpoint we may touch, in a vehicle if there's one
	   near (the driving code takes it from there) */
	if (game_engine == &race_engine)
	{
		real_point3d point;
		long me_index = NONE;
		struct data_iterator iterator;
		struct player_datum *other;
		data_iterator_new(&iterator, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
			if (other == player)
				me_index = iterator.datum_index;
		if (me_index != NONE && race_bot_next_checkpoint(me_index, position, &point))
		{
			real distance = distance3d(&point, position);
			bot_consider(best, focus * 40.f / (1.f + distance / 200.f), _bot_goal_roam, &point, NONE, 20 * BOT_TICKS_PER_SECOND, TRUE);
		}
	}
	/* reverse tag, accumulation (magic ball), juggernaut, stalker
	   (terminator): no ball to carry once someone is "it" */
	if (game_engine == &oddball_engine && game_engine_get_variant()->game_engine_variant.oddball.oddball_ball_type != 0)
	{
		long terminator = game_engine_get_variant()->game_engine_variant.oddball.oddball_ball_type == 2;
		struct data_iterator iterator;
		struct player_datum *other;
		int i_am_it = FALSE;
		real nearest_it = 1.0e9f;
		real_point3d it_at;
		data_iterator_new(&iterator, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			real_point3d at;
			real distance;
			if (!player_has_ball(iterator.datum_index))
				continue;
			if (other == player)
			{
				i_am_it = TRUE;
				continue;
			}
			if (other->unit_index == NONE || !bot_unit_alive(other->unit_index) || !bot_is_enemy((short)player->team_index, other))
				continue;
			at = object_get(object_get_ultimate_parent(other->unit_index))->object.position;
			distance = distance3d(&at, position);
			if (distance < nearest_it)
			{
				nearest_it = distance;
				it_at = bot_intercept(other->unit_index, position);
			}
		}
		if (i_am_it && terminator)
		{
			/* the juggernaut scores by killing: go and find them */
			real_point3d at;
			real distance;
			if (bot_nearest_enemy(player, position, &at, &distance))
				bot_consider(best, focus * 30.f / (1.f + distance / 150.f), _bot_goal_enemy, &at, NONE, 3 * BOT_TICKS_PER_SECOND, TRUE);
		}
		else if (i_am_it)
		{
			/* it scores by staying alive: keep away from them */
			real_point3d enemy;
			real enemy_distance;
			if (bot_nearest_enemy(player, position, &enemy, &enemy_distance) && enemy_distance < 35.f)
			{
				real_point3d away;
				bot_keep_away_spot(bot, &enemy, position, &away);
				bot_consider(best, 1000.f, _bot_goal_roam, &away, NONE, 5 * BOT_TICKS_PER_SECOND, TRUE);
			}
		}
		else if (nearest_it < 1.0e8f)
		{
			/* whoever's it: kill them to take it */
			bot_consider(best, armed * focus * (24.f + 8.f * bot->personality.aggression) / (1.f + nearest_it / 150.f), _bot_goal_enemy,
				&it_at, NONE, 2 * BOT_TICKS_PER_SECOND, TRUE);
		}
	}

	if (king_bot_hill(&hill_center))
	{
		/* on the hill, and stay there */
		real distance = distance3d(&hill_center, position);
		real_point3d spot = hill_center;
		if (distance < 3.f)
		{
			/* on it: move about a little rather than stand still */
			spot.x += (bot_random(bot) - 0.5f) * 3.f;
			spot.y += (bot_random(bot) - 0.5f) * 3.f;
		}
		bot_consider(best, armed * focus * (18.f + 8.f * bot->personality.aggression) / (1.f + distance / 120.f),
			_bot_goal_roam, &spot, NONE, 3 * BOT_TICKS_PER_SECOND, TRUE);
	}
	if (game_engine == &oddball_engine)
	{
		struct object_iterator iterator;
		struct object_datum *object;
		object_iterator_new(&iterator, _object_mask_weapon, 0);
		while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
		{
			long ball = iterator.index;
			if (!bot_name_has(object->definition_index, "ball"))
				continue;
			if (bot_holds_object(unit, ball))
			{
				/* got it: keep away from them */
				real_point3d enemy;
				real enemy_distance;
				if (bot_nearest_enemy(player, position, &enemy, &enemy_distance) && enemy_distance < 30.f)
				{
					real_point3d away;
					bot_keep_away_spot(bot, &enemy, position, &away);
					bot_consider(best, 1000.f, _bot_goal_roam, &away, NONE, 5 * BOT_TICKS_PER_SECOND, TRUE);
				}
				else
					bot_consider(best, 1000.f, _bot_goal_hold, position, NONE, 3 * BOT_TICKS_PER_SECOND, TRUE);
				return;
			}
			if (object->object.parent_object_index == NONE)
			{
				/* loose: go and get it (by the way there, not as the crow
				   flies; somewhere nobody can get to, it's no use going for:
				   hunt the others meanwhile) */
				real distance = distance3d(&object->object.position, position);
				if (aas_ready())
				{
					struct bot_body walker;
					float time;
					memset(&walker, 0, sizeof(walker));
					walker.position = *position;
					walker.vehicle_index = NONE;
					time = bot_travel_time(&walker, AAS_FOOT, &object->object.position);
					if (time < 0.f)
						continue;
					distance = MAX(distance, time * aas_speed(AAS_FOOT));
				}
				/* the ball is how the game is won: before a fight, before a
				   better gun, whoever's about */
				bot_consider(best, (60.f + 40.f * focus) / (1.f + distance / 120.f), _bot_goal_item, &object->object.position, ball,
					20 * BOT_TICKS_PER_SECOND, TRUE);
			}
			else
			{
				/* someone has it: an enemy, get them; a teammate, cover them */
				long carrier = object_get_ultimate_parent(ball);
				struct unit_datum *carrier_unit = unit_try_and_get(carrier);
				struct player_datum *carrier_player = carrier_unit && carrier_unit->unit.player_index != NONE ?
					player_try_and_get(carrier_unit->unit.player_index) : NULL;
				real_point3d at = object_get(carrier)->object.position;
				real distance = distance3d(&at, position);
				if (carrier_player && bot_is_enemy((short)player->team_index, carrier_player))
					at = bot_intercept(carrier, position); /* (cut them off) */
				if (carrier_player && bot_is_enemy((short)player->team_index, carrier_player))
					bot_consider(best, (40.f + 30.f * focus) * MAX(armed, 0.5f) / (1.f + distance / 120.f), _bot_goal_enemy, &at, NONE,
						2 * BOT_TICKS_PER_SECOND, TRUE);
				else if (distance > 6.f)
					bot_consider(best, (4.f + 12.f * bot->personality.teamwork) / (1.f + distance / 80.f), _bot_goal_enemy, &at, NONE,
						2 * BOT_TICKS_PER_SECOND, TRUE);
			}
		}
	}
}

/* a driver's capture-the-flag errand: their flag (to get out at and take),
   or our flag's carrier; FALSE when there's none */
static int bot_ctf_drive_target(struct bot_state *bot, struct player_datum *player, real_point3d *point, int *get_out_there)
{
	long team = player->team_index;
	long my_flag;
	long their_flag;
	real_point3d my_base;
	real_point3d their_base;
	struct object_datum *mine;
	struct object_datum *theirs;

	*get_out_there = FALSE;
	if (team < 0 || team > 1 || !ctf_bot_flag(team, &my_flag, &my_base) || !ctf_bot_flag(1 - team, &their_flag, &their_base))
		return FALSE;
	mine = my_flag != NONE ? object_try_and_get(my_flag) : NULL;
	theirs = their_flag != NONE ? object_try_and_get(their_flag) : NULL;
	if (mine && mine->object.parent_object_index != NONE)
	{
		*point = object_get(object_get_ultimate_parent(my_flag))->object.position;
		return TRUE;
	}
	{
		/* (one flag at a time: whoever has one defends it, the others all
		   attack, whatever their job) */
		struct ctf_variant const *variant = &game_engine_get_variant()->game_engine_variant.ctf;
		int defender = bot_ctf_defender(bot, player);
		if (variant->single_flag_time > 0)
			defender = mine && !theirs;
		if (defender)
			return FALSE;
	}
	if (theirs && theirs->object.parent_object_index == NONE)
	{
		*point = theirs->object.position;
		*get_out_there = TRUE;
		return TRUE;
	}
	return FALSE;
}

/* ---------- roles

A team shares the jobs out: a driver and a gunner when the map has
vehicles, a sniper, a rocket man, (in capture the flag) defenders, and
everyone else slays or attacks. Each job goes to whoever's personality
fits it best, so the same bot tends to get the same job. In free for all
a bot's own tastes decide. */

enum
{
	_bot_role_slayer = 0,
	_bot_role_driver,
	_bot_role_gunner,
	_bot_role_sniper,
	_bot_role_rocket,
	NUMBER_OF_BOT_ROLES
};

static char const *const bot_role_names[NUMBER_OF_BOT_ROLES] = { "slayer", "driver", "gunner", "sniper", "rocket" };


static real bot_role_fit(struct bot_personality const *personality, long role)
{
	switch (role)
	{
	case _bot_role_driver: return personality->vehicle_love * 2.f + personality->aggression * 0.5f;
	case _bot_role_gunner: return personality->vehicle_love + personality->accuracy * -0.5f + 0.5f;
	case _bot_role_sniper: return personality->patience * 2.f - personality->aggression * 0.5f;
	case _bot_role_rocket: return personality->greed * 1.5f + personality->aggression * 0.5f;
	}
	return 0.f;
}

static long bot_role(struct bot_state const *bot, struct player_datum const *player, int teams)
{
	static long const order[] = { _bot_role_driver, _bot_role_sniper, _bot_role_gunner, _bot_role_rocket };
	long taken[BOT_MAXIMUM_PLAYERS];
	long bots[BOT_MAXIMUM_PLAYERS];
	long bot_count = 0;
	long me = NONE;
	long r;
	struct data_iterator iterator;
	struct player_datum *other;

	if (!teams)
	{
		if (bot->personality.patience > 0.7f)
			return _bot_role_sniper;
		if (bot->personality.greed > 0.8f)
			return _bot_role_rocket;
		return _bot_role_slayer;
	}
	/* the bots on our team */
	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		if (index >= BOT_MAXIMUM_PLAYERS || other->team_index != player->team_index || !bot_states[index].has_personality)
			continue;
		if (&bot_states[index] == bot)
			me = bot_count;
		taken[bot_count] = FALSE;
		bots[bot_count++] = index;
	}
	if (me == NONE)
		return _bot_role_slayer;
	/* hand the jobs out in order of importance, each to the best fit left */
	for (r = 0; r < (long)(sizeof(order) / sizeof(order[0])); r++)
	{
		long role = order[r];
		long best = NONE;
		long i;
		if ((role == _bot_role_driver || role == _bot_role_gunner) && bots_vehicle_count == 0)
			continue;
		if (role == _bot_role_gunner && bot_count < 3)
			continue;
		if (role == _bot_role_rocket && bot_count < 4)
			continue;
		for (i = 0; i < bot_count; i++)
			if (!taken[i] && (best == NONE ||
				bot_role_fit(&bot_states[bots[i]].personality, role) > bot_role_fit(&bot_states[bots[best]].personality, role)))
			{
				best = i;
			}
		if (best == NONE)
			break;
		taken[best] = TRUE;
		if (best == me)
			return role;
	}
	return _bot_role_slayer;
}

/* where a sniper sets up: high places that see a lot of the map */
#define BOT_MAXIMUM_VANTAGES 8
static real_point3d bots_vantages[BOT_MAXIMUM_VANTAGES];
static long bots_vantage_count;

static void bots_find_vantages(void)
{
	real best_score[BOT_MAXIMUM_VANTAGES];
	unsigned long seed = bots_match(); /* (a different sample of the map each match) */
	long sample;

	bots_vantage_count = 0;
	if (!aas_ready() || nav_place_count() == 0)
		return;
	for (sample = 0; sample < 300; sample++)
	{
		long node;
		real_point3d eye;
		real_point3d spot;
		real score = 0.f;
		long p;
		long slot;
		seed = bot_hash(seed + (unsigned long)sample);
		node = aas_random_area(AAS_FOOT, seed);
		if (node == NONE)
			continue;
		spot = aas_area(AAS_FOOT, node)->center;
		eye = spot;
		eye.z += 0.6f;
		/* only near the places people fight over */
		{
			real nearest = 1.0e9f;
			for (p = 0; p < nav_place_count(); p++)
				nearest = MIN(nearest, bot_distance2d(&(*aas_place(p)), &eye));
			if (nearest > 30.f)
				continue;
		}
		for (p = 0; p < nav_place_count() && p < 24; p++)
		{
			real_point3d place = (*aas_place((p * 7 + sample) % nav_place_count()));
			real distance = distance3d(&place, &eye);
			place.z += 0.6f;
			if (distance > 15.f && distance < 90.f && bot_line_clear(bot_sight_flags, &eye, &place, NONE))
				score += 1.f;
		}
		score += MAX(0.f, eye.z) * 0.3f;
		/* keep the best few */
		slot = NONE;
		if (bots_vantage_count < BOT_MAXIMUM_VANTAGES)
			slot = bots_vantage_count++;
		else
		{
			long i;
			long worst = 0;
			for (i = 1; i < BOT_MAXIMUM_VANTAGES; i++)
				if (best_score[i] < best_score[worst])
					worst = i;
			if (score > best_score[worst])
				slot = worst;
		}
		if (slot != NONE)
		{
			best_score[slot] = score;
			bots_vantages[slot] = spot;
		}
	}
}

/* how many of our side (bots) are already heading for somewhere near point */
static long bot_side_heading_near(long me, struct player_datum *player, real_point3d const *point, real radius)
{
	struct data_iterator iterator;
	struct player_datum *other;
	long count = 0;
	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		if (index == me || index >= BOT_MAXIMUM_PLAYERS || !bot_states[index].has_personality || other->unit_index == NONE ||
			bot_states[index].unit_index != other->unit_index ||
			(game_engine_has_teams() && bot_is_enemy((short)player->team_index, other)))
		{
			continue;
		}
		if (bot_states[index].goal_kind != _bot_goal_none && distance3d(&bot_states[index].goal, point) < radius)
			count++;
	}
	return count;
}

/* how strong a player is right now: weapon, shields (over one is an
   overshield), camouflage */
static real bot_player_strength(long unit_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	real strength = 1.f;
	if (!unit)
		return 0.f;
	if (unit->unit.current_weapon_index >= 0 && unit->unit.weapon_object_indices[unit->unit.current_weapon_index] != NONE &&
		object_try_and_get(unit->unit.weapon_object_indices[unit->unit.current_weapon_index]))
	{
		strength += bot_item_value(object_get(unit->unit.weapon_object_indices[unit->unit.current_weapon_index])->definition_index) * 0.3f;
	}
	strength += unit->object.shield_vitality * 1.5f + unit->object.body_vitality;
	if (unit->unit.active_camouflage > 0.5f)
		strength += 1.5f;
	if (unit->object.parent_object_index != NONE)
		strength += 2.f; /* in a vehicle */
	return strength;
}

/* our side's strength near a point, and theirs */
static void bot_forces_near(struct player_datum *player, real_point3d const *point, real radius, real *ours, real *theirs)
{
	struct data_iterator iterator;
	struct player_datum *other;
	*ours = 0.f;
	*theirs = 0.f;
	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		real_point3d at;
		if (other->unit_index == NONE || !bot_unit_alive(other->unit_index))
			continue;
		at = object_get(object_get_ultimate_parent(other->unit_index))->object.position;
		if (distance3d(&at, point) > radius)
			continue;
		if (other == player || !bot_is_enemy((short)player->team_index, other))
			*ours += bot_player_strength(other->unit_index);
		else
			*theirs += bot_player_strength(other->unit_index);
	}
}

/* would we have the upper hand going after someone at point? >1 yes */
static real bot_advantage(struct player_datum *player, real_point3d const *position, real_point3d const *point)
{
	real ours_here;
	real theirs_here;
	real ours_there;
	real theirs_there;
	bot_forces_near(player, position, 15.f, &ours_here, &theirs_here);
	bot_forces_near(player, point, 15.f, &ours_there, &theirs_there);
	return (ours_here + 0.5f * ours_there + 0.5f) / (theirs_there + 0.5f * theirs_here + 0.5f);
}

/* somewhere near an enemy to come at them from: off to one side, higher
   ground when there is some (not far above them: somewhere that's theirs
   to hold) */
static real_point3d bot_approach_spot(struct bot_state *bot, real_point3d const *enemy, real_point3d const *from)
{
	real_point3d best = *enemy;
	real best_score = -1.0e9f;
	long sample;
	best.x += (bot_random(bot) - 0.5f) * 10.f;
	best.y += (bot_random(bot) - 0.5f) * 10.f;
	if (!aas_ready())
		return best;
	for (sample = 0; sample < 8; sample++)
	{
		real_point3d spot = *enemy;
		long area;
		real score;
		spot.x += (bot_random(bot) - 0.5f) * 14.f;
		spot.y += (bot_random(bot) - 0.5f) * 14.f;
		area = aas_nearest_area(AAS_FOOT, &spot, 4.f, &spot);
		if (area == NONE || spot.z > enemy->z + 4.f)
			continue;
		/* higher is better; so is from the side we're coming from */
		score = MIN(spot.z - enemy->z, 3.f) - 0.1f * bot_distance2d(&spot, from) / 10.f + bot_random(bot);
		if (score > best_score)
		{
			best_score = score;
			best = spot;
		}
	}
	return best;
}

/* ---------- team slayer: going round the map together

In team slayer a side moves as a squad: a leader (a person on the team
when there is one, or else one of the bots) heads for where the enemy was
last seen, or failing that for part of the map the team hasn't been to in
a while; the rest keep with the leader, spread out a few units behind and
to the sides, and fight whoever they meet. A leader whose squad has
fallen behind waits for it */

struct bot_squad
{
	long leader; /* player absolute index, NONE */
	real_point3d goal;
	long until;
	long updated;
	real_point3d leader_at;
	real leader_yaw;
};

static struct bot_squad bot_squads[2];

/* who was alive last tick and where (deaths mark the danger map); cleared
   with the map */
static long bots_alive_unit[BOT_MAXIMUM_PLAYERS];
static real_point3d bots_last_position[BOT_MAXIMUM_PLAYERS];

static int bot_is_team_slayer(void)
{
	return game_engine == &slayer_engine && game_engine_has_teams();
}

static struct player_datum *bot_player_at_index(long index)
{
	struct player_datum *player;
	if (!player_data || index < 0 || index >= player_data->maximum_count)
		return NULL;
	player = (struct player_datum *)((byte *)xbox_pointer(player_data->data) + index * player_data->size);
	return ((struct datum_header *)player)->identifier ? player : NULL;
}

static struct bot_squad *bot_squad_update(long team, unsigned long seed)
{
	struct bot_squad *squad;
	struct data_iterator iterator;
	struct player_datum *player;
	struct player_datum *leader;
	long now = game_time_get();
	long best = NONE;
	int best_human = FALSE;
	real_point3d known;
	if (team != 0 && team != 1)
		return NULL;
	squad = &bot_squads[team];
	if (squad->updated == now && squad->leader != NONE)
		return squad;
	squad->updated = now;
	/* the leader: a person on our side first; else the one who was; else
	   a bot this match's luck picks (not always the first slot) */
	unsigned long best_rank = 0;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		int human;
		if (player->team_index != team || player->unit_index == NONE || !bot_unit_alive(player->unit_index))
			continue;
		human = !bot_unit_is_bot(player->unit_index);
		{
			unsigned long rank = bot_hash((unsigned long)index ^ bots_match());
			int better = best == NONE || (human && !best_human);
			if (!better && human == best_human && best != squad->leader)
				better = index == squad->leader || rank < best_rank;
			if (better)
			{
				best = index;
				best_human = human;
				best_rank = rank;
			}
		}
	}
	squad->leader = best;
	leader = bot_player_at_index(best);
	if (!leader)
		return NULL;
	{
		struct object_datum *root = object_get(object_get_ultimate_parent(leader->unit_index));
		struct unit_datum *unit = unit_get(leader->unit_index);
		squad->leader_at = root->object.position;
		squad->leader_yaw = bot_wrap((real)atan2(unit->unit.desired_facing_vector.j, unit->unit.desired_facing_vector.i));
	}
	/* where to: the enemy our side knows of; else somewhere we haven't been */
	if (bot_side_nearest_known(team, &squad->leader_at, 8.f, &known))
	{
		if (now >= squad->until || distance3d(&known, &squad->goal) > 12.f)
		{
			squad->goal = known;
			squad->until = now + 8 * BOT_TICKS_PER_SECOND;
		}
	}
	else if ((now >= squad->until || bot_distance2d(&squad->leader_at, &squad->goal) < 8.f) && (seed >> 5) % 10 < 7 &&
		bot_nearest_enemy(leader, &squad->leader_at, &known, NULL))
	{
		/* roughly where they are (as players learn a map's hot spots) */
		squad->goal = known;
		squad->goal.x += (real)((long)(bot_hash(seed + (unsigned long)now) % 1200) - 600) / 100.f;
		squad->goal.y += (real)((long)(bot_hash(seed ^ (unsigned long)now) % 1200) - 600) / 100.f;
		squad->until = now + 10 * BOT_TICKS_PER_SECOND;
	}
	else if (now >= squad->until || bot_distance2d(&squad->leader_at, &squad->goal) < 8.f)
	{
		real best_score = -1.f;
		long sample;
		long region_before = nav_spot_region;
		nav_spot_region = nav_region_at(&squad->leader_at); /* (a very large map: where the leader can get to) */
		for (sample = 0; sample < 10; sample++)
		{
			real_point3d spot;
			real stale;
			real distance;
			real score;
			if (!nav_random_spot(bot_hash(seed + (unsigned long)sample * 7919UL + (unsigned long)now), &spot))
				break;
			stale = PIN((real)(now - *nav_visit_slot(team, &spot)) / (40.f * BOT_TICKS_PER_SECOND), 0.f, 1.f);
			distance = bot_distance2d(&spot, &squad->leader_at);
			if (distance < 20.f)
				continue;
			score = (0.3f + stale) * MIN(distance, 70.f);
			if (score > best_score)
			{
				best_score = score;
				squad->goal = spot;
			}
		}
		nav_spot_region = region_before;
		squad->until = now + 25 * BOT_TICKS_PER_SECOND;
	}
	return squad;
}

static void bot_squad_candidates(struct bot_state *bot, long me, struct player_datum *player, real_point3d const *position,
	struct bot_candidate *best)
{
	struct bot_squad *squad = bot_squad_update(player->team_index, bot->random);
	real teamwork = bot->personality.teamwork;
	if (!squad || squad->leader == NONE)
		return;
	if (squad->leader == me)
	{
		/* leading: on, unless most of them have fallen behind */
		struct data_iterator iterator;
		struct player_datum *other;
		long behind = 0;
		long count = 0;
		data_iterator_new(&iterator, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (other == player || other->team_index != player->team_index || other->unit_index == NONE || !bot_unit_alive(other->unit_index))
				continue;
			count++;
			if (distance3d(&object_get(object_get_ultimate_parent(other->unit_index))->object.position, position) > 18.f)
				behind++;
		}
		real score = 6.f + 4.f * teamwork;
		real_point3d const *where = count > 0 && behind * 2 > count ? position : &squad->goal;
		long kind = where == position ? _bot_goal_hold : _bot_goal_roam;
		if (bot_consider(best, score, kind, where, NONE, (kind == _bot_goal_hold ? 2 : 5) * BOT_TICKS_PER_SECOND, FALSE))
			best->squad = TRUE;
		return;
	}
	{
		/* following: a place of our own a few units behind the leader and
		   to one side (by our order in the team) */
		struct data_iterator iterator;
		struct player_datum *other;
		long order = 0;
		long count = 0;
		real angle;
		real radius;
		real_point3d spot;
		real distance;
		real score;
		data_iterator_new(&iterator, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			long index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
			if (other->team_index != player->team_index || other->unit_index == NONE || !bot_unit_alive(other->unit_index) ||
				index == squad->leader)
			{
				continue;
			}
			if (index < me)
				order++;
			count++;
		}
		angle = squad->leader_yaw + BOT_PI + (order - (count - 1) * 0.5f) * 0.7f;
		radius = 3.f + (order % 2) * 1.5f;
		spot = squad->leader_at;
		spot.x += (real)cos(angle) * radius;
		spot.y += (real)sin(angle) * radius;
		distance = distance3d(&spot, position);
		/* the further behind, the keener to catch up */
		score = (6.f + 6.f * teamwork) * (distance > 20.f ? 1.4f : 1.f);
		if (bot_consider(best, score, _bot_goal_roam, &spot, NONE, 2 * BOT_TICKS_PER_SECOND, FALSE))
		{
			best->squad = TRUE;
			bot->squad_point = spot;
		}
	}
}

/* what to do next: every candidate scored, the best wins; the current goal
   is kept unless something is clearly better */
/* A gun worth having in its own right (by its kind: the rifle, the pistol,
   the power weapons; and with something in it), and what counts as a poor
   one: one to swap for the first of those that turns up (by its kind: the
   needler, the plasma pistol; or any gun nearly dry). The plasma rifle is
   neither. */
#define BOT_PROPER_GUN 5.f
#define BOT_POOR_GUN 4.5f
#define BOT_DRY_GUN 3.5f

static int bot_weapon_is_proper(long weapon_index)
{
	struct object_datum *object = weapon_index != NONE ? object_try_and_get(weapon_index) : NULL;
	return object && bot_item_value(object->definition_index) >= BOT_PROPER_GUN && bot_weapon_worth(weapon_index) >= BOT_DRY_GUN + 0.5f;
}

static int bot_weapon_is_poor(long weapon_index)
{
	struct object_datum *object = weapon_index != NONE ? object_try_and_get(weapon_index) : NULL;
	if (!object || bot_goal_is_game_object(weapon_index))
		return FALSE; /* (the flag, the ball: not a gun at all) */
	return bot_item_value(object->definition_index) < BOT_POOR_GUN || bot_weapon_worth(weapon_index) < BOT_DRY_GUN;
}

/* nothing but poor guns on us (none at all counts) */
static int bot_all_held_poor(struct unit_datum *unit)
{
	long i;
	for (i = 0; i < MAXIMUM_WEAPONS_PER_UNIT; i++)
	{
		long weapon = unit->unit.weapon_object_indices[i];
		if (weapon != NONE && object_try_and_get(weapon) && !bot_weapon_is_poor(weapon))
			return FALSE;
	}
	return TRUE;
}

static int bot_slot_is_favorite(struct bot_state const *bot, struct unit_datum *unit, long slot)
{
	struct bot_weapon described;
	if (slot < 0 || slot >= MAXIMUM_WEAPONS_PER_UNIT || unit->unit.weapon_object_indices[slot] == NONE ||
		!object_try_and_get(unit->unit.weapon_object_indices[slot]))
	{
		return FALSE;
	}
	bot_weapon_describe(unit->unit.weapon_object_indices[slot], &described);
	return described.kind != _bot_gun_other && described.kind == bot->personality.favorite;
}

static void bot_choose_goal(struct bot_state *bot, long me, struct player_datum *player, struct unit_datum *unit,
	real_point3d const *position, int teams)
{
	struct object_iterator iterator;
	struct object_datum *object;
	real held = bot_best_held_value(unit);
	struct bot_candidate best;
	/* the things lying about worth going for, before how far each really is */
	enum { BOT_ITEM_CANDIDATES = 256 };
	struct
	{
		long object_index;
		real score;
		real distance;
		real falloff;
		real most;
	} items[BOT_ITEM_CANDIDATES];
	long item_count = 0;

	long role = bot_role(bot, player, teams);
	/* a game with something to do besides the killing: that first */
	int objective_game = bot_game_has_objective();
	real personal = objective_game ? BOT_OBJECTIVE_PERSONAL : 1.f;
	/* the guns on us: all of them poor ones; and, both hands full, the one
	   we'd drop for another a poor one (and not its favourite) */
	int all_poor = bot_all_held_poor(unit);
	int swap_poor = FALSE;
	{
		long slot = NONE;
		bot_worst_held(unit, &slot);
		swap_poor = slot != NONE && bot_weapon_is_poor(unit->unit.weapon_object_indices[slot]) && !bot_slot_is_favorite(bot, unit, slot);
	}

	memset(&best, 0, sizeof(best));
	best.kind = _bot_goal_none;
	best.object = NONE;
	bot_hunches_begin(bot, me);
	/* (a very large map: the part we are in, or were last, in the air) */
	bot_consider_region = NONE;
	bot_consider_from = *position;
	if (aas_ready() && navm_in_parts() && unit->object.parent_object_index == NONE)
	{
		long region = nav_region_at(position);
		if (region != NONE)
			bot->region = region;
		bot_consider_region = bot->region;
	}
	else
		bot->region = NONE;
	nav_spot_region = bot_consider_region;
	if (role != bot->role)
	{
		if (bot_logging())
			bot_log("bot%ld role %s", me, bot_role_names[role]);
		bot->role = role;
	}

	/* items: power weapons and powerups first, better weapons, grenades,
	   health when hurt */
	object_iterator_new(&iterator, _object_mask_weapon | _object_mask_equipment, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		real distance;
		real value;
		real score;

		if (!bot_item_lying(object))
			continue;
		distance = distance3d(position, &object->object.position);
		value = bot_item_value(object->definition_index);
		if (value <= 0.f)
			continue;
		/* further for better things; anywhere for the job's weapon or a powerup */
		if (distance > 25.f + 60.f * bot->personality.greed * (value >= 7.f ? 1.f : 0.3f) + (value > held + 2.f ? 30.f : 0.f) &&
			!(TEST_FLAG(_object_mask_equipment, object->object.type) && value >= 7.f) &&
			!((role == _bot_role_sniper && bot_name_has(object->definition_index, "sniper")) ||
				(role == _bot_role_rocket && bot_name_has(object->definition_index, "rocket"))))
		{
			continue;
		}
		if (TEST_FLAG(_object_mask_weapon, object->object.type))
		{
			if (bot_holds_weapon_type(unit, object->definition_index))
			{
				/* ammunition: worth it when we're low */
				long k;
				score = 0.2f;
				for (k = 0; k < MAXIMUM_WEAPONS_PER_UNIT; k++)
				{
					long mine = unit->unit.weapon_object_indices[k];
					struct bot_weapon described;
					if (mine == NONE || !object_try_and_get(mine) || object_get(mine)->definition_index != object->definition_index)
						continue;
					bot_weapon_describe(mine, &described);
					if (described.uses_ammunition && described.rounds_total + described.rounds_loaded < described.rounds_capacity / 2)
						score = 1.5f + (value >= 7.f ? 2.f : 0.f);
				}
			}
			else
			{
				/* what it'd be worth to us (an empty one: nothing), against
				   the worse of the two we carry (the one we'd drop for it:
				   an empty one, or a weaker one) */
				real theirs = bot_weapon_worth(iterator.index);
				long worst_slot = NONE;
				real worst = bot_worst_held(unit, &worst_slot);
				if (theirs <= 0.5f)
					continue;
				if (bot_weapons_held(unit) < 2)
					score = MAX(0.5f, theirs - held) + 1.f; /* a free hand */
				else
					score = theirs > worst + 1.f ? (theirs - worst) + (worst < 1.f ? 3.f : 0.f) : 0.1f;
				/* A poor gun in our hands (a needler, a plasma pistol, one
				   run nearly dry) and a proper one lying about (the pistol,
				   a rifle, anything better): worth going over for, and most
				   of all when the poor one is the best we have. (Not to be
				   rid of its favourite, poor as that one may be just now.) */
				if ((all_poor || swap_poor) && bot_weapon_is_proper(iterator.index) && (bot_weapons_held(unit) < 2 ? all_poor :
					swap_poor && theirs > worst))
				{
					score += all_poor ? 7.f : 3.5f;
				}
				/* power weapons: the greedy go a long way for them */
				/* power weapons: what everyone makes for first (worth more
				   than a fight with the rifle), the greedy most of all */
				/* (only one we'd take: not one we'd drop something as good for) */
				if (value >= 7.f && (bot_weapons_held(unit) < 2 || theirs > worst + 1.f))
					score += (7.f + 6.f * bot->personality.greed) * (held >= 7.f ? 0.3f : 1.f); /* (less so with one already) */
				/* its favourite: worth a detour */
				{
					struct bot_weapon described;
					bot_weapon_describe(iterator.index, &described);
					if (described.kind == bot->personality.favorite)
						score += 1.5f + 2.f * bot->personality.loyalty;
				}
			}
			/* the weapon that goes with the job, from anywhere */
			if ((role == _bot_role_sniper && bot_name_has(object->definition_index, "sniper")) ||
				(role == _bot_role_rocket && bot_name_has(object->definition_index, "rocket")))
			{
				if (!bot_holds_weapon_type(unit, object->definition_index))
					score = 12.f * (1.f + distance / 20.f) / (1.f + distance / 100.f); /* undone by the falloff below */
			}
		}
		else
		{
			if (value >= 7.f)
			{
				/* overshield, camouflage: worth going a long way for, and
				   worth having before a fight */
				score = (7.f + 9.f * bot->personality.greed) * (1.f + distance / 20.f) / (1.f + distance / 30.f);
				/* already powered up: not now */
				if (unit->object.shield_vitality > 1.05f || unit->unit.active_camouflage > 0.5f)
					score = 0.f;
			}
			else if (bot_name_has(object->definition_index, "health"))
				/* health: the more hurt, the more it's worth (it heals the
				   body, not the shields) */
				score = unit->object.body_vitality < 0.5f ?
					(2.f + 8.f * (1.f - unit->object.body_vitality)) * (1.3f - 0.6f * bot->personality.aggression) : 0.f;
			else if (bot_name_has(object->definition_index, "grenade"))
			{
				/* grenade lovers stock up */
				long carried = unit->unit.grenade_counts[bot_name_has(object->definition_index, "plasma") ? 1 : 0]; /* (its own kind: a stack of frags is no reason to leave plasmas) */
				score = carried >= 4 ? 0.f : (1.f + 4.f * bot->personality.grenade_happy) / (1.f + carried);
			}
			else
				score = 1.f;
		}
		if (score <= 0.f || bot_ignoring(bot, iterator.index))
			continue;
		/* others after it too (the more, the less worth it; hardly at all
		   when one of them is nearer), and where we've just been */
		{
			int nearer;
			long takers = bot_goal_takers(iterator.index, me, position, &nearer);
			if (takers > 0)
				score *= (nearer ? 0.35f : 0.7f) / (1.f + 0.6f * (takers - 1));
		}
		if (bot_recently_at(bot, &object->object.position) && iterator.index != bot->goal_object)
			score *= 0.4f;
		/* (how far it really is comes next, the likeliest first) */
		if (item_count < BOT_ITEM_CANDIDATES)
		{
			items[item_count].object_index = iterator.index;
			items[item_count].score = score;
			items[item_count].distance = distance;
			items[item_count].falloff = value >= 7.f ? 20.f : 12.f;
			/* (the most it can come to: were it as near as it looks) */
			items[item_count].most = bot_hunch(score / (1.f + distance / items[item_count].falloff) / (1.f + 0.35f * bot->item_streak),
				_bot_goal_item, &object->object.position, iterator.index);
			item_count++;
		}
	}
	/* How far each really is (round, up, over): the way there on foot, and
	   none at all when there's no way there. The way to something is a
	   search from it out to here, and a bot weighs everything lying about,
	   every second: so the ones that could score most are asked first, and
	   one that could not beat the best so far even were it as near as it
	   looks is not asked at all. What is chosen is the same. */
	while (item_count > 0)
	{
		long most = 0;
		long i;
		real distance;
		real score;

		for (i = 1; i < item_count; i++)
			if (items[i].most > items[most].most)
				most = i;
		if (items[most].most <= best.score)
			break;
		object = object_get(items[most].object_index);
		distance = items[most].distance;
		score = items[most].score;
		if (aas_ready())
		{
			struct bot_body walker;
			float time;
			memset(&walker, 0, sizeof(walker));
			walker.position = *position;
			walker.vehicle_index = NONE;
			time = bot_travel_time(&walker, AAS_FOOT, &object->object.position);
			if (time >= 0.f)
				distance = MAX(distance, time * aas_speed(AAS_FOOT));
			else
				score = 0.f;
		}
		if (score > 0.f)
		{
			/* nearer is better: an everyday weapon only close by, the big
			   things from further off */
			score /= 1.f + distance / items[most].falloff;
			/* one item after another after another: time to go somewhere else */
			score /= 1.f + 0.35f * bot->item_streak;
			bot_consider(&best, score, _bot_goal_item, &object->object.position, items[most].object_index, 20 * BOT_TICKS_PER_SECOND, TRUE);
		}
		items[most] = items[--item_count];
	}

	/* vehicles: an empty one to drive, or a seat with a teammate: a
	   person driving gets a gunner before anything else */
	if (unit->object.parent_object_index == NONE)
	{
		object_iterator_new(&iterator, _object_mask_vehicle, 0);
		while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
		{
			real distance = distance3d(position, &object->object.position);
			long driver;
			real score;
			real_point3d entry;

			if (object->object.parent_object_index != NONE || bot_ignoring(bot, iterator.index))
				continue;
			if (!bot_vehicle_entry(unit->object.parent_object_index == NONE ? player->unit_index : NONE, iterator.index, &entry))
				continue; /* full */
			if (bot_vehicle_has_enemy(iterator.index, player))
				continue; /* (one of them in it, driving or not: a goal dropped as soon as it was chosen, every tick) */
			driver = unit_scripting_unit_driver(iterator.index);
			if (driver == NONE)
			{
				/* on big maps everyone wants wheels */
				real desire = MIN(1.2f, bot->personality.vehicle_love * 0.8f + nav_vehicle_bonus);
				if (distance > 40.f + 50.f * bot->personality.vehicle_love + 60.f * nav_vehicle_bonus)
					continue;
				score = (1.f + 7.f * desire * bot_vehicle_worth(object->definition_index)) / (1.f + distance / (15.f + 25.f * nav_vehicle_bonus));
				if (role == _bot_role_driver)
					score = (10.f + 6.f * bot_vehicle_worth(object->definition_index)) / (1.f + distance / 60.f);
				else if (role == _bot_role_gunner || role == _bot_role_sniper)
					score *= 0.3f; /* not our job */
				/* a race is won on wheels */
				if (game_engine == &race_engine)
					score = (20.f + 10.f * bot->personality.vehicle_love) / (1.f + distance / 60.f);
				/* on your own a warthog is only good for running people over */
				if (!teams && !bot_driver_shoots(iterator.index) && game_engine != &race_engine)
					score *= 0.35f;
				/* someone else is on the way: in a team game ride with them */
				if (bot_goal_taken(iterator.index, me))
					score *= teams && bot_vehicle_seats(iterator.index) > 1 ? 0.9f : 0.3f;
			}
			else
			{
				struct unit_datum *driver_unit = unit_try_and_get(driver);
				struct player_datum *driver_player = driver_unit && driver_unit->unit.player_index != NONE ?
					player_try_and_get(driver_unit->unit.player_index) : NULL;
				int person = !bot_unit_is_bot(driver);
				if (!teams || !driver_player || bot_is_enemy((short)player->team_index, driver_player))
					continue;
				if (distance > (person ? 60.f : 30.f))
					continue;
				score = (person ? 30.f + 5.f * bot->personality.vehicle_love : 7.f + 4.f * bot->personality.vehicle_love) /
					(1.f + distance / 25.f);
				if (role == _bot_role_gunner)
					score *= 2.5f;
			}
			bot_consider(&best, score, _bot_goal_vehicle, &entry, iterator.index, 20 * BOT_TICKS_PER_SECOND, TRUE);
		}
	}

	/* the fight itself: armed, going to find someone is worth about as
	   much as a better weapon, more for the aggressive. Not always the
	   nearest: any enemy, the nearer the likelier, and less so where our
	   side is already heading */
	if (held >= 3.5f)
	{
		struct data_iterator others;
		struct player_datum *other;
		real total = 0.f;
		real pick;
		real_point3d chosen;
		int found = FALSE;
		data_iterator_new(&others, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&others)) != NULL)
		{
			real_point3d at;
			if (other == player || other->unit_index == NONE || !bot_unit_alive(other->unit_index) ||
				!bot_is_enemy((short)player->team_index, other))
			{
				continue;
			}
			at = object_get(object_get_ultimate_parent(other->unit_index))->object.position;
			total += 1.f / (1.f + distance3d(&at, position) / 40.f) / (1.f + bot_side_heading_near(me, player, &at, 20.f));
		}
		pick = bot_random(bot) * total;
		data_iterator_new(&others, player_data);
		while (total > 0.f && (other = (struct player_datum *)data_iterator_next(&others)) != NULL)
		{
			real_point3d at;
			if (other == player || other->unit_index == NONE || !bot_unit_alive(other->unit_index) ||
				!bot_is_enemy((short)player->team_index, other))
			{
				continue;
			}
			at = object_get(object_get_ultimate_parent(other->unit_index))->object.position;
			pick -= 1.f / (1.f + distance3d(&at, position) / 40.f) / (1.f + bot_side_heading_near(me, player, &at, 20.f));
			if (pick <= 0.f)
			{
				chosen = at;
				found = TRUE;
				break;
			}
		}
		if (found && distance3d(&chosen, position) > 8.f)
		{
			real distance = distance3d(&chosen, position);
			chosen.x += (bot_random(bot) - 0.5f) * 12.f;
			chosen.y += (bot_random(bot) - 0.5f) * 12.f;
			/* go after them when we'd have the upper hand (the aggressive
			   take worse odds) */
			real advantage = bot_advantage(player, position, &chosen) * (0.8f + 0.6f * bot->personality.aggression); /* (once: PIN is a macro) */
			advantage = PIN(advantage, 0.5f, 2.f);
			/* (few about, a duel: finding them is the whole game) */
			if (bot_player_count <= 3)
				advantage *= 1.8f;
			bot_consider(&best, (5.f + 6.f * bot->personality.aggression + (held >= 7.f ? 3.f : 0.f)) * advantage / (1.f + distance / 150.f) *
				(bot_is_team_slayer() ? 0.5f : 1.f) * /* (team slayer: the squad hunts together) */
				(objective_game ? BOT_OBJECTIVE_HUNT : 1.f),
				_bot_goal_roam, &chosen, NONE, (long)((6.f + distance / 2.f) * BOT_TICKS_PER_SECOND), FALSE);
		}
	}

	/* Who it is after for its own reasons: whoever just killed one of ours
	   beside it (teams: for a few seconds, the vengeful longer), and
	   whoever has killed it again and again this game (the more often, and
	   the more it holds a grudge, the further it will go for them). Where
	   they were last known to be, come at from an angle. */
	if (unit->object.parent_object_index == NONE && (held >= 2.f || bot->personality.aggression > 0.6f))
	{
		long quarry = NONE;
		real worth = 0.f;
		long i;
		if (bot->avenge_player != NONE && game_time_get() < bot->avenge_until)
		{
			quarry = bot->avenge_player;
			worth = 8.f + 6.f * bot->personality.vengeful;
		}
		else if (bot->personality.grudge > 0.3f)
		{
			long most = 1; /* (twice at least) */
			for (i = 0; i < BOT_MAXIMUM_PLAYERS; i++)
			{
				if (bot->memory.killed_by[i] > most)
				{
					most = bot->memory.killed_by[i];
					quarry = i;
				}
			}
			worth = 3.f + 7.f * bot->personality.grudge * MIN(most, 4) / 4.f;
		}
		if (quarry != NONE && quarry != me)
		{
			struct player_datum *them = bot_player_at_index(quarry);
			if (them && them->unit_index != NONE && bot_unit_alive(them->unit_index) && bot_is_enemy((short)player->team_index, them))
			{
				real_point3d at = object_get(object_get_ultimate_parent(them->unit_index))->object.position;
				real distance = distance3d(&at, position);
				if (distance > 8.f)
				{
					at = bot_approach_spot(bot, &at, position);
					bot_consider(&best, personal * worth / (1.f + distance / 100.f), _bot_goal_roam, &at, NONE, 6 * BOT_TICKS_PER_SECOND, FALSE);
				}
			}
		}
	}

	/* Its own places: two or three spots on the map it keeps going back to
	   (each bot its own, the same all game), and stays at a while, the
	   longer the more it will sit: where people learn to look for it. */
	if (unit->object.parent_object_index == NONE && nav_place_count() > 0)
	{
		long i;
		if (bot->memory.haunt_count == 0)
		{
			unsigned long seed = bot_hash(((unsigned long)me + 1) * 2246822519UL ^ bots_match());
			for (i = 0; i < BOT_HAUNTS * 4 && bot->memory.haunt_count < BOT_HAUNTS; i++)
			{
				real_point3d spot = nav_random_place((real)((seed = bot_hash(seed)) & 0xFFFF) / 65536.f);
				long k;
				int near = FALSE;
				for (k = 0; k < bot->memory.haunt_count; k++)
					near |= distance3d(&bot->memory.haunts[k], &spot) < 10.f;
				if (!near)
				{
					bot->memory.haunt_visited[bot->memory.haunt_count] = -100000;
					bot->memory.haunts[bot->memory.haunt_count++] = spot;
				}
			}
		}
		for (i = 0; i < bot->memory.haunt_count; i++)
		{
			real distance = distance3d(&bot->memory.haunts[i], position);
			long since = game_time_get() - bot->memory.haunt_visited[i];
			if (since < 0)
				bot->memory.haunt_visited[i] = -100000, since = 100000; /* (a new game) */
			if (distance < 3.f)
			{
				/* there: a while of it (once a visit) */
				if (since > BOT_SECONDS(45.f))
				{
					bot->memory.haunt_visited[i] = game_time_get();
					bot_consider(&best, personal * (3.f + 5.f * bot->personality.camping), _bot_goal_hold, &bot->memory.haunts[i], NONE,
						(long)((3.f + 12.f * bot->personality.camping) * personal * BOT_TICKS_PER_SECOND), FALSE);
				}
			}
			else if (since > BOT_SECONDS(45.f))
				bot_consider(&best, personal * (1.5f + 2.5f * bot->personality.patience + 2.5f * bot->personality.camping) / (1.f + distance / 80.f),
					_bot_goal_roam, &bot->memory.haunts[i], NONE, (long)((8.f + distance / 1.5f) * BOT_TICKS_PER_SECOND), FALSE);
		}
	}

	/* The herd (teams): the ones that trail their own side go where the
	   nearest of them is, with nothing better to do; the loners are left
	   to themselves. */
	if (teams && bot->personality.herd > 0.3f && unit->object.parent_object_index == NONE)
	{
		struct data_iterator others;
		struct player_datum *other;
		real nearest = 1.0e9f;
		real_point3d friend_at;
		data_iterator_new(&others, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&others)) != NULL)
		{
			real_point3d at;
			real distance;
			if (other == player || other->unit_index == NONE || !bot_unit_alive(other->unit_index) ||
				bot_is_enemy((short)player->team_index, other))
			{
				continue;
			}
			at = object_get(object_get_ultimate_parent(other->unit_index))->object.position;
			distance = distance3d(&at, position);
			if (distance > 7.f && distance < nearest)
			{
				nearest = distance;
				friend_at = at;
			}
		}
		if (nearest < 60.f)
			bot_consider(&best, personal * 3.5f * bot->personality.herd * (0.5f + bot->personality.teamwork) / (1.f + nearest / 100.f),
				_bot_goal_roam, &friend_at, NONE, 4 * BOT_TICKS_PER_SECOND, FALSE);
	}

	/* everyone for themselves: a fight we can hear is two people each busy
	   with the other, and each coming out of it hurt. There, for the kills
	   (the greedy and the aggressive most): the more hurt they are, the
	   better worth the walk. */
	if (!teams && unit->object.parent_object_index == NONE && (held >= 3.5f || bot->personality.aggression > 0.5f))
	{
		struct data_iterator others;
		struct player_datum *other;
		real best_fight = 0.f;
		real_point3d fight;
		data_iterator_new(&others, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&others)) != NULL)
		{
			struct object_datum *them;
			real distance;
			real worth;
			if (other == player || other->unit_index == NONE || !bot_unit_alive(other->unit_index))
				continue;
			if (!bot_heard(position, other->unit_index, &distance) || distance < 8.f || !bot_unit_busy(other->unit_index, position))
				continue;
			them = object_get(other->unit_index);
			worth = (1.6f - 0.5f * PIN(them->object.shield_vitality, 0.f, 1.f) - 0.5f * PIN(them->object.body_vitality, 0.f, 1.f)) /
				(1.f + distance / 40.f);
			if (worth > best_fight)
			{
				best_fight = worth;
				fight = object_get(object_get_ultimate_parent(other->unit_index))->object.position;
			}
		}
		if (best_fight > 0.f && bot_consider(&best, personal * (6.f + 5.f * bot->personality.greed + 4.f * bot->personality.aggression) * best_fight *
				(1.3f - bot->personality.sporting), /* (the sporting leave other people's fights to them) */
				_bot_goal_roam, &fight, NONE, 6 * BOT_TICKS_PER_SECOND, FALSE) && bot_logging())
			bot_log("bot%ld t%ld to the fight at %.1f %.1f (worth %.2f)", me, game_time_get(), fight.x, fight.y, best_fight);
	}

	/* explore: somewhere our side hasn't been for a while; and where we
	   were exploring toward before a fight took us off it (to be got to,
	   not forgotten) */
	if (aas_ready() && bot->explore_until > game_time_get() && bot_distance2d(position, &bot->explore_point) > 3.f)
	{
		real distance = distance3d(&bot->explore_point, position);
		bot_consider(&best, 4.f * (1.3f - 0.4f * bot->personality.aggression) / (1.f + distance / 200.f),
			_bot_goal_roam, &bot->explore_point, NONE, (long)((8.f + distance / 1.5f) * BOT_TICKS_PER_SECOND), FALSE);
		best.explore = best.point.x == bot->explore_point.x && best.point.y == bot->explore_point.y && best.kind == _bot_goal_roam;
	}
	if (aas_ready())
	{
		long sample;
		for (sample = 0; sample < 6; sample++)
		{
			real_point3d spot;
			long last;
			if (!nav_random_spot(bot_hash(bot->random + (unsigned long)sample * 7919UL + (unsigned long)game_time_get()), &spot))
				break;
			last = *nav_visit_slot(bot->team, &spot);
			real stale = PIN((real)(game_time_get() - last) / (40.f * BOT_TICKS_PER_SECOND), 0.f, 1.f);
			real distance = distance3d(&spot, position);
			if (distance < 15.f)
				continue;
			if (bot_recently_at(bot, &spot))
				stale *= 0.3f;
			{
				/* (somewhere nobody on our side has been for a good while is
				   worth a look, keen fighters or not: that's where the
				   enemy, the items and the flanks are that we don't know of) */
				real score = (1.f + 6.f * stale * stale) * (1.15f - 0.25f * bot->personality.aggression) /
					(1.f + distance / 200.f) / (1.f + bot_side_heading_near(me, player, &spot, 20.f));
				if (bot_consider(&best, score, _bot_goal_roam, &spot, NONE, (long)((8.f + distance / 1.5f) * BOT_TICKS_PER_SECOND), FALSE))
					best.explore = TRUE;
			}
		}
	}

	/* outgunned where we are: back to our side */
	if (teams)
	{
		real ours;
		real theirs;
		bot_forces_near(player, position, 20.f, &ours, &theirs);
		if (theirs > ours * 1.3f)
		{
			struct data_iterator others;
			struct player_datum *other;
			real nearest = 1.0e9f;
			real_point3d friend_at;
			data_iterator_new(&others, player_data);
			while ((other = (struct player_datum *)data_iterator_next(&others)) != NULL)
			{
				real_point3d at;
				real distance;
				if (other == player || other->unit_index == NONE || !bot_unit_alive(other->unit_index) ||
					bot_is_enemy((short)player->team_index, other))
				{
					continue;
				}
				at = object_get(object_get_ultimate_parent(other->unit_index))->object.position;
				distance = distance3d(&at, position);
				if (distance > 8.f && distance < nearest)
				{
					nearest = distance;
					friend_at = at;
				}
			}
			if (nearest < 80.f)
				bot_consider(&best, (6.f - 3.f * bot->personality.aggression) * (0.5f + bot->personality.teamwork) *
					(1.f + 0.6f * bot->personality.herd) / (1.f + nearest / 100.f), /* (the loners less, the followers more) */
					_bot_goal_roam, &friend_at, NONE, 4 * BOT_TICKS_PER_SECOND, FALSE);
		}
	}

	/* a teammate in trouble close by (shields down, enemies on them): go
	   and help, the team-minded more readily */
	if (teams && bot->personality.teamwork > 0.2f)
	{
		struct data_iterator others;
		struct player_datum *other;
		data_iterator_new(&others, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&others)) != NULL)
		{
			struct object_datum *mate;
			real_point3d at;
			real distance;
			real ours;
			real theirs;
			if (other == player || other->unit_index == NONE || !bot_unit_alive(other->unit_index) ||
				bot_is_enemy((short)player->team_index, other))
			{
				continue;
			}
			mate = object_get(other->unit_index);
			if (mate->object.shield_vitality > 0.6f)
				continue;
			at = object_get(object_get_ultimate_parent(other->unit_index))->object.position;
			distance = distance3d(&at, position);
			if (distance < 6.f || distance > 20.f + 40.f * bot->personality.teamwork)
				continue;
			bot_forces_near(player, &at, 15.f, &ours, &theirs);
			if (theirs <= 0.f)
				continue;
			bot_consider(&best, (2.f + 9.f * bot->personality.teamwork) * (held >= 3.5f ? 1.f : 0.4f) / (1.f + distance / 60.f),
				_bot_goal_roam, &at, NONE, 4 * BOT_TICKS_PER_SECOND, FALSE);
		}
	}

	/* team slayer: with the team */
	if (bot_is_team_slayer() && unit->object.parent_object_index == NONE)
		bot_squad_candidates(bot, me, player, position, &best);

	/* the game's objective: flags, the hill, the ball */
	if (teams)
		bot_ctf_candidates(bot, player, unit, position, held, &best);
	bot_game_candidates(bot, player, unit, position, held, &best);

	/* with the squad: where the squad is now */
	if (bot->goal_squad && best.squad && best.kind == bot->goal_kind)
	{
		bot->goal = best.point;
		bot_goal_snap(bot);
		bot->goal_score = best.score;
		bot->goal_ticks = best.ticks;
		return;
	}
	/* on the way somewhere (exploring, going round): not off somewhere
	   else of the same sort every time the dice come up differently (a new
	   random place each second is a bot that wanders about the middle and
	   never gets anywhere); only for something else altogether */
	if (bot->goal_kind == _bot_goal_roam && !bot->goal_squad && !bot->goal_objective && bot->goal_ticks > 0 && best.kind == _bot_goal_roam &&
		!best.squad && !best.objective &&
		best.object == NONE && bot_distance2d(position, &bot->goal) > 2.f)
	{
		return;
	}
	/* keep at what we're doing unless the best is clearly better */
	if (bot->goal_kind != _bot_goal_none && bot->goal_ticks > 0 && bot->goal_score > 0.f &&
		best.score < bot->goal_score * 1.3f && !(best.kind == bot->goal_kind && best.object == bot->goal_object && best.object != NONE))
	{
		/* the same moving goal (a carrier to chase, one to escort) follows
		   them along */
		if (best.kind == bot->goal_kind && best.object == NONE && bot->goal_object == NONE && best.objective && bot->goal_objective &&
			best.score > bot->goal_score * 0.7f && distance3d(&best.point, &bot->goal) > 3.f && distance3d(&best.point, &bot->goal) < 30.f)
		{
			bot->goal = best.point;
			bot_goal_snap(bot);
			bot->goal_score = best.score;
		}
		return;
	}
	if (best.kind != _bot_goal_none && best.score > 0.25f)
	{
		if (bot_logging() && (best.kind != bot->goal_kind || best.object != bot->goal_object))
			bot_log("bot%ld goal %ld %s score %.2f d %.1f (was %ld %lx ticks %ld score %.2f)", me, best.kind,
				best.object != NONE ? tag_get_name(object_get(best.object)->definition_index) : "-", best.score, distance3d(position, &best.point),
				bot->goal_kind, (unsigned long)bot->goal_object, bot->goal_ticks, bot->goal_score);
		if (best.kind == _bot_goal_item && best.object != bot->goal_object)
			bot->item_streak++;
		else if (best.kind != _bot_goal_item)
			bot->item_streak = 0;
		bot_set_goal(bot, best.kind, &best.point, best.object, best.ticks);
		bot->goal_score = best.score;
		bot->goal_objective = best.objective;
		bot->goal_squad = best.squad;
		if (best.explore)
		{
			bot->explore_point = best.point;
			bot->explore_until = game_time_get() + best.ticks;
		}
		return;
	}
	bot->goal_objective = FALSE;
	bot->goal_score = 0.f;
	/* nothing to get: keep roaming where we were going */
	if ((bot->goal_kind == _bot_goal_roam || bot->goal_kind == _bot_goal_hold) && bot->goal_ticks > 0 &&
		bot_distance2d(position, &bot->goal) > 1.5f)
	{
		return;
	}

	/* a sniper with a rifle: up to a high place that sees a lot, and stay */
	if (role == _bot_role_sniper && held >= 7.f && bots_vantage_count > 0)
	{
		real_point3d spot = bots_vantages[(bot->random >> 4) % bots_vantage_count];
		if (bot_within_reach(&spot))
		{
			if (bot_distance2d(position, &spot) > 2.f)
				bot_set_goal(bot, _bot_goal_roam, &spot, NONE, 30 * BOT_TICKS_PER_SECOND);
			else
				bot_set_goal(bot, _bot_goal_hold, &spot, NONE, 15 * BOT_TICKS_PER_SECOND);
			return;
		}
	}
	/* a good gun and the patience for it: up to high ground that sees a
	   lot (over a gap, up a ladder if that's the way) */
	if (held >= 9.f && bot_holds_named(unit, "sniper") && bots_vantage_count > 0 && bot_random(bot) < 0.2f + 0.5f * bot->personality.patience)
	{
		/* one of the vantages near enough: not always the nearest, and not
		   the one it's just been at if there's another */
		long i;
		long nearest = 0;
		real nearest_score = 1.0e9f;
		for (i = 0; i < bots_vantage_count; i++)
		{
			real score = distance3d(position, &bots_vantages[i]);
			if (score >= 60.f || !bot_within_reach(&bots_vantages[i]))
				continue;
			score *= 0.6f + 0.8f * bot_random(bot);
			if (bot_recently_at(bot, &bots_vantages[i]) && bot_distance2d(position, &bots_vantages[i]) > 2.f)
				score += 40.f;
			if (score < nearest_score)
			{
				nearest_score = score;
				nearest = i;
			}
		}
		if (nearest_score < 1.0e9f)
		{
			if (bot_distance2d(position, &bots_vantages[nearest]) > 2.f)
				bot_set_goal(bot, _bot_goal_roam, &bots_vantages[nearest], NONE, 25 * BOT_TICKS_PER_SECOND);
			else
				bot_set_goal(bot, _bot_goal_hold, &bots_vantages[nearest], NONE,
					(long)((6.f + 8.f * bot->personality.patience) * (0.5f + bot->personality.camping) * BOT_TICKS_PER_SECOND));
			return;
		}
	}
	/* patient bots with precision weapons hold a spot for a while */
	if (bot->personality.patience > 0.6f && held >= 7.f && bot_random(bot) < 0.5f)
	{
		bot_set_goal(bot, _bot_goal_hold, position, NONE,
			(long)((8.f + 12.f * bot_random(bot)) * (0.5f + bot->personality.camping) * BOT_TICKS_PER_SECOND));
		return;
	}

	/* hunt: armed and with nothing better to do, go and find someone (bots
	   know roughly where people are, as players learn a map's hot spots) */
	if (held >= 3.5f || bot->personality.aggression > 0.7f)
	{
		real_point3d point;
		real distance;
		/* where our side knows one is (seen, on a tracker, heard lately);
		   else roughly where someone is */
		int known = bot_side_nearest_known(bot_side_of(player), position, 8.f, &point);
		if (known)
			distance = distance3d(&point, position);
		if ((known || bot_nearest_enemy(player, position, &point, &distance)) && distance > 8.f)
		{
			/* not straight at them: somewhere near, so they're met from an
			   angle, and from above when there's high ground there */
			point = bot_approach_spot(bot, &point, position);
			if (bot_within_reach(&point))
			{
				bot_set_goal(bot, _bot_goal_roam, &point, NONE, 6 * BOT_TICKS_PER_SECOND);
				return;
			}
		}
	}

	/* roam: anywhere on the map (or a spawn point) */
	{
		struct scenario *scenario = global_scenario_get();
		long attempt;
		for (attempt = 0; attempt < 8; attempt++)
		{
			real_point3d point;
			/* anywhere on the map, somewhere we haven't been lately first
			   (not the spawns and weapon spots everyone goes to) */
			if (attempt < 5 && nav_random_spot(bot_hash(bot->random + (unsigned long)attempt * 104729UL + (unsigned long)game_time_get()), &point))
			{
				if (bot_recently_at(bot, &point) && attempt < 4)
					continue;
			}
			else if (nav_place_count() > 0 && bot_random(bot) < 0.5f)
				point = nav_random_place(bot_random(bot));
			else if (scenario->players.count > 0)
			{
				struct player_starting_location *location =
					player_get_starting_location((short)((long)(bot_random(bot) * scenario->players.count) % scenario->players.count));
				if (!location)
					continue;
				point = location->position;
			}
			else
				continue;
			if (distance3d(&point, position) > 10.f && bot_within_reach(&point))
			{
				bot_set_goal(bot, _bot_goal_roam, &point, NONE, 30 * BOT_TICKS_PER_SECOND);
				return;
			}
		}
		bot_set_goal(bot, _bot_goal_roam, position, NONE, 3 * BOT_TICKS_PER_SECOND);
	}
}

/* ---------- movement */

/* next point to head for on the way to the goal */
/* room for a body (or a vehicle, width) all the way from a to b: rays at
   knee and chest height down the middle and along both sides */
static int bot_body_clear(real_point3d const *a, real_point3d const *b, real width, real height, long ignore)
{
	real dx = b->x - a->x;
	real dy = b->y - a->y;
	real length = (real)sqrt(dx * dx + dy * dy);
	real side_x = length > 0.01f ? -dy / length * width : 0.f;
	real side_y = length > 0.01f ? dx / length * width : 0.f;
	static real const heights[] = { 0.3f, 0.65f };
	long h;

	for (h = 0; h < 2; h++)
	{
		long side;
		for (side = -1; side <= 1; side++)
		{
			real_point3d from = *a;
			real_point3d to = *b;
			if (h == 0 && side != 0)
				continue;
			from.x += side_x * side;
			from.y += side_y * side;
			to.x += side_x * side;
			to.y += side_y * side;
			from.z += heights[h] * height;
			to.z += heights[h] * height;
			if (!bot_line_clear(bot_move_flags, &from, &to, ignore))
				return FALSE;
		}
	}
	return TRUE;
}

/* walk toward yaw around walls, not over edges; jump small obstacles.
   The looks ahead follow the ground: up a ramp the ground itself isn't a
   wall, and a slope isn't a ledge to jump */
static int bot_steer(long unit_index, real_point3d const *position, real wanted_yaw, real max_drop, real *yaw_out, int *jump)
{
	static real const offsets[] = { 0.f, 0.35f, -0.35f, 0.7f, -0.7f, 1.1f, -1.1f, 1.6f, -1.6f, 2.2f, -2.2f };
	unsigned long const flags = bot_move_flags | FLAG(_collision_test_objects_vehicles_bit);
	long i;

	*jump = FALSE;
	for (i = 0; i < (long)(sizeof(offsets) / sizeof(offsets[0])); i++)
	{
		real yaw = bot_wrap(wanted_yaw + offsets[i]);
		real c = (real)cos(yaw);
		real sn = (real)sin(yaw);
		real_point3d ahead;
		real_point3d near_point;
		real_point3d waist;
		real ground;
		real near_ground;

		/* ground 1.2 ahead: not a drop (more than a step or two down), not a wall */
		ahead.x = position->x + c * 1.2f;
		ahead.y = position->y + sn * 1.2f;
		ahead.z = position->z + 1.1f;
		if (!bot_ground(&ahead, 1.2f + max_drop, unit_index, &ground) || ground - position->z > 1.f)
			continue;
		/* a body's room at waist height, along the ground */
		waist = *position;
		waist.z += 0.4f;
		ahead.z = MAX(ground, position->z) + 0.4f;
		if (!bot_line_clear(flags, &waist, &ahead, unit_index))
			continue;
		/* just ahead, a step up at once (not a ramp): jump it */
		near_point.x = position->x + c * 0.5f;
		near_point.y = position->y + sn * 0.5f;
		near_point.z = position->z + 1.1f;
		if (bot_ground(&near_point, 1.6f, unit_index, &near_ground) && near_ground - position->z > 0.35f)
			*jump = TRUE;
		*yaw_out = yaw;
		return TRUE;
	}
	*yaw_out = wanted_yaw;
	*jump = TRUE;
	return FALSE;
}

/* learning as the game goes: a bot that keeps dying gets warier and keeps
   its distance, one that keeps winning its fights pushes harder; each
   stays within its own nature (a timid bot never turns reckless) */
#define BOT_LEARNED_LIMIT 0.25f

static void bot_learn(struct bot_personality *personality, real daring, real teamwork, real aggression)
{
	real before;
	personality->learned_aggression = PIN(personality->learned_aggression + aggression, 0.f, 1.f);
	/* its run, too: kills going to its head (as far as it has the swagger
	   for), deaths making it reckless or timid, as its temper is */
	personality->mood = personality->swagger * 0.07f * MIN(personality->kill_streak, 4) +
		personality->temper * 0.09f * MIN(personality->death_streak, 4);
	personality->aggression = PIN(bot_skill_aggression() + personality->learned_aggression + personality->mood, 0.f, 1.f);
	before = personality->learned_daring;
	personality->learned_daring = PIN(before + daring, -BOT_LEARNED_LIMIT, BOT_LEARNED_LIMIT);
	personality->daring = PIN(personality->daring + (personality->learned_daring - before), 0.f, 1.f);
	before = personality->learned_teamwork;
	personality->learned_teamwork = PIN(before + teamwork, -BOT_LEARNED_LIMIT, BOT_LEARNED_LIMIT);
	personality->teamwork = PIN(personality->teamwork + (personality->learned_teamwork - before), 0.f, 1.f);
}

static void bot_learn_from_score(struct bot_state *bot, struct player_datum const *me)
{
	struct bot_personality *personality = &bot->personality;
	long kills = me->statistics.kills[0];
	long deaths = me->statistics.deaths;
	if (kills < personality->kills_seen || deaths < personality->deaths_seen)
	{
		/* a new game: start over */
		personality->kills_seen = kills;
		personality->deaths_seen = deaths;
		personality->learned_aggression = 0.f;
		personality->learned_time = game_time_get();
		personality->aggression = bot_skill_aggression();
		personality->kill_streak = 0;
		personality->death_streak = 0;
		personality->mood = 0.f;
		memset(&bot->memory, 0, sizeof(bot->memory));
		return;
	}
	while (personality->kills_seen < kills)
	{
		personality->kills_seen++;
		personality->kill_streak++;
		personality->death_streak = 0;
		if (BOT_ON(BOT_F_TEABAG) && bot->last_fired_unit != NONE && game_time_get() - bot->last_fired_time < 3 * BOT_TICKS_PER_SECOND)
			bot->teabag_pending = TRUE;
		/* showing off: a turn right round, and a hop (the more of a run it
		   is on, the likelier) */
		if (bot_random(bot) < bot->personality.showboat * 0.45f * (1.f + 0.3f * MIN(personality->kill_streak, 3)))
			bot->spin_ticks = 24;
		/* winning: hungrier, bolder, and happier going it alone */
		bot_learn(personality, 0.02f, -0.01f, 0.04f);
	}
	while (personality->deaths_seen < deaths)
	{
		personality->deaths_seen++;
		personality->death_streak++;
		personality->kill_streak = 0;
		/* who did it: remembered (and the more often, the more it rankles) */
		{
			long index = (long)(bot - bot_states);
			long killer = index >= 0 && index < BOT_MAXIMUM_PLAYERS && game_time_get() - bots_killed_time[index] < BOT_TICKS_PER_SECOND ?
				bots_killer[index] : NONE;
			if (killer >= 0 && killer < BOT_MAXIMUM_PLAYERS && bot->memory.killed_by[killer] < 255)
			{
				bot->memory.killed_by[killer]++;
				if (bot_logging() && bot->memory.killed_by[killer] >= 2 && personality->grudge > 0.4f)
					bot_log("bot%ld t%ld has it in for player %ld (%d times now)", index, game_time_get(), killer, (int)bot->memory.killed_by[killer]);
			}
		}
		if (bot->on_objective_route)
			personality->route_died = TRUE; /* another way next time */
		/* losing: a little warier, and safer in numbers */
		bot_learn(personality, -0.02f, 0.03f, -0.01f);
	}
	/* and as the game goes on, everyone a bit keener: every half minute */
	if (game_time_get() < personality->learned_time)
		personality->learned_time = game_time_get(); /* (a new game) */
	while (game_time_get() - personality->learned_time >= 30 * BOT_TICKS_PER_SECOND)
	{
		personality->learned_time += 30 * BOT_TICKS_PER_SECOND;
		bot_learn(personality, 0.f, 0.f, 0.02f);
	}
}

/* is point worth a risky jump (a gap, a long drop) for: a power weapon or
   powerup, a vantage, high ground in a fight, the game's objective */
static int bot_point_worth_risk(struct bot_state const *bot, real_point3d const *point)
{
	long i;
	if (bot->fight_spot_valid && distance3d(point, &bot->fight_spot) < 0.5f)
		return TRUE;
	if (distance3d(point, &bot->goal) < 0.5f)
	{
		struct object_datum *object = bot->goal_object != NONE ? object_try_and_get(bot->goal_object) : NULL;
		if (bot->goal_kind == _bot_goal_item && object && bot_is_power_item(object->definition_index))
			return TRUE;
		if (bot->goal_objective && bot->goal_score >= 20.f)
			return TRUE;
	}
	for (i = 0; i < bots_vantage_count; i++)
		if (bot_distance2d(point, &bots_vantages[i]) < 2.f && fabs(point->z - bots_vantages[i].z) < 1.5f)
			return TRUE;
	return FALSE;
}

/* where the enemy we lost sight of will be, seconds after they went out of
   sight plus ahead: on along the way they were going (no further than they
   could go over the floor: not through walls, not off into the air). The
   bounding sphere's centre, like last_seen */
static real_point3d bot_predict_enemy(struct bot_state const *bot, real ahead)
{
	real seconds = bot->lost_ticks / (real)BOT_TICKS_PER_SECOND + ahead;
	real_point3d feet = bot->last_seen_feet;
	real_point3d result = bot->last_seen;
	real dx = bot->last_seen_velocity.i * BOT_TICKS_PER_SECOND * seconds;
	real dy = bot->last_seen_velocity.j * BOT_TICKS_PER_SECOND * seconds;
	real length = (real)sqrt(dx * dx + dy * dy);
	long tries;
	if (length < 0.3f)
		return result;
	if (length > 10.f)
	{
		dx *= 10.f / length;
		dy *= 10.f / length;
	}
	for (tries = 0; tries < 4; tries++)
	{
		real_point3d to = feet;
		to.x += dx;
		to.y += dy;
		if (!aas_ready() || aas_line_in_areas(AAS_FOOT, &feet, &to))
		{
			result.x += dx;
			result.y += dy;
			return result;
		}
		dx *= 0.5f;
		dy *= 0.5f;
	}
	return result;
}

/* the nearest health pack we can get to, within reach (NONE) */
static long bot_find_health(real_point3d const *position, real reach)
{
	struct object_iterator iterator;
	struct object_datum *object;
	long best = NONE;
	real best_distance = reach;
	object_iterator_new(&iterator, _object_mask_equipment, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		real distance;
		if (!bot_item_lying(object) || !bot_name_has(object->definition_index, "health"))
			continue;
		distance = distance3d(position, &object->object.position);
		if (distance >= best_distance)
			continue;
		if (aas_ready())
		{
			struct bot_body walker;
			float time;
			memset(&walker, 0, sizeof(walker));
			walker.position = *position;
			walker.vehicle_index = NONE;
			time = bot_travel_time(&walker, AAS_FOOT, &object->object.position);
			if (time < 0.f)
				continue;
			distance = MAX(distance, time * aas_speed(AAS_FOOT));
			if (distance >= best_distance)
				continue;
		}
		best_distance = distance;
		best = iterator.index;
	}
	return best;
}

/* a trip's own way there: somewhere off to one side of the way straight
   there, when that's not much longer. Bots don't all go the same way, and
   the map gets used.
   - an ordinary trip: now and then (the longer, the likelier), and not
     much longer than the straight way;
   - the objective (their flag, the ball, home with the flag): most times,
     and a real way round is worth it: another way into their base than
     last time (and never the way it died last time), and not the way a
     teammate's already taking, so the attack comes from more than one
     side and the defence can't sit on the one door;
   - on the way to an enemy, before the fight: a flank, often (more for
     the skilled, the patient and the team players), coming in out of
     their sight and from the side or behind; dropped once the fight's
     on */
static int bot_goal_is_power_weapon(struct bot_state const *bot);
static int bot_goal_is_powerup(struct bot_state const *bot);

/* on the game's own business just now: the flag or the ball in hand, or on
   the way to one, to the hill, after a carrier (not just any item, which
   also counts as an 'objective' goal: something to go straight to) */
static int bot_on_objective(struct bot_state const *bot)
{
	return bot->carrying_objective || (bot->goal_objective && bot->goal_ticks > 0 && bot->goal_kind != _bot_goal_vehicle &&
		bot->goal_kind != _bot_goal_none &&
		(bot->goal_kind == _bot_goal_item ? bot_goal_is_game_object(bot->goal_object) : bot->goal_score >= 12.f && bot_game_has_objective()));
}

/* whether to look for one, and what for: the tries follow, one a tick */
static void bot_plan_via(struct bot_state *bot, struct bot_body const *body, long player_index)
{
	real_point3d const *from = &body->position;
	real distance = distance3d(from, &bot->goal);
	/* (the game's objective: a flag or a ball to get, one in hand, the
	   hill; not just any item, which also counts as an 'objective' goal) */
	int objective = bot->goal_objective && bot->goal_kind != _bot_goal_vehicle &&
		(bot->carrying_objective || (bot->goal_kind == _bot_goal_item ? bot_goal_is_game_object(bot->goal_object) : bot->goal_score >= 20.f));
	int carrying = bot->carrying_objective;
	int flank = bot->goal_kind == _bot_goal_enemy;
	real chance;
	real slack;
	float direct;
	long quarry = NONE; /* flanking: who we're after (their facing, their sight) */
	bot->via_pending = FALSE;
	bot->via_valid = FALSE;
	bot->via_side = 0;
	bot->via_plan.samples = 0;
	if (bot_goal_is_power_weapon(bot) || bot_goal_is_powerup(bot))
		return; /* (straight there, before someone else gets it) */
	if (!aas_ready() || distance < (objective ? 14.f : flank ? 15.f : 18.f))
		return;
	if (carrying)
		chance = 0.15f + 0.2f * (1.f - bot->personality.daring), slack = 1.12f; /* (home quick: the danger map already steers round them) */
	else if (objective)
		chance = 0.45f + 0.35f * bot->personality.focus, slack = 1.3f;
	else if (flank)
		/* on the way to someone, before the fight: often, the more skilled
		   and the more of a team player the more; a real way round is worth
		   it (the fight starts on our terms) */
		chance = (0.35f + 0.25f * bot->personality.teamwork + 0.15f * bot->personality.patience) * (0.6f + 0.4f * bot_skill_fraction()), slack = 1.3f;
	else if (BOT_ON(BOT_F_VIA))
		chance = 0.2f + MIN(0.3f, distance / 150.f), slack = 1.2f;
	else
		chance = 0.35f + MIN(0.4f, distance / 150.f), slack = 1.3f;
	if (bot->personality.route_died && objective)
		chance = MAX(chance, 0.9f); /* (not that way again) */
	if (bot_random(bot) > chance)
		return;
	direct = bot_travel_time(body, AAS_FOOT, &bot->goal);
	if (direct <= 0.f)
		return;
	if (flank)
	{
		/* the enemy nearest where we're headed */
		struct data_iterator iterator;
		struct player_datum *player;
		struct player_datum *mine = player_index >= 0 && player_index < player_data->maximum_count ?
			(struct player_datum *)((byte *)xbox_pointer(player_data->data) + player_index * player_data->size) : NULL;
		real nearest = 12.f;
		data_iterator_new(&iterator, player_data);
		while (mine && (player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			real d;
			if (player->unit_index == NONE || !bot_unit_alive(player->unit_index) || !bot_is_enemy((short)mine->team_index, player))
				continue;
			d = distance3d(&object_get(player->unit_index)->object.position, &bot->goal);
			if (d < nearest)
			{
				nearest = d;
				quarry = player->unit_index;
			}
		}
	}
	bot->via_plan.samples = objective ? 8 : 4;
	bot->via_plan.objective = objective;
	bot->via_plan.carrying = carrying;
	bot->via_plan.flank = flank;
	bot->via_plan.slack = slack;
	bot->via_plan.direct = direct;
	bot->via_plan.quarry = quarry;
	bot->via_plan.best_score = 1.0e9f;
	bot->via_plan.best_side = 0;
	bot->via_plan.best_first = 0.f;
}

/* the best of the places tried is the way round (if any did) */
static void bot_plan_via_finish(struct bot_state *bot, long player_index)
{
	bot->via_plan.samples = 0;
	if (bot->via_plan.best_score >= 1.0e9f)
		return;
	bot->via_point = bot->via_plan.best_via;
	bot->via_valid = TRUE;
	bot->via_side = bot->via_plan.best_side;
	bot->via_ticks = (long)((bot->via_plan.best_first + 4.f) * BOT_TICKS_PER_SECOND);
	if (bot->via_plan.flank)
	{
		bot->flank_side = bot->via_plan.best_side;
		bot->flank_time = game_time_get();
	}
	if (bot->via_plan.objective)
	{
		bot->personality.route_side = bot->via_plan.best_side;
		bot->personality.route_died = FALSE;
		bot->on_objective_route = TRUE;
	}
	if (bot_logging())
		bot_log("bot%ld t%ld via %s side %ld (%.1f %.1f) for goal (%.1f %.1f)", player_index, game_time_get(),
			bot->via_plan.carrying ? "carrying" : bot->via_plan.objective ? "objective" : bot->via_plan.flank ? "flank" : "trip",
			bot->via_plan.best_side, bot->via_point.x, bot->via_point.y, bot->goal.x, bot->goal.y);
}

/* one place tried: somewhere off to a side of the straight way, the way
   there and on from it against the straight way's */
static void bot_plan_via_step(struct bot_state *bot, struct bot_body const *body, long player_index)
{
	real_point3d const *from = &body->position;
	real distance = distance3d(from, &bot->goal);
	int objective = bot->via_plan.objective;
	int flank = bot->via_plan.flank;
	float direct = bot->via_plan.direct;
	long quarry = bot->via_plan.quarry;
	real_point3d quarry_eye;
	real along = 0.3f + 0.4f * bot_random(bot);
	long side = bot_random(bot) < 0.5f ? -1 : 1;
	real across = (real)side * (0.2f + 0.35f * bot_random(bot)) * distance;
	real dx = bot->goal.x - from->x;
	real dy = bot->goal.y - from->y;
	real length = (real)sqrt(dx * dx + dy * dy);
	real_point3d spot;
	real_point3d via;
	float first;
	float second;
	real score;
	long i;
	struct bot_body there;

	/* (nearly there by now: straight on) */
	if (!aas_ready() || length < 1.f || distance < 8.f)
	{
		bot->via_plan.samples = 0;
		return;
	}
	if (--bot->via_plan.samples <= 0)
		bot->via_plan.samples = -1; /* (the last: the best of them after it) */
	if (quarry != NONE && (!bot_unit_alive(quarry) || !object_try_and_get(quarry)))
		quarry = bot->via_plan.quarry = NONE;
	if (quarry != NONE)
		quarry_eye = object_get(quarry)->object.bounding_sphere_center;
	else
	{
		quarry_eye = bot->goal;
		quarry_eye.z += 0.6f;
	}
	spot.x = from->x + dx * along - dy / length * across;
	spot.y = from->y + dy * along + dx / length * across;
	spot.z = from->z + (bot->goal.z - from->z) * along;
	if (aas_nearest_area(AAS_FOOT, &spot, 8.f, &via) == NONE)
	{
		spot.z += 3.f;
		if (aas_nearest_area(AAS_FOOT, &spot, 8.f, &via) == NONE)
			goto tried;
	}
	first = bot_travel_time(body, AAS_FOOT, &via);
	there = *body;
	there.position = via;
	second = bot_travel_time(&there, AAS_FOOT, &bot->goal);
	if (first <= 0.f || second <= 0.f || first + second > direct * bot->via_plan.slack + 2.f)
		goto tried;
	if (!objective && !flank)
	{
		/* an ordinary trip: the first that will do */
		bot->via_plan.best_via = via;
		bot->via_plan.best_first = first;
		bot->via_plan.best_side = side;
		bot->via_plan.best_score = 0.f;
		bot_plan_via_finish(bot, player_index);
		return;
	}
	/* the shorter the better; then a change from last time, never the
	   way it died, and not where teammates are already going */
	score = (first + second) / direct + 0.3f * bot_random(bot) * bot->personality.whim;
	if (flank)
	{
		/* come at them from where they can't see us coming: out of
		   their sight on the way in, and from the side or behind rather
		   than where they're looking */
		real_point3d raised = via;
		raised.z += 0.6f;
		/* (a way round they can see all of is no flank: straight in) */
		if (bot_line_clear(bot_sight_flags, &quarry_eye, &raised, quarry != NONE ? quarry : NONE))
			goto tried;
		if (quarry != NONE)
		{
			struct unit_datum *enemy = unit_try_and_get(quarry);
			real vx = via.x - quarry_eye.x, vy = via.y - quarry_eye.y;
			real vl = (real)sqrt(vx * vx + vy * vy);
			if (enemy && vl > 1.f)
			{
				real facing = (enemy->unit.aiming_vector.i * vx + enemy->unit.aiming_vector.j * vy) / vl; /* 1: right in front */
				score -= 0.25f * (1.f - facing);
			}
		}
		if (side == bot->flank_side && game_time_get() - bot->flank_time < 6 * BOT_TICKS_PER_SECOND)
			score -= 0.6f;
	}
	if (objective && side == bot->personality.route_side)
		score += bot->personality.route_died ? 1.5f : 0.25f;
	for (i = 0; i < BOT_MAXIMUM_PLAYERS; i++)
	{
		struct bot_state const *mate = &bot_states[i];
		if (i == player_index || mate->unit_index == NONE || mate->team != bot->team || !mate->via_valid ||
			distance3d(&mate->goal, &bot->goal) > 15.f)
			continue;
		if (distance3d(&mate->via_point, &via) < 12.f || (objective && mate->via_side == side))
			score += 0.35f;
	}
	if (score < bot->via_plan.best_score)
	{
		bot->via_plan.best_score = score;
		bot->via_plan.best_via = via;
		bot->via_plan.best_first = first;
		bot->via_plan.best_side = side;
	}
tried:
	if (bot->via_plan.samples < 0)
		bot_plan_via_finish(bot, player_index);
}

/* a tick's movement toward point over the areas (at most once a tick) */
static void bot_go(struct bot_state *bot, struct bot_body const *body, real_point3d const *point, long kind, real radius,
	struct bot_move_result *result)
{
	struct bot_goal goal;
	goal.kind = kind;
	goal.origin = *point;
	goal.radius = radius;
	goal.area = NONE;
	bot->mover.yaw = bot->yaw;
	/* the careful go round gaps and long drops (another way there, even a
	   longer one); the daring jump them, and everyone does when it's the
	   only way, or for something worth it */
	bot->mover.move.careful = bot->personality.daring < 0.4f && !bot_point_worth_risk(bot, point);
	bot->mover.move.human = BOT_ON(BOT_F_HUMAN);
	bot_move_to_goal(&bot->mover, body, &goal, result);
}

/* cover: somewhere a few seconds away that the enemy at `enemy` can't see
   (a wall, a rock, round a corner), nearer the better, not toward them.
   TRUE with its spot */
static int bot_find_cover(struct bot_state *bot, struct bot_body const *body, real_point3d const *enemy, long ignore,
	real_point3d *spot)
{
	real best = 1.0e9f;
	real away = bot_yaw_to(enemy, &body->position);
	long i;
	int found = FALSE;
	if (!aas_ready())
		return FALSE;
	for (i = 0; i < 14; i++)
	{
		/* round the far side of us from them first, then all about */
		real yaw = i < 6 ? away + (bot_random(bot) - 0.5f) * 2.4f : bot_random(bot) * BOT_TWO_PI;
		real reach = 2.5f + 7.f * bot_random(bot);
		real_point3d probe;
		real_point3d snapped;
		real_point3d eye;
		real_point3d their_eye = *enemy;
		float time;
		probe.x = body->position.x + (real)cos(yaw) * reach;
		probe.y = body->position.y + (real)sin(yaw) * reach;
		probe.z = body->position.z + 0.3f;
		if (aas_nearest_area(AAS_FOOT, &probe, 3.f, &snapped) == NONE)
			continue;
		/* hidden standing up: their eye to where our head would be */
		eye = snapped;
		eye.z += 0.62f;
		their_eye.z += 0.1f;
		if (bot_line_clear(bot_sight_flags, &their_eye, &eye, ignore))
			continue;
		/* ...and a bit either side of it (not a sliver of shade) */
		{
			real_point3d side = eye;
			real across = (real)atan2(eye.y - enemy->y, eye.x - enemy->x) + BOT_PI * 0.5f;
			side.x += (real)cos(across) * 0.35f;
			side.y += (real)sin(across) * 0.35f;
			if (bot_line_clear(bot_sight_flags, &their_eye, &side, ignore))
				continue;
			side.x -= (real)cos(across) * 0.7f;
			side.y -= (real)sin(across) * 0.7f;
			if (bot_line_clear(bot_sight_flags, &their_eye, &side, ignore))
				continue;
		}
		time = bot_travel_time(body, AAS_FOOT, &snapped);
		if (time < 0.f || time > 3.5f)
			continue;
		/* not closer to them than we are */
		if (distance3d(&snapped, enemy) < distance3d(&body->position, enemy) - 1.f)
			continue;
		if (time < best)
		{
			best = time;
			*spot = snapped;
			found = TRUE;
		}
	}
	return found;
}

/* higher ground close by that still sees them: a few reachabilities away
   (up a ledge, a ladder; over a gap for the daring). TRUE with its spot */
static int bot_higher_ground(struct bot_state *bot, real_point3d const *position, real_point3d const *enemy, long ignore, real_point3d *spot)
{
	long queue[48];
	long depth[48];
	long head = 0;
	long tail = 0;
	long here;
	long rays = 0;
	real best_height = position->z + 0.6f;
	int found = FALSE;
	if (!aas_ready() || (here = aas_point_area(AAS_FOOT, position)) == NONE)
		return FALSE;
	queue[tail] = here;
	depth[tail++] = 0;
	while (head < tail)
	{
		long a = queue[head];
		long d = depth[head++];
		struct aas_area const *area = aas_area(AAS_FOOT, a);
		long r;
		if (d > 0 && area->center.z > best_height && rays < 10)
		{
			real_point3d eye = area->center;
			eye.z += 0.6f;
			rays++;
			if (bot_line_clear(bot_sight_flags, &eye, enemy, ignore))
			{
				best_height = area->center.z;
				*spot = area->center;
				found = TRUE;
			}
		}
		if (d >= 3)
			continue;
		for (r = area->first_reach; r < area->first_reach + area->reach_count && tail < 48; r++)
		{
			struct aas_reach const *reach = aas_reach(AAS_FOOT, r);
			struct aas_area const *there = aas_area(AAS_FOOT, reach->to);
			long k;
			int seen = FALSE;
			if (reach->travel == _aas_travel_teleport || reach->travel == _aas_travel_drop)
				continue;
			if (bot_distance2d(&there->center, position) > 12.f)
				continue;
			for (k = 0; k < tail && !seen; k++)
				seen = queue[k] == reach->to;
			if (seen)
				continue;
			queue[tail] = reach->to;
			depth[tail++] = d + 1;
		}
	}
	return found;
}

/* the same for movement: by the body's side */
int bot_body_friendly_ahead(struct bot_body const *body, real_point3d const *from, real yaw, real length, real half_width)
{
	struct data_iterator iterator;
	struct player_datum *other;
	real c = (real)cos(yaw);
	real sn = (real)sin(yaw);
	if (!body->teams)
		return FALSE;
	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct object_datum *unit;
		real dx, dy, along, across;
		if (other->unit_index == NONE || other->unit_index == body->unit_index || other->team_index != body->team ||
			!bot_unit_alive(other->unit_index))
		{
			continue;
		}
		unit = object_get(other->unit_index);
		if (unit->object.parent_object_index != NONE)
			continue; /* riding: not under the wheels */
		dx = unit->object.position.x - from->x;
		dy = unit->object.position.y - from->y;
		if (fabs(unit->object.position.z - from->z) > 3.f)
			continue;
		along = dx * c + dy * sn;
		across = (real)fabs(-dx * sn + dy * c);
		if (along > 0.f && along < length && across < half_width)
			return TRUE;
	}
	return FALSE;
}

/* gloating over a body: how far off one may be to go over to, and a dip (a
   crouch held until it is right down, and up again) */
#define BOT_TEABAG_REACH 6.f
#define BOT_TEABAG_DIP_TICKS 16
#define BOT_TEABAG_DOWN_TICKS 10

/* ---------- the bot */

void bot_reset(long player_absolute_index)
{
	if (player_absolute_index >= 0 && player_absolute_index < BOT_MAXIMUM_PLAYERS)
	{
		memset(&bot_states[player_absolute_index], 0, sizeof(bot_states[0]));
		bot_states[player_absolute_index].unit_index = NONE;
	}
}

/* the map's areas: worked out (or read from the cache) when a map starts,
   and again for another structure BSP */
static void nav_update(void)
{
	long i;
	if (nav_aas_tried && nav_aas_bsp == global_structure_bsp_index)
		return;
	nav_aas_tried = TRUE;
	nav_aas_bsp = global_structure_bsp_index;
	nav_danger_reset();
	nav_vehicle_forget(); /* (the areas are other ones) */
	{
		void network_connection_pardon_stall(void);
		unsigned long long start = bot_time_microseconds();
		aas_build();
		if (bot_logging())
			bot_log("bots: areas %s: %ld foot, %ld vehicle, %ld places, in %.1f s", aas_ready() ? "ready" : "none",
				aas_ready() ? aas_area_count(AAS_FOOT) : 0, aas_ready() ? aas_area_count(AAS_VEHICLE) : 0, nav_place_count(),
				(bot_time_microseconds() - start) / 1.0e6);
		/* (worked out in the middle of a game, for the structure BSP a
		   map's script switched to, and nothing heard or said the while:
		   no one is dropped for it) */
		if (bot_time_microseconds() - start > 1000000ULL)
			network_connection_pardon_stall();
	}
	nav_flood_done = TRUE;
	if (getenv("HALO_BOT_AREADUMP") && aas_ready())
	{
		/* testing: every foot area (for coverage checks) */
		long a;
		for (a = 0; a < aas_area_count(AAS_FOOT); a++)
		{
			struct aas_area const *area = aas_area(AAS_FOOT, a);
			bot_log("aasarea %ld %.2f %.2f %.2f %.2f %.2f %ld", a, area->x0, area->y0, area->x1, area->y1, area->center.z, area->flags);
		}
	}
	{
		void aas_debug_scan(void);
		if (aas_ready())
			aas_debug_scan();
	}
	if (getenv("HALO_BOT_AREAAT") && aas_ready())
	{
		/* testing: the areas round a point */
		real_point3d at;
		real radius;
		sscanf(getenv("HALO_BOT_AREAAT"), "%f,%f,%f", &at.x, &at.y, &at.z);
		for (radius = 1.f; radius <= 16.f; radius *= 2.f)
		{
			real_point3d spot = at;
			long area = aas_nearest_area(AAS_FOOT, &at, radius, &spot);
			bot_log("areaat r%.0f: in %ld nearest %ld at %.2f %.2f %.2f; floor clearance %.2f", radius, aas_point_area(AAS_FOOT, &at), area,
				spot.x, spot.y, spot.z, navm_wall_clearance(&at, NULL));
		}
		{
			long area = aas_nearest_area(AAS_FOOT, &at, 2.f, NULL);
			long r;
			if (area != NONE)
			{
				struct aas_area const *info = aas_area(AAS_FOOT, area);
				bot_log("areaat area %ld %.1f %.1f %.1f %.1f z %.2f flags %ld", area, info->x0, info->y0, info->x1, info->y1, info->center.z, info->flags);
				if (getenv("HALO_BOT_TRAVELFROM"))
				{
					real_point3d from;
					sscanf(getenv("HALO_BOT_TRAVELFROM"), "%f,%f,%f", &from.x, &from.y, &from.z);
					bot_log("areaat travel from %.1f %.1f %.1f (area %ld): %.1f s", from.x, from.y, from.z, aas_nearest_area(AAS_FOOT, &from, 2.f, NULL),
						aas_travel_time(AAS_FOOT, &from, &at));
				}
				for (r = 0; r < aas_reach_count(AAS_FOOT); r++)
				{
					struct aas_reach const *reach = aas_reach(AAS_FOOT, r);
					if (reach->to == area || reach->from == area)
						bot_log("areaat reach %ld %s travel %d from area %ld (%.1f %.1f %.1f) to area %ld (%.1f %.1f %.1f)", r, reach->to == area ? "in" : "out",
							reach->travel, reach->from, reach->start.x, reach->start.y, reach->start.z, reach->to, reach->end.x, reach->end.y, reach->end.z);
				}
			}
		}
	}
	if (bot_logging() && aas_ready())
	{
		long counts[NUMBER_OF_AAS_TRAVEL_TYPES] = { 0 };
		long r;
		for (r = 0; r < aas_reach_count(AAS_FOOT); r++)
		{
			struct aas_reach const *reach = aas_reach(AAS_FOOT, r);
			counts[reach->travel]++;
			if (getenv("HALO_BOT_REACHDUMP"))
				bot_log("reachdump %ld %d %ld %ld %.2f %.2f %.2f %.2f %.2f %.2f", r, reach->travel, reach->from, reach->to,
					reach->start.x, reach->start.y, reach->start.z, reach->end.x, reach->end.y, reach->end.z);
			if ((reach->travel == _aas_travel_ladder || reach->travel == _aas_travel_teleport) && getenv("HALO_BOT_LADDERS"))
				bot_log("bots: travel %d reach %ld from %.1f %.1f %.1f to %.1f %.1f %.1f", reach->travel, r, reach->start.x, reach->start.y, reach->start.z,
					reach->end.x, reach->end.y, reach->end.z);
		}
		bot_log("bots: reachabilities: walk %ld jump %ld drop %ld gap %ld ladder %ld teleport %ld", counts[0], counts[1], counts[2],
			counts[3], counts[4], counts[5]);
	}
	for (i = 0; i < BOT_MAXIMUM_PLAYERS; i++)
	{
		long k;
		bot_move_reset(&bot_states[i].mover.move);
		/* (new areas: the links it was avoiding were the old ones') */
		for (k = 0; k < BOT_AVOID_REACHES; k++)
		{
			bot_states[i].mover.move.avoid_reach[k] = NONE;
			bot_states[i].mover.move.avoid_until[k] = 0;
		}
	}
	/* how big is this map? By the spread of its spawns, items and flags */
	nav_vehicle_bonus = 0.f;
	if (nav_place_count() > 1)
	{
		real minimum_x = 1.0e9f, maximum_x = -1.0e9f, minimum_y = 1.0e9f, maximum_y = -1.0e9f;
		real diagonal;
		for (i = 0; i < nav_place_count(); i++)
		{
			minimum_x = MIN(minimum_x, aas_place(i)->x);
			maximum_x = MAX(maximum_x, aas_place(i)->x);
			minimum_y = MIN(minimum_y, aas_place(i)->y);
			maximum_y = MAX(maximum_y, aas_place(i)->y);
		}
		diagonal = (real)sqrt((maximum_x - minimum_x) * (maximum_x - minimum_x) + (maximum_y - minimum_y) * (maximum_y - minimum_y));
		nav_vehicle_bonus = PIN((diagonal - 60.f) / 100.f, 0.f, 0.8f);
	}
	bots_find_vantages();
	{
		struct object_iterator vehicles;
		bots_vehicle_count = 0;
		object_iterator_new(&vehicles, _object_mask_vehicle, 0);
		while (object_iterator_next(&vehicles))
			bots_vehicle_count++;
	}
}

/* testing: the quickest way to a point from any of the map's places, -1 none */
static float bot_debug_best_travel(real_point3d const *to)
{
	float best = -1.f;
	long i;
	long count = aas_spawn_place_count() > 0 ? aas_spawn_place_count() : nav_place_count();
	for (i = 0; i < count && i < 24; i++)
	{
		float time = aas_travel_time(AAS_FOOT, aas_place(i * count / MIN(count, 24) % count), to);
		if (time >= 0.f && (best < 0.f || time < best))
			best = time;
	}
	return best;
}

static long bots_last_graph_tick = -1;

static void bots_world_tick(void)
{
	long now = game_time_get();

	if (now == bots_last_graph_tick)
		return;
	bots_last_graph_tick = now;
	nav_update();
	bots_targets_tally();
	bot_player_count = player_data ? player_data->actual_count : 0;
	/* the vehicles about (the game places them a little after the map
	   loads, and blows them up and brings them back): drivers and gunners
	   are only jobs when there's something to drive */
	if (now % 90 == 5)
	{
		struct object_iterator vehicles;
		bots_vehicle_count = 0;
		object_iterator_new(&vehicles, _object_mask_vehicle, 0);
		while (object_iterator_next(&vehicles))
			bots_vehicle_count++;
	}
	if (bot_logging() && now % 300 == 0)
	{
		struct objects_information information;
		struct data_iterator iterator;
		struct player_datum *player;
		long alive = 0;
		long kills = 0;
		objects_information_get(&information);
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			alive += player->unit_index != NONE && bot_unit_alive(player->unit_index);
			kills += player->statistics.kills[0];
		}
		bot_log("bots: t%ld players %ld alive %ld kills %ld objects %ld (active %ld) object memory %.0f%%", now,
			bot_player_count, alive, kills, (long)information.object_count, (long)information.active_object_count,
			information.used_memory * 100.f);
	}
	if (bot_logging() && now % 900 == 0)
		bot_log("bots: ctf score %ld - %ld", ctf_bot_score(0), ctf_bot_score(1));
	/* testing: HALO_BOT_ITEMS, every item lying about, where, and whether
	   the areas reach it */
	/* ...and, once, every place on the map (where players start, where
	   things lie, the flags): whether there's a way there from where the
	   first player starts, and back */
	if (bot_env_set("HALO_BOT_ITEMS") && now == 30 && aas_ready() && nav_place_count() > 0)
	{
		long p;
		real_point3d const *home = aas_place(0);
		for (p = 0; p < nav_place_count(); p++)
		{
			real_point3d const *place = aas_place(p);
			long area = aas_nearest_area(AAS_FOOT, place, 3.f, NULL);
			bot_log("place %ld %s at %.2f %.2f %.2f area %ld flags %ld there %.1f back %.1f", p, p < aas_spawn_place_count() ? "spawn" : "thing",
				place->x, place->y, place->z, area, area != NONE ? (long)aas_area(AAS_FOOT, area)->flags : -1L,
				aas_travel_time(AAS_FOOT, home, place), aas_travel_time(AAS_FOOT, place, home));
		}
	}
	if (bot_env_set("HALO_BOT_ITEMS") && now == 30)
	{
		static char const *const kinds[NUMBER_OF_BOT_VEHICLE_KINDS] = { "tank", "jeep", "boat", "plane", "scout", "fighter", "turret" };
		struct object_iterator vehicles;
		struct object_datum *object;
		object_iterator_new(&vehicles, _object_mask_vehicle, 0);
		while ((object = (struct object_datum *)object_iterator_next(&vehicles)) != NULL)
		{
			char const *definition = (char const *)object_definition_get(object->definition_index);
			real const *speeds = (real const *)(definition + sizeof(struct unit_definition) + 8);
			bot_log("vehicle %s (%s, %ld seats) at %.1f %.1f %.1f speeds %.3f %.3f %.3f %.3f turn %.3f %.3f", tag_get_name(object->definition_index),
				kinds[bot_vehicle_kind(object->definition_index)], bot_vehicle_seats(vehicles.index),
				object->object.position.x, object->object.position.y, object->object.position.z,
				speeds[0], speeds[1], speeds[2], speeds[3], speeds[4], speeds[5]);
		}
	}
	if (bot_env_set("HALO_BOT_ITEMS") && now % 300 == 30 && aas_ready())
	{
		struct object_iterator items;
		struct object_datum *object;
		object_iterator_new(&items, _object_mask_weapon | _object_mask_equipment, 0);
		while ((object = (struct object_datum *)object_iterator_next(&items)) != NULL)
		{
			real_point3d snapped;
			long area;
			if (!bot_item_lying(object))
				continue;
			area = aas_nearest_area(AAS_FOOT, &object->object.position, 2.f, &snapped);
			bot_log("item t%ld %s at %.2f %.2f %.2f area %ld (%.2f %.2f %.2f) value %.1f from the spawns %.1f s", now, tag_get_name(object->definition_index),
				object->object.position.x, object->object.position.y, object->object.position.z, area,
				snapped.x, snapped.y, snapped.z, bot_item_value(object->definition_index),
				bot_debug_best_travel(&object->object.position));
		}
	}
	if (bot_logging() && now % 60 == 0)
	{
		struct object_iterator balls;
		struct object_datum *ball;
		object_iterator_new(&balls, _object_mask_weapon, 0);
		while ((ball = (struct object_datum *)object_iterator_next(&balls)) != NULL)
		{
			if (!bot_name_has(ball->definition_index, "ball"))
				continue;
			if (ball->object.parent_object_index != NONE)
			{
				real_point3d at = object_get(object_get_ultimate_parent(balls.index))->object.position;
				bot_log("oddball: carried at %.1f %.1f %.1f", at.x, at.y, at.z);
			}
			else
				bot_log("oddball: loose at %.1f %.1f %.1f", ball->object.position.x, ball->object.position.y, ball->object.position.z);
		}
	}
	if (bot_logging() && now % 60 == 0)
	{
		long team;
		for (team = 0; team < 2; team++)
		{
			long flag;
			real_point3d base;
			struct object_datum *object;
			if (!ctf_bot_flag(team, &flag, &base) || flag == NONE || !(object = object_try_and_get(flag)))
				continue;
			if (object->object.parent_object_index != NONE)
			{
				real_point3d at = object_get(object_get_ultimate_parent(flag))->object.position;
				bot_log("ctf: flag %ld carried at %.1f %.1f %.1f (%.1f from its base)", team, at.x, at.y, at.z, distance3d(&at, &base));
			}
			else if (now == 60)
				bot_log("ctf: flag %ld (%lx) home at %.1f %.1f %.2f, base %.1f %.1f %.2f", team, flag, object->object.position.x,
					object->object.position.y, object->object.position.z, base.x, base.y, base.z);
			else if (distance3d(&object->object.position, &base) > 1.f)
				bot_log("ctf: flag %ld dropped at %.1f %.1f %.1f", team, object->object.position.x, object->object.position.y, object->object.position.z);
		}
	}
	/* testing (HALO_BOT_STATE): twice a second, where everyone is and what
	   they're after, and the state of the objective, for analysis
	   (tools: bot behaviour checks) */
	if (bot_env_set("HALO_BOT_STATE") && now % 15 == 0)
	{
		struct data_iterator iterator;
		struct player_datum *player;
		long team;
		for (team = 0; team < 2; team++)
		{
			long flag;
			real_point3d base;
			struct object_datum *object;
			if (ctf_bot_flag(team, &flag, &base) && flag != NONE && (object = object_try_and_get(flag)) != NULL)
			{
				long carrier = NONE;
				real_point3d at = object->object.position;
				if (object->object.parent_object_index != NONE)
				{
					struct unit_datum *unit = unit_try_and_get(object_get_ultimate_parent(flag));
					at = object_get(object_get_ultimate_parent(flag))->object.position;
					if (unit && unit->unit.player_index != NONE)
						carrier = DATUM_INDEX_TO_ABSOLUTE_INDEX(unit->unit.player_index);
				}
				bot_log("bs flag t%ld team %ld at %.1f %.1f %.1f base %.1f %.1f %.1f carrier %ld score %ld", now, team, at.x, at.y, at.z,
					base.x, base.y, base.z, carrier, ctf_bot_score(team));
			}
		}
		if (game_engine == &oddball_engine)
		{
			struct object_iterator balls;
			struct object_datum *ball;
			object_iterator_new(&balls, _object_mask_weapon, 0);
			while ((ball = (struct object_datum *)object_iterator_next(&balls)) != NULL)
			{
				long carrier = NONE;
				real_point3d at = ball->object.position;
				if (!bot_name_has(ball->definition_index, "ball"))
					continue;
				if (ball->object.parent_object_index != NONE)
				{
					struct unit_datum *unit = unit_try_and_get(object_get_ultimate_parent(balls.index));
					at = object_get(object_get_ultimate_parent(balls.index))->object.position;
					if (unit && unit->unit.player_index != NONE)
						carrier = DATUM_INDEX_TO_ABSOLUTE_INDEX(unit->unit.player_index);
				}
				bot_log("bs ball t%ld at %.1f %.1f %.1f carrier %ld", now, at.x, at.y, at.z, carrier);
			}
		}
		{
			real_point3d hill_center;

			if (king_bot_hill(&hill_center))
				bot_log("bs hill t%ld at %.1f %.1f %.1f", now, hill_center.x, hill_center.y, hill_center.z);
		}
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			long index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
			struct bot_state *bot = index < BOT_MAXIMUM_PLAYERS ? &bot_states[index] : NULL;
			real_point3d at;
			long target_player = NONE;
			int ball = game_engine == &oddball_engine && player_has_ball(iterator.datum_index);
			if (player->unit_index == NONE || !bot_unit_alive(player->unit_index))
			{
				bot_log("bs p t%ld %ld team %ld dead", now, index, (long)player->team_index);
				continue;
			}
			at = object_get(object_get_ultimate_parent(player->unit_index))->object.position;
			if (bot && bot->target_unit != NONE)
			{
				struct unit_datum *target = unit_try_and_get(bot->target_unit);
				if (target && target->unit.player_index != NONE)
					target_player = DATUM_INDEX_TO_ABSOLUTE_INDEX(target->unit.player_index);
			}
			bot_log("bs p t%ld %ld team %ld at %.1f %.1f %.1f goal %ld %.1f %.1f %.1f obj %d target %ld ball %d veh %d role %ld what %s",
				now, index, (long)player->team_index, at.x, at.y, at.z, bot ? bot->goal_kind : 0, bot ? bot->goal.x : 0.f,
				bot ? bot->goal.y : 0.f, bot ? bot->goal.z : 0.f, bot ? bot->goal_objective : 0, target_player, ball,
				object_get(player->unit_index)->object.parent_object_index != NONE, bot ? bot->role : 0,
				bot && bot->goal_object != NONE && object_try_and_get(bot->goal_object) ? tag_get_name(object_get(bot->goal_object)->definition_index) :
				bot && bot->goal_squad ? "squad" : "-");
		}
	}
	/* deaths make a place dangerous for the victim's side; and who did it
	   (whoever's kills went up as they died) is who the dead, and their
	   friends, may have it in for */
	{
		long *alive_unit = bots_alive_unit;
		real_point3d *last_position = bots_last_position;
		struct data_iterator iterator;
		struct player_datum *player;
		long died[8];
		long died_count = 0;
		long scored[8];
		long scored_count = 0;
		long i;
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			long index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
			int alive;
			long kills;
			if (index >= BOT_MAXIMUM_PLAYERS)
				continue;
			kills = player->statistics.kills[0];
			if (kills > bots_kills_counted[index] && scored_count < 8)
				scored[scored_count++] = index;
			bots_kills_counted[index] = kills;
			alive = player->unit_index != NONE && bot_unit_alive(player->unit_index);
			if (alive)
			{
				if (alive_unit[index] != player->unit_index)
					bots_spawned_time[index] = now;
				alive_unit[index] = player->unit_index;
				last_position[index] = object_get(object_get_ultimate_parent(player->unit_index))->object.position;
			}
			else if (alive_unit[index] != 0)
			{
				alive_unit[index] = 0;
				bot_side_saw(bot_side_of(player), NONE, &last_position[index]);
				if (died_count < 8)
					died[died_count++] = index;
			}
		}
		for (i = 0; i < died_count; i++)
		{
			/* (one of those who scored, not the dead one themselves: with
			   two deaths in a tick, each a scorer of their own) */
			long k;
			bots_killer[died[i]] = NONE;
			for (k = 0; k < scored_count; k++)
			{
				if (scored[k] != NONE && scored[k] != died[i])
				{
					bots_killer[died[i]] = scored[k];
					bots_killed_time[died[i]] = now;
					if (died_count > 1 && scored_count > 1)
						scored[k] = NONE;
					break;
				}
			}
		}
	}
	/* each side's route costs, a part each tick (by turns) */
	if (aas_ready())
		bot_sides_update();
	/* who's been where */
	if (now % 15 == 0)
	{
		struct data_iterator iterator;
		struct player_datum *player;
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (player->unit_index != NONE && bot_unit_alive(player->unit_index))
			{
				long team = player->team_index >= 0 ? player->team_index : DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
				*nav_visit_slot(team, &object_get(object_get_ultimate_parent(player->unit_index))->object.position) = now;
			}
		}
	}
}

/* pull the trigger the way a player does: bursts at range with automatic
   weapons (they spray), a human cadence with semi-automatic ones, a steady
   aim before a sniper or rocket shot, the plasma pistol charged up for
   shields, the plasma rifle let cool before it overheats; zoomed in at
   range with a scope */
/* how well a weapon suits this fight: the range, what's in it (an empty
   magazine is a swap, not a reload, with someone shooting back) and the
   target's shields (plasma strips them, bullets and headshots finish) */
/* a rocket's blast is half a world unit to two: this far off and further,
   one at someone's feet is theirs and not ours */
#define BOT_ROCKET_CLOSEST 2.8f
/* ... and this near, the floor at their feet is the thing to hit (the blast
   does it: no need of the rocket itself landing on them) */
#define BOT_ROCKET_FEET 22.f

static real bot_weapon_fit(struct bot_weapon const *w, real d, real shields)
{
	real fit;

	if (w->none || w->spent)
		return 0.f;
	switch (w->kind)
	{
	case _bot_gun_shotgun: fit = d < 5.f ? 10.f : d < 9.f ? 7.f : d < 14.f ? 3.f : 1.f; break;
	case _bot_gun_sniper: fit = d > 20.f ? 9.f : d > 9.f ? 6.5f : 2.f; break;
	case _bot_gun_rocket: fit = d < BOT_ROCKET_CLOSEST ? 1.f : d < 35.f ? 9.f : 4.f; break;
	case _bot_gun_fuel_rod: fit = d < 5.f ? 1.f : d < 30.f ? 7.5f : 3.f; break;
	case _bot_gun_flamethrower: fit = d < 7.f ? 8.f : 1.f; break;
	case _bot_gun_pistol: fit = d < 3.f ? 5.5f : 7.f; break;
	case _bot_gun_assault_rifle: fit = d < 10.f ? 6.f : d < 22.f ? 4.f : 2.f; break;
	case _bot_gun_plasma_rifle: fit = d < 14.f ? 6.f : 3.f; break;
	case _bot_gun_needler: fit = d < 18.f ? 3.5f : 1.5f; break; /* (the rifle or the pistol, if there is one) */
	case _bot_gun_plasma_pistol: fit = d < 15.f ? 4.f : 2.f; break;
	default: fit = w->value * 0.7f; break;
	}
	switch (w->kind)
	{
	case _bot_gun_plasma_pistol:
	case _bot_gun_plasma_rifle:
	case _bot_gun_needler:
		if (shields > 0.6f)
			fit += w->kind == _bot_gun_needler ? 1.f : 2.f;
		else if (shields < 0.15f)
			fit -= 1.5f;
		break;
	case _bot_gun_pistol:
	case _bot_gun_assault_rifle:
	case _bot_gun_sniper:
		if (shields < 0.15f)
			fit += 1.5f;
		break;
	}
	if (w->uses_ammunition)
	{
		if (w->rounds_loaded <= 0 && w->rounds_total <= 0)
			return 0.f;
		if (w->rounds_loaded <= 0)
			fit *= 0.25f;
	}
	else if (w->heat > 0.85f)
		fit *= 0.4f;
	return fit;
}

static void bot_fire_control(struct bot_state *bot, struct bot_weapon const *weapon, real off_yaw, real off_pitch,
	real target_distance, real target_shield, struct player_action *action)
{
	real tolerance = (real)atan2(0.45f, MAX(target_distance, 0.5f)) + 0.01f;
	int on_target;
	int pull = FALSE;

	if (bot->shot_cooldown > 0)
		bot->shot_cooldown--;
	if (weapon->none || target_distance >= weapon->range)
		return;
	if (weapon->uses_ammunition && weapon->rounds_loaded <= 0 && weapon->rounds_total > 0)
	{
		action->control_flags |= FLAG(_unit_control_weapon_reload_bit);
		return;
	}
	/* scope in at range */
	if (weapon->precision && target_distance > 20.f)
		action->desired_zoom_level = (short)(target_distance > 55.f && weapon->kind == _bot_gun_sniper ? 1 : 0);
	/* (its trigger: the sprayer fires when it is about on them and keeps
	   firing; the disciplined wait for the shot, and pace it) */
	on_target = off_yaw < tolerance * (1.9f - 0.8f * bot->personality.discipline) &&
		off_pitch < tolerance * (2.5f - 1.f * bot->personality.discipline);
	switch (weapon->kind)
	{
	case _bot_gun_assault_rifle:
	case _bot_gun_needler:
	case _bot_gun_plasma_rifle:
	case _bot_gun_flamethrower:
		/* automatic: close, hold it down; further off, bursts */
		if (weapon->kind == _bot_gun_plasma_rifle && weapon->heat > 0.75f)
			bot->burst_pause = 20; /* let it cool */
		if (bot->burst_pause > 0)
		{
			bot->burst_pause--;
			break;
		}
		if (!on_target)
			break;
		pull = TRUE;
		if (target_distance > 10.f && ++bot->burst_ticks > (long)((6.f + 8.f * 10.f / target_distance) * (1.6f - 1.2f * bot->personality.discipline)))
		{
			/* (and a pause, both a bit different each time: a rhythm is
			   something to time a peek by) */
			bot->burst_ticks = -(long)(bot_random(bot) * 4.f);
			bot->burst_pause = 2 + (long)((2.f + target_distance / 4.f * (0.75f + 0.5f * bot_random(bot))) * (0.5f + bot->personality.discipline));
		}
		break;
	case _bot_gun_plasma_pistol:
		/* against shields, charge it up and let go on target; else tap.
		   (A charge that was broken off, not held last tick, is gone: the
		   trigger let go and it went off at nothing) */
		if (bot->charge_ticks > 0 && bot->charge_time != game_time_get() - 1)
			bot->charge_ticks = 0;
		if (target_shield > 0.5f && target_distance < 15.f && bot->charge_ticks >= 0)
		{
			bot->charge_time = game_time_get();
			if (bot->charge_ticks < 30)
			{
				bot->charge_ticks++;
				pull = TRUE;
			}
			else if (on_target)
			{
				bot->charge_ticks = -20; /* released: tap for a while */
			}
			else
				pull = TRUE; /* keep holding until the aim's on */
			break;
		}
		if (bot->charge_ticks < 0)
			bot->charge_ticks++;
		if (on_target && bot->shot_cooldown <= 0)
		{
			pull = TRUE;
			bot->shot_cooldown = 5 + (long)(bot_random(bot) * 3.f);
		}
		break;
	case _bot_gun_sniper:
		/* only a settled aim, a second or so between shots */
		if (off_yaw < tolerance * 0.6f && off_pitch < tolerance && bot->shot_cooldown <= 0)
		{
			pull = TRUE;
			bot->shot_cooldown = 20 + (long)(bot_random(bot) * 15.f);
		}
		break;
	case _bot_gun_rocket:
	case _bot_gun_fuel_rod:
		/* never at our own feet (nor into the floor or wall beside them
		   while still pitching round to them): no nearer than its blast
		   reaches, and a little */
		if (target_distance > (weapon->kind == _bot_gun_rocket ? BOT_ROCKET_CLOSEST : 5.f) && off_yaw < tolerance &&
			off_pitch < tolerance * 1.5f && bot->shot_cooldown <= 0)
		{
			pull = TRUE;
			bot->shot_cooldown = (weapon->kind == _bot_gun_rocket ? 26 : 10) + (long)(bot_random(bot) * (weapon->kind == _bot_gun_rocket ? 10.f : 5.f));
		}
		break;
	case _bot_gun_shotgun:
		if (target_distance < 9.f && on_target && bot->shot_cooldown <= 0)
		{
			pull = TRUE;
			bot->shot_cooldown = 12 + (long)(bot_random(bot) * 6.f);
		}
		break;
	case _bot_gun_pistol:
	default:
		/* semi-automatic: a click every few ticks, slower when far */
		if (on_target && bot->shot_cooldown <= 0)
		{
			pull = TRUE;
			bot->shot_cooldown = (weapon->automatic ? 1 : 4) + (long)(target_distance / 12.f * (0.5f + bot->personality.discipline)) +
				(long)(bot_random(bot) * 3.f);
		}
		break;
	}
	if (pull)
	{
		action->primary_trigger = 1.f;
		action->control_flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
	}
}

/* a power weapon (the rockets, the sniper rifle, a shotgun, a fuel rod)
   lying there for the taking */
static int bot_goal_is_power_weapon(struct bot_state const *bot)
{
	struct object_datum *object = bot->goal_kind == _bot_goal_item && bot->goal_object != NONE ? object_try_and_get(bot->goal_object) : NULL;
	return object && TEST_FLAG(_object_mask_weapon, object->object.type) && bot_item_lying(object) &&
		bot_is_power_item(object->definition_index);
}

/* a flag or a ball (weapons with no trigger): the game's objective */
static int bot_goal_is_powerup(struct bot_state const *bot)
{
	struct object_datum *object = bot->goal_kind == _bot_goal_item && bot->goal_object != NONE ? object_try_and_get(bot->goal_object) : NULL;
	return object && TEST_FLAG(_object_mask_equipment, object->object.type) && bot_item_value(object->definition_index) >= 7.f;
}

static int bot_goal_is_game_object(long object_index)
{
	struct object_datum *object = object_index != NONE ? object_try_and_get(object_index) : NULL;
	return object && TEST_FLAG(_object_mask_weapon, object->object.type) &&
		weapon_definition_get(object->definition_index)->weapon.triggers.count <= 0;
}

/* standing guard: look one way for a second or two, then another, the way
   the enemy is likeliest to come from (roughly toward the nearest one) */
static real bot_glance(struct bot_state *bot, struct player_datum *player, real_point3d const *position)
{
	if (--bot->glance_ticks <= 0)
	{
		real_point3d enemy;
		real distance;
		bot->glance_ticks = 40 + (long)(bot_random(bot) * 50.f);
		if (bot_nearest_enemy(player, position, &enemy, &distance) && bot_random(bot) < 0.7f)
			bot->glance_yaw = bot_wrap(bot_yaw_to(position, &enemy) + (bot_random(bot) - 0.5f) * 1.2f);
		else
			bot->glance_yaw = bot_wrap(bot->yaw + (bot_random(bot) - 0.5f) * 3.f);
	}
	return bot->glance_yaw;
}

#define BOT_GRENADE_RANGE 17.f /* about as far as one lands where it's thrown */

/* how keen on a grenade just now: its nature, what it has learned (how
   many of its grenades told), and whether the gun in hand would do */
static real bot_grenade_keenness(struct bot_state const *bot, struct bot_weapon const *weapon, real target_distance)
{
	struct bot_personality const *personality = &bot->personality;
	/* until a few have been thrown, a guess of one in three */
	real hit_rate = (personality->grenades_hit + 1.f) / (personality->grenades_thrown + 3.f);
	real keen = personality->grenade_happy * (0.4f + 1.8f * hit_rate);
	if (!weapon->none && (weapon->value >= 6.f || weapon->precision) && target_distance < weapon->range)
		keen *= 0.25f;
	else if (weapon->none || (weapon->uses_ammunition && weapon->rounds_loaded == 0))
		keen *= 2.f;
	return MIN(1.f, keen);
}

static void bot_grenade_watch(struct bot_state *bot, long target_unit)
{
	struct object_datum *target = target_unit != NONE ? object_try_and_get(target_unit) : NULL;
	bot->grenade_watch_unit = target ? target_unit : NONE;
	bot->grenade_watch_ticks = target ? 3 * BOT_TICKS_PER_SECOND : 0;
	bot->grenade_watch_vitality = target ? target->object.shield_vitality + target->object.body_vitality : 0.f;
}

/* did it tell? A kill or a good chunk of their shields within a few
   seconds counts */
static void bot_grenade_learn(struct bot_state *bot)
{
	struct object_datum *target;
	if (bot->grenade_watch_ticks <= 0 || bot->grenade_pending_ticks > 0)
		return;
	target = bot->grenade_watch_unit != NONE ? object_try_and_get(bot->grenade_watch_unit) : NULL;
	if (!target || TEST_FLAG(target->object.damage_flags, _object_dead_bit) ||
		bot->grenade_watch_vitality - (target->object.shield_vitality + target->object.body_vitality) > 0.35f)
	{
		bot->personality.grenades_thrown++;
		bot->personality.grenades_hit++;
		bot->grenade_watch_ticks = 0;
		return;
	}
	if (--bot->grenade_watch_ticks == 0)
		bot->personality.grenades_thrown++;
}

/* a throw at point won't bounce straight back at us: the first few units
   of the way clear, and the lob's top clear of the ceiling */
static int bot_grenade_clear(real_point3d const *eye, real_point3d const *point, long ignore)
{
	real_point3d near_point = *eye;
	real distance = bot_distance2d(eye, point);
	real t = MIN(1.f, 5.f / MAX(distance, 0.1f));
	real_point3d top;
	near_point.x += (point->x - eye->x) * t;
	near_point.y += (point->y - eye->y) * t;
	near_point.z += (point->z - eye->z) * t + 0.5f;
	if (!bot_line_clear(bot_sight_flags, eye, &near_point, ignore))
		return FALSE;
	top = *eye;
	top.z += 1.f + distance * 0.12f;
	return bot_line_clear(bot_sight_flags, eye, &top, ignore);
}

/* get ready to throw a grenade at point (if we have one) */
static void bot_start_grenade(struct bot_state *bot, struct unit_datum *unit, real_point3d const *point)
{
	long i;
	for (i = 0; i < NUMBER_OF_UNIT_GRENADE_TYPES; i++)
	{
		if (unit->unit.grenade_counts[i] > 0)
		{
			bot->grenade_index = (short)i;
			bot->grenade_point = *point;
			bot->grenade_pending_ticks = 20;
			return;
		}
	}
}

static void bot_think_inner(long player_absolute_index, struct player_action *action);
/* infection.c's */
boolean infection_player_is_zombie(long player_index);

/* testing: HALO_BOT_PROFILE, what the bots cost a tick, every 5 s of game */
void bot_think(long player_absolute_index, struct player_action *action)
{
	static int profile = -1;
	static unsigned long long total, worst, calls, tick_total, worst_tick;
	static long last_tick = -1, ticks, window_start;
	unsigned long long start, spent;
	if (profile < 0)
	{
		profile = getenv("HALO_BOT_PROFILE") != NULL;
		bot_profiling = profile;
	}
	if (!profile)
	{
		bot_think_inner(player_absolute_index, action);
		return;
	}
	start = bot_time_microseconds();
	BOT_PROFILE_MARK("setup");
	bot_think_inner(player_absolute_index, action);
	BOT_PROFILE_MARK(NULL);
	spent = bot_time_microseconds() - start;
	bot_profile_call_end(player_absolute_index, game_time_get(), spent, 2000);
	total += spent;
	calls++;
	if (spent > worst)
		worst = spent;
	if (game_time_get() != last_tick)
	{
		last_tick = game_time_get();
		ticks++;
		tick_total = 0;
	}
	/* (all the bots' thinking in one tick: what a frame waits for) */
	tick_total += spent;
	if (tick_total > worst_tick)
		worst_tick = tick_total;
	if (last_tick - window_start >= 150 && ticks > 0)
	{
		bot_log("bot profile t%ld: %.3f ms a tick for %.1f bots, worst call %.2f ms, worst tick %.2f ms, objects %ld", last_tick,
			(double)total / 1000.0 / ticks, (double)calls / ticks, (double)worst / 1000.0, (double)worst_tick / 1000.0,
			object_header_data ? (long)object_header_data->actual_count : -1L);
		bot_profile_report(ticks);
		total = worst = calls = worst_tick = 0;
		ticks = 0;
		window_start = last_tick;
	}
}

static void bot_think_inner(long player_absolute_index, struct player_action *action)
{
	struct bot_skill_parameters const *skill = &bot_skills[PIN(bot_skill, 1, 5) - 1];
	struct bot_state *bot;
	struct data_iterator iterator;
	struct player_datum *player;
	struct player_datum *me = NULL;
	struct unit_datum *unit;
	struct bot_weapon weapon;
	real_point3d eye;
	real_point3d position;
	real target_distance = 1.0e9f;
	long target;
	long vehicle_index = NONE;
	int driving = FALSE;
	int gunner = FALSE;
	int teams = game_engine_has_teams();
	real move_yaw;
	int moving = FALSE;
	int jump = FALSE;
	real path_yaw = NONE; /* straight along the route, this tick */
	real desired_yaw;
	real desired_pitch;
	real aim_yaw;
	real aim_pitch;
	real move_speed = 1.f;
	int threatened = FALSE;
	struct bot_body body;
	struct bot_move_result result;
	int moved = FALSE; /* this tick's movement is the route's (bot_move.c) */
	int driving_moved = FALSE;
	int committed = FALSE; /* in the middle of a jump, a gap, a ladder: see it through */
	int look_forced = FALSE; /* the route needs us facing a way (a ladder) */
	/* Why it looks where it does (desired_yaw, desired_pitch): each place
	   that sets them says which of these it is, and what is done with the
	   look afterwards goes by the reason, not by which place wrote last. */
	enum
	{
		_bot_look_ahead, /* nothing in particular: the way it faces */
		_bot_look_aim, /* at someone in sight, to shoot */
		_bot_look_expect, /* where someone gone out of sight will come out */
		_bot_look_glance, /* standing: a look about */
		_bot_look_noise, /* someone on the tracker, or heard */
		_bot_look_path, /* walking: the way it goes */
		_bot_look_scan, /* walking: a deliberate look to a side or behind, for a moment */
		_bot_look_steer, /* driving, flying: a vehicle goes where its driver looks */
		_bot_look_move, /* the route needs it (a ladder, a ledge): look_forced */
		_bot_look_act, /* something it is doing: at a vehicle's door, over a body, at a friend */
		_bot_look_lob, /* lining up a grenade */
	};
	long look_reason = _bot_look_ahead;
	int weapon_chosen = FALSE;
	real drive_throttle = 0.f;
	int zombie = FALSE; /* infection: one of the infected (infection.c) */
	long look_period;

	action->control_flags = 0;
	action->throttle.i = 0.f;
	action->throttle.j = 0.f;
	action->primary_trigger = 0.f;
	action->desired_weapon_index = NONE;
	action->desired_grenade_index = NONE;
	action->desired_zoom_level = NONE;
	if (player_absolute_index < 0 || player_absolute_index >= BOT_MAXIMUM_PLAYERS || !player_data || !player_data->valid)
		return;
	bots_world_tick();
	bot = &bot_states[player_absolute_index];
	if (!bot->has_personality)
	{
		bot_make_personality(player_absolute_index, &bot->personality);
		bot->temperament = bot->personality;
		bot->has_personality = TRUE;
		bot->unit_index = NONE;
		if (bot_logging())
			bot_log("bot%ld is a %s (%s): grudge %.1f temper %+.1f swagger %.1f trigger %.1f melee %.1f vengeful %.1f composure %.1f loyal %.1f sporting %.1f showboat %.1f camping %.1f herd %+.1f",
				player_absolute_index, bot_style_names[bot->personality.style], bot_signature_names[bot->personality.signature],
				bot->personality.grudge, bot->personality.temper, bot->personality.swagger, bot->personality.discipline, bot->personality.melee,
				bot->personality.vengeful, bot->personality.composure, bot->personality.loyalty, bot->personality.sporting,
				bot->personality.showboat, bot->personality.camping, bot->personality.herd);
	}
	/* (straight to our slot: looking through every player, for every bot,
	   every tick, is a lot of looking in a big game) */
	if (player_absolute_index < player_data->maximum_count)
	{
		player = (struct player_datum *)((byte *)xbox_pointer(player_data->data) + player_absolute_index * player_data->size);
		if (((struct datum_header *)player)->identifier)
			me = player;
	}
	(void)iterator;
	bot_off = me ? bot_off_for_team(me->team_index) : 0;
	if (me)
		bot_learn_from_score(bot, me);
	if (!me || me->unit_index == NONE || !bot_unit_alive(me->unit_index))
	{
		action->desired_facing.yaw = bot->yaw;
		action->desired_facing.pitch = 0.f;
		bot->unit_index = NONE;
		return;
	}
	unit = unit_get(me->unit_index);
	zombie = infection_player_is_zombie(DATUM_INDEX_NEW(player_absolute_index, (unsigned short)((struct datum_header *)me)->identifier));
	if (bot_logging())
	{
		static int told;
		if (!told && TEST_FLAG(_object_mask_biped, unit->object.type))
		{
			struct biped_definition *definition = biped_definition_get(object_get(me->unit_index)->definition_index);
			told = TRUE;
			bot_log("bots: biped slope %.1f deg (min normal k %.3f), uphill falloff %.1f cutoff %.1f, jump %.3f, height %.2f radius %.2f",
				definition->biped.maximum_slope_angle * 57.2958f, definition->biped.runtime_minimum_normal_k,
				definition->biped.uphill_falloff_angle * 57.2958f, definition->biped.uphill_cutoff_angle * 57.2958f,
				definition->biped.jump_velocity, definition->biped.collision_height_standing, definition->biped.collision_radius);
		}
	}
	if (bot->unit_index != me->unit_index)
	{
		/* a new life: keep the personality, forget the rest. What it
		   learned last life stays; its mood this life is its own (a bit
		   bolder or warier, greedier or steadier than last time) */
		struct bot_personality personality = bot->personality;
		struct bot_personality temperament = bot->temperament;
		unsigned long seed = bot->random ? bot->random : bot_hash(((unsigned long)player_absolute_index + 1) ^ bots_match());
		/* what it's learned and counted this game carries over (the counts
		   too: otherwise every life would learn the whole game's kills and
		   deaths over again); the temperament itself stays as it was drawn */
		personality = temperament;
		personality.grenades_thrown = bot->personality.grenades_thrown;
		personality.grenades_hit = bot->personality.grenades_hit;
		personality.kills_seen = bot->personality.kills_seen;
		personality.deaths_seen = bot->personality.deaths_seen;
		personality.route_side = bot->personality.route_side;
		personality.route_died = bot->personality.route_died;
		personality.learned_daring = bot->personality.learned_daring;
		personality.learned_teamwork = bot->personality.learned_teamwork;
		personality.learned_aggression = bot->personality.learned_aggression;
		personality.learned_time = bot->personality.learned_time;
		personality.kill_streak = bot->personality.kill_streak;
		personality.death_streak = bot->personality.death_streak;
		personality.mood = bot->personality.mood;
		personality.daring = PIN(temperament.daring + personality.learned_daring, 0.f, 1.f);
		personality.teamwork = PIN(temperament.teamwork + personality.learned_teamwork, 0.f, 1.f);
		{
			unsigned long mood = bot_hash(seed ^ 0x27d4eb2dUL);
			/* (the draw first: PIN is a macro, and would draw it again for
			   each bound it checks) */
			real swing;
#define BOT_MOOD(field, low, high) (swing = 0.3f * ((real)((mood = bot_hash(mood)) & 0xFFFF) / 65535.f - 0.5f), \
	personality.field = PIN(personality.field + swing, low, high))
			mood = bot_hash(mood); /* (aggression's draw, when it had one) */
			personality.aggression = PIN(bot_skill_aggression() + personality.learned_aggression + personality.mood, 0.f, 1.f); /* (the skill level may have changed) */
			BOT_MOOD(greed, 0.2f, 1.f);
			BOT_MOOD(patience, 0.f, 1.f);
			BOT_MOOD(daring, 0.f, 1.f);
			BOT_MOOD(jumpiness, 0.1f, 1.f);
			BOT_MOOD(vehicle_love, 0.f, 1.f);
			BOT_MOOD(focus, 0.2f, 1.f);
#undef BOT_MOOD
		}
		bot_skill_traits(&personality); /* (the skill level may have changed) */
		long ignore_object[6];
		long ignore_until[6];
		memcpy(ignore_object, bot->ignore_object, sizeof(ignore_object));
		memcpy(ignore_until, bot->ignore_until, sizeof(ignore_until));
		/* (what it remembers of this game, through the life's clean slate) */
		static struct bot_state kept;
		memcpy(&kept.memory, &bot->memory, sizeof(kept.memory));
		memset(bot, 0, sizeof(*bot));
		memcpy(&bot->memory, &kept.memory, sizeof(bot->memory));
		memcpy(bot->ignore_object, ignore_object, sizeof(ignore_object));
		memcpy(bot->ignore_until, ignore_until, sizeof(ignore_until));
		bot->personality = personality;
		bot->temperament = temperament;
		bot->has_personality = TRUE;
		bot->random = seed;
		bot->unit_index = me->unit_index;
		bot->target_unit = NONE;
		bot->goal_object = NONE;
		bot->region = NONE;
		bot_move_reset(&bot->mover.move);
		bot->tracked_unit = NONE;
		bot->aim_velocity_unit = NONE;
		bot->health_object = NONE;
		bot->mover.index = player_absolute_index;
		bot->mover.random = bot_hash(seed ^ 0x5bd1e995UL);
		bot->yaw = bot_wrap((real)atan2(unit->unit.desired_facing_vector.j, unit->unit.desired_facing_vector.i));
		bot->strafe_sign = 1.f;
	}

	bot->team = me->team_index >= 0 ? me->team_index : player_absolute_index;
	unit_get_camera_position(me->unit_index, &eye);
	if (unit->object.parent_object_index != NONE)
	{
		vehicle_index = object_get_ultimate_parent(me->unit_index);
		if (!object_try_and_get_and_verify_type(vehicle_index, _object_mask_vehicle))
			vehicle_index = NONE;
		else
		{
			driving = unit_seat_is_driver(unit->object.parent_object_index, unit->unit.parent_seat_index);
			gunner = unit_seat_is_gunner(unit->object.parent_object_index, unit->unit.parent_seat_index);
		}
	}
	position = vehicle_index != NONE ? object_get(vehicle_index)->object.position : unit->object.position;
	if (bot_logging() && (vehicle_index != bot->last_vehicle || (driving ? 1 : gunner ? 2 : 3) != bot->last_seat_kind))
	{
		if (vehicle_index != NONE)
			bot_log("bot%ld t%ld in %s as %s", player_absolute_index, game_time_get(), tag_get_name(object_get(vehicle_index)->definition_index),
				driving ? "driver" : gunner ? "gunner" : "passenger");
		else if (bot->last_vehicle != NONE && bot->last_vehicle != 0)
			bot_log("bot%ld t%ld out of vehicle stuck %ld", player_absolute_index, game_time_get(), bot->mover.move.stuck_count);
	}
	bot->last_vehicle = vehicle_index;
	bot->last_seat_kind = driving ? 1 : gunner ? 2 : 3;
	memset(&result, 0, sizeof(result));
	/* the body movement works with */
	{
		struct object_datum *root = object_get(vehicle_index != NONE ? vehicle_index : me->unit_index);
		body.unit_index = me->unit_index;
		body.vehicle_index = vehicle_index;
		body.driving = driving;
		body.position = position;
		body.velocity = root->object.translational_velocity;
		body.facing_yaw = bot_wrap((real)atan2(root->object.forward.j, root->object.forward.i));
		body.team = me->team_index;
		body.teams = teams;
		body.speed_scale = me->speed_multiplier > 0.1f ? me->speed_multiplier : 1.f;
	}
	bot->mover.unit_index = me->unit_index;
	bot->mover.teamwork = bot->personality.teamwork;
	/* routes round what our side knows is dangerous (the most daring
	   don't bother) */
	/* (not when going after someone: that's toward the danger; nor for
	   the aggressive, who go straight in) */
	bot->carrying_objective = vehicle_index == NONE && (bot_holds_named(unit, "flag") || bot_holds_named(unit, "ball"));
	/* (whoever has the flag or the ball keeps out of their sight, whatever
	   its nature) */
	bot->mover.move.cost_slot = ((bot->personality.daring < 0.8f && bot->personality.aggression < (BOT_ON(BOT_F_COSTSLOT) ? 0.6f : 0.7f)) ||
		(bot->carrying_objective && BOT_ON(BOT_F_COSTSLOT))) &&
		bot->goal_kind != _bot_goal_enemy && bot->target_unit == NONE ? bot_side_of(me) : NONE;
	committed = vehicle_index == NONE && bot_move_committed(&bot->mover);
	bot_weapon_describe(unit->unit.current_weapon_index >= 0 ?
		unit->unit.weapon_object_indices[unit->unit.current_weapon_index] : NONE, &weapon);
	if (vehicle_index != NONE && (driving || gunner))
	{
		/* a vehicle's gun: a chaingun or a cannon, fired in bursts (a
		   passenger fires its own weapon, as it is) */
		weapon.kind = _bot_gun_assault_rifle;
		weapon.precision = FALSE;
		weapon.spent = FALSE;
		weapon.range = 60.f;
		weapon.automatic = TRUE;
		weapon.charging = FALSE;
		weapon.none = FALSE;
		weapon.uses_ammunition = FALSE;
		weapon.projectile_speed = 0.f;
	}

	BOT_PROFILE_MARK("perception");
	/* ---- perception */
	bot_finding_teamwork = bot->personality.teamwork;
	/* A full look round (a line of sight to everyone) only every second
	   tick in a game of twelve or more, every third in one of twenty-four
	   (by turns: a fifteenth of a second, a tenth, well inside anyone's
	   reaction time); in between, keep on whoever we're shooting while
	   they're in sight. It is the bots' steadiest cost. */
	target = NONE;
	look_period = MIN(3, 1 + bot_player_count / 12);
	if (bot_env_set("HALO_BOT_LOOK_ALWAYS")) /* (testing) */
		look_period = 1;
	bot_look_period = look_period;
	if (look_period > 1 && bot->target_unit != NONE && bot_unit_alive(bot->target_unit) &&
		(game_time_get() + player_absolute_index) % look_period != 0)
	{
		/* (as the look round sees them: no further off than it looks) */
		real_point3d at = object_get(bot->target_unit)->object.bounding_sphere_center;
		if (distance3d(&eye, &at) <= (weapon.precision ? 150.f : 90.f) &&
			bot_line_clear(bot_sight_flags, &eye, &at, object_get_ultimate_parent(me->unit_index)) &&
			bot_sees_camouflaged(bot->target_unit, distance3d(&eye, &at), TRUE))
		{
			target = bot->target_unit;
			target_distance = distance3d(&eye, &at);
		}
	}
	else if (look_period > 1 && bot->target_unit == NONE &&
		(game_time_get() + player_absolute_index) % look_period != 0)
	{
		; /* nobody in sight last time: look again soon */
	}
	else
	target = bot_find_target(bot, bot->hit_ticks > 0, me->unit_index, (short)me->team_index, &eye, weapon.precision ? 150.f : 90.f, bot->target_unit,
		bot_name_has(unit->unit.current_weapon_index >= 0 && unit->unit.weapon_object_indices[unit->unit.current_weapon_index] != NONE &&
			object_try_and_get(unit->unit.weapon_object_indices[unit->unit.current_weapon_index]) ?
			object_get(unit->unit.weapon_object_indices[unit->unit.current_weapon_index])->definition_index : NONE, "rocket"), &target_distance);
	if (target != NONE)
	{
		if (target != bot->target_unit)
		{
			real error = skill->aim_error * bot->personality.accuracy;
			struct object_datum *root = object_get(object_get_ultimate_parent(target));
			bot->target_unit = target;
			bot->seen_ticks = 0;
			bot->aim_error_yaw = (bot_random(bot) - 0.5f) * 2.f * error;
			bot->aim_error_pitch = (bot_random(bot) - 0.5f) * error;
			/* (their movement, not the last one's) */
			bot->last_seen_velocity.i = root->object.translational_velocity.i;
			bot->last_seen_velocity.j = root->object.translational_velocity.j;
			bot->last_seen_velocity.k = 0.f;
		}
		else if (bot->lost_ticks > 10)
		{
			/* the same one back in sight after a while (a peek): half a
			   reaction again, and the aim to settle again, if not as far off */
			real error = skill->aim_error * bot->personality.accuracy * 0.6f;
			bot->seen_ticks = MIN(bot->seen_ticks, (long)(skill->reaction_ticks * bot->personality.reaction * 0.5f));
			bot->aim_error_yaw = (bot_random(bot) - 0.5f) * 2.f * error;
			bot->aim_error_pitch = (bot_random(bot) - 0.5f) * error;
		}
		bot->seen_ticks++;
		bot->lost_ticks = 0;
		bot->last_seen = object_get(target)->object.bounding_sphere_center;
		{
			struct object_datum *root = object_get(object_get_ultimate_parent(target));
			bot->last_seen_feet = root->object.position;
			/* (smoothed: a strafe's twitches aren't where they're going) */
			bot->last_seen_velocity.i = bot->last_seen_velocity.i * 0.7f + root->object.translational_velocity.i * 0.3f;
			bot->last_seen_velocity.j = bot->last_seen_velocity.j * 0.7f + root->object.translational_velocity.j * 0.3f;
			bot->last_seen_velocity.k = 0.f;
		}
		/* tell our side (the danger map) where they are */
		if ((bot->seen_ticks % 10) == 1)
			bot_side_saw(bot_side_of(me), target, &object_get(object_get_ultimate_parent(target))->object.position);
	}
	else if (bot->target_unit != NONE)
	{
		bot->lost_ticks++;
		/* after them where they went: a good while, the more so the more aggressive */
		if (bot->lost_ticks > (long)((3.f + 5.f * bot->personality.aggression) * BOT_TICKS_PER_SECOND) || !bot_unit_alive(bot->target_unit))
			bot->target_unit = NONE;
		else if (bot->goal_kind != _bot_goal_enemy && bot->personality.aggression > 0.2f && vehicle_index == NONE &&
			bot->personality.style != _bot_style_lurker && /* (waits for them to come back) */
			(weapon.value >= 3.5f || bot->personality.aggression > 0.7f) && !bot->goal_objective)
		{
			real_point3d ahead = bot_predict_enemy(bot, 1.f);
			if (!bot_noroute_near(bot, &ahead)) /* (not straight back to where there's no way) */
				bot_set_goal(bot, _bot_goal_enemy, &ahead, NONE, 5 * BOT_TICKS_PER_SECOND);
			/* (a score of its own: with none, the look round each second
			   would drop the chase for any wander, and it'd start again the
			   tick after, a new way there every second) */
			bot->goal_score = 8.f + 6.f * bot->personality.aggression;
		}
	}

	BOT_PROFILE_MARK("what we can't see");
	/* ---- what we can't see: the motion tracker, and gunfire we hear */
	{
		long my_datum = DATUM_INDEX_NEW(player_absolute_index, (unsigned short)((struct datum_header *)me)->identifier);
		int noise;
		/* the one we're after, gone behind something but still on the
		   tracker (or still shooting): we know where they are */
		if (target == NONE && bot->target_unit != NONE && bot_unit_alive(bot->target_unit) &&
			(bot_on_tracker(my_datum, &eye, bot->target_unit) || bot_heard(&eye, bot->target_unit, NULL)))
		{
			/* (where they are, not how long since we saw them: a blip on the
			   tracker doesn't keep us on someone for ever) */
			bot->last_seen = object_get(bot->target_unit)->object.bounding_sphere_center;
			bot->last_seen_feet = object_get(object_get_ultimate_parent(bot->target_unit))->object.position;
		}
		bot->tracked_age++;
		if (--bot->tracked_check_ticks <= 0)
		{
			struct data_iterator others;
			struct player_datum *other;
			real best = 1.0e9f;
			long found = NONE;
			real_point3d found_at;
			bot->tracked_check_ticks = 8 + (long)(bot_random(bot) * 6.f);
			data_iterator_new(&others, player_data);
			while ((other = (struct player_datum *)data_iterator_next(&others)) != NULL)
			{
				real distance;
				real heard_at;
				if (other == me || other->unit_index == NONE || other->unit_index == target || !bot_unit_alive(other->unit_index) ||
					!bot_is_enemy((short)me->team_index, other))
				{
					continue;
				}
				distance = distance3d(&object_get(other->unit_index)->object.bounding_sphere_center, &eye);
				if (distance >= best)
					continue;
				noise = FALSE;
				if (!bot_on_tracker(my_datum, &eye, other->unit_index))
				{
					if (!bot_heard(&eye, other->unit_index, &heard_at))
						continue;
					noise = TRUE; /* (heard: roughly where) */
				}
				best = distance;
				found = other->unit_index;
				found_at = object_get(other->unit_index)->object.bounding_sphere_center;
				if (noise)
				{
					found_at.x += (bot_random(bot) - 0.5f) * distance * 0.15f;
					found_at.y += (bot_random(bot) - 0.5f) * distance * 0.15f;
				}
				else if (BOT_ON(BOT_F_NOTICE))
				{
					/* (a dot on the tracker: about there) */
					found_at.x += (bot_random(bot) - 0.5f) * distance * 0.2f;
					found_at.y += (bot_random(bot) - 0.5f) * distance * 0.2f;
				}
			}
			if (found != NONE)
			{
				bot->tracked_unit = found;
				bot->tracked_at = found_at;
				bot->tracked_age = 0;
				bot_side_saw(bot_side_of(me), found, &found_at);
			}
			else if (bot->tracked_age > 5 * BOT_TICKS_PER_SECOND)
				bot->tracked_unit = NONE;
		}
		/* someone out of sight we know is there: after them, when there's
		   nothing that matters more (armed, not after an objective, and
		   not one to hang back) */
		if (target == NONE && bot->target_unit == NONE && bot->tracked_unit != NONE && bot->tracked_age < 2 * BOT_TICKS_PER_SECOND &&
			vehicle_index == NONE && !weapon.none && !bot->goal_objective && bot->personality.aggression > 0.35f &&
			(weapon.value >= 4.f || bot->personality.aggression > 0.75f) && bot->goal_kind != _bot_goal_enemy &&
			!(bot->goal_kind == _bot_goal_item && bot_goal_is_powerup(bot)) && bot_unit_alive(bot->tracked_unit))
		{
			if (!bot_noroute_near(bot, &bot->tracked_at))
				bot_set_goal(bot, _bot_goal_enemy, &bot->tracked_at, NONE, 4 * BOT_TICKS_PER_SECOND);
			bot->goal_score = 6.f + 6.f * bot->personality.aggression;
		}
	}

	BOT_PROFILE_MARK("under threat");
	/* ---- under threat? hit lately, or someone we see is looking our way */
	{
		real health = unit->object.shield_vitality + unit->object.body_vitality;
		if (health < bot->last_health - 0.01f)
		{
			/* (hit: its aim jolted, as far as it lacks the composure) */
			real jolt = skill->aim_error * (1.f - bot->personality.composure) * 2.5f;
			bot->aim_error_yaw += (bot_random(bot) - 0.5f) * 2.f * jolt;
			bot->aim_error_pitch += (bot_random(bot) - 0.5f) * jolt;
			bot->hit_ticks = 45;
		}
		else if (bot->hit_ticks > 0)
			bot->hit_ticks--;
		bot->last_health = health;
		threatened = bot->hit_ticks > 0;
		if (target != NONE)
		{
			struct unit_datum *enemy = unit_try_and_get(target);
			if (enemy)
			{
				real_vector3d toward;
				real length;
				toward.i = eye.x - enemy->object.bounding_sphere_center.x;
				toward.j = eye.y - enemy->object.bounding_sphere_center.y;
				toward.k = eye.z - enemy->object.bounding_sphere_center.z;
				length = (real)sqrt(toward.i * toward.i + toward.j * toward.j + toward.k * toward.k);
				if (length > 0.01f &&
					(enemy->unit.aiming_vector.i * toward.i + enemy->unit.aiming_vector.j * toward.j + enemy->unit.aiming_vector.k * toward.k) / length > 0.95f)
				{
					threatened = TRUE;
				}
			}
		}
	}

	BOT_PROFILE_MARK("aim");
	/* ---- aim */
	/* (going for the head or not: a fresh roll every second and a half or
	   so, the bot's own) */
	if (--bot->headshot_ticks <= 0)
	{
		bot->headshot_roll = bot_random(bot);
		bot->headshot_ticks = (long)((1.f + bot_random(bot)) * BOT_TICKS_PER_SECOND);
	}
	desired_yaw = bot->yaw;
	desired_pitch = 0.f;
	if (target != NONE)
	{
		struct object_datum *target_object = object_get(target);
		real height = weapon.precision || !weapon.automatic ? 0.18f : 0.05f;
		real_point3d aim = target_object->object.bounding_sphere_center;
		long iteration;
		real_vector3d lead;

		/* how they're moving: their vehicle's, or their own. Someone
		   weaving (strafing one way then the other, jumping) is led by how
		   they've been moving over the last half second or so, not by this
		   instant's sidestep (which will have turned round by the time the
		   shot gets there): the middle of the weave. Someone running
		   steadily comes out the same either way */
		{
			struct object_datum *root = object_get(object_get_ultimate_parent(target));
			real_vector3d now = root->object.translational_velocity;
			if (bot->aim_velocity_unit != target)
			{
				bot->aim_velocity_unit = target;
				bot->aim_velocity = now;
			}
			else
			{
				bot->aim_velocity.i += (now.i - bot->aim_velocity.i) * 0.08f;
				bot->aim_velocity.j += (now.j - bot->aim_velocity.j) * 0.08f;
				bot->aim_velocity.k += (now.k - bot->aim_velocity.k) * 0.08f;
			}
			lead.i = now.i * 0.35f + bot->aim_velocity.i * 0.65f;
			lead.j = now.j * 0.35f + bot->aim_velocity.j * 0.65f;
			lead.k = bot->aim_velocity.k * 0.5f; /* (a jump's up and down: mostly the middle of it) */
		}
		aim.z += height;
		/* shields down and a gun that rewards it (the pistol, the sniper
		   rifle): for the head, the better the bot the more often (a
		   headshot is the kill). Their eyes: where their head is, standing
		   or crouched */
		if (weapon.precision && target_object->object.shield_vitality < 0.05f &&
			TEST_FLAG(_object_mask_biped, target_object->object.type) && target_object->object.parent_object_index == NONE &&
			bot_skill_fraction() * 0.8f + 0.2f > bot->headshot_roll)
		{
			real_point3d head;
			unit_get_camera_position(target, &head);
			height += head.z - 0.04f - aim.z;
			aim.z = head.z - 0.04f;
		}
		if (weapon.projectile_speed > 0.f)
		{
			for (iteration = 0; iteration < 3; iteration++)
			{
				real flight = distance3d(&eye, &aim) / weapon.projectile_speed;
				aim.x = target_object->object.bounding_sphere_center.x + lead.i * flight;
				aim.y = target_object->object.bounding_sphere_center.y + lead.j * flight;
				aim.z = target_object->object.bounding_sphere_center.z + height + lead.k * flight;
			}
			/* Rockets: at the floor at their feet when they are near enough
			   and standing on it (the blast gets them, a step to one side
			   or not, where a rocket at the body is one they sidestep);
			   in the air, or far off, or their feet out of sight (up on a
			   ledge, behind a rise): at the body, low. */
			if (bot_name_has(unit->unit.current_weapon_index >= 0 && unit->unit.weapon_object_indices[unit->unit.current_weapon_index] != NONE ?
				object_get(unit->unit.weapon_object_indices[unit->unit.current_weapon_index])->definition_index : NONE, "rocket"))
			{
				struct object_datum *root = object_get(object_get_ultimate_parent(target));
				int at_feet = FALSE;
				if (target_distance < BOT_ROCKET_FEET && target_object->object.parent_object_index == NONE &&
					TEST_FLAG(_object_mask_biped, target_object->object.type) && fabs(root->object.translational_velocity.k) < 0.01f)
				{
					real flight = distance3d(&eye, &aim) / weapon.projectile_speed;
					real_point3d feet;
					feet.x = root->object.position.x + lead.i * flight;
					feet.y = root->object.position.y + lead.j * flight;
					feet.z = root->object.position.z + 0.08f;
					if (bot_line_clear(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit), &eye, &feet, NONE))
					{
						aim = feet;
						at_feet = TRUE;
					}
				}
				if (!at_feet)
					aim.z -= 0.3f;
			}
		}
		desired_yaw = bot_yaw_to(&eye, &aim);
		desired_pitch = (real)atan2(aim.z - eye.z, bot_distance2d(&aim, &eye));
		look_reason = _bot_look_aim;
		bot->aim_error_yaw *= skill->aim_settle;
		bot->aim_error_pitch *= skill->aim_settle;
		/* a camouflaged target: only a rough idea where they are, the aim
		   wandering round them (the further, the more) */
		if (unit_try_and_get(target) && unit_get(target)->unit.active_camouflage > 0.5f && (game_time_get() + player_absolute_index) % 8 == 0)
		{
			real spread = (0.02f + 0.9f / MAX(target_distance, 2.f)) * unit_get(target)->unit.active_camouflage * bot->personality.accuracy;
			bot->aim_error_yaw = (bot_random(bot) - 0.5f) * 2.f * spread;
			bot->aim_error_pitch = (bot_random(bot) - 0.5f) * spread;
		}
		desired_yaw = bot_wrap(desired_yaw + bot->aim_error_yaw);
		desired_pitch += bot->aim_error_pitch;
	}
	else if (bot->target_unit != NONE)
	{
		/* out of sight: aimed where they'll come out (the way they were
		   going), ready for them */
		real_point3d ahead = bot_predict_enemy(bot, 0.3f);
		desired_yaw = bot_yaw_to(&eye, &ahead);
		desired_pitch = (real)atan2(ahead.z - eye.z, MAX(0.5f, bot_distance2d(&ahead, &eye)));
		look_reason = _bot_look_expect;
	}

	/* where the target is, for the trigger: what we end up looking at can
	   be something else (the way we're driving, a ladder, a door, a lob) */
	aim_yaw = desired_yaw;
	aim_pitch = desired_pitch;

	/* ---- one of ours killed close by (teams): after whoever did it, for
	   a while, when it is the vengeful kind (one look at each death) */
	if (teams && me->team_index >= 0)
	{
		long i;
		for (i = 0; i < BOT_MAXIMUM_PLAYERS; i++)
		{
			struct player_datum *mate;
			if (i == player_absolute_index || bots_killed_time[i] <= bot->avenge_seen_time || game_time_get() - bots_killed_time[i] > BOT_SECONDS(1.f) ||
				game_time_get() < bots_killed_time[i] || bots_killer[i] == NONE || bots_killer[i] == player_absolute_index)
			{
				continue;
			}
			mate = bot_player_at_index(i);
			if (mate && mate->team_index == me->team_index && distance3d(&bots_last_position[i], &position) < 18.f &&
				bot_random(bot) < bot->personality.vengeful)
			{
				bot->avenge_player = bots_killer[i];
				bot->avenge_until = game_time_get() + BOT_SECONDS(5.f + 8.f * bot->personality.vengeful);
				if (bot_logging())
					bot_log("bot%ld t%ld after player %ld for player %ld", player_absolute_index, game_time_get(), bots_killer[i], i);
			}
		}
		bot->avenge_seen_time = game_time_get();
	}

	BOT_PROFILE_MARK("goals");
	/* ---- goals */
	if (bot->goal_ticks > 0)
		bot->goal_ticks--;
	{
		long held = unit->unit.current_weapon_index >= 0 ? unit->unit.weapon_object_indices[unit->unit.current_weapon_index] : NONE;
		long definition = held != NONE && object_try_and_get(held) ? object_get(held)->definition_index : NONE;
		if (definition != bot->last_weapon_definition)
		{
			if (bot->last_weapon_definition != 0 && definition != NONE && bot_logging())
				bot_log("bot%ld weapon %s", player_absolute_index, tag_get_name(definition));
			bot->last_weapon_definition = definition;
		}
	}
	if (vehicle_index == NONE)
	{
		int arrived = bot_distance2d(&position, &bot->goal) < 1.2f;
		int goal_gone = (bot->goal_kind == _bot_goal_item || bot->goal_kind == _bot_goal_vehicle) &&
			(bot->goal_object == NONE || !object_try_and_get(bot->goal_object) ||
				object_get(bot->goal_object)->object.parent_object_index != NONE ||
				/* (taken, and put away on its taker's back) */
				(bot->goal_kind == _bot_goal_item && !bot_item_lying(object_get(bot->goal_object))));

		/* (a thing to pick up moves too: a weapon dropped on a slope slides, one
		   knocked by a grenade flies. Where it is now, not where it was when it
		   was chosen: a bot stood three seconds at the old place.) */
		if (bot->goal_kind == _bot_goal_item && !goal_gone && distance3d(&object_get(bot->goal_object)->object.position, &bot->goal) > 0.25f)
			bot->goal = object_get(bot->goal_object)->object.position;
		if (bot->goal_kind == _bot_goal_vehicle && !goal_gone)
		{
			/* it moves; and the seat we want has its own door */
			real_point3d entry;
			if (bot_vehicle_has_enemy(bot->goal_object, me))
				goal_gone = TRUE; /* taken by them */
			else if (bot_vehicle_entry(me->unit_index, bot->goal_object, &entry))
				bot->goal = entry;
			else
				goal_gone = TRUE; /* full */
		}
		/* not getting any closer (it's up on a ledge we don't know the way
		   to, say): leave it for a while */
		if ((bot->goal_kind == _bot_goal_item || bot->goal_kind == _bot_goal_vehicle) && !bot->via_valid && !bot_goal_is_game_object(bot->goal_object))
		{
			real distance = distance3d(&position, &bot->goal);
			if (distance < bot->goal_best_distance - 0.5f)
			{
				bot->goal_best_distance = distance;
				bot->goal_progress_ticks = 0;
			}
			else if (++bot->goal_progress_ticks > (target == NONE ? 5 : 12) * BOT_TICKS_PER_SECOND)
			{
				bot_ignore(bot, bot->goal_object, 45 * BOT_TICKS_PER_SECOND);
				goal_gone = TRUE;
			}
		}
		if (goal_gone || bot->goal_kind == _bot_goal_none)
		{
			bot->goal_kind = _bot_goal_none;
			bot->goal_score = 0.f;
			bot->goal_objective = FALSE;
		}
		/* there (somewhere to go to, someone who was there): that's done
		   with, whatever time it had left (or the choice below would keep
		   it, and we'd stand about there till it ran out) */
		if (arrived && (bot->goal_kind == _bot_goal_roam || bot->goal_kind == _bot_goal_enemy))
			bot->goal_ticks = 0;
		/* (and the flag gone from our hands: not still off home with it) */
		if (bot->goal_score >= 500.f && bot->goal_objective && !bot->carrying_objective && game_engine == &ctf_engine) /* (1000 by the hunch: 740-1260) */
		{
			bot->goal_ticks = 0;
			bot->goal_score = 0.f;
		}
		if (zombie)
		{
			/* infection: a zombie wants nothing but the living, and knows
			   where they are: after the nearest (the one it is after
			   already counts as a little nearer: no turning back and forth
			   between two), with the rest of its kind */
			struct data_iterator others;
			struct player_datum *other;
			real best = 1.0e9f;
			real_point3d best_at = position;
			data_iterator_new(&others, player_data);
			while ((other = (struct player_datum *)data_iterator_next(&others)) != NULL)
			{
				real_point3d where;
				real distance;
				if (other == me || other->unit_index == NONE || !bot_unit_alive(other->unit_index) ||
					!bot_is_enemy((short)me->team_index, other))
				{
					continue;
				}
				where = object_get(object_get_ultimate_parent(other->unit_index))->object.position;
				distance = distance3d(&where, &position) + 3.f * (real)fabs(where.z - position.z);
				if (bot->goal_kind == _bot_goal_enemy && distance3d(&where, &bot->goal) < 2.5f)
					distance *= 0.7f;
				if (distance < best)
				{
					best = distance;
					best_at = where;
				}
			}
			if (best < 1.0e8f)
			{
				if (bot->goal_kind != _bot_goal_enemy || distance3d(&bot->goal, &best_at) > 1.f || bot->goal_ticks < BOT_TICKS_PER_SECOND)
					bot_set_goal(bot, _bot_goal_enemy, &best_at, NONE, 4 * BOT_TICKS_PER_SECOND);
				bot->goal_objective = FALSE;
				bot->goal_score = 1000.f;
			}
			else if (bot->goal_kind == _bot_goal_none || bot->goal_ticks <= 0 || arrived)
				bot_choose_goal(bot, player_absolute_index, me, unit, &position, teams); /* (nobody alive just now: about the place) */
		}
		else
		if (bot->goal_kind == _bot_goal_none || bot->goal_ticks <= 0 ||
			(arrived && bot->goal_kind != _bot_goal_item && bot->goal_kind != _bot_goal_vehicle && bot->goal_kind != _bot_goal_hold) ||
			/* and a look round every second: something better may have come up */
			(game_time_get() + player_absolute_index * 7) % BOT_TICKS_PER_SECOND == 0)
		{
			bot_choose_goal(bot, player_absolute_index, me, unit, &position, teams);
		}
	}

	/* testing: HALO_BOT_GOTO=x,y,z sends every bot there */
	if (bot_env_set("HALO_BOT_GOTO"))
	{
		real_point3d there;
		sscanf(getenv("HALO_BOT_GOTO"), "%f,%f,%f", &there.x, &there.y, &there.z);
		if (getenv("HALO_BOT_GOTO2") && game_time_get() > (getenv("HALO_BOT_GOTO2_AT") ? atol(getenv("HALO_BOT_GOTO2_AT")) : 25) * BOT_TICKS_PER_SECOND)
			sscanf(getenv("HALO_BOT_GOTO2"), "%f,%f,%f", &there.x, &there.y, &there.z);
		if (bot->goal_kind != _bot_goal_roam || distance3d(&bot->goal, &there) > 0.5f)
			bot_set_goal(bot, _bot_goal_roam, &there, NONE, 1000000);
		bot->goal_objective = TRUE;
		bot->goal_score = 1000.f;
		target = NONE;
		if (game_time_get() % 30 == 0)
			bot_log("goto bot%ld t%ld at %.2f %.2f %.2f area %ld reach %ld corridor %ld", player_absolute_index, game_time_get(), position.x, position.y, position.z,
				bot->mover.move.area, bot->mover.move.reach, bot->mover.move.corridor_count);
	}

	BOT_PROFILE_MARK("a powerup right he");
	/* ---- a powerup right here (an overshield, camouflage), or health when
	   hurt: take it now, fight or no fight (it wins the fight) */
	if (vehicle_index == NONE && !zombie && (game_time_get() + player_absolute_index) % 6 == 0 && !bot_goal_is_powerup(bot) &&
		!(bot->goal_kind == _bot_goal_item && bot_goal_is_game_object(bot->goal_object)) && !bot_holds_named(unit, "flag") &&
		!bot_holds_named(unit, "ball"))
	{
		struct object_iterator nearby;
		struct object_datum *item;
		object_iterator_new(&nearby, _object_mask_equipment, 0);
		while ((item = (struct object_datum *)object_iterator_next(&nearby)) != NULL)
		{
			real value;
			real d;
			if (!bot_item_lying(item) || bot_ignoring(bot, nearby.index))
				continue;
			if (bot->goal_kind == _bot_goal_item && bot->goal_object == nearby.index)
				continue; /* (on the way to it already: setting it again would reset the check that it's getting anywhere) */
			d = distance3d(&item->object.position, &position);
			if (d > 6.f || fabs(item->object.position.z - position.z) > 1.5f)
				continue;
			value = bot_item_value(item->definition_index);
			if (value >= 7.f && (unit->object.shield_vitality > 1.05f || unit->unit.active_camouflage > 0.5f))
				continue; /* (powered up already) */
			if (value < 7.f && !(bot_name_has(item->definition_index, "health") && unit->object.body_vitality < 0.6f))
				continue;
			if (aas_ready())
			{
				float time = bot_travel_time(&body, AAS_FOOT, &item->object.position);
				if (time < 0.f || time > 3.f)
					continue; /* (no way there at all is no grab either) */
			}
			bot_set_goal(bot, _bot_goal_item, &item->object.position, nearby.index, 6 * BOT_TICKS_PER_SECOND);
			bot->goal_objective = TRUE;
			bot->goal_score = 50.f;
			if (bot_logging())
				bot_log("bot%ld t%ld grabs %s %.1f away", player_absolute_index, game_time_get(), tag_get_name(item->definition_index), d);
			break;
		}
	}

	/* ---- a poor gun in our hands and a proper one lying a few steps off:
	   over to it now, whatever we were about (the hill, a hunt, a fight not
	   yet on top of us; not with the flag or the ball, nor on the way to
	   one: that does not wait). From further off when a poor gun is all we
	   have; with a good one beside it, only when there is nobody to fight. */
	if (vehicle_index == NONE && !zombie && (game_time_get() + player_absolute_index * 3) % 15 == 0 && !bot_goal_is_powerup(bot) &&
		!(bot->goal_kind == _bot_goal_item && bot->goal_ticks > 0 && (bot_goal_is_game_object(bot->goal_object) || bot->goal_score >= 50.f)) &&
		!bot->carrying_objective && !committed)
	{
		long worst_slot = NONE;
		real worst = bot_worst_held(unit, &worst_slot);
		int all_poor = bot_all_held_poor(unit);
		int full_hands = bot_weapons_held(unit) >= 2;
		if (all_poor || (target == NONE && full_hands && worst_slot != NONE &&
			bot_weapon_is_poor(unit->unit.weapon_object_indices[worst_slot]) && !bot_slot_is_favorite(bot, unit, worst_slot)))
		{
			struct object_iterator nearby;
			struct object_datum *item;
			long best_item = NONE;
			/* (on the game's business: only one that is right by the way) */
			int busy = bot_on_objective(bot);
			real best_d = all_poor && !busy ? 12.f : 7.f;
			object_iterator_new(&nearby, _object_mask_weapon, 0);
			while ((item = (struct object_datum *)object_iterator_next(&nearby)) != NULL)
			{
				real d;
				int nearer = FALSE;
				if (!bot_item_lying(item) || bot_ignoring(bot, nearby.index))
					continue;
				d = distance3d(&item->object.position, &position);
				if (d >= best_d || fabs(item->object.position.z - position.z) > 1.5f)
					continue;
				if (!bot_weapon_is_proper(nearby.index) || bot_holds_weapon_type(unit, item->definition_index) ||
					(full_hands && bot_weapon_worth(nearby.index) <= worst))
				{
					continue;
				}
				/* (one of the others nearer to it and after it: theirs) */
				if (bot_goal_takers(nearby.index, player_absolute_index, &position, &nearer) > 0 && nearer)
					continue;
				if (aas_ready())
				{
					float time = bot_travel_time(&body, AAS_FOOT, &item->object.position);
					if (time < 0.f || time > (all_poor && !busy ? 4.f : 2.5f))
						continue;
				}
				best_item = nearby.index;
				best_d = d;
			}
			if (best_item != NONE && !(bot->goal_kind == _bot_goal_item && bot->goal_object == best_item && bot->goal_ticks > 0))
			{
				item = object_get(best_item);
				bot_set_goal(bot, _bot_goal_item, &item->object.position, best_item, 6 * BOT_TICKS_PER_SECOND);
				bot->goal_objective = TRUE;
				bot->goal_score = 50.f;
				bot->grab_object = best_item;
				if (bot_logging())
					bot_log("bot%ld t%ld grabs %s %.1f away (a better gun)", player_absolute_index, game_time_get(), tag_get_name(item->definition_index), best_d);
			}
		}
	}

	BOT_PROFILE_MARK("rubbing it in");
	/* ---- rubbing it in: a kill, nobody else about, and a bot that likes
	   to gloat steps over to the body and teabags it (two or three dips,
	   each right down and up again so that it reads as what it is, a second
	   or so in all, then on with the game; only a body a few steps away,
	   never with the flag or the ball, never under fire). It was up to
	   three seconds of walking and three of standing over the body with a
	   bob too quick to see: a bot that stopped and looked at the floor. */
	if (zombie)
		bot->teabag_pending = FALSE;
	if (bot->teabag_pending)
	{
		struct object_datum *victim = bot->last_fired_unit != NONE ? object_try_and_get(bot->last_fired_unit) : NULL;
		bot->teabag_pending = FALSE;
		if (vehicle_index == NONE && target == NONE && !bot_on_objective(bot) && bot->hit_ticks <= 0 &&
			bot_random(bot) < 0.7f * bot->personality.taunt * (0.5f + 0.5f * (1.f - bot->personality.focus)) *
				(1.f + 0.4f * bot->personality.swagger * MIN(bot->personality.kill_streak, 3)))
		{
			bot->teabag_spot = victim ? victim->object.position : bot->last_seen_feet;
			if (distance3d(&bot->teabag_spot, &position) < BOT_TEABAG_REACH && fabs(bot->teabag_spot.z - position.z) < 1.f)
			{
				bot->teabag_ticks = BOT_TEABAG_DIP_TICKS * (bot_random(bot) < 0.5f ? 2 : 3);
				bot->teabag_walk_ticks = 0;
				if (bot_logging())
					bot_log("bot%ld t%ld teabags %.1f away", player_absolute_index, game_time_get(), distance3d(&bot->teabag_spot, &position));
			}
		}
	}
	if (bot->teabag_ticks > 0 && (target != NONE || bot->hit_ticks > 0 || bot->carrying_objective || vehicle_index != NONE ||
		distance3d(&bot->teabag_spot, &position) > BOT_TEABAG_REACH + 3.f))
		bot->teabag_ticks = 0;

	/* in the fight: the flank (if any) has done its job */
	if (target != NONE && bot->goal_kind == _bot_goal_enemy)
	{
		bot->via_valid = FALSE;
		bot->via_plan.samples = 0;
	}

	BOT_PROFILE_MARK("movement");
	/* ---- movement */
	move_yaw = bot->yaw;
	if (vehicle_index != NONE && !driving)
	{
		/* a passenger or gunner: aim and shoot, nothing else */
		moving = FALSE;
		if (target == NONE && bot->target_unit == NONE)
		{
			/* look around a little */
			desired_yaw = bot_glance(bot, me, &position);
			look_reason = _bot_look_glance;
			if (--bot->strafe_ticks <= 0)
			{
				bot->strafe_ticks = 60 + (long)(bot_random(bot) * 60);
				bot->strafe_sign = bot_random(bot) < 0.5f ? -1.f : 1.f;
			}
		}
		/* get out when the vehicle has no driver and no one to shoot */
		if (unit_scripting_unit_driver(vehicle_index) == NONE && target == NONE && bot_random(bot) < 0.01f)
			action->control_flags |= FLAG(_unit_control_action_bit);
		/* in the wrong seat (a passenger with the wheel or the gun free):
		   out, and in again at the right door; twice at most */
		if (!gunner && bot->seat_retries < 2 && ++bot->seat_check_ticks > 20)
		{
			struct object_datum *vehicle = object_get(vehicle_index);
			real speed = (real)sqrt(vehicle->object.translational_velocity.i * vehicle->object.translational_velocity.i +
				vehicle->object.translational_velocity.j * vehicle->object.translational_velocity.j);
			real_point3d entry;
			int rank = 99;
			bot->seat_check_ticks = 0;
			/* the wheel (rank 0 when nobody drives) or the gun (rank 0 when someone does) */
			if (speed < 0.05f && bot_vehicle_entry_rank(me->unit_index, vehicle_index, &entry, &rank) && rank == 0)
			{
				bot->seat_retries++;
				action->control_flags |= FLAG(_unit_control_action_bit);
				/* (a goal like any other: its closest-yet and progress start
				   afresh, not the last goal's) */
				bot_set_goal(bot, _bot_goal_vehicle, &entry, vehicle_index, 10 * BOT_TICKS_PER_SECOND);
				bot->goal_objective = TRUE;
				bot->goal_score = 20.f;
			}
		}
		/* at their flag: out, and take it */
		{
			real_point3d errand;
			int get_out = FALSE;
			if (teams && bot_ctf_drive_target(bot, me, &errand, &get_out) && get_out && distance3d(&errand, &position) < 7.f)
				action->control_flags |= FLAG(_unit_control_action_bit);
		}
	}
	else if (vehicle_index != NONE && driving)
	{
		/* drive: out to where the enemy is, then circle them so our gun
		   (a tank's, a ghost's) or our gunner's (a warthog's) keeps on
		   them; a warthog with nobody on the gun runs people over */
		real_point3d next;
		int driver_shoots = bot_driver_shoots(vehicle_index);
		int has_gunner = FALSE;
		long waiting = teams ? bot_riders_coming(vehicle_index, player_absolute_index) : 0;
		real_point3d enemy;
		int know_enemy = FALSE;
		real enemy_distance = 1.0e9f;
		int circling = FALSE;

		{
			/* someone on the gun? */
			struct data_iterator riders;
			struct player_datum *rider;
			data_iterator_new(&riders, player_data);
			while ((rider = (struct player_datum *)data_iterator_next(&riders)) != NULL)
			{
				struct unit_datum *rider_unit = rider != me && rider->unit_index != NONE ? unit_try_and_get(rider->unit_index) : NULL;
				if (rider_unit && rider_unit->object.parent_object_index != NONE &&
					object_get_ultimate_parent(rider->unit_index) == vehicle_index &&
					unit_seat_is_gunner(rider_unit->object.parent_object_index, rider_unit->unit.parent_seat_index))
				{
					has_gunner = TRUE;
				}
			}
		}
		if (target != NONE)
		{
			enemy = object_get(object_get_ultimate_parent(target))->object.position;
			know_enemy = TRUE;
		}
		else if (bot->target_unit != NONE)
		{
			enemy = bot->last_seen;
			know_enemy = TRUE;
		}
		else
		{
			/* the nearest enemy anywhere: drivers go hunting */
			struct data_iterator others;
			struct player_datum *other;
			data_iterator_new(&others, player_data);
			while ((other = (struct player_datum *)data_iterator_next(&others)) != NULL)
			{
				real distance;
				real_point3d at;
				if (other == me || other->unit_index == NONE || !bot_unit_alive(other->unit_index) ||
					!bot_is_enemy((short)me->team_index, other))
				{
					continue;
				}
				at = object_get(object_get_ultimate_parent(other->unit_index))->object.position;
				distance = distance3d(&at, &position);
				if (distance < enemy_distance)
				{
					enemy_distance = distance;
					enemy = at;
					know_enemy = TRUE;
				}
			}
		}
		if (know_enemy)
			enemy_distance = distance3d(&enemy, &position);
		/* a race: the next checkpoint, and never mind anyone */
		{
			real_point3d checkpoint;
			if (game_engine == &race_engine &&
				race_bot_next_checkpoint(DATUM_INDEX_NEW(player_absolute_index, (unsigned short)((struct datum_header *)me)->identifier),
					&position, &checkpoint))
			{
				enemy = checkpoint;
				enemy_distance = 1.0e9f;
				know_enemy = TRUE;
			}
		}
		/* capture the flag: the flag first; fight on the way only up close */
		{
			real_point3d errand;
			int get_out = FALSE;
			if (teams && bot_ctf_drive_target(bot, me, &errand, &get_out) && !(target != NONE && target_distance < 12.f))
			{
				if (get_out && distance3d(&errand, &position) < 7.f)
					action->control_flags |= FLAG(_unit_control_action_bit); /* out, and take it */
				enemy = errand;
				/* the flag lying there: route to it, don't circle; our flag's
				   carrier: after them, and run them down or circle them close */
				enemy_distance = get_out ? 1.0e9f : distance3d(&errand, &position);
				know_enemy = TRUE;
			}
		}

		if (know_enemy && enemy_distance < 28.f)
		{
			/* close: circle them (or ram) */
			real radius = driver_shoots ? (bot_name_has(object_get(vehicle_index)->definition_index, "scorpion") ? 18.f : 10.f) : 13.f;
			real around = bot_yaw_to(&enemy, &position);
			real_point3d point;
			long attempt;

			if (!driver_shoots && !has_gunner && target != NONE)
				radius = 0.f; /* run them down */
			if (bot->circle_sign == 0.f || bot->circle_target != target)
			{
				bot->circle_sign = bot_random(bot) < 0.5f ? -1.f : 1.f;
				bot->circle_target = target;
			}
			for (attempt = 0; attempt < 2; attempt++)
			{
				real angle = bot_wrap(around + bot->circle_sign * 0.8f);
				point.x = enemy.x + (real)cos(angle) * radius;
				point.y = enemy.y + (real)sin(angle) * radius;
				point.z = enemy.z;
				if (radius == 0.f || bot_body_clear(&position, &point, 1.f, 1.f, vehicle_index))
					break;
				bot->circle_sign = -bot->circle_sign; /* something in the way: the other way round */
			}
			/* a gun that points where we drive: drive at them, and past
			   (a ghost) or stop at range (a tank) */
			if (driver_shoots && target != NONE && enemy_distance > radius * 0.7f)
				point = enemy;
			else if (driver_shoots && target != NONE && bot_name_has(object_get(vehicle_index)->definition_index, "scorpion"))
				point = position;
			bot->goal = point;
			bot->goal_kind = _bot_goal_enemy;
			bot->goal_ticks = BOT_TICKS_PER_SECOND;
			next = point;
			circling = TRUE;
		}
		else
		{
			if (bot->goal_ticks <= 0 || bot_distance2d(&position, &bot->goal) < 8.f ||
				(bot->goal_kind != _bot_goal_roam && bot->goal_kind != _bot_goal_enemy) ||
				(know_enemy && bot->goal_kind != _bot_goal_enemy) ||
				(know_enemy && distance3d(&bot->drive_enemy_at, &enemy) > 5.f)) /* (they've moved on: after where they are now) */
			{
				bot->drive_enemy_at = enemy; /* (as they were: the goal itself is snapped onto the areas) */
				if (know_enemy)
					bot_set_goal(bot, _bot_goal_enemy, &enemy, NONE, 6 * BOT_TICKS_PER_SECOND);
				else
				{
					/* somewhere well away */
					real_point3d point = position;
					long attempt;
					for (attempt = 0; attempt < 12 && nav_place_count() > 0; attempt++)
					{
						point = nav_random_place(bot_random(bot));
						if (bot_distance2d(&point, &position) > 35.f)
							break;
					}
					bot_set_goal(bot, _bot_goal_roam, &point, NONE, 25 * BOT_TICKS_PER_SECOND);
				}
			}
			next = bot->goal;
		}
		/* over the vehicle areas (a vehicle's width off walls, no stairs,
		   no jumps): the route pulled straight, round corners, slowing for
		   sharp ones, backing out when stuck, never over our own people */
		bot_go(bot, &body, &next, _bot_move_goal_position, circling ? 1.5f : 3.f, &result);
		driving_moved = TRUE;
		if (result.no_route || result.get_out)
		{
			/* no way to drive there: close to something we're after, get
			   out and walk; otherwise somewhere else next time */
			if (result.get_out || (bot->goal_objective && bot_distance2d(&position, &bot->goal) < 30.f))
				action->control_flags |= FLAG(_unit_control_action_bit);
			if (!circling)
				bot->goal_ticks = 0;
			/* an errand a vehicle can't get to (the flag inside their base):
			   as close as it gets, then on foot (and leave this one be a
			   while) */
			if (!circling && result.no_route && bot->goal_objective)
			{
				real_point3d dropoff;
				if (nav_vehicle_dropoff(&position, &next, &dropoff) && bot_distance2d(&position, &dropoff) > 8.f)
				{
					memset(&result, 0, sizeof(result));
					bot_go(bot, &body, &dropoff, _bot_move_goal_position, 4.f, &result);
				}
				if ((result.no_route || !result.moving) && ++bot->drive_stranded_ticks > BOT_TICKS_PER_SECOND)
				{
					action->control_flags |= FLAG(_unit_control_action_bit);
					bot_ignore(bot, vehicle_index, 45 * BOT_TICKS_PER_SECOND);
					bot->drive_stranded_ticks = 0;
				}
			}
		}
		if (!result.no_route)
			bot->drive_stranded_ticks = 0;
		move_yaw = result.moving ? result.steer_yaw : move_yaw;
		drive_throttle = result.throttle;
		moving = result.moving && (waiting == 0 || bot->goal_progress_ticks > 5 * BOT_TICKS_PER_SECOND);
		if (waiting)
			bot->goal_progress_ticks++; /* don't wait forever */
		else
			bot->goal_progress_ticks = 0;
		/* a vehicle steers toward where its driver looks: look where we're
		   going, at an enemy only when our own gun can hit them ahead */
		if (!(driver_shoots && target != NONE && fabs(bot_angle_difference(desired_yaw, move_yaw)) < 1.f))
		{
			/* (an aircraft climbs and dives as its pilot looks up and
			   down: what the flying asked for, bot_move.c) */
			desired_yaw = move_yaw;
			desired_pitch = result.look ? result.look_pitch : 0.f;
			look_reason = _bot_look_steer;
		}
		if (!driver_shoots)
			target_distance = 1.0e9f; /* nothing to shoot with */
	}
	else if (bot->teabag_ticks > 0 && !committed)
	{
		real off = bot_distance2d(&position, &bot->teabag_spot);
		bot->teabag_ticks--;
		if (off > 0.6f)
		{
			bot_go(bot, &body, &bot->teabag_spot, _bot_move_goal_position, 0.5f, &result);
			moved = TRUE;
			move_yaw = result.yaw;
			path_yaw = result.path_yaw;
			moving = result.moving;
			move_speed = result.speed;
			/* (the clock starts once it's there; getting there gets three
			   seconds, and none if there's no way) */
			bot->teabag_ticks++;
			if (result.no_route || off > BOT_TEABAG_REACH + 2.f || ++bot->teabag_walk_ticks > BOT_SECONDS(1.5f))
				bot->teabag_ticks = 0;
		}
		else
		{
			/* down, up, down, up: looking at them */
			moving = FALSE;
			desired_yaw = bot_yaw_to(&eye, &bot->teabag_spot);
			desired_pitch = -0.9f;
			look_reason = _bot_look_act;
			look_forced = TRUE;
			/* (a dip: down for most of it, then up) */
			if (bot->teabag_ticks % BOT_TEABAG_DIP_TICKS >= BOT_TEABAG_DIP_TICKS - BOT_TEABAG_DOWN_TICKS)
				action->control_flags |= FLAG(_unit_control_crouch_modifier_bit);
		}
	}
	else if (zombie && target != NONE && target_distance < 5.f && !committed &&
		(!aas_ready() || aas_line_in_areas(AAS_FOOT, &position, &object_get(object_get_ultimate_parent(target))->object.position)))
	{
		/* infection: one of the living in sight and a clear run to them:
		   straight at them, with no thought for cover or for keeping a
		   distance (the route takes it the rest of the way) */
		real_point3d at = object_get(object_get_ultimate_parent(target))->object.position;
		real_vector3d const *velocity = &object_get(object_get_ultimate_parent(target))->object.translational_velocity;
		/* (where they will be when we get there, not where they are) */
		real lead = MIN(target_distance / (AAS_FOOT_SPEED_BOT * body.speed_scale), 0.6f) * BOT_TICKS_PER_SECOND;
		at.x += velocity->i * lead;
		at.y += velocity->j * lead;
		move_yaw = bot_yaw_to(&position, &at);
		moving = TRUE;
		move_speed = 1.f;
	}
	else if (target != NONE && !(weapon.none && target_distance > 2.f) /* a flag carrier runs for home */ && !committed &&
		/* an objective (a flag, a vehicle, a weapon) comes first: we shoot on
		   the way; only someone right on top of us, when we're armed and
		   it's no flag run, makes us stand and fight */
		((!bot->goal_objective &&
			/* exploring, hunting: someone far off who isn't onto us is shot
			   at on the way, not stopped for */
			!(bot->goal_kind == _bot_goal_roam && !bot->goal_squad && bot_distance2d(&position, &bot->goal) > 3.f &&
				target_distance > (threatened ? 18.f : 12.f) * (0.7f + 0.6f * bot->personality.aggression))) ||
			/* a weapon or a vehicle: armed, fight anyone close enough to
			   hit; armed badly, get the weapon unless they're on top of us */
			/* (an overshield or camouflage: grab it first, shooting on the way,
			   unless they're right there; the greedy let them come closer) */
			((bot->goal_kind == _bot_goal_vehicle || (bot->goal_kind == _bot_goal_item && !bot_goal_is_game_object(bot->goal_object))) &&
				(bot_goal_is_powerup(bot) ? target_distance < 1.5f :
				/* (a power weapon, the rockets say: worth more than this
				   fight; get it, shooting on the way, then come back) */
				bot_goal_is_power_weapon(bot) ? target_distance < 2.5f :
				/* (a proper gun for the poor one in our hands, a few steps
				   off: the same; this fight is better had with it) */
				bot->goal_object == bot->grab_object && bot->goal_score >= 50.f && bot_weapon_is_poor(unit->unit.current_weapon_index >= 0 &&
					unit->unit.current_weapon_index < MAXIMUM_WEAPONS_PER_UNIT ? unit->unit.weapon_object_indices[unit->unit.current_weapon_index] : NONE) ?
					target_distance < 2.5f :
				weapon.value >= 4.5f ? target_distance < 9.f : target_distance < 6.f)) ||
			/* a flag, the hill, a carrier: fight whoever's in the way
			   when they're close; carrying it, never stop */
			(target_distance < 7.f && bot->goal_score < 100.f && !weapon.none &&
				bot->goal_kind != _bot_goal_vehicle && !(bot->goal_kind == _bot_goal_item && !bot_goal_is_game_object(bot->goal_object)) &&
				/* (a loose oddball: straight for it, shooting on the way:
				   holding it is how the game's won) */
				!(game_engine == &oddball_engine && bot->goal_kind == _bot_goal_item && bot_goal_is_game_object(bot->goal_object))) ||
			/* there already (holding the hill, by our flag, with the carrier):
			   fight there, not leaving it */
			(bot->goal_kind != _bot_goal_item && bot->goal_kind != _bot_goal_vehicle && !weapon.none && bot->goal_score < 100.f &&
				bot_distance2d(&position, &bot->goal) < 5.f)))
	{
		/* fight on foot: keep a distance that suits the weapon, and always
		   on the move (a still target is an easy one): strafing one way then
		   the other, the more skilled the more often and the less
		   predictably, jumping and now and then crouch jumping */
		real to_target = bot_yaw_to(&position, &bot->last_seen);
		real preferred = weapon.preferred_distance * (1.4f - 0.8f * bot->personality.aggression);
		real forward = 0.f;
		int hurt = unit->object.shield_vitality < 0.2f && unit->object.body_vitality < 0.5f;
		real skill_move = bot_skill_movement();
		int closing = FALSE; /* in for a blow */

		/* how it fights: the rusher in close (not with a gun that wants
		   room), the lurker a little further back than most */
		if (bot->personality.style == _bot_style_rusher && !weapon.precision && weapon.kind != _bot_gun_rocket && weapon.kind != _bot_gun_fuel_rod)
			preferred *= 0.45f;
		else if (bot->personality.style == _bot_style_lurker)
			preferred *= 1.15f;
		/* in for a blow: their shields are down and it is its habit, or it
		   likes a blow; or their back is to us and it likes one enough to
		   go for it (the further, the more it likes them) */
		if (target != NONE && !hurt && !zombie && object_try_and_get(target) && TEST_FLAG(_object_mask_biped, object_get(target)->object.type) &&
			object_get(target)->object.parent_object_index == NONE && fabs(object_get(target)->object.position.z - position.z) < 1.f &&
			target_distance < 2.5f + 4.f * bot->personality.melee &&
			((object_get(target)->object.shield_vitality < 0.15f &&
				(bot->personality.signature == _bot_signature_melee_finish || bot->personality.melee > 0.6f)) ||
			 (bot->personality.melee > 0.5f && bot_unit_looking_away(target, &eye))))
		{
			preferred = 0.8f;
			closing = TRUE;
		}

		if (--bot->strafe_ticks <= 0)
		{
			/* how often it changes direction: the skill level and its own
			   agility (a nimble bot jinks every few tenths of a second, a
			   stolid one sidesteps one way a long while) */
			real skill = skill_move * (0.3f + 0.7f * bot->personality.agility);
			bot->strafe_ticks = BOT_SECONDS((0.3f + 0.9f * bot_random(bot)) * (2.2f - 1.6f * skill) * (threatened ? 0.7f : 1.f));
			if (BOT_ON(BOT_F_HUMAN))
			{
				/* as a person strafes: a side held a good half second or
				   more, most times; now and then a long run one way, now
				   and then a quick step back and forth (the nimble more);
				   never a twitch of a tick or two */
				real kind = bot_random(bot);
				real jiggle = 0.1f + 0.2f * skill;
				real seconds = kind < jiggle ? 0.2f + 0.15f * bot_random(bot) :
					kind < jiggle + 0.25f ? 0.9f + 0.9f * bot_random(bot) : 0.4f + 0.5f * bot_random(bot);
				bot->strafe_ticks = BOT_SECONDS(seconds * (1.5f - 0.5f * skill) * (threatened ? 0.85f : 1.f));
			}
			bot->strafe_sign = bot_random(bot) < 0.5f ? -1.f : 1.f;
			/* (the least skilled don't always strafe: they walk at them) */
			if (bot_random(bot) > 0.35f + 0.65f * skill)
				bot->strafe_sign = 0.f;
		}
		/* the upper hand: now and then look for higher ground close by that
		   still sees them (up a ledge, a ladder, over a gap for the daring),
		   and fight from there */
		if ((game_time_get() + player_absolute_index * 3) % 20 == 0)
		{
			bot->fight_spot_valid = FALSE;
			if (!hurt)
				bot->fight_spot_valid = bot_higher_ground(bot, &position, &bot->last_seen, me->unit_index, &bot->fight_spot);
		}
		/* hurt, and a health pack close by: get it, shooting on the way */
		int wants_health = unit->object.body_vitality < 0.35f || (unit->object.body_vitality < 0.6f && bot->personality.aggression < 0.6f);
		if (wants_health && --bot->health_check_ticks <= 0)
		{
			bot->health_check_ticks = 15;
			bot->health_object = bot_find_health(&position, 10.f + 10.f * (1.f - unit->object.body_vitality));
		}
		if (wants_health && bot->health_object != NONE && object_try_and_get(bot->health_object) &&
			bot_item_lying(object_get(bot->health_object)))
		{
			bot_go(bot, &body, &object_get(bot->health_object)->object.position, _bot_move_goal_item, 0.3f, &result);
			moved = TRUE;
			if (result.moving)
			{
				move_yaw = result.yaw;
				move_speed = result.speed;
				moving = TRUE;
				if (!result.look && result.travel == _aas_travel_walk && !bot_move_committed(&bot->mover) && bot->strafe_sign != 0.f &&
					bot_move_direction_safe(&body, bot_wrap(move_yaw + bot->strafe_sign * 0.5f), 0.8f))
				{
					move_yaw = bot_wrap(move_yaw + bot->strafe_sign * 0.5f);
				}
			}
			else
			{
				moved = FALSE;
				bot->health_object = NONE;
			}
		}
		else
			bot->health_object = NONE;
		/* shields gone and losing the trade: into cover till they're back
		   (a moment behind a wall beats dying in the open). The reckless
		   and those carrying the game's objective push on */
		{
			real mine = unit->object.shield_vitality;
			int losing = target != NONE && object_try_and_get(target) &&
				object_get(target)->object.shield_vitality > mine + 0.3f;
			/* (they're nearly done too: finish them instead) */
			int finishing = target != NONE && object_try_and_get(target) &&
				object_get(target)->object.shield_vitality < 0.3f && object_get(target)->object.body_vitality < 0.7f;
			int broken = mine < 0.1f && unit->object.body_vitality < 0.55f && losing && !finishing;
			/* (hit and run: off as soon as its shields are going, whoever
			   is winning, and back when they are up) */
			int breaking_off = bot->personality.style == _bot_style_hit_and_run && mine < 0.4f && !finishing && !closing &&
				!bot_on_objective(bot); /* (its way of fighting is for its own time) */
			if (!bot->in_cover && (broken || breaking_off) && !bot_holds_named(unit, "flag") && !bot_holds_named(unit, "ball") &&
				(breaking_off || bot->personality.aggression < 0.45f + 0.25f * bot->personality.patience) &&
				--bot->cover_check_ticks <= 0)
			{
				bot->cover_check_ticks = 10;
				if (bot_find_cover(bot, &body, &bot->last_seen, me->unit_index, &bot->cover_spot))
				{
					bot->in_cover = TRUE;
					bot->cover_until = game_time_get() + BOT_SECONDS(3.f + 4.f * bot->personality.patience);
					if (bot_logging())
						bot_log("bot%ld t%ld cover: shields %.2f body %.2f, %.1f away", player_absolute_index, game_time_get(),
							mine, unit->object.body_vitality, bot_distance2d(&position, &bot->cover_spot));
				}
			}
			if (bot->in_cover && (mine > 0.7f || game_time_get() > bot->cover_until ||
				(unit->object.body_vitality < 0.2f && target_distance < 3.f)))
			{
				if (bot_logging())
					bot_log("bot%ld t%ld out of cover: shields %.2f body %.2f", player_absolute_index, game_time_get(), mine,
						unit->object.body_vitality);
				bot->in_cover = FALSE; /* back in */
			}
		}
		if (bot->in_cover && !moved)
		{
			bot_go(bot, &body, &bot->cover_spot, _bot_move_goal_position, 0.5f, &result);
			moved = TRUE;
			if (result.moving)
			{
				move_yaw = result.yaw;
				move_speed = result.speed;
				moving = TRUE;
			}
			else if (bot_distance2d(&position, &bot->cover_spot) > 1.2f)
				bot->in_cover = FALSE; /* no way there after all */
			else
			{
				/* there: crouch, and wait (and shoot anyone who comes round) */
				moving = TRUE;
				move_speed = 0.f;
				action->control_flags |= FLAG(_unit_control_crouch_modifier_bit);
			}
		}
		else if (hurt && bot->personality.aggression < 0.35f)
			forward = -1.f; /* back off to recharge */
		else if (target_distance > preferred * 1.3f)
			forward = 1.f;
		/* too close for the gun: a sniper or a rocket backs off; anything
		   else stands its ground (strafing), it doesn't turn tail */
		else if ((target_distance < preferred * 0.5f && preferred >= 15.f) ||
			(weapon.kind == _bot_gun_rocket && target_distance < BOT_ROCKET_CLOSEST + 0.7f))
			forward = -1.f;
		/* its habit: backing away, firing, when it is being hit and they
		   are no further off than it likes */
		if (bot->personality.signature == _bot_signature_backpedal && threatened && !closing && !bot->in_cover &&
			target_distance < preferred * 1.1f && forward >= 0.f)
		{
			forward = -1.f;
		}
		if (forward == 0.f && bot->strafe_sign == 0.f)
			forward = 0.4f;
		if (!moved && bot->fight_spot_valid && bot_distance2d(&position, &bot->fight_spot) > 0.7f && !hurt)
		{
			/* up there (the route: its jumps, its ladder), weaving on the way */
			bot_go(bot, &body, &bot->fight_spot, _bot_move_goal_position, 0.6f, &result);
			moved = TRUE;
			if (result.moving)
			{
				move_yaw = result.yaw;
				move_speed = result.speed;
				if (!result.look && result.travel == _aas_travel_walk && !bot_move_committed(&bot->mover) && bot->strafe_sign != 0.f &&
					bot_move_direction_safe(&body, bot_wrap(move_yaw + bot->strafe_sign * 0.5f), 0.8f))
				{
					move_yaw = bot_wrap(move_yaw + bot->strafe_sign * 0.5f);
				}
				moving = TRUE;
			}
			else
				bot->fight_spot_valid = FALSE;
		}
		if (!moved && forward > 0.f && aas_ready() && !aas_line_in_areas(AAS_FOOT, &position, &bot->last_seen))
		{
			/* closing in, and no straight way: the route */
			real_point3d there = bot->last_seen;
			there.z -= 0.5f;
			bot_go(bot, &body, &there, _bot_move_goal_position, preferred * 0.8f, &result);
			moved = TRUE;
			if (result.moving)
			{
				move_yaw = result.yaw;
				move_speed = result.speed;
				moving = TRUE;
			}
			else
			{
				moved = FALSE;
				forward = 0.f;
			}
		}
		if (!moving)
		{
			move_yaw = bot_wrap(to_target + (real)atan2(bot->strafe_sign * 0.8f, forward));
			moving = TRUE;
			/* not shot at: a slow sidestep that keeps the aim steady; under
			   fire, flat out */
			/* how fast: its agility; not shot at, slower (a steadier aim),
			   under fire flat out (the nimble) or near it */
			if (BOT_ON(BOT_F_HUMAN))
				/* (the stick all the way over, as a person has it in a
				   fight; a touch less with nobody shooting back) */
				move_speed = forward > 0.f || threatened ? 1.f : 0.85f + 0.15f * bot->personality.agility;
			else if (BOT_ON(BOT_F_PACE))
				/* closing in: flat out; sidestepping: quick (a bot's aim
				   doesn't suffer for it, and a quick target is a hard one) */
				move_speed = forward > 0.f ? 1.f : threatened ? 0.8f + 0.2f * bot->personality.agility : 0.55f + 0.35f * bot->personality.agility;
			else
				move_speed = threatened ? 0.7f + 0.3f * bot->personality.agility : 0.35f + 0.35f * bot->personality.agility;
			if (aas_ready() && !bot_move_direction_safe(&body, move_yaw, 0.7f))
			{
				/* the nearest way round that's open (the other way first: a
				   strafe that turns back) */
				static real const offsets[] = { 0.5f, 1.f, 1.6f, 2.2f, 2.8f };
				long i;
				int found = FALSE;
				for (i = 0; i < (long)(sizeof(offsets) / sizeof(offsets[0])) && !found; i++)
				{
					long side;
					for (side = 0; side < 2 && !found; side++)
					{
						real base = bot->strafe_sign != 0.f ? bot->strafe_sign : 1.f;
						real sign = side ? base : -base;
						real candidate = bot_wrap(move_yaw + sign * offsets[i]);
						if (bot_move_direction_safe(&body, candidate, 0.7f))
						{
							move_yaw = candidate;
							found = TRUE;
							if (i >= 2)
								bot->strafe_sign = -bot->strafe_sign;
						}
					}
				}
				if (!found)
				{
					/* hemmed in (by a wall, at an edge): out to the open */
					real_point3d spot;
					long area = aas_point_area(AAS_FOOT, &position);
					if (area != NONE)
						spot = aas_area(AAS_FOOT, area)->center;
					else if (aas_nearest_area(AAS_FOOT, &position, 3.f, &spot) == NONE)
						spot = position;
					move_yaw = bot_yaw_to(&position, &spot);
					moving = bot_distance2d(&position, &spot) > 0.3f;
				}
			}
		}
		/* holding somewhere (the hill, our flag, with the carrier, what we're
		   after): fighting, but not strafing away from it */
		if (!moved && moving && bot->goal_objective && bot->goal_kind != _bot_goal_item && bot->goal_kind != _bot_goal_vehicle &&
			bot_distance2d(&position, &bot->goal) > 4.f)
		{
			real back = bot_yaw_to(&position, &bot->goal);
			real candidate = bot_wrap(back + bot->strafe_sign * 0.6f);
			if (!aas_ready() || bot_move_direction_safe(&body, candidate, 0.7f))
				move_yaw = candidate;
			else if (bot_distance2d(&position, &bot->goal) > 7.f)
			{
				/* (the way back isn't straight: the route) */
				bot_go(bot, &body, &bot->goal, _bot_move_goal_position, 3.f, &result);
				moved = TRUE;
				move_yaw = result.yaw;
				moving = result.moving;
			}
		}
		/* a sniper at their spot stays put and shoots */
		if (bot->role == _bot_role_sniper && bot->goal_kind == _bot_goal_hold && bot_distance2d(&position, &bot->goal) < 2.f &&
			target_distance > 8.f && !moved)
		{
			/* (a step either way, never still: a still sniper is an easy one) */
			real side = bot->strafe_sign != 0.f ? bot->strafe_sign : 1.f;
			move_yaw = bot_distance2d(&position, &bot->goal) > 1.2f ? bot_yaw_to(&position, &bot->goal) : bot_wrap(to_target + side * BOT_PI * 0.5f);
			move_speed = 0.6f;
			moving = TRUE;
		}
		/* jumping in a fight: more the higher the skill level and the jumpier
		   the bot, mostly when shot at; now and then a crouch jump (tucked up
		   in the air: a smaller, odder target) */
		if (moving && !moved)
		{
			/* (not shot at: steady, taking the shot; jumping spoils our own aim) */
			/* how much: its jumpiness (the jumpiest jump now and then even
			   when nobody's shooting at them; the least hardly ever) */
			real chance = (threatened ? 0.003f + 0.05f * bot->personality.jumpiness :
				MAX(0.f, bot->personality.jumpiness - 0.6f) * 0.03f) * skill_move * 1.5f;
			/* its habit: in the air half the fight, shot at or not */
			if (bot->personality.signature == _bot_signature_jump_shot)
				chance = MAX(chance, 0.035f);
			/* ... or ducking as it sidesteps: down for a third of a second,
			   every second or two */
			if (bot->personality.signature == _bot_signature_crouch_strafe && target_distance > 3.f)
			{
				if (--bot->signature_ticks <= 0)
					bot->signature_ticks = 32 + (long)(bot_random(bot) * 30.f);
				if (bot->signature_ticks <= 10)
					action->control_flags |= FLAG(_unit_control_crouch_modifier_bit);
			}
			if (bot->attack_jump_cooldown <= 0 && bot_random(bot) < chance && (!aas_ready() || bot_move_direction_safe(&body, move_yaw, 1.5f)))
			{
				jump = TRUE;
				bot->attack_jump_cooldown = BOT_SECONDS(1.8f - 0.9f * skill_move);
				if (bot_random(bot) < (0.1f + 0.6f * skill_move) * (0.5f + bot->personality.jumpiness))
					bot->crouch_jump_ticks = 23;
			}
		}
	}
	else
	{
		/* travel */
		if (bot->goal_kind == _bot_goal_hold && bot_distance2d(&position, &bot->goal) < 1.5f && !committed)
		{
			/* holding a spot: pacing about it, never standing still */
			moving = FALSE; /* (the pacing below) */
			if (target == NONE && bot->target_unit == NONE)
			{
				desired_yaw = bot_glance(bot, me, &position);
				look_reason = _bot_look_glance;
			}
		}
		else
		{
			long kind = bot->goal_kind == _bot_goal_item ? _bot_move_goal_item :
				bot->goal_kind == _bot_goal_vehicle ? _bot_move_goal_vehicle : _bot_move_goal_position;
			real radius = bot->goal_kind == _bot_goal_item || bot->goal_kind == _bot_goal_vehicle ? 0.35f : 1.f;
			/* by this trip's way round, first (not for what's urgent: the
			   flag, a carrier, the enemy) */
			if (bot->via_pending && !bot->goal_squad &&
				(((bot->goal_kind == _bot_goal_roam || bot->goal_kind == _bot_goal_item) && !bot->goal_objective) ||
				 (BOT_ON(BOT_F_VIA) && ((bot->goal_objective && bot->goal_kind != _bot_goal_vehicle) ||
				  (BOT_FLANKING && bot->goal_kind == _bot_goal_enemy)))))
			{
				bot_plan_via(bot, &body, player_absolute_index);
			}
			else if (bot->via_pending)
				bot->via_plan.samples = 0;
			bot->via_pending = FALSE;
			/* (a place tried each tick, straight on meanwhile) */
			if (bot->via_plan.samples > 0)
				bot_plan_via_step(bot, &body, player_absolute_index);
			if (bot->via_valid && (bot_distance2d(&position, &bot->via_point) < 2.5f || --bot->via_ticks <= 0 || committed))
				bot->via_valid = committed && bot->via_valid && bot->via_ticks > 0;
			if (bot->via_valid)
				bot_go(bot, &body, &bot->via_point, _bot_move_goal_position, 2.f, &result);
			else
				bot_go(bot, &body, &bot->goal, kind, radius, &result);
			if (bot->via_valid && (result.no_route || result.arrived))
			{
				bot->via_valid = FALSE;
				result.no_route = FALSE;
			}
			moved = TRUE;
			if (result.no_route)
			{
				/* no way there */
				if (bot_logging())
					bot_log("bot%ld: no route to %.1f %.1f %.1f (goal %ld) from %.1f %.1f %.1f areas %ld -> %ld", player_absolute_index, bot->goal.x, bot->goal.y, bot->goal.z, bot->goal_kind,
						position.x, position.y, position.z, aas_point_area(AAS_FOOT, &position), aas_nearest_area(AAS_FOOT, &bot->goal, 3.f, NULL));
				if (bot->goal_object != NONE && (bot->goal_kind == _bot_goal_item || bot->goal_kind == _bot_goal_vehicle))
					bot_ignore(bot, bot->goal_object, 60 * BOT_TICKS_PER_SECOND);
				else if (aas_point_area(AAS_FOOT, &position) != NONE)
				{
					/* (somewhere, someone: not that way again for a while, or
					   it's chosen again the next second, and again; not when
					   it's where we are that's the trouble: in the air, off
					   the areas) */
					bot->noroute_point = bot->goal;
					bot->noroute_until = game_time_get() + 8 * BOT_TICKS_PER_SECOND;
				}
				/* (a place we were exploring toward: done with, not offered
				   again at full worth once the mark wears off) */
				if (bot->goal_kind == _bot_goal_roam && bot->explore_until > game_time_get() &&
					distance3d(&bot->explore_point, &bot->goal) < 4.f)
				{
					bot->explore_until = 0;
					bot_remember_spot(bot, &bot->goal);
				}
				bot->goal_kind = _bot_goal_none;
				bot->goal_ticks = 0;
			}
			move_yaw = result.yaw;
			path_yaw = result.path_yaw;
			moving = result.moving;
			move_speed = result.speed;
			if (target != NONE && bot_distance2d(&position, &bot->goal) > 3.f && !committed && !result.look &&
				result.travel == _aas_travel_walk && moving)
			{
				/* going for something with them in sight, shooting on the way:
				   weave (more the more skilled, and when shot at) */
				if (--bot->strafe_ticks <= 0)
				{
					bot->strafe_ticks = (long)((10 + bot_random(bot) * 25) * (threatened ? 0.7f : 1.f) * (1.6f - 0.6f * bot_skill_movement()) *
						(1.4f - 0.7f * bot->personality.agility));
					bot->strafe_sign = bot_random(bot) < 0.5f ? -1.f : 1.f;
				}
				if (bot_move_direction_safe(&body, bot_wrap(move_yaw + bot->strafe_sign * 0.45f * (0.5f + bot_skill_movement())), 0.8f))
					move_yaw = bot_wrap(move_yaw + bot->strafe_sign * 0.45f * (0.5f + bot_skill_movement()));
			}
			/* hopping along on open ground now and then (the jumpier and the
			   more skilled, the more; with someone in sight, more), a crouch
			   jump now and then: harder to hit, and quicker over bumps */
			if (moving && result.travel == _aas_travel_walk && !committed && !result.look && bot->attack_jump_cooldown <= 0 &&
				bot_distance2d(&position, &bot->goal) > 3.f)
			{
				real skill = bot_skill_movement();
				real chance = (target != NONE ? (threatened ? 0.004f + 0.04f * bot->personality.jumpiness : MAX(0.f, bot->personality.jumpiness - 0.6f) * 0.03f) :
					0.0005f + 0.016f * bot->personality.jumpiness * bot->personality.jumpiness) * skill;
				if (bot_random(bot) < chance && (!aas_ready() || bot_move_direction_safe(&body, move_yaw, 2.f)))
				{
					jump = TRUE;
					bot->attack_jump_cooldown = BOT_SECONDS(0.9f + 0.7f * bot_random(bot));
					if (bot_random(bot) < 0.3f + 0.4f * skill)
						bot->crouch_jump_ticks = 23;
				}
			}
			/* look where we're going (along the route) unless there's someone
			   to shoot; a ladder needs facing */
			if (result.look)
			{
				desired_yaw = result.look_yaw;
				desired_pitch = result.look_pitch;
				look_reason = _bot_look_move;
				look_forced = TRUE;
			}
			else if (target == NONE && bot->target_unit == NONE && bot->tracked_unit != NONE && bot->tracked_age < 3 * BOT_TICKS_PER_SECOND)
			{
				/* someone on the tracker, or heard: looking their way */
				desired_yaw = bot_yaw_to(&eye, &bot->tracked_at);
				desired_pitch = (real)atan2(bot->tracked_at.z - eye.z, MAX(0.5f, bot_distance2d(&bot->tracked_at, &eye)));
				look_reason = _bot_look_noise;
			}
			else if (target == NONE && bot->target_unit == NONE)
			{
				desired_yaw = moving ? path_yaw : desired_yaw;
				desired_pitch = 0.f;
				look_reason = moving ? _bot_look_path : look_reason;
				/* a look about on the way, as a player has: mostly off to one
				   side or the other, now and then right round behind (there's
				   no seeing what we're not looking at: bot_notices); the more
				   skilled, the oftener */
				if (moving && !committed && result.travel == _aas_travel_walk && BOT_ON(BOT_F_NOTICE))
				{
					if (bot->look_about_ticks > 0)
					{
						desired_yaw = bot_wrap(path_yaw + bot->look_about_yaw);
						look_reason = _bot_look_scan;
						if (--bot->look_about_ticks == 0)
							bot->look_about_ticks = -(long)(BOT_SECONDS(1.5f + 3.5f * bot_random(bot)) * (1.6f - 0.6f * bot_skill_fraction()));
					}
					else if (++bot->look_about_ticks >= 0)
					{
						real side = bot_random(bot) < 0.5f ? -1.f : 1.f;
						bot->look_about_yaw = side * (bot_random(bot) < 0.15f ? 2.4f + 0.7f * bot_random(bot) : 0.6f + 0.9f * bot_random(bot));
						bot->look_about_ticks = BOT_SECONDS(0.35f + 0.35f * bot_random(bot));
					}
				}
			}
		}
		/* nearly at a gun to swap for: the one it will drop comes to hand on
		   the last steps (as a person does it: the swap is then the moment it
		   gets there, not a second of standing over the gun first) */
		if (bot->goal_kind == _bot_goal_item && target == NONE && !weapon_chosen && bot_distance2d(&position, &bot->goal) < 5.f &&
			unit->unit.weapon_object_indices[0] != NONE && unit->unit.weapon_object_indices[1] != NONE &&
			object_try_and_get(bot->goal_object) && TEST_FLAG(_object_mask_weapon, object_get(bot->goal_object)->object.type) &&
			!bot_goal_is_game_object(bot->goal_object) && !bot_holds_weapon_type(unit, object_get(bot->goal_object)->definition_index))
		{
			long drop_slot = NONE;
			bot_worst_held(unit, &drop_slot);
			if (drop_slot != NONE)
			{
				action->desired_weapon_index = (short)drop_slot;
				weapon_chosen = TRUE;
			}
		}
		/* at an item or a vehicle: hold action to pick up or get in */
		/* (on its own floor: right over it on the roof, or under it, is not at
		   it, and the route down or up to it is what goes on, not a stand
		   over the spot and a hop) */
		if ((bot->goal_kind == _bot_goal_item || bot->goal_kind == _bot_goal_vehicle) &&
			bot_distance2d(&position, &bot->goal) < (bot->goal_kind == _bot_goal_vehicle ? 1.2f : 1.3f) &&
			fabs(position.z - bot->goal.z) < (bot->goal_kind == _bot_goal_vehicle ? 2.f : 1.f))
		{
			long worst_slot = NONE;
			struct object_datum *item = bot->goal_object != NONE ? object_try_and_get(bot->goal_object) : NULL;
			/* (the game has it in our reach: the swap it offers is for this) */
			int offered = item && me->action_object_index == bot->goal_object &&
				(me->action_result == _player_action_result_swap_for_weapon || me->action_result == _player_action_result_add_weapon_to_inventory ||
				 me->action_result == _player_action_result_swap_for_powerup);
			/* held: with two weapons, swapping one takes a held action */
			bot->action_ticks++;
			/* weapons are picked up with the swap button (players.c,
			   player_handle_weapon_swap), pressed afresh each time; the one
			   in hand is the one dropped, so the worse of our two goes in
			   hand first; vehicles are entered with action */
			if (bot->goal_kind == _bot_goal_item)
			{
				int weapon_item = item && TEST_FLAG(_object_mask_weapon, item->object.type) && !bot_goal_is_game_object(bot->goal_object);
				if (weapon_item && bot_holds_weapon_type(unit, item->definition_index))
				{
					/* one like it in hand already: its rounds come with walking
					   over it (and a swap for the same is refused): done here */
					bot->goal_kind = _bot_goal_none;
					bot->goal_ticks = 0;
					bot->action_ticks = 0;
				}
				else if (weapon_item && unit->unit.weapon_object_indices[0] != NONE && unit->unit.weapon_object_indices[1] != NONE &&
					(bot_worst_held(unit, &worst_slot), worst_slot != NONE))
				{
					/* the worse of ours in hand (and kept there: nothing else
					   switching weapons meanwhile), then the swap; never the swap
					   with the good one out (that's the one it'd drop) */
					action->desired_weapon_index = (short)worst_slot;
					weapon_chosen = TRUE;
					if (worst_slot == unit->unit.current_weapon_index && unit->unit.desired_weapon_index == worst_slot &&
						(bot->action_ticks / 4) % 2 == 0)
					{
						action->control_flags |= FLAG(_unit_control_swap_weapons_bit);
					}
				}
				else if ((bot->action_ticks / 4) % 2 == 0)
					action->control_flags |= FLAG(_unit_control_swap_weapons_bit);
				/* Not ours yet, and not in the game's reach of us: the route
				   ends where the floor it knows ends, and the thing may lie a
				   step past that (against a wall, on a crate's lid, on a lip).
				   Right up to it, as a person walks onto what they want, and a
				   hop when it is above us or will not come. */
				/* (While the route still has somewhere to take us, it is the
				   route's: the thing may be up on a plinth a jump away round
				   the other side, and straight at it from here is into the
				   plinth's side. The clock for giving up waits too.) */
				if (bot->goal_kind == _bot_goal_item && item && !offered &&
					(committed || (moved && result.moving && !result.arrived && !result.no_route)) &&
					bot->item_route_ticks < 4 * BOT_TICKS_PER_SECOND)
				{
					bot->item_route_ticks++;
					bot->action_ticks--;
				}
				else if (bot->goal_kind == _bot_goal_item && item && !offered && !committed)
				{
					real off = bot_distance2d(&position, &item->object.position);
					real above = item->object.position.z - position.z;
					if (off > 0.1f)
					{
						real yaw = bot_yaw_to(&position, &item->object.position);
						int open = !aas_ready() || bot_move_direction_safe(&body, yaw, MIN(off, 0.6f));
						if (!open)
						{
							/* (floor the navigation has no areas on: an alcove too
							   narrow for one, a plinth a step too high to walk
							   onto. Nothing between us and it at the height of
							   the higher of the two, and something to stand on
							   halfway there: it is somewhere a body gets to) */
							real level = MAX(position.z, item->object.position.z);
							real_point3d from = position;
							real_point3d to = item->object.position;
							real_point3d half, under;
							from.z = to.z = level + 0.5f;
							/* (as far as just short of it: the thing itself is not in the way) */
							to.x -= (to.x - from.x) / off * MIN(0.25f, off * 0.5f);
							to.y -= (to.y - from.y) / off * MIN(0.25f, off * 0.5f);
							half.x = (from.x + to.x) * 0.5f;
							half.y = (from.y + to.y) * 0.5f;
							half.z = level + 0.3f;
							under = half;
							under.z = MIN(position.z, item->object.position.z) - 1.2f;
							open = bot_line_clear(bot_move_flags, &from, &to, me->unit_index) &&
								!bot_line_clear(bot_move_flags, &half, &under, me->unit_index);
							if (bot_logging() && bot_env_set("HALO_BOT_PICKUPS") && bot->action_ticks % 10 == 1)
								bot_log("bot%ld pickup: the last step: %.2f off, %.2f above, way clear %d, floor halfway %d", player_absolute_index, off, above,
									bot_line_clear(bot_move_flags, &from, &to, me->unit_index), !bot_line_clear(bot_move_flags, &half, &under, me->unit_index));
						}
						if (open)
						{
							moving = TRUE;
							move_yaw = yaw;
							move_speed = off > 0.5f ? 1.f : 0.6f;
						}
					}
					/* (up onto what it lies on, as soon as we are at the foot of
					   it; a hop anyway when it will not come) */
					if ((above > 0.3f && off < 1.f && (bot->action_ticks == 4 || bot->action_ticks == 16)) ||
						(above <= 0.3f && bot->action_ticks == 20))
					{
						jump = TRUE;
					}
				}
			}
			else
			{
				/* a vehicle's doors are close together, and the game puts us
				   in whichever seat's door we're nearest: get right onto the
				   one we want before pressing */
				real off = bot_distance2d(&position, &bot->goal);
				moving = off > 0.15f;
				move_yaw = bot_yaw_to(&position, &bot->goal);
				move_speed = off > 0.6f ? 0.5f : 0.25f;
				desired_yaw = bot_yaw_to(&position, &bot->goal);
				look_reason = _bot_look_act;
				if (off < 0.55f && (bot->action_ticks / 6) % 2 == 0)
					action->control_flags |= FLAG(_unit_control_action_bit);
			}
			/* couldn't: pick something else. (A swap the game offers is under
			   way, the worse gun coming to hand first: three seconds. Something
			   the game will not give us where we stand: one, not three of
			   standing at it.) */
			if (bot->action_ticks > (bot->goal_kind == _bot_goal_vehicle || offered ? 3 * BOT_TICKS_PER_SECOND : 36))
			{
				if (bot_logging())
					bot_log("bot%ld t%ld gives up on %s (%.1f away, %s)", player_absolute_index, game_time_get(),
						item ? tag_get_name(item->definition_index) : "it", item ? distance3d(&item->object.position, &position) : 0.f,
						offered ? "offered" : "not in reach");
				if (bot_logging() && item && bot_env_set("HALO_BOT_PICKUPS"))
				{
					/* (testing: what the game has for us about it) */
					bot_log("bot%ld pickup: result %d object %s; holds %d, in hand %d wanted %d; item at %.2f %.2f %.2f (we %.2f %.2f %.2f), its sphere %.2f %.2f %.2f r %.2f, ours %.2f %.2f %.2f r %.2f, ignores us %d, parent %lx, type %d",
						player_absolute_index, (int)me->action_result, me->action_object_index == bot->goal_object ? "it" : me->action_object_index == NONE ? "none" : "another",
						(int)bot_weapons_held(unit), (int)unit->unit.current_weapon_index, (int)unit->unit.desired_weapon_index,
						item->object.position.x, item->object.position.y, item->object.position.z, position.x, position.y, position.z,
						item->object.bounding_sphere_center.x, item->object.bounding_sphere_center.y, item->object.bounding_sphere_center.z, item->object.bounding_sphere_radius,
						unit->object.bounding_sphere_center.x, unit->object.bounding_sphere_center.y, unit->object.bounding_sphere_center.z, unit->object.bounding_sphere_radius,
						(int)(((struct item_datum *)item)->item.ignore_object_index == me->unit_index), (unsigned long)item->object.parent_object_index, (int)item->object.type);
					{
						boolean unit_can_use_weapon(long unit_index, long weapon_index);
						boolean unit_should_autopick_weapon(long unit_index, long weapon_index);
						boolean unit_approve_weapon_swap(long unit_index, long weapon_index);
						long found[64];
						short count = objects_in_sphere(0, NONE, &unit->object.location, &unit->object.bounding_sphere_center,
							unit->object.bounding_sphere_radius, found, 64);
						short i;
						int among = FALSE;
						for (i = 0; i < count; i++)
							among = among || found[i] == bot->goal_object;
						bot_log("bot%ld pickup: its flags %lx header %x",
							player_absolute_index, (unsigned long)item->object.flags, (unsigned)object_header_get(bot->goal_object)->flags);
						bot_log("bot%ld pickup: %d objects in our sphere, it among them %d; cluster ours %d its %d; can use %d autopick %d swap %d; control flags %lx",
							player_absolute_index, (int)count, among, (int)unit->object.location.cluster_index, (int)item->object.location.cluster_index,
							TEST_FLAG(_object_mask_weapon, item->object.type) ? (int)unit_can_use_weapon(me->unit_index, bot->goal_object) : -1,
							TEST_FLAG(_object_mask_weapon, item->object.type) ? (int)unit_should_autopick_weapon(me->unit_index, bot->goal_object) : -1,
							TEST_FLAG(_object_mask_weapon, item->object.type) ? (int)unit_approve_weapon_swap(me->unit_index, bot->goal_object) : -1,
							(unsigned long)unit->unit.control_flags);
					}
				}
				bot->action_ticks = 0;
				bot_ignore(bot, bot->goal_object, 30 * BOT_TICKS_PER_SECOND);
				bot->goal_kind = _bot_goal_none;
				bot->goal_ticks = 0;
			}
		}
		else
		{
			bot->action_ticks = 0;
			bot->item_route_ticks = 0;
		}
	}

	/* never standing still (a still target is an easy one): nowhere to go
	   just now, holding a spot, waiting a turn: pacing about, a step or two
	   this way and that, about where we are */
	BOT_PROFILE_MARK("flag juggling");
	/* ---- flag juggling: running home with the flag (which slows its
	   carrier), drop it ahead on the run (it keeps our speed, and a
	   little more), run after it unburdened, and snatch it up again the
	   moment it settles (the game won't give it back to whoever dropped
	   it before then): quicker home.
	   Only on open ground with nobody about (on the ground it's theirs to
	   touch and send back), and the more skilled and the jumpier do it.
	   Off for now (BOT_FLAG_JUGGLING): the carrier keeps hold of the flag */
	if (bot->juggle_cooldown > 0)
		bot->juggle_cooldown--;
	if (bot->juggle_ticks > 0)
	{
		struct object_datum *flag = bot->juggle_flag != NONE ? object_try_and_get(bot->juggle_flag) : NULL;
		bot->juggle_ticks++;
		if (!flag || flag->object.parent_object_index != NONE || bot->juggle_ticks > 40 || target != NONE)
		{
			/* caught it (or someone else did, or it's gone, or trouble):
			   the goals see to the rest */
			if (bot_logging() && flag && object_get_ultimate_parent(bot->juggle_flag) == me->unit_index)
				bot_log("bot%ld t%ld juggled in %ld ticks", player_absolute_index, game_time_get(), bot->juggle_ticks);
			bot->juggle_ticks = 0;
		}
		else if (bot->juggle_ticks > 2)
		{
			/* after it (it's ahead, along the way) and grab it */
			real d = bot_distance2d(&position, &flag->object.position);
			if (d < 1.2f && fabs(flag->object.position.z - position.z) < 1.5f && bot->juggle_ticks % 2 == 0)
				action->control_flags |= FLAG(_unit_control_swap_weapons_bit);
			if (d > 0.4f && vehicle_index == NONE)
			{
				real to_flag = bot_yaw_to(&position, &flag->object.position);
				if (!aas_ready() || bot_move_direction_safe(&body, to_flag, 0.8f))
				{
					move_yaw = to_flag;
					moving = TRUE;
					move_speed = 1.f;
				}
			}
		}
	}
	else if (BOT_FLAG_JUGGLING && vehicle_index == NONE && game_engine == &ctf_engine && weapon.none && moving && moved && !committed &&
		result.travel == _aas_travel_walk && !result.look && target == NONE && bot->hit_ticks <= 0 &&
		bot->juggle_cooldown <= 0 && bot->weapon_switch_ticks <= 0 &&
		unit->unit.current_weapon_index >= 0 && unit->unit.current_weapon_index < MAXIMUM_WEAPONS_PER_UNIT &&
		unit->unit.weapon_object_indices[unit->unit.current_weapon_index] != NONE &&
		bot_name_has(object_get(unit->unit.weapon_object_indices[unit->unit.current_weapon_index])->definition_index, "flag") &&
		fabs(bot_angle_difference(move_yaw, bot->yaw)) < 0.6f && fabs(bot->pitch) < 0.4f &&
		bot_distance2d(&position, &bot->goal) > 6.f &&
		(!aas_ready() || bot_move_direction_safe(&body, move_yaw, 2.5f)))
	{
		bot->juggle_cooldown = (long)((0.4f + 0.6f * bot_random(bot)) * BOT_TICKS_PER_SECOND);
		if (bot_random(bot) < (0.15f + 0.85f * bot_skill_fraction()) * (0.4f + 0.6f * MAX(bot->personality.jumpiness, bot->personality.daring)))
		{
			bot->juggle_flag = unit->unit.weapon_object_indices[unit->unit.current_weapon_index];
			bot->juggle_ticks = 1;
			/* (pulling the trigger with the flag in hand drops it: players.c) */
			action->primary_trigger = 1.f;
			action->control_flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
		}
	}

	if (!moving && vehicle_index == NONE && !committed && nav_flood_done && bot->teabag_ticks <= 0 &&
		!((bot->goal_kind == _bot_goal_item || bot->goal_kind == _bot_goal_vehicle) && bot->action_ticks > 0))
	{
		real_point3d anchor = bot->goal_kind == _bot_goal_hold ? bot->goal : bot->pace_anchor;
		real_point3d spot;
		if (bot->goal_kind != _bot_goal_hold && (bot->pace_ticks <= 0 || bot_distance2d(&position, &bot->pace_anchor) > 4.f))
			bot->pace_anchor = anchor = position;
		spot.x = anchor.x + (real)cos(bot->pace_angle) * 1.6f;
		spot.y = anchor.y + (real)sin(bot->pace_angle) * 1.6f;
		spot.z = anchor.z;
		if (--bot->pace_ticks <= 0 || bot_distance2d(&position, &spot) < 0.4f)
		{
			bot->pace_ticks = (long)((0.8f + bot_random(bot)) * BOT_TICKS_PER_SECOND);
			bot->pace_angle = bot_wrap(bot->pace_angle + (bot_random(bot) < 0.5f ? -1.f : 1.f) * (1.2f + bot_random(bot) * 1.5f));
		}
		move_yaw = bot_yaw_to(&position, &spot);
		if (aas_ready() && !bot_move_direction_safe(&body, move_yaw, 0.6f))
		{
			bot->pace_angle = bot_wrap(bot->pace_angle + BOT_PI);
			move_yaw = bot_wrap(move_yaw + BOT_PI);
		}
		move_speed = 0.6f;
		moving = !aas_ready() || bot_move_direction_safe(&body, move_yaw, 0.6f);
	}

	/* the first moments of a map, while its areas are being worked out:
	   stay put rather than walk into walls */
	if (!nav_flood_done)
		moving = FALSE;

	BOT_PROFILE_MARK("getting unstuck");
	/* ---- getting unstuck (moving about on our own, in a fight: the route
	   has its own) */
	if (moving && !moved && vehicle_index == NONE)
	{
		if (++bot->progress_ticks >= BOT_TICKS_PER_SECOND)
		{
			if (bot_distance2d(&position, &bot->progress_point) < 0.6f && bot->unstick_ticks <= 0)
			{
				bot->unstick_ticks = 15 + (long)(bot_random(bot) * 20);
				bot->unstick_yaw = bot_wrap(move_yaw + BOT_PI * (0.5f + bot_random(bot)));
				if (aas_ready() && !bot_move_direction_safe(&body, bot->unstick_yaw, 0.7f))
					bot->unstick_yaw = bot_wrap(bot->unstick_yaw + BOT_PI);
			}
			bot->progress_point = position;
			bot->progress_ticks = 0;
		}
		if (bot->unstick_ticks > 0)
		{
			bot->unstick_ticks--;
			move_yaw = bot->unstick_yaw;
			if (bot->unstick_ticks % 12 == 0)
				jump = TRUE;
		}
		else if (!aas_ready())
		{
			/* no areas for this map: steer round walls, not over edges */
			real steered;
			int steer_jump;
			bot_steer(me->unit_index, &position, move_yaw, 0.8f + 1.4f * bot->personality.daring, &steered, &steer_jump);
			move_yaw = steered;
			if (steer_jump)
				jump = TRUE;
		}
	}
	else
		bot->progress_ticks = 0;
	if (bot->jump_cooldown > 0)
		bot->jump_cooldown--;
	if (bot->attack_jump_cooldown > 0)
		bot->attack_jump_cooldown--;
	if (vehicle_index == NONE)
	{
		/* a crouch jump under way: tucked up a few ticks into it until it lands */
		if (bot->crouch_jump_ticks > 0 && --bot->crouch_jump_ticks < 20)
			action->control_flags |= FLAG(_unit_control_crouch_modifier_bit);
		/* the route's jumps (up a ledge, over a gap) and crouches (legs
		   tucked up to clear a lip): as they come */
		if (moved && result.jump)
			action->control_flags |= FLAG(_unit_control_jump_bit);
		if (moved && result.crouch)
			action->control_flags |= FLAG(_unit_control_crouch_modifier_bit);
		if (jump && bot->jump_cooldown <= 0 && !committed)
		{
			action->control_flags |= FLAG(_unit_control_jump_bit);
			bot->jump_cooldown = 16 + (long)(bot_random(bot) * 10.f);
		}
	}

	BOT_PROFILE_MARK("walking with nobod");
	/* ---- walking with nobody in sight: eyes ahead, down the way it's going.
	   Where someone went, or a noise, only pulls its look part of the way
	   round (it glances, it doesn't walk looking over its shoulder). Not
	   the look about it means to take (to a side, now and then right
	   round, for half a second): that one is the whole turn, or it sees no
	   more to its sides and behind than it does walking. */
	if (target == NONE && !look_forced && look_reason != _bot_look_scan && look_reason != _bot_look_act &&
		moving && path_yaw != NONE && vehicle_index == NONE &&
		bot->grenade_pending_ticks <= 0 && !(bot->in_cover && move_speed == 0.f))
	{
		real off = bot_angle_difference(desired_yaw, path_yaw);
		real allowed = bot->target_unit != NONE ? 0.8f : 0.5f;
		desired_yaw = bot_wrap(path_yaw + PIN(off, -allowed, allowed));
		if (bot->target_unit == NONE && bot->tracked_unit == NONE)
			desired_pitch = 0.f;
	}

	/* ---- showing off, with nobody to fight: the turn right round after a
	   kill; and, standing about by one of its own side, a few ducks at
	   them */
	if (target != NONE || look_forced || vehicle_index != NONE || bot->hit_ticks > 0 || bot_on_objective(bot))
	{
		bot->spin_ticks = 0;
		bot->antic_ticks = 0;
	}
	if (bot->spin_ticks > 0)
	{
		if (bot->spin_ticks == 24 && !committed && bot->jump_cooldown <= 0)
		{
			action->control_flags |= FLAG(_unit_control_jump_bit);
			bot->jump_cooldown = 20;
		}
		bot->spin_ticks--;
		desired_yaw = bot_wrap(bot->yaw + 0.55f);
		desired_pitch = 0.f;
		look_reason = _bot_look_act;
	}
	if (bot->antic_ticks > 0)
	{
		bot->antic_ticks--;
		if ((bot->antic_ticks / 5) & 1)
			action->control_flags |= FLAG(_unit_control_crouch_modifier_bit);
	}
	else if (teams && target == NONE && !moving && bot->personality.showboat > 0.5f && vehicle_index == NONE &&
		game_time_get() >= bot->antic_time && bot->hit_ticks <= 0)
	{
		struct data_iterator others;
		struct player_datum *other;
		bot->antic_time = game_time_get() + BOT_SECONDS(6.f + 10.f * bot_random(bot));
		data_iterator_new(&others, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&others)) != NULL)
		{
			if (other != me && other->unit_index != NONE && bot_unit_alive(other->unit_index) && !bot_is_enemy((short)me->team_index, other) &&
				distance3d(&object_get(object_get_ultimate_parent(other->unit_index))->object.position, &position) < 3.f)
			{
				bot->antic_ticks = 30;
				desired_yaw = bot_yaw_to(&position, &object_get(object_get_ultimate_parent(other->unit_index))->object.position);
				look_reason = _bot_look_act;
				break;
			}
		}
	}

	BOT_PROFILE_MARK("a grenade on its w");
	/* ---- a grenade on its way: aim high enough to lob it there, then throw */
	if (bot->grenade_pending_ticks > 0)
	{
		real distance = bot_distance2d(&eye, &bot->grenade_point);
		bot->grenade_pending_ticks--;
		desired_yaw = bot_yaw_to(&eye, &bot->grenade_point);
		desired_pitch = (real)atan2(bot->grenade_point.z - eye.z, distance) + 0.06f + distance * 0.011f;
		look_reason = _bot_look_lob;
		if (fabs(bot_angle_difference(desired_yaw, bot->yaw)) < 0.12f && fabs(desired_pitch - bot->pitch) < 0.12f)
		{
			action->desired_grenade_index = bot->grenade_index;
			action->control_flags |= FLAG(_unit_control_throw_grenade_bit);
			if (bot_logging())
				bot_log("bot%ld grenade %d at %.1f", player_absolute_index, bot->grenade_index, distance);
			bot->grenade_pending_ticks = 0;
			bot->grenade_cooldown = (long)((7.f - 4.f * bot->personality.grenade_happy) * BOT_TICKS_PER_SECOND);
			/* (whether it told: from now, not from when we started lining
			   it up, while the rifle was doing the damage) */
			bot_grenade_watch(bot, bot->grenade_watch_unit);
		}
		else if (bot->grenade_pending_ticks == 0)
			bot->grenade_watch_ticks = 0; /* never lined up: never thrown, nothing to learn */
	}

	BOT_PROFILE_MARK("turn at a limited ");
	/* ---- turn at a limited rate */
	{
		real max_turn = skill->turn_rate * BOT_TICK * (driving_moved ? 1.5f : 1.f);
		real dy;
		if (look_forced)
			max_turn = MAX(max_turn, 0.3f); /* (onto a ladder: facing it at once, or we let go) */
		dy = bot_angle_difference(desired_yaw, bot->yaw);
		real dp = desired_pitch - bot->pitch;

		if (BOT_ON(BOT_F_HUMAN) && !look_forced && !driving_moved)
		{
			/* As a hand turns it: the turn starts, gathers speed over a
			   few ticks, and slows into where it is looking: a part of
			   what is left each tick (a big turn, half; the small ones of
			   keeping on someone, nearly all, the more skilled the more).
			   Not the whole turn at full speed from one tick to the next
			   and a dead stop, which is a machine's. */
			real gather = max_turn * (0.3f + 0.15f * bot_skill_fraction());
			real keeping = 0.6f + 0.3f * bot_skill_fraction();
			long axis;
			for (axis = 0; axis < 2; axis++)
			{
				real left = axis ? dp : dy;
				real *rate = axis ? &bot->pitch_rate : &bot->yaw_rate;
				real part = keeping + (0.5f - keeping) * MIN(1.f, (real)fabs(left) / 0.6f);
				real want = PIN(left * part, -max_turn, max_turn);
				if (fabs(left) < 0.002f)
					*rate = left;
				else if (want * *rate >= 0.f && fabs(want) > fabs(*rate))
					*rate += PIN(want - *rate, -gather, gather);
				else if (want * *rate < 0.f)
					*rate = PIN(want, -gather, gather); /* (the other way: from a stop) */
				else
					*rate = want;
				if (axis)
					dp = *rate;
				else
					dy = *rate;
			}
		}
		else
		{
			if (dy > max_turn)
				dy = max_turn;
			else if (dy < -max_turn)
				dy = -max_turn;
			if (dp > max_turn)
				dp = max_turn;
			else if (dp < -max_turn)
				dp = -max_turn;
			bot->yaw_rate = dy;
			bot->pitch_rate = dp;
		}
		bot->yaw = bot_wrap(bot->yaw + dy);
		bot->pitch = PIN(bot->pitch + dp, -1.4f, 1.4f);
	}
	action->desired_facing.yaw = bot->yaw;
	action->desired_facing.pitch = bot->pitch;
	{
		/* (testing; looked up once, not for every bot every tick) */
		static int read;
		static char const *move_debug;
		static char const *trace;
		static int trace_all;
		if (!read)
		{
			read = TRUE;
			move_debug = getenv("HALO_BOT_MOVEDEBUG");
			trace = getenv("HALO_BOT_TRACE");
			trace_all = trace && !strcmp(trace, "all");
		}
		if (move_debug && atol(move_debug) == player_absolute_index && game_time_get() % 15 == 0)
			bot_move_debug(&bot->mover, &body);
		if (trace && (trace_all || atol(trace) == player_absolute_index))
			bot_log("trace bot%ld t%ld pos %.2f %.2f %.2f yaw %.2f want %.2f move %.2f moving %d goal %ld (%.1f %.1f) obj %d area %ld reach %ld travel %ld target %d threat %d routed %d strafe %.0f speed %.2f known %d look %ld",
				player_absolute_index, game_time_get(), position.x, position.y, position.z, bot->yaw, desired_yaw, move_yaw, moving,
				bot->goal_kind, bot->goal.x, bot->goal.y, bot->goal_objective, bot->mover.move.area, bot->mover.move.reach, result.travel,
				target != NONE, threatened, moved, bot->strafe_sign, move_speed, bot->target_unit != NONE, look_reason);
	}
	if (moving)
	{
		if (vehicle_index != NONE && driving)
		{
			/* the route's throttle: easing off for sharp turns, backing out
			   when stuck, braking for our own people */
			struct object_datum *vehicle = object_get(vehicle_index);
			action->throttle.i = drive_throttle;
			action->throttle.j = 0.f;
			if (bot_logging() && game_time_get() % 30 == 0)
				bot_log("bot%ld drive thr %.2f yaw %.2f move %.2f speed %.2f area %ld corridor %ld", player_absolute_index,
					action->throttle.i, bot->yaw, move_yaw,
					(real)sqrt(vehicle->object.translational_velocity.i * vehicle->object.translational_velocity.i +
						vehicle->object.translational_velocity.j * vehicle->object.translational_velocity.j) * 30.f,
					bot->mover.move.area, bot->mover.move.corridor_count);
		}
		else
		{
			real want_x = (real)cos(move_yaw) * move_speed;
			real want_y = (real)sin(move_yaw) * move_speed;
			/* The movement is a thumb's on a stick: it goes from one way
			   to another over a few ticks, not from one tick to the next
			   (a way wanted for a tick or two, then the old one again, is
			   hardly a move of it at all). Not where the route has a jump,
			   a ladder or a ledge under way: there the feet go exactly
			   where it says. */
			if (BOT_ON(BOT_F_HUMAN) && vehicle_index == NONE && !committed && !look_forced &&
				!(moved && (result.travel != _aas_travel_walk || result.jump || result.crouch || result.exact || result.speed < 0.99f)))
			{
				real step = moved ? 0.7f : 0.45f;
				real dx = want_x - bot->thumb_x;
				real dy = want_y - bot->thumb_y;
				real length = (real)sqrt(dx * dx + dy * dy);
				if (length > step)
				{
					dx *= step / length;
					dy *= step / length;
				}
				bot->thumb_x += dx;
				bot->thumb_y += dy;
			}
			else
			{
				bot->thumb_x = want_x;
				bot->thumb_y = want_y;
			}
			action->throttle.i = bot->thumb_x * (real)cos(bot->yaw) + bot->thumb_y * (real)sin(bot->yaw);
			action->throttle.j = bot->thumb_y * (real)cos(bot->yaw) - bot->thumb_x * (real)sin(bot->yaw);
		}
	}
	else
	{
		/* (let go) */
		bot->thumb_x = 0.f;
		bot->thumb_y = 0.f;
	}

	BOT_PROFILE_MARK("shoot");
	/* ---- shoot */
	if (bot->weapon_switch_ticks > 0)
		bot->weapon_switch_ticks--;
	if (bot->grenade_cooldown > 0)
		bot->grenade_cooldown--;
	bot_grenade_learn(bot);
	if (bot->melee_cooldown > 0)
		bot->melee_cooldown--;
	if (target != NONE && bot->seen_ticks > (long)(skill->reaction_ticks * bot->personality.reaction))
	{
		/* (how far our facing is off the target, not off wherever we've
		   turned to look) */
		real off_yaw = (real)fabs(bot_angle_difference(aim_yaw, bot->yaw));
		real off_pitch = (real)fabs(aim_pitch - bot->pitch);
		if (bot->grenade_pending_ticks > 0 || look_forced)
			off_yaw = off_pitch = BOT_PI; /* (lining up a lob, on a ladder: not shooting) */

		/* (who we were fighting: if they die, we got them) */
		bot->last_fired_unit = target;
		bot->last_fired_time = game_time_get();
		/* a beatdown: at arm's length, facing them, unless a shotgun blast
		   is ready (it does more) */
		/* (a zombie's blow takes a moment to land, and they back away: from
		   closer in) */
		if (vehicle_index == NONE && target_distance < (zombie ? 1.25f : 1.6f) && (!BOT_ON(BOT_F_MELEE) || (off_yaw < 0.6f &&
			!(weapon.kind == _bot_gun_shotgun && bot->shot_cooldown <= 0 && weapon.rounds_loaded > 0))))
		{
			if (bot->melee_cooldown <= 0)
			{
				action->control_flags |= FLAG(_unit_control_use_equipment_bit);
				bot->melee_cooldown = 18 + (long)(bot_random(bot) * 5.f);
			}
		}
		else if (!(driving && vehicle_index != NONE && off_yaw > 0.3f) &&
			!bot_friend_in_line(me, &eye, &object_get(target)->object.bounding_sphere_center, vehicle_index))
			bot_fire_control(bot, &weapon, off_yaw, off_pitch, target_distance, object_get(target)->object.shield_vitality, action);

		/* the right gun for the fight: the shotgun when they close in, the
		   pistol or sniper when they back off, bullets once their shields
		   are down, the other gun rather than a reload under fire. Better
		   bots see it sooner; a switch takes a moment, so not for a small
		   gain and not over and over */
		if (BOT_ON(BOT_F_SWITCH) && vehicle_index == NONE && !weapon_chosen && bot->weapon_switch_ticks <= 0 &&
			unit->unit.current_weapon_index >= 0 && unit->unit.current_weapon_index <= 1)
		{
			short other_slot = (short)(unit->unit.current_weapon_index == 0 ? 1 : 0);
			long other_index = unit->unit.weapon_object_indices[other_slot];
			if (other_index != NONE && object_try_and_get(other_index))
			{
				struct bot_weapon other;
				real shields = object_get(target)->object.shield_vitality;
				real here, there;
				bot_weapon_describe(other_index, &other);
				here = bot_weapon_fit(&weapon, target_distance, shields);
				there = bot_weapon_fit(&other, target_distance, shields);
				/* (its favourite stays in its hands longer, and comes
				   back to them sooner, the more loyal it is to it) */
				if (weapon.kind == bot->personality.favorite)
					here += 3.f * bot->personality.loyalty;
				if (other.kind == bot->personality.favorite)
					there += 2.f * bot->personality.loyalty;
				/* (a poor gun out and a proper one on our back: that one,
				   as soon as it is any better for this) */
				if (((there > here + 3.f && here <= 4.f + 3.f * bot->personality.loyalty) ||
					(there > here && bot_weapon_is_poor(unit->unit.weapon_object_indices[unit->unit.current_weapon_index]) &&
						!bot_weapon_is_poor(other_index))) &&
					bot_random(bot) < 0.08f + 0.3f * bot_skill_fraction())
				{
					action->desired_weapon_index = other_slot;
					bot->weapon_switch_ticks = (long)(1.5f * BOT_TICKS_PER_SECOND);
					weapon_chosen = TRUE;
					if (bot_logging())
						bot_log("bot%ld switches %.1f -> %.1f at %.1f shields %.2f", player_absolute_index, here, there, target_distance, shields);
				}
			}
		}

		/* grenades: to open a fight (a grenade-happy bot's first move on
		   someone it has just seen), and now and then in one; only as far
		   as one can be thrown, and less with a gun that does the job
		   (a pistol, a power weapon) in range. How it has gone so far
		   this game makes a bot keener or less keen */
		if (vehicle_index == NONE && bot->grenade_cooldown <= 0 && bot->grenade_pending_ticks <= 0 &&
			target_distance > 6.f && target_distance < BOT_GRENADE_RANGE)
		{
			int opening = bot->opened_on_target != target && bot->seen_ticks < 45;
			real happy = bot_grenade_keenness(bot, &weapon, target_distance);
			real chance = opening ? 0.05f + 0.9f * happy : 0.003f + 0.03f * happy;
			if (opening)
				bot->opened_on_target = target;
			/* (its habit: a grenade first, whatever gun it has) */
			if (opening && bot->personality.signature == _bot_signature_grenade_opener)
				chance = 0.95f;
			/* Into a fight: they are busy with someone else, or there are
			   more of them standing together: a grenade in among them
			   (one look at it every couple of seconds; with our own people
			   there, never). */
			if (game_time_get() >= bot->crowd_grenade_time && unit->unit.grenade_counts[0] + unit->unit.grenade_counts[1] > 0)
			{
				struct data_iterator others;
				struct player_datum *other;
				long crowd = 0;
				int ours = FALSE;
				data_iterator_new(&others, player_data);
				while ((other = (struct player_datum *)data_iterator_next(&others)) != NULL)
				{
					real_point3d at;
					if (other == me || other->unit_index == NONE || other->unit_index == target || !bot_unit_alive(other->unit_index))
						continue;
					at = object_get(object_get_ultimate_parent(other->unit_index))->object.position;
					if (distance3d(&at, &bot->last_seen_feet) > 4.f)
						continue;
					if (bot_is_enemy((short)me->team_index, other))
						crowd++;
					else
						ours = TRUE;
				}
				if (!ours && (crowd > 0 || (bot->personality.sporting < 0.75f && bot_unit_busy(target, &eye))))
				{
					bot->crowd_grenade_time = game_time_get() + BOT_SECONDS(2.5f);
					chance = MAX(chance, (crowd > 0 ? 0.5f : 0.3f) + 0.4f * bot->personality.grenade_happy);
					if (bot_logging())
						bot_log("bot%ld t%ld a fight to throw into: %ld more of them there, %.1f away", player_absolute_index, game_time_get(), crowd, target_distance);
				}
			}
			if (bot_random(bot) < chance)
			{
				/* where they'll be when it lands (it takes a second or so) */
				real_point3d lead = bot->last_seen_feet;
				real flight = 0.4f + target_distance / 14.f;
				lead.x += bot->last_seen_velocity.i * BOT_TICKS_PER_SECOND * flight;
				lead.y += bot->last_seen_velocity.j * BOT_TICKS_PER_SECOND * flight;
				if (aas_ready() && !aas_line_in_areas(AAS_FOOT, &bot->last_seen_feet, &lead))
					lead = bot->last_seen_feet;
				if (bot_grenade_clear(&eye, &lead, me->unit_index)) /* (not into a ceiling or a doorframe, to land at our feet) */
					bot_start_grenade(bot, unit, &lead);
				if (bot->grenade_pending_ticks > 0)
					bot_grenade_watch(bot, target);
			}
		}
	}
	else if (target == NONE)
	{
		bot->trigger_ticks = 0;
		/* someone just went behind cover: a grenade where they'll pop out
		   (on the way they were going, as far as they'll have got when it
		   lands), or where they went in when they stopped */
		real_point3d popout = bot_predict_enemy(bot, 1.f);
		popout.z = bot->last_seen_feet.z;
		if (vehicle_index == NONE && bot->target_unit != NONE && bot->lost_ticks > 6 && bot->lost_ticks < 60 &&
			bot->grenade_cooldown <= 0 && bot->grenade_pending_ticks <= 0 &&
			distance3d(&position, &popout) > 6.f && distance3d(&position, &popout) < BOT_GRENADE_RANGE &&
			bot_grenade_clear(&eye, &popout, me->unit_index) &&
			bot_random(bot) < 0.015f + 0.09f * bot_grenade_keenness(bot, &weapon, 1.0e9f))
		{
			bot_start_grenade(bot, unit, &popout);
			if (bot->grenade_pending_ticks > 0)
				bot_grenade_watch(bot, bot->target_unit);
		}
		if (vehicle_index == NONE && !weapon.none && weapon.uses_ammunition && weapon.rounds_total > 0 &&
			weapon.rounds_loaded < MAX(1, weapon.rounds_total / 4) && weapon.rounds_loaded < 8)
		{
			action->control_flags |= FLAG(_unit_control_weapon_reload_bit);
		}
		/* the better of our two weapons out when nothing is happening (by
		   what's in them: an empty rocket launcher is no rocket launcher);
		   not with someone just out of sight or on the tracker (the fight's
		   still on: the gun for it stays out) */
		if (vehicle_index == NONE && !weapon_chosen && bot->weapon_switch_ticks <= 0 && !weapon.none &&
			bot->target_unit == NONE && bot->tracked_unit == NONE &&
			unit->unit.weapon_object_indices[0] != NONE && unit->unit.weapon_object_indices[1] != NONE &&
			object_try_and_get(unit->unit.weapon_object_indices[0]) && object_try_and_get(unit->unit.weapon_object_indices[1]) &&
			!(bot->goal_kind == _bot_goal_item && bot->action_ticks > 0))
		{
			real v0 = bot_weapon_worth(unit->unit.weapon_object_indices[0]);
			real v1 = bot_weapon_worth(unit->unit.weapon_object_indices[1]);
			short better = (short)(v1 > v0 ? 1 : 0);
			if (unit->unit.current_weapon_index != better && fabs(v1 - v0) > 0.3f && bot_random(bot) < 0.05f)
			{
				action->desired_weapon_index = better;
				bot->weapon_switch_ticks = 2 * BOT_TICKS_PER_SECOND;
			}
		}
	}
	/* out of ammunition (or battery): the other weapon, if it's any better
	   (both empty: keep this one and use our fists, rather than swapping
	   back and forth) */
	if (vehicle_index == NONE && !weapon_chosen && !weapon.none &&
		((weapon.uses_ammunition && weapon.rounds_loaded <= 0 && weapon.rounds_total <= 0) || weapon.spent) &&
		unit->unit.current_weapon_index >= 0 && unit->unit.current_weapon_index <= 1)
	{
		short other_slot = (short)(unit->unit.current_weapon_index == 0 ? 1 : 0);
		long other_index = unit->unit.weapon_object_indices[other_slot];
		if (other_index != NONE && object_try_and_get(other_index) && bot_weapon_worth(other_index) > 0.3f)
			action->desired_weapon_index = other_slot;
	}

	{
		static int log = -1;
		if (log < 0)
			log = bot_logging();
		if (log && game_time_get() % 150 == (player_absolute_index * 17) % 150)
		{
			bot_log("bot%ld t%ld pos %.1f %.1f %.1f goal %ld (%.1f %.1f) area %ld reach %ld target %lx d %.1f veh %lx %s%s wpn %.1f ammo %d/%d areas %ld stuck %ld aggr %.2f flags %lx cw %d %s gren %d/%d",
				player_absolute_index, game_time_get(), position.x, position.y, position.z, bot->goal_kind, bot->goal.x, bot->goal.y,
				bot->mover.move.area, bot->mover.move.reach, (unsigned long)target, target != NONE ? target_distance : -1.f,
				(unsigned long)vehicle_index, driving ? "driver" : "", gunner ? "gunner" : "", weapon.value,
				weapon.rounds_loaded, weapon.rounds_total, aas_ready() ? aas_area_count(AAS_FOOT) : 0L, bot->mover.move.stuck_count, bot->personality.aggression, (unsigned long)action->control_flags, unit->unit.current_weapon_index,
				unit->unit.current_weapon_index >= 0 && unit->unit.weapon_object_indices[unit->unit.current_weapon_index] != NONE ? tag_get_name(object_get(unit->unit.weapon_object_indices[unit->unit.current_weapon_index])->definition_index) : "-", unit->unit.grenade_counts[0], unit->unit.grenade_counts[1]);
		}
	}
}

/* ---------- the bots of the games this machine hosts

A game's bots are players of its lobby, on machines of their own that the
host's server adds (network_server_manager.c), so every machine has them in
the game's settings and makes their players. Each tick the server asks here
for their input (bot_think), which the host's update takes as it takes a
client machine's players' (player_queues_new.c). */

const char *config_string(char const *name);
long config_integer(char const *name);
void platform_log(char const *format, ...);

/* (set from the console: bots, bot_skill; NONE: the settings') */
static long bots_count_setting = NONE;
static boolean bot_skill_set;

void bot_log(char const *format, ...)
{
	char text[512];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	platform_log(strncmp(text, "bots: ", 6) ? "bots: %s" : "%s", text);
}

static void bots_settings_apply(void)
{
	if (!bot_skill_set)
	{
		long skill = config_integer("bots.skill");

		bot_skill_set = TRUE;
		if (skill >= 1)
			bot_skill = PIN(skill, 1, 5);
	}
}

long bots_configured(void)
{
	long count;

	bots_settings_apply();
	count = bots_count_setting != NONE ? bots_count_setting : config_integer("bots.count");

	return PIN(count, 0, BOT_MAXIMUM_PLAYERS - 1);
}

void bots_set_count(long count)
{
	bots_count_setting = PIN(count, 0, BOT_MAXIMUM_PLAYERS - 1);
}

/* bots.fill: the bots fill the game up to this many players with the people
   in it, fewer as people join and more as they leave, in the lobby and in
   the game (network_server_manager.c); 0: bots.count of them, whoever joins */
static long bots_fill_setting = NONE;

long bots_fill(void)
{
	long fill = bots_fill_setting != NONE ? bots_fill_setting : config_integer("bots.fill");

	return PIN(fill, 0, BOT_MAXIMUM_PLAYERS - 1);
}

void bots_set_fill(long fill)
{
	bots_fill_setting = PIN(fill, 0, BOT_MAXIMUM_PLAYERS - 1);
}

void bots_set_skill(long skill)
{
	bot_skill_set = TRUE;
	bot_skill = PIN(skill, 1, 5);
}

long bots_get_skill(void)
{
	bots_settings_apply();
	return bot_skill;
}

/* The map's navigation is worked out (or read from its cache) the first
   time a bot thinks: half a second for a big map from the cache, ten the
   first time ever. In the game's first tick that is a stall of the host
   with the game already running on every client. So a machine that hosts a
   game with bots does it here, when the map has loaded and its objects are
   placed: the other machines are loading too, and wait for the host as
   they wait for one another. */
void bots_map_loaded(void)
{
	struct scenario *scenario = global_scenario_get();

	/* (and when the navigation is being baked, bot_bake.c: whoever plays) */
	if (game_connection() != _game_connection_network_server || !scenario ||
		scenario->type != _scenario_type_multiplayer || (bots_configured() <= 0 && bots_fill() <= 0 && !bot_bake_enabled()))
	{
		return;
	}
	nav_update();
}

/* the console's "bots [count]" and "bot_skill [1-5]" (hs.c): TRUE when the
text was one of them. The count is the lobby's at once, and a game in
progress keeps the bots it started with. */
boolean bots_console_command(char const *text)
{
	void console_printf(boolean clear, const char *format, ...);
	char word[16];
	long length = 0;
	long value = NONE;

	while (*text == ' ' || *text == '\t' || *text == '(')
		text++;
	while (*text && *text != ' ' && *text != '\t' && *text != ')' && length < (long)sizeof(word) - 1)
		word[length++] = (char)tolower((unsigned char)*text++);
	word[length] = 0;
	if (strcmp(word, "bots") && strcmp(word, "bot_skill") && strcmp(word, "bots_fill"))
		return FALSE;
	while (*text == ' ' || *text == '\t')
		text++;
	if (*text >= '0' && *text <= '9')
		value = atol(text);
	if (!strcmp(word, "bots_fill"))
	{
		if (value != NONE)
			bots_set_fill(value);
		if (bots_fill() > 0)
			console_printf(FALSE, "bots_fill: bots fill the games this machine hosts to %ld players, making way for people", bots_fill());
		else
			console_printf(FALSE, "bots_fill: off (0): the games have \"bots\" bots, whoever joins");
	}
	else if (!strcmp(word, "bots"))
	{
		if (value != NONE)
			bots_set_count(value);
		console_printf(FALSE, "bots: %ld in the games this machine hosts%s", bots_configured(),
			value != NONE ? " (from the next lobby on, if a game is running)" : "");
	}
	else
	{
		if (value != NONE)
			bots_set_skill(value);
		console_printf(FALSE, "bot_skill: %ld (1 easy, 2 normal, 3 hard, 4 legendary, 5 near perfect)", bots_get_skill());
	}
	return TRUE;
}

void bots_map_disposed(void)
{
	bot_name_cache_map++;
	bots_match_seed = 0;
	{
		long i;
		memset(bot_states, 0, sizeof(bot_states));
		for (i = 0; i < BOT_MAXIMUM_PLAYERS; i++)
			bot_states[i].unit_index = NONE;
	}
	aas_dispose();
	nav_danger_reset();
	/* (game time starts again at 0: last game's visits, squads and who was
	   alive mean nothing now) */
	memset(nav_visited, 0, sizeof(nav_visited));
	memset(bot_squads, 0, sizeof(bot_squads));
	bot_squads[0].leader = bot_squads[1].leader = NONE;
	memset(bots_alive_unit, 0, sizeof(bots_alive_unit));
	memset(bots_kills_counted, 0, sizeof(bots_kills_counted));
	memset(bots_killer, 0, sizeof(bots_killer));
	memset(bots_killed_time, 0, sizeof(bots_killed_time));
	memset(bots_spawned_time, 0, sizeof(bots_spawned_time));
	nav_vehicle_forget();
	nav_aas_tried = FALSE;
	nav_aas_bsp = -1;
	nav_flood_done = FALSE;
	bots_vantage_count = 0;
	bots_last_graph_tick = -1;
}

static char const *const bot_names[] = { "Anvil", "Brick", "Cobalt", "Dagger", "Ember", "Flint", "Granite", "Hawk",
	"Iron", "Jackal", "Kestrel", "Lance", "Mako", "Nomad", "Onyx", "Pike" };

/* past the first sixteen, numbered (a player's name is 11 letters) */
char const *bot_name(long slot)
{
	static char names[8][16];
	static long next;
	char *name;
	if (slot < 16)
		return bot_names[slot % 16];
	name = names[next++ & 7];
	snprintf(name, sizeof(names[0]), "%s%ld", bot_names[slot % 16], slot / 16 + 1);
	return name;
}
