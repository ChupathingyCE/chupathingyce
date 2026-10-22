/*
UI_POINT.H

Where a point of the window is in the menus' 640x480, and back: the inverse
of the display's letterboxed blit at presentation (d3d8_gl.c), the window's
display scale (a HiDPI window has more pixels than points, and SDL gives the
pointer in points), the screen's width and the menus' centering in it (a
wide screen draws the menus' 640 columns in its middle). The pointer's
clicks and taps (halo_ui_pointer_update) and debug.test_input's scripted
clicks go through it.

No SDL or game types, so that the build machine can test it
(port/linux/tests/ui_point_test.c).
*/

#ifndef UI_POINT_H
#define UI_POINT_H

struct ui_point_view
{
	/* the window in points (the pointer's units) and in pixels */
	int window_width, window_height;
	int pixel_width, pixel_height;
	/* the back buffer's size in the game's units (screen_width x 480) and
	in GL pixels (larger when supersampled); its shape is the picture's */
	int target_width, target_height;
	int gl_width, gl_height;
	/* the game's screen width: 640, or more for a wide screen */
	long screen_width;
};

/* a point of the window in the menus' coordinates; 0 and -1, -1 when the
view has no size yet */
int ui_point_from_window(struct ui_point_view const *view, float window_x, float window_y, short *x, short *y);

/* the middle of a menu pixel in the window's points; 0 when the view has
no size yet */
int ui_point_to_window(struct ui_point_view const *view, short x, short y, float *window_x, float *window_y);

#endif
