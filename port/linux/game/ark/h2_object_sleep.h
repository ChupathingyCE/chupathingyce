#ifndef H2_OBJECT_SLEEP_H
#define H2_OBJECT_SLEEP_H

/* Halo 2's object sleep (port/linux/game/h2_object_sleep.c): an object the
tick's update pass leaves alone until something wakes it. See the .c. */

struct h2_object_sleep_snapshot
{
	real position;
	real power;
};

/* the rule is on: a Halo 2 level and HALO_H2_OBJECT_SLEEP != 0 (re-read at @set) */
boolean h2_object_sleep_active(void);
/* a new map: every slot forgotten */
void h2_object_sleep_reset(void);
/* the tick's pass asks of each active root: awake? (a slot not seen before
is a new object: awake, with Halo 2's creation countdown) */
boolean h2_object_sleep_awake(long object_index);
/* around a root's update: before clears "woken this tick"; after
decrements the countdown or, with no activity and not woken, sleeps it */
void h2_object_sleep_begin(long object_index);
void h2_object_sleep_end(long object_index, boolean active);
/* something moved, struck or changed the object: its root is awake and
woken this tick (0xbba20) */
void h2_object_sleep_wake(long object_index);
/* (perf/ce-floor-2) the same, its answer known first: a woken object only
matters to Halo 2's object sleep (h2_object_sleep_active: a Halo 2 level's) -
no call on any other level (objects.c's relink and placement paths) */
#include "h2_level_fast.h"
static __inline void h2_object_sleep_wake_fast(long object_index)
{
	if (h2_rules_level_fast())
		h2_object_sleep_wake(object_index);
}
/* the object's root was woken this tick: its node matrices, bounding sphere
and lights are to be rebuilt (Halo 2's post-physics pass runs only for
objects woken this tick, 0xb7060) */
boolean h2_object_sleep_woken(long object_index);
/* a root asleep this tick, for the census (and the garbage count) */
void h2_object_sleep_note_asleep(long object_index, short type);
/* around an object's type update: the device values before, and the
type's activity after (Halo 2's slot 0x40 return; wakes where Halo 2's
type update wakes) */
void h2_object_sleep_type_before(long object_index, short type, struct h2_object_sleep_snapshot *snapshot);
boolean h2_object_sleep_type_activity(long object_index, short type, struct h2_object_sleep_snapshot const *snapshot);
/* the object's own activity apart from its type: the damage update's
(shields recharging, stun), and what this engine computes every tick that
Halo 2 does not (object functions, scaled change colours, interpolation) */
boolean h2_object_sleep_object_activity(long object_index);
/* HALO_H2_SLEEP_LOG=1: the census every 300 ticks */
void h2_object_sleep_census(void);

#endif
