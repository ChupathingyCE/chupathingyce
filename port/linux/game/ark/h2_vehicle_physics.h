/*
H2_VEHICLE_PHYSICS.H

port: Halo 2's vehicle force model (its Warthog's friction points, engine
and gears, its Ghost's anti-gravity points and controller) on a Halo 2
level, h2_vehicle_physics.c. The level's builder (h2_vehicles.c) reads each
vehicle's Halo 2 tags - its vehi "havok vehicle physics" struct (0x2AC) and
point blocks, its engine and gears, its physics model's mass - into a
struct h2vp_definition, kept in the level as the one string of a string
list tag named "h2\vehicles\<name> physics"; the game finds it by that name.
*/

#ifndef __H2_VEHICLE_PHYSICS_H
#define __H2_VEHICLE_PHYSICS_H

/* ---------- constants */

#define H2VP_MAGIC 0x50563248UL     /* 'H2VP' */
#define H2VP_VERSION 12UL
#define H2VP_MAXIMUM_CHILD_NODES 16

enum
{
	H2VP_MAXIMUM_POINTS = 16,
	H2VP_MAXIMUM_GEARS = 8,
	H2VP_MAXIMUM_HULL_SHAPES = 6,
	H2VP_MAXIMUM_HULL_VERTICES = 96,
};

/* a friction point's flags (Halo 2's, its +4) */
enum
{
	_h2vp_friction_powered_bit = 1,
	_h2vp_friction_front_turning_bit = 2,
	_h2vp_friction_rear_turning_bit = 3,
	_h2vp_friction_e_brake_bit = 4,
	_h2vp_friction_destroyable_bit = 5,
};

/* ---------- structures */

/* a friction point (Halo 2's 0x4C): where its marker is (the model's
space), its flags, fraction of the total mass (+8), radius (+0xC), moving
friction velocity difference (+0x18) and its e-brake values (+0x1C..0x24) */
struct h2vp_friction_point
{
	float position[3];
	unsigned long flags;
	float fraction;
	float radius;
	float moving_velocity_difference;
	float e_brake_moving_friction;
	float e_brake_friction;
	float e_brake_moving_velocity_difference;
};

/* an anti-gravity point (Halo 2's 0x4C): its marker's place, flags,
strength (+8), height (+0x10), damp (+0x14), normal k1 (+0x18) and k0
(+0x1C), radius (+0x20) and its default damage state's error (+0x38) */
struct h2vp_antigrav_point
{
	float position[3];
	unsigned long flags;
	float strength;
	float height;
	float damp;
	float normal_k1;
	float normal_k0;
	float radius;
	float error;
};

/* a gear (Halo 2's 0x44): its loaded and cruising torque curves (minimum,
maximum, peak scale, past-peak exponent, at 1x and 2x the maximum angular
velocity), up-shift time and scale, ratio, down-shift time and scale */
struct h2vp_gear
{
	float loaded[6];
	float cruising[6];
	float minimum_upshift_time;
	float upshift_scale;
	float ratio;
	float minimum_downshift_time;
	float downshift_scale;
};

/* a convex shape of its rigid hull (Halo 2's physics model polyhedron or
box): its vertices in hull_vertices, its contact friction and restitution
(the shape's +0xC, +0x10) */
struct h2vp_hull_shape
{
	long first_vertex;
	long vertex_count;
	float friction;
	float restitution;
};

struct h2vp_definition
{
	unsigned long magic;
	unsigned long version;
	unsigned long vehicle_flags;        /* 0x1EC */
	short type;                         /* 0x1F0 */
	short control;                      /* 0x1F2 */
	short specific_type;                /* 0x218 */
	short pad;
	float speed[4];                     /* 0x1F4: maximum forward, reverse, acceleration, deceleration (a second's) */
	float turn_angles[2];               /* 0x204, 0x208: maximum left and right (degrees) */
	float wheel_circumference;          /* 0x20C */
	float turn_rate;                    /* 0x210 (degrees a second) */
	float slide[4];                     /* 0x22C: maximum left, right slide, acceleration, deceleration */
	float overdampen_cusp;              /* 0x250 (degrees) */
	float overdampen_exponent;          /* 0x254 */
	float turn_scale;                   /* 0x220 (a tank's: its turn toward the stick's direction) */
	float engine_momentum;              /* 0x260 */
	float engine_maximum_angular_velocity; /* 0x264 */
	float flying_torque_scale;          /* 0x270 */
	float seat_entrance_scale;          /* 0x274 */
	float seat_exit_scale;              /* 0x278 */
	float boost_peak_power;             /* 0x1D0 */
	float boost_rise_power;             /* 0x1D4 (seconds to rise to the peak) */
	float boost_peak_time;              /* 0x1D8 (seconds it may be held) */
	float boost_fall_power;             /* 0x1DC (seconds to fall back) */
	float boost_dead_time;              /* 0x1E0 (seconds before the next) */
	/* (its boost's sound: its looping sound attachments scaled by "boost",
	each its track's folder - "vehicles\ghost\ghost_boost_left\track1\"
	-, whose in and loop the level has as sound\h2\<folder>in, ...loop) */
	char boost_sounds[2][96];
	/* (its death's push: its model's main section's destroyed response's
	transition effect's acceleration - the Warthog's trans_main_destroyed,
	2 at its "explosion" marker, cone angles 0 and 180: its place in the
	model's space and the marker's direction, the acceleration, the cone's
	inner and outer angles as the tag has them; has_death_push 0 none) */
	float death_push_position[3];
	float death_push_direction[3];
	float death_push_acceleration;
	float death_push_cone;
	float death_push_inner;
	/* (its child objects in Halo 2 - the Scorpion's cannon, the Warthog's
	chaingun or gauss turret, the Wraith's mortar -, made a part of it
	here: its damage info's flags (hlmt +0x60 +0) - bit 0 it takes shield
	damage for its children, bit 1 body damage - and the nodes only they
	have, by their name here; a hit on them is theirs, not its:
	h2_vehicle_physics_child_part) */
	unsigned long damage_info_flags;
	long child_node_count;
	char child_nodes[H2VP_MAXIMUM_CHILD_NODES][32];
	long has_death_push;
	/* the havok vehicle physics (0x2AC) */
	unsigned long physics_flags;        /* +0 */
	float ground_friction;              /* +4 */
	float ground_depth;                 /* +8 */
	float ground_damp;                  /* +0xC */
	float ground_moving_friction;       /* +0x10 */
	float slope[2];                     /* +0x14, +0x18 */
	float bank_lift;                    /* +0x2C */
	float bank_reaction;                /* +0x30 */
	float gravity_scale;                /* +0x34 */
	float radius;                       /* +0x38 */
	/* its physics model's mass, a rider's (the Spartan's bipd 0x274), its
	driver's seat's turnover time (0x20) */
	float mass;
	float rider_mass;
	float turnover_time;
	/* its heaviest rigid body's linear and angular damping (phmo 0x38,
	0xA0 each: 0x28, 0x2C; its mass at 0x40) */
	float linear_damping;
	float angular_damping;
	/* its rigid bodies' centre of mass in the model's space (their mass's
	mean: each body's centre, 0x50, in its node's space - 0 its node, -1 the
	model's) and whether it was read */
	float center_of_mass[3];
	long has_center_of_mass;
	/* its rigid hull, for a solver's rigid body (h2_jolt_hull.c): the
	physics model's shapes of its living rigid bodies - the regions' "base"
	permutations, not "destroyed" -, in the model's space, world units; their
	mass (the physics model's +4 counts the destroyed permutation's too: the
	Scorpion's 52011 is 24774 + 27238), centre of mass, and inertia about it
	(mass x world units squared, the model's axes: rigid body 0x60..0x80);
	hull_shape_count 0: not read */
	float hull_mass;
	float hull_linear_damping;
	float hull_angular_damping;
	float hull_center[3];
	float hull_inertia[9];
	long hull_shape_count;
	struct h2vp_hull_shape hull_shapes[H2VP_MAXIMUM_HULL_SHAPES];
	float hull_vertices[H2VP_MAXIMUM_HULL_VERTICES][3];
	/* (a vehicle whose body is its own - h2_vehicles.c's own_body: its
	physics' centre of mass Halo 2's, level with the stand-in's - weighs at
	its body's centre, not at center_of_mass) */
	long weight_at_body;
	/* (how far its aim turns from its forward, radians: a standing turret's
	its Halo 2 aim screen's, h2v_turret_aim_limits - the anti-personnel
	turret's yaw -90..90, pitch -50..50; else its first seat's yaw limits,
	0x88/0x8C, pitch free. has_yaw_limits 0 none) */
	float yaw_limits[2];
	float pitch_limits[2];
	long has_yaw_limits;
	/* (its gun's second trigger on the melee button - the Banshee's fuel
	rod bomb, Halo 2's trigger input "melee attack") */
	long secondary_on_melee;
	/* (its hatch or canopy: Halo 2's "combat:opening" and "combat:closing"
	lengths, seconds - the graph's hatch overlay played open while a rider
	gets in or out, h2_vehicle_hatch_value; has_hatch 0 none) */
	float hatch_seconds[2];
	long has_hatch;
	/* (its collision damage, obje 0x6C..0x8C: "apply collision damage
	scale" - what it passes to what it hits -, then the limits of what it
	takes: from another body's blow (unknown_1da550.cpp function_1da6d0)
	minimum and maximum game acceleration and damage scale, and from its own
	speed against anything (function_1da9e0) minimum and maximum absolute
	acceleration and damage scale; world units a second: Havok's contact
	impulse per mass. h2_collision_damage.c; has_collision 0 not read) */
	float collision[9];
	long has_collision;
	long gear_count;
	long friction_count;
	long antigrav_count;
	struct h2vp_gear gears[H2VP_MAXIMUM_GEARS];
	struct h2vp_friction_point friction[H2VP_MAXIMUM_POINTS];
	struct h2vp_antigrav_point antigrav[H2VP_MAXIMUM_POINTS];
	unsigned long end;                  /* (0: the string list's terminator) */
};

/* ---------- prototypes */

/* a vehicle definition's values (its "<name> physics" string list), NULL if
it has none (not a Halo 2 level, or HALO_H2_STOCK_VEHICLE_PHYSICS) */
struct h2vp_definition const *h2_vehicle_physics_definition(long definition_index);

#endif
