/*
UI_POINT_TEST.C

Tests of the menus' points and the window's (port/linux/src/ui_point.c): a
click lands on the menu pixel under it at any window shape, display scale
and screen width. Built and run by tools/test_ui_point.py; exits nonzero on
a failure.
*/

#include "../src/ui_point.h"

#include <stdio.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

/* every menu pixel of a row and a column (the screen's whole width) comes
back to itself through the window */
static void check_round_trip(struct ui_point_view const *view)
{
	short margin = (short)((view->screen_width - 640) / 2);
	short x, y;

	for (x = (short)-margin; x < 640 + margin; x++)
	{
		float window_x, window_y;
		short back_x, back_y;

		CHECK(ui_point_to_window(view, x, 240, &window_x, &window_y));
		CHECK(ui_point_from_window(view, window_x, window_y, &back_x, &back_y));
		if (back_x != x || back_y != 240)
		{
			printf("  x %d came back as %d,%d\n", x, back_x, back_y);
			failures++;
			return;
		}
	}
	for (y = 0; y < 480; y++)
	{
		float window_x, window_y;
		short back_x, back_y;

		CHECK(ui_point_to_window(view, 320, y, &window_x, &window_y));
		CHECK(ui_point_from_window(view, window_x, window_y, &back_x, &back_y));
		if (back_x != 320 || back_y != y)
		{
			printf("  y %d came back as %d,%d\n", y, back_x, back_y);
			failures++;
			return;
		}
	}
}

int main(void)
{
	short x, y;
	float window_x, window_y;

	/* the Xbox's 640x480 in a window of its size */
	{
		struct ui_point_view view = { 640, 480, 640, 480, 640, 480, 640, 480, 640 };

		CHECK(ui_point_from_window(&view, 0.0f, 0.0f, &x, &y) && x == 0 && y == 0);
		CHECK(ui_point_from_window(&view, 639.9f, 479.9f, &x, &y) && x == 639 && y == 479);
		check_round_trip(&view);
	}
	/* a HiDPI window: 1280x960 points, twice the pixels, the picture
	supersampled */
	{
		struct ui_point_view view = { 1280, 960, 2560, 1920, 640, 480, 1280, 960, 640 };

		CHECK(ui_point_from_window(&view, 640.0f, 480.0f, &x, &y) && x == 320 && y == 240);
		CHECK(ui_point_from_window(&view, 1279.0f, 959.0f, &x, &y) && x == 639 && y == 479);
		check_round_trip(&view);
	}
	/* a wide screen: 852x480 drawn in a 1920x1080 window on a 2x display;
	the menus' 640 columns are its middle, x 0 at its 106th (the picture
	letterboxed by a few pixels: 852x480 is not quite 16:9) */
	{
		struct ui_point_view view = { 1920, 1080, 3840, 2160, 852, 480, 1704, 960, 852 };

		CHECK(ui_point_from_window(&view, 960.0f, 540.0f, &x, &y) && x == 320 && y == 240);
		CHECK(ui_point_from_window(&view, 2.0f, 540.0f, &x, &y) && x == -106);
		check_round_trip(&view);
	}
	/* the Xbox's 4:3 in a 16:9 window: bars at the sides, outside the menus */
	{
		struct ui_point_view view = { 1920, 1080, 1920, 1080, 640, 480, 640, 480, 640 };

		CHECK(ui_point_from_window(&view, 240.0f, 0.0f, &x, &y) && x == 0 && y == 0);
		CHECK(ui_point_from_window(&view, 100.0f, 540.0f, &x, &y) && x < 0);
		CHECK(ui_point_to_window(&view, 0, 0, &window_x, &window_y) && window_x > 240.0f && window_x < 242.0f);
		check_round_trip(&view);
	}
	/* a tall window (a phone held upright): bars above and below */
	{
		struct ui_point_view view = { 1080, 2340, 1080, 2340, 640, 480, 640, 480, 640 };

		CHECK(ui_point_from_window(&view, 540.0f, 1170.0f, &x, &y) && x == 320 && y == 240);
		check_round_trip(&view);
	}
	/* no window yet */
	{
		struct ui_point_view view = { 0, 0, 0, 0, 640, 480, 640, 480, 640 };

		CHECK(!ui_point_from_window(&view, 10.0f, 10.0f, &x, &y) && x == -1 && y == -1);
		CHECK(!ui_point_to_window(&view, 10, 10, &window_x, &window_y));
	}
	/* the host's carnage report prompt: B =quit from 380, A =pick game
	from 470 to 600, on the line at 410 */
	{
		struct ui_point_prompt prompt = { 410, 428, 380, 600, 470 };

		CHECK(ui_point_prompt_part(&prompt, 0, 400, 418) == 0);
		CHECK(ui_point_prompt_part(&prompt, 0, 469, 418) == 0);
		CHECK(ui_point_prompt_part(&prompt, 0, 470, 418) == 1);
		CHECK(ui_point_prompt_part(&prompt, 0, 599, 418) == 1);
		CHECK(ui_point_prompt_part(&prompt, 0, 600, 418) == -1);
		CHECK(ui_point_prompt_part(&prompt, 0, 500, 400) == -1);
		CHECK(ui_point_prompt_part(&prompt, 0, 300, 418) == -1);
		/* a finger's margin: just above, below and beside the text */
		CHECK(ui_point_prompt_part(&prompt, 24, 500, 395) == 1);
		CHECK(ui_point_prompt_part(&prompt, 24, 500, 450) == 1);
		CHECK(ui_point_prompt_part(&prompt, 24, 360, 418) == 0);
		CHECK(ui_point_prompt_part(&prompt, 24, 620, 418) == 1);
		CHECK(ui_point_prompt_part(&prompt, 24, 500, 300) == -1);
	}
	/* a client's prompt has one part: B =quit only */
	{
		struct ui_point_prompt prompt = { 410, 428, 520, 600, 600 };

		CHECK(ui_point_prompt_part(&prompt, 24, 560, 418) == 0);
		CHECK(ui_point_prompt_part(&prompt, 24, 610, 418) == 0);
		CHECK(ui_point_prompt_part(&prompt, 24, 400, 418) == -1);
	}
	/* a prompt not drawn yet */
	{
		struct ui_point_prompt prompt = { 0, 0, 0, 0, 0 };

		CHECK(ui_point_prompt_part(&prompt, 24, 0, 0) == -1);
	}
	if (failures)
		printf("%d failures\n", failures);
	return failures ? 1 : 0;
}
