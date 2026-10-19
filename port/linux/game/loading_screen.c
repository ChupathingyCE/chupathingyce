/*
LOADING_SCREEN.C

The Xbox's loading screen (progress_bar.c), made by the CPU. The Xbox drew it
with Direct3D: each frame, the last frame (the front buffer, read through a
texture made from its address in this_is_awful) a little larger and dimmer,
and over it the loading picture (loading.tga, beside the maps) lit by a band
of glow that crosses the screen as the load goes on, fading in and out. The
port's Direct3D has no front buffer to read and no fixed function vertex
pipeline for those draws, so the same picture is made here, at the Xbox's
640 by 480, from the same textures, and put over the back buffer
(halo_draw_picture, port/linux/src/d3d8_gl.c).

The Xbox's loading screen stepped once each refresh (60 a second); so does
this one, however fast the frames come.

While the port holds the loading screen for display.loading_screen's
seconds (game.c), the menus' accept skips the rest of the wait
(loading_screen_skip_pressed).
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"

#include "input/input.h"

#include "loading_screen.h"

/* sdl_platform.c */
void platform_menus_set_active(int active);

/* ---------- constants */

#define LOADING_SCREEN_WIDTH 640
#define LOADING_SCREEN_HEIGHT 480
#define LOADING_SCREEN_STEP_MILLISECONDS 16

/* the last frame's quad: past the screen's edges by 1/128 each way, its
texture coordinates 3 to 637 across and 0 to 480 down (draw_fade_layer) */
#define LOADING_SCREEN_FADE_EDGE (1.0f + 1.0f / 128.0f)
#define LOADING_SCREEN_FADE_U0 3.0f
#define LOADING_SCREEN_FADE_U1 637.0f

/* ---------- globals */

static struct
{
	unsigned char frame[LOADING_SCREEN_HEIGHT][LOADING_SCREEN_WIDTH][4];
	unsigned char last[LOADING_SCREEN_HEIGHT][LOADING_SCREEN_WIDTH][4];
	unsigned long stepped_at;
	boolean stepped;
} loading_screen;

/* ---------- private code */

/* a sample's first texel and how far toward the next it is (texture
coordinates in texels, as the Xbox's linear textures take them) */
static void loading_screen_texel(
	float coordinate,
	long size,
	long *texel,
	float *fraction)
{
	float position = coordinate - 0.5f;
	long first;

	if (position < 0.0f)
		position = 0.0f;
	if (position > (float)(size - 1))
		position = (float)(size - 1);
	first = (long)position;
	if (first > size - 2)
		first = size > 1 ? size - 2 : 0;
	*texel = first;
	*fraction = size > 1 ? position - (float)first : 0.0f;
}

/* a channel of a texture, filtered as the Xbox's linear filter does */
static float loading_screen_sample(
	unsigned int const *pixels,
	long width,
	long height,
	float u,
	float v,
	int shift)
{
	long x, y;
	float fx, fy;
	unsigned int const *row0;
	unsigned int const *row1;
	float top, bottom;

	loading_screen_texel(u, width, &x, &fx);
	loading_screen_texel(v, height, &y, &fy);
	row0 = pixels + y * width;
	row1 = height > 1 ? row0 + width : row0;
#define CHANNEL(pixel) ((float)(((pixel) >> shift) & 0xff))
	top = CHANNEL(row0[x]) + (CHANNEL(row0[x + (width > 1)]) - CHANNEL(row0[x])) * fx;
	bottom = CHANNEL(row1[x]) + (CHANNEL(row1[x + (width > 1)]) - CHANNEL(row1[x])) * fx;
#undef CHANNEL

	return top + (bottom - top) * fy;
}

/* a channel of the last frame, filtered */
static float loading_screen_last(
	float u,
	float v,
	int channel)
{
	long x, y;
	float fx, fy;
	float top, bottom;

	loading_screen_texel(u, LOADING_SCREEN_WIDTH, &x, &fx);
	loading_screen_texel(v, LOADING_SCREEN_HEIGHT, &y, &fy);
	top = loading_screen.last[y][x][channel] +
		((float)loading_screen.last[y][x + 1][channel] - loading_screen.last[y][x][channel]) * fx;
	bottom = loading_screen.last[y + 1][x][channel] +
		((float)loading_screen.last[y + 1][x + 1][channel] - loading_screen.last[y + 1][x][channel]) * fx;

	return top + (bottom - top) * fy;
}

/* ---------- public code */

void loading_screen_reset(
	void)
{
	csmemset(loading_screen.frame, 0, sizeof(loading_screen.frame));
	loading_screen.stepped = FALSE;
}

boolean loading_screen_step_due(
	void)
{
	unsigned long now = system_milliseconds();

	if (loading_screen.stepped && now - loading_screen.stepped_at < LOADING_SCREEN_STEP_MILLISECONDS)
	{
		/* (the same picture again) */
		halo_draw_picture(&loading_screen.frame[0][0][0], LOADING_SCREEN_WIDTH, LOADING_SCREEN_HEIGHT);
		return FALSE;
	}
	loading_screen.stepped_at = now;
	loading_screen.stepped = TRUE;

	return TRUE;
}

void loading_screen_draw(
	float progress,
	float brightness,
	float fade_alpha,
	unsigned int const *picture,
	long picture_width,
	long picture_height,
	unsigned int const *mask,
	long mask_width,
	long mask_height)
{
	/* the glow's band: its texture's left edge this far across the 640
	(draw_layer_int's mask_position) */
	float mask_position = progress * 768.0f - 64.0f - 64.0f;
	long x, y;

	csmemcpy(loading_screen.last, loading_screen.frame, sizeof(loading_screen.last));
	if (fade_alpha < 0.0f)
		fade_alpha = 0.0f;
	for (y = 0; y < LOADING_SCREEN_HEIGHT; y++)
	{
		/* the screen's y, 1 at the top, -1 at the bottom */
		float screen_y = 1.0f - ((float)y + 0.5f) * (2.0f / LOADING_SCREEN_HEIGHT);
		float fade_v = (LOADING_SCREEN_FADE_EDGE - screen_y) / (2.0f * LOADING_SCREEN_FADE_EDGE) * LOADING_SCREEN_HEIGHT;
		float picture_v = ((float)y + 0.5f) * (float)picture_height / LOADING_SCREEN_HEIGHT;
		/* (the band's texture is 16 texels high, 0 at the bottom) */
		float mask_v = (1.0f - ((float)y + 0.5f) / LOADING_SCREEN_HEIGHT) * (float)mask_height;

		for (x = 0; x < LOADING_SCREEN_WIDTH; x++)
		{
			float screen_x = ((float)x + 0.5f) * (2.0f / LOADING_SCREEN_WIDTH) - 1.0f;
			float fade_u = LOADING_SCREEN_FADE_U0 + (screen_x + LOADING_SCREEN_FADE_EDGE) /
				(2.0f * LOADING_SCREEN_FADE_EDGE) * (LOADING_SCREEN_FADE_U1 - LOADING_SCREEN_FADE_U0);
			float picture_u = ((float)x + 0.5f) * (float)picture_width / LOADING_SCREEN_WIDTH;
			float mask_u = ((float)x + 0.5f) - mask_position;
			float glow = 5.0f / 255.0f;
			int channel;

			/* (outside the band, its texture's border: 5 of 255) */
			if (mask && mask_u >= 0.0f && mask_u < (float)mask_width)
				glow = loading_screen_sample(mask, mask_width, mask_height, mask_u, mask_v, 24) / 255.0f;
			for (channel = 0; channel < 3; channel++)
			{
				/* the last frame, dimmed; then the picture, added */
				float value = loading_screen_last(fade_u, fade_v, channel) * fade_alpha;

				if (picture && brightness > 0.0f)
					value += loading_screen_sample(picture, picture_width, picture_height, picture_u, picture_v,
						channel * 8) * brightness * glow;
				loading_screen.frame[y][x][channel] = (unsigned char)(value >= 255.0f ? 255 : value <= 0.0f ? 0 : (int)value);
			}
			loading_screen.frame[y][x][3] = 255;
		}
	}
	halo_draw_picture(&loading_screen.frame[0][0][0], LOADING_SCREEN_WIDTH, LOADING_SCREEN_HEIGHT);
}

/* the keys drive the first controller as they do in the menus, which the
loading screen stands in for (the menus set it again each frame once they
run); a press counts only as it starts, so the A or the key that started
the level, still held, does not skip it, nor does one held through a level's
end into the next's load */
boolean loading_screen_skip_pressed(
	void)
{
	short gamepad_index;

	platform_menus_set_active(TRUE);
	/* (the controllers are read as the main loop's frame begins) */
	input_frame_begin();
	input_update();
	input_frame_end();
	for (gamepad_index = 0; gamepad_index < MAXIMUM_GAMEPADS; gamepad_index++)
	{
		if (input_has_gamepad(gamepad_index) &&
			input_get_gamepad_state(gamepad_index)->buttons[_gamepad_analog_button_a] == 1)
		{
			return TRUE;
		}
	}

	return FALSE;
}
