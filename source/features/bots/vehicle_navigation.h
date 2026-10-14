/*
VEHICLE_NAVIGATION.H

port: routes for bots driving ground vehicles (vehicle_navigation.c), over the
vehicle layer of the walkable surfaces (navigation_world.h): a vehicle's slope
limit, and each segment swept with a pill as wide and tall as the widest
drivable vehicle on the map. A fixed pool of drivers shares one search queue.
*/

#ifndef HALO_VEHICLE_NAVIGATION_H
#define HALO_VEHICLE_NAVIGATION_H
#pragma once

#include "cseries/cseries.h"
#include "engine_ai/surface_navigation.h"
#include "bot_navigation.h"

/* ---------- constants */

enum
{
	/* bots driving at once; a driver beyond them steers straight */
	VEHICLE_NAVIGATION_DRIVERS_MAX = 8,
};

/* ---------- structures */

/* what the driver does this tick: face yaw (the vehicle steers toward where
its driver looks), with throttle forward (positive) or back (negative) */
struct vehicle_navigation_command
{
	boolean has_drive;
	real yaw;
	real throttle;
};

/* The steering seam (vehicle_navigation_steer): the route ahead of the
vehicle, its pose and size. No game state. */
struct vehicle_steer_input
{
	struct sn_point origin;
	real forward_yaw; /* where the vehicle's nose points */
	real half_length;
	struct sn_point const *points; /* the route, start to goal */
	uint32_t count;
	uint32_t waypoint; /* the first not yet reached */
	uint32_t target; /* output: the waypoint steered at */
};

/* ---------- prototypes/VEHICLE_NAVIGATION.C */

/* a new map, BSP or game state: every route and search dropped */
void vehicle_navigation_reset_all(void);

/* once per tick, before bots decide: a driver that did not ask last tick lets
go of its slot; the shared searches advance (at most four begin, 64
polygons expanded, one route string-pulled) */
void vehicle_navigation_update(void);

/* Drive vehicle_index toward goal (bot_number 1..BOT_NAVIGATION_AGENTS_MAX
is its driver). ARRIVED within arrival_distance horizontally; WAITING while
searching, or for a vehicle in the way; FAILED when no route is found, or
the vehicle stays stuck after backing up: the caller chooses another goal.
UNAVAILABLE with no vehicle layer or no free driver slot: the caller may
steer straight. check_goal_segment FALSE for a goal that is a unit's
position (its body stands there). */
enum bot_navigation_result vehicle_navigation_drive(
	short bot_number,
	long vehicle_index,
	struct sn_point goal,
	real arrival_distance,
	boolean check_goal_segment,
	struct vehicle_navigation_command *command);

/* the bot is no longer driving: its slot and search go */
void vehicle_navigation_release(short bot_number);

/* The connected set of the vehicle layer at a point (SN_NONE if it is not on
drivable ground), for choosing goals a vehicle can reach. */
uint32_t vehicle_navigation_component(struct sn_point point);

/* Steering along a route: advances input->waypoint past the waypoints
reached (within 0.8 times the half-length, or driven past), and returns the
waypoint index steered at: the first at least two vehicle lengths ahead
(a vehicle cannot turn on the spot). Throttle eases into sharp turns and
toward the route's end. FALSE once the route is done. */
boolean vehicle_navigation_steer(
	struct vehicle_steer_input *input,
	struct vehicle_navigation_command *command);

#endif /* HALO_VEHICLE_NAVIGATION_H */
