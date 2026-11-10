/*
MEDAL_HUD.H

The medals on the HUD as icons in the kill feed (display.medals;
medal_hud.c), in the builds that have them (HALO_MEDAL_ICONS). Called from
the game's HUD messages (source/interface/hud_messaging.c) and its
statistics recorder (port/linux/game/game_stats.c).
*/

#ifndef MEDAL_HUD_H
#define MEDAL_HUD_H

#ifdef HALO_MEDAL_ICONS

/* a medal a local player earned, by its key (medal_icons.h's, the site's):
it waits for the next line printed for them this tick (the kill's) */
void medal_hud_earned(short local_player_index, char const *key);
/* a line printed for a local player (hud_print_message: its time and
number): the medals waiting go with it */
void medal_hud_printed(short local_player_index, long time, short number);
/* while hold, the lines printed take no medals (a player quitting's:
game_engine.c) */
void medal_hud_hold(boolean hold);
/* before a player's HUD messages are drawn: medals no line took get a
line of their own */
void medal_hud_flush(short local_player_index);
/* how many medals a line (its time and number) has */
short medal_hud_line_count(short local_player_index, long time, short number);
/* a line's medals drawn from x (the HUD's coordinates, in its view), top
their top, size square and gap apart, alpha the line's */
void medal_hud_draw_line(short local_player_index, long time, short number, real x, real top, real size, real gap,
	real alpha);
/* every line's medals forgotten (a new game) */
void medal_hud_clear(void);

#endif

#endif
