/*
LOADING_SCREEN.H

The Xbox's loading screen (progress_bar.c), made by the CPU and put over the
back buffer (loading_screen.c).
*/

#ifndef __LOADING_SCREEN_H
#define __LOADING_SCREEN_H

/* a new loading screen starts from black (the Xbox cleared its front buffer) */
void loading_screen_reset(
	void);

/* whether a new step is due (60 a second); if not, the last picture is drawn
again */
boolean loading_screen_step_due(
	void);

/* a step: the last picture dimmed by `fade_alpha` and grown a little, and
the loading picture (A8B8G8R8 texels) added, lit by `brightness` and the glow
band's texture (its alpha) where `progress` puts it */
void loading_screen_draw(
	float progress,
	float brightness,
	float fade_alpha,
	unsigned int const *picture,
	long picture_width,
	long picture_height,
	unsigned int const *mask,
	long mask_width,
	long mask_height);

/* whether the menus' accept (A on any controller, space or enter on the
keyboard) was pressed this frame, while the loading screen is held for
display.loading_screen's seconds (game.c): the rest of the wait is skipped */
boolean loading_screen_skip_pressed(
	void);

#if defined(HALO_GAME_BROWSER) && !defined(HALO_CONSOLE_NO_LOADING_PICTURE)
/* the join screen's picture (display.join_screen, browser_screen.c): the
loading screen's, its sweep looped while a game is joined or its map
downloads, 640 by 480, RGBA; restart starts it from black. A new step
at most 60 times a second, else the last again */
unsigned char const *loading_screen_join_frame(
	boolean restart);
#endif

#endif
