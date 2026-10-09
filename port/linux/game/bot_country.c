/*
BOT_COUNTRY.C

The bots across country: over the ground between the parts of a map that
its navigation does not join.

The navigation (bot_nav.c, bot_aas.c) knows a floor to the hand's width, and
on a map a mile or two across it is worked out only round where the map is
played (navm_choose_parts): its bases, its weapons, its vehicles. Between
those parts it says nothing, and a bot asked to go from one to another had
no way: it got into a Warthog, found no route for it, and got out again.

Nobody driving from one base to the other needs the ground to a hand's
width. What they need is which way the valley runs. So, for going far:

- The country. The map from above (bot_flight.c's squares, a couple of
  world units across: the floor in the middle of each, the top one, and
  how level it is), and of each square whether something on the ground can
  be on it: a floor, not steeper than a Warthog climbs. Two squares side
  by side join when the step between their floors is no steeper either,
  and is what the floors' own slopes come to; a step that is more, or that
  has a side to it (a ramp, a bridge), is looked at with rays once: no
  kerb in it, and wide enough for wheels. What a map's machines put in
  (a bridge up out of a hangar) is ground too, and what its script does
  with a vehicle driven into a volume (a gate out of a base) is a way.
- Room. A square at the edge of the ground (the foot of a wall, the lip of
  a cliff, the side of a bridge) costs several times one with ground all
  round it, and one next to those more than one out in the open: the way
  goes down the middle of a bridge and round the end of a wall.
- A field. From a place to go to, the way there from every square the
  country joins to it: the search goes out from the place over the whole
  map once, a part of it each tick, and leaves in each square which
  neighbour is the next on the way (as the navigation's routes are searched
  from their goal, and shared by every bot going there). Everyone bound for
  the same end of the map reads the same field; a few are kept.
- The way. From where a bot is, the squares the field leads through for
  some way ahead, and the furthest of them that can be made for in a
  straight line: that is where it steers. What stands on the ground that
  the map does not show (a tree, a rock, another vehicle) is seen by a ray
  and steered round; held up, it backs off and comes again.
- The driving. Onto something narrow along its own line, and up it with a
  run; a corner come to no faster than it is got round on snow; an edge
  that is no part of the way stopped for, and backed away from; what
  hovers steered as what hovers. Stuck time and again, or with no way on,
  the driver gets out, and the vehicle is left where it is by everyone
  for some minutes.

It is used when the navigation cannot say (the place is in another part, or
the bot is out between them), and is no part of how a bot gets about where
the navigation can.
*/

#include "bot_internal.h"
#include "bot_nav_internal.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "physics/collision_bsp_definitions.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

/* a square's side, world units, at the least; and how many squares along
   the longer side of the map at the most */
#define COUNTRY_CELL_FINEST 2.f
#define COUNTRY_MAXIMUM_SIDE 1024
/* a floor no steeper than this is ground to be on (its normal's up: 45
   degrees and a little. A bevel of 45 where a floor meets a ledge is ridden
   over; a hillside of 45 is not climbed all the same: COUNTRY_RISE) */
#define COUNTRY_LEVEL 0.70f
/* ... with this much room over it (a Warthog and its gunner) */
#define COUNTRY_HEADROOM 1.6f
/* ... and the step from one square's floor to the next's no more than
   this for each unit along (40 degrees), */
#define COUNTRY_RISE 0.85f
/* ... nor more than the two floors' own slopes come to over that distance,
   and this (a ramp is gone up; a kerb as high between two level floors is
   not: what a wheel rides over) */
#define COUNTRY_KERB 0.45f
/* ... a step that is more is looked at closely (country_kerbed): the ground
   between the two floors may jump this much and no more. (A Warthog goes
   up a step of 0.9 taken square and at a run: the way onto the platform of
   Coldsnap's south-east base is two of them, a unit apart, and there is
   no other. The edges of the platforms of Hugeass's hangar are 1.8 and
   2.1. A wall of 0.9 with the same ground on both sides is another thing,
   and country_walled's: on top of one a Warthog sits.) */
#define COUNTRY_KERB_HIGHEST 0.6f
/* ... and a step of more than this is only one where the ground goes on at
   its new height (country_kerbed: not a wall with a top, not a rut) */
#define COUNTRY_RIDGE 0.6f
/* the fields kept: the ends of the map people go to */
#define COUNTRY_FIELDS 14
#define COUNTRY_FIELDS_WHOLE 10 /* (of the whole map; the rest are partial ones) */
/* (how high over a floor a wall is looked for: over a kerb that is gone
   over, and no higher. A parapet of 0.9 at the head of Coldsnap's bridge is
   under a ray at 0.9, and a Warthog sent across it sits on top of it) */
#define COUNTRY_BONNET 0.75f
/* (the room a vehicle's body wants from its middle to anything standing) */
#define COUNTRY_BODY_ROOM 1.8f
#define COUNTRY_MICROSECONDS_PER_TICK 6000
/* (how wide a way that has sides has to be for what crosses it: a Ghost,
   and near enough a Warthog's wheels) */
#define COUNTRY_WIDTH_NEEDED 2.f
/* (off a ledge and down: no further than this in one go. As good as any
   distance: a Warthog comes down a ninety-unit cliff on Coldsnap with its
   people alive in it, and the bases of Hugeass are left no other way) */
#define COUNTRY_DROP_HIGHEST 1000.f
/* floors a field's search goes through in one tick, of all the fields' */
#define COUNTRY_STEPS_PER_TICK 60000
/* a place within so many squares of a field's own is that field's */
#define COUNTRY_FIELD_NEAR 6

/* (on a map whose navigation is whole: the ground worked out this far
   round what the navigation covers, and no further) */
#define COUNTRY_ROUND_THE_AREAS 30.f

/* A lift: a ramp the map has that is too narrow for wheels (the way up
   onto the platform of Coldsnap's south-east base, 0.8 of a unit across,
   where its ten Warthogs stand below it and the gate they leave by is
   beyond it) and the only way up there is. A vehicle a bot drives to the
   foot of one is carried up it, as it is put into the frame of a gate. */
#define COUNTRY_LIFTS 12
#define COUNTRY_SQUEEZE_LEAST 0.5f /* as narrow as that (measured a quarter of a unit at a time), and no narrower */
#define COUNTRY_LIFT_RISE 0.9f /* up at least this much: less is a kerb */
#define COUNTRY_LIFT_PACE 4.5f /* world units a second, up it */
#define COUNTRY_RIDE_HEIGHT 0.4f /* a vehicle's own middle over the ground it stands on */

/* the ways a map's script carries a vehicle (hs_find_scripted_teleports) */
#define COUNTRY_PORTALS 79 /* (a floor's way on is a byte: so many and no more) */
/* the sets of them that one vehicle or another may go by (0: none) */
#define COUNTRY_PORTAL_SETS 32
#define COUNTRY_PORTAL_NAMES 64

/* a floor's way on, in a field: a byte */
enum
{
	/* (0 to 31: a step to the next square over, the way in its low three
	   bits, and which of the floors there it steps onto in the two above) */
	_country_next_goal = 32,
	_country_next_drop = 40, /* (and 3 more: off the edge that way) */
	_country_next_portal = 48, /* (and so many more: into that portal) */
	_country_next_none = 255,
	_country_next_tentative = 0x80, /* (reached, not final: the search is still on its way through) */
};

#define COUNTRY_STEPS 4 /* floors of one square that a floor beside it can step onto, at most */
#define COUNTRY_STEPS_UNKNOWN 0xff
/* a vehicle this near a portal's middle is put in it (bot_country_drive) */
#define COUNTRY_PORTAL_REACH 3.5f

/* ---------- structures */

/* what a surface is in the middle of a square: a floor or something over one */
struct country_hit
{
	float z;
	float up; /* its normal's up: a floor's above 0, a ceiling's below */
	float lean_x, lean_y; /* how it rises to the east and to the north, a unit of height a unit along */
};

/* A way a map's script carries a vehicle: driven into a trigger volume, it
   is put down at a flag somewhere else (out of a base inside a mountain, to
   the open ground by it). For the vehicles the script names, and no others. */
struct country_portal
{
	short volume;
	short flag;
	real_point3d entry; /* the volume's middle */
	real_point3d exit;
	long entry_floor; /* the floors of the country by them (NONE: none) */
	long exit_floor;
	short names[COUNTRY_PORTAL_NAMES];
	long name_count;
	real size; /* the volume's longest side: a gate is a frame; a volume a base wide is something else */
	long lift; /* NONE: the script's. Else which lift it is (no volume, no flag, and anything's to go by) */
};

struct country_lift
{
	real_point3d foot, top, exit; /* on the ground at each */
	real side_x, side_y; /* to the open hand of it, going up (0: neither) */
};

struct country_field
{
	boolean used;
	boolean complete;
	long goal;
	long portals; /* the portals it goes by: the set of them of whoever it was made for */
	boolean drops; /* ... and whether it goes off ledges (a vehicle's does) */
	unsigned long last; /* when it was last asked for */
	long begun; /* the tick its search began */
	/* (a field for one traveller near its place, someone on foot: the search
	   is out from the place only as far as where they are, and not past
	   so much of a way) */
	boolean partial;
	long stop_at;
	float limit;
	unsigned char *next; /* per floor */
};

static struct
{
	boolean ready;
	real x0, y0;
	real cell;
	long width, height;
	/* the floors: every one a vehicle has room on, in every square, a
	   square's together and the lowest first (a hangar's under the mountain
	   that is over it, a bridge's over the river's) */
	long *first; /* per square: its first floor (and one more entry, the count) */
	float *z; /* per floor */
	float *room; /* per floor: how far it is to whatever is over it */
	long *square; /* per floor: the square it is in */
	/* (per floor: too steep to stand on or climb. Kept all the same, as
	   somewhere to come down: a vehicle driven off the top of a cliff goes
	   down its face) */
	unsigned char *steep;
	/* (per floor: how much it rises in a unit along, at the most: 0 for a
	   level floor, 0.9 for one as steep as is ground at all) */
	float *slope;
	float *lean_x, *lean_y; /* (... and which way: its rise a unit east, a unit north) */
	/* (per floor: one of a good many joined together, not a shelf on its
	   own: somewhere a way can end) */
	unsigned char *wide;
	/* (per floor, two bits for each way out of it: whether a step that way
	   which is more than its floors' slopes account for has been looked at
	   closely, and is a ramp after all (1) or a kerb (2): country_kerbed) */
	unsigned short *judged;
	unsigned char *clear; /* (country_clear: how far a floor is from ground that is not crossed, once looked at) */
	unsigned short *walls; /* (country_walled: each way from a floor, 2 bits: not looked at, open, a wall) */
	/* (country_steps: each way from a floor, a byte: which of the next
	   square's floors it steps onto, a bit each for the first seven of
	   them; COUNTRY_STEPS_UNKNOWN until it has been worked out) */
	unsigned char *step_mask;
	long floor_count;
	/* (the machines that are ground too, a bridge, a lift's deck: put in a
	   moment after the game starts, when they stand as they will) */
	boolean machines_in;
	/* per square: something of the map's scenery stands in it or by it (a
	   tree, a rock): looked at for room wherever it is, not at an edge alone */
	unsigned char *scenery;
	struct country_portal portals[COUNTRY_PORTALS];
	long portal_count;
	/* (the sets of portals the map's vehicles have: which portals are in each) */
	unsigned char portal_sets[COUNTRY_PORTAL_SETS][COUNTRY_PORTALS];
	long portal_set_count;
	struct country_lift lifts[COUNTRY_LIFTS];
	long lift_count;
	struct country_field fields[COUNTRY_FIELDS];
	unsigned long clock;
	/* the field being worked out (one at a time) */
	long building;
	float *cost;
	long *heap;
	float *heap_key;
	long heap_count;
	long heap_capacity;
	long steps_tick;
	long steps_used;
} country;

/* (the kerb a wheel goes up as it is; testing, HALO_BOT_COUNTRYKERB=<height>:
   another highest kerb than COUNTRY_KERB_HIGHEST) */
static real const country_kerb = COUNTRY_KERB;
static real country_kerb_highest = COUNTRY_KERB_HIGHEST;

static long const country_dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
static long const country_dy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
static float const country_length[8] = { 1.f, 1.f, 1.f, 1.f, 1.41421356f, 1.41421356f, 1.41421356f, 1.41421356f };

long game_time_get(void);

/* ---------- the country */

void bot_country_dispose(void)
{
	long i;
	for (i = 0; i < COUNTRY_FIELDS; i++)
		free(country.fields[i].next);
	free(country.first);
	free(country.z);
	free(country.room);
	free(country.square);
	free(country.steep);
	free(country.slope);
	free(country.lean_x);
	free(country.lean_y);
	free(country.wide);
	free(country.judged);
	free(country.scenery);
	free(country.clear);
	free(country.walls);
	free(country.step_mask);
	free(country.cost);
	free(country.heap);
	free(country.heap_key);
	memset(&country, 0, sizeof(country));
	country.building = NONE;
}

boolean bot_country_ready(void)
{
	return country.ready;
}

static long country_square_at(real x, real y)
{
	long cx = (long)floor((x - country.x0) / country.cell);
	long cy = (long)floor((y - country.y0) / country.cell);
	if (cx < 0 || cy < 0 || cx >= country.width || cy >= country.height)
		return NONE;
	return cy * country.width + cx;
}

static void country_floor_point(long f, real_point3d *point)
{
	long c = country.square[f];
	point->x = country.x0 + (c % country.width + 0.5f) * country.cell;
	point->y = country.y0 + (c / country.width + 0.5f) * country.cell;
	point->z = country.z[f];
}

/* The floors of the next square over (direction 0 to 7) that a floor steps
   onto: each of them that is no more of a step up or down than is climbed.
   More than one where floors lie close over one another (a ramp over the
   floor it rises from, a deck over a trench): which of them is the way on
   is the field's to say. */
/* A step from one square's floor to the next's that is more of a rise than
   the two floors' own slopes come to: a ramp narrower than the squares, a
   bank, the lip of a rut, which a wheel goes over; or a kerb, the edge of a
   platform, which it does not. The ground between the two, by rays down a
   quarter of a unit apart: a kerb is where it jumps. Looked at once, and
   kept (country.judged) where there is one floor that way to keep it of:
   with two in the next square (a ramp, and the floor under it) what was
   made of the one is not what is made of the other. */
/* A floor with a side to it, crossed along x or y: in the square to one hand
   or the other there is no floor that carries its own on (by how it slopes).
   The edge of a ramp, of a bridge, of a ledge along a cliff. */
static boolean country_has_side(long f, long direction)
{
	long const hands[2] = { direction < 2 ? 2 : 0, direction < 2 ? 3 : 1 };
	long c = country.square[f];
	long k;
	for (k = 0; k < 2; k++)
	{
		long x = c % country.width + country_dx[hands[k]];
		long y = c / country.width + country_dy[hands[k]];
		real expected = country.z[f] + country.cell * (country.lean_x[f] * country_dx[hands[k]] + country.lean_y[f] * country_dy[hands[k]]);
		long n, g;
		if (x < 0 || y < 0 || x >= country.width || y >= country.height)
			return TRUE;
		n = y * country.width + x;
		for (g = country.first[n]; g < country.first[n + 1]; g++)
			if (fabs(country.z[g] - expected) <= COUNTRY_KERB)
				break;
		if (g == country.first[n + 1])
			return TRUE;
	}
	return FALSE;
}

/* (while every step is looked at, country_prepare: the steps that failed
   only for want of width, and not by much) */
static struct country_squeeze { long f, g; long direction; } *country_squeezes;
static long country_squeeze_count, country_squeeze_capacity;
static boolean country_squeeze_noting;

static boolean country_kerbed(long f, long g, long direction, boolean keep)
{
	unsigned long const flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_machines_bit) | FLAG(_collision_test_objects_scenery_bit);
	long known = keep ? (country.judged[f] >> (direction * 2)) & 3 : 0;
	real_point3d from, to;
	real top, length, before;
	long samples, i;
	boolean kerbed = FALSE;

	if (known)
		return known == 2;
	country_floor_point(f, &from);
	country_floor_point(g, &to);
	top = MAX(from.z, to.z) + 0.9f;
	length = (real)fabs(to.z - from.z) + 2.4f;
	samples = (long)(bot_distance2d(&from, &to) / 0.25f) + 1;
	before = from.z;
	{
		/* (A step of some height in it, up or down: what the ground does
		   for a vehicle's length past the step, on beyond the far floor if
		   need be. A step up onto something that goes on, a platform, the
		   next step of a stair, is climbed at a run; a step up and down
		   again, a wall, a parapet with its top in a square's middle, is a
		   Warthog sat on top of it with its wheels in the air; a step down
		   and up again is one in a rut.) */
		real step = 0.f, landed = 0.f;
		long step_at = 0;
		real at_end = from.z;
		for (i = 1; i <= samples + (step != 0.f ? MAX(0, step_at + 10 - samples) : 0) && !kerbed; i++)
		{
			struct collision_result collision;
			real_point3d at;
			real_vector3d down = { 0.f, 0.f, 0.f };
			at.x = from.x + (to.x - from.x) * i / samples;
			at.y = from.y + (to.y - from.y) * i / samples;
			at.z = top + 0.3f;
			down.k = -(length + 0.6f);
			if (!collision_test_vector(flags, &at, &down, NONE, &collision))
			{
				/* (nothing under it there: no going that way on wheels; past
				   the far floor, the end of the look) */
				if (i <= samples)
					kerbed = TRUE;
				break;
			}
			if (i <= samples)
			{
				/* (a jump: no going that way on wheels) */
				if (fabs(collision.point.z - before) > country_kerb_highest)
					kerbed = TRUE;
				else if (fabs(collision.point.z - before) > COUNTRY_RIDGE && step == 0.f)
				{
					step = collision.point.z - before;
					landed = collision.point.z;
					step_at = i;
				}
				before = collision.point.z;
				at_end = before;
			}
			if (step != 0.f && i > step_at && i <= step_at + 10 &&
				(step > 0.f ? landed - collision.point.z : collision.point.z - landed) > 0.5f * (real)fabs(step))
			{
				kerbed = TRUE;
			}
		}
		before = at_end;
	}
	if (!kerbed && fabs(before - to.z) > country_kerb_highest)
		kerbed = TRUE;
	/* (... and wide enough for wheels: the same ground a vehicle's half
	   width to each hand of the way, half way along it and at its end. The
	   skirt of a platform, a unit wide and sloped along the platform's
	   face, is a ramp down the middle of its square and no way up for
	   anything wider than a person) */
	if (!kerbed && direction < 4 && (country_has_side(f, direction) || country_has_side(g, direction)))
	{
		real lx = to.x - from.x, ly = to.y - from.y;
		real run = (real)sqrt(lx * lx + ly * ly);
		real jump = 0.2f + 0.25f * MIN(MAX(country.slope[f], country.slope[g]), 1.f);
		real narrowest = 1.0e9f;
		long part, side;
		for (part = 1; part <= 2; part++)
		{
			real_point3d middle;
			real width = 0.f;
			middle.x = from.x + lx * part * 0.5f;
			middle.y = from.y + ly * part * 0.5f;
			/* (out to each hand, a quarter of a unit at a time, for as far
			   as the ground goes on as it is: the way's width there. Its
			   middle need not be the square's) */
			for (side = 0; side < 2; side++)
			{
				real last = 0.f;
				real off;
				for (off = 0.f; off <= COUNTRY_WIDTH_NEEDED; off += 0.25f)
				{
					struct collision_result collision;
					real_point3d at;
					real_vector3d down = { 0.f, 0.f, 0.f };
					real signed_off = side ? -off : off;
					at.x = middle.x - ly / run * signed_off;
					at.y = middle.y + lx / run * signed_off;
					at.z = top + 0.6f;
					down.k = -(length + 1.2f);
					if (!collision_test_vector(flags, &at, &down, NONE, &collision) || (off > 0.f && fabs(collision.point.z - last) > jump))
						break;
					last = collision.point.z;
					if (off > 0.f)
						width += 0.25f;
				}
			}
			if (width + 0.01f < COUNTRY_WIDTH_NEEDED)
				kerbed = TRUE;
			narrowest = MIN(narrowest, width);
		}
		if (kerbed && narrowest + 0.01f >= COUNTRY_SQUEEZE_LEAST && country_squeeze_noting)
		{
			if (country_squeeze_count >= country_squeeze_capacity)
			{
				long capacity = country_squeeze_capacity ? country_squeeze_capacity * 2 : 1024;
				struct country_squeeze *more = (struct country_squeeze *)realloc(country_squeezes, capacity * sizeof(*more));
				if (more)
				{
					country_squeezes = more;
					country_squeeze_capacity = capacity;
				}
			}
			if (country_squeeze_count < country_squeeze_capacity)
			{
				country_squeezes[country_squeeze_count].f = f;
				country_squeezes[country_squeeze_count].g = g;
				country_squeezes[country_squeeze_count++].direction = direction;
			}
		}
	}
	/* testing (HALO_BOT_COUNTRYJUDGE=x,y): what was made of the steps from the square there */
	if (getenv("HALO_BOT_COUNTRYJUDGE"))
	{
		real x = 0.f, y = 0.f;
		sscanf(getenv("HALO_BOT_COUNTRYJUDGE"), "%f,%f", &x, &y);
		if (fabs(from.x - x) < 5.f && fabs(from.y - y) < 1.2f)
			bot_log("countryjudge t%ld from %.1f %.1f %.2f to %.1f %.1f %.2f (way %ld): %s at sample %ld of %ld, ground %.2f before", (long)game_time_get(),
				from.x, from.y, from.z, to.x, to.y, to.z, direction, kerbed ? "kerbed" : "fine", i - 1, samples, before);
	}
	if (keep)
		country.judged[f] |= (unsigned short)((kerbed ? 2 : 1) << (direction * 2));
	return kerbed;
}

/* Something standing between two floors that step one to the other: a wall,
   a railing, a pillar, thinner than the squares and in the middle of
   neither. A ray from over the one's middle to over the other's, at the
   height of a bonnet (a kerb that is gone over is under it). The floors on
   both sides of a parapet are the same floor to the squares, and a Warthog
   sent across one spends its day against it. Looked at once, and kept, as
   country_kerbed's are. */
static boolean country_walled(long f, long g, long direction, boolean keep)
{
	unsigned long const flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_machines_bit) | FLAG(_collision_test_objects_scenery_bit);
	long known = keep ? (country.walls[f] >> (direction * 2)) & 3 : 0;
	struct collision_result collision;
	real_point3d from, to;
	real_vector3d across;
	boolean walled;
	if (known)
		return known == 2;
	country_floor_point(f, &from);
	country_floor_point(g, &to);
	from.z += COUNTRY_BONNET;
	across.i = to.x - from.x;
	across.j = to.y - from.y;
	across.k = to.z + COUNTRY_BONNET - from.z;
	walled = collision_test_vector(flags, &from, &across, NONE, &collision);
	if (keep)
		country.walls[f] |= (unsigned short)((walled ? 2 : 1) << (direction * 2));
	return walled;
}

static long country_steps(long f, long direction, long *to)
{
	long c = country.square[f];
	long x = c % country.width + country_dx[direction];
	long y = c / country.width + country_dy[direction];
	real run = country.cell * country_length[direction];
	real limit = COUNTRY_RISE * run;
	long count = 0;
	long near = 0;
	long n, g;
	unsigned char known;
	if (x < 0 || y < 0 || x >= country.width || y >= country.height || country.steep[f])
		return 0;
	n = y * country.width + x;
	/* (worked out before: as it was found) */
	known = country.step_mask[f * 8 + direction];
	if (known != COUNTRY_STEPS_UNKNOWN)
	{
		for (g = country.first[n]; known && count < COUNTRY_STEPS && g < country.first[n + 1]; g++, known >>= 1)
			if (known & 1)
				to[count++] = g;
		return count;
	}
	for (g = country.first[n]; g < country.first[n + 1]; g++)
		if (!country.steep[g] && fabs(country.z[g] - country.z[f]) <= limit)
			near++;
	for (g = country.first[n]; g < country.first[n + 1] && count < COUNTRY_STEPS; g++)
	{
		real rise = (real)fabs(country.z[g] - country.z[f]);
		/* (what the two floors' own slopes account for, the way the step
		   goes, is let by unlooked at: a ramp is as steep as it is the way
		   it runs, and from the floor beside it its side is a wall; a bank
		   on level ground is a kerb) */
		real expected = 0.5f * country.cell * ((country.lean_x[f] + country.lean_x[g]) * country_dx[direction] +
			(country.lean_y[f] + country.lean_y[g]) * country_dy[direction]);
		/* (... but a slope with a side to it is looked at whatever: it may
		   be no wider than a person) */
		boolean plain = fabs(country.z[g] - country.z[f] - expected) <= country_kerb &&
			!(rise > country_kerb && direction < 4 && (country_has_side(f, direction) || country_has_side(g, direction)));
		if (!country.steep[g] && rise <= limit && (plain || !country_kerbed(f, g, direction, near == 1)) &&
			!country_walled(f, g, direction, near == 1))
		{
			to[count++] = g;
		}
	}
	if (country.first[n + 1] - country.first[n] <= 7)
	{
		long i;
		known = 0;
		for (i = 0; i < count; i++)
			known |= (unsigned char)(1 << (to[i] - country.first[n]));
		country.step_mask[f * 8 + direction] = known;
	}
	return count;
}

/* ... the one of them nearest its own height (NONE: none) */
static long country_step(long f, long direction)
{
	long to[COUNTRY_STEPS];
	long count = country_steps(f, direction, to);
	long best = NONE;
	long i;
	for (i = 0; i < count; i++)
		if (best == NONE || fabs(country.z[to[i]] - country.z[f]) < fabs(country.z[best] - country.z[f]))
			best = to[i];
	return best;
}

/* cornerwise, a step is only taken where both the squares beside the way
   have floor to step onto too (not across the corner of a cliff, of a wall) */
static boolean country_corner_open(long f, long direction)
{
	return direction < 4 ||
		(country_step(f, country_dx[direction] > 0 ? 0 : 1) != NONE && country_step(f, country_dy[direction] > 0 ? 2 : 3) != NONE);
}

/* ... and joined to it, the nearest of them */
static long country_joined(long f, long direction)
{
	return country_corner_open(f, direction) ? country_step(f, direction) : NONE;
}

/* How much room there is round a floor: 1 at the edge of the ground (a
   square beside it, along x or y, that it does not step onto: the foot of
   a wall, the lip of a cliff, the side of a bridge); 2 next to one that is,
   along or cornerwise; 3 with two squares of ground all round it. Looked
   at once, and kept. */
static boolean country_at_edge(long f)
{
	long known = country.clear[f] & 3;
	if (!known)
	{
		known = country_step(f, 0) == NONE || country_step(f, 1) == NONE || country_step(f, 2) == NONE || country_step(f, 3) == NONE ? 1 : 2;
		country.clear[f] |= (unsigned char)known;
	}
	return known == 1;
}

static long country_clear(long f)
{
	long known = (country.clear[f] >> 2) & 3;
	if (!known)
	{
		if (country_at_edge(f))
			known = 1;
		else
		{
			long d;
			known = 3;
			for (d = 0; d < 4 && known == 3; d++)
			{
				long n = country_step(f, d);
				long m;
				if (n == NONE || country_at_edge(n))
					known = 2;
				/* (... and round the corner from it) */
				m = n != NONE ? country_step(n, d < 2 ? 2 + d : 1 - (d - 2)) : NONE;
				if (m == NONE || country_at_edge(m))
					known = 2;
			}
			/* (Next to the edge of the ground by the squares: how near the
			   edge really is. What stands at an edge, a block, the end of a
			   wall, is as big as it is and not as its squares are; a vehicle
			   is a unit and more to each side of its middle. Eight rays out
			   from over the floor's middle, along the lie of the floor:
			   something within a vehicle's half width and a bit, and this is
			   the edge too.) */
			/* (... and out in the open too, where a tree or a rock stands:
			   the squares say nothing of those, the way went through them,
			   and the driver was left to see the tree when it was at it) */
			if (known == 2 || (known == 3 && country.scenery && country.scenery[country.square[f]]))
			{
				unsigned long const flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit) |
					FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_machines_bit) | FLAG(_collision_test_objects_scenery_bit);
				long before = known;
				real_point3d from;
				country_floor_point(f, &from);
				from.z += 0.7f;
				for (d = 0; d < 8 && known == before; d++)
				{
					struct collision_result collision;
					real_vector3d out;
					out.i = country_dx[d] / country_length[d] * COUNTRY_BODY_ROOM;
					out.j = country_dy[d] / country_length[d] * COUNTRY_BODY_ROOM;
					out.k = country.lean_x[f] * out.i + country.lean_y[f] * out.j;
					if (collision_test_vector(flags, &from, &out, NONE, &collision))
						known = 1;
				}
			}
		}
		country.clear[f] |= (unsigned char)(known << 2);
	}
	return known;
}

/* Off the edge of a floor, to the side (direction 0 to 3): where a vehicle
   comes down, in the next square over. The top floor there that is under
   this one, level or steep, with nothing between the two; not a floor it
   steps onto (country_step), and NONE where there is no going off (a wall,
   a floor as high). One way: down a cliff, off a ledge, down the face of
   a slope too steep to come back up. */
static long country_drop(long f, long direction)
{
	long c = country.square[f];
	long x = c % country.width + country_dx[direction];
	long y = c / country.width + country_dy[direction];
	real limit = COUNTRY_RISE * country.cell;
	long n, g;
	if (direction >= 4 || x < 0 || y < 0 || x >= country.width || y >= country.height)
		return NONE;
	n = y * country.width + x;
	/* (the highest of them no higher than this floor) */
	for (g = country.first[n + 1] - 1; g >= country.first[n]; g--)
		if (country.z[g] <= country.z[f] + (country.steep[f] ? 0.f : 0.3f))
			break;
	if (g < country.first[n])
	{
		/* (Nothing under it in the next square: the lip of a ledge, whose
		   face is in no square's middle. The square beyond, then, with
		   nothing of the map in the way out to it: a vehicle goes off a
		   ledge with some way on it.) */
		struct collision_result wall;
		real_point3d from;
		real_vector3d out;
		x += country_dx[direction];
		y += country_dy[direction];
		if (country.steep[f] || x < 0 || y < 0 || x >= country.width || y >= country.height)
			return NONE;
		n = y * country.width + x;
		for (g = country.first[n + 1] - 1; g >= country.first[n]; g--)
			if (country.z[g] <= country.z[f])
				break;
		if (g < country.first[n] || country.z[f] - country.z[g] > COUNTRY_DROP_HIGHEST)
			return NONE;
		country_floor_point(f, &from);
		from.z += 0.8f;
		out.i = country_dx[direction] * country.cell * 2.f;
		out.j = country_dy[direction] * country.cell * 2.f;
		out.k = 0.f;
		if (collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit), &from, &out, NONE, &wall))
			return NONE;
		if (country.z[g] + country.room[g] < country.z[f] + COUNTRY_HEADROOM)
			return NONE;
		return g;
	}
	if (!country.steep[f] && !country.steep[g] && country.z[f] - country.z[g] <= limit)
	{
		long to[COUNTRY_STEPS];
		long count = country_steps(f, direction, to);
		long k;
		for (k = 0; k < count; k++)
			if (to[k] == g)
				return NONE; /* (a step: country_step's) */
	}
	if (country.z[g] + country.room[g] < country.z[f] + COUNTRY_HEADROOM)
		return NONE; /* (something between: a roof it would be under, not a floor it comes down on) */
	if (country.z[f] - country.z[g] > COUNTRY_DROP_HIGHEST)
		return NONE;
	return g;
}

/* a surface's corners and its plane (as bot_nav.c's navm_surface_polygon) */
static long country_surface_polygon(struct collision_bsp *bsp, long s, real_point3d *polygon, real_plane3d *plane)
{
	struct collision_surface const *surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, s, struct collision_surface);
	long count = 0;
	long edge_index = surface->first_edge_index;
	*plane = *TAG_BLOCK_GET_ELEMENT(&bsp->bsp3d.planes, surface->plane_designator & 0x7fffffff, real_plane3d);
	if (surface->plane_designator < 0)
	{
		plane->n.i = -plane->n.i;
		plane->n.j = -plane->n.j;
		plane->n.k = -plane->n.k;
		plane->d = -plane->d;
	}
	do
	{
		struct collision_edge const *edge = TAG_BLOCK_GET_ELEMENT(&bsp->edges, edge_index, struct collision_edge);
		int second = edge->surface_indices[1] == s;
		struct collision_vertex const *vertex = TAG_BLOCK_GET_ELEMENT(&bsp->vertices, edge->vertex_indices[second], struct collision_vertex);
		polygon[count++] = vertex->point;
		edge_index = edge->edge_indices[second];
	}
	while (edge_index != surface->first_edge_index && count < 16);
	return count;
}

/* Every surface that is over the middle of a square, floor or ceiling: a
   first time through to count them a square, a second to keep them (hits:
   NULL the first time; counts: how many a square has so far). */
static void country_surfaces(struct collision_bsp *bsp, long *counts, long const *first, struct country_hit *hits)
{
	long s;
	for (s = 0; s < bsp->surfaces.count; s++)
	{
		real_plane3d plane;
		real_point3d polygon[16];
		long count = country_surface_polygon(bsp, s, polygon, &plane);
		real minimum_x = 1.0e9f, maximum_x = -1.0e9f, minimum_y = 1.0e9f, maximum_y = -1.0e9f;
		long cx0, cx1, cy0, cy1, cx, cy;
		long i;

		/* (not a wall: it is over no square's middle to speak of) */
		if (count < 3 || fabs(plane.n.k) < 0.05f)
			continue;
		for (i = 0; i < count; i++)
		{
			minimum_x = MIN(minimum_x, polygon[i].x);
			maximum_x = MAX(maximum_x, polygon[i].x);
			minimum_y = MIN(minimum_y, polygon[i].y);
			maximum_y = MAX(maximum_y, polygon[i].y);
		}
		cx0 = MAX(0, (long)ceil((minimum_x - country.x0) / country.cell - 0.5f));
		cx1 = MIN(country.width - 1, (long)floor((maximum_x - country.x0) / country.cell - 0.5f));
		cy0 = MAX(0, (long)ceil((minimum_y - country.y0) / country.cell - 0.5f));
		cy1 = MIN(country.height - 1, (long)floor((maximum_y - country.y0) / country.cell - 0.5f));
		for (cy = cy0; cy <= cy1; cy++)
		{
			for (cx = cx0; cx <= cx1; cx++)
			{
				real mx = country.x0 + (cx + 0.5f) * country.cell;
				real my = country.y0 + (cy + 0.5f) * country.cell;
				long c = cy * country.width + cx;
				int positive = 0, negative = 0;
				for (i = 0; i < count; i++)
				{
					real_point3d const *p = &polygon[i];
					real_point3d const *q = &polygon[(i + 1) % count];
					real cross = (q->x - p->x) * (my - p->y) - (q->y - p->y) * (mx - p->x);
					positive += cross > 0.f;
					negative += cross < 0.f;
				}
				if (positive && negative)
					continue;
				if (hits)
				{
					struct country_hit *hit = &hits[first[c] + counts[c]];
					hit->z = polygon[0].z - (plane.n.i * (mx - polygon[0].x) + plane.n.j * (my - polygon[0].y)) / plane.n.k;
					hit->up = plane.n.k;
					hit->lean_x = -plane.n.i / plane.n.k;
					hit->lean_y = -plane.n.j / plane.n.k;
				}
				counts[c]++;
			}
		}
	}
}

static int country_compare_hits(void const *a, void const *b)
{
	float x = ((struct country_hit const *)a)->z, y = ((struct country_hit const *)b)->z;
	return x < y ? -1 : x > y;
}

static long country_floor_near(real_point3d const *point, long rings);
static long country_floor_under(real_point3d const *point, long rings);

void hs_find_scripted_teleports(void (*found)(short trigger_volume_index, short object_name_index, short cutscene_flag_index, void *context), void *context);

static void country_portal_found(short trigger_volume_index, short object_name_index, short cutscene_flag_index, void *context)
{
	struct scenario *scenario = global_scenario_get();
	struct country_portal *portal = NULL;
	long i;

	(void)context;
	if (trigger_volume_index < 0 || trigger_volume_index >= scenario->trigger_volumes.count ||
		cutscene_flag_index < 0 || cutscene_flag_index >= scenario->cutscene_flags.count || object_name_index < 0)
	{
		return;
	}
	for (i = 0; i < country.portal_count; i++)
		if (country.portals[i].volume == trigger_volume_index && country.portals[i].flag == cutscene_flag_index)
			portal = &country.portals[i];
	if (!portal)
	{
		struct scenario_trigger_volume const *volume = TAG_BLOCK_GET_ELEMENT(&scenario->trigger_volumes, trigger_volume_index, struct scenario_trigger_volume);
		struct scenario_cutscene_flag const *flag = TAG_BLOCK_GET_ELEMENT(&scenario->cutscene_flags, cutscene_flag_index, struct scenario_cutscene_flag);
		if (country.portal_count >= COUNTRY_PORTALS)
			return;
		portal = &country.portals[country.portal_count++];
		memset(portal, 0, sizeof(*portal));
		portal->volume = trigger_volume_index;
		portal->flag = cutscene_flag_index;
		portal->exit = flag->position;
		if (volume->type == _scenario_trigger_volume_type_axis_aligned)
		{
			portal->entry.x = (volume->bounds.x0 + volume->bounds.x1) * 0.5f;
			portal->entry.y = (volume->bounds.y0 + volume->bounds.y1) * 0.5f;
			portal->entry.z = (volume->bounds.z0 + volume->bounds.z1) * 0.5f;
			portal->size = MAX(volume->bounds.x1 - volume->bounds.x0, MAX(volume->bounds.y1 - volume->bounds.y0, volume->bounds.z1 - volume->bounds.z0));
		}
		else
		{
			real_matrix4x3 matrix;
			real_point3d middle;
			middle.x = volume->extents.i * 0.5f;
			middle.y = volume->extents.j * 0.5f;
			middle.z = volume->extents.k * 0.5f;
			matrix4x3_from_point_and_vectors(&matrix, &volume->position, &volume->forward, &volume->up);
			matrix4x3_transform_point(&matrix, &middle, &portal->entry);
			portal->size = MAX((real)fabs(volume->extents.i), MAX((real)fabs(volume->extents.j), (real)fabs(volume->extents.k)));
		}
	}
	for (i = 0; i < portal->name_count; i++)
		if (portal->names[i] == object_name_index)
			return;
	if (portal->name_count < COUNTRY_PORTAL_NAMES)
		portal->names[portal->name_count++] = object_name_index;
}

/* the portals a vehicle may go by, those whose script names it: as one of
   the sets of them the map's vehicles have (0: none of them; a Warthog's
   are not a fighter's, and a route is by the ones its vehicle has) */
long bot_country_portals_of(long vehicle_index)
{
	struct object_datum *vehicle = vehicle_index != NONE ? object_try_and_get(vehicle_index) : NULL;
	unsigned char set[COUNTRY_PORTALS];
	boolean any = FALSE;
	long p, i;
	if (!country.ready || !vehicle || vehicle->object.name_index < 0)
		return 0;
	for (p = 0; p < country.portal_count; p++)
	{
		set[p] = country.portals[p].lift != NONE; /* (a lift is anything's) */
		for (i = 0; i < country.portals[p].name_count; i++)
			if (country.portals[p].names[i] == vehicle->object.name_index)
				set[p] = any = TRUE;
	}
	if (!any)
		return 0;
	for (i = 1; i < country.portal_set_count; i++)
		if (!memcmp(country.portal_sets[i], set, country.portal_count))
			return i;
	if (country.portal_set_count >= COUNTRY_PORTAL_SETS)
		return 0;
	memcpy(country.portal_sets[country.portal_set_count], set, country.portal_count);
	return country.portal_set_count++;
}

/* A craft's way out of where it stands by the map's script: of the ways
   that name it, the one whose volume is in sight from here and nearest the
   way it faces (a jet in a closed hangar, with a gate in the wall ahead of
   it that puts it down in the sky outside). NONE: none. */
long bot_country_gate(long vehicle_index, real_point3d const *from, real facing_yaw, real within, long keep, real_point3d *entry, real_point3d *exit_point,
	real *across)
{
	struct object_datum *vehicle = vehicle_index != NONE ? object_try_and_get(vehicle_index) : NULL;
	long best = NONE;
	real best_score = 1.0e9f;
	long p, i;
	if (!country.ready || !vehicle || vehicle->object.name_index < 0)
		return NONE;
	for (p = 0; p < country.portal_count; p++)
	{
		struct country_portal const *portal = &country.portals[p];
		struct collision_result wall;
		real_vector3d way;
		real off = distance3d(from, &portal->entry);
		real turn, score;
		boolean named = FALSE;
		for (i = 0; i < portal->name_count; i++)
			if (portal->names[i] == vehicle->object.name_index)
				named = TRUE;
		/* (a frame to go into, or a mouth a large craft flies into: not a
		   volume the size of the base it stands in, which a script asks
		   about for some other reason; the largest gate of Coldsnap is 62
		   across, the smallest of its bases 133) */
		if (!named || off > within || portal->size > 90.f)
			continue;
		way.i = portal->entry.x - from->x;
		way.j = portal->entry.y - from->y;
		way.k = portal->entry.z - from->z;
		{
			boolean hit = collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit), from, &way, vehicle_index, &wall);
			if (getenv("HALO_BOT_AIRTRACE") && bot_logging())
				bot_log("gate: craft %lx t%ld at %.1f %.1f %.1f: portal %ld at %.1f %.1f %.1f (%.0f across), %.0f off, %s %.1f along at %.1f %.1f %.1f", vehicle_index & 0xffff,
					game_time_get(), from->x, from->y, from->z, p, portal->entry.x, portal->entry.y, portal->entry.z, portal->size, off, hit ? "a wall" : "clear", hit ? wall.t * off : off,
					hit ? wall.point.x : 0.f, hit ? wall.point.y : 0.f, hit ? wall.point.z : 0.f);
			if (hit && wall.t * off < off - 4.f)
				continue;
		}
		turn = (real)fabs(bot_angle_difference(bot_yaw_to(from, &portal->entry), facing_yaw));
		/* (the one it is making for already, sooner than another that is a
		   little nearer now: not from one to the next and back) */
		score = off + 40.f * turn - (p == keep ? 60.f : 0.f);
		if (score < best_score)
		{
			best_score = score;
			best = p;
			*entry = portal->entry;
			*exit_point = portal->exit;
			*across = portal->size;
		}
	}
	return best;
}

/* ... and into it: set down in its volume, as bot_country_drive sets a
   Warthog down in one, for the script to do the rest (FALSE: it is in
   already, and the script has not taken it: nothing more to do here) */
boolean bot_country_gate_enter(long vehicle_index, long gate)
{
	struct country_portal const *portal;
	if (!country.ready || gate < 0 || gate >= country.portal_count || !object_try_and_get(vehicle_index))
		return FALSE;
	portal = &country.portals[gate];
	if (scenario_trigger_volume_test_object(portal->volume, vehicle_index))
		return FALSE;
	object_set_position(vehicle_index, (real_point3d *)&portal->entry, NULL, NULL);
	return TRUE;
}

static void country_debug_box(void)
{
	/* testing (HALO_BOT_COUNTRYAT=x,y[,n]): the floors of the squares from a
	   point eastward (or northward, with n), each with its room and where it
	   steps or comes down that way */
	if (getenv("HALO_BOT_COUNTRYAT"))
	{
		real x = 0.f, y = 0.f;
		char way = 'e';
		long at, k;
		long direction, stride;
		sscanf(getenv("HALO_BOT_COUNTRYAT"), "%f,%f,%c", &x, &y, &way);
		direction = way == 'n' ? 2 : way == 'w' ? 1 : way == 's' ? 3 : 0;
		stride = way == 'n' ? country.width : way == 'w' ? -1 : way == 's' ? -country.width : 1;
		at = country_square_at(x, y);
		for (k = 0; at != NONE && k < 28 && at + k * stride >= 0 && at + k * stride < country.width * country.height; k++)
		{
			char line[400];
			long length = 0;
			long f;
			for (f = country.first[at + k * stride]; f < country.first[at + k * stride + 1] && length < 300; f++)
			{
				long step = country_step(f, direction), drop = country_drop(f, direction);
				length += snprintf(line + length, sizeof(line) - length, " %.1f%s(room %.0f, on %s %.1f)", country.z[f], country.steep[f] ? "s" : "",
					MIN(country.room[f], 9999.f), step != NONE ? "step" : drop != NONE ? "drop" : "none",
					step != NONE ? country.z[step] : drop != NONE ? country.z[drop] : 0.f);
			}
			line[length] = 0;
			bot_log("countryat %.0f %.0f:%s", x + country_dx[direction] * k * country.cell, y + country_dy[direction] * k * country.cell, line);
		}
	}
	/* testing (HALO_BOT_RAYPROBE=x,y,z,yaw,length): rays level from a point,
	   at that height and each tenth of a unit for a unit up: how far each
	   goes, and what stops it */
	if (getenv("HALO_BOT_RAYPROBE"))
	{
		real_point3d from = { 0.f, 0.f, 0.f };
		real yaw = 0.f, length = 2.f, up;
		sscanf(getenv("HALO_BOT_RAYPROBE"), "%f,%f,%f,%f,%f", &from.x, &from.y, &from.z, &yaw, &length);
		for (up = 0.f; up <= 1.01f; up += 0.1f)
		{
			struct collision_result collision;
			real_point3d at = from;
			real_vector3d along;
			at.z += up;
			along.i = (real)cos(yaw) * length;
			along.j = (real)sin(yaw) * length;
			along.k = 0.f;
			if (collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_back_facing_surfaces_bit) |
					FLAG(_collision_test_structure_bit) | FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
					FLAG(_collision_test_objects_machines_bit) | FLAG(_collision_test_objects_weapons_bit), &at, &along, NONE, &collision))
			{
				bot_log("rayprobe z %.2f: stopped %.2f on, at %.2f %.2f %.2f, normal %.2f %.2f %.2f, type %d, object %s", at.z, collision.t * length,
					collision.point.x, collision.point.y, collision.point.z, collision.plane.n.i, collision.plane.n.j, collision.plane.n.k, (int)collision.type,
					collision.object_index != NONE && object_try_and_get(collision.object_index) ? tag_get_name(object_get(collision.object_index)->definition_index) : "-");
			}
			else
				bot_log("rayprobe z %.2f: clear for %.2f", at.z, length);
		}
	}
	/* testing (HALO_BOT_GROUNDPROBE=x0,y0,x1,y1,step,top): the ground itself
	   under a grid of points, looked for from a height down, a row a line
	   with the north at the top: each one's height under the top, in tenths */
	if (getenv("HALO_BOT_GROUNDPROBE"))
	{
		real x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f, step = 1.f, top = 0.f;
		real x, y;
		sscanf(getenv("HALO_BOT_GROUNDPROBE"), "%f,%f,%f,%f,%f,%f", &x0, &y0, &x1, &y1, &step, &top);
		for (y = y1; y >= y0 - 0.001f; y -= step)
		{
			char line[1200];
			long length = 0;
			for (x = x0; x <= x1 + 0.001f && length < 1100; x += step)
			{
				real_point3d at;
				real ground;
				at.x = x, at.y = y, at.z = top;
				if (bot_ground(&at, 12.f, NONE, &ground))
					length += snprintf(line + length, sizeof(line) - length, "%3ld", (long)((top - ground) * 10.f + 0.5f));
				else
					length += snprintf(line + length, sizeof(line) - length, "  -");
			}
			line[length] = 0;
			bot_log("groundprobe %7.2f:%s", y, line);
		}
	}
	/* testing (HALO_BOT_COUNTRYBOX=x0,y0,x1,y1,z0,z1): the floors between two
	   heights in a box of squares, a row of them a line, the north at the top:
	   each square's floor there (its height less z0, in tenths) and a mark, =
	   level, / steep, with ' where it steps east and . where it steps north */
	if (getenv("HALO_BOT_COUNTRYBOX"))
	{
		real x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f, z0 = 0.f, z1 = 0.f;
		long ax, ay, bx, by, x, y;
		sscanf(getenv("HALO_BOT_COUNTRYBOX"), "%f,%f,%f,%f,%f,%f", &x0, &y0, &x1, &y1, &z0, &z1);
		ax = (long)floor((x0 - country.x0) / country.cell);
		bx = (long)floor((x1 - country.x0) / country.cell);
		ay = (long)floor((y0 - country.y0) / country.cell);
		by = (long)floor((y1 - country.y0) / country.cell);
		for (y = MIN(by, country.height - 1); y >= MAX(ay, 0); y--)
		{
			char line[1200];
			long length = 0;
			for (x = MAX(ax, 0); x <= MIN(bx, country.width - 1) && length < 1100; x++)
			{
				long at = y * country.width + x;
				long f;
				long found = NONE;
				for (f = country.first[at]; f < country.first[at + 1]; f++)
					if (country.z[f] >= z0 && country.z[f] <= z1 && (found == NONE || !country.steep[f]))
						found = f;
				if (found == NONE)
					length += snprintf(line + length, sizeof(line) - length, "     ");
				else
					length += snprintf(line + length, sizeof(line) - length, "%3ld%c%c", (long)((country.z[found] - z0) * 10.f),
						country.steep[found] ? '/' : '=', country_step(found, 0) != NONE ? (country_step(found, 2) != NONE ? ':' : '\'') :
						country_step(found, 2) != NONE ? '.' : ' ');
			}
			line[length] = 0;
			bot_log("countrybox %6.0f:%s", country.y0 + (y + 0.5f) * country.cell, line);
		}
	}
}

/* the floors of the country that each portal is between (into a volume over
   a floor: its middle may be in the air over it; out at a flag: put down on
   the floor under it) */
/* (the floor by a portal's volume or flag: one of a good stretch of ground,
   within a few squares and about as high. Not whatever is right under its
   middle: that may be the sill of the frame the volume stands in, or the
   top of it, which nothing drives onto) */
static long country_portal_floor(real_point3d const *point)
{
	long center = country_square_at(point->x, point->y);
	long best = NONE;
	real best_distance = 1.0e9f;
	long dx, dy;
	if (center == NONE)
		return NONE;
	for (dy = -3; dy <= 3; dy++)
	{
		for (dx = -3; dx <= 3; dx++)
		{
			long x = center % country.width + dx, y = center / country.width + dy;
			long c, f;
			if (x < 0 || y < 0 || x >= country.width || y >= country.height)
				continue;
			c = y * country.width + x;
			for (f = country.first[c]; f < country.first[c + 1]; f++)
			{
				real distance = (real)sqrt((real)(dx * dx + dy * dy)) * country.cell + (real)fabs(country.z[f] - point->z) * 2.f;
				if (!country.steep[f] && country.wide[f] && fabs(country.z[f] - point->z) <= 2.5f && distance < best_distance)
				{
					best_distance = distance;
					best = f;
				}
			}
		}
	}
	return best;
}

static void country_portal_floors(void)
{
	long i;
	for (i = 0; i < country.portal_count; i++)
	{
		struct country_portal *portal = &country.portals[i];
		real_point3d down = portal->entry;
		long tries;
		portal->entry_floor = country_portal_floor(&portal->entry);
		for (tries = 0; tries < 8 && portal->entry_floor == NONE; tries++)
		{
			portal->entry_floor = country_floor_under(&down, 2);
			down.z -= 2.f;
		}
		down = portal->exit;
		portal->exit_floor = country_portal_floor(&portal->exit);
		for (tries = 0; tries < 8 && portal->exit_floor == NONE; tries++)
		{
			portal->exit_floor = country_floor_under(&down, 2);
			down.z -= 2.f;
		}
	}
}

/* What is no part of the level's shell but is ground all the same: a
   machine that stands where it is (a bridge let down from a hangar's floor
   to its runway, the deck of a lift). The top of it over each square's
   middle, by a ray down through it; what is under it has it for a roof.
   Done a second into the game, not with the rest when the map loads: a
   machine stands as its animation has it, and has no shape to hit until
   the game has run that. The floors are put in among those of their
   squares, and every field is begun afresh (the floors are numbered anew). */
struct country_addition
{
	long square;
	float z;
	float up;
	float lean_x, lean_y;
	boolean on; /* (ground: joined to the floors there are) */
};

static int country_compare_additions(void const *a, void const *b)
{
	struct country_addition const *x = (struct country_addition const *)a, *y = (struct country_addition const *)b;
	return x->square != y->square ? (x->square < y->square ? -1 : 1) : x->z < y->z ? -1 : x->z > y->z;
}

static void country_fields_forget(void)
{
	long i;
	for (i = 0; i < COUNTRY_FIELDS; i++)
	{
		free(country.fields[i].next);
		memset(&country.fields[i], 0, sizeof(country.fields[i]));
	}
	free(country.cost);
	free(country.heap);
	free(country.heap_key);
	country.cost = NULL;
	country.heap = NULL;
	country.heap_key = NULL;
	country.heap_count = 0;
	country.building = NONE;
}

/* what was made of the ground within a few squares of the marked ones (its
   steps, its walls, its room), forgotten: to be looked at again */
static void country_forget_near(unsigned char const *marked)
{
	long x, y;
	for (y = 0; y < country.height; y++)
	{
		for (x = 0; x < country.width; x++)
		{
			long dx, dy;
			if (!marked[y * country.width + x])
				continue;
			for (dy = -3; dy <= 3; dy++)
			{
				for (dx = -3; dx <= 3; dx++)
				{
					long nx = x + dx, ny = y + dy;
					long f;
					if (nx < 0 || ny < 0 || nx >= country.width || ny >= country.height)
						continue;
					for (f = country.first[ny * country.width + nx]; f < country.first[ny * country.width + nx + 1]; f++)
					{
						country.judged[f] = 0;
						country.walls[f] = 0;
						country.clear[f] = 0;
						memset(country.step_mask + f * 8, COUNTRY_STEPS_UNKNOWN, 8);
					}
				}
			}
		}
	}
}

/* The squares the map's scenery stands in or by (what of it has something
   to run into: a tree, a rock, a barricade), marked; and the room of the
   floors there forgotten, to be looked at again with that in mind
   (country_clear). The ways worked out so far go with it. */
static void country_scenery_mark(void)
{
	struct object_iterator iterator;
	struct object_datum *object;
	long cells = country.width * country.height;
	long marked = 0, things = 0;
	long c, f;
	if (!bot_refined_is(64))
		return;
	free(country.scenery);
	country.scenery = (unsigned char *)calloc(MAX(1, cells), 1);
	if (!country.scenery)
		return;
	object_iterator_new(&iterator, _object_mask_scenery, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		real reach = object->object.bounding_sphere_radius + COUNTRY_BODY_ROOM;
		long x0, x1, y0, y1, x, y;
		if (object_definition_get(object->definition_index)->object.collision_model.index == NONE || object->object.bounding_sphere_radius < 0.2f)
			continue;
		things++;
		x0 = (long)floor((object->object.bounding_sphere_center.x - reach - country.x0) / country.cell);
		x1 = (long)floor((object->object.bounding_sphere_center.x + reach - country.x0) / country.cell);
		y0 = (long)floor((object->object.bounding_sphere_center.y - reach - country.y0) / country.cell);
		y1 = (long)floor((object->object.bounding_sphere_center.y + reach - country.y0) / country.cell);
		for (y = MAX(y0, 0); y <= MIN(y1, country.height - 1); y++)
			for (x = MAX(x0, 0); x <= MIN(x1, country.width - 1); x++)
				country.scenery[y * country.width + x] = 1;
	}
	for (c = 0; c < cells; c++)
	{
		if (!country.scenery[c])
			continue;
		marked++;
		for (f = country.first[c]; f < country.first[c + 1]; f++)
			country.clear[f] &= 3; /* (at an edge or not, as the squares say: kept. The room: again) */
	}
	if (marked > 0)
		country_fields_forget();
	if (bot_logging())
		bot_log("country: %ld things of the map's scenery to drive round, by %ld squares", things, marked);
}

static void country_machines(void)
{
	enum { MAXIMUM_ADDITIONS = 8192 };
	unsigned long const flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_objects_bit) |
		FLAG(_collision_test_objects_machines_bit) | FLAG(_collision_test_objects_scenery_bit);
	struct country_addition *added = (struct country_addition *)malloc(MAXIMUM_ADDITIONS * sizeof(struct country_addition));
	long count = 0;
	long cells = country.width * country.height;
	struct object_iterator iterator;
	struct object_datum *object;
	long *first;
	float *z, *room, *slope, *lean_x, *lean_y;
	long *square;
	unsigned char *steep, *wide;
	unsigned short *judged, *walls;
	unsigned char *clear, *step_mask;
	unsigned char *touched = (unsigned char *)calloc(MAX(1, cells), 1); /* (squares a machine stands over) */
	long total, written = 0, next = 0;
	long c, i;

	if (!added || !touched)
	{
		free(added);
		free(touched);
		return;
	}
	/* (... and its scenery that has a shape: the pier a flag stands on over
	   a canyon is scenery, and so is the rock in the road) */
	object_iterator_new(&iterator, _object_mask_machine | _object_mask_scenery, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		struct object_definition *definition = object_definition_get(object->definition_index);
		real_point3d const *middle = &object->object.bounding_sphere_center;
		real radius = object->object.bounding_sphere_radius;
		long cx0, cx1, cy0, cy1, cx, cy;
		if (definition->object.collision_model.index == NONE || radius < 1.f)
			continue;
		cx0 = MAX(0, (long)ceil((middle->x - radius - country.x0) / country.cell - 0.5f));
		cx1 = MIN(country.width - 1, (long)floor((middle->x + radius - country.x0) / country.cell - 0.5f));
		cy0 = MAX(0, (long)ceil((middle->y - radius - country.y0) / country.cell - 0.5f));
		cy1 = MIN(country.height - 1, (long)floor((middle->y + radius - country.y0) / country.cell - 0.5f));
		for (cy = cy0; cy <= cy1; cy++)
		{
			for (cx = cx0; cx <= cx1; cx++)
				touched[cy * country.width + cx] = TRUE;
			for (cx = cx0; cx <= cx1 && count < MAXIMUM_ADDITIONS; cx++)
			{
				struct collision_result collision;
				real_point3d from;
				real_vector3d down = { 0.f, 0.f, 0.f };
				from.x = country.x0 + (cx + 0.5f) * country.cell;
				from.y = country.y0 + (cy + 0.5f) * country.cell;
				long layer;
				from.z = middle->z + radius + 0.2f;
				/* (every deck of it that is under this point, the top one
				   first: under a canopy, an arch, there is a floor) */
				for (layer = 0; layer < 4 && count < MAXIMUM_ADDITIONS; layer++)
				{
					down.k = -(from.z - (middle->z - radius - 0.2f));
					if (down.k > -0.5f || !collision_test_vector(flags, &from, &down, NONE, &collision))
						break;
					if (collision.object_index == iterator.index && collision.plane.n.k > 0.05f)
					{
						added[count].square = cy * country.width + cx;
						added[count].z = collision.point.z;
						added[count].up = collision.plane.n.k;
						added[count].lean_x = -collision.plane.n.i / collision.plane.n.k;
						added[count].lean_y = -collision.plane.n.j / collision.plane.n.k;
						count++;
					}
					from.z = collision.point.z - 0.6f;
				}
			}
		}
	}
	/* Of those, the ones that are ground: on from a floor that is part of a
	   good stretch of it, by no more of a step than a floor's is; and on
	   from those, the rest of the same machine (a bridge from its foot to its
	   head). The top of a gate's frame, of a holder for a control, is over
	   the floor and joined to nothing: left out, and what is under it left
	   as it was. */
	{
		boolean more = TRUE;
		long kept = 0;
		for (i = 0; i < count; i++)
			added[i].on = FALSE;
		while (more)
		{
			more = FALSE;
			for (i = 0; i < count; i++)
			{
				long x = added[i].square % country.width, y = added[i].square / country.width;
				real own = (real)sqrt(MAX(0.f, 1.f - added[i].up * added[i].up)) / MAX(added[i].up, 0.05f);
				long d;
				if (added[i].on || added[i].up < COUNTRY_LEVEL)
					continue;
				for (d = 0; d < 4 && !added[i].on; d++)
				{
					long nx = x + country_dx[d], ny = y + country_dy[d];
					long n, f, k;
					if (nx < 0 || ny < 0 || nx >= country.width || ny >= country.height)
						continue;
					n = ny * country.width + nx;
					for (f = country.first[n]; f < country.first[n + 1] && !added[i].on; f++)
						if (!country.steep[f] && country.wide[f] &&
							fabs(country.z[f] - added[i].z) <= COUNTRY_KERB + country.cell * MAX(own, country.slope[f]))
						{
							added[i].on = TRUE;
						}
					for (k = 0; k < count && !added[i].on; k++)
						if (added[k].on && added[k].square == n && fabs(added[k].z - added[i].z) <= COUNTRY_KERB + country.cell * own)
							added[i].on = TRUE;
				}
				more = more || added[i].on;
			}
		}
		for (i = 0; i < count; i++)
			if (added[i].on)
				added[kept++] = added[i];
		count = kept;
	}
	if (count == 0)
	{
		/* (none of them ground; but they stand where they stand, and what
		   was made of the ground round them was made without them) */
		country_forget_near(touched);
		country_fields_forget();
		free(touched);
		free(added);
		return;
	}
	bot_sort_stable(added, count, sizeof(struct country_addition), country_compare_additions);
	total = country.floor_count + count;
	first = (long *)malloc((cells + 1) * sizeof(long));
	z = (float *)malloc(total * sizeof(float));
	room = (float *)malloc(total * sizeof(float));
	slope = (float *)malloc(total * sizeof(float));
	lean_x = (float *)malloc(total * sizeof(float));
	lean_y = (float *)malloc(total * sizeof(float));
	square = (long *)malloc(total * sizeof(long));
	steep = (unsigned char *)malloc(total);
	wide = (unsigned char *)malloc(total);
	judged = (unsigned short *)calloc(total, sizeof(unsigned short));
	walls = (unsigned short *)calloc(total, sizeof(unsigned short));
	clear = (unsigned char *)calloc(total, 1);
	step_mask = (unsigned char *)malloc(total * 8);
	if (step_mask)
		memset(step_mask, COUNTRY_STEPS_UNKNOWN, total * 8);
	if (!first || !z || !room || !slope || !lean_x || !lean_y || !square || !steep || !wide || !judged || !walls || !clear || !step_mask)
	{
		free(step_mask);
		free(judged);
		free(walls);
		free(clear);
		free(touched);
		free(lean_x);
		free(lean_y);
		free(first);
		free(z);
		free(room);
		free(slope);
		free(square);
		free(steep);
		free(wide);
		free(added);
		return;
	}
	for (c = 0; c < cells; c++)
	{
		long f = country.first[c];
		long end = country.first[c + 1];
		long last = next;
		while (last < count && added[last].square == c)
			last++;
		first[c] = written;
		/* (a square's floors, the lowest first, the old and the new together) */
		while (f < end || next < last)
		{
			if (next < last && (f >= end || added[next].z < country.z[f]))
			{
				long over;
				real above = 1.0e9f;
				for (over = f; over < end; over++)
				{
					if (country.z[over] > added[next].z + 0.1f)
					{
						above = country.z[over] - added[next].z;
						break;
					}
				}
				if (above >= COUNTRY_HEADROOM)
				{
					z[written] = added[next].z;
					room[written] = above;
					square[written] = c;
					steep[written] = added[next].up < COUNTRY_LEVEL;
					slope[written] = (real)sqrt(MAX(0.f, 1.f - added[next].up * added[next].up)) / MAX(added[next].up, 0.05f);
					lean_x[written] = added[next].lean_x;
					lean_y[written] = added[next].lean_y;
					wide[written] = TRUE;
					written++;
				}
				next++;
			}
			else
			{
				/* (an old one: the room over it may be less now, and not enough) */
				real over_it = country.room[f];
				for (i = next; i < last; i++)
				{
					if (added[i].z > country.z[f] + 0.1f)
					{
						over_it = MIN(over_it, added[i].z - 0.25f - country.z[f]);
						break;
					}
				}
				if (over_it >= COUNTRY_HEADROOM)
				{
					z[written] = country.z[f];
					room[written] = over_it;
					square[written] = c;
					steep[written] = country.steep[f];
					slope[written] = country.slope[f];
					lean_x[written] = country.lean_x[f];
					lean_y[written] = country.lean_y[f];
					/* (what was made of it goes with it) */
					judged[written] = country.judged[f];
					walls[written] = country.walls[f];
					clear[written] = country.clear[f];
					memcpy(step_mask + written * 8, country.step_mask + f * 8, 8);
					wide[written] = country.wide[f];
					written++;
				}
				f++;
			}
		}
	}
	first[cells] = written;
	free(country.first);
	free(country.z);
	free(country.room);
	free(country.slope);
	free(country.lean_x);
	free(country.lean_y);
	free(country.square);
	free(country.steep);
	free(country.wide);
	free(country.judged);
	free(country.clear);
	free(country.walls);
	free(country.step_mask);
	country.judged = judged;
	country.clear = clear;
	country.walls = walls;
	country.step_mask = step_mask;
	country.first = first;
	country.z = z;
	country.room = room;
	country.slope = slope;
	country.lean_x = lean_x;
	country.lean_y = lean_y;
	country.square = square;
	country.steep = steep;
	country.wide = wide;
	country.floor_count = written;
	/* (... but not round a machine: the steps there were looked at without
	   it, and to floors that may be other ones now) */
	country_forget_near(touched);
	free(touched);
	/* (every field, and what the search keeps, were of the old floors) */
	country_fields_forget();
	country_portal_floors();
	if (bot_logging())
		bot_log("country: %ld floors of machines and scenery put in (a bridge, a pier): %ld floors now", count, written);
	free(added);
	country_debug_box();
	{
		void bot_country_debug_way(void);
		bot_country_debug_way();
	}
}

/* ---------- the file */

#define COUNTRY_CACHE_MAGIC 0x59544e43UL /* CNTY */
#define COUNTRY_CACHE_VERSION 8

struct country_cache_header
{
	unsigned int magic;
	unsigned int version;
	char map[128];
	unsigned int signature[4];
	int width, height;
	float cell, x0, y0;
	float kerb, kerb_highest;
	int floor_count;
};

static void country_lifts_find(void);

/* every step from every floor looked at (country_kerbed, country_walled:
   kept as they are found), and the room round every floor (country_clear) */
static void country_prepare(void)
{
	unsigned long long start = bot_time_microseconds();
	long f, d;
	for (f = 0; f < country.floor_count; f++)
	{
		long to[COUNTRY_STEPS];
		if (country.steep[f])
			continue;
		for (d = 0; d < 8; d++)
			country_steps(f, d, to);
	}
	country_squeeze_noting = FALSE;
	for (f = 0; f < country.floor_count; f++)
		if (!country.steep[f])
			country_clear(f);
	country_lifts_find();
	if (bot_logging())
		bot_log("country: every step looked at, %.2f s (%ld of them too narrow by a little, %ld lifts)", (bot_time_microseconds() - start) / 1.0e6,
			country_squeeze_count, country.lift_count);
	free(country_squeezes);
	country_squeezes = NULL;
	country_squeeze_count = country_squeeze_capacity = 0;
}

/* The lifts, of the steps that were too narrow (country_prepare has just
   looked at every step): a run of them one after another the same way, up;
   from the floor at its foot to somewhere with room at its top; and only
   where that is somewhere the ground at its foot does not lead to already
   (a platform with a proper ramp as well has no need of one, and a ramp
   for people into a building is no way in for a Warthog). */
static void country_lifts_find(void)
{
	long i, j;
	country.lift_count = 0;
	for (i = 0; i < country_squeeze_count && country.lift_count < COUNTRY_LIFTS; i++)
	{
		struct country_squeeze const *first = &country_squeezes[i];
		long d = first->direction;
		long foot = first->f, top = first->g, exit_floor;
		long hands[2];
		long extra;
		boolean begun = TRUE, more = TRUE;
		if (d >= 4 || country.z[first->g] < country.z[first->f] + 0.15f)
			continue;
		/* (noted twice: the once) */
		for (j = 0; j < i; j++)
			if (country_squeezes[j].f == first->f && country_squeezes[j].g == first->g && country_squeezes[j].direction == d)
				break;
		if (j < i)
			continue;
		/* (the first of its run: none comes up to its foot) */
		for (j = 0; j < country_squeeze_count && begun; j++)
			if (country_squeezes[j].g == foot && country_squeezes[j].direction == d && country.z[foot] > country.z[country_squeezes[j].f] + 0.15f)
				begun = FALSE;
		if (!begun)
			continue;
		while (more)
		{
			more = FALSE;
			for (j = 0; j < country_squeeze_count; j++)
			{
				if (country_squeezes[j].f == top && country_squeezes[j].direction == d && country.z[country_squeezes[j].g] > country.z[top] + 0.15f)
				{
					top = country_squeezes[j].g;
					more = TRUE;
					break;
				}
			}
		}
		/* (... and the last of the rise, where it is wide again) */
		for (extra = 0; extra < 2; extra++)
		{
			long on = country_step(top, d);
			if (on == NONE || country.z[on] < country.z[top] + 0.1f)
				break;
			top = on;
		}
		if (country.z[top] - country.z[foot] < COUNTRY_LIFT_RISE)
			continue;
		/* (the place to be put down: the floor with the most room within a
		   couple of steps of the top, and about as high) */
		{
			long ring[32];
			long count = 0, head = 0;
			long best_clear = 0;
			real best_off = 1.0e9f;
			real_point3d top_point;
			country_floor_point(top, &top_point);
			exit_floor = NONE;
			ring[count++] = top;
			while (head < count)
			{
				long at = ring[head++];
				long clear = country_clear(at);
				real_point3d point;
				long way;
				country_floor_point(at, &point);
				if (clear > best_clear || (clear == best_clear && bot_distance2d(&point, &top_point) < best_off))
				{
					best_clear = clear;
					best_off = bot_distance2d(&point, &top_point);
					exit_floor = at;
				}
				if (bot_distance2d(&point, &top_point) > country.cell * 2.1f)
					continue;
				for (way = 0; way < 8 && count < 32; way++)
				{
					long on = country_joined(at, way);
					long k;
					if (on == NONE || fabs(country.z[on] - country.z[top]) > 0.4f)
						continue;
					for (k = 0; k < count; k++)
						if (ring[k] == on)
							break;
					if (k == count)
						ring[count++] = on;
				}
			}
			if (exit_floor == NONE || best_clear < 2)
				continue;
		}
		/* (not somewhere the foot leads to by the ground: a look outward from
		   the foot over what joins, as far as a base's yard goes) */
		{
			enum { budget = 4000 };
			long *queue = (long *)malloc(budget * sizeof(long));
			unsigned char *seen = (unsigned char *)calloc(MAX(1, country.floor_count), 1);
			long head = 0, tail = 0;
			boolean found = FALSE, whole = TRUE;
			if (!queue || !seen)
			{
				free(queue);
				free(seen);
				continue;
			}
			queue[tail++] = foot;
			seen[foot] = TRUE;
			while (head < tail && !found)
			{
				long at = queue[head++];
				long way;
				for (way = 0; way < 8 && !found; way++)
				{
					long on = country_joined(at, way);
					if (on == NONE || seen[on])
						continue;
					if (on == exit_floor)
						found = TRUE;
					else if (tail < budget)
					{
						seen[on] = TRUE;
						queue[tail++] = on;
					}
					else
						whole = FALSE;
				}
			}
			free(queue);
			free(seen);
			/* (open country at its foot: there is some other way round) */
			if (found || !whole)
				continue;
		}
		{
			struct country_lift *lift = &country.lifts[country.lift_count++];
			real_point3d middle;
			long hand;
			memset(lift, 0, sizeof(*lift));
			country_floor_point(foot, &lift->foot);
			country_floor_point(top, &lift->top);
			country_floor_point(exit_floor, &lift->exit);
			/* (the hand of it with nothing beside it as high: a vehicle is
			   wider than it, and hangs over on that hand) */
			hands[0] = d < 2 ? 2 : 0;
			hands[1] = d < 2 ? 3 : 1;
			country_floor_point(first->g, &middle);
			for (hand = 0; hand < 2; hand++)
			{
				long c = country.square[first->g];
				long x = c % country.width + country_dx[hands[hand]];
				long y = c / country.width + country_dy[hands[hand]];
				boolean open = TRUE;
				long g;
				if (x >= 0 && y >= 0 && x < country.width && y < country.height)
				{
					long n = y * country.width + x;
					for (g = country.first[n]; g < country.first[n + 1]; g++)
						if (country.z[g] > middle.z + 0.3f && country.z[g] < middle.z + 4.f)
							open = FALSE;
				}
				if (open && lift->side_x == 0.f && lift->side_y == 0.f)
				{
					lift->side_x = (real)country_dx[hands[hand]];
					lift->side_y = (real)country_dy[hands[hand]];
				}
				else if (open)
					lift->side_x = lift->side_y = 0.f; /* (both: down its middle) */
			}
			if (bot_logging())
				bot_log("country: a lift: from %.0f %.0f %.1f up to %.0f %.0f %.1f, put down at %.0f %.0f %.1f", lift->foot.x, lift->foot.y, lift->foot.z,
					lift->top.x, lift->top.y, lift->top.z, lift->exit.x, lift->exit.y, lift->exit.z);
		}
	}
}

static void country_cache_header_now(struct country_cache_header *header)
{
	unsigned long signature[4];
	long i;
	memset(header, 0, sizeof(*header));
	header->magic = COUNTRY_CACHE_MAGIC;
	header->version = COUNTRY_CACHE_VERSION;
	navm_signature(header->map, signature);
	for (i = 0; i < 4; i++)
		header->signature[i] = (unsigned int)signature[i];
	header->width = (int)country.width;
	header->height = (int)country.height;
	header->cell = country.cell;
	header->x0 = country.x0;
	header->y0 = country.y0;
	header->kerb = country_kerb;
	header->kerb_highest = country_kerb_highest;
	header->floor_count = (int)country.floor_count;
}

static void country_cache_save(void)
{
	struct country_cache_header header;
	char path[512];
	FILE *file;
	long cells = country.width * country.height;
	int *first;
	boolean whole;
	long c;

	if (getenv("HALO_BOT_NOCACHE"))
		return;
	first = (int *)malloc((cells + 1) * sizeof(int));
	if (!first)
		return;
	for (c = 0; c <= cells; c++)
		first[c] = (int)country.first[c];
	country_cache_header_now(&header);
	navm_cache_path(header.map, "country", navm_is_baking(), path, sizeof(path));
	file = fopen(path, "wb");
	if (!file)
	{
		free(first);
		navm_baked_file(FALSE, path);
		return;
	}
	whole = fwrite(&header, sizeof(header), 1, file) == 1;
	whole = fwrite(first, sizeof(int), (size_t)(cells + 1), file) == (size_t)(cells + 1) && whole;
	whole = fwrite(country.z, sizeof(float), (size_t)country.floor_count, file) == (size_t)country.floor_count && whole;
	whole = fwrite(country.room, sizeof(float), (size_t)country.floor_count, file) == (size_t)country.floor_count && whole;
	whole = fwrite(country.slope, sizeof(float), (size_t)country.floor_count, file) == (size_t)country.floor_count && whole;
	whole = fwrite(country.lean_x, sizeof(float), (size_t)country.floor_count, file) == (size_t)country.floor_count && whole;
	whole = fwrite(country.lean_y, sizeof(float), (size_t)country.floor_count, file) == (size_t)country.floor_count && whole;
	whole = fwrite(country.steep, 1, (size_t)country.floor_count, file) == (size_t)country.floor_count && whole;
	whole = fwrite(country.wide, 1, (size_t)country.floor_count, file) == (size_t)country.floor_count && whole;
	whole = fwrite(country.clear, 1, (size_t)country.floor_count, file) == (size_t)country.floor_count && whole;
	whole = fwrite(country.step_mask, 8, (size_t)country.floor_count, file) == (size_t)country.floor_count && whole;
	{
		int lifts = (int)country.lift_count;
		whole = fwrite(&lifts, sizeof(lifts), 1, file) == 1 && whole;
		whole = fwrite(country.lifts, sizeof(struct country_lift), COUNTRY_LIFTS, file) == COUNTRY_LIFTS && whole;
	}
	whole = fclose(file) == 0 && whole;
	free(first);
	navm_baked_file(whole, path);
	if (!whole)
		remove(path);
	else if (bot_logging())
		bot_log("country: wrote %s", path);
}

/* the country from a file of this map made with these rules, whole: the
   squares' size and place are worked out already, and are the file's */
static boolean country_cache_read(FILE *file)
{
	struct country_cache_header header, now;
	long cells = country.width * country.height;
	size_t floors;
	int *first = NULL;
	int lifts = 0;
	boolean ok;
	long c;

	country.floor_count = 0;
	country.lift_count = 0;
	country_cache_header_now(&now);
	ok = fread(&header, sizeof(header), 1, file) == 1 && header.floor_count >= 0 && header.floor_count < 64 * 1024 * 1024;
	now.floor_count = header.floor_count;
	ok = ok && !memcmp(&header, &now, sizeof(header));
	if (!ok)
		return FALSE;
	floors = (size_t)header.floor_count;
	first = (int *)malloc((cells + 1) * sizeof(int));
	country.first = (long *)malloc((cells + 1) * sizeof(long));
	country.z = (float *)malloc(MAX(1, floors) * sizeof(float));
	country.room = (float *)malloc(MAX(1, floors) * sizeof(float));
	country.slope = (float *)malloc(MAX(1, floors) * sizeof(float));
	country.lean_x = (float *)malloc(MAX(1, floors) * sizeof(float));
	country.lean_y = (float *)malloc(MAX(1, floors) * sizeof(float));
	country.square = (long *)malloc(MAX(1, floors) * sizeof(long));
	country.steep = (unsigned char *)malloc(MAX(1, floors));
	country.wide = (unsigned char *)malloc(MAX(1, floors));
	country.clear = (unsigned char *)malloc(MAX(1, floors));
	country.judged = (unsigned short *)calloc(MAX(1, floors), sizeof(unsigned short));
	country.walls = (unsigned short *)calloc(MAX(1, floors), sizeof(unsigned short));
	country.step_mask = (unsigned char *)malloc(MAX(1, floors) * 8);
	ok = first && country.first && country.z && country.room && country.slope && country.lean_x && country.lean_y && country.square &&
		country.steep && country.wide && country.clear && country.judged && country.walls && country.step_mask &&
		fread(first, sizeof(int), (size_t)(cells + 1), file) == (size_t)(cells + 1) &&
		fread(country.z, sizeof(float), floors, file) == floors &&
		fread(country.room, sizeof(float), floors, file) == floors &&
		fread(country.slope, sizeof(float), floors, file) == floors &&
		fread(country.lean_x, sizeof(float), floors, file) == floors &&
		fread(country.lean_y, sizeof(float), floors, file) == floors &&
		fread(country.steep, 1, floors, file) == floors &&
		fread(country.wide, 1, floors, file) == floors &&
		fread(country.clear, 1, floors, file) == floors &&
		fread(country.step_mask, 8, floors, file) == floors &&
		fread(&lifts, sizeof(lifts), 1, file) == 1 && lifts >= 0 && lifts <= COUNTRY_LIFTS &&
		fread(country.lifts, sizeof(struct country_lift), COUNTRY_LIFTS, file) == COUNTRY_LIFTS &&
		fgetc(file) == EOF;
	if (ok)
		country.lift_count = lifts;
	/* (a damaged file: each square's floors in their place, or none of it) */
	ok = ok && first[0] == 0 && first[cells] == header.floor_count;
	for (c = 0; ok && c < cells; c++)
	{
		long f;
		ok = first[c] <= first[c + 1] && first[c + 1] <= (int)header.floor_count; /* (each within the floors there are, before any is written to) */
		country.first[c] = first[c];
		for (f = first[c]; ok && f < first[c + 1]; f++)
			country.square[f] = c;
	}
	if (ok)
	{
		country.first[cells] = first[cells];
		country.floor_count = header.floor_count;
	}
	free(first);
	return ok;
}

static boolean country_cache_load(void)
{
	char map[128];
	unsigned long signature[4];
	long attempt;

	if (getenv("HALO_BOT_NOCACHE") || navm_is_baking())
		return FALSE;
	navm_signature(map, signature);
	for (attempt = 0; attempt < 2; attempt++)
	{
		char path[512];
		FILE *file;
		boolean ok;

		navm_cache_path(map, "country", attempt == 0, path, sizeof(path));
		file = fopen(path, "rb");
		if (!file)
			continue;
		ok = country_cache_read(file);
		fclose(file);
		if (ok)
		{
			if (bot_logging())
				bot_log("country: read %s", path);
			return TRUE;
		}
		/* (nothing of a file that was not whole) */
		{
			real cell = country.cell, x0 = country.x0, y0 = country.y0;
			long width = country.width, height = country.height;
			bot_country_dispose();
			country.cell = cell;
			country.x0 = x0;
			country.y0 = y0;
			country.width = width;
			country.height = height;
		}
	}
	return FALSE;
}

void bot_country_build(void)
{
	struct collision_bsp *bsp = global_collision_bsp_get();
	unsigned long long start = bot_time_microseconds();
	real minimum_x = 1.0e9f, maximum_x = -1.0e9f, minimum_y = 1.0e9f, maximum_y = -1.0e9f;
	long *counts = NULL;
	long *hit_first = NULL;
	struct country_hit *hits = NULL;
	long cells;
	long total = 0;
	long level = 0;
	long c, i;
	long pass;

	bot_country_dispose();
	if (!bsp || bsp->vertices.count <= 0)
		return;
	/* (from the first step looked at: some are before country_prepare) */
	country_squeeze_count = 0;
	country_squeeze_noting = TRUE;
	country_kerb_highest = getenv("HALO_BOT_COUNTRYKERB") ? (real)atof(getenv("HALO_BOT_COUNTRYKERB")) : COUNTRY_KERB_HIGHEST;
	for (i = 0; i < bsp->vertices.count; i++)
	{
		struct collision_vertex const *vertex = TAG_BLOCK_GET_ELEMENT(&bsp->vertices, i, struct collision_vertex);
		minimum_x = MIN(minimum_x, vertex->point.x);
		maximum_x = MAX(maximum_x, vertex->point.x);
		minimum_y = MIN(minimum_y, vertex->point.y);
		maximum_y = MAX(maximum_y, vertex->point.y);
	}
	/* (a map whose navigation is whole: no more of it than the navigation
	   has, and some way round that. The shell of an island map goes out to
	   its horizon, and squares fine enough for its sea are too coarse for
	   its roads) */
	if (aas_ready() && !bot_nav_in_parts())
	{
		real x0 = 1.0e9f, x1 = -1.0e9f, y0 = 1.0e9f, y1 = -1.0e9f;
		long type;
		for (type = AAS_FOOT; type <= AAS_VEHICLE; type++)
		{
			for (i = 0; i < aas_area_count(type); i++)
			{
				struct aas_area const *area = aas_area(type, i);
				x0 = MIN(x0, area->x0);
				x1 = MAX(x1, area->x1);
				y0 = MIN(y0, area->y0);
				y1 = MAX(y1, area->y1);
			}
		}
		if (x1 > x0 && y1 > y0)
		{
			minimum_x = MAX(minimum_x, x0 - COUNTRY_ROUND_THE_AREAS);
			maximum_x = MIN(maximum_x, x1 + COUNTRY_ROUND_THE_AREAS);
			minimum_y = MAX(minimum_y, y0 - COUNTRY_ROUND_THE_AREAS);
			maximum_y = MIN(maximum_y, y1 + COUNTRY_ROUND_THE_AREAS);
		}
	}
	country.cell = MAX(COUNTRY_CELL_FINEST, MAX(maximum_x - minimum_x, maximum_y - minimum_y) / COUNTRY_MAXIMUM_SIDE);
	country.x0 = (real)floor(minimum_x) - country.cell;
	country.y0 = (real)floor(minimum_y) - country.cell;
	country.width = (long)((maximum_x - country.x0) / country.cell) + 2;
	country.height = (long)((maximum_y - country.y0) / country.cell) + 2;
	cells = country.width * country.height;
	if (country_cache_load())
	{
		country_squeeze_noting = FALSE; /* (noted while it is built, and it was not) */
		goto ready;
	}
	counts = (long *)calloc(cells + 1, sizeof(long));
	hit_first = (long *)malloc((cells + 1) * sizeof(long));
	country.first = (long *)malloc((cells + 1) * sizeof(long));
	if (!counts || !hit_first || !country.first)
		goto no_memory;
	/* the surfaces over each square's middle, a square's together and the
	   lowest first */
	country_surfaces(bsp, counts, NULL, NULL);
	for (c = 0; c < cells; c++)
	{
		hit_first[c] = total;
		total += counts[c];
		counts[c] = 0;
	}
	hit_first[cells] = total;
	hits = (struct country_hit *)malloc(MAX(1, total) * sizeof(struct country_hit));
	if (!hits)
		goto no_memory;
	country_surfaces(bsp, counts, hit_first, hits);
	/* the floors of them: level enough, and with room over them (to the next
	   surface up, a floor's underside or another floor); once through to
	   count them, once to keep them */
	for (pass = 0; pass < 2; pass++)
	{
		long floors = 0;
		for (c = 0; c < cells; c++)
		{
			long h;
			if (pass == 0)
				bot_sort_stable(&hits[hit_first[c]], hit_first[c + 1] - hit_first[c], sizeof(struct country_hit), country_compare_hits);
			else
				country.first[c] = floors;
			for (h = hit_first[c]; h < hit_first[c + 1]; h++)
			{
				long over;
				real room = 1.0e9f;
				if (hits[h].up <= 0.f)
					continue;
				/* (the same floor twice over, two surfaces that meet under the
				   middle: the one) */
				if (h > hit_first[c] && hits[h - 1].up > 0.f && hits[h].z - hits[h - 1].z < 0.05f)
					continue;
				for (over = h + 1; over < hit_first[c + 1]; over++)
				{
					if (hits[over].z > hits[h].z + 0.1f)
					{
						room = hits[over].z - hits[h].z;
						break;
					}
				}
				if (room < COUNTRY_HEADROOM)
					continue;
				if (pass == 1)
				{
					country.z[floors] = hits[h].z;
					country.room[floors] = room;
					country.square[floors] = c;
					country.steep[floors] = hits[h].up < COUNTRY_LEVEL;
					country.slope[floors] = (real)sqrt(MAX(0.f, 1.f - hits[h].up * hits[h].up)) / MAX(hits[h].up, 0.05f);
					country.lean_x[floors] = hits[h].lean_x;
					country.lean_y[floors] = hits[h].lean_y;
					level += hits[h].up >= COUNTRY_LEVEL;
				}
				floors++;
			}
		}
		if (pass == 0)
		{
			country.floor_count = floors;
			country.z = (float *)malloc(MAX(1, floors) * sizeof(float));
			country.room = (float *)malloc(MAX(1, floors) * sizeof(float));
			country.square = (long *)malloc(MAX(1, floors) * sizeof(long));
			country.steep = (unsigned char *)malloc(MAX(1, floors));
			country.slope = (float *)malloc(MAX(1, floors) * sizeof(float));
			country.lean_x = (float *)malloc(MAX(1, floors) * sizeof(float));
			country.lean_y = (float *)malloc(MAX(1, floors) * sizeof(float));
			country.judged = (unsigned short *)calloc(MAX(1, floors), sizeof(unsigned short));
			country.clear = (unsigned char *)calloc(MAX(1, floors), 1);
			country.walls = (unsigned short *)calloc(MAX(1, floors), sizeof(unsigned short));
			country.step_mask = (unsigned char *)malloc(MAX(1, floors) * 8);
			if (country.step_mask)
				memset(country.step_mask, COUNTRY_STEPS_UNKNOWN, MAX(1, floors) * 8);
			if (!country.z || !country.room || !country.square || !country.steep || !country.slope || !country.lean_x || !country.lean_y || !country.judged || !country.clear || !country.walls || !country.step_mask)
				goto no_memory;
		}
		else
			country.first[cells] = floors;
	}
	free(counts);
	free(hit_first);
	free(hits);
	counts = NULL;
	hit_first = NULL;
	hits = NULL;
	/* the floors that are part of a good stretch of ground (a hundred and
	   fifty joined by steps, or more): a place to go to is taken to be on
	   one of those, not on the shelf or the plinth it stands on */
	{
		long *queue = (long *)malloc(MAX(1, country.floor_count) * sizeof(long));
		unsigned char *seen = (unsigned char *)calloc(MAX(1, country.floor_count), 1);
		long f;
		country.wide = (unsigned char *)calloc(MAX(1, country.floor_count), 1);
		if (!queue || !seen || !country.wide)
		{
			free(queue);
			free(seen);
			goto no_memory;
		}
		for (f = 0; f < country.floor_count; f++)
		{
			long head = 0, tail = 0;
			if (seen[f] || country.steep[f])
				continue;
			seen[f] = TRUE;
			queue[tail++] = f;
			while (head < tail)
			{
				long at = queue[head++];
				long d;
				for (d = 0; d < 4; d++)
				{
					long to[COUNTRY_STEPS];
					long count = country_steps(at, d, to);
					long k;
					for (k = 0; k < count; k++)
					{
						if (!seen[to[k]])
						{
							seen[to[k]] = TRUE;
							queue[tail++] = to[k];
						}
					}
				}
			}
			if (tail >= 150)
				for (head = 0; head < tail; head++)
					country.wide[queue[head]] = TRUE;
		}
		free(queue);
		free(seen);
	}
	/* Every step looked at now, and kept in a file for the next time the map
	   is played (country_cache_save): the rays are most of the work of a
	   search, and a driver waiting a minute for the first of a game is a
	   minute of the game. */
	country_prepare();
	country_cache_save();
ready:
	country.ready = TRUE;
	country.portal_set_count = 1; /* (the first: none of them) */
	/* the ways the map's script carries vehicles, and the floors they are
	   between (into a volume over a floor: its middle may be in the air over
	   it; out at a flag: put down on the floor under it) */
	hs_find_scripted_teleports(country_portal_found, NULL);
	for (i = 0; i < country.portal_count; i++)
		country.portals[i].lift = NONE;
	/* (... and the lifts, as ways of the same kind that anything goes by:
	   on a map the navigation has only in parts. On another, everywhere a
	   vehicle is wanted the areas lead to, and a ramp for people goes into
	   a building) */
	for (i = 0; bot_nav_in_parts() && i < country.lift_count && country.portal_count < COUNTRY_PORTALS; i++)
	{
		struct country_portal *portal = &country.portals[country.portal_count];
		long set;
		memset(portal, 0, sizeof(*portal));
		portal->volume = NONE;
		portal->flag = NONE;
		portal->lift = i;
		portal->entry = country.lifts[i].foot;
		portal->exit = country.lifts[i].exit;
		portal->entry.z += COUNTRY_RIDE_HEIGHT;
		portal->exit.z += COUNTRY_RIDE_HEIGHT;
		for (set = 0; set < COUNTRY_PORTAL_SETS; set++)
			country.portal_sets[set][country.portal_count] = TRUE;
		country.portal_count++;
	}
	country_portal_floors();
	for (i = 0; i < country.portal_count; i++)
	{
		struct country_portal *portal = &country.portals[i];
		if (portal->lift != NONE)
		{
			if (bot_logging())
				bot_log("country: portal %ld, lift %ld at %.0f %.0f %.0f (%s) to %.0f %.0f %.0f (%s)", i, portal->lift, portal->entry.x, portal->entry.y, portal->entry.z,
					portal->entry_floor != NONE ? "a floor under it" : "no floor", portal->exit.x, portal->exit.y, portal->exit.z,
					portal->exit_floor != NONE ? "a floor under it" : "no floor");
			continue;
		}
		if (bot_logging())
			bot_log("country: portal %ld, volume %s (%.0f across) at %.0f %.0f %.0f (%s) to flag %s at %.0f %.0f %.0f (%s), for %ld by name", i,
				TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->trigger_volumes, portal->volume, struct scenario_trigger_volume)->name, portal->size,
				portal->entry.x, portal->entry.y, portal->entry.z, portal->entry_floor != NONE ? "a floor under it" : "no floor",
				TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->cutscene_flags, portal->flag, struct scenario_cutscene_flag)->name,
				portal->exit.x, portal->exit.y, portal->exit.z, portal->exit_floor != NONE ? "a floor under it" : "no floor", portal->name_count);
		if (bot_logging() && getenv("HALO_BOT_AIRTRACE"))
		{
			struct scenario_trigger_volume const *volume = TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->trigger_volumes, portal->volume, struct scenario_trigger_volume);
			bot_log("country: portal %ld volume type %d: position %.1f %.1f %.1f extents %.1f %.1f %.1f forward %.2f %.2f %.2f up %.2f %.2f %.2f", i, (int)volume->type,
				volume->position.x, volume->position.y, volume->position.z, volume->extents.i, volume->extents.j, volume->extents.k,
				volume->forward.i, volume->forward.j, volume->forward.k, volume->up.i, volume->up.j, volume->up.k);
		}
	}
	if (bot_logging())
		bot_log("country: %ld x %ld squares of %.1f, %ld floors to cross by and %ld slopes to come down (%ld surfaces over their middles), %ld portals, %.2f s",
			country.width, country.height, country.cell, level, country.floor_count - level, total, country.portal_count,
			(bot_time_microseconds() - start) / 1.0e6);
	{
		void bot_country_debug_way(void);
		bot_country_debug_way();
	}
	/* testing (HALO_BOT_COUNTRYPROFILE=x0,y0,x1,y1,z): the ground along a line,
	   a quarter of a unit apart, by rays down from z */
	if (getenv("HALO_BOT_COUNTRYPROFILE"))
	{
		real x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f, z = 0.f;
		char line[1500];
		long length = 0;
		long samples, k;
		sscanf(getenv("HALO_BOT_COUNTRYPROFILE"), "%f,%f,%f,%f,%f", &x0, &y0, &x1, &y1, &z);
		samples = (long)(sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)) / 0.25f) + 1;
		for (k = 0; k <= samples && length < 1400; k++)
		{
			struct collision_result collision;
			real_point3d at;
			real_vector3d down = { 0.f, 0.f, -12.f };
			at.x = x0 + (x1 - x0) * k / samples;
			at.y = y0 + (y1 - y0) * k / samples;
			at.z = z;
			if (collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit), &at, &down, NONE, &collision))
				length += snprintf(line + length, sizeof(line) - length, " %.2f", collision.point.z);
			else
				length += snprintf(line + length, sizeof(line) - length, " -");
		}
		line[length] = 0;
		bot_log("countryprofile:%s", line);
	}
	country_debug_box();
	if (getenv("HALO_BOT_COUNTRYSURF"))
	{
		real x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f, z0 = 0.f, z1 = 0.f, k0 = 0.f, k1 = 1.f;
		long s2;
		sscanf(getenv("HALO_BOT_COUNTRYSURF"), "%f,%f,%f,%f,%f,%f,%f,%f", &x0, &y0, &x1, &y1, &z0, &z1, &k0, &k1);
		for (s2 = 0; s2 < bsp->surfaces.count; s2++)
		{
			real_point3d polygon[16];
			real_plane3d plane;
			long n = country_surface_polygon(bsp, s2, polygon, &plane);
			real ax = 1e9f, bx = -1e9f, ay = 1e9f, by = -1e9f, az = 1e9f, bz = -1e9f;
			long k;
			for (k = 0; k < n; k++)
			{
				ax = MIN(ax, polygon[k].x); bx = MAX(bx, polygon[k].x);
				ay = MIN(ay, polygon[k].y); by = MAX(by, polygon[k].y);
				az = MIN(az, polygon[k].z); bz = MAX(bz, polygon[k].z);
			}
			if (plane.n.k >= k0 && plane.n.k <= k1 && bx >= x0 && ax <= x1 && by >= y0 && ay <= y1 && bz >= z0 && az <= z1)
				bot_log("countrysurf %ld: x %.1f..%.1f y %.1f..%.1f z %.1f..%.1f normal %.2f %.2f %.2f", s2, ax, bx, ay, by, az, bz, plane.n.i, plane.n.j, plane.n.k);
		}
	}
	/* testing (HALO_BOT_COUNTRYMAP=<file>): the country as a picture, a
	   grey for each square by the height of its top floor, black where
	   there is none, the map's places red (where players start) and yellow,
	   and green what the country joins to HALO_BOT_COUNTRYFROM=x,y,z (a
	   plain PPM, the top of it the map's north) */
	if (getenv("HALO_BOT_COUNTRYMAP"))
	{
		FILE *file = fopen(getenv("HALO_BOT_COUNTRYMAP"), "wb");
		unsigned char *joined = (unsigned char *)calloc(MAX(1, country.floor_count), 1);
		unsigned char *marks = (unsigned char *)calloc(MAX(1, cells), 1);
		real lowest = 1.0e9f, highest = -1.0e9f;
		long f;
		for (f = 0; f < country.floor_count; f++)
		{
			lowest = MIN(lowest, country.z[f]);
			highest = MAX(highest, country.z[f]);
		}
		if (getenv("HALO_BOT_COUNTRYFROM") && joined)
		{
			real_point3d from = { 0.f, 0.f, 0.f };
			long *queue = (long *)malloc(MAX(1, country.floor_count) * sizeof(long));
			long head = 0, tail = 0;
			long seed;
			sscanf(getenv("HALO_BOT_COUNTRYFROM"), "%f,%f,%f", &from.x, &from.y, &from.z);
			seed = country_floor_near(&from, 20);
			if (queue && seed != NONE)
			{
				joined[seed] = TRUE;
				queue[tail++] = seed;
				while (head < tail)
				{
					long at = queue[head++];
					long d;
					for (d = 0; d < 8; d++)
					{
						long to[COUNTRY_STEPS + 1];
						long count = country_corner_open(at, d) ? country_steps(at, d, to) : 0;
						long k;
						if (count == 0 && (to[0] = country_drop(at, d)) != NONE)
							count = 1;
						for (k = 0; k < count; k++)
						{
							if (!joined[to[k]])
							{
								joined[to[k]] = TRUE;
								queue[tail++] = to[k];
							}
						}
					}
					for (d = 0; d < country.portal_count; d++)
					{
						long n = country.portals[d].exit_floor;
						if (country.portals[d].entry_floor == at && n != NONE && !joined[n])
						{
							joined[n] = TRUE;
							queue[tail++] = n;
						}
					}
				}
				bot_log("country: %ld floors got to from %.0f %.0f %.0f (by any portal, and coming down)", tail, from.x, from.y, from.z);
			}
			free(queue);
		}
		for (i = 0; marks && i < aas_place_count(); i++)
		{
			long at = country_square_at(aas_place(i)->x, aas_place(i)->y);
			long dx, dy;
			for (dy = -1; at != NONE && dy <= 1; dy++)
				for (dx = -1; dx <= 1; dx++)
					if (at + dx + dy * country.width >= 0 && at + dx + dy * country.width < cells)
						marks[at + dx + dy * country.width] = 1 + (unsigned char)(i < aas_spawn_place_count());
			if (at != NONE && joined && bot_logging() && i < aas_spawn_place_count() && i % 8 == 0)
			{
				long near = country_floor_near(aas_place(i), 20);
				bot_log("country: place %ld at %.0f %.0f %.0f: %ld floors in its square, the floor by it %s", i, aas_place(i)->x, aas_place(i)->y,
					aas_place(i)->z, country.first[at + 1] - country.first[at], near == NONE ? "none" : joined[near] ? "joined" : "not joined");
			}
		}
		if (file && joined && marks)
		{
			long x, y;
			fprintf(file, "P6\n%ld %ld\n255\n", country.width, country.height);
			for (y = country.height - 1; y >= 0; y--)
			{
				for (x = 0; x < country.width; x++)
				{
					long at = y * country.width + x;
					unsigned char grey = 0;
					unsigned char rgb[3];
					boolean any = FALSE;
					for (f = country.first[at]; f < country.first[at + 1]; f++)
					{
						grey = (unsigned char)(60.f + 195.f * (country.z[f] - lowest) / MAX(highest - lowest, 1.f));
						any = any || joined[f];
					}
					rgb[0] = rgb[1] = rgb[2] = grey;
					if (any)
						rgb[0] = rgb[2] = (unsigned char)(grey / 2);
					if (marks[at] == 2)
						rgb[0] = 255, rgb[1] = 40, rgb[2] = 40;
					else if (marks[at] == 1)
						rgb[0] = 255, rgb[1] = 220, rgb[2] = 0;
					fwrite(rgb, 1, 3, file);
				}
			}
		}
		if (file)
			fclose(file);
		free(joined);
		free(marks);
	}
	return;

no_memory:
	free(counts);
	free(hit_first);
	free(hits);
	bot_log("country: no memory for the country: the bots do not cross this map on the ground");
	bot_country_dispose();
}

/* the floor of the country a point is on: one of its own square's, or of a
   square within a few of it, at the point's height or near it (a vehicle's
   middle is a little over the ground it stands on); NONE when it is on none */
static long country_floor_under(real_point3d const *point, long rings)
{
	long center = country_square_at(point->x, point->y);
	long cx, cy, ring;
	long best = NONE;
	real best_distance = 1.0e9f;

	if (center == NONE)
		return NONE;
	cx = center % country.width;
	cy = center / country.width;
	/* (By the ground that is really under it, first: the floor of its own
	   square or of one beside it that this ground is part of, by height.
	   The floor of its own square is what is under the square's middle, and
	   a vehicle against the side of a block, of a bridge, of a bank, has its
	   middle in the block's square and its wheels on the ground beside it.) */
	{
		struct collision_result collision;
		real_point3d from = *point;
		real_vector3d down = { 0.f, 0.f, -5.f };
		from.z += 0.5f;
		if (collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit) |
				FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_machines_bit) | FLAG(_collision_test_objects_scenery_bit), &from, &down, NONE, &collision))
		{
			long dx, dy;
			for (dy = -1; dy <= 1; dy++)
			{
				for (dx = -1; dx <= 1; dx++)
				{
					long x = cx + dx, y = cy + dy;
					long c, f;
					real mx, my, away;
					if (x < 0 || y < 0 || x >= country.width || y >= country.height)
						continue;
					c = y * country.width + x;
					mx = country.x0 + (x + 0.5f) * country.cell;
					my = country.y0 + (y + 0.5f) * country.cell;
					away = (real)sqrt((mx - point->x) * (mx - point->x) + (my - point->y) * (my - point->y));
					for (f = country.first[c]; f < country.first[c + 1]; f++)
					{
						if (!country.steep[f] && fabs(country.z[f] - collision.point.z) <= 0.4f + away * MAX(country.slope[f], 0.05f) && away < best_distance)
						{
							best_distance = away;
							best = f;
						}
					}
				}
			}
			if (best != NONE)
				return best;
		}
	}
	for (ring = 0; ring <= rings && best == NONE; ring++)
	{
		real reach = 2.5f + ring * country.cell * COUNTRY_RISE;
		long dx, dy;
		for (dy = -ring; dy <= ring; dy++)
		{
			for (dx = -ring; dx <= ring; dx++)
			{
				long x = cx + dx, y = cy + dy;
				long c, f;
				if ((labs(dx) != ring && labs(dy) != ring) || x < 0 || y < 0 || x >= country.width || y >= country.height)
					continue;
				c = y * country.width + x;
				for (f = country.first[c]; f < country.first[c + 1]; f++)
				{
					real off = point->z - country.z[f];
					real distance;
					if (off < -1.5f - ring * country.cell * COUNTRY_RISE || off > reach || (country.steep[f] && ring > 0))
						continue;
					distance = (real)(dx * dx + dy * dy) + (real)fabs(off) * 0.1f;
					if (distance < best_distance)
					{
						best_distance = distance;
						best = f;
					}
				}
			}
		}
	}
	return best;
}

/* the floor a field to a place starts from: the nearest to it, across and
   up or down (a place on a roof, on a ledge: the ground by it), and one
   that is part of a good stretch of ground when there is one */
static long country_floor_near(real_point3d const *point, long rings)
{
	long center = country_square_at(point->x, point->y);
	long cx, cy, ring;
	long best = NONE;
	real best_distance = 1.0e9f;
	long pass;

	if (center == NONE)
		return NONE;
	cx = center % country.width;
	cy = center / country.width;
	for (pass = 0; pass < 2 && best == NONE; pass++)
	{
		for (ring = 0; ring <= rings; ring++)
		{
			long dx, dy;
			/* (nothing further out can be nearer than what is found) */
			if (best != NONE && (ring - 1) * country.cell > best_distance)
				break;
			for (dy = -ring; dy <= ring; dy++)
			{
				for (dx = -ring; dx <= ring; dx++)
				{
					long x = cx + dx, y = cy + dy;
					long c, f;
					if ((labs(dx) != ring && labs(dy) != ring) || x < 0 || y < 0 || x >= country.width || y >= country.height)
						continue;
					c = y * country.width + x;
					for (f = country.first[c]; f < country.first[c + 1]; f++)
					{
						real distance = (real)sqrt((real)(dx * dx + dy * dy)) * country.cell + (real)fabs(country.z[f] - point->z) * 2.f;
						if (!country.steep[f] && (pass == 1 || country.wide[f]) && distance < best_distance)
						{
							best_distance = distance;
							best = f;
						}
					}
				}
			}
		}
	}
	return best;
}

/* ---------- a field */

static void country_heap_push(long c, float key)
{
	long i;
	if (country.heap_count >= country.heap_capacity)
		return;
	i = country.heap_count++;
	while (i > 0 && country.heap_key[(i - 1) / 2] > key)
	{
		country.heap[i] = country.heap[(i - 1) / 2];
		country.heap_key[i] = country.heap_key[(i - 1) / 2];
		i = (i - 1) / 2;
	}
	country.heap[i] = c;
	country.heap_key[i] = key;
}

static long country_heap_pop(float *key)
{
	long top = country.heap[0];
	long last;
	float last_key;
	long i = 0;
	*key = country.heap_key[0];
	country.heap_count--;
	last = country.heap[country.heap_count];
	last_key = country.heap_key[country.heap_count];
	for (;;)
	{
		long child = i * 2 + 1;
		if (child >= country.heap_count)
			break;
		if (child + 1 < country.heap_count && country.heap_key[child + 1] < country.heap_key[child])
			child++;
		if (country.heap_key[child] >= last_key)
			break;
		country.heap[i] = country.heap[child];
		country.heap_key[i] = country.heap_key[child];
		i = child;
	}
	country.heap[i] = last;
	country.heap_key[i] = last_key;
	return top;
}

/* what a step onto a square costs, in world units: its length; more of it
   for a climb; and more again by ground that cannot be crossed (the foot of
   a cliff, the lip of one, the side of a bridge): room is left where there
   is room, and a bridge is crossed down its middle */
static float country_step_cost(long from, long to, long direction)
{
	float length = country_length[direction] * country.cell;
	float rise = (float)fabs(country.z[to] - country.z[from]);
	float cost = length + 2.f * rise;
	long clear = country_clear(to);
	if (clear < 3)
		cost *= clear == 1 ? 4.f : 1.5f;
	return cost;
}

/* the search of the field being worked out, carried on: so many squares of
   it this tick, of all that is asked of it */
static void country_field_advance(void)
{
	struct country_field *field;
	long now = game_time_get();
	unsigned long long started;

	if (country.building == NONE)
		return;
	field = &country.fields[country.building];
	if (country.steps_tick != now)
	{
		country.steps_tick = now;
		country.steps_used = 0;
	}
	started = bot_time_microseconds();
	while (country.heap_count > 0 && country.steps_used < COUNTRY_STEPS_PER_TICK)
	{
		float key;
		/* (... and of the time there is in a tick: a step whose way on has
		   to be looked at with rays is many times the work of one that has
		   not, and a first field over hills is all of them) */
		if ((country.steps_used & 127) == 127 && bot_time_microseconds() - started > COUNTRY_MICROSECONDS_PER_TICK)
			break;
		long c = country_heap_pop(&key);
		long d;
		if (key > country.cost[c] || !(field->next[c] & _country_next_tentative))
			continue; /* (bettered since, or done) */
		/* (as far as it was wanted: no way within reach, or there) */
		if (field->partial && key > field->limit)
		{
			country.heap_count = 0;
			break;
		}
		field->next[c] &= (unsigned char)~_country_next_tentative;
		country.steps_used++;
		if (field->partial && c == field->stop_at)
		{
			country.heap_count = 0;
			break;
		}
		for (d = 0; d < 8; d++)
		{
			/* (the floors that step onto this one: those it steps onto, a
			   step being the same both ways; each steps here the way back,
			   onto this one of the floors it has that way) */
			static long const opposite[8] = { 1, 0, 3, 2, 7, 6, 5, 4 };
			long to[COUNTRY_STEPS];
			long count = country_corner_open(c, d) ? country_steps(c, d, to) : 0;
			long k;
			for (k = 0; k < count; k++)
			{
				long n = to[k];
				long back[COUNTRY_STEPS];
				long back_count, which;
				float through;
				if (field->next[n] != _country_next_none && !(field->next[n] & _country_next_tentative))
					continue;
				if (!country_corner_open(n, opposite[d]))
					continue;
				back_count = country_steps(n, opposite[d], back);
				for (which = 0; which < back_count && back[which] != c; which++)
					;
				if (which == back_count)
					continue;
				through = key + country_step_cost(n, c, d);
				if (field->next[n] == _country_next_none || through < country.cost[n])
				{
					country.cost[n] = through;
					field->next[n] = (unsigned char)((opposite[d] | (which << 3)) | _country_next_tentative);
					country_heap_push(n, through);
				}
			}
		}
		/* (what comes down onto this floor from the squares beside it: off a
		   ledge, down a cliff. One way, and only for a field that is a
		   vehicle's: on foot it is a fall) */
		for (d = 0; field->drops && d < 8; d++)
		{
			/* (from the square beside it, or from the one beyond that: a
			   ledge's lip, country_drop) */
			static long const opposite[4] = { 1, 0, 3, 2 };
			long sq = country.square[c];
			long reach = d < 4 ? 1 : 2;
			long x = sq % country.width + country_dx[d & 3] * reach, y = sq / country.width + country_dy[d & 3] * reach;
			long n;
			if (x < 0 || y < 0 || x >= country.width || y >= country.height)
				continue;
			for (n = country.first[y * country.width + x]; n < country.first[y * country.width + x + 1]; n++)
			{
				float through;
				if (country.z[n] <= country.z[c] || (reach == 2 && country.z[n] < country.z[c] + 3.f) || country_drop(n, opposite[d & 3]) != c)
					continue;
				if (field->next[n] != _country_next_none && !(field->next[n] & _country_next_tentative))
					continue;
				through = key + country.cell + 4.f;
				if (field->next[n] == _country_next_none || through < country.cost[n])
				{
					country.cost[n] = through;
					field->next[n] = (unsigned char)((_country_next_drop + opposite[d & 3]) | _country_next_tentative);
					country_heap_push(n, through);
				}
			}
		}
		/* (a portal that comes out here: from where it goes in, this way) */
		for (d = 0; d < country.portal_count; d++)
		{
			struct country_portal const *portal = &country.portals[d];
			long n = portal->entry_floor;
			float through = key + 6.f;
			if (portal->exit_floor != c || n == NONE || !country.portal_sets[field->portals][d])
				continue;
			/* (a lift is for what is driven: someone on foot walks up it) */
			if (portal->lift != NONE && !field->drops)
				continue;
			if (field->next[n] != _country_next_none && !(field->next[n] & _country_next_tentative))
				continue;
			if (field->next[n] == _country_next_none || through < country.cost[n])
			{
				country.cost[n] = through;
				field->next[n] = (unsigned char)((_country_next_portal + d) | _country_next_tentative);
				country_heap_push(n, through);
			}
		}
	}
	if (country.heap_count == 0)
	{
		field->complete = TRUE;
		country.building = NONE;
		if (bot_logging() && !field->partial)
			bot_log("country: the field is done, %.1f s after it was begun", (now - field->begun) / (real)BOT_TICKS_PER_SECOND);
	}
}

void bot_country_update(void)
{
	/* testing (HALO_BOT_COUNTRYNEAR=x,y,z): what objects there are near a point, two seconds in */
	if (getenv("HALO_BOT_COUNTRYNEAR") && game_time_get() == 60)
	{
		real_point3d at = { 0.f, 0.f, 0.f };
		struct object_iterator iterator;
		struct object_datum *object;
		sscanf(getenv("HALO_BOT_COUNTRYNEAR"), "%f,%f,%f", &at.x, &at.y, &at.z);
		object_iterator_new(&iterator, 0xffff, 0);
		while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
			if (distance3d(&object->object.position, &at) < 40.f)
				bot_log("countrynear: %s type %d at %.1f %.1f %.1f radius %.1f collision %d", tag_get_name(object->definition_index), (int)object->object.type,
					object->object.position.x, object->object.position.y, object->object.position.z, object->object.bounding_sphere_radius,
					object_definition_get(object->definition_index)->object.collision_model.index != NONE);
	}
	if (country.ready && !country.machines_in && game_time_get() >= 30)
	{
		country.machines_in = TRUE;
		country_machines();
		country_scenery_mark();
	}
	if (country.ready)
		country_field_advance();
}

/* the field to a floor: one there is, to there or near it; or a new one,
   begun when no other is being worked out (NULL until then) */
/* The field to a place: one that is kept and ends within `slack` squares of
   it, the nearest such. (From a long way off, the way to anywhere near the
   place is the way to the place: whoever is being driven at across a mile
   of map has moved by the time a field to where they stood is made, and a
   field for every step they take is never finished. The slack is the
   caller's, by how far off it is.) With none, one is begun; and until it is
   done, one that ends within three times the slack does. */
static struct country_field *country_field_to(long goal, long portals, boolean drops, long slack, long start, float limit)
{
	long cells = country.floor_count;
	long gx = country.square[goal] % country.width, gy = country.square[goal] / country.width;
	struct country_field *oldest = NULL;
	struct country_field *nearest = NULL, *meanwhile = NULL;
	long nearest_off = 0, meanwhile_off = 0;
	long i;

	for (i = 0; i < COUNTRY_FIELDS; i++)
	{
		struct country_field *field = &country.fields[i];
		long off;
		if (!field->used || field->portals != portals || field->drops != drops)
			continue;
		/* (a partial one: for whoever it was made for, or anyone on the
		   part of it that was searched) */
		if (field->partial && (start == NONE || (field->stop_at != start &&
			(field->next[start] == _country_next_none || (field->next[start] & _country_next_tentative)))))
		{
			continue;
		}
		off = MAX(labs(country.square[field->goal] % country.width - gx), labs(country.square[field->goal] / country.width - gy));
		off = MAX(off, (long)(fabs(country.z[field->goal] - country.z[goal]) / country.cell));
		if (off <= slack && (!nearest || off < nearest_off))
		{
			nearest = field;
			nearest_off = off;
		}
		if (off <= slack * 3 && field->complete && (!meanwhile || off < meanwhile_off))
		{
			meanwhile = field;
			meanwhile_off = off;
		}
	}
	if (nearest)
	{
		nearest->last = ++country.clock;
		/* (still being made, and another near enough is done: that, for now) */
		if (!nearest->complete && meanwhile)
		{
			meanwhile->last = ++country.clock;
			return meanwhile;
		}
		return nearest;
	}
	if (meanwhile)
		meanwhile->last = ++country.clock;
	if (country.building != NONE)
		return meanwhile;
	for (i = start == NONE ? 0 : COUNTRY_FIELDS_WHOLE; i < (start == NONE ? COUNTRY_FIELDS_WHOLE : COUNTRY_FIELDS); i++)
		if (!oldest || !country.fields[i].used || (oldest->used && country.fields[i].last < oldest->last))
			oldest = &country.fields[i];
	if (!country.cost)
	{
		country.heap_capacity = cells * 2 + 16;
		country.cost = (float *)malloc(MAX(1, cells) * sizeof(float));
		country.heap = (long *)malloc(country.heap_capacity * sizeof(long));
		country.heap_key = (float *)malloc(country.heap_capacity * sizeof(float));
		if (!country.cost || !country.heap || !country.heap_key)
		{
			free(country.cost);
			free(country.heap);
			free(country.heap_key);
			country.cost = NULL;
			country.heap = NULL;
			country.heap_key = NULL;
			return NULL;
		}
	}
	if (!oldest->next)
		oldest->next = (unsigned char *)malloc(MAX(1, cells));
	if (!oldest->next)
		return NULL;
	memset(oldest->next, _country_next_none, cells);
	oldest->used = TRUE;
	oldest->complete = FALSE;
	oldest->goal = goal;
	oldest->portals = portals;
	oldest->drops = drops;
	oldest->last = ++country.clock;
	oldest->begun = game_time_get();
	oldest->partial = start != NONE;
	oldest->stop_at = start;
	oldest->limit = limit;
	oldest->next[goal] = _country_next_goal | _country_next_tentative;
	country.cost[goal] = 0.f;
	country.heap_count = 0;
	country_heap_push(goal, 0.f);
	country.building = (long)(oldest - country.fields);
	if (bot_logging() && start == NONE)
	{
		real_point3d at;
		country_floor_point(goal, &at);
		bot_log("country: a field to %.0f %.0f %.0f (portal set %ld%s)", at.x, at.y, at.z, portals, drops ? ", off ledges" : "");
	}
	country_field_advance();
	return meanwhile ? meanwhile : oldest;
}

/* ---------- the way */

/* the floor a field leads on to from a floor, by a step or off an edge
   (*dropped); NONE at its end, at a portal, or where it has no way */
static long country_follow(struct country_field const *field, long c, boolean *dropped)
{
	long code = field->next[c];
	*dropped = FALSE;
	if (code < _country_next_goal)
	{
		long to[COUNTRY_STEPS];
		long count = country_steps(c, code & 7, to);
		return (code >> 3) < count ? to[code >> 3] : NONE;
	}
	if (code >= _country_next_drop && code < _country_next_drop + 4)
	{
		*dropped = TRUE;
		return country_drop(c, code - _country_next_drop);
	}
	return NONE;
}

/* straight over the country from one floor to another: floor all the way,
   and no step on it that the floors do not join across */
static boolean country_line_open(long from, long to)
{
	long fx = country.square[from] % country.width, fy = country.square[from] / country.width;
	long tx = country.square[to] % country.width, ty = country.square[to] / country.width;
	long steps = MAX(labs(tx - fx), labs(ty - fy)) * 2;
	long last = from;
	long lx = fx, ly = fy;
	/* (from ground with room round it to more of the same: not by way of
	   the edge of it, which a line between the two may well cut across) */
	boolean roomy = country_clear(from) > 1 && country_clear(to) > 1;
	long squares = 0;
	long i;
	for (i = 1; i <= steps; i++)
	{
		long x = fx + (long)floor((tx - fx) * (real)i / (real)steps + 0.5f);
		long y = fy + (long)floor((ty - fy) * (real)i / (real)steps + 0.5f);
		long d;
		if (x == lx && y == ly)
			continue;
		for (d = 0; d < 8; d++)
			if (lx + country_dx[d] == x && ly + country_dy[d] == y)
				break;
		if (d == 8)
			return FALSE;
		last = country_joined(last, d);
		squares++;
		/* (... and from the edge of it or to the edge of it, by the edge
		   for the first of the way or the last of it and no more) */
		if (last == NONE || ((roomy || (squares > 2 && MAX(labs(tx - x), labs(ty - y)) > 2)) && country_at_edge(last)))
			return FALSE;
		lx = x;
		ly = y;
	}
	return last == to;
}

/* (the point bot_country_way last gave is the middle of a portal's volume:
   to be driven right into, wall or no wall) */
static boolean country_way_into_portal;
static long country_way_portal; /* (... which) */
/* (... or is on the way onto something narrow, a bridge, a ramp no wider
   than a vehicle or two: slowly, and straight) */
static boolean country_way_narrow;
static boolean country_way_lined_up; /* (... and on its line, or on it: up it) */
static real country_narrow_half; /* (half its width, as country_narrow_middle last found it) */
/* (... or is a corner of the way: how much of a turn the way takes there) */
static real country_way_turn;
static boolean country_way_dropping; /* (... or is over an edge the way goes down off) */
/* (the way for the next stretch of it: how far round it bends, how far off
   the bend is, and how much room there is beside it: country_clear's least) */
static real country_way_bend, country_way_bend_at;
static long country_way_room;
/* (... or is up a step, a kerb as high as is climbed: taken square, and at
   a run. How far before it the vehicle is, along its line, and which way
   the line runs) */
static boolean country_way_step;
static real country_way_before, country_way_axis;

/* Where the middle of something narrow really is, across the way it runs: a
   bridge two units wide is somewhere in its square, not in the middle of
   it, and a wheel off its edge is a vehicle off it. Rays down, a fifth of a
   unit apart, to each hand of the floor's middle for as far as there is
   floor at its height: how far to one hand the middle of that is. */
static real country_narrow_middle(long f, long direction)
{
	unsigned long const flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_machines_bit) | FLAG(_collision_test_objects_scenery_bit);
	real px = -(real)country_dy[direction], py = (real)country_dx[direction]; /* (to the left of the way) */
	real_point3d middle;
	real reach[2] = { 0.f, 0.f };
	long side;
	country_floor_point(f, &middle);
	for (side = 0; side < 2; side++)
	{
		real sign = side ? -1.f : 1.f;
		real off;
		for (off = 0.2f; off <= country.cell * 1.5f; off += 0.2f)
		{
			struct collision_result collision;
			real_point3d at;
			real_vector3d down = { 0.f, 0.f, -1.6f };
			at.x = middle.x + px * off * sign;
			at.y = middle.y + py * off * sign;
			at.z = middle.z + 0.8f;
			if (!collision_test_vector(flags, &at, &down, NONE, &collision) || fabs(collision.point.z - middle.z) > 0.4f)
				break;
			reach[side] = off;
		}
	}
	country_narrow_half = (reach[0] + reach[1]) * 0.5f;
	return (reach[0] - reach[1]) * 0.5f;
}

/* how many squares wide the ground is at a floor, across the way it is
   crossed (a step along x or y): itself and up to two to each hand */
/* (ground to one hand of a floor: a floor of the next square that way, level
   and about as high. Not only one it steps onto: the top of a bank that is
   crossed has the ground at the bank's foot to each hand of it, a kerb's
   height down, and that is not a bridge with nothing beside it) */
static long country_beside(long f, long direction)
{
	long c = country.square[f];
	long x = c % country.width + country_dx[direction];
	long y = c / country.width + country_dy[direction];
	long n, g;
	long nearest = NONE;
	if (x < 0 || y < 0 || x >= country.width || y >= country.height)
		return NONE;
	n = y * country.width + x;
	for (g = country.first[n]; g < country.first[n + 1]; g++)
		if (!country.steep[g] && fabs(country.z[g] - country.z[f]) <= COUNTRY_KERB_HIGHEST + 0.2f &&
			(nearest == NONE || fabs(country.z[g] - country.z[f]) < fabs(country.z[nearest] - country.z[f])))
		{
			nearest = g;
		}
	return nearest;
}

static long country_width(long f, long direction)
{
	long const left = direction < 2 ? 2 : 0, right = direction < 2 ? 3 : 1;
	long width = 1;
	long n = country_beside(f, left);
	if (n != NONE)
		width += 1 + (country_beside(n, left) != NONE);
	n = country_beside(f, right);
	if (n != NONE)
		width += 1 + (country_beside(n, right) != NONE);
	return width;
}

/* Over the ground from a point toward a place: the point to make for now,
   some way ahead along the way there. FALSE when there is none to give:
   *soon when that is because the way is still being worked out (ask again),
   *off when the point is not on the country at all (under a roof, in a
   building: the navigation's to get out of), and neither when the country
   does not join the two. *left: how far it still is by the way, about. */
static boolean country_way(real_point3d const *from, real_point3d const *to, long portals, boolean drops, real ahead, real_point3d *next,
	boolean *soon, boolean *off, real *left, boolean on_foot);

boolean bot_country_way(real_point3d const *from, real_point3d const *to, long portals, boolean drops, real ahead, real_point3d *next,
	boolean *soon, boolean *off, real *left)
{
	return country_way(from, to, portals, drops, ahead, next, soon, off, left, FALSE);
}

/* ... and on foot: over the ground the vehicles cross, without the ledges
   they go down off and the gates that carry them. For someone off the
   areas on such a map, or bound for a place the areas do not lead to: the
   last few steps to a flag from where the Warthog stopped, back to the
   Warthog, to the next one. The search is theirs alone and goes only as far
   as they are (a field of the whole map for every step a person takes is
   never done). */
boolean bot_country_walk(real_point3d const *from, real_point3d const *to, real_point3d *next, boolean *soon, boolean *off)
{
	return country_way(from, to, 0, FALSE, 7.f, next, soon, off, NULL, TRUE);
}

static boolean country_way(real_point3d const *from, real_point3d const *to, long portals, boolean drops, real ahead, real_point3d *next,
	boolean *soon, boolean *off, real *left, boolean on_foot)
{
	struct country_field *field;
	long start, goal, c, best;
	real along = 0.f;
	long steps = 0;
	long gate = NONE, gate_way = 0; /* the first floor of something narrow ahead, and which way it runs */

	*soon = FALSE;
	*off = FALSE;
	country_way_into_portal = FALSE;
	country_way_narrow = FALSE;
	country_way_lined_up = FALSE;
	country_way_turn = 0.f;
	country_way_dropping = FALSE;
	country_way_bend = 0.f;
	country_way_bend_at = 0.f;
	country_way_room = 3;
	country_way_step = FALSE;
	country_way_before = 0.f;
	if (left)
		*left = distance3d(from, to);
	if (!country.ready)
		return FALSE;
	start = country_floor_under(from, 3);
	if (start == NONE)
	{
		*off = TRUE;
		return FALSE;
	}
	goal = country_floor_near(to, 20);
	if (goal == NONE)
		return FALSE;
	/* (a place near by with open ground all the way to it: straight there.
	   Someone being driven round is not somewhere a way has to be worked
	   out to every time they move) */
	if (bot_distance2d(from, to) < 45.f && country_line_open(start, goal))
	{
		*next = *to;
		return TRUE;
	}
	if (on_foot)
		field = country_field_to(goal, portals, drops, 1, start, 2.5f * distance3d(from, to) + 80.f);
	else
		field = country_field_to(goal, portals, drops, MAX(COUNTRY_FIELD_NEAR, (long)(0.15f * distance3d(from, to) / country.cell)), NONE, 0.f);
	if (!field)
	{
		*soon = TRUE;
		return FALSE;
	}
	if (field->next[start] == _country_next_none || (field->next[start] & _country_next_tentative))
	{
		*soon = !field->complete;
		return FALSE;
	}
	c = start;
	best = start;
	/* (at the way into a portal: into it, its volume's middle) */
	if (field->next[start] >= _country_next_portal)
	{
		*next = country.portals[field->next[start] - _country_next_portal].entry;
		next->z = from->z;
		country_way_into_portal = TRUE;
		country_way_portal = field->next[start] - _country_next_portal;
		return TRUE;
	}
	while (field->next[c] != _country_next_goal && field->next[c] < _country_next_portal && steps < 16384)
	{
		boolean dropped = FALSE;
		long n = country_follow(field, c, &dropped);
		if (n == NONE)
			break; /* (never: the field was made over these very steps) */
		if (dropped)
		{
			/* (off an edge: as far as the edge, and over it) */
			if (best == start || c == best)
			{
				country_floor_point(n, next);
				next->z = country.z[c];
				country_way_dropping = TRUE;
				return TRUE;
			}
			break;
		}
		/* (onto something narrow, by a step along x or y: a bridge two
		   squares wide is not driven onto from the side) */
		if (gate == NONE && !on_foot && along <= ahead && (field->next[c] & 7) < 4 && country_width(n, field->next[c] & 7) <= 2)
		{
			gate = n;
			gate_way = field->next[c] & 7;
			if (country_width(start, gate_way) <= 2)
				gate = start; /* (on it already) */
		}
		/* (... or up a step: more of a rise than the two floors' slopes
		   come to, by a wheel's half height and over) */
		if (gate == NONE && !on_foot && along <= ahead && (field->next[c] & 7) < 4)
		{
			long way = field->next[c] & 7;
			real expected = 0.5f * country.cell * ((country.lean_x[c] + country.lean_x[n]) * country_dx[way] +
				(country.lean_y[c] + country.lean_y[n]) * country_dy[way]);
			if (country.z[n] - country.z[c] - expected > COUNTRY_RIDGE - 0.1f)
			{
				gate = n;
				gate_way = way;
				country_way_step = TRUE;
			}
		}
		along += country_length[field->next[c] & 7] * country.cell;
		c = n;
		steps++;
		if (along <= ahead)
		{
			if (country_line_open(start, c))
				best = c;
			else if (best != start)
				break; /* (round a corner: as far as the corner) */
			else
				best = c; /* (the first step of it, whatever) */
		}
		else if (!left)
			break;
	}
	if (left)
		*left = along;
	/* The lie of the next stretch of the way, corners and all: its bearing
	   six units at a time, and the most that differs from the first of
	   them (a bend, and how far off); and the least room beside the nearest
	   of it. What the driving takes its pace from. */
	{
		real_point3d mark, at;
		real first_bearing = 0.f;
		real gone = 0.f, since = 0.f;
		long chords = 0;
		long k = start;
		country_floor_point(start, &mark);
		for (steps = 0; steps < 64 && gone < ahead + 8.f && field->next[k] < _country_next_goal; steps++)
		{
			boolean dropped = FALSE;
			long n = country_follow(field, k, &dropped);
			real length;
			if (n == NONE || dropped)
				break;
			length = country_length[field->next[k] & 7] * country.cell;
			gone += length;
			since += length;
			k = n;
			if (gone <= 12.f)
				country_way_room = MIN(country_way_room, country_clear(k));
			if (since >= 6.f)
			{
				real bearing;
				country_floor_point(k, &at);
				bearing = bot_yaw_to(&mark, &at);
				if (chords++ == 0)
					first_bearing = bearing;
				else if (fabs(bot_angle_difference(bearing, first_bearing)) > country_way_bend)
				{
					if (country_way_bend < 0.3f)
						country_way_bend_at = gone - since;
					country_way_bend = (real)fabs(bot_angle_difference(bearing, first_bearing));
				}
				mark = at;
				since = 0.f;
			}
		}
	}
	/* Something narrow ahead, and not yet on it: onto it along its own line.
	   Off that line, the point to make for is a couple of squares before
	   its first floor, on the line; on the line (within a wheel's width of
	   it, and before it), the way is straight along it. */
	if (gate != NONE)
	{
		country_way_narrow = TRUE;
		country_way_lined_up = gate == start;
		if (gate != start)
		{
			real_point3d first;
			real ux = (real)country_dx[gate_way], uy = (real)country_dy[gate_way];
			real shift = country_narrow_middle(gate, gate_way);
			real before, aside;
			country_floor_point(gate, &first);
			/* (its real middle, to the left of the square's by so much) */
			first.x += -uy * shift;
			first.y += ux * shift;
			before = (from->x - first.x) * ux + (from->y - first.y) * uy;
			aside = (real)fabs((from->x - first.x) * uy - (from->y - first.y) * ux);
			{
				/* (how far off its line a vehicle can be and still have its
				   wheels on it: half its width, less half the vehicle's) */
				real room = MAX(0.35f, country_narrow_half - 0.8f);
				real far_end = country.cell;
				/* (a step's foot is a square before the floor it is up onto) */
				real foot = country_way_step ? -country.cell : 0.f;
				country_way_before = before - foot;
				country_way_axis = (real)atan2(uy, ux);
				if (before > foot - 1.f && aside > room)
				{
					/* (beside its foot, or past it, and off its line: back to
					   a place three squares before it, and at it again) */
					next->x = first.x + ux * (foot - 3.f * country.cell);
					next->y = first.y + uy * (foot - 3.f * country.cell);
					next->z = from->z;
					return TRUE;
				}
				/* (how far the narrow part runs) */
				c = gate;
				for (steps = 0; steps < 8 && field->next[c] < _country_next_goal && (field->next[c] & 7) == gate_way; steps++)
				{
					long n = country_step(c, gate_way);
					if (n == NONE)
						break;
					far_end += country.cell;
					if (country_width(n, gate_way) > 2)
						break;
					c = n;
				}
				/* (a point on its line, a few units on from where the vehicle
				   is abreast of: made for all the way in, that brings it onto
				   the line as it comes, and along it) */
				before = MIN(MAX(before + 5.f, foot - 3.f * country.cell), far_end);
				next->x = first.x + ux * before;
				next->y = first.y + uy * before;
				next->z = from->z;
				country_way_lined_up = aside <= room;
				return TRUE;
			}
		}
	}
	if (field->next[best] == _country_next_goal)
	{
		/* (the last of it: the place itself) */
		*next = *to;
		return TRUE;
	}
	if (field->next[best] >= _country_next_portal && best != start)
	{
		/* (as far as the way into a portal: on into it) */
		*next = country.portals[field->next[best] - _country_next_portal].entry;
		next->z = country.z[best];
		country_way_into_portal = TRUE;
		country_way_portal = field->next[best] - _country_next_portal;
		return TRUE;
	}
	country_floor_point(best, next);
	/* (which way the way goes on from there, a few squares of it: a corner
	   is come to no faster than it can be got round) */
	{
		real_point3d on;
		long k;
		c = best;
		for (k = 0; k < 5 && field->next[c] < _country_next_goal; k++)
		{
			boolean dropped = FALSE;
			long n = country_follow(field, c, &dropped);
			if (n == NONE || dropped)
				break;
			c = n;
		}
		if (c != best && best != start)
		{
			country_floor_point(c, &on);
			country_way_turn = (real)fabs(bot_angle_difference(bot_yaw_to(next, &on), bot_yaw_to(from, next)));
		}
	}
	return TRUE;
}

/* the ground of the country nearest a point, within so many squares: the
   way out from under a roof */
boolean bot_country_nearest(real_point3d const *point, long rings, real_point3d *ground)
{
	long f = country.ready ? country_floor_near(point, rings) : NONE;
	if (f == NONE)
		return FALSE;
	country_floor_point(f, ground);
	return TRUE;
}

/* testing (HALO_BOT_COUNTRYWAY=x,y,z:x,y,z): the way from one point to
   another by every portal the map has, worked out at once and written out */
void bot_country_debug_way(void)
{
	real_point3d from, to;
	struct country_field *field;
	long start, goal, c, steps = 0, set;
	real along = 0.f;
	if (!country.ready || !getenv("HALO_BOT_COUNTRYWAY") ||
		sscanf(getenv("HALO_BOT_COUNTRYWAY"), "%f,%f,%f:%f,%f,%f", &from.x, &from.y, &from.z, &to.x, &to.y, &to.z) != 6)
	{
		return;
	}
	start = country_floor_under(&from, 3);
	goal = country_floor_near(&to, 20);
	bot_log("countryway: from floor %ld (%.0f), to floor %ld (%.0f)", start, start != NONE ? country.z[start] : 0.f, goal, goal != NONE ? country.z[goal] : 0.f);
	if (start == NONE || goal == NONE || country.portal_set_count >= COUNTRY_PORTAL_SETS)
		return;
	set = country.portal_set_count++;
	memset(country.portal_sets[set], TRUE, sizeof(country.portal_sets[set]));
	field = country_field_to(goal, set, TRUE, COUNTRY_FIELD_NEAR, NONE, 0.f);
	while (field && !field->complete)
	{
		country.steps_used = 0;
		country_field_advance();
	}
	if (getenv("HALO_BOT_COUNTRYPORTALS"))
	{
		long k;
		for (k = 0; field && k < country.portal_count; k++)
		{
			struct country_portal const *portal = &country.portals[k];
			real_point3d in = { 0.f, 0.f, 0.f }, out = { 0.f, 0.f, 0.f };
			if (portal->entry_floor != NONE)
				country_floor_point(portal->entry_floor, &in);
			if (portal->exit_floor != NONE)
				country_floor_point(portal->exit_floor, &out);
			bot_log("countryway: portal %ld in by floor %ld (%.0f %.0f %.1f: way on %d) out by floor %ld (%.0f %.0f %.1f: way on %d)", k,
				portal->entry_floor, in.x, in.y, in.z, portal->entry_floor != NONE ? (int)field->next[portal->entry_floor] : -1,
				portal->exit_floor, out.x, out.y, out.z, portal->exit_floor != NONE ? (int)field->next[portal->exit_floor] : -1);
		}
	}
	if (!field || field->next[start] == _country_next_none)
	{
		long reached = 0;
		for (c = 0; field && c < country.floor_count; c++)
			reached += field->next[c] != _country_next_none;
		bot_log("countryway: no way (%ld floors have one)", reached);
		return;
	}
	c = start;
	while (field->next[c] != _country_next_goal && steps < 20000)
	{
		long d = field->next[c];
		long n;
		real_point3d at;
		country_floor_point(c, &at);
		if (d >= _country_next_portal)
		{
			struct country_portal const *portal = &country.portals[d - _country_next_portal];
			bot_log("countryway: %.0f along, at %.0f %.0f %.0f into portal %ld (out at %.0f %.0f %.0f)", along, at.x, at.y, at.z, d - _country_next_portal,
				portal->exit.x, portal->exit.y, portal->exit.z);
			n = portal->exit_floor;
		}
		else
		{
			boolean dropped = FALSE;
			n = country_follow(field, c, &dropped);
			if (n != NONE && !dropped && d < _country_next_goal &&
				fabs(country.z[n] - country.z[c]) > COUNTRY_KERB + country.cell * country_length[d & 7] * MAX(country.slope[c], country.slope[n]))
			{
				bot_log("countryway: %.0f along, at %.0f %.0f %.0f a kerb: to %.2f from %.2f, slopes %.2f and %.2f", along, at.x, at.y, at.z, country.z[n], country.z[c],
					country.slope[c], country.slope[n]);
			}
			if (n != NONE && dropped)
				bot_log("countryway: %.0f along, at %.0f %.0f %.0f off an edge, down to %.0f", along, at.x, at.y, at.z, country.z[n]);
			along += country_length[d < _country_next_goal ? (d & 7) : 0] * country.cell;
		}
		if (n == NONE)
		{
			bot_log("countryway: the way stops at %.0f %.0f %.0f", at.x, at.y, at.z);
			return;
		}
		if (steps % 60 == 0)
			bot_log("countryway: %.0f along, at %.0f %.0f %.0f", along, at.x, at.y, at.z);
		c = n;
		steps++;
	}
	bot_log("countryway: there, %.0f along in %ld steps", along, steps);
}

/* ---------- driving it */

static real country_ray(real_point3d const *from, real yaw, real pitch, real length, long ignore)
{
	struct collision_result collision;
	real_vector3d vector;
	unsigned long const flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) | FLAG(_collision_test_objects_machines_bit) |
		FLAG(_collision_test_objects_vehicles_bit);
	vector.i = (real)(cos(yaw) * cos(pitch)) * length;
	vector.j = (real)(sin(yaw) * cos(pitch)) * length;
	vector.k = (real)sin(pitch) * length;
	if (!collision_test_vector(flags, from, &vector, ignore, &collision))
		return length;
	return collision.t * length;
}

/* ... and what stands on the ground that is no part of it: a tree, a rock,
   another vehicle (the ground itself the country knows, and a ray at the
   ground ahead on a hillside is no news) */
static real country_thing_ray(real_point3d const *from, real_vector3d const *along, real length, long ignore)
{
	struct collision_result collision;
	real_vector3d vector;
	/* (not a machine: what a machine is to a vehicle is the country's to say,
	   and a bridge it is about to go up is no tree) */
	unsigned long const flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_objects_bit) |
		FLAG(_collision_test_objects_scenery_bit) | FLAG(_collision_test_objects_vehicles_bit);
	vector.i = along->i * length;
	vector.j = along->j * length;
	vector.k = along->k * length;
	if (!collision_test_vector(flags, from, &vector, ignore, &collision))
		return length;
	return collision.t * length;
}

/* What a driver sees ahead. A ray at the bonnet's height down the middle of
   the way saw a tree dead ahead, from a second off; one a wheel's width to
   the side of that it did not see at all, and the wing took it. So: three
   rays, down the middle and down each side of the vehicle, as far as it
   takes to stop in and half as much again, of what stands on the ground
   (a tree, a rock, a vehicle, parked or not) and of a face of the map that
   is no ground (a wall, a bank too steep to be a slope). How far the
   nearest of it is; the length, nothing. */
#define COUNTRY_SEE_FURTHEST 45.f
#define COUNTRY_BRAKING 3.f /* what a Warthog's brakes take off its speed, units a second each second (found by driving one) */
static char country_lane_saw[64]; /* (testing: what the last look down a lane met first) */
static real country_lane(real_point3d const *eye, real yaw, real slope, real half, real length, long ignore)
{
	unsigned long const flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit) | FLAG(_collision_test_objects_bit) |
		FLAG(_collision_test_objects_scenery_bit) | FLAG(_collision_test_objects_vehicles_bit);
	real nearest = length;
	long ray;
	for (ray = 0; ray < 3; ray++)
	{
		struct collision_result collision;
		real_point3d from = *eye;
		real_vector3d vector;
		real side = ray == 0 ? 0.f : ray == 1 ? half : -half;
		real reach = length;
		long pass;
		from.x -= (real)sin(yaw) * side;
		from.y += (real)cos(yaw) * side;
		vector.i = (real)(cos(yaw) * cos(slope));
		vector.j = (real)(sin(yaw) * cos(slope));
		vector.k = (real)sin(slope);
		/* (the ground rising to meet the ray is not in the way, and is
		   looked past once: what stands on the rise is) */
		for (pass = 0; pass < 2 && reach > 0.5f; pass++)
		{
			real_vector3d along;
			along.i = vector.i * reach, along.j = vector.j * reach, along.k = vector.k * reach;
			if (!collision_test_vector(flags, &from, &along, ignore, &collision))
				break;
			if (collision.type == _collision_result_object || collision.plane.n.k < COUNTRY_LEVEL)
			{
				if (length - reach + collision.t * reach < nearest && getenv("HALO_BOT_COUNTRYTRACE"))
				{
					if (collision.type == _collision_result_object && object_try_and_get(collision.object_index))
						snprintf(country_lane_saw, sizeof(country_lane_saw), "%.40s", strrchr(tag_get_name(object_get(collision.object_index)->definition_index), '\\') ?
							strrchr(tag_get_name(object_get(collision.object_index)->definition_index), '\\') + 1 : tag_get_name(object_get(collision.object_index)->definition_index));
					else
						snprintf(country_lane_saw, sizeof(country_lane_saw), "map:%.1f:%.1f", collision.plane.n.k, collision.point.z - (eye->z - 0.6f));
				}
				nearest = MIN(nearest, length - reach + collision.t * reach);
				break;
			}
			/* (on from where it met the ground, a little over it, along it) */
			reach -= collision.t * reach + 0.5f;
			from.x = collision.point.x + vector.i * 0.5f;
			from.y = collision.point.y + vector.j * 0.5f;
			from.z = collision.point.z + 0.7f;
			vector.k = MAX(vector.k, 0.f);
		}
	}
	return nearest;
}

/* One tick of driving across country to a place: which way to steer and
   how hard to go. */
/* is there ground that way, a little way off and about as high as here: not
   the top of a cliff, the lip of a platform (a way with nothing in it is
   the freest there is, and the last to take) */
/* (a wall of the map that way, nearer than so far: what is looked for under
   a place that is in the rock is not found, and that is no edge. A Warthog
   with a wall a length behind it took the wall for one, would not back
   toward it, and with another before its nose stood between the two until
   the game ended: 40% of all the driving on Ice Fields) */
static boolean country_wall_within(real_point3d const *from, real yaw, real reach, long ignore)
{
	struct collision_result collision;
	real_point3d eye = *from;
	real_vector3d way;
	if (!bot_refined_is(64))
		return FALSE;
	eye.z += 1.f;
	way.i = (real)cos(yaw) * reach, way.j = (real)sin(yaw) * reach, way.k = 0.f;
	return collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit), &eye, &way, ignore, &collision);
}

static boolean country_ground_as_far(real_point3d const *from, real yaw, real furthest, long ignore)
{
	real reach;
	for (reach = 2.5f; reach <= furthest + 0.1f; reach += 2.5f)
	{
		real_point3d at = *from;
		real ground;
		at.x += (real)cos(yaw) * reach;
		at.y += (real)sin(yaw) * reach;
		at.z += 1.f;
		if (!bot_ground(&at, 3.5f + (reach > 5.f ? reach - 5.f : 0.f), ignore, &ground))
			return country_wall_within(from, yaw, reach, ignore); /* (a wall: no edge this side of it) */
	}
	return TRUE;
}

static boolean country_ground_that_way(real_point3d const *from, real yaw, long ignore)
{
	return country_ground_as_far(from, yaw, 5.f, ignore);
}

/* Ground under a place a way ahead, looked for from over it (as high as a
   hill would have risen by there) down to as low as one would have fallen.
   Under a roof that place over it is in the rock, and a ray from in the
   rock meets nothing: every way on out of a tunnel was an edge, and the
   Warthogs of Ice Fields stood in its tunnels for minutes, stopped for
   edges that were floor. So, with nothing found from over it: from the
   vehicle's own height, down as far. */
static boolean country_ground_under(real_point3d const *over, real from_z, real reach, long ignore, real *ground)
{
	real_point3d at = *over;
	if (bot_ground(&at, 3.f + 1.7f * reach, ignore, ground))
		return TRUE;
	if (!bot_refined_is(64))
		return FALSE;
	at.z = from_z + 1.f;
	return bot_ground(&at, 3.f + 0.7f * reach, ignore, ground);
}

/* (... at one place that way) */
static boolean country_ground_at(real_point3d const *from, real yaw, real reach, long ignore)
{
	real_point3d at = *from;
	real ground;
	at.x += (real)cos(yaw) * reach;
	at.y += (real)sin(yaw) * reach;
	at.z += 1.f;
	return bot_ground(&at, 3.5f + (reach > 5.f ? reach - 5.f : 0.f), ignore, &ground) || country_wall_within(from, yaw, reach, ignore);
}

/* Carried up a lift: along its points at a steady pace, its nose the way it
   goes (brought round to that over the first third of a second), and set
   down at the last of them, standing. TRUE while it is being carried. */
static boolean country_carry(struct bot_traveller *traveller, struct bot_body const *body, struct bot_country_result *result)
{
	struct object_datum *vehicle = object_get(body->vehicle_index);
	long now = game_time_get();
	real gone, along = 0.f;
	real_point3d at;
	real_vector3d forward, left, up;
	long k;
	if (traveller->carry_count < 2 || now >= traveller->carry_until || now < traveller->carry_from)
		return FALSE;
	gone = (now - traveller->carry_from + 1) * COUNTRY_LIFT_PACE / BOT_TICKS_PER_SECOND;
	at = traveller->carry[traveller->carry_count - 1];
	forward.i = traveller->carry[traveller->carry_count - 1].x - traveller->carry[traveller->carry_count - 2].x;
	forward.j = traveller->carry[traveller->carry_count - 1].y - traveller->carry[traveller->carry_count - 2].y;
	forward.k = 0.f;
	for (k = 0; k + 1 < traveller->carry_count; k++)
	{
		real length = distance3d(&traveller->carry[k], &traveller->carry[k + 1]);
		if (gone <= along + length && length > 0.01f)
		{
			real part = (gone - along) / length;
			at.x = traveller->carry[k].x + (traveller->carry[k + 1].x - traveller->carry[k].x) * part;
			at.y = traveller->carry[k].y + (traveller->carry[k + 1].y - traveller->carry[k].y) * part;
			at.z = traveller->carry[k].z + (traveller->carry[k + 1].z - traveller->carry[k].z) * part;
			forward.i = traveller->carry[k + 1].x - traveller->carry[k].x;
			forward.j = traveller->carry[k + 1].y - traveller->carry[k].y;
			forward.k = traveller->carry[k + 1].z - traveller->carry[k].z;
			break;
		}
		along += length;
	}
	if (k + 1 >= traveller->carry_count)
		traveller->carry_until = now; /* (there) */
	if (normalize3d(&forward) <= 0.f)
		forward = vehicle->object.forward;
	{
		real blend = MIN((now - traveller->carry_from + 1) / (real)BOT_SECONDS(0.35f), 1.f);
		forward.i = traveller->carry_forward.i + (forward.i - traveller->carry_forward.i) * blend;
		forward.j = traveller->carry_forward.j + (forward.j - traveller->carry_forward.j) * blend;
		forward.k = traveller->carry_forward.k + (forward.k - traveller->carry_forward.k) * blend;
		if (normalize3d(&forward) <= 0.f)
			forward = vehicle->object.forward;
	}
	left.i = -forward.j;
	left.j = forward.i;
	left.k = 0.f;
	if (normalize3d(&left) <= 0.f)
	{
		left.i = 0.f, left.j = 1.f, left.k = 0.f;
	}
	up.i = forward.j * left.k - forward.k * left.j;
	up.j = forward.k * left.i - forward.i * left.k;
	up.k = forward.i * left.j - forward.j * left.i;
	object_set_position(body->vehicle_index, &at, &forward, &up);
	vehicle = object_get(body->vehicle_index);
	if (now >= traveller->carry_until)
		memset(&vehicle->object.translational_velocity, 0, sizeof(real_vector3d));
	else
	{
		vehicle->object.translational_velocity.i = forward.i * COUNTRY_LIFT_PACE / BOT_TICKS_PER_SECOND;
		vehicle->object.translational_velocity.j = forward.j * COUNTRY_LIFT_PACE / BOT_TICKS_PER_SECOND;
		vehicle->object.translational_velocity.k = forward.k * COUNTRY_LIFT_PACE / BOT_TICKS_PER_SECOND;
	}
	memset(&vehicle->object.angular_velocity, 0, sizeof(real_vector3d));
	traveller->stuck_point = at;
	traveller->stuck_tick = now;
	traveller->escape_until = 0;
	traveller->run_up_until = 0;
	traveller->swerve_ticks = 0;
	memset(result, 0, sizeof(*result));
	result->moving = TRUE;
	result->yaw = (real)atan2(forward.j, forward.i);
	result->throttle = now >= traveller->carry_until ? 0.f : 0.5f;
	result->next = traveller->carry[traveller->carry_count - 1];
	return TRUE;
}

void bot_country_drive(struct bot_traveller *traveller, struct bot_body const *body, real_point3d const *to, struct bot_country_result *result)
{
	struct object_datum *vehicle = object_get(body->vehicle_index);
	real_point3d const *position = &body->position;
	long now = game_time_get();
	real speed = (real)sqrt(body->velocity.i * body->velocity.i + body->velocity.j * body->velocity.j) * BOT_TICKS_PER_SECOND;
	real heading = (real)atan2(vehicle->object.forward.j, vehicle->object.forward.i);
	real to_place = bot_distance2d(position, to);
	real_point3d next = *to;
	boolean soon = FALSE, off = FALSE;
	real yaw;
	real throttle = 1.f;
	real turn;
	real seen_ahead = -1.f, seen_reach = 0.f; /* (testing: what the driver saw ahead, of how far it looked) */
	int seen_found = -1, seen_ours = 0;

	memset(result, 0, sizeof(*result));
	if (traveller->vehicle != body->vehicle_index)
	{
		memset(traveller, 0, sizeof(*traveller));
		traveller->vehicle = body->vehicle_index;
		traveller->stuck_point = *position;
		traveller->stuck_tick = now;
	}
	if (country_carry(traveller, body, result))
		return;
	/* testing (HALO_BOT_DRIVETEST=x0,y0,z0,x1,y1,seconds[,throttle]): the first
	   vehicle driven within sixty units of the first place is put down
	   there, standing, with its nose to the second, and driven straight at
	   it for so long; where it is then is written, and it is done again */
	if (getenv("HALO_BOT_DRIVETEST"))
	{
		static long test_vehicle = NONE;
		static long test_tick;
		static real_point3d highest;
		real_point3d from = { 0.f, 0.f, 0.f }, toward = { 0.f, 0.f, 0.f };
		real seconds = 4.f, full = 1.f;
		real_point3d then = { 0.f, 0.f, 0.f };
		real within = 1.2f;
		static boolean second;
		long given = sscanf(getenv("HALO_BOT_DRIVETEST"), "%f,%f,%f,%f,%f,%f,%f,%f,%f,%f", &from.x, &from.y, &from.z, &toward.x, &toward.y, &seconds, &full,
			&then.x, &then.y, &within);
		if (test_vehicle == NONE && bot_distance2d(position, &from) < 60.f)
		{
			test_vehicle = body->vehicle_index;
			test_tick = now - BOT_SECONDS(seconds) - 1;
		}
		if (test_vehicle == body->vehicle_index)
		{
			real way = (real)atan2(toward.y - from.y, toward.x - from.x);
			if (now - test_tick > BOT_SECONDS(seconds))
			{
				real_vector3d forward, up = { 0.f, 0.f, 1.f };
				if (test_tick > 0)
					bot_log("drivetest: t%ld ended at %.1f %.1f %.1f (%.1f on from the start, %.1f up), highest %.1f at %.1f %.1f", now, position->x, position->y, position->z,
						bot_distance2d(position, &from), position->z - from.z, highest.z, highest.x, highest.y);
				forward.i = (real)cos(way), forward.j = (real)sin(way), forward.k = 0.f;
				object_set_position(body->vehicle_index, &from, &forward, &up);
				memset(&vehicle->object.translational_velocity, 0, sizeof(real_vector3d));
				memset(&vehicle->object.angular_velocity, 0, sizeof(real_vector3d));
				test_tick = now;
				highest = from;
				second = FALSE;
			}
			/* (... and on to a second place, from so near the first) */
			if (given >= 9 && bot_distance2d(position, &toward) < within)
				second = TRUE;
			if (position->z > highest.z)
				highest = *position;
			if (now % 10 == 0)
				bot_log("drivetest: t%ld at %.1f %.1f %.2f speed %.1f heading %.2f", now, position->x, position->y, position->z, speed, heading);
			result->moving = TRUE;
			result->yaw = second ? bot_yaw_to(position, &then) : now - test_tick < BOT_SECONDS(0.7f) ? way : bot_yaw_to(position, &toward);
			result->throttle = now - test_tick < BOT_SECONDS(0.7f) ? 0.f : full;
			result->next = toward;
			traveller->stuck_point = *position;
			traveller->stuck_tick = now;
			return;
		}
	}
	if (!bot_country_way(position, to, bot_country_portals_of(body->vehicle_index), TRUE, MAX(14.f, speed * 1.6f), &next, &soon, &off, NULL))
	{
		/* (off the country, and floor a way under it: in the air, coming down
		   off a ledge. As it goes) */
		if (off)
		{
			long square = country_square_at(position->x, position->y);
			long f;
			for (f = square != NONE ? country.first[square] : 0; square != NONE && f < country.first[square + 1]; f++)
			{
				if (country.z[f] < position->z - 2.5f && country.z[f] + country.room[f] > position->z)
				{
					result->moving = TRUE;
					result->yaw = heading;
					result->throttle = 0.f;
					result->note = "in the air over the country";
					traveller->stuck_point = *position;
					traveller->stuck_tick = now;
					return;
				}
			}
		}
		result->soon = soon;
		result->off = off;
		result->none = !soon && !off;
		/* (no way at all, or not on the country: the caller's to deal with.
		   The way not worked out yet: where it is until it is, a few
		   seconds. Off toward the place meanwhile is off the edge of a
		   base on a mountain as often as not) */
		result->note = soon ? "the way is not worked out yet" : off ? "not on the country" : "no way";
		if (soon)
		{
			result->moving = TRUE;
			result->yaw = heading;
			result->throttle = speed > 2.f ? -0.6f : 0.f;
			result->next = *position;
			traveller->stuck_point = *position;
			traveller->stuck_tick = now;
		}
		return;
	}
	traveller->next = next;
	result->next = next;
	result->into_portal = country_way_into_portal && bot_distance2d(position, &next) < 12.f;
	yaw = bot_yaw_to(position, &next);
	turn = bot_angle_difference(yaw, heading);
	if (to_place < 10.f)
		throttle = MIN(throttle, 0.6f);
	/* (a corner of the way: come to no faster than it is got round, on snow) */
	if (!country_way_into_portal)
	{
		real bend = MAX(country_way_turn, country_way_bend);
		real pace = 1.0e9f;
		if (bend > 0.3f && !(country_way_narrow && country_way_lined_up))
		{
			real corner = bend > 1.f ? 4.f : bend > 0.6f ? 5.5f : 7.5f;
			real off = country_way_turn >= country_way_bend ? bot_distance2d(position, &next) : country_way_bend_at;
			pace = (real)sqrt(corner * corner + 2.f * 3.f * MAX(off - 2.f, 0.f));
		}
		/* (... and by an edge, by a wall, no faster than leaves time to
		   mend a slide) */
		if (country_way_room < 3 && !(country_way_narrow && country_way_lined_up))
			pace = MIN(pace, country_way_room == 1 ? 6.5f : 9.5f);
		if (speed > pace)
			throttle = speed > pace + 1.5f ? -0.6f : 0.f;
	}
	/* (onto a bridge, a ramp: slowly up to it, and not across it at an angle) */
	if (country_way_narrow && !country_way_lined_up)
	{
		/* (no faster than it can be put on a line a wheel's width across) */
		real pace = fabs(turn) > 0.5f ? 3.f : 5.f;
		throttle = speed > pace + 2.f ? -0.5f : speed > pace ? 0.f : fabs(turn) > 0.35f ? 0.4f : 0.7f;
	}
	/* Into a portal. It is a frame standing on the floor, as often as not,
	   no wider than the vehicle, with the volume inside it: the vehicle has
	   to go at it square, in the middle, and hard (the frame stops it, and it
	   is the run at it that carries it in for the tick the script wants).
	   So: to a place on the line through the frame's middle, a few lengths
	   out, and round to face it; then at it flat out; and, stopped at the
	   frame and still here, back out along the line and at it again. */
	/* Into a portal. What stands at one is a frame hardly wider than the
	   vehicle, open on one side, with the volume inside it: a vehicle's
	   middle has to be in the volume for the map's script to carry it, and
	   getting it there is a piece of parking (square on, in the middle, in
	   a base with no room to turn). The bots drive up to the frame and are
	   put in: within a length of the volume's middle, with nothing of the
	   map between, the vehicle is set down in it, and the script does the
	   rest as it would for anyone who had parked there. */
	if (result->into_portal)
	{
		struct country_portal const *portal = &country.portals[country_way_portal];
		real_point3d middle = portal->entry;
		real off = bot_distance2d(position, &portal->entry);
		/* (at the volume's own height, on the floor under it: not where the
		   vehicle happens to be, on the bank beside it, which may be over
		   the top of the volume) */
		if (portal->entry_floor != NONE)
			middle.z = MAX(middle.z, country.z[portal->entry_floor] + 0.35f);
		throttle = fabs(turn) > 0.6f ? 0.4f : 0.7f;
		traveller->swerve_ticks = 0;
		if (portal->lift != NONE)
		{
			/* (a lift: from within a length of its foot, with nothing of the
			   map between, and going slowly, up it) */
			struct country_lift const *lift = &country.lifts[portal->lift];
			real_point3d foot = lift->foot;
			foot.x += lift->side_x * 0.45f;
			foot.y += lift->side_y * 0.45f;
			foot.z += COUNTRY_RIDE_HEIGHT;
			if (speed > 4.f)
				throttle = 0.f;
			if (bot_distance2d(position, &foot) < COUNTRY_PORTAL_REACH && fabs(position->z - foot.z) < 1.5f && speed < 5.f)
			{
				struct collision_result wall;
				real_vector3d way;
				real_point3d eye = *position;
				eye.z += 0.5f;
				way.i = foot.x - eye.x;
				way.j = foot.y - eye.y;
				way.k = foot.z + 0.3f - eye.z;
				if (!collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit), &eye, &way,
						body->vehicle_index, &wall))
				{
					real length = 0.f;
					long k;
					traveller->carry_count = 4;
					traveller->carry[0] = *position;
					traveller->carry[1] = foot;
					traveller->carry[2] = lift->top;
					traveller->carry[2].z += COUNTRY_RIDE_HEIGHT;
					traveller->carry[3] = lift->exit;
					traveller->carry[3].z += COUNTRY_RIDE_HEIGHT;
					for (k = 0; k < 3; k++)
						length += distance3d(&traveller->carry[k], &traveller->carry[k + 1]);
					traveller->carry_from = now;
					traveller->carry_until = now + (long)(length / COUNTRY_LIFT_PACE * BOT_TICKS_PER_SECOND) + 2;
					traveller->carry_forward = vehicle->object.forward;
					if (bot_logging())
						bot_log("country: unit %ld t%ld up lift %ld from %.1f %.1f %.1f to %.1f %.1f %.1f", body->unit_index & 0xffff, now, portal->lift,
							position->x, position->y, position->z, lift->exit.x, lift->exit.y, lift->exit.z);
					if (country_carry(traveller, body, result))
						return;
				}
			}
		}
		else if (off < COUNTRY_PORTAL_REACH && fabs(position->z - portal->entry.z) < 2.5f)
		{
			struct collision_result wall;
			real_vector3d way;
			real_point3d eye = *position;
			eye.z += 0.5f;
			way.i = middle.x - position->x;
			way.j = middle.y - position->y;
			way.k = middle.z - position->z;
			if (!collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit), &eye, &way,
					body->vehicle_index, &wall))
			{
				boolean before = scenario_trigger_volume_test_object(portal->volume, body->vehicle_index);
				object_set_position(body->vehicle_index, &middle, NULL, NULL);
				if (bot_logging())
					bot_log("country: unit %ld t%ld into portal %ld at %.0f %.0f %.0f (volume's middle %.1f %.1f %.1f; in it before %d, now %d; its sphere's middle %.1f %.1f %.1f)",
						body->unit_index & 0xffff, now, country_way_portal, middle.x, middle.y, middle.z, portal->entry.x, portal->entry.y, portal->entry.z,
						before, scenario_trigger_volume_test_object(portal->volume, body->vehicle_index),
						vehicle->object.bounding_sphere_center.x, vehicle->object.bounding_sphere_center.y, vehicle->object.bounding_sphere_center.z);
			}
		}
	}
	result->arrived = to_place < 5.f;

	/* What stands in the way that the country does not show: a tree, a rock,
	   another vehicle. A ray ahead at the bonnet's height, the way the
	   vehicle lies on the ground; something there, and it is round the side
	   with more room: a way picked once and held (not so much to one hand
	   of wherever the nose is now, which is a spin), and slower. */
	if (bot_refined_is(64) && bot_vehicle_kind(vehicle->definition_index) != _bot_vehicle_turret)
	{
		/* As far as it takes to stop in and half as much again (not past where
		   the way turns: what stands beyond that is not in it), across its own
		   width, the way it is going. Something there: of the ways to
		   either hand (a little, more, a good deal), the least turn that is
		   clear past it and has ground under it, and is nearest the way it
		   was bound; slower by how soon it would be at it, hard on the
		   brake when that is half a second. One of our own on foot in the
		   way is gone round as a tree is, or stopped for. */
		real_point3d eye = *position;
		real slope = (real)asin(PIN(vehicle->object.forward.k, -1.f, 1.f));
		real half = PIN(vehicle->object.bounding_sphere_radius * 0.4f, 0.8f, 1.7f);
		real going = speed > 3.f ? (real)atan2(body->velocity.j, body->velocity.i) : heading;
		real reach = MIN(PIN(speed * speed / (2.f * COUNTRY_BRAKING) * 1.5f + speed * 0.5f + 5.f, 6.f, COUNTRY_SEE_FURTHEST), MAX(bot_distance2d(position, &next) + 1.5f, 4.f));
		boolean looks = !result->into_portal && !(country_way_narrow && country_way_lined_up);
		real ahead = reach;
		boolean ours = FALSE;
		eye.z += 0.6f;
		if (looks)
		{
			country_lane_saw[0] = 0;
			ahead = country_lane(&eye, going, slope, half, reach, body->vehicle_index);
			if (getenv("HALO_BOT_COUNTRYTRACE") && ahead < reach * 0.95f && now % 3 == 0)
				bot_log("countrysaw %ld t%ld %s at %.1f of %.1f, speed %.1f", body->unit_index & 0xffff, now, country_lane_saw, ahead, reach, speed);
			ours = bot_body_friendly_ahead(body, position, going, MIN(reach, 4.f + speed * 1.2f), half + 0.7f);
		}
		seen_ahead = ahead, seen_reach = reach, seen_ours = ours;
		if ((ahead < reach * 0.95f || ours) && traveller->swerve_ticks <= 0)
		{
			static real const turns[6] = { 0.3f, -0.3f, 0.6f, -0.6f, 0.95f, -0.95f };
			real past = MIN(reach, ahead + 7.f);
			real best_score = -1.0e9f;
			real best_yaw = going;
			boolean found = FALSE;
			long t;
			for (t = 0; t < 6; t++)
			{
				real way = bot_wrap(going + turns[t]);
				real clear, score;
				if (bot_body_friendly_ahead(body, position, way, MIN(past, 4.f + speed * 1.2f), half + 0.5f))
					continue;
				clear = country_lane(&eye, way, slope, half, past, body->vehicle_index);
				if (clear < past * 0.95f && clear < ahead + 3.f)
					continue;
				if (!country_ground_that_way(position, way, body->vehicle_index))
					continue;
				/* (clear, and the less of a turn from where the way leads the better) */
				score = clear - 6.f * (real)fabs(bot_angle_difference(way, yaw)) - 2.f * (real)fabs(turns[t]);
				if (score > best_score)
				{
					best_score = score;
					best_yaw = way;
					found = TRUE;
				}
			}
			seen_found = found;
			if (found)
			{
				traveller->swerve = best_yaw;
				traveller->swerve_ticks = BOT_SECONDS(0.35f);
			}
			else if (ours)
				throttle = MIN(throttle, speed > 1.f ? -0.6f : 0.f); /* (no way round them: stopped for) */
		}
		if (ahead < reach * 0.95f || ours)
		{
			/* (a Warthog on its brakes loses three units a second each second:
			   from twelve it is twenty-four units stopping, and from six,
			   six. Within what it stops in and a length: the brake; within
			   half as much again: off the throttle) */
			real off = (ours ? MIN(ahead, 4.f) : ahead) - half - 1.2f;
			real stops_in = speed * speed / (2.f * COUNTRY_BRAKING);
			throttle = MIN(throttle, off < stops_in + 1.f ? (speed > 2.5f ? -1.f : 0.3f) : off < stops_in * 1.5f + 2.f ? 0.f : 0.6f);
		}
		if (traveller->swerve_ticks > 0)
		{
			traveller->swerve_ticks--;
			yaw = traveller->swerve;
		}
		/* (nose to a wall, of the map or not, and stopped at it: straight back) */
		if (speed < 1.f && now >= traveller->escape_until && !result->into_portal && now - traveller->stuck_tick <= BOT_SECONDS(1.5f) &&
			country_ray(&eye, heading, slope, 2.5f, body->vehicle_index) < 2.2f)
		{
			traveller->escape_yaw = bot_wrap(heading + BOT_PI);
			traveller->escape_until = now + BOT_SECONDS(1.2f);
		}
	}
	else
	{
		real_point3d eye = *position;
		real_vector3d along = vehicle->object.forward;
		real reach = MIN(MAX(5.f, speed * 0.9f), MAX(bot_distance2d(position, &next) + 0.5f, 2.5f));
		real ahead;
		eye.z += 0.6f;
		ahead = result->into_portal || (country_way_narrow && country_way_lined_up) ? reach : country_thing_ray(&eye, &along, reach, body->vehicle_index);
		if (ahead < reach * 0.9f && fabs(turn) < 0.9f && traveller->swerve_ticks <= 0)
		{
			real_vector3d to_left = along, to_right = along;
			real left, right, side;
			to_left.i = along.i * (real)cos(0.5f) - along.j * (real)sin(0.5f);
			to_left.j = along.j * (real)cos(0.5f) + along.i * (real)sin(0.5f);
			to_right.i = along.i * (real)cos(0.5f) + along.j * (real)sin(0.5f);
			to_right.j = along.j * (real)cos(0.5f) - along.i * (real)sin(0.5f);
			left = country_thing_ray(&eye, &to_left, reach, body->vehicle_index);
			right = country_thing_ray(&eye, &to_right, reach, body->vehicle_index);
			side = left > right ? 1.f : -1.f;
			/* (not round it over an edge) */
			if (!country_ground_that_way(position, heading + side * 0.6f, body->vehicle_index))
				side = -side;
			if (country_ground_that_way(position, heading + side * 0.6f, body->vehicle_index))
			{
				traveller->swerve = bot_wrap(heading + side * 0.55f);
				traveller->swerve_ticks = BOT_SECONDS(0.7f);
			}
		}
		if (traveller->swerve_ticks > 0)
		{
			traveller->swerve_ticks--;
			yaw = traveller->swerve;
			throttle = MIN(throttle, speed > 7.f ? 0.f : 0.5f);
		}
		/* (nose to a wall, of the map or not, and stopped at it: straight back) */
		if (speed < 1.f && now >= traveller->escape_until && !result->into_portal && now - traveller->stuck_tick <= BOT_SECONDS(1.5f) &&
			country_ray(&eye, heading, (real)asin(PIN(along.k, -1.f, 1.f)), 2.5f, body->vehicle_index) < 2.2f)
		{
			traveller->escape_yaw = bot_wrap(heading + BOT_PI);
			traveller->escape_until = now + BOT_SECONDS(1.2f);
		}
	}

	/* How hard to go. A Warthog's wheels turn as far as its driver looks off
	   its nose, to thirty degrees: a look well to one side at speed is full
	   lock, and on snow that is the tail coming round. So: no more of a
	   turn asked than the speed will take, the speed taken off first for a
	   turn that needs more; easy down a steep hill; and, sliding (going one
	   way and pointing another), off the throttle with the wheels the way
	   it is going until it has hold again. */
	{
		/* (what hovers goes where it is pushed, not where it points: its
		   nose is put to the far side of the way it is drifting off the line,
		   and it is not slowed by pushing backward along a nose that points
		   somewhere it is not going) */
		boolean hovers = bot_vehicle_kind(vehicle->definition_index) == _bot_vehicle_scout;
		real going = speed > 3.f ? (real)atan2(body->velocity.j, body->velocity.i) : heading;
		real slide = (real)fabs(bot_angle_difference(going, heading));
		real wanted, most;
		real fall = (next.z - position->z) / MAX(bot_distance2d(position, &next), 2.f);
		if (hovers && traveller->swerve_ticks <= 0 && speed > 2.f)
		{
			real drift = (real)sin(bot_angle_difference(going, yaw)) * speed; /* (to the left of the line, a second) */
			yaw = bot_wrap(yaw - PIN(drift / 7.f, -0.7f, 0.7f));
			/* (... which wants a push to do anything) */
			if (fabs(drift) > 1.5f && throttle < 0.5f && fabs(bot_angle_difference(yaw, heading)) < 1.2f)
				throttle = 0.5f;
		}
		wanted = bot_angle_difference(yaw, heading);
		most = hovers ? 3.2f : speed > 11.f ? 0.22f : speed > 8.f ? 0.32f : speed > 5.f ? 0.5f : 3.2f;
		/* (full lock under power is the tail round, on snow: round a tight
		   turn on little of it) */
		if (fabs(wanted) > 1.2f)
			throttle = MIN(throttle, speed > 4.5f ? -0.4f : speed > 2.5f ? 0.15f : 0.5f);
		else if (fabs(wanted) > 0.6f)
			throttle = MIN(throttle, speed > 7.f ? -0.2f : speed > 4.5f ? 0.3f : 0.6f);
		else if (fabs(wanted) > 0.3f && speed > 10.f)
			throttle = MIN(throttle, 0.3f);
		if (fall < -0.25f && speed > 8.f)
			throttle = MIN(throttle, 0.f);
		if (fabs(wanted) > most)
			yaw = bot_wrap(heading + (wanted > 0.f ? most : -most));
		/* (going at an edge that is no part of the way: nothing under the
		   place it will be in a second or so. Stopped for, whatever else) */
		if (speed > 3.f && !country_way_dropping && !result->into_portal)
		{
			real reach;
			for (reach = MAX(3.f, speed * 0.45f); reach <= MAX(6.f, speed * 1.35f) + 0.1f; reach += MAX(3.f, speed * 0.45f))
			{
				real_point3d at = *position;
				real ground;
				/* (from over it, as high as a hill would have risen by there,
				   down to as low as one would have fallen: a slope is not an
				   edge, up or down) */
				at.x += (real)cos(going) * reach;
				at.y += (real)sin(going) * reach;
				at.z += 1.f + reach;
				if (!country_ground_under(&at, position->z, reach, body->vehicle_index, &ground))
				{
					throttle = slide < 1.f ? -1.f : slide > 2.2f ? 1.f : 0.f;
					result->edge = TRUE;
					/* (stopping for it is not being stuck) */
					traveller->stuck_point = *position;
					traveller->stuck_tick = now;
					break;
				}
			}
		}
		/* (... and stopped at one, its nose out over it and the way on to one
		   side: not round on the lip, which is over it sideways. Back from
		   it first, its tail to where it came from) */
		else if (speed <= 3.f && throttle > 0.f && !country_way_dropping && !result->into_portal && now >= traveller->escape_until)
		{
			real reach;
			for (reach = 2.5f; reach <= 4.6f; reach += 2.f)
			{
				real_point3d at = *position;
				real ground;
				at.x += (real)cos(heading) * reach;
				at.y += (real)sin(heading) * reach;
				at.z += 1.f + reach;
				if (!country_ground_under(&at, position->z, reach, body->vehicle_index, &ground))
				{
					if (fabs(wanted) > 0.5f)
					{
						traveller->escape_yaw = bot_wrap(heading + BOT_PI);
						traveller->escape_until = now + BOT_SECONDS(1.2f);
					}
					else
						throttle = 0.f; /* (the way on is over it, the country says, and it is not: left to be held up) */
					result->edge = TRUE;
					result->note = "stopped at an edge";
					break;
				}
			}
		}
		if (hovers)
		{
			if (throttle < 0.f && slide > 0.5f)
				throttle = 0.f;
		}
		else if (speed > 4.f && slide > 0.7f && throttle >= 0.f)
		{
			yaw = slide < 2.f ? going : heading;
			throttle = 0.f;
		}
	}

	/* Held up (in a row of parked Warthogs, against a rock, inside the frame
	   a portal put it down in): a look round, twelve ways, for the one with
	   the most room, the way it is bound sooner than another; and out that
	   way for a couple of seconds, nose first when it is before it and
	   backing when it is behind. Time and again of it, and it is the
	   caller's to get out. */
	/* (from a stand: it waited for its way, or for its people, or has just
	   got clear of something. A Warthog is two seconds getting a length
	   from where it stood, with its wheels turned: that is not being held
	   up, and taken for it it was sent off for two seconds the way with the
	   most room, which from the row the Warthogs of Coldsnap stand in is
	   over the edge of the base) */
	if (now - traveller->stuck_tick >= 0 && now - traveller->stuck_tick <= 1)
		traveller->stand_tick = now;
	if (distance3d(position, &traveller->stuck_point) > 2.5f)
	{
		/* (carried somewhere by a portal: afresh) */
		if (distance3d(position, &traveller->stuck_point) > 30.f)
			traveller->escape_until = 0;
		traveller->stuck_point = *position;
		traveller->stuck_tick = now;
		if (now - traveller->stuck_last > BOT_SECONDS(12.f))
			traveller->stuck_count = 0;
	}
	else if (speed > (now - traveller->stand_tick < BOT_SECONDS(4.f) && now >= traveller->stand_tick ? 0.5f : 1.5f) && now >= traveller->escape_until)
	{
		/* (moving, if it has not got far yet: pulling away from a stand) */
		traveller->stuck_tick = MAX(traveller->stuck_tick, now - BOT_SECONDS(1.f));
	}
	else if (now - traveller->stuck_tick > BOT_SECONDS(1.5f) && now >= traveller->escape_until)
	{
		real_point3d eye = *position;
		real best = -1.0e9f;
		long k;
		eye.z += 0.6f;
		for (k = 0; k < 12; k++)
		{
			real way = bot_wrap(heading + k * (BOT_PI / 6.f));
			real room = country_ray(&eye, way, 0.f, 9.f, body->vehicle_index);
			real score = room + 1.5f * (real)cos(bot_angle_difference(way, yaw));
			if (!country_ground_that_way(position, way, body->vehicle_index))
				score -= 100.f;
			/* (... nor a way with an edge as far off as two seconds take it,
			   while there is another) */
			else if (!country_ground_as_far(position, way, 12.5f, body->vehicle_index))
				score -= 30.f;
			if (score > best)
			{
				best = score;
				traveller->escape_yaw = way;
			}
		}
		traveller->escape_until = now + BOT_SECONDS(2.f);
		traveller->stuck_tick = now + BOT_SECONDS(2.f);
		traveller->stuck_last = now;
		traveller->stuck_count++;
	}
	/* (getting clear, and going at an edge: that is the end of getting
	   clear, and the stopping for it stands) */
	if (now < traveller->escape_until && result->edge && speed > 3.f)
		traveller->escape_until = now;
	if (now < traveller->escape_until)
	{
		real to_it = bot_angle_difference(traveller->escape_yaw, heading);
		if (fabs(to_it) < 1.4f)
		{
			yaw = traveller->escape_yaw;
			/* (a length or two is clear of it: not a run) */
			throttle = speed > 5.f ? 0.f : 0.8f;
		}
		else
		{
			/* (backing, a vehicle's nose swings away from where its driver
			   looks: its tail is brought to the way out) */
			real tail_off = bot_angle_difference(bot_wrap(traveller->escape_yaw + BOT_PI), heading);
			yaw = bot_wrap(heading - PIN(tail_off * 1.5f, -0.9f, 0.9f));
			throttle = -0.8f;
		}
		result->escaping = TRUE;
	}
	/* A step ahead, on its line, close to it and with no way on: it is not
	   climbed from a stand. Back along the line, straight, for a run at it. */
	if (country_way_step && !result->into_portal)
	{
		if (traveller->run_up_until > now)
		{
			if (country_way_before < -10.f)
				traveller->run_up_until = 0;
			else
			{
				yaw = country_way_lined_up ? heading : country_way_axis;
				throttle = -0.9f;
				traveller->escape_until = 0;
				traveller->stuck_point = *position;
				traveller->stuck_tick = now;
				result->escaping = TRUE;
			}
		}
		else if (country_way_lined_up && country_way_before > -4.5f && speed < 5.f && now >= traveller->escape_until)
			traveller->run_up_until = now + BOT_SECONDS(5.f);
	}
	else
		traveller->run_up_until = 0;
	/* Backing (to get clear, for a run at a step, off a lip): only onto
	   ground. Nothing behind it within what it would cover in a second, and
	   it is stopped, and whatever had it backing is over: the Warthogs of
	   Coldsnap backed for their run at the step to the gate, the length of
	   the platform, and off the far side of it. */
	if (throttle < 0.f && !result->into_portal)
	{
		real backward = -(body->velocity.i * vehicle->object.forward.i + body->velocity.j * vehicle->object.forward.j) * BOT_TICKS_PER_SECOND;
		if (backward > -0.5f)
		{
			real back = bot_wrap(heading + BOT_PI);
			if (!country_ground_at(position, back, 2.5f, body->vehicle_index) || !country_ground_at(position, back, 3.5f, body->vehicle_index))
			{
				if (bot_logging() && now % 30 == 0)
					bot_log("country: unit %ld t%ld backing and no ground behind: at %.1f %.1f %.1f heading %.2f, wanted yaw %.2f throttle %.2f, %s%s%s", body->unit_index & 0xffff, now,
						position->x, position->y, position->z, heading, yaw, throttle, now < traveller->escape_until ? "getting clear" : "",
						traveller->run_up_until > now ? " a run-up" : "", result->edge ? " (an edge ahead too)" : "");
				throttle = backward > 1.5f ? 1.f : 0.f;
				traveller->run_up_until = 0;
				if (now < traveller->escape_until)
					traveller->escape_until = now;
				/* (stopping for it is not being stuck, while it is still
				   going. Standing, with an edge before it as well, it is: on
				   a shelf a length and a half long, where it stood for good,
				   backing from the one edge and stopping for the other) */
				if (backward > 0.5f || !bot_refined_is(64))
				{
					traveller->stuck_point = *position;
					traveller->stuck_tick = now;
				}
				result->edge = TRUE;
				result->note = "backing: an edge behind";
			}
			/* (the edge a little further off: slowly, to stop at it. The
			   run at the step to Coldsnap's gate wants all the platform
			   there is behind it) */
			else if (!country_ground_at(position, back, 5.f, body->vehicle_index) || !country_ground_at(position, back, 7.5f, body->vehicle_index))
			{
				throttle = backward > 1.6f ? 0.4f : -0.4f;
				traveller->stuck_point = *position;
				traveller->stuck_tick = now;
			}
		}
	}
	result->held_up = traveller->stuck_count >= 4;
	result->yaw = yaw;
	result->throttle = throttle;
	result->moving = TRUE;
	if (!result->note && throttle <= 0.f && speed < 1.f)
		result->note = result->edge ? "an edge" : now < traveller->escape_until ? "getting clear" : country_way_narrow ? "onto something narrow" :
			traveller->swerve_ticks > 0 ? "round something" : "the throttle shut";
	if (bot_logging() && getenv("HALO_BOT_COUNTRYTRACE") && now % 3 == 0)
		bot_log("countrytrace %ld t%ld at %.1f %.1f %.1f heading %.2f speed %.1f by %.1f %.1f %.1f yaw %.2f throttle %.2f%s%s%s swerve %ld stuck %ld; sees %.1f of %.1f%s, a way round %d", body->unit_index & 0xffff, now,
			position->x, position->y, position->z, heading, speed, next.x, next.y, next.z, yaw, throttle, result->edge ? " edge" : country_way_narrow ? " narrow" : "",
			now < traveller->escape_until ? " escaping" : "", result->into_portal ? " portal" : "", traveller->swerve_ticks, traveller->stuck_count,
			seen_ahead, seen_reach, seen_ours ? " (one of ours)" : "", seen_found);
	if (bot_logging() && now % 90 == 0)
		bot_log("country: unit %ld t%ld at %.0f %.0f %.0f speed %.0f to %.0f %.0f %.0f (%.0f off) by %.0f %.0f %.0f yaw %.2f throttle %.2f%s%s",
			body->unit_index & 0xffff, now, position->x, position->y, position->z, speed, to->x, to->y, to->z, to_place,
			next.x, next.y, next.z, yaw, throttle, soon ? " (way to come)" : "", now < traveller->escape_until ? " getting clear" : "");
}
