/*
UI_POINT.C

The menus' points and the window's (ui_point.h).
*/

#include "ui_point.h"

#include <math.h>

/* where the picture is drawn in the window's pixels: letterboxed to the
back buffer's shape */
static int picture_rectangle(struct ui_point_view const *view, int *left, int *top, int *width, int *height)
{
	if (view->window_width <= 0 || view->window_height <= 0 || view->pixel_width <= 0 || view->pixel_height <= 0 ||
		view->target_width <= 0 || view->target_height <= 0 || view->gl_width <= 0 || view->gl_height <= 0)
	{
		return 0;
	}
	*width = view->pixel_width;
	*height = (int)((long)view->pixel_width * view->gl_height / view->gl_width);
	if (*height > view->pixel_height)
	{
		*height = view->pixel_height;
		*width = (int)((long)view->pixel_height * view->gl_width / view->gl_height);
	}
	*left = (view->pixel_width - *width) / 2;
	*top = (view->pixel_height - *height) / 2;
	return 1;
}

int ui_point_from_window(struct ui_point_view const *view, float window_x, float window_y, short *x, short *y)
{
	int left, top, width, height;
	float screen_x, screen_y;

	*x = *y = -1;
	if (!picture_rectangle(view, &left, &top, &width, &height))
		return 0;
	screen_x = (window_x * view->pixel_width / view->window_width - left) * (float)view->target_width / (float)width;
	screen_y = (window_y * view->pixel_height / view->window_height - top) * (float)view->target_height / (float)height;
	*x = (short)floorf(screen_x - (float)(view->screen_width - 640) / 2.0f);
	*y = (short)floorf(screen_y);
	return 1;
}

int ui_point_to_window(struct ui_point_view const *view, short x, short y, float *window_x, float *window_y)
{
	int left, top, width, height;
	float screen_x, screen_y;

	if (!picture_rectangle(view, &left, &top, &width, &height))
		return 0;
	screen_x = (float)x + 0.5f + (float)(view->screen_width - 640) / 2.0f;
	screen_y = (float)y + 0.5f;
	*window_x = (screen_x * (float)width / (float)view->target_width + left) * view->window_width / view->pixel_width;
	*window_y = (screen_y * (float)height / (float)view->target_height + top) * view->window_height / view->pixel_height;
	return 1;
}
