/*
POWERUP_TIMERS.H

display.powerup_timers: the seconds left on a local player's timed powerups,
in a small line of the HUD (powerup_timers.c).
*/

#ifndef __POWERUP_TIMERS_H
#define __POWERUP_TIMERS_H

/* the line for a living local player with a timed powerup on; nothing with
the setting off */
void powerup_timers_draw(
	short local_player_index);

#endif
