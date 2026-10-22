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

#endif
