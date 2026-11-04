/*
LINE_OF_SIGHT.H

Whether a client's reported hit had a clear line to its target, as the host
decides it (network_damage.c, network.hit_line_of_sight): the geometry of the
decision, with no game in it. The host hands in the points its history has
the shooter at (the eye, the head, the gun of the vehicle seat they hold), the
target where the report has it (its bounding sphere), and a test of a segment
against the map, the engine's collision test, which the unit tests (port/
linux/tests/line_of_sight_test.c) stand in for with walls of their own.

A shot is taken when any of the shooter's points has a clear line to any of
the target's points: its center, the impact, and the edges of its bounding
sphere and a little more. A point of the shooter's is also tried a little to
either side and above (the host's copy of a player is where the client's
says, within a tolerance). A projectile whose path bends (it homes in, or
falls) is also taken along a path of two segments through a point as far off
the straight line as it can bend over that distance. Surfaces the segment
test calls passable (glass, a surface the projectile goes through) are gone
through.

An explosion is taken when its epicenter has a clear line to the target's
center or the edges of its sphere, or a point its core reaches (as damage.c
spreads an explosion with a core: area_of_effect_cause_damage_to_object)
has one to its center. The shooter's own position plays no part: a grenade
that went around a corner hits what its explosion sees.

Every test is counted against a budget: one the budget runs out on is
taken (a report the host cannot decide stays dealt).
*/

#ifndef __LINE_OF_SIGHT_H
#define __LINE_OF_SIGHT_H

/* ---------- constants */

/* how far (world units) beyond the target's bounding sphere a line to it
may pass, and how far to the side or above its point a shooter is tried */
#define LINE_OF_SIGHT_TARGET_TOLERANCE 0.1f
#define LINE_OF_SIGHT_SHOOTER_TOLERANCE 0.15f
/* the target's points: center, impact, four edges, top */
#define LINE_OF_SIGHT_TARGET_POINTS 7
/* passable surfaces gone through on one segment, at most */
#define LINE_OF_SIGHT_MAXIMUM_PASSES 4
/* the segment tests a decision makes at most */
#define LINE_OF_SIGHT_DEFAULT_BUDGET 256

/* ---------- structures */

struct line_of_sight_point
{
	float x, y, z;
};

/* what a segment test found: whether it hit, how far along (0 to 1), and
whether what it hit lets the projectile through */
struct line_of_sight_hit
{
	int hit;
	float t;
	int passable;
};

/* the segment from one point to the other against the map */
typedef void (*line_of_sight_test)(void *context, struct line_of_sight_point const *from,
	struct line_of_sight_point const *to, struct line_of_sight_hit *hit);

struct line_of_sight_target
{
	struct line_of_sight_point center;
	float radius;
	/* where the shot hit it, when has_impact */
	struct line_of_sight_point impact;
	int has_impact;
};

/* how a projectile's path bends: the radius it turns in at its fastest when
it homes in (0 for none), and its fall (world units a tick squared, 0 for
none) at its slowest speed (world units a tick) */
struct line_of_sight_bend
{
	float turn_radius;
	float gravity;
	float slowest_speed;
};

struct line_of_sight_query
{
	line_of_sight_test test;
	void *context;
	/* segment tests left (counted down) */
	int budget;
};

/* ---------- prototypes/LINE_OF_SIGHT.C */

/* whether the segment is clear: passable surfaces gone through, at most
LINE_OF_SIGHT_MAXIMUM_PASSES. Returns -1 when the budget has run out. */
int line_of_sight_segment_clear(struct line_of_sight_query *query, struct line_of_sight_point const *from,
	struct line_of_sight_point const *to);

/* the target's points as seen from the point: center, impact (when it has
one), and the edges of its sphere (with the tolerance) across the line to
it and above; returns how many */
int line_of_sight_target_points(struct line_of_sight_target const *target, struct line_of_sight_point const *from,
	struct line_of_sight_point points[LINE_OF_SIGHT_TARGET_POINTS]);

/* how far off the straight line a path that bends can be at its middle over
the distance: across (homing) and up (falling) */
void line_of_sight_bend_offsets(struct line_of_sight_bend const *bend, float distance, float *across, float *up);

/* whether a shot from any of the shooter's points had a clear line to the
target (bend NULL for a straight path). 1 for clear, 0 for blocked, -1 when
the budget ran out first (taken). */
int line_of_sight_shot(struct line_of_sight_query *query, struct line_of_sight_point const *starts, int start_count,
	struct line_of_sight_target const *target, struct line_of_sight_bend const *bend);

/* whether an explosion at the epicenter, with the core radius, had a clear
line to the target, as for line_of_sight_shot */
int line_of_sight_explosion(struct line_of_sight_query *query, struct line_of_sight_point const *epicenter,
	float core_radius, struct line_of_sight_target const *target);

#endif /* __LINE_OF_SIGHT_H */
